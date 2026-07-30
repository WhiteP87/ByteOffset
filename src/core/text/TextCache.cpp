#include "TextCache.h"
#include "DataTextTranslator.h"
#include <algorithm>
#include <cassert>
#include <QObject>

void TextCache::updateCache(qint64 centerOffset,
	qint64 totalSize,
	DataTextTranslator* translator) {
	if(!translator || totalSize <= 0) {
		clearCache();
		return;
	}

	m_cacheStart = std::max<qint64>(0, centerOffset - m_cacheBlockSize * CACHE_BLOCK_FACTOR_BEFORE);
	m_cacheEnd = std::min<qint64>(totalSize, centerOffset + m_cacheBlockSize * CACHE_BLOCK_FACTOR_AFTER);

	const int cacheWindowLength = static_cast<int>(m_cacheEnd - m_cacheStart);
	m_cacheMapping.clear();
	if(cacheWindowLength <= 0) return;

	m_cacheMapping.resize(cacheWindowLength);
	rebuildCacheRange(translator, m_cacheStart, m_cacheEnd);
}

void TextCache::invalidateCache(qint64 startOffset,
	qint64 length,
	DataTextTranslator* translator,
	std::optional<qint64> newTotalSize) {
	if(!translator || m_cacheMapping.isEmpty())
		return;
	
	//изменение правее окна
	if(startOffset >= m_cacheEnd)
		return;

	//диапазон пересчёта, при необходимости корректировка окна
	qint64 requestedRangeStart = startOffset;
	qint64 requestedRangeEnd = (length == 0) ? m_cacheEnd : (startOffset + length);

	if(length == 0) {
		//размер изменился, требуется новый размер
		if(!newTotalSize.has_value()) return;

		const qint64 oldWindowLength = m_cacheEnd - m_cacheStart;

		//если новый размер меньше начала окна — окно целиком выпало
		if(newTotalSize.value() <= m_cacheStart) {
			clearCache();
			return;
		}

		const qint64 adjustedCacheEnd = std::min<qint64>(newTotalSize.value(), m_cacheStart + oldWindowLength);
		if(adjustedCacheEnd != m_cacheEnd) {
			m_cacheEnd = adjustedCacheEnd;
			m_cacheMapping.resize(static_cast<int>(m_cacheEnd - m_cacheStart));
			if(m_cacheMapping.isEmpty()) return;
		}

		requestedRangeEnd = m_cacheEnd;
	}

	//расширить на границы многобайтной кодовой точки
	const qint64 widenedRangeStart = std::max<qint64>(m_cacheStart, requestedRangeStart - MAX_CODEPOINT_BYTES);
	const qint64 widenedRangeEnd = std::min<qint64>(m_cacheEnd, requestedRangeEnd + MAX_CODEPOINT_BYTES);

	if(widenedRangeEnd <= widenedRangeStart)
		return;

	if(length == 0) {
		//серия правок (0/1/0/1...). Не пересчитываем сейчас.
		scheduleDeferredRebuild(translator, widenedRangeStart, widenedRangeEnd);
		return;
	}

	//нет серии — можно сделать сразу
	rebuildCacheRange(translator, widenedRangeStart, widenedRangeEnd);
}

void TextCache::rebuildCacheRange(DataTextTranslator* translator,
	qint64 rebuildGlobalStart,
	qint64 rebuildGlobalEnd) {
	if(!translator || m_cacheMapping.isEmpty())
		return;

	const qint64 effectiveStart = std::max<qint64>(m_cacheStart, rebuildGlobalStart);
	const qint64 effectiveEnd = std::min<qint64>(m_cacheEnd, rebuildGlobalEnd);
	if(effectiveEnd <= effectiveStart)
		return;

	const int  cacheWindowLength = m_cacheMapping.size();
	int        cacheLocalIndex = static_cast<int>(effectiveStart - m_cacheStart);
	qint64     globalOffset = effectiveStart;

	while(globalOffset < effectiveEnd && cacheLocalIndex < cacheWindowLength) {
		const QPair<QString, int> decoded = translator->decodeAt(globalOffset);
		const QString& glyph = decoded.first;
		int codepointByteCount = decoded.second;
		if(codepointByteCount <= 0) codepointByteCount = 1;

		//первый байт кодовой точки
		m_cacheMapping[cacheLocalIndex] = glyph.isEmpty()
			? translator->policies().m_nonPrintablePlaceholder
			: glyph;

		//хвостовые байты — плейсхолдеры продолжения
		for(int tail = 1; tail < codepointByteCount; ++tail) {
			const int    nextLocalIndex = cacheLocalIndex + tail;
			const qint64 nextGlobalOff = globalOffset + tail;
			if(nextLocalIndex >= cacheWindowLength) break;
			if(nextGlobalOff >= effectiveEnd)      break;
			m_cacheMapping[nextLocalIndex] = translator->policies().m_continuationPlaceholder;
		}

		globalOffset += codepointByteCount;
		cacheLocalIndex += codepointByteCount;
	}

}

void TextCache::scheduleDeferredRebuild(DataTextTranslator* translator,
	qint64 widenedGlobalStart,
	qint64 widenedGlobalEnd) {
	if(!translator)
		return;

	if(!m_debounceInited) {
		m_debounceInited = true;
		m_debounceTimer.setSingleShot(true);
		m_debounceTimer.setInterval(50); // 50мс для key repeat
		QObject::connect(&m_debounceTimer, &QTimer::timeout,
			[this] { flushDeferredRebuild(); });
	}

	// При смене транслятора пересобираем немедленно, без дебаунса, чтобы не допустить накопления отложенных пересборок.
	if(m_debouncePending && m_pendingTranslator && m_pendingTranslator != translator)
		flushDeferredRebuild();

	m_pendingTranslator = translator;
	if(!m_debouncePending) {
		m_debouncePending = true;
		m_pendingStart = widenedGlobalStart;
		m_pendingEnd = widenedGlobalEnd;
	} else {
		m_pendingStart = std::min(m_pendingStart, widenedGlobalStart);
		m_pendingEnd = std::max(m_pendingEnd, widenedGlobalEnd);
	}

	// Restart
	m_debounceTimer.start();
	if(!m_deferredMaxWaitTimer.isValid())
		m_deferredMaxWaitTimer.start();

	if(m_deferredMaxWaitTimer.elapsed() >= m_deferredMaxWaitMs) {
		flushDeferredRebuild();
		m_deferredMaxWaitTimer.restart();
	}
}

void TextCache::flushDeferredRebuild() {
	if(!m_debouncePending)
		return;

	m_debouncePending = false;

	DataTextTranslator* translator = m_pendingTranslator;
	m_pendingTranslator = nullptr;

	if(!translator || m_cacheMapping.isEmpty())
		return;

	const qint64 effectiveStart = std::max<qint64>(m_cacheStart, m_pendingStart);
	const qint64 effectiveEnd = std::min<qint64>(m_cacheEnd, m_pendingEnd);
	if(effectiveEnd <= effectiveStart)
		return;

	rebuildCacheRange(translator, effectiveStart, effectiveEnd);
}