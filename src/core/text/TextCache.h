// TextCache.h
#pragma once
#include <QtGlobal>
#include <QString>
#include <QVector>
#include <memory>
#include <QTimer>
#include <QElapsedTimer>

class IDataProvider;
class DataTextTranslator;

class TextCache {
public:
	explicit TextCache(int cacheBlockSize)
		: m_cacheBlockSize(cacheBlockSize) {
	}

	void setCacheBlockSize(int size) { m_cacheBlockSize = size; }
	void clearCache() { m_cacheMapping.clear(); m_cacheStart = 0; m_cacheEnd = 0; }

	void updateCache(qint64 centerOffset, qint64 totalSize, DataTextTranslator* translator);

	//инвалидация кэша - если length==0, то пересчитываем от startOffset до конца окна
	void invalidateCache(qint64 startOffset,
		qint64 length,
		DataTextTranslator* translator,
		std::optional<qint64> newTotalSize = std::nullopt);

	const QVector<QString>& cacheArray() const { return m_cacheMapping; }
	int  cacheSize() const { return m_cacheMapping.size(); }
	qint64 start() const { return m_cacheStart; }
	qint64 end() const { return m_cacheEnd; }

private:
	// Общая низкоуровневая перекодировка в пределах указанного глобального диапазона,
	// с записью результата в m_cacheMapping. Диапазон автоматически обрезается окном [m_cacheStart, m_cacheEnd).
	void rebuildCacheRange(DataTextTranslator* translator,
		qint64 rebuildGlobalStart,
		qint64 rebuildGlobalEnd);

	// Отложенная пересборка кэша для крупных правок (например для key-repeat в режиме вставки).
	void scheduleDeferredRebuild(DataTextTranslator* translator,
		qint64 widenedGlobalStart,
		qint64 widenedGlobalEnd);
	void flushDeferredRebuild();

	// Состояние отложенной пересборки.
	QTimer m_debounceTimer;
	bool   m_debounceInited{false};
	bool   m_debouncePending{false};
	qint64 m_pendingStart{0};
	qint64 m_pendingEnd{0};
	DataTextTranslator* m_pendingTranslator{nullptr};

	//принудительный сброс кэша при превышении времени с момента первой отложенной пересборки
	QElapsedTimer m_deferredMaxWaitTimer;
	int m_deferredMaxWaitMs{50};   
private:
	QVector<QString> m_cacheMapping;
	qint64 m_cacheStart{0};
	qint64 m_cacheEnd{0};
	int    m_cacheBlockSize{6000};

	static constexpr int CACHE_BLOCK_FACTOR_BEFORE = 4;
	static constexpr int CACHE_BLOCK_FACTOR_AFTER = 3;

	// Локальный «коридор» для корректной обработки границ многобайтных символов (UTF-8/UTF-16).
	static constexpr int MAX_CODEPOINT_BYTES = 4;
};
