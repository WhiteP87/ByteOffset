#pragma once
#include <QtCore>
#include <QByteArrayView>

struct OverwriteSpan { qint64 off{0}; qint64 len{0}; };

class IOverlay {
public:
	virtual ~IOverlay() = default;

	// логический размер
	virtual qint64 logicalSize() const = 0;

	// чтение с учётом правок
	virtual quint8 readByte(qint64 logicalOffset) const = 0;
	virtual qint64 readRange(qint64 logicalOffset, qint64 length, QByteArray& out) const = 0;
	virtual qint64 readRange(qint64 logicalOffset, qint64 length, char* destination) const = 0;

	// запись в overlay
	virtual bool overwriteByte(qint64 logicalOffset, quint8 value, quint8 mask = 0xFF) = 0;
	virtual qint64 overwriteRange(qint64 logicalOffset, QByteArrayView data) = 0;
	virtual qint64 insertRange(qint64 logicalOffset, QByteArrayView data) = 0;
	virtual qint64 removeRange(qint64 logicalOffset, qint64 length) = 0;
	virtual qint64 fillRange(qint64 logicalOffset, qint64 length, QByteArrayView pattern) = 0;

	// модифицированность
	virtual bool isModified(qint64 logicalOffset) const = 0;
	virtual bool hasAnyModification() const = 0;
	virtual bool hasStructuralChanges() const = 0;//были insert/remove?
	virtual QVector<OverwriteSpan> enumerateOverwriteSpans() const = 0;
};
