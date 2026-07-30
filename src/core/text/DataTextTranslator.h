#pragma once
#include <QtGlobal>
#include <QString>
#include <QPair>
#include <QByteArray>
#include <QByteArrayView>
#include <memory>
#include "IDataProvider.h"
#include "ITextCodec.h"
class IDataProvider;
class TextCache;

class DataTextTranslator : public ITextCodec {
public:
	explicit DataTextTranslator(std::shared_ptr<IDataProvider> provider);
	virtual ~DataTextTranslator();

	void setDataProvider(std::shared_ptr<IDataProvider> provider);
	void setCacheSize(qint32 bytes);
	void clearCache();

	//при len==0 - пересчитываем до конца окна кэша
	void invalidateCache(qint64 startOffset, qint64 len);

	void setPolicies(const TranslatorPolicies& policies) override;
	const TranslatorPolicies& policies() const override;

	//чтение через кэш. возвращает один глиф
	QString readAt(qint64 byteOffset);

	//декодирование из провайдера (глиф, длина_в_байтах).
	//UTF-16 переопределяет для проверки выравнивания файлового смещения.
	//Базовая реализация читает байты из провайдера и вызывает decodeBytes.
	virtual QPair<QString, qint32> decodeAt(qint64 byteOffset);

	//декодирование из произвольных байт (глиф, длина_в_байтах).
	//offset — начальная позиция внутри data.
	virtual QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) = 0;

	//кодирует один символ в байты (без записи в провайдер).
	virtual QByteArray encodeToBytes(const QString& oneChar) = 0;

	//кодирует один символ и пишет его в IDataProvider, начиная с byteOffset.
	//возвращает фактически записанное число байт.
	//при записи >=1 байта кэш инвалидируется.
	//Базовая реализация вызывает encodeToBytes и записывает результат в провайдер.
	virtual int encodeAndWriteAt(qint64 byteOffset, const QString& oneChar);

	//отображаемое имя кодека
	virtual QString name() const = 0;

	//ID кодека
	virtual QString id() const = 0;

	virtual qint32 charLen(const QString& oneChar) = 0;
protected:
	DataTextTranslator(); // лёгкий конструктор без провайдера/кэша

	std::shared_ptr<IDataProvider> m_provider;
	std::unique_ptr<TextCache> m_cache;
	TranslatorPolicies m_policies;
};
