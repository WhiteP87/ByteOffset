#pragma once
#include "IBinaryConfig.h"
#include <QString>


struct BookmarkData {
	qint64 offset;
	QString comment;
};

class BookmarkManager final : public IBinaryConfig {
public:
	void addBookmark(qint64 offset, const QString& comment);
	void deleteBookmark(qint64 offset);
	std::optional<std::vector<BookmarkData>> getBookmarksFromTo(qint64 startOff, qint64 endOffset) const;
	bool isLineBookmarks(qint64 startOff, qint64 endOffset) const;//проверка без копирования
	std::optional<BookmarkData> getBookmark(qint64 offset) const;
	bool hasBookmarks() const { return !m_bookMarks.empty(); }
	void onInsert(qint64 at, qint64 len);
	void onRemove(qint64 at, qint64 len);
	void getBookmarksFromTo(qint64 from, qint64 to, std::vector<BookmarkData>& out) const;
	//IBinaryConfig реализация
	BinaryConfigDescriptor configDescriptor() const override;
	bool exportBinaryConfig(QByteArray& outPayload, QString* errorText) const override;
	bool importBinaryConfig(const QByteArray& payload, QString* errorText) override;
private:
	mutable QReadWriteLock m_dataLock;
	std::map<qint64, BookmarkData> m_bookMarks;
};