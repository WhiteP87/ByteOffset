#pragma once
#include <map>
#include <QString>
#include <QColor>
#include <functional>
#include <optional>
#include <QReadWriteLock>   // добавлено для многопотока
#include "IDataProvider.h"
#include "IBinaryConfig.h"
#include "BlockManagerTypes.h"

class BlockManager final: public IBinaryConfig {
public:
	//добавление	
	qint32 addBlock(qint64 start, qint64 end, const QString& name = "", const QString& comment = "", BlockType type = BlockType::raw, bool isBE = false,
		const QColor& color = Qt::black, bool collapsed = false, qint32 blockId = 0);

	qint32 addBlock(const BlockInfo& blockMeta);

	bool editBlock(qint32 blockId, qint64 start, qint64 end, const QString& name,
		const QString& comment, BlockType type, bool isBE, const QColor& color, bool collapsed);

	bool editBlock(const BlockInfo& blockMeta);

	//удаление
	void removeBlockById(qint32 id);
	void clear(bool invokeable = true);

	//коллбэки
	using callbackType = std::function<void(const BlockChangeData&)>;
	using callbackID = qint32;
	callbackID registerChangedCallback(callbackType cb);
	void unregisterChangedCallback(callbackID cbId);

	//свернутость
	bool isCollapsed(qint32 blockId) const;
	bool collapseBlock(qint32 blockId) { return setBlockCollapse(blockId, true); }
	bool expandBlock(qint32 blockId) { return setBlockCollapse(blockId, false); }
	qint32 expandAllToOffset(qint64 offset);
	void toggleCollapse(qint32 blockId);
	bool isOffsetIntoCollapsedBlock(qint64 offset);
	qint32 findNearPrevCollapsed(qint64 offset);

	//получение состояния
	qint32 getMaxLevel() { QReadLocker lock(&m_dataLock); return m_maxLevel; }
	bool hasBlocks() const { return !m_blockMarkers.empty(); }
	bool hasBlockMarkerAt(qint64 offset) const;
	bool isVisible() const { return m_visible; }

	//установка состояния
	void setDataSize(qint64 dataSize) { QWriteLocker lock(&m_dataLock); m_dataSize = dataSize; }
	void setNotifying(bool enable) { QWriteLocker lock(&m_dataLock); m_callbacksEnabled = enable; }
	void setVisible(bool visibleState) {
		if(m_visible != visibleState) {
			m_visible = visibleState; 
			invokeChangedCallbacks({BlockEventKind::StateChanged,0});
		}
	}

	//доступ
	const std::map<qint64, std::vector<BlockInfo>>& getBlockMarkersMap() const { return m_blockMarkers; }
	const std::vector<BlockInfo>& getBlocksAt(qint64 offset) const;
	const BlockInfo* getBlockById(qint32 id, MarkerType type = MarkerType::BlockStart) const;
	std::vector<std::reference_wrapper<const BlockInfo>> getMarkersFromTo(MarkerType type,
		qint64 offsetFrom = 0, qint64 offsetTo = std::numeric_limits<qint64>::max()) const;
	std::vector<const BlockInfo*> collectStartBlocksCovering(qint64 offset) const;
	std::pair<const BlockInfo*, const BlockInfo*> getBoundBlocks(qint64 offset, CollapseFilter filter) const;

	void onInsert(qint64 at, qint64 len);
	void onRemove(qint64 at, qint64 len);

	//IBinaryConfig реализация
	BinaryConfigDescriptor configDescriptor() const override;
	bool exportBinaryConfig(QByteArray& outPayload, QString* errorText) const override;
	bool importBinaryConfig(const QByteArray& payload, QString* errorText) override;

private:

	qint32 insertBlockMarkers(BlockInfo block);

	void recomputeLevels();
	BlockInfo* getBlockPtr(qint32 id, MarkerType type);

	bool setBlockCollapse(qint32 blockId, bool collapse);

	static bool blockLess(const BlockInfo& a, const BlockInfo& b) noexcept {
		const bool aIsFooter = a.type == MarkerType::BlockEnd;
		const bool bIsFooter = b.type == MarkerType::BlockEnd;
		const bool aIsHeader = a.type == MarkerType::BlockStart;
		const bool bIsHeader = b.type == MarkerType::BlockStart;

		if(aIsFooter && bIsFooter)
			return a.startOffset > b.startOffset;
		if(aIsFooter != bIsFooter)
			return aIsFooter;          //футер раньше
		if(aIsHeader && bIsHeader)
			return a.endOffset > b.endOffset;
		if(aIsHeader != bIsHeader)
			return aIsHeader;          //хэдер раньше прочих

		return false;//стабильный порядок для остальных
	}

	//вспомогатель­ная вставка + сортировка
	void emplaceSorted(qint64 key, BlockInfo bi) {
		auto& vec = m_blockMarkers[key];
		vec.emplace_back(std::move(bi));
		std::stable_sort(vec.begin(), vec.end(), blockLess);
	}

	bool isDuplicateBlock(qint64 startOffset, qint64 endOffset, qint32* blockId = nullptr);
	callbackID cbID{1};
	void invokeChangedCallbacks(const BlockChangeData& changes );

	qint32 m_blockId{1};
	qint64 m_dataSize{};
	qint32 m_maxLevel{-1};
	bool m_visible{true};
	bool m_callbacksEnabled{true};
	std::map<qint64, std::vector<BlockInfo>> m_blockMarkers;
	std::unordered_map<callbackID, callbackType> changeCalls;

	// многопоточность
	mutable QReadWriteLock m_dataLock; // защищает: m_blockMarkers, m_maxLevel, m_dataSize, m_blockId
	mutable QReadWriteLock m_cbLock;   // защищает: changeCalls, cbID
};
