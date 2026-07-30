#pragma once
#include <QtGlobal>
#include <QString>
#include <QPair>
#include <QByteArray>
#include <QByteArrayView>

#include "TranslatorPolicies.h"

class ITextCodec {
public:
	virtual ~ITextCodec() = default;

	//декодирование из произвольных байт (глиф, длина_в_байтах).
	virtual QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) = 0;

	//кодирует один символ в байты.
	virtual QByteArray encodeToBytes(const QString& oneChar) = 0;

	//отображаемое имя кодека
	virtual QString name() const = 0;

	//ID кодека
	virtual QString id() const = 0;

	//длина символа в байтах
	virtual qint32 charLen(const QString& oneChar) = 0;

	virtual void setPolicies(const TranslatorPolicies& policies) = 0;
	virtual const TranslatorPolicies& policies() const = 0;
};
