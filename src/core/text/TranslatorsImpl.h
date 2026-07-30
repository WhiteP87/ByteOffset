#pragma once
#include "DataTextTranslator.h"
#include <QHash>
#include <array>
#include <memory>


//-----------------------------Транслятор Utf8--------------------------------------
class Utf8Translator final: public DataTextTranslator {
public:
	Utf8Translator() = default;
	explicit Utf8Translator(std::shared_ptr<IDataProvider> provider)
		: DataTextTranslator(std::move(provider)) {
	}

	QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) override;
	QByteArray encodeToBytes(const QString& oneChar) override;

	QString name() const override { return QStringLiteral("UTF-8"); }
	QString id() const override { return QStringLiteral("utf8"); }
	qint32 charLen(const QString& oneChar) override;
private:
	static bool isPrintableString(const QString& s);
};



//-----------------------------Транслятор Utf16BE--------------------------------------
class Utf16BETranslator final: public DataTextTranslator {
public:
	Utf16BETranslator() = default;
	explicit Utf16BETranslator(std::shared_ptr<IDataProvider> provider)
		: DataTextTranslator(std::move(provider)) {
	}

	// переопределяем для проверки выравнивания файлового смещения
	QPair<QString, qint32> decodeAt(qint64 byteOffset) override;
	QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) override;
	QByteArray encodeToBytes(const QString& oneChar) override;

	QString name() const override { return QStringLiteral("UTF-16(BE)"); }
	QString id() const override { return QStringLiteral("utf16be"); }
	qint32 charLen(const QString& oneChar) override;
};



//-----------------------------Транслятор Utf16LE--------------------------------------
class Utf16LETranslator final: public DataTextTranslator {
public:
	Utf16LETranslator() = default;
	explicit Utf16LETranslator(std::shared_ptr<IDataProvider> provider)
		: DataTextTranslator(std::move(provider)) {
	}

	// переопределяем для проверки выравнивания файлового смещения
	QPair<QString, qint32> decodeAt(qint64 byteOffset) override;
	QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) override;
	QByteArray encodeToBytes(const QString& oneChar) override;

	QString name() const override { return QStringLiteral("UTF-16(LE)"); }
	QString id() const override { return QStringLiteral("utf16le"); }
	qint32 charLen(const QString& oneChar) override;
};



//-----------------------------Транслятор однобайтовых табличных кодировок--------------
struct SingleByteTable {
	QString m_id;
	QString m_label;
	std::array<quint32, 256> m_codepointMap{};
};

class SingleByteCodepageTranslator final: public DataTextTranslator {
public:
	explicit SingleByteCodepageTranslator(std::shared_ptr<const SingleByteTable> table);
	SingleByteCodepageTranslator(std::shared_ptr<IDataProvider> provider,
		std::shared_ptr<const SingleByteTable> table);

	QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) override;
	QByteArray encodeToBytes(const QString& oneChar) override;

	QString name() const override { return m_table ? m_table->m_label : QString(); }
	QString id()   const override { return m_table ? m_table->m_id : QString(); }
	qint32 charLen(const QString& oneChar) override { return 1; }
private:
	void buildReverseMap();

	std::shared_ptr<const SingleByteTable> m_table;
	//обратная карта U+ в байт
	QHash<quint32, uchar> m_reverseMap;
};


//-----------------------------Транслятор ASCII--------------------------------------
class AsciiTranslator final: public DataTextTranslator {
public:
	AsciiTranslator() = default;
	explicit AsciiTranslator(std::shared_ptr<IDataProvider> provider)
		: DataTextTranslator(std::move(provider)) {
	}

	QPair<QString, int> decodeBytes(QByteArrayView data, int offset = 0) override;
	QByteArray encodeToBytes(const QString& oneChar) override;

	QString name() const override { return QStringLiteral("ASCII"); }
	QString id() const override { return QStringLiteral("ascii"); }
	qint32 charLen(const QString& oneChar) override { return 1; }
};
