//комментарии оформлены над каждой функцией
//комментарии с маленькой буквы, без пробела после //, без кавычек и без двоеточия
#include "PagedPieceOverlay.h"
#include "OverlayChangesWriter.h"
#include "OverlayChangesReader.h"

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#endif
#ifdef Q_OS_UNIX
#  include <unistd.h>
#endif

//получение системной гранулярности памяти
static qint64 sysGranularityImpl() {
#ifdef Q_OS_WIN
	SYSTEM_INFO si; ::GetSystemInfo(&si);
	return qint64(si.dwAllocationGranularity);
#else
	long pageSize = ::sysconf(_SC_PAGESIZE);
	return pageSize > 0 ? qint64(pageSize) : qint64(4096);
#endif
}

//обёртка над реализацией получения гранулярности
qint64 PagedPieceOverlay::systemGranularity() {
	return sysGranularityImpl();
}

//конструктор инициализирует стор, базовый кусок и индексы
PagedPieceOverlay::PagedPieceOverlay(const ISrcReader& baseReader, const Config& cfg)
	: m_base(baseReader)
	, m_store(std::make_unique<TempMMapPageStore>())
	, m_logicalSize(baseReader.size())
	, m_logicalPageSize(cfg.m_logicalPageSize > 0 ? cfg.m_logicalPageSize : systemGranularity())
	, m_fullRatio(cfg.m_fullRatio) {

	Q_ASSERT(m_logicalPageSize > 0);

	//один базовый кусок на весь файл при создании
	Piece basePiece;
	basePiece.m_src = PieceSrc::Base;
	basePiece.m_srcOff = 0;
	basePiece.m_len = m_logicalSize;
	m_pieces.push_back(basePiece);

	//инициализируем индекс
	m_checkpointsDirty = true;
	m_cachedPieceIndex = -1;
	m_cachedPieceLogicalBase = 0;
}

//возвращает логический размер наложения
qint64 PagedPieceOverlay::logicalSize() const {
	return m_logicalSize;
}

//перестраивает таблицу контрольных точек для ускоренного поиска кусков
void PagedPieceOverlay::rebuildCheckpoints() const {
	QMutexLocker indexLock(&m_indexMutex);
	if(!m_checkpointsDirty) {
		return;
	}

	m_checkpoints.clear();
	m_checkpoints.reserve((m_pieces.size() / CHECKPOINT_STEPS) + 2);

	qint64 logicalPrefix = 0;
	for(int pieceIndex = 0; pieceIndex < m_pieces.size(); ++pieceIndex) {
		if(pieceIndex % CHECKPOINT_STEPS == 0) {
			m_checkpoints.push_back(Checkpoint{logicalPrefix, pieceIndex});
		}
		logicalPrefix += m_pieces[pieceIndex].m_len;
	}
	m_checkpointsDirty = false;

	//сбрасываем локальный кеш
	m_cachedPieceIndex = -1;
	m_cachedPieceLogicalBase = 0;
}

//помечает контрольные точки как устаревшие после изменения кусков
void PagedPieceOverlay::markPiecesChanged(int /*fromPieceIndex*/) {
	QMutexLocker indexLock(&m_indexMutex);
	m_checkpointsDirty = true;
	m_cachedPieceIndex = -1;
	m_cachedPieceLogicalBase = 0;
}

//сливает соседние куски при возможности
void PagedPieceOverlay::mergeAround(qint32 aroundIndex) {
	if(m_pieces.isEmpty()) {
		return;
	}
	aroundIndex = std::clamp(aroundIndex, 0, (qint32)(m_pieces.size() - 1));

	//влево
	if(aroundIndex > 0) {
		Piece& left = m_pieces[aroundIndex - 1];
		Piece& curr = m_pieces[aroundIndex];
		if(left.m_src == curr.m_src) {
			const bool contBase = (left.m_src == PieceSrc::Base) &&
				(left.m_srcOff + left.m_len == curr.m_srcOff);
			const bool contAdd = (left.m_src == PieceSrc::Add) &&
				(left.m_handle.isValid() && curr.m_handle.isValid() &&
					left.m_handle.m_offset == curr.m_handle.m_offset &&
					left.m_srcOff + left.m_len == curr.m_srcOff);
			if(contBase || contAdd) {
				left.m_len += curr.m_len;
				m_pieces.removeAt(aroundIndex);
				aroundIndex -= 1;
			}
		}
	}
	//вправо
	if(aroundIndex + 1 < m_pieces.size()) {
		Piece& curr = m_pieces[aroundIndex];
		Piece& right = m_pieces[aroundIndex + 1];
		if(curr.m_src == right.m_src) {
			const bool contBase = (curr.m_src == PieceSrc::Base) &&
				(curr.m_srcOff + curr.m_len == right.m_srcOff);
			const bool contAdd = (curr.m_src == PieceSrc::Add) &&
				(curr.m_handle.isValid() && right.m_handle.isValid() &&
					curr.m_handle.m_offset == right.m_handle.m_offset &&
					curr.m_srcOff + curr.m_len == right.m_srcOff);
			if(contBase || contAdd) {
				curr.m_len += right.m_len;
				m_pieces.removeAt(aroundIndex + 1);
			}
		}
	}
}

//находит индекс куска для логического смещения и возвращает смещение внутри куска
int PagedPieceOverlay::findPiece(qint64 logicalOffset, qint64* inPieceOffset) const {
	Q_ASSERT(logicalOffset >= 0 && logicalOffset <= m_logicalSize);

	//быстрый путь через кеш и соседей под коротким локом
	{
		QMutexLocker indexLock(&m_indexMutex);
		if(!m_checkpointsDirty && m_cachedPieceIndex >= 0 && m_cachedPieceIndex < m_pieces.size()) {
			const Piece& cachedPiece = m_pieces[m_cachedPieceIndex];
			const qint64 cachedBase = m_cachedPieceLogicalBase;

			if(logicalOffset >= cachedBase && logicalOffset < cachedBase + cachedPiece.m_len) {
				if(inPieceOffset) {
					*inPieceOffset = logicalOffset - cachedBase;
				}
				return m_cachedPieceIndex;
			}
			//сосед слева
			if(m_cachedPieceIndex > 0) {
				const Piece& leftPiece = m_pieces[m_cachedPieceIndex - 1];
				const qint64 leftBase = cachedBase - leftPiece.m_len;
				if(logicalOffset >= leftBase && logicalOffset < leftBase + leftPiece.m_len) {
					m_cachedPieceIndex -= 1;
					m_cachedPieceLogicalBase = leftBase;
					if(inPieceOffset) {
						*inPieceOffset = logicalOffset - leftBase;
					}
					return m_cachedPieceIndex;
				}
			}
			//сосед справа
			if(m_cachedPieceIndex + 1 < m_pieces.size()) {
				const Piece& rightPiece = m_pieces[m_cachedPieceIndex + 1];
				const qint64 rightBase = cachedBase + cachedPiece.m_len;
				if(logicalOffset >= rightBase && logicalOffset < rightBase + rightPiece.m_len) {
					m_cachedPieceIndex += 1;
					m_cachedPieceLogicalBase = rightBase;
					if(inPieceOffset) {
						*inPieceOffset = logicalOffset - rightBase;
					}
					return m_cachedPieceIndex;
				}
			}
		}
	}

	//jump table построится под локом внутри rebuildCheckpoints
	rebuildCheckpoints();

	//двоичный поиск по контрольным точкам
	qint32 lowIndex = 0;
	qint32 highIndex = m_checkpoints.size() - 1;
	qint32 checkpointIndex = 0;
	while(lowIndex <= highIndex) {
		const qint32 midIndex = (lowIndex + highIndex) >> 1;
		const qint64 prefix = m_checkpoints[midIndex].logicalPrefix;
		if(prefix <= logicalOffset) {
			checkpointIndex = midIndex;
			lowIndex = midIndex + 1;
		} else {
			highIndex = midIndex - 1;
		}
	}

	int pieceIndex = m_checkpoints[checkpointIndex].pieceIndex;
	qint64 pieceLogicalBase = m_checkpoints[checkpointIndex].logicalPrefix;
	for(int pieceLimit = m_pieces.size(); pieceIndex < pieceLimit; ++pieceIndex) {
		const qint64 pieceLength = m_pieces[pieceIndex].m_len;
		if(logicalOffset < pieceLogicalBase + pieceLength) {
			QMutexLocker indexLock(&m_indexMutex);
			m_cachedPieceIndex = pieceIndex;
			m_cachedPieceLogicalBase = pieceLogicalBase;
			if(inPieceOffset) {
				*inPieceOffset = logicalOffset - pieceLogicalBase;
			}
			return pieceIndex;
		}
		pieceLogicalBase += pieceLength;
	}

	if(inPieceOffset) {
		*inPieceOffset = 0;
	}
	return m_pieces.size();
}

//читает один байт по логическому смещению с учетом full и sparse
quint8 PagedPieceOverlay::readByte(qint64 logicalOffset) const {
	if(logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return 0;
	}

	const qint64 pageIndex = logicalPageIndex(logicalOffset);
	const int inPage = inLogicalPage(logicalOffset);

	//full страница читается напрямую из стора
	if(const auto itFull = m_fullPages.constFind(pageIndex); itFull != m_fullPages.cend()) {
		char b = 0;
		m_store->read(itFull.value(), inPage, 1, &b);
		return quint8(uchar(b));
	}

	//sparse патч приоритетнее кусков
	if(const auto itPg = m_patchPages.constFind(pageIndex); itPg != m_patchPages.cend()) {
		const auto it = itPg->m_edits.constFind(inPage);
		if(it != itPg->m_edits.cend()) {
			return it.value();
		}
	}

	//чтение из таблицы кусков
	qint64 inPieceOffset = 0;
	const int pieceIndex = findPiece(logicalOffset, &inPieceOffset);
	if(pieceIndex >= m_pieces.size()) {
		return 0;
	}

	char b = 0;
	if(readFromPiece(m_pieces[pieceIndex], inPieceOffset, 1, &b) != 1) {
		return 0;
	}
	return quint8(uchar(b));
}

//читает диапазон в QByteArray с учетом full и sparse
qint64 PagedPieceOverlay::readRange(qint64 logicalOffset, qint64 length, QByteArray& out) const {
	if(length <= 0 || logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		out.clear();
		return 0;
	}
	const qint64 maxLen = std::min<qint64>(length, m_logicalSize - logicalOffset);
	out.resize(qsizetype(maxLen));
	const qint64 got = readRange(logicalOffset, maxLen, out.data());
	if(got < maxLen) {
		out.resize(qsizetype(got));
	}
	return got;
}

//читает диапазон в внешний буфер с учетом full и sparse
qint64 PagedPieceOverlay::readRange(qint64 logicalOffset, qint64 length, char* destination) const {
	if(!destination || length <= 0 || logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return 0;
	}

	const qint64 maxLen = std::min<qint64>(length, m_logicalSize - logicalOffset);
	if(maxLen <= 0) {
		return 0;
	}

	qint64 remaining = maxLen;
	qint64 cursor = logicalOffset;
	qint64 copied = 0;

	//нет full, нет sparse, один кусок base на весь файл
	if(m_fullPages.isEmpty() && m_patchPages.isEmpty() && m_pieces.size() == 1) {
		const Piece& only = m_pieces[0];
		if(only.m_src == PieceSrc::Base) {
			const qint64 physOff = only.m_srcOff + logicalOffset;
			return m_base.readRange(physOff, maxLen, destination);
		}
	}

	while(remaining > 0) {
		const qint64 pageIndex = logicalPageIndex(cursor);
		const int inPage = inLogicalPage(cursor);
		const qint64 pageAvail = logicalPageLen(pageIndex) - inPage;
		const qint64 step = std::min<qint64>(remaining, pageAvail);

		//full страница читается напрямую
		if(const auto itFull = m_fullPages.constFind(pageIndex); itFull != m_fullPages.cend()) {
			const qint64 got = m_store->read(itFull.value(), inPage, step, destination + copied);
			if(got <= 0) {
				break;
			}
			copied += got;
			cursor += got;
			remaining -= got;
			continue;
		}

		//сборка из кусков
		const qint64 got = copyFromPieces(cursor, step, destination + copied);
		if(got <= 0) {
			break;
		}

		//наложение sparse поверх полученного окна
		applyLogicalPatches(cursor, got, destination + copied, true);

		copied += got;
		cursor += got;
		remaining -= got;
	}
	return copied;
}

//читает диапазон из одного куска в буфер
qint64 PagedPieceOverlay::readFromPiece(const Piece& piece, qint64 inPieceOffset, qint64 length, char* dst) const {
	if(length <= 0) {
		return 0;
	}
	if(piece.m_src == PieceSrc::Base) {
		return m_base.readRange(piece.m_srcOff + inPieceOffset, length, dst);
	} else {
		return m_store->read(piece.m_handle, inPieceOffset, length, dst);
	}
}

//накладывает sparse патчи на буфер логического диапазона
void PagedPieceOverlay::applyLogicalPatches(qint64 absOffset, qint64 length, char* dst, bool ignoreFull) const {
	if(length <= 0) {
		return;
	}

	const qint64 firstPage = logicalPageIndex(absOffset);
	const qint64 lastPage = logicalPageIndex(absOffset + length - 1);

	for(qint64 pageIndex = firstPage; pageIndex <= lastPage; ++pageIndex) {
		const qint64 baseAbs = logicalPageBase(pageIndex);
		const qint64 segStart = std::max<qint64>(absOffset, baseAbs);
		const qint64 segEnd = std::min<qint64>(absOffset + length, baseAbs + logicalPageLen(pageIndex));
		const int inStart = int(segStart - baseAbs);
		const int inLength = int(segEnd - segStart);
		if(inLength <= 0) {
			continue;
		}

		if(!ignoreFull) {
			if(const auto itFull = m_fullPages.constFind(pageIndex); itFull != m_fullPages.cend()) {
				m_store->read(itFull.value(), inStart, inLength, dst + (segStart - absOffset));
				continue;
			}
		}

		const auto itPg = m_patchPages.constFind(pageIndex);
		if(itPg == m_patchPages.constEnd()) {
			continue;
		}

		auto it = itPg->m_edits.lowerBound(inStart);
		const int endKey = inStart + inLength;
		for(; it != itPg->m_edits.cend() && it.key() < endKey; ++it) {
			const qint64 editAbs = baseAbs + it.key();
			dst[editAbs - absOffset] = char(it.value());
		}
	}
}

//собирает полную страницу в буфер из кусков и накладывает sparse
void PagedPieceOverlay::buildPageBuffer(qint64 pageIndex, QByteArray& out) const {
	const qint64 pageLength = logicalPageLen(pageIndex);
	out.resize(qsizetype(pageLength));
	if(pageLength <= 0) {
		return;
	}

	const qint64 absBase = logicalPageBase(pageIndex);

	const qint64 got = copyFromPieces(absBase, pageLength, out.data());
	Q_ASSERT(got == pageLength);

	if(got != pageLength) {
		out.truncate(int(std::max<qint64>(0, got)));
		return;
	}

	applyLogicalPatches(absBase, pageLength, out.data(), true);
}

//вычисляет порог преобразования страницы в full в байтах
qsizetype PagedPieceOverlay::calcPromoteSize(qint64 /*pageIndex*/) const {
	if(m_fullRatio < 0.0) {
		return std::numeric_limits<qsizetype>::max();
	}
	const qint64 denom = std::max<qint64>(1, m_logicalPageSize);
	const qint64 raw = qint64(std::floor(double(denom) * m_fullRatio));
	return qsizetype(std::clamp<qint64>(raw, 1, denom));
}

//проверяет необходимость преобразования страницы в full по количеству патчей
void PagedPieceOverlay::checkNeedPromote(qint64 pageIndex) {
	if(m_fullRatio < 0.0) {
		return;
	}
	if(m_fullPages.contains(pageIndex)) {
		return;
	}

	const auto it = m_patchPages.constFind(pageIndex);
	const qsizetype edits = (it == m_patchPages.cend()) ? 0 : it->m_edits.size();
	if(edits <= 0) {
		return;
	}
	if(edits < calcPromoteSize(pageIndex)) {
		return;
	}

	QByteArray buf;
	buildPageBuffer(pageIndex, buf);
	if(buf.isEmpty()) {
		return;
	}

	PageHandle handle = m_store->allocate(buf.size(), buf.constData());
	if(!handle.isValid()) {
		return;
	}

	m_fullPages.insert(pageIndex, handle);
	m_patchPages.remove(pageIndex);

	{
		QMutexLocker fullLock(&m_fullDiffMutex);
		m_fullDiffBits.remove(pageIndex);
		m_fullDiffCount.remove(pageIndex);
	}
}

//перезаписывает один байт с маской
bool PagedPieceOverlay::overwriteByte(qint64 logicalOffset, quint8 value, quint8 mask) {
	const quint8 oldByte = readByte(logicalOffset);
	const quint8 newByte = (oldByte & ~mask) | (value & mask);
	if(newByte == oldByte) {
		return true;
	}
	const char c = char(newByte);
	const qint64 written = overwriteRange(logicalOffset, QByteArrayView(&c, 1));
	return written == 1;
}

//перезаписывает диапазон значениями из буфера
qint64 PagedPieceOverlay::overwriteRange(qint64 logicalOffset, QByteArrayView data) {
	if(data.isEmpty() || logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return 0;
	}

	const qint64 maxLen = std::min<qint64>(data.size(), m_logicalSize - logicalOffset);
	qint64 writtenTotal = 0;
	qint64 cursor = logicalOffset;

	while(writtenTotal < maxLen) {
		const qint64 pageIndex = logicalPageIndex(cursor);
		const int inPage = inLogicalPage(cursor);
		const qint64 pageAvail = logicalPageLen(pageIndex) - inPage;
		const qint64 step = std::min<qint64>(maxLen - writtenTotal, pageAvail);

		updateFull(pageIndex);

		if(const auto itFull = m_fullPages.find(pageIndex); itFull != m_fullPages.end()) {
			const qint64 got = m_store->write(itFull.value(), inPage, QByteArrayView(data.data() + writtenTotal, int(step)));
			if(got <= 0) {
				break;
			}

			const qint64 logicalStart = cursor;
			updateFullDiffBitsOnWrite(pageIndex, logicalStart, got);

			writtenTotal += got;
			cursor += got;
		} else {
			qint64 leftInStep = step;
			while(leftInStep > 0) {
				qint64 inPieceOffset = 0;
				const int pieceIndex = findPiece(cursor, &inPieceOffset);

				if(pieceIndex >= m_pieces.size()) {
					return writtenTotal;
				}

				const Piece& piece = m_pieces[pieceIndex];
				const qint64 take = std::min<qint64>(leftInStep, piece.m_len - inPieceOffset);

				if(piece.m_src == PieceSrc::Add) {
					const qint64 got = m_store->write(piece.m_handle,
						inPieceOffset,
						QByteArrayView(data.data() + writtenTotal, int(take)));
					if(got <= 0) {
						return writtenTotal;
					}
					writtenTotal += got;
					cursor += got;
					leftInStep -= got;
				} else {
					QByteArray baseBuf; baseBuf.resize(int(take));
					const qint64 baseSrcOff = piece.m_srcOff + inPieceOffset;
					if(m_base.readRange(baseSrcOff, take, baseBuf.data()) != take) {
						return writtenTotal;
					}

					auto& edits = m_patchPages[pageIndex].m_edits;
					const int basePos = inPage + int(step - leftInStep);

					auto hint = edits.lowerBound(basePos);
					for(qint64 i = 0; i < take; ++i) {
						const int pos = basePos + int(i);
						const quint8 newByte = quint8(uchar(data[int(writtenTotal + i)]));
						const quint8 baseByte = quint8(uchar(baseBuf[int(i)]));
						if(newByte == baseByte) {
							if(auto it = edits.find(pos); it != edits.end()) {
								edits.erase(it);
							}
						} else {
							hint = edits.insert(hint, pos, newByte);
							++hint;
						}
					}

					if(edits.isEmpty()) {
						m_patchPages.remove(pageIndex);
					}

					writtenTotal += take;
					cursor += take;
					leftInStep -= take;
				}
			}

			checkNeedPromote(pageIndex);
		}
	}

	if(writtenTotal > 0) {
		recordOverwrite(logicalOffset, writtenTotal);
	}
	return writtenTotal;
}

//вставляет диапазон данных в указанное логическое место
qint64 PagedPieceOverlay::insertRange(qint64 logicalOffset, QByteArrayView data) {
	if(data.isEmpty() || logicalOffset < 0 || logicalOffset > m_logicalSize) {
		return 0;
	}

	PageHandle handle = m_store->allocate(data.size(), data.data());
	if(!handle.isValid()) {
		return 0;
	}

	Piece addPiece;
	addPiece.m_src = PieceSrc::Add;
	addPiece.m_srcOff = 0;
	addPiece.m_len = data.size();
	addPiece.m_handle = handle;

	const int idx = splitAt(logicalOffset);
	m_pieces.insert(idx, addPiece);
	m_logicalSize += addPiece.m_len;

	mergeAround(idx);
	markPiecesChanged(idx);

	shiftPatches(logicalOffset, +addPiece.m_len);

	tryFoldInsertedAsBase(logicalOffset, addPiece.m_len);

	dropFullPages();

	for(qint64 pos = logicalOffset, end = logicalOffset + addPiece.m_len; pos < end; ) {
		const qint64 pageIdx = logicalPageIndex(pos);
		checkNeedPromote(pageIdx);
		const qint64 pageEnd = logicalPageBase(pageIdx) + logicalPageLen(pageIdx);
		pos = std::min(end, pageEnd);
	}

	m_structuralChanges = true;
	return addPiece.m_len;
}

//удаляет диапазон данных из логического пространства
qint64 PagedPieceOverlay::removeRange(qint64 logicalOffset, qint64 length) {
	if(length <= 0 || logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return 0;
	}
	const qint64 maxLen = std::min<qint64>(length, m_logicalSize - logicalOffset);
	if(maxLen <= 0) {
		return 0;
	}

	const int leftIdx = splitAt(logicalOffset);
	const int rightIdx = splitAt(logicalOffset + maxLen);

	int toRemove = rightIdx - leftIdx;
	while(toRemove-- > 0 && leftIdx < m_pieces.size()) {
		m_pieces.removeAt(leftIdx);
	}

	m_logicalSize -= maxLen;

	erasePatches(logicalOffset, maxLen);
	shiftPatches(logicalOffset + maxLen, -maxLen);

	mergeAround(std::max(0, leftIdx - 1));
	markPiecesChanged(std::max(0, leftIdx - 1));

	m_structuralChanges = true;
	return maxLen;
}

//заливает диапазон повторяющимся шаблоном
qint64 PagedPieceOverlay::fillRange(qint64 logicalOffset, qint64 length, QByteArrayView pattern) {
	if(length <= 0 || pattern.isEmpty() || logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return 0;
	}

	const qint64 maxLen = std::min<qint64>(length, m_logicalSize - logicalOffset);
	const qsizetype patLen = pattern.size();

	qint64 writtenTotal = 0;
	qint64 cursor = logicalOffset;

	while(writtenTotal < maxLen) {
		const qint64 pageIndex = logicalPageIndex(cursor);
		const qint32 inPage = inLogicalPage(cursor);
		const qint64 pageAvail = logicalPageLen(pageIndex) - inPage;
		const qint64 step = std::min<qint64>(maxLen - writtenTotal, pageAvail);

		updateFull(pageIndex);

		if(const auto itFull = m_fullPages.find(pageIndex); itFull != m_fullPages.end()) {
			qint64 left = step;
			qint64 seq = 0;
			while(left > 0) {
				const qint64 chunk = std::min<qint64>(left, 1ll << 16);
				QByteArray tmp(qsizetype(chunk), Qt::Uninitialized);

				for(qint64 i = 0; i < chunk; ++i) {
					tmp[qsizetype(i)] = pattern[qsizetype((seq + i) % patLen)];
				}

				const qint64 got = m_store->write(itFull.value(), inPage + int(step - left), QByteArrayView(tmp));
				if(got <= 0) {
					break;
				}
				left -= got;
				seq += got;
			}
			writtenTotal += step;
			cursor += step;
			updateFullDiffBitsOnWrite(pageIndex, cursor - step, step);

		} else {
			qint64 left = step;
			qint64 seq = 0;
			while(left > 0) {
				qint64 inPieceOffset = 0;
				const int pieceIndex = findPiece(cursor, &inPieceOffset);

				if(pieceIndex >= m_pieces.size()) {
					return writtenTotal;
				}

				const Piece& piece = m_pieces[pieceIndex];
				const qint64 take = std::min<qint64>(left, piece.m_len - inPieceOffset);

				if(piece.m_src == PieceSrc::Add) {
					qint64 localLeft = take;
					qint64 localOfs = 0;
					while(localLeft > 0) {
						const qint64 chunk = std::min<qint64>(localLeft, 1ll << 16);
						QByteArray tmp(qsizetype(chunk), Qt::Uninitialized);

						for(qint64 i = 0; i < chunk; ++i) {
							tmp[qsizetype(i)] = pattern[qsizetype((seq + i) % patLen)];
						}

						m_store->write(piece.m_handle, inPieceOffset + localOfs, QByteArrayView(tmp));
						localLeft -= chunk;
						localOfs += chunk;
						seq += chunk;
					}
				} else {
					auto& edits = m_patchPages[pageIndex].m_edits;
					const int basePos = inPage + int(step - left);

					QByteArray baseBuf(qsizetype(take), Qt::Uninitialized);
					{
						const qint64 gotBase = readFromPiece(piece, inPieceOffset, take, baseBuf.data());
						if(gotBase != take) {
							return writtenTotal;
						}
					}

					auto hint = edits.lowerBound(basePos);
					for(qint64 i = 0; i < take; ++i) {
						const int pos = basePos + int(i);
						const quint8 newB = quint8(uchar(pattern[int((seq + i) % patLen)]));
						const quint8 baseB = quint8(uchar(baseBuf[int(i)]));

						if(newB == baseB) {
							if(edits.contains(pos)) {
								edits.remove(pos);
							}
							hint = edits.lowerBound(pos);
						} else {
							hint = edits.insert(hint, pos, newB);
							++hint;
						}
					}
					seq += take;
				}

				writtenTotal += take;
				cursor += take;
				left -= take;
			}

			checkNeedPromote(pageIndex);
		}
	}

	if(writtenTotal > 0) {
		recordOverwrite(logicalOffset, writtenTotal);
	}
	return writtenTotal;
}

//проверяет наличие любых модификаций содержимого или геометрии
bool PagedPieceOverlay::hasAnyModification() const {
	if(m_logicalSize != m_base.size()) {
		return true;
	}
	return !enumerateOverwriteSpans().isEmpty();
}

//проверяет модифицированность одного байта с учетом full и sparse
bool PagedPieceOverlay::isModified(qint64 logicalOffset) const {
	if(logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return false;
	}

	const qint64 pageIndex = logicalPageIndex(logicalOffset);
	const int inPage = inLogicalPage(logicalOffset);

	qint64 inPieceOffset = 0;
	const int pieceIndex = findPiece(logicalOffset, &inPieceOffset);
	if(pieceIndex >= m_pieces.size()) {
		return false;
	}
	const Piece& piece = m_pieces[pieceIndex];
	if(piece.m_src == PieceSrc::Add) {
		return true;
	}

	if(const auto itFull = m_fullPages.constFind(pageIndex); itFull != m_fullPages.cend()) {
		buildFullDiffBits(pageIndex);
		{
			QMutexLocker fullLock(&m_fullDiffMutex);
			if(const auto itBits = m_fullDiffBits.constFind(pageIndex); itBits != m_fullDiffBits.cend()) {
				const QBitArray& bits = itBits.value();
				const int bitIndex = inPage;
				return bitIndex >= 0 && bitIndex < bits.size() ? bits.testBit(bitIndex) : true;
			}
		}
		return true;
	}

	if(const auto itPg = m_patchPages.constFind(pageIndex); itPg != m_patchPages.cend()) {
		return itPg->m_edits.contains(inPage);
	}

	return false;
}

//пытается свернуть недавно вставленный диапазон в базовый кусок при полном совпадении
bool PagedPieceOverlay::tryFoldInsertedAsBase(qint64 logicalOffset, qint64 length) {
	if(length <= 0 || logicalOffset < 0 || logicalOffset + length > m_logicalSize) {
		return false;
	}

	const int iStart = splitAt(logicalOffset);
	const int iEnd = splitAt(logicalOffset + length);
	if(iStart < 0 || iStart > iEnd || iEnd > m_pieces.size()) {
		return false;
	}

	bool hasOnlyAdd = true;
	for(int i = iStart; i < iEnd; ++i) {
		if(m_pieces[i].m_src != PieceSrc::Add) {
			hasOnlyAdd = false;
			break;
		}
	}
	if(!hasOnlyAdd) {
		return false;
	}

	qint64 anchorSrc = -1;
	if(iStart - 1 >= 0 && m_pieces[iStart - 1].m_src == PieceSrc::Base) {
		anchorSrc = m_pieces[iStart - 1].m_srcOff + m_pieces[iStart - 1].m_len;
	}
	if(iEnd < m_pieces.size() && m_pieces[iEnd].m_src == PieceSrc::Base) {
		const qint64 rightAnchor = m_pieces[iEnd].m_srcOff - length;
		if(anchorSrc < 0) {
			anchorSrc = rightAnchor;
		}
	}
	if(anchorSrc < 0 || anchorSrc + length > m_base.size()) {
		return false;
	}

	QByteArray inserted; inserted.resize(int(length));
	if(readRange(logicalOffset, length, inserted.data()) != length) {
		return false;
	}

	QByteArray baseBuf; baseBuf.resize(int(length));
	if(m_base.readRange(anchorSrc, length, baseBuf.data()) != length) {
		return false;
	}

	if(inserted.isEmpty() || baseBuf.isEmpty()) {
		return false;
	}

	const bool equalAll = (::memcmp(inserted.constData(), baseBuf.constData(), size_t(length)) == 0);

	m_pieces.erase(m_pieces.begin() + iStart, m_pieces.begin() + iEnd);

	Piece basePiece;
	basePiece.m_src = PieceSrc::Base;
	basePiece.m_srcOff = anchorSrc;
	basePiece.m_len = length;
	m_pieces.insert(m_pieces.begin() + iStart, basePiece);

	mergeAround(iStart);
	markPiecesChanged(iStart);

	if(!equalAll) {
		for(qint64 i = 0; i < length; ++i) {
			const quint8 bIns = quint8(uchar(inserted[int(i)]));
			const quint8 bBase = quint8(uchar(baseBuf[int(i)]));
			if(bIns != bBase) {
				const qint64 absPos = logicalOffset + i;
				const qint64 pageIndex = logicalPageIndex(absPos);
				const int inPage = inLogicalPage(absPos);
				m_patchPages[pageIndex].m_edits.insert(inPage, bIns);
			}
		}
	}
	return true;
}

//очищает все full страницы и связанные карты отличий
void PagedPieceOverlay::dropFullPages() {
	m_fullPages.clear();
	{
		QMutexLocker fullLock(&m_fullDiffMutex);
		m_fullDiffBits.clear();
		m_fullDiffCount.clear();
	}
}

//актуализирует full страницу при изменении длины логической страницы
bool PagedPieceOverlay::updateFull(qint64 pageIndex) {
	auto it = m_fullPages.find(pageIndex);
	if(it == m_fullPages.end()) {
		return false;
	}

	const qint64 wantLen = logicalPageLen(pageIndex);
	if(wantLen <= 0) {
		m_fullPages.erase(it);
		return false;
	}

	const PageHandle current = it.value();
	if(current.m_size == wantLen) {
		return true;
	}

	QByteArray oldBuf;
	oldBuf.resize(qsizetype(current.m_size));
	if(current.m_size > 0) {
		const qint64 got = m_store->read(current, 0, current.m_size, oldBuf.data());
		Q_UNUSED(got);
	}

	QByteArray newBuf;
	newBuf.resize(qsizetype(wantLen));
	const qint64 copyLen = std::min<qint64>(wantLen, current.m_size);

	if(copyLen > 0) {
		::memcpy(newBuf.data(), oldBuf.constData(), size_t(copyLen));
	}

	if(wantLen > current.m_size) {
		const qint64 baseAbs = logicalPageBase(pageIndex);
		const qint64 tailAbs = baseAbs + copyLen;
		const qint64 tailLen = wantLen - copyLen;

		qint64 remaining = tailLen;
		qint64 cursor = tailAbs;
		qint64 copied = 0;
		while(remaining > 0) {
			qint64 inPieceOffset = 0;
			const int pieceIndex = findPiece(cursor, &inPieceOffset);
			if(pieceIndex >= m_pieces.size()) {
				break;
			}

			const Piece& piece = m_pieces[pieceIndex];
			const qint64 take = std::min<qint64>(remaining, piece.m_len - inPieceOffset);
			const qint64 got = readFromPiece(piece, inPieceOffset, take, newBuf.data() + copyLen + copied);
			if(got <= 0) {
				break;
			}

			copied += got;
			cursor += got;
			remaining -= got;
		}
		applyLogicalPatches(tailAbs, copied, newBuf.data() + copyLen, true);
	}

	PageHandle newHandle = m_store->allocate(newBuf.size(), newBuf.constData());
	if(!newHandle.isValid()) {
		return false;
	}
	it.value() = newHandle;

	{
		QMutexLocker fullLock(&m_fullDiffMutex);
		m_fullDiffBits.remove(pageIndex);
		m_fullDiffCount.remove(pageIndex);
	}

	return true;
}

//строит битсет отличий для full страницы относительно базы
void PagedPieceOverlay::buildFullDiffBits(qint64 pageIndex) const {
	if(!m_fullPages.contains(pageIndex)) {
		return;
	}

	{
		QMutexLocker fullLock(&m_fullDiffMutex);
		if(m_fullDiffBits.contains(pageIndex)) {
			return;
		}
	}

	const PageHandle fullHandle = m_fullPages.value(pageIndex);
	const qint64 pageLength = logicalPageLen(pageIndex);
	if(pageLength <= 0) {
		return;
	}

	QBitArray localBits(int(pageLength), false);
	int localDiffCount = 0;

	const qint64 pageBaseAbsolute = logicalPageBase(pageIndex);
	qint64 remainingBytes = pageLength;
	qint64 cursorAbsolute = pageBaseAbsolute;

	while(remainingBytes > 0) {
		qint64 inPieceOffset = 0;
		const int pieceIndex = findPiece(cursorAbsolute, &inPieceOffset);
		if(pieceIndex >= m_pieces.size()) {
			break;
		}

		const Piece& currentPiece = m_pieces[pieceIndex];
		const qint64 maxTakeFromPiece = std::min<qint64>(
			currentPiece.m_len - inPieceOffset,
			pageBaseAbsolute + pageLength - cursorAbsolute);
		const int indexInPage = inLogicalPage(cursorAbsolute);

		if(currentPiece.m_src == PieceSrc::Add) {
			for(qint64 byteIndex = 0; byteIndex < maxTakeFromPiece; ++byteIndex) {
				const int bitIndex = indexInPage + int(byteIndex);
				if(bitIndex >= 0 && bitIndex < localBits.size() && !localBits.testBit(bitIndex)) {
					localBits.setBit(bitIndex, true);
					++localDiffCount;
				}
			}
		} else {
			QByteArray fullBuffer(int(maxTakeFromPiece), Qt::Uninitialized);
			QByteArray baseBuffer(int(maxTakeFromPiece), Qt::Uninitialized);

			const bool okFull = (m_store->read(fullHandle, indexInPage, maxTakeFromPiece, fullBuffer.data()) == maxTakeFromPiece);
			const bool okBase = (m_base.readRange(currentPiece.m_srcOff + inPieceOffset, maxTakeFromPiece, baseBuffer.data()) == maxTakeFromPiece);

			for(qint64 byteIndex = 0; byteIndex < maxTakeFromPiece; ++byteIndex) {
				const int bitIndex = indexInPage + int(byteIndex);
				if(bitIndex < 0 || bitIndex >= localBits.size()) {
					continue;
				}

				bool isDifferent = true;
				if(okFull && okBase) {
					isDifferent = (quint8(uchar(fullBuffer[int(byteIndex)])) != quint8(uchar(baseBuffer[int(byteIndex)])));
				}
				const bool wasDifferent = localBits.testBit(bitIndex);
				if(isDifferent != wasDifferent) {
					localBits.setBit(bitIndex, isDifferent);
					localDiffCount += isDifferent ? +1 : -1;
				}
			}
		}

		cursorAbsolute += maxTakeFromPiece;
		remainingBytes -= maxTakeFromPiece;
	}

	{
		QMutexLocker fullLock(&m_fullDiffMutex);
		if(!m_fullDiffBits.contains(pageIndex)) {
			m_fullDiffBits.insert(pageIndex, std::move(localBits));
			m_fullDiffCount.insert(pageIndex, localDiffCount);
		}
	}
}

//обновляет битсет отличий full страницы на записанном отрезке
void PagedPieceOverlay::updateFullDiffBitsOnWrite(qint64 pageIndex, qint64 logicalStart, qint64 length) {
	if(!m_fullPages.contains(pageIndex) || length <= 0) {
		return;
	}

	buildFullDiffBits(pageIndex);

	QMutexLocker fullLock(&m_fullDiffMutex);

	auto itBits = m_fullDiffBits.find(pageIndex);
	auto itCnt = m_fullDiffCount.find(pageIndex);
	if(itBits == m_fullDiffBits.end() || itCnt == m_fullDiffCount.end()) {
		return;
	}

	QBitArray& bits = itBits.value();
	int& diffCount = itCnt.value();

	const qint64 pageBase = logicalPageBase(pageIndex);
	const qint64 pageLen = logicalPageLen(pageIndex);
	const qint64 endAbs = std::min<qint64>(logicalStart + length, pageBase + pageLen);
	qint64 cursor = std::max<qint64>(logicalStart, pageBase);

	const PageHandle handle = m_fullPages.value(pageIndex);

	while(cursor < endAbs) {
		qint64 inPieceOffset = 0;
		const int pieceIndex = findPiece(cursor, &inPieceOffset);

		if(pieceIndex >= m_pieces.size()) {
			break;
		}

		const Piece& piece = m_pieces[pieceIndex];
		const qint64 take = std::min<qint64>(piece.m_len - inPieceOffset, endAbs - cursor);
		const int inPage = inLogicalPage(cursor);

		if(piece.m_src == PieceSrc::Add) {
			for(qint64 i = 0; i < take; ++i) {
				const int bit = inPage + int(i);

				if(bit < 0 || bit >= bits.size()) {
					continue;
				}

				if(!bits.testBit(bit)) {
					bits.setBit(bit, true);
					++diffCount;
				}
			}
		} else {
			QByteArray fullBuf(int(take), Qt::Uninitialized);
			QByteArray baseBuf(int(take), Qt::Uninitialized);
			const bool okFull = (m_store->read(handle, inPage, take, fullBuf.data()) == take);
			const bool okBase = (m_base.readRange(piece.m_srcOff + inPieceOffset, take, baseBuf.data()) == take);

			for(qint64 i = 0; i < take; ++i) {
				const int bit = inPage + int(i);

				if(bit < 0 || bit >= bits.size()) {
					continue;
				}

				bool newDiff = true;
				if(okFull && okBase) {
					newDiff = (quint8(uchar(fullBuf[int(i)])) != quint8(uchar(baseBuf[int(i)])));
				}
				const bool oldDiff = bits.testBit(bit);
				if(newDiff != oldDiff) {
					bits.setBit(bit, newDiff);
					diffCount += newDiff ? +1 : -1;
				}
			}
		}

		cursor += take;
	}
	checkDemoteIfClean(pageIndex);
}

//демоутит full страницу если отличий не осталось
void PagedPieceOverlay::checkDemoteIfClean(qint64 pageIndex) {
	QMutexLocker fullLock(&m_fullDiffMutex);
	const int differences = m_fullDiffCount.value(pageIndex, -1);
	if(differences != 0) {
		return;
	}
	m_fullPages.remove(pageIndex);
	m_fullDiffBits.remove(pageIndex);
	m_fullDiffCount.remove(pageIndex);
}

//записывает диапазон перезаписи для отчетности и склейки
void PagedPieceOverlay::recordOverwrite(qint64 logicalOffset, qint64 length) {
	if(length <= 0) {
		return;
	}
	m_overwriteSpans.push_back(OverwriteSpan{logicalOffset, length});
}

//склеивает пересекающиеся или соседние интервалы перезаписи
QVector<OverwriteSpan> PagedPieceOverlay::mergeSpans(QVector<OverwriteSpan> spans) {
	if(spans.isEmpty()) {
		return spans;
	}

	std::sort(spans.begin(), spans.end(),
		[](const OverwriteSpan& a, const OverwriteSpan& b) {
			return a.off < b.off;
		}
	);

	QVector<OverwriteSpan> merged; merged.reserve(spans.size());
	OverwriteSpan current = spans.front();
	for(int i = 1; i < spans.size(); ++i) {
		const auto& s = spans[i];
		if(s.off <= current.off + current.len) {
			const qint64 newEnd = std::max(current.off + current.len, s.off + s.len);
			current.len = newEnd - current.off;
		} else {
			merged.push_back(current);
			current = s;
		}
	}
	merged.push_back(current);
	return merged;
}

//возвращает интервалы модификаций с учетом add sparse и full
QVector<OverwriteSpan> PagedPieceOverlay::enumerateOverwriteSpans() const {
	QVector<OverwriteSpan> spans;

	//вставки целиком модифицированы
	{
		qint64 logicalBase = 0;
		for(int i = 0; i < m_pieces.size(); ++i) {
			const Piece& p = m_pieces[i];
			if(p.m_src == PieceSrc::Add) {
				spans.push_back(OverwriteSpan{logicalBase, p.m_len});
			}
			logicalBase += p.m_len;
		}
	}

	//sparse патчи для страниц без full
	for(auto it = m_patchPages.constBegin(); it != m_patchPages.constEnd(); ++it) {
		const qint64 pageIndex = it.key();
		if(m_fullPages.contains(pageIndex)) {
			continue;
		}

		const auto& edits = it->m_edits;
		if(edits.isEmpty()) {
			continue;
		}

		const qint64 pageBase = logicalPageBase(pageIndex);
		qint64 runStart = -1;
		int prevKey = -2;

		for(auto ie = edits.cbegin(); ie != edits.cend(); ++ie) {
			const int key = ie.key();
			if(runStart < 0) {
				runStart = key;
				prevKey = key;
			} else if(key == prevKey + 1) {
				prevKey = key;
			} else {
				spans.push_back(OverwriteSpan{pageBase + runStart, qint64(prevKey - runStart + 1)});
				runStart = key;
				prevKey = key;
			}
		}
		if(runStart >= 0) {
			spans.push_back(OverwriteSpan{pageBase + runStart, qint64(prevKey - runStart + 1)});
		}
	}

	//full страницы учитываются по битам отличий
	for(auto itF = m_fullPages.constBegin(); itF != m_fullPages.constEnd(); ++itF) {
		const qint64 pageIndex = itF.key();
		const qint64 pageBase = logicalPageBase(pageIndex);
		const qint64 pageLen = logicalPageLen(pageIndex);
		if(pageLen <= 0) {
			continue;
		}

		buildFullDiffBits(pageIndex);
		{
			QMutexLocker fullLock(&m_fullDiffMutex);
			if(const auto itBits = m_fullDiffBits.constFind(pageIndex); itBits != m_fullDiffBits.cend()) {
				const QBitArray& bits = itBits.value();
				qint64 runStart = -1;
				const int bitsLen = std::min<int>(bits.size(), int(pageLen));
				for(int i = 0; i < bitsLen; ++i) {
					if(bits.testBit(i)) {
						if(runStart < 0) {
							runStart = pageBase + i;
						}
					} else if(runStart >= 0) {
						spans.push_back(OverwriteSpan{runStart, (pageBase + i) - runStart});
						runStart = -1;
					}
				}
				if(runStart >= 0) {
					spans.push_back(OverwriteSpan{runStart, (pageBase + bitsLen) - runStart});
				}
			}
		}
	}

	return mergeSpans(std::move(spans));
}

//делит кусок в точке логического смещения и возвращает индекс правого куска
int PagedPieceOverlay::splitAt(qint64 logicalOffset) {
	if(logicalOffset <= 0) {
		return 0;
	}
	if(logicalOffset >= m_logicalSize) {
		return m_pieces.size();
	}

	qint64 inPieceOffset = 0;
	const int pieceIndex = findPiece(logicalOffset, &inPieceOffset);
	if(pieceIndex >= m_pieces.size()) {
		return m_pieces.size();
	}
	if(inPieceOffset == 0) {
		return pieceIndex;
	}

	const Piece current = m_pieces[pieceIndex];
	Piece left = current; left.m_len = inPieceOffset;
	Piece right = current; right.m_srcOff += inPieceOffset; right.m_len -= inPieceOffset;

	m_pieces[pieceIndex] = left;
	m_pieces.insert(pieceIndex + 1, right);

	markPiecesChanged(pieceIndex);
	return pieceIndex + 1;
}

//вставляет готовый кусок в позицию и обновляет состояние
void PagedPieceOverlay::insertPieceAt(qint64 logicalOffset, Piece newPiece) {
	const int pieceIndex = splitAt(logicalOffset);
	m_pieces.insert(pieceIndex, newPiece);
	m_logicalSize += newPiece.m_len;

	mergeAround(pieceIndex);
	markPiecesChanged(pieceIndex);
}

//сдвигает sparse патчи правее указанной позиции на дельту
void PagedPieceOverlay::shiftPatches(qint64 fromLogical, qint64 delta) {
	if(delta == 0 || m_patchPages.isEmpty()) {
		return;
	}

	QHash<qint64, SparsePage> newPages;
	for(auto itPage = m_patchPages.begin(); itPage != m_patchPages.end(); ++itPage) {
		const qint64 pageIndex = itPage.key();
		const qint64 pageBase = logicalPageBase(pageIndex);
		SparsePage& sparse = itPage.value();

		for(auto it = sparse.m_edits.cbegin(); it != sparse.m_edits.cend(); ++it) {
			const qint64 absPos = pageBase + it.key();
			if(absPos < fromLogical) {
				newPages[pageIndex].m_edits.insert(it.key(), it.value());
			} else {
				const qint64 newAbs = absPos + delta;
				if(newAbs < 0) {
					continue;
				}
				const qint64 newPage = newAbs / m_logicalPageSize;
				const int newIn = int(newAbs % m_logicalPageSize);
				newPages[newPage].m_edits.insert(newIn, it.value());
			}
		}
	}
	m_patchPages.swap(newPages);

	dropFullPages();
}

//удаляет sparse патчи на заданном логическом отрезке
void PagedPieceOverlay::erasePatches(qint64 logicalOffset, qint64 length) {
	if(length <= 0 || m_patchPages.isEmpty()) {
		return;
	}
	const qint64 endAbs = logicalOffset + length;

	for(auto itPage = m_patchPages.begin(); itPage != m_patchPages.end(); ) {
		const qint64 pageIndex = itPage.key();
		const qint64 baseAbs = logicalPageBase(pageIndex);
		SparsePage& sparse = itPage.value();

		auto it = sparse.m_edits.lowerBound(int(std::max<qint64>(0, logicalOffset - baseAbs)));
		while(it != sparse.m_edits.end()) {
			const qint64 abs = baseAbs + it.key();
			if(abs >= endAbs) {
				break;
			}
			it = sparse.m_edits.erase(it);
		}

		if(sparse.m_edits.isEmpty()) {
			itPage = m_patchPages.erase(itPage);
		} else {
			++itPage;
		}
	}

	dropFullPages();
}

//возвращает список кусков для сериализации состояния
QVector<PagedPieceOverlay::PieceInfo> PagedPieceOverlay::enumeratePieces() const {
	QVector<PieceInfo> pieceList;
	pieceList.reserve(m_pieces.size());
	for(const Piece& piece : m_pieces) {
		PieceInfo info;
		if(piece.m_src == PieceSrc::Base) {
			info.isBase = true;
			info.srcOffset = piece.m_srcOff;
			info.length = piece.m_len;
		} else {
			info.isBase = false;
			info.length = piece.m_len;
			if(info.length > 0) {
				info.insertBytes.resize(int(info.length));
				const qint64 readBytes = m_store->read(piece.m_handle, 0, info.length, info.insertBytes.data());
				if(readBytes != info.length) {
					info.insertBytes.clear();
				}
			}
		}
		pieceList.push_back(std::move(info));
	}
	return pieceList;
}

//возвращает список full страниц с их байтами
QVector<PagedPieceOverlay::FullPageInfo> PagedPieceOverlay::listFullPages() const {
	QVector<FullPageInfo> pages;
	if(m_fullPages.isEmpty()) {
		return pages;
	}

	QVector<qint64> pageKeys;
	pageKeys.reserve(m_fullPages.size());

	for(auto it = m_fullPages.constBegin(); it != m_fullPages.constEnd(); ++it) {
		pageKeys.push_back(it.key());
	}

	std::sort(pageKeys.begin(), pageKeys.end());
	pages.reserve(pageKeys.size());

	for(qint64 pageIndex : pageKeys) {
		const PageHandle handle = m_fullPages.value(pageIndex);
		FullPageInfo info;
		info.pageIndex = pageIndex;
		info.pageBytes.resize(int(handle.m_size));

		if(handle.m_size > 0) {
			m_store->read(handle, 0, handle.m_size, info.pageBytes.data());
		}

		pages.push_back(std::move(info));
	}
	return pages;
}

//возвращает список sparse страниц с патчами
QVector<PagedPieceOverlay::SparsePageInfo> PagedPieceOverlay::listSparsePages() const {
	QVector<SparsePageInfo> pages;
	if(m_patchPages.isEmpty()) {
		return pages;
	}

	QVector<qint64> pageKeys;
	pageKeys.reserve(m_patchPages.size());

	for(auto it = m_patchPages.constBegin(); it != m_patchPages.constEnd(); ++it) {
		pageKeys.push_back(it.key());
	}

	std::sort(pageKeys.begin(), pageKeys.end());
	pages.reserve(pageKeys.size());

	for(qint64 pageIndex : pageKeys) {
		const SparsePage& page = m_patchPages.value(pageIndex);
		SparsePageInfo info;
		info.pageIndex = pageIndex;
		info.edits.reserve(page.m_edits.size());

		for(auto itEdit = page.m_edits.cbegin(); itEdit != page.m_edits.cend(); ++itEdit) {
			info.edits.push_back(SparseEdit{quint32(itEdit.key()), quint8(itEdit.value())});
		}
		pages.push_back(std::move(info));
	}
	return pages;
}

//готовит структуру к импортному восстановлению состояния
bool PagedPieceOverlay::beginImport() {
	m_pieces.clear();
	m_patchPages.clear();
	dropFullPages();
	m_fullDiffBits.clear();
	m_fullDiffCount.clear();
	m_overwriteSpans.clear();
	m_structuralChanges = false;

	m_logicalSize = 0;

	markPiecesChanged(0);
	return true;
}

//добавляет базовый фрагмент при импорте
bool PagedPieceOverlay::appendBaseSlice(qint64 srcOffset, qint64 length) {
	if(length <= 0) {
		return true;
	}

	Piece piece;
	piece.m_src = PieceSrc::Base;
	piece.m_srcOff = srcOffset;
	piece.m_len = length;
	m_pieces.push_back(piece);
	m_logicalSize += length;

	mergeAround(int(m_pieces.size()) - 1);
	markPiecesChanged(int(m_pieces.size()) - 1);

	return true;
}

//добавляет вставочный фрагмент при импорте
bool PagedPieceOverlay::appendAddSlice(const QByteArray& data) {
	if(data.isEmpty()) {
		return true;
	}

	PageHandle handle = m_store->allocate(data.size(), data.constData());
	if(!handle.isValid()) {
		return false;
	}

	Piece piece;
	piece.m_src = PieceSrc::Add;
	piece.m_srcOff = 0;
	piece.m_len = data.size();
	piece.m_handle = handle;
	m_pieces.push_back(piece);
	m_logicalSize += data.size();

	mergeAround(int(m_pieces.size()) - 1);
	markPiecesChanged(int(m_pieces.size()) - 1);

	return true;
}

//восстанавливает full страницу при импорте
bool PagedPieceOverlay::restoreFullPage(qint64 pageIndex, const QByteArray& pageBytes) {
	if(pageBytes.isEmpty()) {
		m_fullPages.remove(pageIndex);
		m_fullDiffBits.remove(pageIndex);
		m_fullDiffCount.remove(pageIndex);
		return true;
	}

	PageHandle handle = m_store->allocate(pageBytes.size(), pageBytes.constData());
	if(!handle.isValid()) {
		return false;
	}

	m_fullPages.insert(pageIndex, handle);
	m_patchPages.remove(pageIndex);
	m_fullDiffBits.remove(pageIndex);
	m_fullDiffCount.remove(pageIndex);
	return true;
}

//восстанавливает sparse страницу при импорте
bool PagedPieceOverlay::restoreSparsePage(qint64 pageIndex, const QVector<QPair<quint32, quint8>>& edits) {
	m_fullPages.remove(pageIndex);
	m_fullDiffBits.remove(pageIndex);
	m_fullDiffCount.remove(pageIndex);
	SparsePage& page = m_patchPages[pageIndex];
	page.m_edits.clear();
	for(const auto& editPair : edits) {
		page.m_edits.insert(int(editPair.first), editPair.second);
	}
	return true;
}

//завершает импорт и актуализирует флаги
bool PagedPieceOverlay::finalizeAfterImport() {
	m_structuralChanges = (m_logicalSize != m_base.size());
	markPiecesChanged(0);
	return true;
}

//копирует диапазон последовательно обходя куски
qint64 PagedPieceOverlay::copyFromPieces(qint64 logicalOffset, qint64 length, char* dst) const {
	if(!dst || length <= 0 || logicalOffset < 0 || logicalOffset >= m_logicalSize) {
		return 0;
	}
	const qint64 maxLen = std::min<qint64>(length, m_logicalSize - logicalOffset);

	qint64 remaining = maxLen;
	qint64 cursor = logicalOffset;
	qint64 copied = 0;
	while(remaining > 0) {
		qint64 inPieceOffset = 0;
		const int pieceIndex = findPiece(cursor, &inPieceOffset);
		if(pieceIndex >= m_pieces.size()) {
			break;
		}

		const Piece& piece = m_pieces[pieceIndex];
		const qint64 take = std::min<qint64>(remaining, piece.m_len - inPieceOffset);
		const qint64 got = readFromPiece(piece, inPieceOffset, take, dst + copied);
		if(got <= 0) {
			break;
		}

		copied += got;
		cursor += got;
		remaining -= got;
	}
	return copied;
}
