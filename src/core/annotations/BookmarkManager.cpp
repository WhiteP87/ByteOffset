#include <BookmarkManager.h>

namespace {
	static constexpr char BOOKMARKS_MAGIC[6] = {'B','M','K','V','1','\0'}; //magic payload’а блоков
	static constexpr quint16 BOOKMARKS_PAYLOAD_VER = 1;                    //версия формата
}

//добавляет закладку. Если смещение уже в закладках - конкатенируется комментарий с имеющимся
void BookmarkManager::addBookmark(qint64 offset, const QString& comment) {
	QWriteLocker lock(&m_dataLock);
	if(auto it = m_bookMarks.find(offset); it != m_bookMarks.end()) {
		it->second.comment += '\n' + comment;
		return;
	}
	m_bookMarks.insert(std::make_pair(offset, BookmarkData{offset, comment}));
}

//удаляет закладку
void BookmarkManager::deleteBookmark(qint64 offset) {
	QWriteLocker lock(&m_dataLock);
	m_bookMarks.erase(offset);
}

void BookmarkManager::getBookmarksFromTo(qint64 from, qint64 to, std::vector<BookmarkData>& out) const {
	QReadLocker lock(&m_dataLock);

	const auto bound = m_bookMarks.upper_bound(to);

	for(auto iter = m_bookMarks.lower_bound(from); iter != bound; ++iter) {
		out.push_back(iter->second);
	}
}

//возвращает все закладки со смещения startOff до endOff, если есть
std::optional<std::vector<BookmarkData>> BookmarkManager::getBookmarksFromTo(qint64 startOff, qint64 endOff) const{

	if(startOff > endOff) 
		std::swap(startOff, endOff);

	QReadLocker lock(&m_dataLock);

	const auto first = m_bookMarks.lower_bound(startOff);
	const auto last = m_bookMarks.upper_bound(endOff);

	if(first == last) 
		return std::nullopt;

	std::vector<BookmarkData> out;
	out.reserve(std::distance(first, last));

	for(auto it = first; it != last; ++it) 
		out.push_back(it->second);
	return out;
}

//проверяет есть ли закладки в указанном диапазоне от startOff до endOff
bool BookmarkManager::isLineBookmarks(qint64 startOff, qint64 endOff) const {

	if(startOff > endOff) 
		std::swap(startOff, endOff);

	QReadLocker lock(&m_dataLock);

	auto it = m_bookMarks.lower_bound(startOff);
	return it != m_bookMarks.end() && it->first <= endOff;
}

std::optional<BookmarkData> BookmarkManager::getBookmark(qint64 offset) const {
	QReadLocker lock(&m_dataLock);
	if(auto it = m_bookMarks.find(offset); it != m_bookMarks.end()) {
		return it->second;
	}
	return std::nullopt;
}

BinaryConfigDescriptor BookmarkManager::configDescriptor() const {
	return BinaryConfigDescriptor{
	QByteArray("hexeditor/bookmarks/v1"),
	quint16(1),
	30 
	};
}

bool BookmarkManager::exportBinaryConfig(QByteArray& outPayload, QString* errorText) const {
	outPayload.clear();

	QBuffer buffer(&outPayload);
	if(!buffer.open(QIODevice::WriteOnly)) {
		if(errorText) *errorText = QStringLiteral("BookmarkManager: не удалось открыть буфер для записи");
		return false;
	}

	QDataStream s(&buffer);
	s.setByteOrder(QDataStream::LittleEndian);

	//заголовок payload секции блоков
	s.writeRawData(BOOKMARKS_MAGIC, 6);
	s << BOOKMARKS_PAYLOAD_VER;

	//число блоков
	s << quint32(m_bookMarks.size());

	//каждый блок: [смещение][комментарий]
	for(const auto& [key,data] : m_bookMarks) {
		s << qint64(data.offset);
		s << data.comment;
	}

	if(s.status() != QDataStream::Ok) {
		if(errorText) *errorText = QStringLiteral("blockManager: ошибка записи секции");
		return false;
	}
	return true;
}

bool BookmarkManager::importBinaryConfig(const QByteArray& payload, QString* errorText) {
	QBuffer buffer(const_cast<QByteArray*>(&payload));
	if(!buffer.open(QIODevice::ReadOnly)) {
		if(errorText) *errorText = QStringLiteral("BookmarkManager: не удалось открыть буфер для чтения");
		return false;
	}

	QDataStream s(&buffer);
	s.setByteOrder(QDataStream::LittleEndian);

	//проверить magic/версию
	char magic[6];
	if(s.readRawData(magic, 6) != 6 || ::memcmp(magic, BOOKMARKS_MAGIC, 6) != 0) {
		if(errorText) *errorText = QStringLiteral("BookmarkManager: неверный magic секции");
		return false;
	}
	quint16 ver = 0;
	s >> ver;
	if(ver != BOOKMARKS_PAYLOAD_VER) {
		if(errorText) *errorText = QStringLiteral("BookmarkManager: неподдерживаемая версия секции");
		return false;
	}

	quint32 count = 0;
	s >> count;

	struct Rec { qint64 offset; QString comment; };
	std::vector<Rec> recs; recs.reserve(count);

	for(quint32 i = 0; i < count; ++i) {
		Rec r{};
		qint64 offset = 0; QString comment;
		s >> offset; s >> comment;
		if(s.status() != QDataStream::Ok) {
			if(errorText) *errorText = QStringLiteral("blockManager: повреждённая секция");
			return false;
		}
		r.offset = offset;
		r.comment = std::move(comment);
		recs.push_back(std::move(r));
	}

	//применяем — очищаем текущее и добавляем блоки заново
	m_bookMarks.clear();

	for(const Rec& r : recs) {
		m_bookMarks.insert(std::make_pair(r.offset, BookmarkData{r.offset, r.comment}));
	}
	return true;
}

void BookmarkManager::onInsert(qint64 at, qint64 len) {
	QWriteLocker lock(&m_dataLock);

	//пересобрать map
	std::map<qint64, BookmarkData> newMarkers;

	for(const auto& [key, data] : m_bookMarks) {
		qint64 newKey = key >= at ? key + len: key;
		newMarkers[newKey] = data;
	}
	m_bookMarks.swap(newMarkers);
}

void BookmarkManager::onRemove(qint64 at, qint64 len) {
	QWriteLocker lock(&m_dataLock);

	//пересобрать map
	std::map<qint64, BookmarkData> newMarkers;

	for(const auto& [off, data] : m_bookMarks) {
		if(off >= at && off < at + len) continue;
		if(off < at) { newMarkers[off] = data; continue; }
		newMarkers[off - len] = data;
	}
	m_bookMarks.swap(newMarkers);
}