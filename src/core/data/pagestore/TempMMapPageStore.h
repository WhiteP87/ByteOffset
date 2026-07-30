#pragma once
#include "IPageStore.h"
#include <QtCore>
#include <QFile>
#include <QMutex>

class TempMMapPageStore final: public IPageStore {
public:
	explicit TempMMapPageStore();
	~TempMMapPageStore() override;

	PageHandle allocate(qint64 size, const char* initData = nullptr) override;
	qint64 read(const PageHandle& h, qint64 off, qint64 len, char* dst) const override;
	qint64 write(const PageHandle& h, qint64 off, QByteArrayView data) override;
	qint64 fill(const PageHandle& h, qint64 off, qint64 len, QByteArrayView pattern) override;
	void free(const PageHandle& h) override;

private:
	//системная гранулярность файла
	static qint64 systemGranularity();

	//гранулярность выравнивания
	const qint64 m_gran;

	//mutex только для операций изменения размера файла (resize/разметка)
	mutable QMutex m_resizeMutex;

	//основной файл (жизненный цикл и resize)
	mutable QFile  m_file;
	qint64         m_fileSize{0};//текущий размер temp-файла
	qint64         m_nextOff{0};//следующий свободный выровненный оффсет

	//для позиционного I/O
#ifdef Q_OS_UNIX
	int            m_posixFd{-1};//fd для pread/pwrite
#endif
#ifdef Q_OS_WIN
	void* m_winHandle{nullptr};//HANDLE для ReadFile/WriteFile + OVERLAPPED
#endif
};
