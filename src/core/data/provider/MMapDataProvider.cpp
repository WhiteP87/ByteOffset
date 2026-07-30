#include "MMapDataProvider.h"
#include <QSaveFile>
#include "IBinaryConfig.h"
#include "OverlayChangesWriter.h"
#include "OverlayChangesReader.h"
#include "UnifiedProjectReader.h"
#include "PagedPieceOverlay.h"


MMapDataProvider::MMapDataProvider(std::unique_ptr<ISrcReader> srcReader,
	std::unique_ptr<IOverlay>   overlay)
	: m_srcReader(std::move(srcReader))
	, m_overlay(std::move(overlay)) {
}

MMapDataProvider::~MMapDataProvider() = default;

qint64 MMapDataProvider::size() const {
	QReadLocker guard(&m_rwLock);
	return m_overlay ? m_overlay->logicalSize() : 0;
}

quint8 MMapDataProvider::readByte(qint64 off) const {
	QReadLocker guard(&m_rwLock);
	return m_overlay ? m_overlay->readByte(off) : 0;
}

qint64 MMapDataProvider::readRange(qint64 off, qint64 len, QByteArray& out) const {
	QReadLocker guard(&m_rwLock);
	if(!m_overlay){ 
		out.clear();
		return 0; 
	}
	return m_overlay->readRange(off, len, out);
}

qint64 MMapDataProvider::readRange(qint64 off, qint64 len, char* destination) const {
	QReadLocker guard(&m_rwLock);
	if(!m_overlay) 
		return 0;

	return m_overlay->readRange(off, len, destination);
}

qint64 MMapDataProvider::readRange(qint64 off, qint64 len, char* destination, quint64* outVersion) const {
	QReadLocker guard(&m_rwLock);
	if(!m_overlay) 
		return 0;

	const qint64 n = m_overlay->readRange(off, len, destination);
	if(outVersion) 
		*outVersion = m_version.load(std::memory_order_acquire);

	return n;
}

bool MMapDataProvider::writeByte(qint64 off, quint8 value, quint8 mask) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay) 
		return false;

	if(!m_overlay->overwriteByte(off, value, mask))
		return false;

	QVector<ChangedSpan> spans;
	spans.push_back(ChangedSpan{off, 1, 1});

	const quint64 newVersion = ++m_version;
	const bool modifiedWhole = m_overlay ? m_overlay->hasAnyModification() : false;
	guard.unlock();
	notifyDataChanged(spans, newVersion, modifiedWhole);
	return true;
}

bool MMapDataProvider::writeByte(qint64 off, quint8 value, int nibbleIndex) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay) 
		return false;

	quint8 mask = 0xFF;
	quint8 alignedVal = value;

	if(nibbleIndex == 0) {
		mask = 0xF0;
		alignedVal = quint8((value & 0x0F) << 4);
	} else if(nibbleIndex == 1) {
		mask = 0x0F;
		alignedVal = quint8(value & 0x0F);
	} else if(nibbleIndex != -1) {
		return false;
	}

	if(!m_overlay->overwriteByte(off, alignedVal, mask))
		return false;

	QVector<ChangedSpan> spans;
	spans.push_back(ChangedSpan{off, 1, 1});

	const quint64 newVersion = ++m_version;
	const bool modifiedWhole = m_overlay ? m_overlay->hasAnyModification() : false;
	guard.unlock();
	notifyDataChanged(spans, newVersion, modifiedWhole);
	return true;
}

qint64 MMapDataProvider::writeRange(qint64 off, QByteArrayView data) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay) 
		return 0;

	const qint64 n = m_overlay->overwriteRange(off, data);
	if(n <= 0) 
		return 0;

	QVector<ChangedSpan> spans;
	spans.push_back(ChangedSpan{off, n, n});

	const quint64 newVersion = ++m_version;
	const bool modifiedWhole = m_overlay ? m_overlay->hasAnyModification() : false;
	guard.unlock();
	notifyDataChanged(spans, newVersion, modifiedWhole);
	return n;
}

qint64 MMapDataProvider::insert(qint64 off, QByteArrayView data) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay) return 0;

	const qint64 n = m_overlay->insertRange(off, data);
	if(n <= 0) return 0;

	const qint64 newSz = m_overlay->logicalSize();
	QVector<GeometryEvent> ev{
		GeometryEvent{ GeometryKind::Insert, off, n, newSz }
	};

	const quint64 newVersion = ++m_version;
	const bool modifiedWhole = m_overlay ? m_overlay->hasAnyModification() : false;
	guard.unlock();
	notifyGeometryChanged(ev, newVersion);
	notifyDataChanged(QVector<ChangedSpan>{}, newVersion, modifiedWhole);
	return n;
}

qint64 MMapDataProvider::remove(qint64 off, qint64 len) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay || len <= 0) 
		return 0;

	const qint64 n = m_overlay->removeRange(off, len);
	if(n <= 0) 
		return 0;

	const qint64 newSz = m_overlay->logicalSize();
	QVector<GeometryEvent> ev{
		GeometryEvent{ GeometryKind::Remove, off, n, newSz }
	};

	const quint64 newVersion = ++m_version;
	const bool modifiedWhole = m_overlay ? m_overlay->hasAnyModification() : false;
	guard.unlock();
	notifyGeometryChanged(ev, newVersion);
	notifyDataChanged(QVector<ChangedSpan>{}, newVersion, modifiedWhole);
	return n;
}

qint64 MMapDataProvider::fillRange(qint64 off, qint64 len, QByteArrayView pattern) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay || len <= 0 || pattern.isEmpty()) 
		return 0;

	const qint64 n = m_overlay->fillRange(off, len, pattern);
	if(n <= 0) 
		return 0;

	QVector<ChangedSpan> spans;
	spans.push_back(ChangedSpan{off, n, n});

	const quint64 newVersion = ++m_version;
	const bool modifiedWhole = m_overlay ? m_overlay->hasAnyModification() : false;
	guard.unlock();
	notifyDataChanged(spans, newVersion, modifiedWhole);
	return n;
}

bool MMapDataProvider::isModified(qint64 offset) const {
	QReadLocker guard(&m_rwLock);
	return m_overlay ? m_overlay->isModified(offset) : false;
}

bool MMapDataProvider::isDataModified() const {
	return m_overlay?m_overlay->hasAnyModification():false;
}

quint64 MMapDataProvider::dataVersion() const {
	return m_version.load(std::memory_order_acquire);
}

DataChangeToken MMapDataProvider::subscribeDataChanged(DataChangedCallback callback) {
	if(!callback) 
		return 0;
	QMutexLocker lock(&m_subscribersMutex);
	const DataChangeToken token = ++m_nextToken;
	m_subscribers.insert(token, std::move(callback));
	return token;
}

void MMapDataProvider::unsubscribeDataChanged(DataChangeToken token) {
	if(token == 0) 
		return;
	QMutexLocker lock(&m_subscribersMutex);
	m_subscribers.remove(token);
}

DataChangeToken MMapDataProvider::subscribeGeometryChanged(GeometryChangedCallback cb) {
	QWriteLocker lk(&m_geomCbLock);
	const auto tok = m_nextGeomToken++;
	m_geomSubs.insert(tok, std::move(cb));
	return tok;
}

void MMapDataProvider::unsubscribeGeometryChanged(DataChangeToken token) {
	QWriteLocker lk(&m_geomCbLock);
	m_geomSubs.remove(token);
}

void MMapDataProvider::notifyDataChanged(const QVector<ChangedSpan>& spans, quint64 newVersion, bool modifiedWhole) {
	QHash<DataChangeToken, DataChangedCallback> local;
	{
		QMutexLocker lock(&m_subscribersMutex);
		local = m_subscribers;
	}
	for(auto it = local.constBegin(); it != local.constEnd(); ++it) {
		if(it.value()) 
			it.value()(newVersion, spans, modifiedWhole);
	}
}

void MMapDataProvider::notifyGeometryChanged(const QVector<GeometryEvent>& ev, quint64 ver) {
	QReadLocker lk(&m_geomCbLock);
	for(auto it = m_geomSubs.cbegin(); it != m_geomSubs.cend(); ++it) {
		it.value()(ver, ev);
	}
}


void MMapDataProvider::reopenFile(const QString& path, const PagedPieceOverlay::Config& cfg, qint64 mapWindowSize) {
	auto newReader = std::make_unique<MMapSrcReader>(path, mapWindowSize);
	auto newOverl = std::make_unique<PagedPieceOverlay>(*newReader, cfg);
	m_srcReader = std::move(newReader);
	m_overlay = std::move(newOverl);
	m_version.store(0, std::memory_order_release);
}

SaveStatus MMapDataProvider::saveInPlaceSameTarget(QString* errorMessage) {
	if(!m_srcReader || !m_srcReader->writable()) {
		if(errorMessage) 
			*errorMessage = QStringLiteral("файл доступен только для чтения");
		return SaveStatus::IoError;
	}

	//параметры для переоткрытия до операций
	const auto* po = dynamic_cast<const PagedPieceOverlay*>(m_overlay.get());
	PagedPieceOverlay::Config ocfg{};
	if(po) 
		ocfg = po->config(); // иначе оставить дефолт
	qint64 winSize = 0;
	if(auto* mm = dynamic_cast<MMapSrcReader*>(m_srcReader.get())) {
		winSize = mm->windowSize();
		mm->invalidateAllWindows();
	}

	QFile f(m_srcReader->id());
	if(!f.open(QIODevice::ReadWrite)) {
		if(errorMessage) 
			*errorMessage = QStringLiteral("не открыть файл для записи: %1").arg(f.errorString());
		return SaveStatus::IoError;
	}

	const auto spans = m_overlay->enumerateOverwriteSpans(); // QVector<OverwriteSpan{off,len}>
	QByteArray buf;
	for(const auto& s : spans) {
		if(s.len <= 0) 
			continue;
		buf.resize(int(s.len));

		const qint64 n = m_overlay->readRange(s.off, s.len, buf.data());
		if(n != s.len) {
			if(errorMessage) *errorMessage =
				QStringLiteral("ошибка чтения данных для записи (off=%1, len=%2)").arg(s.off).arg(s.len);
			f.close();
			return SaveStatus::IoError;
		}
		if(!f.seek(s.off)) {
			if(errorMessage) 
				*errorMessage = QStringLiteral("seek(%1) не удался").arg(s.off);
			f.close();
			return SaveStatus::IoError;
		}
		qint64 written = 0;
		while(written < n) {
			const qint64 w = f.write(buf.constData() + written, n - written);
			if(w <= 0) {
				const auto es = f.errorString();
				if(errorMessage) 
					*errorMessage = QStringLiteral("ошибка записи (off=%1, len=%2): %3").arg(s.off).arg(n).arg(es);
				f.close();
				return SaveStatus::IoError;
			}
			written += w;
		}
	}
	f.flush();
	f.close();

	reopenFile(m_srcReader->id(), ocfg, winSize);
	return SaveStatus::Ok;
}

SaveStatus MMapDataProvider::saveByReplaceSameTarget(QString* errorMessage) {
	//параметры для переоткрытия
	const QString srcPath = m_srcReader ? m_srcReader->id() : QString();
	const auto* po = dynamic_cast<const PagedPieceOverlay*>(m_overlay.get());
	PagedPieceOverlay::Config ocfg{};
	if(po) 
		ocfg = po->config();

	qint64 winSize = 0;
	if(auto* mm = dynamic_cast<MMapSrcReader*>(m_srcReader.get()))
		winSize = mm->windowSize();

	//запись в QSaveFile 
	QSaveFile out(srcPath);
	if(!out.open(QIODevice::WriteOnly)) {
		if(errorMessage) *errorMessage =
			QStringLiteral("Не удалось открыть целевой файл для записи: %1").arg(out.errorString());
		return SaveStatus::IoError;
	}

	constexpr qint64 CHUNK = 256ll << 20;
	const qint64 total = m_overlay->logicalSize();

	QByteArray buffer; 
	buffer.resize(qsizetype(std::min<qint64>(CHUNK, total)));

	qint64 offset = 0;
	while(offset < total) {
		const qint64 take = std::min<qint64>(qint64(buffer.size()), total - offset);
		const qint64 nread = m_overlay->readRange(offset, take, buffer.data());
		if(nread <= 0) {
			if(errorMessage) 
				*errorMessage = QStringLiteral("ошибка чтения логических данных");
			return SaveStatus::IoError;
		}
		qint64 written = 0;
		while(written < nread) {
			const qint64 n = out.write(buffer.constData() + written, nread - written);
			if(n <= 0) {
				if(errorMessage) 
					*errorMessage = QStringLiteral("ошибка записи во временный файл: %1").arg(out.errorString());
				return SaveStatus::IoError;
			}
			written += n;
		}
		offset += nread;
	}

	//отпустить исходник перед commit
	if(auto* mm = dynamic_cast<MMapSrcReader*>(m_srcReader.get()))
		mm->invalidateAllWindows();
	m_overlay.reset();
	m_srcReader.reset();

	//commit
	if(!out.commit()) {
		if(errorMessage) *errorMessage =
			QStringLiteral("не удалось заменить исходный файл: %1").arg(out.errorString());

		//переоткрытие файла
		auto reopen = std::make_unique<MMapSrcReader>(srcPath, winSize);
		m_srcReader = std::move(reopen);
		m_overlay = std::make_unique<PagedPieceOverlay>(*m_srcReader, ocfg);
		return SaveStatus::IoError;
	}

	reopenFile(srcPath, ocfg, winSize);

	return SaveStatus::Ok;
}

SaveStatus MMapDataProvider::saveAsToPath(const QString& dstPath, QString* errorMessage) {
	const auto* po = dynamic_cast<const PagedPieceOverlay*>(m_overlay.get());
	PagedPieceOverlay::Config ocfg{};
	if(po) 
		ocfg = po->config();
	qint64 winSize = 0;
	if(auto* mm = dynamic_cast<MMapSrcReader*>(m_srcReader.get()))
		winSize = mm->windowSize();

	QSaveFile out(dstPath);
	if(!out.open(QIODevice::WriteOnly)) {
		if(errorMessage) 
			*errorMessage = QStringLiteral("ошибка открытия целевого файла: %1").arg(out.errorString());
		return SaveStatus::IoError;
	}

	constexpr qint64 CHUNK = 256ll << 20;
	const qint64 total = m_overlay->logicalSize();
	QByteArray buffer; buffer.resize(qsizetype(std::min<qint64>(CHUNK, total)));

	qint64 offset = 0;
	while(offset < total) {
		const qint64 take = std::min<qint64>(qint64(buffer.size()), total - offset);
		const qint64 nread = m_overlay->readRange(offset, take, buffer.data());
		if(nread <= 0) {
			if(errorMessage) 
				*errorMessage = QStringLiteral("ошибка чтения логических данных");
			return SaveStatus::IoError;
		}
		qint64 written = 0;
		while(written < nread) {
			const qint64 n = out.write(buffer.constData() + written, nread - written);
			if(n <= 0) {
				if(errorMessage) *errorMessage =
					QStringLiteral("ошибка записи в целевой файл: %1").arg(out.errorString());
				return SaveStatus::IoError;
			}
			written += n;
		}
		offset += nread;
	}
	if(!out.commit()) {
		if(errorMessage) *errorMessage = QStringLiteral("ошибка завершения записи: %1").arg(out.errorString());
		return SaveStatus::IoError;
	}

	//переключиться на новый файл
	reopenFile(dstPath, ocfg, winSize);

	return SaveStatus::Ok;
}


SaveStatus MMapDataProvider::saveToFile(const QString& filePath, QString* errorMessage) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay) {
		if(errorMessage) 
			*errorMessage = QStringLiteral("нет данных для сохранения");
		return SaveStatus::NoSource;
	}

	auto norm = [](const QString& p)->QString {
		QFileInfo fi(p);
		return fi.exists() ? fi.canonicalFilePath() : fi.absoluteFilePath();
		};

	const QString srcPath = m_srcReader ? m_srcReader->id() : QString();
	const bool    haveSrc = !srcPath.isEmpty();
	const bool    samePath = haveSrc && norm(filePath) == norm(srcPath);

	if(samePath) {
		if(!m_overlay->hasStructuralChanges()) {
			const auto st = saveInPlaceSameTarget(errorMessage);
			if(st == SaveStatus::Ok) 
				return st;
		}
		return saveByReplaceSameTarget(errorMessage);
	}

	return saveAsToPath(filePath, errorMessage);
}

std::optional<SourceMeta> MMapDataProvider::sourceMeta() const {
	QReadLocker guard(&m_rwLock);
	if(!m_srcReader) 
		return std::nullopt;
	SourceMeta srcMeta;
	srcMeta.id = m_srcReader->id();
	srcMeta.writable = m_srcReader->writable();
	srcMeta.baseSize = m_srcReader->size();
	srcMeta.hashVal = m_srcReader->getHash();
	return srcMeta;
}

BinaryConfigDescriptor MMapDataProvider::configDescriptor() const {
	return BinaryConfigDescriptor{
		QByteArray("hexeditor/patches/v1"), //уникальный utf-8 тег
		quint16(1),
		10
	};
}

bool MMapDataProvider::exportBinaryConfig(QByteArray& outPayload, QString* errorText) const {
	QReadLocker guard(&m_rwLock);
	if(!m_overlay || !m_srcReader) { if(errorText) *errorText = QStringLiteral("источник не открыт"); return false; }
	const qint64 baseSize = m_srcReader->size();
	const QByteArray baseHash = m_srcReader->getHash();
	const auto* ov = static_cast<const PagedPieceOverlay*>(m_overlay.get());
	OverlayChangesWriter w(*ov, baseSize, baseHash);
	return w.writeToBuffer(outPayload, errorText);
}

bool MMapDataProvider::importBinaryConfig(const QByteArray& payload, QString* errorText) {
	QWriteLocker guard(&m_rwLock);
	if(!m_overlay) { if(errorText) *errorText = QStringLiteral("overlay отсутствует"); return false; }

	const qint64 oldSize = m_overlay->logicalSize();
	auto* ov = static_cast<PagedPieceOverlay*>(m_overlay.get());

	OverlayChangesReader r(ov);
	if(!r.applyFromBuffer(payload, errorText))
		return false;

	const qint64 newSize = m_overlay->logicalSize();
	const quint64 ver = ++m_version;
	const bool modifiedWhole = m_overlay->hasAnyModification();
	guard.unlock();

	QVector<ChangedSpan> spans{ChangedSpan{0, oldSize, newSize}};
	notifyDataChanged(spans, ver, modifiedWhole);
	if(newSize != oldSize) {
		GeometryEvent ev;
		if(newSize > oldSize) { ev.kind = GeometryKind::Insert; ev.at = oldSize; ev.length = newSize - oldSize; ev.newSize = newSize; } else { ev.kind = GeometryKind::Remove; ev.at = newSize; ev.length = oldSize - newSize; ev.newSize = newSize; }
		notifyGeometryChanged(QVector<GeometryEvent>{ev}, ver);
	}
	return true;
}

bool MMapDataProvider::importFromProjectFile(const QString& filePath, QString* errorText) {
	return IBinaryConfig::importFromProjectFile(filePath, errorText);
}