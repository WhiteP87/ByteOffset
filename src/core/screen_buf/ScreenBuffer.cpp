#include "ScreenBuffer.h"
#include "BlockManager.h"
#include <algorithm>

//строит вектор отображаемых строк на экране
qint64 ScreenBuffer::fillRowsInfo(qint64 pageStartLine, qint32 rowsCount) {
	m_screenRows.clear();
	qint64 totalAdded = 0;

	auto it = std::lower_bound(
		m_dataMap.begin(), m_dataMap.end(),
		pageStartLine,
		[](const FileBlockInfo& bi, qint64 line) {
			return bi.lastLine < line;
		});

	for(; it != m_dataMap.end() && totalAdded < rowsCount; ++it) {
		const auto& bi = *it;

		rowType rt{};
		switch(bi.type) {
		case fileBlockType::HeaderBlock:
			rt = rowType::blockHeader; break;
		case fileBlockType::FooterBlock:
			rt = rowType::blockFooter; break;
		case fileBlockType::DataBlock:
			rt = rowType::data; break;
		default:
			break;
		}

		qint64 linesFromStart = (pageStartLine - bi.firstLine);
		qint64 startOffset = 0;
		qint64 endOffset = 0;
		qint64 localAdded = 0;
		for(; localAdded < (bi.lastLine - pageStartLine + 1) && totalAdded < rowsCount; localAdded++, totalAdded++) {
			startOffset = qMax((linesFromStart + localAdded) * m_bytesPerRow + alignDown(bi.startOffset, m_bytesPerRow), bi.startOffset);
			endOffset = qMin((linesFromStart + localAdded) * m_bytesPerRow + m_bytesPerRow - 1 + alignDown(bi.startOffset, m_bytesPerRow), bi.endOffset);
			m_screenRows.emplace_back(
				rowInfo{
					rt,
					alignDown((rt == rowType::blockFooter ? bi.endOffset : startOffset), m_bytesPerRow),
					(rt != rowType::data ? startOffset : startOffset % m_bytesPerRow),
					(rt != rowType::data ? endOffset : endOffset % m_bytesPerRow),
					bi.firstLine,
					bi.blockId,
					bi.level,
					bi.partial,
					bi.collapsed
				});
		}
		pageStartLine += localAdded;
	}
	return totalAdded;
}

ScreenBuffer::ScreenBuffer(BlockManager* bm)
	:m_blockManager{bm} {
	m_screenRows.reserve(CACHE_LINES);
}

rowInfo ScreenBuffer::at(qint32 idx) {
	return m_screenRows[m_windowStartIdx + idx];
}

void ScreenBuffer::updateView(qint64 pageStartLine, qint32 viewPortLines) {
	if(pageStartLine >= m_totalRows) 
		return;
	if(pageStartLine < m_cachedStartLine || (pageStartLine + viewPortLines) > (m_cachedStartLine + m_cachedLength)) {
		invalidate(pageStartLine, viewPortLines);
		return;
	}

	m_windowStartIdx = pageStartLine - m_cachedStartLine;
	m_windowLength = std::min(static_cast<qint64>(viewPortLines), m_totalRows-m_windowStartIdx);
	m_viewportLines = viewPortLines;
}

//строит карту непрерывных блоков файла
void ScreenBuffer::updateDataMap(const qint32 bytesPerRow, const qint64 dataSize) {
	m_dataMap.clear();
	m_bytesPerRow = bytesPerRow;
	m_dataSize = dataSize;

	qint64 lineCounter = 0;
	qint64 prevBlockOffset = 0;

	if(m_blockManager && m_blockManager->isVisible()) {
		const auto& markerMap = m_blockManager->getBlockMarkersMap();

		//проходим по всем смещениям, содержащим маркеры
		for(const auto& [offset, markers] : markerMap) {

			//если предыдущий блок был схлопнут
			if(offset && prevBlockOffset > offset)
				continue;

			//проходим по каждому описателю блоков
			for(const auto& blockInfo : markers) {

				if(prevBlockOffset > offset)
					continue;

				MarkerType bt = blockInfo.type;
				bool collapsed = blockInfo.collapsed;
				qint32 blockId = blockInfo.blockId;
				FileBlockInfo bi{};

				//если перед текущим описателем был неучтенный блок данных, то добавляем
				if(offset > prevBlockOffset) {
					bi.type = fileBlockType::DataBlock;
					bi.startOffset = prevBlockOffset;
					bi.endOffset = offset - 1;
					bi.firstLine = lineCounter;
					bi.lastLine = lineCounter + (bi.endOffset / bytesPerRow - bi.startOffset / bytesPerRow);
					bi.blockId = -1;
					bi.level = -1;
					bi.collapsed = false;
					bi.partial = false;
					m_dataMap.push_back(bi);
					lineCounter = bi.lastLine + 1;
				}


				// не отображаем футеры схлопнутых блоков
				if(bt == MarkerType::BlockEnd) {
					const auto& prev = m_dataMap.back();
					if(collapsed || (prev.collapsed && prev.startOffset <= blockInfo.startOffset)) {
						continue;
					}
				}

				//рассчитываем возможное изменение конечного смещения для
				//блока, в случае если он содержит внутри свернутый блок с конечным смещением
				//больше чем конечное смещение блока, заполняем partial хэдеры и футеры
				std::vector<FileBlockInfo> addedHeaders{};
				std::vector<FileBlockInfo> addedFooters{};
				qint64 newEndOffset = blockInfo.endOffset;
				if(bt == MarkerType::BlockStart && collapsed) {
					newEndOffset = calcNewEndCollapsed(blockInfo);
					prevBlockOffset = newEndOffset + 1;
					processPartialHeaders(blockInfo, newEndOffset, addedHeaders);
					processPartialFooters(blockInfo, newEndOffset, addedFooters);
				} else {
					prevBlockOffset = offset;
				}

				//добавляем partial футеры, если они есть
				for(auto& el : addedFooters) {
					el.firstLine = el.lastLine = lineCounter++;
					m_dataMap.push_back(el);
				}

				//добавляем хэдер/футер для текущего маркера
				bi.type = (bt == MarkerType::BlockEnd) ? (fileBlockType::FooterBlock) : (fileBlockType::HeaderBlock);
				bi.firstLine = bi.lastLine = lineCounter;
				bi.startOffset = blockInfo.startOffset;
				bi.endOffset = newEndOffset;
				bi.blockId = blockId;
				bi.collapsed = collapsed;
				bi.level = blockInfo.level;
				bi.partial = false;
				m_dataMap.push_back(bi);
				lineCounter++;

				//добавляем partial хэдеры, если они есть
				for(auto& el : addedHeaders) {
					el.firstLine = el.lastLine = lineCounter++;
					m_dataMap.push_back(el);
				}
			}
		}
	}

	//если до конца файла остались неучтенные данные
	if(prevBlockOffset < dataSize) {

		auto startOffset = 0ll;
		auto linesToEnd = 0ll;

		if(m_dataMap.empty()) {
			linesToEnd = (dataSize - 1) / bytesPerRow;
			startOffset = 0;
		} else {
			const auto endOffset = prevBlockOffset;
			linesToEnd = ((dataSize - 1) / bytesPerRow) - (endOffset / bytesPerRow);
			startOffset = endOffset;
		}

		m_dataMap.push_back(FileBlockInfo{
			fileBlockType::DataBlock,
			lineCounter,
			lineCounter + linesToEnd,
			startOffset,
			dataSize - 1,
			-1,
			-1,
			false,
			false});
	}

	if(!m_dataMap.empty()) {
		m_totalRows = m_dataMap.back().lastLine + 1;
	} else {
		m_totalRows = 0;
	}
	invalidate(m_cachedStartLine + m_windowStartIdx, m_viewportLines);
}

void ScreenBuffer::invalidate(qint64 pageStartLine, qint32 viewPortLines) {
	const auto bufCenter = CACHE_LINES / 2;

	const qint64 startLine = std::max(0ll, pageStartLine - bufCenter + viewPortLines / 2);
	qint64 filled = fillRowsInfo(startLine, CACHE_LINES);

	m_cachedStartLine = startLine;
	m_cachedLength = filled;

	m_windowStartIdx = pageStartLine - startLine;
	m_windowLength = std::clamp(m_cachedLength - m_windowStartIdx, 0ll, static_cast<qint64>(viewPortLines));

	m_viewportLines = viewPortLines;
}

//рассчет нового смещения конца блока с учетом внутренних свернутых
qint64 ScreenBuffer::calcNewEndCollapsed(const BlockInfo& blockInfo) {
	auto endOffset = blockInfo.endOffset;
	//получаем все стартовые маркеры, находящиеся внутри добавленного блока
	auto internalMarkers = m_blockManager->getMarkersFromTo(MarkerType::BlockStart, blockInfo.startOffset, blockInfo.endOffset);

	//ищем максимально дальний конец внутреннего свернутого блока, в который входит
	//конечное смещение текущего блока
	qint64 findedEnd{blockInfo.endOffset};
	for(const auto& ref : internalMarkers) {
		const BlockInfo& block = ref.get();
		if(block.collapsed && block.endOffset > findedEnd)
			findedEnd = block.endOffset;
	}

	if(findedEnd != endOffset) {
		if(m_blockManager->isOffsetIntoCollapsedBlock(findedEnd)) {
			const auto bid = m_blockManager->findNearPrevCollapsed(findedEnd);
			findedEnd = m_blockManager->getBlockById(bid)->endOffset;
		}
		endOffset = findedEnd;
	}
	return endOffset;
}

//формирует "частичные" футеры для блоков частично скрытых в схлопнутом блоке
void ScreenBuffer::processPartialFooters(const BlockInfo& blockInfo, qint64 endOffset, std::vector<FileBlockInfo>& added) {
	const auto internalMarkers = m_blockManager->getMarkersFromTo(MarkerType::BlockEnd, blockInfo.startOffset, endOffset);

	for(const auto ref : internalMarkers) {
		const BlockInfo& block = ref.get();
		if(block.startOffset < blockInfo.startOffset&& block.endOffset >= blockInfo.startOffset) {
			FileBlockInfo partialFooter{};
			partialFooter.type = fileBlockType::FooterBlock;
			partialFooter.firstLine = partialFooter.lastLine = 0;
			partialFooter.startOffset = block.startOffset;
			partialFooter.endOffset = block.endOffset;
			partialFooter.blockId = block.blockId;
			partialFooter.level = block.level;
			partialFooter.collapsed = block.collapsed;
			partialFooter.partial = true;
			added.push_back(std::move(partialFooter));
		}
	}

	std::sort(added.begin(), added.end(),
		[](const FileBlockInfo& a, const FileBlockInfo& b) {
			return a.startOffset > b.startOffset;
		});
}

//формирует "частичные" заголовки для блоков частично скрытых в схлопнутом блоке
void ScreenBuffer::processPartialHeaders(const BlockInfo& blockInfo, qint64 endOffset, std::vector<FileBlockInfo>& added) {

	const auto internalMarkers = m_blockManager->getMarkersFromTo(MarkerType::BlockStart, blockInfo.startOffset, endOffset);

	for(const auto& ref : internalMarkers) {
		const BlockInfo& block = ref.get();

		if(block.endOffset < endOffset && block.endOffset < blockInfo.endOffset)
			continue;

		if(block.startOffset == blockInfo.startOffset)
			continue;

		const auto [headerMin, headerMax] = m_blockManager->getBoundBlocks(block.startOffset, CollapseFilter::CollapsedOnly);
		const auto [footerMin, footerMax] = m_blockManager->getBoundBlocks(block.endOffset, CollapseFilter::CollapsedOnly);

		if(headerMax && footerMin) {
			if(headerMax->endOffset >= footerMin->startOffset && headerMax->blockId != block.blockId && footerMin->blockId != block.blockId)
				continue;

			//if(headerMax->blockId == block.blockId && footerMin->blockId == block.blockId)
			if(footerMax->blockId != block.blockId && footerMax->startOffset <= headerMin->endOffset)
				continue;
		}

		FileBlockInfo partialHeader{};
		partialHeader.type = fileBlockType::HeaderBlock;
		partialHeader.firstLine = partialHeader.lastLine = 0;
		partialHeader.startOffset = block.startOffset;
		partialHeader.endOffset = block.endOffset;
		partialHeader.blockId = block.blockId;
		partialHeader.level = block.level;
		partialHeader.collapsed = block.collapsed;
		partialHeader.partial = true;
		added.push_back(std::move(partialHeader));
	}

	std::sort(added.begin(), added.end(),
		[](const FileBlockInfo& a, const FileBlockInfo& b) {
			return a.endOffset > b.endOffset; // убывание
			/*return a.startOffset < b.startOffset;*/
		});
}

std::pair<qint64, qint64> ScreenBuffer::getLineNumsOfMarkers(qint32 blockId) {
	qint64 startMarker{-1}, endMarker{-1};
	for(const auto& desc : m_dataMap) {
		if(desc.blockId == blockId) {
			if(desc.type == fileBlockType::HeaderBlock) {
				startMarker = desc.firstLine;
				if(desc.collapsed) break;
			}
			endMarker = desc.firstLine;
		}
	}
	return std::make_pair(startMarker, endMarker);
}

//возвращает номер строки (с начала файла) в которой находится смещение
//если смещение скрыто в схлопнутом блоке - возвращает -1
qint64 ScreenBuffer::offsetToLine(qint64 offset) {

	if(offset < 0 || offset >= m_dataSize)
		throw std::invalid_argument("Offset to line: invalid offset");

	if(m_blockManager->isOffsetIntoCollapsedBlock(offset))
		return -1;

	for(const auto& block : m_dataMap) {
		if(block.type == fileBlockType::DataBlock) {
			if(offset >= block.startOffset && offset <= block.endOffset) {
				const auto lines = offset / m_bytesPerRow - block.startOffset / m_bytesPerRow;
				return block.firstLine + lines;
			}
		}
	}
	return -1;
}

//возвращает пару <начальное смещение, конечное смещение> для текущего экрана
std::optional<std::pair<qint64, qint64>>  ScreenBuffer::getScreenOffsetsLimits() {

	qint32 i = 0;
	while(i < m_windowLength && at(i).type != rowType::data) ++i;

	if(i >= m_windowLength)
		return std::nullopt;

	qint32 c = static_cast<qint32>(m_windowLength - 1);
	while(c > i && at(c).type != rowType::data) --c;

	const auto& firstDataRow = at(i);
	const auto& lastDataRow = at(c);

	return std::make_pair(firstDataRow.rowOffset + firstDataRow.startColumn, lastDataRow.rowOffset + lastDataRow.endColumn);
}

//возвращает позицию смещения на экране в виде пары (дельта строки относительно начальной 
//строки страницы, столбец). Если в свернутом блоке то -1,-1
std::pair<qint64, qint32> ScreenBuffer::onScreenRowCol(qint64 offset) {
	if(offset < 0 || offset >= m_dataSize || m_blockManager->isOffsetIntoCollapsedBlock(offset)) {
		return std::make_pair(-1, -1);
	}

	return std::make_pair(offsetToLine(offset) - (m_cachedStartLine + m_windowStartIdx), offset % m_bytesPerRow);
}

//true, если в данный момент смещение на экране
bool ScreenBuffer::isOnScreen(qint64 offset) {
	const auto limits = getScreenOffsetsLimits();
	if(limits.has_value()) {
		return offset >= limits->first && offset <= limits->second;
	}
	return false;
}