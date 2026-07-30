#pragma once
#include <QtCore>
#include <QByteArray>

class ISrcReader {
public:
	virtual ~ISrcReader() = default;

	//физический размер
	virtual qint64 size() const = 0;

	//чтение
	virtual quint8 readByte(qint64 physOff) const = 0;
	virtual qint64 readRange(qint64 physOff, qint64 len, QByteArray& out) const = 0;
	virtual qint64 readRange(qint64 physOff, qint64 len, char* destination) const = 0;

	// идентификатор источника (путь для файла)
	virtual QString id() const = 0;

	//вычислить хэш источника
	virtual QByteArray getHash() const = 0;

	virtual bool writable() const = 0;

};
