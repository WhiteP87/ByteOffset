#pragma once
#include <QtCore>
#include <QByteArrayView>

struct PageHandle {
	qint64 m_offset{0}; // смещение в файле-сторе
	qint64 m_size{0}; // фактическая длина страницы/буфера

	bool isValid() const {
		return m_size > 0; 
	}
};

class IPageStore {
public:
	virtual ~IPageStore() = default;

	//выделить буфер в сторе; initData (если не null) — начальное содержимое
	virtual PageHandle allocate(qint64 size, const char* initData = nullptr) = 0;

	//чтение/запись/заливка внутри буфера
	virtual qint64 read(const PageHandle& h, qint64 off, qint64 len, char* dst) const = 0;
	virtual qint64 write(const PageHandle& h, qint64 off, QByteArrayView data) = 0;
	virtual qint64 fill(const PageHandle& h, qint64 off, qint64 len, QByteArrayView pattern) = 0;

	virtual void free(const PageHandle& h) = 0;
};
