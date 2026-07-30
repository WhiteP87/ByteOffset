#pragma once
#include "ISrcReader.h"

#include <QtCore>
#include <QFile>
#include <QMutex>
#include <QHash>

class MMapSrcReader final: public ISrcReader {
public:
	explicit MMapSrcReader(QString filePath, qint64 windowSizeBytes);
	~MMapSrcReader() override;

	qint64 size() const override;
	quint8 readByte(qint64 offset) const override;
	qint64 readRange(qint64 offset, qint64 length, QByteArray& out) const override;
	qint64 readRange(qint64 offset, qint64 length, char* destination) const override;

	QString id() const override;
	QByteArray getHash() const override;
	bool writable() const override;

	//размапить все текущие окна (во всех потоках)
	void invalidateAllWindows();

	qint64 windowSize() const { return m_windowSize; }

private:
	static constexpr qint64 HASH_THRESHOLD = 128 << 20;

	static qint64 systemMapGranularity();
	static qint64 alignDown(qint64 value, qint64 alignment);

	struct Window {
		qint64 m_base{0};
		qint64 m_length{0};
		uchar* m_ptr{nullptr};
	};

	Window& windowForCurrentThread() const;
	bool ensureWindowFor(qint64 offset) const;
	void unmapWindow(Window& w) const;

	const qint64 m_mapGranularity;
	const qint64 m_windowSize;
	mutable QByteArray m_Hash{};

	QString m_filePath;
	mutable QFile m_file;
	mutable qint64 m_fileSize{0};

	mutable QMutex m_windowsMutex;
	mutable QHash<Qt::HANDLE, Window> m_windows;

	mutable QMutex m_mapOpMutex;

	Q_DISABLE_COPY_MOVE(MMapSrcReader)
};
