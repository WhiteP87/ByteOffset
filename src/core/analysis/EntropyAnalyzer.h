#pragma once

#include <QtCore>
#include <QColor>
#include <QtConcurrent/QtConcurrentRun>
#include <memory>
#include <QTimer>
#include <QElapsedTimer>

class IDataProvider;
struct ChangedSpan;
struct GeometryEvent;

//результат точечной выборки (одно окно)
struct EntropySample {
	qint64 offset{0};
	qint32 window{0};
	float  H{0.f};
	float  confidence{0.f};
};

//результат расчёта по произвольному диапазону
struct EntropyStats {
	qint64 offset{0};
	qint64 length{0};
	qint32 N{0};
	qint32 nonEmptyBins{0};
	float  H{0.f};
};

//выборка для экрана
struct ViewportSample {
	qint64 firstByte{0};
	qint64 lastByte{0};
	bool   valid{false};
	EntropySample sample{};
};

class EntropyAnalyzer final: public QObject {
	Q_OBJECT
public:
	struct Config {
		qint32 level1Window = 4096;
		qint32 level1MaxBatchBytes = 4 * 1024 * 1024;
		int    workers = 2;
		qreal  baseAlpha = 0.40;
		bool   heatmapRefineEnabled{false};
		qint32 heatmapRefineSamples{3};
	};

	// объявляем аксессор ПЕРЕД конструктором
	static const Config& DefaultConfig();

	explicit EntropyAnalyzer(std::shared_ptr<IDataProvider> provider,
		const Config& config = DefaultConfig(), QObject* parent = nullptr);

	//горячие настройки
	void setLevel1Window(qint32 bytes);
	void setWorkers(int workers);
	void setBaseAlpha(qreal alpha);
	void setRefineHeatMapEnabled(bool enabled);

	//остановка расчётов
	void stopHeatmap();
	void stopAllCalculations();

	qint32 level1Window() const noexcept { return m_config.level1Window; }
	qreal  baseAlpha() const noexcept { return m_config.baseAlpha; }

	//экран: асинхронная выборка по центру
	void requestViewportCenter(qint64 firstByteOffset, qint64 lastByteOffset);

	//диапазон: async + progress
	quint64 requestRangeEntropyAsync(qint64 offset, qint64 length, qint64 chunkBytes = (1ll << 20));

	//диапазон: sync
	EntropyStats computeRangeEntropy(qint64 offset, qint64 length, qint64 chunkBytes = (1ll << 20)) const;

	//получить последнюю экранную выборку (если готово)
	bool tryGetViewportSample(ViewportSample& out) const;

	//цвет по энтропии
	static QColor colorForEntropy(float entropyH, float confidence, qreal baseAlpha);

	//теплокарта
	void   initHeatmap(qint32 desiredVisualBins = 0); //0 => авто 1024
	qint64 heatmapBinCount() const;
	QColor heatmapColorByIndex(qint64 binIndex, qreal baseAlpha) const;
	qint64 heatmapBinCenterOffset(qint64 binIndex) const;

	bool isEnabled() const { return m_enabled; }
	void setEnabled(bool enabled) { m_enabled = enabled; }

signals:
	//экран
	void viewportEntropyStarted(qint64 firstByteOffset, qint64 lastByteOffset);
	void viewportEntropyReady(qint64 firstByteOffset, qint64 lastByteOffset);

	//теплокарта: обновлён диапазон бинов
	void heatmapRangeUpdated(qint64 firstBinIndex, qint64 lastBinIndex);

	//диапазон
	void rangeEntropyStarted(quint64 jobId, qint64 offset, qint64 length);
	void rangeEntropyProgress(quint64 jobId, qint64 processedBytes, qint64 totalBytes);
	void rangeEntropyReady(quint64 jobId, const EntropyStats& stats);
	void rangeEntropyError(quint64 jobId, const QString& message);

private:
	//провайдер и состояние
	std::shared_ptr<IDataProvider> m_provider;
	Config m_config{};
	std::atomic<quint64> m_revision{0};
	std::atomic<quint64> m_configEpoch{0};   //для отмены viewport/range-задач
	QThreadPool m_pool;
	std::atomic<quint64> m_jobCounter{1};
	quint64 m_subDataToken{0};
	quint64 m_subGeomToken{0};

	//экранная выборка
	mutable QMutex m_viewportMutex;
	ViewportSample m_lastSample{};
	bool m_lastSampleValid{false};

	//визуальные бины
	struct HeatmapBin {
		qint64 centerOffset{0};
		qint32 windowBytes{0};
		float  entropyH{0.f};
		float  confidence{0.f};
	};

	mutable QMutex m_heatmapMutex;
	QVector<HeatmapBin> m_heatmapBins;
	qint32 m_heatmapVisualBins{0};
	QColor m_heatmapNotReadyColor{QColor(120, 72, 0)};
	std::atomic<quint64> m_heatmapEpoch{0};  //для отмены теплокарты
	bool m_enabled{true};

	void scheduleHeatmapRecalc(const QVector<ChangedSpan>& spans, bool fullRebuild);
	void flushHeatmapRecalc();

	QTimer m_heatmapTimer;
	QElapsedTimer m_heatmapMaxWait;
	bool m_heatmapTimerInited{false};

	int m_heatmapDebounceMs{120};  
	int m_heatmapMaxWaitMs{240};   

	bool m_heatmapPending{false};
	bool m_heatmapPendingFullRebuild{false};
	QVector<ChangedSpan> m_heatmapPendingSpans;

private:
	//подписки провайдера
	void subscribeProvider();
	void onDataChanged(quint64 newRevision, const QVector<ChangedSpan>& spans, bool modifiedWhole);
	void onGeometryChanged(quint64 newRevision, const QVector<GeometryEvent>& events);

	//вспомогательные
	static EntropySample computeCenteredWindow(const uint8_t* batchPtr,
		qint64 batchBegin,
		qint64 fileSize,
		qint64 centerByte,
		qint32 nominalWindow);

	static float shannonEntropy(const uint8_t* data, int length, int* outNonEmptyBins = nullptr);

	//heatmap infra
	quint64 beginHeatmapEpoch();                         //увеличивает m_heatmapEpoch
	bool    isHeatmapObsolete(quint64 jobEpoch) const;   //сравнение с m_heatmapEpoch

	void prepareHeatmapGeometry(qint32 visualBins, qint32 nominalWindowBytes, qint64 fileSizeBytes);

	//единый проход: sampleCount==1 (coarse), >1 (refine); touched==nullptr => все бины
	void updateHeatmap(quint64 jobEpoch,
		qint64 fileSize,
		qint32 visualBins,
		qint32 nominalWindowBytes,
		int sampleCount,
		const QVector<int>* touchedBinIndices);

	void recalcHeatmapForSpans(const QVector<ChangedSpan>& changedSpans);
	QVector<int> computeTouchedIndices(const QVector<ChangedSpan>& changedSpans,
		qint64 fileSize,
		qint32 binsCount,
		qint32 nominalWindow) const;

	void rebuildHeatmapPreservingVisualBins();
};
