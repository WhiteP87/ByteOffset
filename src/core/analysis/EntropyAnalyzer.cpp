
#include "EntropyAnalyzer.h"
#include "IDataProvider.h"

#include <cmath>
#include <cstring>
#include <limits>

//утилита ограничения значения в 64 битах
static inline qint64 clamp64(qint64 v, qint64 lo, qint64 hi) {
	return std::min(std::max(v, lo), hi);
}

//подбор цвета по энтропии и уверенности
QColor EntropyAnalyzer::colorForEntropy(float entropyH, float confidence, qreal baseAlpha) {
	float clampedH = std::clamp(entropyH, 0.f, 8.f);
	float norm = clampedH / 8.f;
	float hueDeg = (1.f - norm) * 240.f;
	float sat = 0.85f + 0.15f * norm;
	qreal alpha = std::clamp((baseAlpha <= 0 ? 0.40 : baseAlpha) * double(std::clamp(confidence, 0.f, 1.f)),
		0.0, 0.85);
	return QColor::fromHsvF(hueDeg / 360.0, sat, 1.0, alpha);
}

const EntropyAnalyzer::Config& EntropyAnalyzer::DefaultConfig() {
	static const Config cfg{}; //все дефолты применены
	return cfg;
}

//конструктор инициализирует провайдер настройки и пул потоков
EntropyAnalyzer::EntropyAnalyzer(std::shared_ptr<IDataProvider> provider,
	const Config& config, QObject* parent)
	: QObject(parent),
	m_provider(std::move(provider)),
	m_config(config) {
	m_pool.setMaxThreadCount(std::clamp(m_config.workers, 1, 8));
	if(m_provider) {
		m_revision.store(m_provider->dataVersion(), std::memory_order_release);
	}
	subscribeProvider();
}

//оформляет подписки на события провайдера
void EntropyAnalyzer::subscribeProvider() {
	if(!m_provider) {
		return;
	}
	m_subDataToken = m_provider->subscribeDataChanged(
		[this](quint64 newRevision, const QVector<ChangedSpan>& spans, bool modifiedWhole) {
			this->onDataChanged(newRevision, spans, modifiedWhole);
		}
	);
	m_subGeomToken = m_provider->subscribeGeometryChanged(
		[this](quint64 newRevision, const QVector<GeometryEvent>& events) {
			this->onGeometryChanged(newRevision, events);
		}
	);
}

//обрабатывает уведомление об изменении данных
void EntropyAnalyzer::onDataChanged(quint64 newRevision,
	const QVector<ChangedSpan>& spans,
	bool /*modifiedWhole*/) {

	m_revision.store(newRevision, std::memory_order_release);
	{
		QMutexLocker lock(&m_viewportMutex);
		m_lastSampleValid = false;
	}

	// Не стартуем пересчёт heatmap каждый тик — только планируем.
	scheduleHeatmapRecalc(spans, /*fullRebuild*/false);
}

//обрабатывает уведомление об изменении геометрии
void EntropyAnalyzer::onGeometryChanged(quint64 newRevision,
	const QVector<GeometryEvent>& /*events*/) {

	m_revision.store(newRevision, std::memory_order_release);
	{
		QMutexLocker lock(&m_viewportMutex);
		m_lastSampleValid = false;
	}

	// Геометрия -> нужен полный пересчёт.
	scheduleHeatmapRecalc({}, /*fullRebuild*/true);
}

//устанавливает размер окна первого уровня с ограничением снизу
void EntropyAnalyzer::setLevel1Window(qint32 bytes) {
	qint32 clamped = std::max(256, bytes);
	if(m_config.level1Window == clamped) {
		return;
	}
	m_config.level1Window = clamped;
	m_configEpoch.fetch_add(1, std::memory_order_acq_rel);
	{
		QMutexLocker lock(&m_viewportMutex);
		m_lastSampleValid = false;
	}
	emit viewportEntropyStarted(0, std::numeric_limits<qint64>::max());
	rebuildHeatmapPreservingVisualBins();
}

//устанавливает число рабочих потоков
void EntropyAnalyzer::setWorkers(int workers) {
	int clamped = std::clamp(workers, 1, 8);
	if(m_pool.maxThreadCount() == clamped) {
		return;
	}
	m_config.workers = clamped;
	m_pool.setMaxThreadCount(clamped);
}

//устанавливает базовую прозрачность цвета на теплокарте
void EntropyAnalyzer::setBaseAlpha(qreal alpha) {
	qreal clamped = std::clamp(alpha, 0.0, 0.85);
	if(std::abs(m_config.baseAlpha - clamped) < 1e-6) {
		return;
	}
	m_config.baseAlpha = clamped;
	emit viewportEntropyStarted(0, std::numeric_limits<qint64>::max());
}

//включает или отключает уточнение теплокарты
void EntropyAnalyzer::setRefineHeatMapEnabled(bool enabled) {
	m_config.heatmapRefineEnabled = enabled;
}

//вычисляет центрированное окно энтропии относительно байта центра
EntropySample EntropyAnalyzer::computeCenteredWindow(const uint8_t* batchPtr,
	qint64 batchBegin,
	qint64 fileSize,
	qint64 centerByte,
	qint32 nominalWindow) {
	qint64 start = centerByte - qint64(nominalWindow) / 2;
	qint64 end = start + nominalWindow;

	if(start < 0) {
		end += -start;
		start = 0;
	}
	if(end > fileSize) {
		start -= (end - fileSize);
		end = fileSize;
		if(start < 0) {
			start = 0;
		}
	}

	const qint64 len64 = end - start;

	EntropySample sample{};
	sample.offset = start;
	sample.window = int(std::max<qint64>(0, len64));
	if(sample.window > 0) {
		const uint8_t* ptr = batchPtr + int(start - batchBegin);
		sample.H = shannonEntropy(ptr, sample.window, nullptr);
		sample.confidence = nominalWindow > 0 ? float(sample.window) / float(nominalWindow) : 0.f;
	}
	return sample;
}

//расчитывает энтропию шеннона по буферу с опциональным подсчетом заполненных ведер
float EntropyAnalyzer::shannonEntropy(const uint8_t* data, int length, int* outNonEmptyBins) {
	if(length <= 0) {
		if(outNonEmptyBins) {
			*outNonEmptyBins = 0;
		}
		return 0.f;
	}

	uint32_t hist[256];
	std::memset(hist, 0, sizeof(hist));
	for(int index = 0; index < length; ++index) {
		++hist[data[index]];
	}

	if(outNonEmptyBins) {
		int nonEmpty = 0;
		float invN = 1.0f / float(length);
		float H = 0.f;
		for(int v = 0; v < 256; ++v) {
			uint32_t c = hist[v];
			if(!c) {
				continue;
			}
			++nonEmpty;
			float p = c * invN;
			H -= p * std::log2(p);
		}
		*outNonEmptyBins = nonEmpty;
		return H;
	}

	double sumCLogC = 0.0;
	for(int v = 0; v < 256; ++v) {
		uint32_t c = hist[v];
		if(!c) {
			continue;
		}
		sumCLogC += double(c) * std::log2(double(c));
	}
	double H = std::log2(double(length)) - (sumCLogC / double(length));
	return float(H);
}

//запрашивает асинхронную выборку энтропии для центра видимой области
void EntropyAnalyzer::requestViewportCenter(qint64 firstByte, qint64 lastByte) {
	if(!m_provider || firstByte >= lastByte || !m_enabled) {
		return;
	}
	emit viewportEntropyStarted(firstByte, lastByte);

	const quint64 epoch = m_configEpoch.load(std::memory_order_acquire);
	const qint64  fileSize = m_provider->size();
	const qint32  nominalW = std::max(256, m_config.level1Window);
	const qint64  center = firstByte / 2 + lastByte / 2 + (((firstByte & 1) && (lastByte & 1)) ? 1 : 0);

	auto job = [this, epoch, firstByte, lastByte, fileSize, nominalW, center]() {
		qint64 start = std::max<qint64>(0, center - qint64(nominalW) / 2);
		qint64 end = std::min<qint64>(fileSize, start + nominalW);
		if(end - start < nominalW) {
			start = std::max<qint64>(0, end - nominalW);
		}

		qint64 readBegin = start;
		qint64 readEnd = end;

		const qint64 maxBatch = std::max<qint32>(nominalW * 8, m_config.level1MaxBatchBytes);
		if(readEnd - readBegin > maxBatch) {
			const qint64 mid = firstByte / 2 + lastByte / 2;
			readBegin = std::max<qint64>(0, mid - maxBatch / 2);
			readEnd = std::min<qint64>(fileSize, readBegin + maxBatch);
		}

		QByteArray buf; buf.resize(int(readEnd - readBegin));
		if(buf.isEmpty()) {
			return;
		}
		const qint64 got = m_provider->readRange(readBegin, readEnd - readBegin, buf.data());
		if(got <= 0) {
			return;
		}
		if(epoch != m_configEpoch.load(std::memory_order_acquire)) {
			return;
		}

		const uint8_t* p = reinterpret_cast<const uint8_t*>(buf.constData());
		EntropySample s = computeCenteredWindow(p, readBegin, fileSize, center, nominalW);

		if(epoch != m_configEpoch.load(std::memory_order_acquire)) {
			return;
		}
		{
			QMutexLocker lk(&m_viewportMutex);
			m_lastSample.firstByte = firstByte;
			m_lastSample.lastByte = lastByte;
			m_lastSample.valid = (s.window > 0);
			m_lastSample.sample = s;
			m_lastSampleValid = m_lastSample.valid;
		}
		emit viewportEntropyReady(firstByte, lastByte);
		};

	(void)QtConcurrent::run(&m_pool, std::move(job));
}

//пытается синхронно получить последнюю экранную выборку
bool EntropyAnalyzer::tryGetViewportSample(ViewportSample& out) const {
	QMutexLocker lk(&m_viewportMutex);
	if(!m_lastSampleValid) {
		return false;
	}
	out = m_lastSample;
	return true;
}

//асинхронно запрашивает расчёт энтропии по диапазону с прогрессом
quint64 EntropyAnalyzer::requestRangeEntropyAsync(qint64 offset, qint64 length, qint64 chunkBytes) {
	if(!m_provider || offset < 0 || length <= 0) {
		return 0;
	}

	const quint64 jobId = m_jobCounter.fetch_add(1, std::memory_order_acq_rel);
	const quint64 epochSnapshot = m_configEpoch.load(std::memory_order_acquire);
	const qint64  fileSize = m_provider->size();
	const qint64  endOffset = std::min<qint64>(offset + length, fileSize);
	const qint64  totalBytes = std::max<qint64>(0, endOffset - offset);

	emit rangeEntropyStarted(jobId, offset, totalBytes);

	auto job = [this, jobId, epochSnapshot, offset, endOffset, totalBytes, chunkBytes]() {
		if(endOffset <= offset) {
			emit rangeEntropyReady(jobId, {});
			return;
		}

		QByteArray buffer; buffer.resize(int(std::min<qint64>(chunkBytes, endOffset - offset)));
		if(buffer.isEmpty()) {
			emit rangeEntropyError(jobId, QStringLiteral("empty buffer"));
			return;
		}

		uint32_t hist[256]; std::memset(hist, 0, sizeof(hist));
		qint64 processed = 0;
		qint64 position = offset;

		while(position < endOffset) {
			const qint64 toRead = std::min<qint64>(buffer.size(), endOffset - position);
			const qint64 got = m_provider->readRange(position, toRead, buffer.data());
			if(got < 0) {
				emit rangeEntropyError(jobId, QStringLiteral("provider read error"));
				return;
			}
			if(got == 0) {
				break;
			}

			const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer.constData());
			for(int i = 0; i < got; ++i) {
				++hist[ptr[i]];
			}

			position += got;
			processed += got;

			emit rangeEntropyProgress(jobId, processed, totalBytes);

			if(epochSnapshot != m_configEpoch.load(std::memory_order_acquire)) {
				return;
			}
		}

		EntropyStats stats{};
		stats.offset = offset;
		stats.length = totalBytes;
		stats.N = int(processed);

		int nonEmpty = 0;
		stats.H = shannonEntropy(nullptr, 0, nullptr);
		{
			float H = 0.f;
			if(processed > 0) {
				float invN = 1.0f / float(processed);
				for(int v = 0; v < 256; ++v) {
					uint32_t c = hist[v];
					if(!c) {
						continue;
					}
					++nonEmpty;
					float p = c * invN;
					H -= p * std::log2(p);
				}
			}
			stats.H = H;
		}
		stats.nonEmptyBins = nonEmpty;

		emit rangeEntropyReady(jobId, stats);
		};

	(void)QtConcurrent::run(&m_pool, std::move(job));
	return jobId;
}

//синхронно вычисляет энтропию по диапазону
EntropyStats EntropyAnalyzer::computeRangeEntropy(qint64 offset, qint64 length, qint64 chunkBytes) const {
	EntropyStats stats{}; stats.offset = offset; stats.length = length; stats.N = 0; stats.nonEmptyBins = 0; stats.H = 0.f;
	if(!m_provider || offset < 0 || length <= 0) {
		return stats;
	}

	const qint64 fileSize = m_provider->size();
	const qint64 endOffset = std::min<qint64>(offset + length, fileSize);
	if(endOffset <= offset) {
		return stats;
	}

	QByteArray buffer; buffer.resize(int(std::min<qint64>(chunkBytes, endOffset - offset)));
	uint32_t hist[256]; std::memset(hist, 0, sizeof(hist));

	for(qint64 position = offset; position < endOffset; ) {
		const qint64 toRead = std::min<qint64>(buffer.size(), endOffset - position);
		const qint64 got = m_provider->readRange(position, toRead, buffer.data());
		if(got <= 0) {
			break;
		}

		const uint8_t* ptr = reinterpret_cast<const uint8_t*>(buffer.constData());
		for(int i = 0; i < got; ++i) {
			++hist[ptr[i]];
		}

		position += got;
		stats.N += int(got);
	}

	int nonEmpty = 0;
	if(stats.N > 0) {
		float invN = 1.0f / float(stats.N);
		float H = 0.f;
		for(int v = 0; v < 256; ++v) {
			uint32_t c = hist[v];
			if(!c) {
				continue;
			}
			++nonEmpty;
			float p = c * invN;
			H -= p * std::log2(p);
		}
		stats.H = H;
	}
	stats.nonEmptyBins = nonEmpty;
	return stats;
}

//начинает новый эпохальный цикл теплокарты
quint64 EntropyAnalyzer::beginHeatmapEpoch() {
	return m_heatmapEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
}

//проверяет устаревание задания теплокарты
bool EntropyAnalyzer::isHeatmapObsolete(quint64 jobEpoch) const {
	return jobEpoch != m_heatmapEpoch.load(std::memory_order_acquire);
}

//подготавливает геометрию теплокарты с равномерным разбиением
void EntropyAnalyzer::prepareHeatmapGeometry(qint32 visualBins, qint32 nominalWindowBytes, qint64 fileSizeBytes) {
	QVector<HeatmapBin> prepared; prepared.resize(visualBins);

	for(qint32 binIndex = 0; binIndex < visualBins; ++binIndex) {
		const qint64 spanLeft = qint64(std::floor((double(binIndex) / double(visualBins)) * double(fileSizeBytes)));
		const qint64 spanRight = qint64(std::floor((double(binIndex + 1) / double(visualBins)) * double(fileSizeBytes)));
		const qint64 spanWidth = std::max<qint64>(0, spanRight - spanLeft);

		qint64 left = 0;
		qint32 window = 0;

		if(spanWidth >= nominalWindowBytes) {
			left = spanLeft + (spanWidth - nominalWindowBytes) / 2;
			window = nominalWindowBytes;
		} else {
			left = clamp64(fileSizeBytes - nominalWindowBytes, 0, std::max<qint64>(0, fileSizeBytes));
			window = int(std::min<qint64>(nominalWindowBytes, std::max<qint64>(0, fileSizeBytes - left)));
		}

		HeatmapBin bin{};
		bin.centerOffset = left + window / 2;
		bin.windowBytes = window;
		bin.entropyH = 0.f;
		bin.confidence = 0.f;
		prepared[binIndex] = bin;
	}

	QMutexLocker locker(&m_heatmapMutex);
	m_heatmapBins.swap(prepared);
	m_heatmapVisualBins = visualBins;
}

//выполняет единый проход обновления теплокарты по списку индексов
void EntropyAnalyzer::updateHeatmap(quint64 jobEpoch,
	qint64 fileSize,
	qint32 visualBins,
	qint32 nominalWindowBytes,
	int sampleCount,
	const QVector<int>* touchedBinIndices) {
	if(isHeatmapObsolete(jobEpoch)) {
		return;
	}

	QVector<int> indices;
	if(touchedBinIndices && !touchedBinIndices->isEmpty()) {
		indices = *touchedBinIndices;
	} else {
		indices.reserve(visualBins);
		for(int i = 0; i < visualBins; ++i) {
			indices.push_back(i);
		}
	}

	QByteArray buffer; buffer.resize(nominalWindowBytes);

	struct Pair { int idx; HeatmapBin bin; };
	QVector<Pair> updates; updates.reserve(std::min(visualBins, 512));

	auto applyBatch = [this, jobEpoch, &updates]() {
		if(updates.isEmpty()) {
			return;
		}
		int first = updates.first().idx;
		int last = updates.last().idx;

		{
			QMutexLocker lk(&m_heatmapMutex);
			if(isHeatmapObsolete(jobEpoch)) {
				return;
			}
			const qint64 n = m_heatmapBins.size();
			for(const auto& p : updates) {
				if(p.idx >= 0 && p.idx < n) {
					m_heatmapBins[p.idx] = p.bin;
				}
			}
			first = clamp64(first, 0, n > 0 ? n - 1 : 0);
			last = clamp64(last, 0, n > 0 ? n - 1 : 0);
		}
		if(first <= last) {
			emit heatmapRangeUpdated(first, last);
		}
		updates.clear();
		};

	for(int idx : indices) {
		if(isHeatmapObsolete(jobEpoch)) {
			return;
		}

		qint32 binWindow = 0;
		{
			QMutexLocker lk(&m_heatmapMutex);
			const qint64 n = m_heatmapBins.size();
			if(idx < 0 || idx >= n) {
				continue;
			}
			binWindow = m_heatmapBins[idx].windowBytes;
		}
		if(binWindow <= 0) {
			continue;
		}

		const qint64 spanLeft = qint64(std::floor((double(idx) / double(visualBins)) * double(fileSize)));
		const qint64 spanRight = qint64(std::floor((double(idx + 1) / double(visualBins)) * double(fileSize)));

		qint64 allowedLeftMin = 0, allowedLeftMax = 0;
		if((spanRight - spanLeft) >= binWindow) {
			allowedLeftMin = spanLeft;
			allowedLeftMax = spanRight - binWindow;
		} else {
			const qint64 maxLeft = std::max<qint64>(0, fileSize - binWindow);
			allowedLeftMin = clamp64(spanRight - binWindow, 0, maxLeft);
			allowedLeftMax = clamp64(spanLeft, 0, maxLeft);
			if(allowedLeftMin > allowedLeftMax) {
				std::swap(allowedLeftMin, allowedLeftMax);
			}
		}

		const qint64 sweepRange = std::max<qint64>(0, allowedLeftMax - allowedLeftMin);
		const int    samples = std::max(1, sampleCount);
		const qint64 sweepSteps = std::max<qint64>(1, samples - 1);

		double entropySum = 0.0;
		double confSum = 0.0;
		int    accepted = 0;
		qint64 lastLeft = -1;

		for(int s = 0; s < samples; ++s) {
			if(isHeatmapObsolete(jobEpoch)) {
				return;
			}
			const qint64 left = allowedLeftMin + (sweepSteps
				? qint64(std::llround(double(sweepRange) * double(s) / double(sweepSteps)))
				: 0);

			if(left == lastLeft) {
				continue;
			}
			lastLeft = left;

			const qint64 bytesReq = std::min<qint64>(binWindow, fileSize - left);
			if(bytesReq <= 0) {
				continue;
			}
			if(buffer.size() < bytesReq) {
				buffer.resize(int(bytesReq));
			}

			const qint64 got = m_provider->readRange(left, bytesReq, buffer.data());
			if(got <= 0) {
				continue;
			}

			const float H = shannonEntropy(reinterpret_cast<const uint8_t*>(buffer.constData()),
				int(got), nullptr);
			entropySum += double(H);
			confSum += (nominalWindowBytes > 0)
				? (double(got) / double(nominalWindowBytes))
				: 0.0;
			++accepted;
		}

		if(accepted > 0) {
			HeatmapBin ready{};
			ready.centerOffset = allowedLeftMin + (sweepRange / 2) + binWindow / 2;
			ready.windowBytes = binWindow;
			ready.entropyH = float(entropySum / double(accepted));
			ready.confidence = float(confSum / double(accepted));

			updates.push_back({idx, ready});
			if(updates.size() >= 64) {
				applyBatch();
			}
		}
	}

	applyBatch();
}

//инициализирует теплокарту и запускает расчёт
void EntropyAnalyzer::initHeatmap(qint32 desiredVisualBins) {
	if(!m_provider || !m_enabled) {
		return;
	}
	const qint64 fileSize = m_provider->size();
	if(fileSize <= 0) {
		return;
	}

	const quint64 jobEpoch = beginHeatmapEpoch();
	const qint32  visualBins = std::clamp<qint32>((desiredVisualBins > 0 ? desiredVisualBins : 1024), 256, 4096);
	const qint32  nominalWindow = std::max<qint32>(256, m_config.level1Window);

	prepareHeatmapGeometry(visualBins, nominalWindow, fileSize);

	(void)QtConcurrent::run(&m_pool, [this, jobEpoch, fileSize, visualBins, nominalWindow]() {
		updateHeatmap(jobEpoch, fileSize, visualBins, nominalWindow, /*samples*/1, /*touched*/nullptr);
		if(!isHeatmapObsolete(jobEpoch) && m_config.heatmapRefineEnabled) {
			const int samples = std::clamp<int>(m_config.heatmapRefineSamples, 2, 9);
			updateHeatmap(jobEpoch, fileSize, visualBins, nominalWindow, samples, nullptr);
		}
		});
}

//пересчитывает теплокарту для измененных диапазонов
void EntropyAnalyzer::recalcHeatmapForSpans(const QVector<ChangedSpan>& changedSpans) {
	if(!m_provider || changedSpans.isEmpty()) {
		return;
	}

	const qint64 fileSize = m_provider->size();
	qint32 binsCount = 0;
	{
		QMutexLocker locker(&m_heatmapMutex);
		binsCount = m_heatmapBins.size();
	}
	if(fileSize <= 0 || binsCount <= 0) {
		return;
	}

	const qint32 nominalWindow = std::max<qint32>(256, m_config.level1Window);
	const quint64 jobEpoch = m_heatmapEpoch.load(std::memory_order_acquire);
	const QVector<int> touched = computeTouchedIndices(changedSpans, fileSize, binsCount, nominalWindow);

	if(touched.isEmpty()) {
		return;
	}

	(void)QtConcurrent::run(&m_pool, [this, jobEpoch, fileSize, binsCount, nominalWindow, touched]() {
		updateHeatmap(jobEpoch, fileSize, binsCount, nominalWindow, /*samples*/1, &touched);
		if(!isHeatmapObsolete(jobEpoch) && m_config.heatmapRefineEnabled) {
			const int samples = std::clamp<int>(m_config.heatmapRefineSamples, 2, 9);
			updateHeatmap(jobEpoch, fileSize, binsCount, nominalWindow, samples, &touched);
		}
		});
}

//определяет индексы бинов теплокарты затронутые изменениями
QVector<int> EntropyAnalyzer::computeTouchedIndices(const QVector<ChangedSpan>& changedSpans,
	qint64 fileSize,
	qint32 binsCount,
	qint32 nominalWindow) const {
	QVector<int> result; result.reserve(std::min(512, binsCount));

	auto binCenterByIndex = [binsCount, fileSize](qint32 binIndex)->qint64 {
		const qreal pos01 = (binsCount == 1) ? 0.5 : ((qreal(binIndex) + 0.5) / qreal(binsCount));
		return std::clamp<qint64>(qint64(pos01 * qreal(fileSize)), 0, std::max<qint64>(0, fileSize - 1));
		};
	auto windowLeftForCenter = [fileSize, nominalWindow](qint64 centerByte)->qint64 {
		qint64 left = centerByte - nominalWindow / 2;
		if(left < 0) {
			left = 0;
		}
		if(left + nominalWindow > fileSize) {
			left = std::max<qint64>(0, fileSize - nominalWindow);
		}
		return left;
		};

	QBitArray mark(binsCount, false);

	for(const ChangedSpan& span : changedSpans) {
		const qint64 spanLeft = span.m_offset;
		const qint64 spanRight = span.m_offset + span.m_newLen;

		const qint64 effLeft = std::max<qint64>(0, spanLeft - nominalWindow / 2);
		const qint64 effRight = std::min<qint64>(fileSize, spanRight + nominalWindow / 2);

		const qint32 approxFirst = std::clamp<qint32>(qint32((qreal(effLeft) / qreal(fileSize)) * binsCount) - 2, 0, binsCount - 1);
		const qint32 approxLast = std::clamp<qint32>(qint32((qreal(effRight) / qreal(fileSize)) * binsCount) + 2, 0, binsCount - 1);

		for(qint32 binIndex = approxFirst; binIndex <= approxLast; ++binIndex) {
			const qint64 center = binCenterByIndex(binIndex);
			const qint64 left = windowLeftForCenter(center);
			const qint64 right = left + nominalWindow;
			if(!(right <= effLeft || left >= effRight)) {
				mark.setBit(binIndex, true);
			}
		}
	}

	for(int i = 0; i < binsCount; ++i) {
		if(mark.testBit(i)) {
			result.push_back(i);
		}
	}
	return result;
}

//перестраивает теплокарту сохраняя текущее число бинов
void EntropyAnalyzer::rebuildHeatmapPreservingVisualBins() {
	const qint32 keep = (m_heatmapVisualBins > 0 ? m_heatmapVisualBins : 1024);
	initHeatmap(keep);
}

//возвращает количество бинов теплокарты
qint64 EntropyAnalyzer::heatmapBinCount() const {
	QMutexLocker lock(&m_heatmapMutex);
	return m_heatmapBins.size();
}

//возвращает цвет бина по индексу или цвет неготовности
QColor EntropyAnalyzer::heatmapColorByIndex(qint64 binIndex, qreal baseAlpha) const {
	QMutexLocker lock(&m_heatmapMutex);
	const qint64 n = m_heatmapBins.size();
	if(binIndex < 0 || binIndex >= n) {
		return m_heatmapNotReadyColor;
	}
	const HeatmapBin& bin = m_heatmapBins[int(binIndex)];
	if(bin.windowBytes <= 0 || bin.confidence <= 0.0f) {
		return m_heatmapNotReadyColor;
	}
	return EntropyAnalyzer::colorForEntropy(bin.entropyH, bin.confidence, baseAlpha);
}

//возвращает логический центр окна бина
qint64 EntropyAnalyzer::heatmapBinCenterOffset(qint64 binIndex) const {
	QMutexLocker lock(&m_heatmapMutex);
	if(binIndex < 0 || binIndex >= m_heatmapBins.size()) {
		return 0;
	}
	return m_heatmapBins[int(binIndex)].centerOffset;
}

//останавливает текущие задания теплокарты
void EntropyAnalyzer::stopHeatmap() {
	beginHeatmapEpoch();
}

//останавливает все активные расчеты и сбрасывает экранную выборку
void EntropyAnalyzer::stopAllCalculations() {
	beginHeatmapEpoch();
	m_configEpoch.fetch_add(1, std::memory_order_acq_rel);
	{
		QMutexLocker lock(&m_viewportMutex);
		m_lastSampleValid = false;
	}
}

void EntropyAnalyzer::scheduleHeatmapRecalc(const QVector<ChangedSpan>& spans, bool fullRebuild) {
	if(!m_provider || !m_enabled)
		return;

	if(!m_heatmapTimerInited) {
		m_heatmapTimerInited = true;
		m_heatmapTimer.setSingleShot(true);
		m_heatmapTimer.setInterval(m_heatmapDebounceMs);
		connect(&m_heatmapTimer, &QTimer::timeout,
			this, &EntropyAnalyzer::flushHeatmapRecalc);
	}

	// накапливаем
	m_heatmapPending = true;
	if(fullRebuild)
		m_heatmapPendingFullRebuild = true;
	if(!spans.isEmpty())
		m_heatmapPendingSpans += spans;

	// trailing debounce
	m_heatmapTimer.start();

	// max-wait throttle: при непрерывном key-repeat debounce может не сработать никогда
	if(!m_heatmapMaxWait.isValid())
		m_heatmapMaxWait.start();

	if(m_heatmapMaxWait.elapsed() >= m_heatmapMaxWaitMs) {
		flushHeatmapRecalc();
		m_heatmapMaxWait.restart();
	}
}

void EntropyAnalyzer::flushHeatmapRecalc() {
	if(!m_heatmapPending || !m_provider || !m_enabled)
		return;

	m_heatmapPending = false;

	const bool full = m_heatmapPendingFullRebuild;
	m_heatmapPendingFullRebuild = false;

	QVector<ChangedSpan> spans;
	spans.swap(m_heatmapPendingSpans);

	// если UI показывает "busy" по viewportEntropyStarted — эмитим не каждый тик, а только на flush
	emit viewportEntropyStarted(0, std::numeric_limits<qint64>::max());

	if(full) {
		rebuildHeatmapPreservingVisualBins();
		return;
	}

	if(!spans.isEmpty())
		recalcHeatmapForSpans(spans);
}