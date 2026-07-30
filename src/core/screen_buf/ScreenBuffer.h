#pragma once
#include "HexEditorTypes.h"
#include <QtCore>
#include <vector>

struct BlockInfo;
class BlockManager;

class ScreenBuffer {
public:
	ScreenBuffer(BlockManager * bm);

	//возвращает размер видимой области
	qint64 size() const { return m_windowLength; }

	//возвращает данные о строке по индексу
	rowInfo at(qint32 idx);

	//сдвигает окно на pageStartLine если возможно
	void updateView(qint64 pageStartLine, qint32 viewPortLines);

	//обновляет карту файла полностью
	void updateDataMap(const qint32 bytesPerRow, const qint64 dataSize);

	//форсированный пересчёт
	void invalidate(qint64 pageStartLine, qint32 viewPortLines);

	//возвращает количество строк для всего файла
	qint64 getTotalRows() const { return m_totalRows; }

	//возвращает линии, на которых расположены начальный и конечный маркер (хэдер
	//и футер) блока с blockId
	std::pair<qint64, qint64> getLineNumsOfMarkers(qint32 blockId);

	qint64 offsetToLine(qint64 offset);
	std::optional<std::pair<qint64, qint64>>  getScreenOffsetsLimits();
	std::pair<qint64, qint32> onScreenRowCol(qint64 offset);
	bool isOnScreen(qint64 offset);

	bool empty() { return (m_dataMap.empty() || m_screenRows.empty()); }
private:

	const qint32 CACHE_LINES = 150;

	void processPartialFooters(const BlockInfo& blockInfo, qint64 endOffset, std::vector<FileBlockInfo>& added);
	void processPartialHeaders(const BlockInfo& blockInfo, qint64 endOffset, std::vector<FileBlockInfo>& added);
	qint64 calcNewEndCollapsed(const BlockInfo& blockInfo);

	qint64 fillRowsInfo(qint64 pageStartLine, qint32 rowsCount);
	std::vector<rowInfo> m_screenRows{};
	std::vector<FileBlockInfo> m_dataMap{};

	qint64 m_windowStartIdx{};
	qint32 m_windowLength{};

	qint64 m_cachedStartLine{};
	qint32 m_cachedLength{};

	qint32 m_viewportLines{};
	qint32 m_bytesPerRow{};

	qint64 m_totalRows{};
	qint64 m_dataSize{};

	BlockManager* m_blockManager{nullptr};
};
