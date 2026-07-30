#include "FieldManager.h"

namespace {
	static constexpr char FIELDS_MAGIC[6] = {'F','L','D','V','1','\0'}; //magic payload’а
	static constexpr quint16 FIELDS_PAYLOAD_VER = 1;                    //версия формата
}

FieldManager::callbackID FieldManager::registerChangedCallback(callbackType cb) {
	QWriteLocker lock(&m_cbLock);
	changeCalls.emplace(cbID, std::move(cb));
	return cbID++;
}

void FieldManager::unregisterChangedCallback(callbackID cbId) {
	QWriteLocker lock(&m_cbLock);
	changeCalls.erase(cbId);
}

void FieldManager::setNotifying(bool enable) {
	QWriteLocker lock(&m_dataLock);
	m_callbacksEnabled = enable;
}

void FieldManager::invokeChangedCallbacks(const FieldChangeData& changes) {
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

void FieldManager::clear(bool invokeable) {
	{
		QWriteLocker lock(&m_dataLock);
		m_fields.clear();
		m_jump.clear();
		m_fieldId = 1;
	}
	if(invokeable)
		invokeChangedCallbacks({FieldEventKind::StateChanged, 0});
}

FieldID FieldManager::addField(qint64 startOffset, qint64 endOffset, const QString& name,
	const QString& comment, const QColor& color, FieldType type, const QString& typeFormat,
	bool isBigEndian, FieldID fieldId, qint32 parentBlockId) {

	FieldInfo field{};
	field.startOffset = startOffset;
	field.endOffset = endOffset;
	field.name = name;
	field.comment = comment;
	field.color = color;
	field.type = type;
	field.typeFormat = typeFormat;
	field.isBigEndian = isBigEndian;
	field.fieldId = fieldId;
	field.parentBlockId = parentBlockId;

	return insertField(field);
}

FieldID FieldManager::addField(const FieldInfo& field) {
	return insertField(field);
}

bool FieldManager::editField(FieldID fieldId, qint64 startOffset, qint64 endOffset, const QString& name,
	const QString& comment, const QColor& color, FieldType type, 
	const QString& typeFormat, bool isBigEndian, qint32 parentBlockId) {

	{
		QReadLocker lock(&m_dataLock);
		if(m_fields.empty() || fieldId > m_fieldId)
			return false;
	}

	FieldInfo field{};
	field.startOffset = startOffset;
	field.endOffset = endOffset;
	field.name = name;
	field.comment = comment;
	field.color = color;
	field.type = type;
	field.typeFormat = typeFormat;
	field.isBigEndian = isBigEndian;
	field.fieldId = fieldId;
	field.parentBlockId = parentBlockId;

	return insertField(field);
}

bool FieldManager::editField(const FieldInfo& field) {

	if(m_fields.empty() || field.fieldId > m_fieldId)
		return false;

	auto fieldPtr = getFieldByIdPtr(field.fieldId);
	QWriteLocker lock(&m_dataLock);
	if(fieldPtr) {
		if(fieldPtr->startOffset != field.startOffset || fieldPtr->endOffset != field.endOffset) {
			lock.unlock();
			removeFieldById(field.fieldId);
			insertField(field);
		} else {
			fieldPtr->name = field.name;
			fieldPtr->comment = field.comment;
			fieldPtr->color = field.color;
			fieldPtr->type = field.type;
			fieldPtr->typeFormat = field.typeFormat;
			fieldPtr->isBigEndian = field.isBigEndian;
			fieldPtr->parentBlockId = field.parentBlockId;
			lock.unlock();
			invokeChangedCallbacks({FieldEventKind::Edit, field.fieldId});
		}
		return true;
	}
	return false;
}

void FieldManager::removeFieldById(FieldID fieldId) {

	{
		QWriteLocker lock(&m_dataLock);
		if(m_fields.empty()) return;

		auto jumpIt = m_jump.find(fieldId);
		if(jumpIt == m_jump.end())
			return;

		const auto fiOffset = jumpIt->second;

		auto fieldsIt = m_fields.find(fiOffset);
		if(fieldsIt == m_fields.end()) {
			m_jump.erase(jumpIt);//рассинхрон(нет смещения в филдах)
			return;
		}

		auto& vec = fieldsIt->second;

		const auto it = std::find_if(vec.begin(), vec.end(),
			[&](const FieldInfo& fi) { return fi.fieldId == fieldId; });
		if(it == vec.end()) {
			m_jump.erase(jumpIt);//рассинхрон(нет филдId в векторе по смещению)
			return;
		}

		vec.erase(it);

		m_jump.erase(jumpIt);
		if(vec.empty()) {
			m_fields.erase(fieldsIt);
		}
	}
	invokeChangedCallbacks({FieldEventKind::Delete, fieldId});
}

std::optional<std::vector<const FieldInfo*>> FieldManager::isOffsetIntoField(qint64 offset) const {
	QReadLocker lock(&m_dataLock);

	if(offset < 0 || m_fields.empty())	return std::nullopt;

	std::vector<const FieldInfo*> out;

	const auto end = m_fields.upper_bound(offset + MAX_FIELD_LENGTH);
	const auto startOff = std::max(0ll, offset - MAX_FIELD_LENGTH);

	for(auto iter = m_fields.lower_bound(startOff); iter != end; ++iter) {
		for(const auto& fieldInfo : iter->second) {
			const auto startOff = fieldInfo.startOffset;
			const auto endOff = fieldInfo.endOffset;
			if(offset >= startOff && offset <= endOff) {
				out.push_back(&fieldInfo);
			}
		}
	}
	return out;
}

const FieldInfo* FieldManager::getFieldById(FieldID fieldId) const {
	QReadLocker lock(&m_dataLock);

	if(m_fields.empty())
		return nullptr;

	auto itJump = m_jump.find(fieldId);
	if(itJump == m_jump.end())
		return nullptr;

	const auto fiOffset = itJump->second;

	auto itFields = m_fields.find(fiOffset);
	if(itFields == m_fields.end())
		return nullptr;

	for(const auto& fi : itFields->second) {
		if(fi.fieldId == fieldId)
			return &fi;
	}
	return nullptr;
}

std::vector<const FieldInfo*> FieldManager::getFieldsFromTo(qint64 from, qint64 to) const {

	QReadLocker lock(&m_dataLock);

	std::vector<const FieldInfo*> result;
	if(m_fields.empty())
		return result;

	if(from > to) std::swap(from, to);

	const auto end = m_fields.upper_bound(to);
	const auto startOff = std::max(0ll, from - MAX_FIELD_LENGTH);

	for(auto iter = m_fields.lower_bound(startOff); iter != end; ++iter) {
		const auto& vec = iter->second;
		for(const FieldInfo& fi : vec) {
			if(fi.endOffset >= from && fi.startOffset <= to) {
				result.push_back(&fi);
			}
		}
	}
	return result;
}

BinaryConfigDescriptor FieldManager::configDescriptor() const {
	return BinaryConfigDescriptor{
	QByteArray("hexeditor/fields/v1"),
	quint16(1),
	40 //применять после IDataProvider
	};
}

bool FieldManager::exportBinaryConfig(QByteArray& outPayload, QString* errorText) const {
	outPayload.clear();

	std::vector<FieldInfo> fields;

	{
		QReadLocker lock(&m_dataLock);
		if(m_fields.empty()) {
			//допустимо вернуть пустую секцию
		} else {
			for(const auto& [key, vec] : m_fields) {
				for(const auto& fi : vec) {              
					fields.push_back(fi);
				}
			}
		}
	}

	QBuffer buffer(&outPayload);
	if(!buffer.open(QIODevice::WriteOnly)) {
		if(errorText) *errorText = QStringLiteral("FieldManager: не удалось открыть буфер для записи");
		return false;
	}

	QDataStream s(&buffer);
	s.setByteOrder(QDataStream::LittleEndian);

	//заголовок payload секции блоков
	s.writeRawData(FIELDS_MAGIC, 6);
	s << FIELDS_PAYLOAD_VER;

	//число записей полей
	s << quint32(fields.size());

	for(const FieldInfo& f : fields) {
		s << qint64(f.startOffset);
		s << qint64(f.endOffset);
		s << f.name;
		s << f.comment;
		s << quint32(f.color.rgba());     //комментарий: ARGB32
		s << qint32(f.type);
		s << f.typeFormat;
		s << bool(f.isBigEndian);
	}

	if(s.status() != QDataStream::Ok) {
		if(errorText) *errorText = QStringLiteral("FieldManager: ошибка записи секции");
		return false;
	}
	return true;
}

bool FieldManager::importBinaryConfig(const QByteArray& payload, QString* errorText) {

	QBuffer buffer(const_cast<QByteArray*>(&payload));

	if(!buffer.open(QIODevice::ReadOnly)) {
		if(errorText) *errorText = QStringLiteral("FieldManager: не удалось открыть буфер для чтения");
		return false;
	}

	QDataStream s(&buffer);
	s.setByteOrder(QDataStream::LittleEndian);

	//проверить magic/версию
	char magic[6];
	if(s.readRawData(magic, 6) != 6 || ::memcmp(magic, FIELDS_MAGIC, 6) != 0) {
		if(errorText) *errorText = QStringLiteral("FieldManager: неверный magic секции");
		return false;
	}
	quint16 ver = 0;
	s >> ver;
	if(ver != FIELDS_PAYLOAD_VER) {
		if(errorText) *errorText = QStringLiteral("FieldManager: неподдерживаемая версия секции");
		return false;
	}

	quint32 count = 0;
	s >> count;

	std::vector<FieldInfo> recs; recs.reserve(count);

	for(quint32 i = 0; i < count; ++i) {
		FieldInfo f{};
		qint64 start = 0, end = 0; QString name, comment, typeFormat;  quint32 argb = 0; qint32 type; bool isBE;
		s >> start; s >> end; s >> name; s >> comment; s >> argb; s >> type; s >> typeFormat; s >> isBE;
		if(s.status() != QDataStream::Ok) {
			if(errorText) *errorText = QStringLiteral("FieldManager: повреждённая секция");
			return false;
		}
		f.startOffset = start; 
		f.endOffset = end;
		f.name = std::move(name);
		f.comment = std::move(comment);
		f.color = QColor::fromRgba(argb);
		f.type = static_cast<FieldType>(type);
		f.typeFormat = std::move(typeFormat);
		f.isBigEndian = isBE;
		recs.push_back(std::move(f));
	}

	//применяем — очищаем текущее и добавляем поля заново
	setNotifying(false);
	clear(false);

	for(const FieldInfo& f : recs) {
		insertField(f);
	}

	setNotifying(true);
	invokeChangedCallbacks({FieldEventKind::StateChanged, 0});
	return true;
}

FieldInfo* FieldManager::getFieldByIdPtr(FieldID id) {
	QReadLocker lock(&m_dataLock);
	if(m_fields.empty()) return nullptr;

	if(m_jump.find(id) != m_jump.end()) {
		const auto fiOffset = m_jump[id];
		for(auto& fi : m_fields[fiOffset]) {
			if(fi.fieldId == id)
				return &fi;
		}
	}
	return nullptr;
}

FieldID FieldManager::insertField(FieldInfo field) {
	QWriteLocker lock(&m_dataLock);

	//если длина нулевая или начало < 0 выходим
	if(!field.length() || field.startOffset < 0)
		return 0;

	//если требуется вставить поле с существующим id (пересоздание при редактировании)
	if(field.fieldId) {
		
		//если id больше текущего счетчика id - выходим
		if(field.fieldId > m_fieldId || m_jump.find(field.fieldId) != m_jump.end()) {
			return 0;
		}
	}

	//начало всегда раньше
	if(field.startOffset > field.endOffset)
		std::swap(field.startOffset, field.endOffset);

	if(field.length() > MAX_FIELD_LENGTH)
		return 0;

	field.fieldId = !field.fieldId ? m_fieldId++ : field.fieldId;

	if(field.name.isEmpty()) {
		field.name = QStringLiteral("FIELD ") + QString::number(field.fieldId);
	}

	QColor c = field.color;
	c.setAlpha(FIELD_COLOR_ALPHA);
	field.color = c;

	emplaceSorted(field.startOffset, field);
	m_jump.insert(std::make_pair(field.fieldId, field.startOffset));

	const auto id = field.fieldId;
	lock.unlock();

	invokeChangedCallbacks({FieldEventKind::Add, id});
	return id;
}

void FieldManager::onInsert(qint64 at, qint64 len) {

	if(len <= 0 || at < 0) 
		return;

	QWriteLocker lock(&m_dataLock);
	if(m_fields.empty()) 
		return;

	std::map<qint64, std::vector<FieldInfo>> newFields;

	for(const auto& [oldKey, vec] : m_fields) {
		for(const auto& field : vec) {
			FieldInfo f = field;
			//cдвиг геометрии
			if(f.startOffset > at)
				f.startOffset += len;
			if(f.endOffset >= at)
				f.endOffset += len;
			newFields[f.startOffset].push_back(std::move(f));
		}
	}

	//сортировка по длине (убывание)
	for(auto it = newFields.begin(); it != newFields.end(); ) {
		auto& v = it->second;
		if(v.empty()) 
			it = newFields.erase(it);
		else {
			std::stable_sort(v.begin(), v.end(),
				[](const FieldInfo& a, const FieldInfo& b) { return a.length() > b.length(); });
			++it;
		}
	}

	//пересборка карты полей и jump-индекса
	m_fields.swap(newFields);
	m_jump.clear();
	for(const auto& [k, v] : m_fields)
		for(const auto& f : v)
			m_jump.emplace(f.fieldId, f.startOffset);

	lock.unlock();
	invokeChangedCallbacks({FieldEventKind::StateChanged, 0});
}

void FieldManager::onRemove(qint64 at, qint64 len) {
	if(len <= 0) return;

	QWriteLocker lock(&m_dataLock);
	if(m_fields.empty()) return;

	const qint64 cutL = at;
	const qint64 cutR = at + len;

	std::map<qint64, std::vector<FieldInfo>> newFields;

	auto clipField = [&](FieldInfo& nf) -> bool {
		qint64 s = nf.startOffset;
		qint64 e = nf.endOffset;

		
		if(e < cutL) {
			//полностью слева - без изменений
		}
		//полностью справа — сдвиг влево на len
		else if(s >= cutR) {
			s -= len; e -= len;
		}
		//целиком внутри удаляемого отрезка — удалить поле
		else if(s >= cutL && e < cutR) {
			return false;
		}
		//внутри поля — укоротить на len
		else if(s < cutL && e >= cutR) {
			e -= len;
		}
		//пересечение хвостом — обрезать хвост до cutL-1
		else if(s < cutL && e >= cutL && e < cutR) {
			e = cutL - 1;
		}
		//пересечение головой — перенести голову на cutL и сдвинуть хвост
		else if(s >= cutL && s < cutR && e >= cutR) {
			s = cutL;
			e -= len;
		}

		nf.startOffset = s;
		nf.endOffset = e;
		return e >= s && s >= 0;
		};

	for(const auto& [oldKey, vec] : m_fields) {
		for(const auto& field : vec) {
			FieldInfo f = field;
			if(!clipField(f)) continue;
			newFields[f.startOffset].push_back(std::move(f));
		}
	}

	//сортировка по длине (убывание)
	for(auto it = newFields.begin(); it != newFields.end(); ) {
		auto& v = it->second;
		if(v.empty()) it = newFields.erase(it);
		else {
			std::stable_sort(v.begin(), v.end(),
				[](const FieldInfo& a, const FieldInfo& b) { return a.length() > b.length(); });
			++it;
		}
	}

	m_fields.swap(newFields);
	m_jump.clear();
	for(const auto& [k, v] : m_fields)
		for(const auto& f : v)
			m_jump.emplace(f.fieldId, f.startOffset);

	lock.unlock();
	invokeChangedCallbacks({FieldEventKind::StateChanged, 0});
}
