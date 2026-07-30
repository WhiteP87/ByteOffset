#include "BlockManager.h"
#include <stdexcept>
#include <set>
#include <algorithm>
#include <unordered_map>

namespace {
	static constexpr char BLOCKS_MAGIC[6] = {'B','L','K','V','1','\0'}; //magic payload’а блоков
	static constexpr quint16 BLOCKS_PAYLOAD_VER = 1;                    //версия формата
}

//добавление блока
qint32 BlockManager::addBlock(qint64 start, qint64 end, const QString& name, const QString& comment, 
	BlockType type, bool isBE, const QColor& color, bool collapsed, qint32 blockId /*= 0*/) {
	BlockInfo tmp{};
	tmp.startOffset = start;
	tmp.endOffset = end;
	tmp.name = name;
	tmp.comment = comment;
	tmp.blockType = type;
	tmp.color = color;
	tmp.collapsed = collapsed;
	tmp.blockId = blockId;
	tmp.isBigEndian = isBE;
	return insertBlockMarkers(tmp);
}

qint32 BlockManager::addBlock(const BlockInfo& blockMeta) {
	return insertBlockMarkers(blockMeta);
}

//редактирование метаинфы существующего блока без изменения id
bool BlockManager::editBlock(qint32 blockId, qint64 start, qint64 end, const QString& name, const QString& comment,
	BlockType type, bool isBE, const QColor& color, bool collapsed) {

	BlockInfo block{}; block.blockId = blockId; block.startOffset = start;block.endOffset = end;	
	block.name = name; block.comment = comment; block.blockType = type; block.color = color;	
	block.collapsed = collapsed; block.isBigEndian = isBE;

	return editBlock(block);
}

bool BlockManager::editBlock(const BlockInfo& block) {

	QWriteLocker lock(&m_dataLock);

	if(block.blockId > m_blockId)
		return false;

	auto blockInfoStart = getBlockPtr(block.blockId, MarkerType::BlockStart);
	if(blockInfoStart) {
		if(blockInfoStart->startOffset != block.startOffset || blockInfoStart->endOffset != block.endOffset) {
			lock.unlock();
			removeBlockById(block.blockId);
			insertBlockMarkers(block);
		} else {
			auto blockInfoEnd = getBlockPtr(block.blockId, MarkerType::BlockEnd);
			blockInfoStart->name = blockInfoEnd->name = block.name;
			blockInfoStart->comment = blockInfoEnd->comment = block.comment;
			blockInfoStart->color = blockInfoEnd->color = block.color;
			blockInfoStart->collapsed = blockInfoEnd->collapsed = block.collapsed;
			blockInfoStart->blockType = blockInfoEnd->blockType = block.blockType;
			blockInfoStart->isBigEndian = blockInfoEnd->isBigEndian = block.isBigEndian;
			recomputeLevels();
			lock.unlock();
			invokeChangedCallbacks({BlockEventKind::Edit, block.blockId});
		}
		return true;
	}
	return false;
}

//удаление блока по имени
void BlockManager::removeBlockById(qint32 id) {
	{
		QWriteLocker lock(&m_dataLock);
		for(auto it = m_blockMarkers.begin(); it != m_blockMarkers.end(); ) {
			auto& vec = it->second;
			vec.erase(std::remove_if(vec.begin(), vec.end(),
				[&](const BlockInfo& b) { return b.blockId == id; }),
				vec.end());

			if(vec.empty()) it = m_blockMarkers.erase(it);
			else ++it;
		}
		recomputeLevels();
	}
	invokeChangedCallbacks({BlockEventKind::Delete, id});
}

//удаление всех блоков
void BlockManager::clear(bool invokeable) {
	{
		QWriteLocker lock(&m_dataLock);
		m_blockMarkers.clear();
		m_blockId = 1;
	}
	if(invokeable)
		invokeChangedCallbacks({BlockEventKind::StateChanged,0});
}

//есть ли блок по смещению
bool BlockManager::hasBlockMarkerAt(qint64 offset) const {
	QReadLocker lock(&m_dataLock);
	auto it = m_blockMarkers.find(offset);
	return it != m_blockMarkers.end() && !it->second.empty();
}

//возвращает блоки всех типов по смещению
const std::vector<BlockInfo>&
BlockManager::getBlocksAt(qint64 offset) const {
	QReadLocker lock(&m_dataLock);
	auto it = m_blockMarkers.find(offset);
	if(it == m_blockMarkers.end()) {
		static const std::vector<BlockInfo> empty;
		return empty;
	}

	return it->second;
}

//установка статуса схлопнутости блока с blockId
bool BlockManager::setBlockCollapse(qint32 blockId, bool collapse) {
	QWriteLocker lock(&m_dataLock);

	auto startBlock = getBlockPtr(blockId, MarkerType::BlockStart);
	if(!startBlock) throw std::invalid_argument("setBlockCollapse: invalid startBlock blockId");

	bool prevCollapse = startBlock->collapsed;
	startBlock->collapsed = collapse;

	auto endBlock = getBlockPtr(blockId, MarkerType::BlockEnd);
	if(!endBlock) throw std::invalid_argument("setBlockCollapse: invalid endBlock blockId");
	endBlock->collapsed = collapse;

	if(prevCollapse != collapse) {
		lock.unlock();
		invokeChangedCallbacks({BlockEventKind::Collapse,blockId});
	}
	return prevCollapse;
}

bool BlockManager::isDuplicateBlock(qint64 startOffset, qint64 endOffset, qint32* blockId) {
	//вызывается под удерживаемым локом
	auto it = m_blockMarkers.find(startOffset);
	if(it == m_blockMarkers.end()) {
		if(blockId) *blockId = 0;
		return false;
	}

	for(const auto& blockInfo : it->second) {
		if(blockInfo.startOffset == startOffset && blockInfo.endOffset == endOffset) {
			if(blockId) *blockId = blockInfo.blockId;
			return true;
		}
	}

	if(blockId) *blockId = 0;
	return false;
}


//добавление блока. добавляется два маркера - для начала и конца блока
qint32 BlockManager::insertBlockMarkers(BlockInfo block) {

	QWriteLocker lock(&m_dataLock);

	if(block.startOffset > block.endOffset) 
		std::swap(block.startOffset, block.endOffset);
	if(!m_dataSize || block.startOffset < 0 || block.startOffset == block.endOffset 
		|| isDuplicateBlock(block.startOffset, block.endOffset)) 
		return 0;

	if(block.blockId) {
		if(block.blockId > m_blockId || getBlockPtr(block.blockId, MarkerType::BlockStart) != nullptr) {
			return 0;
		}
	}

	block.blockId = !block.blockId ? m_blockId++ : block.blockId;

	if(block.name.isEmpty()) {
		block.name = QStringLiteral("BLOCK ") + QString::number(block.blockId);
	}

	block.type = MarkerType::BlockStart;
	emplaceSorted(block.startOffset, block);
	block.type = MarkerType::BlockEnd;
	emplaceSorted(block.endOffset + 1, block);

	recomputeLevels();
	lock.unlock();
	invokeChangedCallbacks({BlockEventKind::Add, block.blockId});
	return block.blockId;
}


//возвращает указатель на информацию о блоке с указанным id и типом
const BlockInfo* BlockManager::getBlockById(qint32 id, MarkerType type/* = MarkerType::BlockStart*/) const {
	QReadLocker lock(&m_dataLock);
	for(const auto& [offset, vec] : m_blockMarkers)
		for(const auto& b : vec)
			if(b.blockId == id && b.type == type)
				return &b;
	return nullptr;
}



//возвращает вектор блоков в указанном диапазоне с типом type
std::vector<std::reference_wrapper<const BlockInfo>> BlockManager::getMarkersFromTo(MarkerType type, qint64 offsetFrom /*= 0*/, qint64 offsetTo /*= std::numeric_limits<qint64>::max()*/) const {
	QReadLocker lock(&m_dataLock);
	std::vector<std::reference_wrapper<const BlockInfo>> markers{};

	if(m_blockMarkers.empty())
		return markers;

	const auto bound = m_blockMarkers.upper_bound(offsetTo);

	for(auto iter = m_blockMarkers.lower_bound(offsetFrom); iter != bound; ++iter) {
		for(const auto& blockInfo : iter->second) {
			if(blockInfo.type == type || type == MarkerType::Any) {
				markers.push_back(blockInfo);
			}
		}
	}
	return markers;
}

//возвращает true, если смещение внутри схлопнутого блока
bool BlockManager::isOffsetIntoCollapsedBlock(qint64 offset) {
	QReadLocker lock(&m_dataLock);
	if(findNearPrevCollapsed(offset))
		return true;
	return false;
}

//возвращает вектор стартовых маркеров блоков, в которых лежит смещение offset
//обход справа налево -> первыми лежат ближайшие сверху смещения
std::vector<const BlockInfo*>
BlockManager::collectStartBlocksCovering(qint64 offset) const {
	QReadLocker lock(&m_dataLock);
	std::vector<const BlockInfo*> out;
	if(m_blockMarkers.empty())
		return out;

	// Идем влево по ключам <= offset; берём только BlockStart, покрывающие offset
	auto it = m_blockMarkers.upper_bound(offset); // первый ключ > offset
	while(it != m_blockMarkers.begin()) {
		--it; // it->first <= offset
		for(const auto& b : it->second) {
			if(b.type != MarkerType::BlockStart) continue;
			if(b.startOffset > offset)           continue;
			if(b.endOffset < offset)           continue;
			out.push_back(&b);
		}
	}
	return out;
}


//возвращает пару блоков, которые задают левую и правую внешние границы набора всех (отфильтрованных) блоков, перекрывающих offset
std::pair<const BlockInfo*, const BlockInfo*>
BlockManager::getBoundBlocks(qint64 offset, CollapseFilter filter) const {
	const auto covered = collectStartBlocksCovering(offset);
	const BlockInfo* minStartPtr = nullptr; //минимум по start. при равенстве бОльший end
	const BlockInfo* maxEndPtr = nullptr; //максимум по end. при равенстве меньший start

	auto accept = [&](const BlockInfo* b) {
		switch(filter) {
		case CollapseFilter::CollapsedOnly:    return b->collapsed;
		case CollapseFilter::NonCollapsedOnly: return !b->collapsed;
		case CollapseFilter::Any:              return true;
		}
		return true;
		};

	for(const BlockInfo* b : covered) {
		if(!accept(b)) continue;

		//поиск минимального старта блока
		if(!minStartPtr
			|| b->startOffset < minStartPtr->startOffset
			|| (b->startOffset == minStartPtr->startOffset && b->endOffset > minStartPtr->endOffset)) {
			minStartPtr = b;
		}

		//максимальный конечный маркер
		if(!maxEndPtr
			|| b->endOffset > maxEndPtr->endOffset
			|| (b->endOffset == maxEndPtr->endOffset && b->startOffset < maxEndPtr->startOffset)) {
			maxEndPtr = b;
		}
	}

	return {minStartPtr, maxEndPtr};
}

void BlockManager::onInsert(qint64 at, qint64 len) {
	if(len <= 0) return;

	{
		QWriteLocker lock(&m_dataLock);

		//пересобрать map
		std::map<qint64, std::vector<BlockInfo>> newMarkers;

		auto shiftKey = [&](qint64 key) -> qint64 {
			return (key > at) ? (key + len) : key;
			};

		for(const auto& [key, vec] : m_blockMarkers) {
			qint64 newKey = shiftKey(key);
			for(const auto& b : vec) {
				BlockInfo nb = b;

				if(nb.startOffset > at)
					nb.startOffset += len;
				if(nb.endOffset >= at)
					nb.endOffset += len;
				if(nb.startOffset < at && ((m_dataSize - len) == (nb.endOffset + 1))) {
					nb.endOffset += len;
					if(nb.type == MarkerType::BlockEnd)
						newKey = nb.endOffset + 1;
				}

				newMarkers[newKey].push_back(std::move(nb));
			}
		}

		//пересортировать записи вектора
		for(auto& [k, v] : newMarkers) {
			std::stable_sort(v.begin(), v.end(), blockLess);
		}
		m_blockMarkers.swap(newMarkers);

		recomputeLevels();
	}
	invokeChangedCallbacks({BlockEventKind::StateChanged,0});
}


void BlockManager::onRemove(qint64 at, qint64 len) {
	if(len <= 0) return;

	{
		QWriteLocker lock(&m_dataLock);

		const qint64 cutL = at;
		const qint64 cutR = at + len; // полуинтервал [cutL, cutR)

		// обновить размер данных
		if(m_dataSize) {
			m_dataSize = std::max<qint64>(0, m_dataSize - len);
		}

		std::map<qint64, std::vector<BlockInfo>> newMarkers;

		auto clipBlock = [&](BlockInfo& nb) -> bool {
			qint64 s = nb.startOffset;
			qint64 e = nb.endOffset;

			//полностью слева без изменений
			if(e < cutL) {
				// s, e неизменны
			}
			//полностью справа сдвигаем целиком
			else if(s >= cutR) {
				s -= len; e -= len;
			}
			//полностью внутри удаляемого полуинтервала 
			else if(s >= cutL && e < cutR) {
				return false; // удалить блок
			}
			//удаление внутри блока (s < cutL && e >= cutR) укорачиваем на len
			else if(s < cutL && e >= cutR) {
				e -= len;
			}
			//пересечение хвостом (s < cutL && e в [cutL, cutR) ): обрезаем хвост
			else if(s < cutL && e >= cutL && e < cutR) {
				e = cutL - 1;
			}
			//пересечение головой (s в [cutL, cutR) && e >= cutR): переносим голову и сдвигаем хвост
			else if(s >= cutL && s < cutR && e >= cutR) {
				s = cutL;
				e -= len;
			}

			nb.startOffset = s;
			nb.endOffset = e;
			return e >= s;
			};

		for(const auto& [key, vec] : m_blockMarkers) {
			for(const auto& b : vec) {
				BlockInfo nb = b; // копия

				if(!clipBlock(nb)) continue; // удаляем блок полностью

				//маркерный ключ должен соответствовать обновлённым полям:
				//start-маркер должен висеть на nb.startOffset
				//end-маркер на (nb.endOffset + 1)
				bool keep = true;
				qint64 targetKey{};

				if(b.type == MarkerType::BlockStart) {
					targetKey = nb.startOffset;
				} else {
					targetKey = nb.endOffset + 1;
				}

				// если targetKey попал в дыру слева от 0, пропускаем
				if(targetKey < 0) keep = false;

				if(keep) {
					newMarkers[targetKey].push_back(std::move(nb));
				}
			}
		}

		//очистить пустые ключи и отсортировать
		for(auto it = newMarkers.begin(); it != newMarkers.end(); ) {
			auto& v = it->second;
			if(v.empty()) it = newMarkers.erase(it);
			else { std::stable_sort(v.begin(), v.end(), blockLess); ++it; }
		}

		m_blockMarkers.swap(newMarkers);

		recomputeLevels();
	}
	invokeChangedCallbacks({BlockEventKind::StateChanged,0});
}

//возвращает id ближайшего свернутого блока, относительно смещения
qint32 BlockManager::findNearPrevCollapsed(qint64 offset) {

	if(m_blockMarkers.empty())
		return 0;

	auto it = m_blockMarkers.upper_bound(offset); //первый ключ > offset
	while(it != m_blockMarkers.begin()) {
		--it; //теперь it->first <= offset

		qint32 bestId = 0;
		qint64 bestEnd = std::numeric_limits<qint64>::max();

		for(const auto& b : it->second) {
			if(b.type != MarkerType::BlockStart || !b.collapsed) continue;
			if(b.endOffset >= offset && b.endOffset < bestEnd) {
				bestEnd = b.endOffset;
				bestId = b.blockId;
				if(bestEnd == offset) //дальше лучше не будет
					return bestId;
			}
		}

		if(bestId) return bestId;
	}
	return 0;
}

//регистрация колбэка для уведомления об изменении
BlockManager::callbackID BlockManager::registerChangedCallback(callbackType cb) {
	QWriteLocker lock(&m_cbLock);
	changeCalls.emplace(cbID, std::move(cb));
	return cbID++;
}

//удаление зарегистрированного колбэка
void BlockManager::unregisterChangedCallback(callbackID cbId) {
	QWriteLocker lock(&m_cbLock);
	changeCalls.erase(cbId);
}

//проверяет, является ли блок свернутым
bool BlockManager::isCollapsed(qint32 blockId) const {

	auto block = getBlockById(blockId, MarkerType::BlockStart);
	if(block) {
		return block->collapsed;
	} else {
		throw std::invalid_argument("isCollapsed: invalid blockId");
	}
	return false;
}

//разворачивает все блоки, в которые входит смещение
//возвращает количество развернутых блоков
qint32 BlockManager::expandAllToOffset(qint64 offset) {

	if(m_blockMarkers.empty())
		return 0;

	qint32 expanded = 0;

	auto markers = getMarkersFromTo(MarkerType::BlockStart, 0, offset);
	for(const auto& ref : markers) {
		const BlockInfo& block = ref.get();
		if(offset >= block.startOffset && offset <= block.endOffset && block.collapsed) {
			expandBlock(block.blockId);
			++expanded;
		}
	}
	return expanded;
}

//переключает свернутость блока с blockId
void BlockManager::toggleCollapse(qint32 blockId) {
	if(isCollapsed(blockId)) {
		expandBlock(blockId);
	} else {
		collapseBlock(blockId);
	}
	invokeChangedCallbacks({BlockEventKind::Collapse, blockId});
}

//вызов колбэков при изменении блоков
void BlockManager::invokeChangedCallbacks(const BlockChangeData& changes) {
	if(!m_callbacksEnabled) return;
	std::vector<callbackType> snapshot;
	{
		QReadLocker lock(&m_cbLock);
		snapshot.reserve(changeCalls.size());
		for(const auto& p : changeCalls) 
			snapshot.push_back(p.second);
	}
	for(const auto& cb : snapshot) { cb(changes); }
}

//пересчёт уровней вложенности блоков
void BlockManager::recomputeLevels() {
	if(m_blockMarkers.empty()) {
		m_maxLevel = -1;
		return;
	}

	struct SimpleBlock { qint64 start; qint64 end; qint32 id; };
	std::vector<SimpleBlock> blocks;
	blocks.reserve(m_blockMarkers.size());

	for(auto& kv : m_blockMarkers) {
		const auto& vec = kv.second;
		for(const auto& b : vec) {
			if(b.type == MarkerType::BlockStart) {
				blocks.push_back(SimpleBlock{b.startOffset, b.endOffset, b.blockId});
			}
		}
	}

	std::sort(blocks.begin(), blocks.end(),
		[](const SimpleBlock& a, const SimpleBlock& b) {
			if(a.start != b.start) return a.start < b.start;
			return a.end > b.end; //при равном старте — более длинный раньше
		});

	struct Active { qint64 start; qint64 end; qint32 id; qint32 level; };
	std::vector<Active> active;
	active.reserve(blocks.size());

	std::unordered_map<qint32, qint32> levelById;
	std::unordered_map<qint32, qint32> parentById;
	levelById.reserve(blocks.size());
	parentById.reserve(blocks.size());

	qint32 maxLevel = -1;

	for(const auto& blk : blocks) {
		//снимаем с хвоста то, что закончилось раньше текущего старта
		while(!active.empty() && active.back().end < blk.start)
			active.pop_back();

		//ищем родителя
		qint32 bestParentId = 0;
		qint32 bestParentLevel = -1;
		qint64 bestParentLen = std::numeric_limits<qint64>::max();
		qint64 bestParentStart = std::numeric_limits<qint64>::min();

		for(int i = int(active.size()) - 1; i >= 0; --i) {
			const auto& a = active[i];

			// строгая включенность: a включает blk и шире его
			const bool contains = (a.start <= blk.start) && (a.end >= blk.end)
				&& (a.start <  blk.start || a.end >  blk.end);
			if(!contains) continue;

			const qint64 len = a.end - a.start;

			if(a.level > bestParentLevel
				|| (a.level == bestParentLevel && (len < bestParentLen
					|| (len == bestParentLen && a.start > bestParentStart)))) {
				bestParentLevel = a.level;
				bestParentLen = len;
				bestParentStart = a.start;
				bestParentId = a.id;
			}
		}

		const qint32 level = (bestParentLevel >= 0) ? (bestParentLevel + 1) : 0;

		levelById[blk.id] = level;
		parentById[blk.id] = bestParentId;
		if(level > maxLevel) maxLevel = level;

		// текущий становится активным
		active.push_back(Active{blk.start, blk.end, blk.id, level});
	}

	// проставляем уровень и родителя всем маркерам
	for(auto& kv : m_blockMarkers) {
		for(auto& b : kv.second) {
			if(auto it = levelById.find(b.blockId); it != levelById.end())
				b.level = it->second;
			else
				b.level = 0;

			if(auto ip = parentById.find(b.blockId); ip != parentById.end())
				b.parentId = ip->second;
			else
				b.parentId = 0;
		}
	}
	m_maxLevel = maxLevel;
}



//возвращает неконстантный указатель на информацию о блоке с указанным id и типом
BlockInfo* BlockManager::getBlockPtr(qint32 id, MarkerType type) {
	for(auto& [offset, vec] : m_blockMarkers)
		for(auto& b : vec)
			if(b.blockId == id && b.type == type)
				return &b;
	return nullptr;
}

//уникальный utf-8 тег + порядок применения после патчей
BinaryConfigDescriptor BlockManager::configDescriptor() const {

	return BinaryConfigDescriptor{
		QByteArray("hexeditor/blocks/v1"),
		quint16(1),
		20 //применять после IDataProvider (патчи/геометрия)
	};
}

bool BlockManager::exportBinaryConfig(QByteArray& outPayload, QString* errorText) const {
	outPayload.clear();

	//только стартовые маркеры (BlockStart), End восстановится по endOffset
	std::vector<BlockInfo> starts;

	{
		QReadLocker lock(&m_dataLock);
		if(m_blockMarkers.empty()) {
			//допустимо вернуть пустую секцию
		} else {
			for(const auto& [key, vec] : m_blockMarkers) { 
				for(const auto& bi : vec) {               
					if(bi.type == MarkerType::BlockStart)
						starts.push_back(bi);
				}
			}
		}
	}

	QBuffer buffer(&outPayload);
	if(!buffer.open(QIODevice::WriteOnly)) {
		if(errorText) *errorText = QStringLiteral("blockManager: не удалось открыть буфер для записи");
		return false;
	}

	QDataStream s(&buffer);
	s.setByteOrder(QDataStream::LittleEndian);

	//заголовок payload секции блоков
	s.writeRawData(BLOCKS_MAGIC, 6);
	s << BLOCKS_PAYLOAD_VER;

	//число блоков
	s << quint32(starts.size());

	//каждый блок
	for(const BlockInfo& b : starts) {
		s << qint64(b.startOffset);
		s << qint64(b.endOffset);
		s << quint8(b.collapsed ? 1 : 0);
		s << quint32(b.color.rgba());    
		s << b.name;
		s << b.comment;
		s << b.blockType;
	}

	if(s.status() != QDataStream::Ok) {
		if(errorText) *errorText = QStringLiteral("blockManager: ошибка записи секции");
		return false;
	}
	return true;
}

bool BlockManager::importBinaryConfig(const QByteArray& payload, QString* errorText) {
	QBuffer buffer(const_cast<QByteArray*>(&payload));
	if(!buffer.open(QIODevice::ReadOnly)) {
		if(errorText) *errorText = QStringLiteral("blockManager: не удалось открыть буфер для чтения");
		return false;
	}

	QDataStream s(&buffer);
	s.setByteOrder(QDataStream::LittleEndian);

	//проверить magic/версию
	char magic[6];
	if(s.readRawData(magic, 6) != 6 || ::memcmp(magic, BLOCKS_MAGIC, 6) != 0) {
		if(errorText) *errorText = QStringLiteral("blockManager: неверный magic секции");
		return false;
	}
	quint16 ver = 0;
	s >> ver;
	if(ver != BLOCKS_PAYLOAD_VER) {
		if(errorText) *errorText = QStringLiteral("blockManager: неподдерживаемая версия секции");
		return false;
	}

	quint32 count = 0;
	s >> count;

	std::vector<BlockInfo> recs; recs.reserve(count);

	for(quint32 i = 0; i < count; ++i) {
		BlockInfo b{};
		qint64 start = 0, end = 0; quint8 collapsed = 0; quint32 argb = 0; QString name, comment; BlockType type = BlockType::raw;
		s >> start; s >> end; s >> collapsed; s >> argb; s >> name; s >> comment; s >> type;
		if(s.status() != QDataStream::Ok) {
			if(errorText) *errorText = QStringLiteral("blockManager: повреждённая секция");
			return false;
		}
		b.startOffset = start;
		b.endOffset = end;
		b.collapsed = collapsed;
		b.color = QColor::fromRgba(argb);
		b.name = std::move(name);
		b.comment = std::move(comment);
		b.blockType = type;
		recs.push_back(std::move(b));
	}

	//запрещаем вызов колбэков
	setNotifying(false);

	//применяем — очищаем текущее и добавляем блоки заново
	clear();

	for(const BlockInfo& bi : recs) {
		insertBlockMarkers(bi);
	}

	//разрешаем вызов колбэков и вызываем
	setNotifying(true);
	invokeChangedCallbacks({BlockEventKind::StateChanged,0});

	return true;
}


