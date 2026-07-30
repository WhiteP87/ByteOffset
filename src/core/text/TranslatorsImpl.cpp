#include "TranslatorsImpl.h"
#include "EncodingUtils.h"
#include "IDataProvider.h"
#include <QStringDecoder>
#include <QStringEncoder>

//-----------------------------Транслятор UTF-8--------------------------------------
QPair<QString, int> Utf8Translator::decodeBytes(QByteArrayView data, int offset) {
	if(offset < 0 || offset >= data.size())
		return qMakePair(QString(), 1);

	const quint8 firstByte = static_cast<quint8>(data[offset]);

	//ASCII символ
	if((firstByte & 0x80) == 0) {
		const QChar asciiChar(firstByte);
		return asciiChar.isPrint() ? qMakePair(QString(asciiChar), 1)
		                           : qMakePair(QString(), 1);
	}

	//байт-продолжение (10xxxxxx) — всегда потребляем 1 байт
	if((firstByte & 0xC0) == 0x80)
		return qMakePair(QString(), 1);

	//определяем ожидаемую длину по лидеру
	int expectedLen = 0;
	if      ((firstByte & 0xE0) == 0xC0) expectedLen = 2;
	else if ((firstByte & 0xF0) == 0xE0) expectedLen = 3;
	else if ((firstByte & 0xF8) == 0xF0) expectedLen = 4;
	else
		return qMakePair(QString(), 1); // невалидный лидер — потребляем 1 байт

	//данные не помещаются целиком
	if(offset + expectedLen > data.size())
		return qMakePair(QString(), 1);

	//читаем продолжения и валидируем 10xxxxxx
	quint8 b1 = 0, b2 = 0, b3 = 0;
	if(expectedLen >= 2) {
		b1 = static_cast<quint8>(data[offset + 1]);
		if((b1 & 0xC0) != 0x80) return qMakePair(QString(), 1);
	}
	if(expectedLen >= 3) {
		b2 = static_cast<quint8>(data[offset + 2]);
		if((b2 & 0xC0) != 0x80) return qMakePair(QString(), 1);
	}
	if(expectedLen == 4) {
		b3 = static_cast<quint8>(data[offset + 3]);
		if((b3 & 0xC0) != 0x80) return qMakePair(QString(), 1);
	}

	//декодируем кодпоинт
	char32_t codepoint = 0;
	bool sequenceValid = true;

	if(expectedLen == 2) {
		codepoint = ((firstByte & 0x1Fu) << 6) | (b1 & 0x3Fu);
		if(codepoint < 0x80u) sequenceValid = false; // overlong
	} else if(expectedLen == 3) {
		codepoint = ((firstByte & 0x0Fu) << 12) | ((b1 & 0x3Fu) << 6) | (b2 & 0x3Fu);
		if(codepoint < 0x800u) sequenceValid = false; // overlong
		if(codepoint >= 0xD800u && codepoint <= 0xDFFFu) sequenceValid = false; // суррогаты
	} else { // 4
		codepoint = ((firstByte & 0x07u) << 18) | ((b1 & 0x3Fu) << 12)
		          | ((b2 & 0x3Fu) << 6)  | (b3 & 0x3Fu);
		if(codepoint < 0x10000u || codepoint > 0x10FFFFu) sequenceValid = false;
	}

	if(!sequenceValid)
		return qMakePair(QString(), 1);

	if(codepoint <= 0xFFFFu) {
		const QChar ch(static_cast<ushort>(codepoint));
		if(ch.isPrint() && !ch.isHighSurrogate() && !ch.isLowSurrogate())
			return qMakePair(QString(ch), expectedLen);
	}
	return qMakePair(QString(), expectedLen);
}

QByteArray Utf8Translator::encodeToBytes(const QString& singleCharacter) {
	if(singleCharacter.isEmpty()) return {};

	const bool hasPair = singleCharacter.size() >= 2
		&& singleCharacter.at(0).isHighSurrogate()
		&& singleCharacter.at(1).isLowSurrogate();
	const QString oneGlyph = singleCharacter.left(hasPair ? 2 : 1);

	QStringEncoder encoder(QStringEncoder::Utf8);
	return encoder.encode(oneGlyph);
}

bool Utf8Translator::isPrintableString(const QString& text) {
	if(text.isEmpty())
		return false;
	for(int index = 0; index < text.size(); ++index) {
		const QChar ch = text.at(index);
		if(!ch.isPrint())
			return false;
	}
	return true;
}

qint32 Utf8Translator::charLen(const QString& oneChar) {
	QStringEncoder encoder(QStringEncoder::Utf8);
	return QByteArray(encoder.encode(oneChar.left(1))).size();
}

//-----------------------------Транслятор UTF-16BE--------------------------------------
QPair<QString, qint32> Utf16BETranslator::decodeAt(qint64 byteOffset) {
	if(!m_provider)
		return qMakePair(QString(), 1);

	const qint64 totalSize = m_provider->size();
	if(byteOffset < 0 || byteOffset >= totalSize)
		return qMakePair(QString(), 1);

	//попадание в середину 16-битного код-юнита (второй байт)
	if((byteOffset & 1) != 0)
		return qMakePair(QString(), 1);

	const qint64 avail = totalSize - byteOffset;
	const int readLen = static_cast<int>(qMin(avail, qint64(4)));
	char buf[4] = {};
	m_provider->readRange(byteOffset, readLen, buf);
	return decodeBytes(QByteArrayView(buf, readLen), 0);
}

QPair<QString, int> Utf16BETranslator::decodeBytes(QByteArrayView data, int offset) {
	if(data.size() - offset < 2)
		return qMakePair(QString(), 1);

	//чтение первого код-юнита (старший, затем младший байт)
	const quint8 firstHighByte = static_cast<quint8>(data[offset + 0]);
	const quint8 firstLowByte  = static_cast<quint8>(data[offset + 1]);
	const quint16 firstCodeUnit = static_cast<quint16>((firstHighByte << 8) | firstLowByte);
	const QChar firstChar(firstCodeUnit);

	//если попали на low surrogate — это середина пары
	if(firstChar.isLowSurrogate())
		return qMakePair(QString(), 2);

	//High surrogate, ожидаем вторую часть пары
	if(firstChar.isHighSurrogate()) {
		if(data.size() - offset < 4)
			return qMakePair(QString(), 2); // обрезанная пара

		const quint8 secondHighByte = static_cast<quint8>(data[offset + 2]);
		const quint8 secondLowByte  = static_cast<quint8>(data[offset + 3]);
		const quint16 secondCodeUnit = static_cast<quint16>((secondHighByte << 8) | secondLowByte);
		const QChar secondChar(secondCodeUnit);

		if(!secondChar.isLowSurrogate())
			return qMakePair(QString(), 2); // невалидная пара

		//Валидная суррогатная пара — не отображаем, но двигаемся на 4 байта
		return qMakePair(QString(), 4);
	}

	if(firstChar.isPrint())
		return qMakePair(QString(firstChar), 2);
	return qMakePair(QString(), 2);
}

QByteArray Utf16BETranslator::encodeToBytes(const QString& singleCharacter) {
	if(singleCharacter.isEmpty())
		return {};

	const bool hasPair = singleCharacter.size() >= 2
		&& singleCharacter.at(0).isHighSurrogate()
		&& singleCharacter.at(1).isLowSurrogate();
	const QString oneGlyph = singleCharacter.left(hasPair ? 2 : 1);

	QStringEncoder encoder(QStringEncoder::Utf16BE); // без BOM
	const QByteArray bytes = encoder.encode(oneGlyph);
	if(bytes.size() != 2 && bytes.size() != 4)
		return {};
	return bytes;
}

qint32 Utf16BETranslator::charLen(const QString& oneChar) {
	QStringEncoder encoder(QStringEncoder::Utf16BE);
	return QByteArray(encoder.encode(oneChar.left(1))).size();
}

//-----------------------------Транслятор UTF-16LE--------------------------------------
QPair<QString, qint32> Utf16LETranslator::decodeAt(qint64 byteOffset) {
	if(!m_provider)
		return qMakePair(QString(), 1);

	const qint64 totalSize = m_provider->size();
	if(byteOffset < 0 || byteOffset >= totalSize)
		return qMakePair(QString(), 1);

	//попадание на второй байт 16-битного код-юнита (нечётное смещение)
	if((byteOffset & 1) != 0)
		return qMakePair(QString(), 1);

	const qint64 avail = totalSize - byteOffset;
	const int readLen = static_cast<int>(qMin(avail, qint64(4)));
	char buf[4] = {};
	m_provider->readRange(byteOffset, readLen, buf);
	return decodeBytes(QByteArrayView(buf, readLen), 0);
}

QPair<QString, int> Utf16LETranslator::decodeBytes(QByteArrayView data, int offset) {
	if(data.size() - offset < 2)
		return qMakePair(QString(), 1);

	//читаем первый код-юнит (сначала младший, затем старший байт)
	const quint8 firstLowByte  = static_cast<quint8>(data[offset + 0]);
	const quint8 firstHighByte = static_cast<quint8>(data[offset + 1]);
	const quint16 firstCodeUnit = static_cast<quint16>(firstLowByte | (firstHighByte << 8));
	const QChar firstChar(firstCodeUnit);

	//low surrogate, находимся в середине пары
	if(firstChar.isLowSurrogate())
		return qMakePair(QString(), 2);

	//high surrogate, ожидаем вторую часть пары
	if(firstChar.isHighSurrogate()) {
		if(data.size() - offset < 4)
			return qMakePair(QString(), 2); // обрезанная пара

		const quint8 secondLowByte  = static_cast<quint8>(data[offset + 2]);
		const quint8 secondHighByte = static_cast<quint8>(data[offset + 3]);
		const quint16 secondCodeUnit = static_cast<quint16>(secondLowByte | (secondHighByte << 8));
		const QChar secondChar(secondCodeUnit);

		if(!secondChar.isLowSurrogate())
			return qMakePair(QString(), 2); // невалидная пара

		//валидная суррогатная пара — не отображаем широкий глиф, но сдвигаемся на 4 байта
		return qMakePair(QString(), 4);
	}

	if(firstChar.isPrint())
		return qMakePair(QString(firstChar), 2);
	return qMakePair(QString(), 2);
}

QByteArray Utf16LETranslator::encodeToBytes(const QString& singleCharacter) {
	if(singleCharacter.isEmpty())
		return {};

	const bool hasPair = singleCharacter.size() >= 2
		&& singleCharacter.at(0).isHighSurrogate()
		&& singleCharacter.at(1).isLowSurrogate();
	const QString oneGlyph = singleCharacter.left(hasPair ? 2 : 1);

	QStringEncoder encoder(QStringEncoder::Utf16LE);
	const QByteArray bytes = encoder.encode(oneGlyph);
	if(bytes.size() != 2 && bytes.size() != 4)
		return {};
	return bytes;
}

qint32 Utf16LETranslator::charLen(const QString& oneChar) {
	QStringEncoder encoder(QStringEncoder::Utf16LE);
	return QByteArray(encoder.encode(oneChar.left(1))).size();
}

//-----------------------------Транслятор ASCII--------------------------------------
static inline bool isAsciiPrintable(quint8 value) { return value >= 0x20 && value <= 0x7E; }

QPair<QString, int> AsciiTranslator::decodeBytes(QByteArrayView data, int offset) {
	if(offset < 0 || offset >= data.size())
		return qMakePair(QString(), 1);

	const quint8 byteValue = static_cast<quint8>(data[offset]);
	if(isAsciiPrintable(byteValue))
		return qMakePair(QString(QChar(byteValue)), 1);
	return qMakePair(QString(), 1);
}

QByteArray AsciiTranslator::encodeToBytes(const QString& singleCharacter) {
	if(singleCharacter.isEmpty()) return {};

	const QChar inputChar = singleCharacter.at(0);
	if(inputChar.unicode() <= 0x7F && inputChar.isPrint())
		return QByteArray(1, static_cast<char>(inputChar.unicode() & 0x7F));

	if(m_policies.m_unrepresentableMode == UnrepresentableMode::substituteWithQuestion)
		return QByteArray(1, static_cast<char>(0x3F));

	return {};
}


//-----------------------------Транслятор однобайтовых табличных кодировок--------------------------------------
SingleByteCodepageTranslator::SingleByteCodepageTranslator(
	std::shared_ptr<const SingleByteTable> table)
	: DataTextTranslator()
	, m_table(std::move(table)) {
	buildReverseMap();
}

SingleByteCodepageTranslator::SingleByteCodepageTranslator(
	std::shared_ptr<IDataProvider> provider,
	std::shared_ptr<const SingleByteTable> table)
	: DataTextTranslator(std::move(provider))
	, m_table(std::move(table)) {
	buildReverseMap();
}

void SingleByteCodepageTranslator::buildReverseMap() {
	m_reverseMap.clear();
	if(!m_table)
		return;

	for(int byteValue = 0; byteValue < 256; ++byteValue) {
		const quint32 tableCodepoint = m_table->m_codepointMap[static_cast<size_t>(byteValue)];
		if(tableCodepoint == 0)
			continue; // «пустая» ячейка
		if(!m_reverseMap.contains(tableCodepoint))
			m_reverseMap.insert(tableCodepoint, static_cast<uchar>(byteValue));
	}
}

QPair<QString, int> SingleByteCodepageTranslator::decodeBytes(QByteArrayView data, int offset) {
	if(offset < 0 || offset >= data.size())
		return qMakePair(QString(), 1);

	const quint8 byteValue = static_cast<quint8>(data[offset]);
	const quint32 tableCodepoint = m_table
		? m_table->m_codepointMap[static_cast<size_t>(byteValue)]
		: 0;

	if(tableCodepoint == 0)
		return qMakePair(QString(), 1);

	const char32_t unicodeCodepoint = static_cast<char32_t>(tableCodepoint);
	const QString glyph = QString::fromUcs4(&unicodeCodepoint, 1);
	if(glyph.isEmpty() || !glyph.at(0).isPrint())
		return qMakePair(QString(), 1);

	return qMakePair(glyph, 1);
}

QByteArray SingleByteCodepageTranslator::encodeToBytes(const QString& singleCharacter) {
	if(singleCharacter.isEmpty() || !m_table) return {};

	char32_t unicodeCodepoint = 0;
	const QChar firstChar = singleCharacter.at(0);
	if(firstChar.isHighSurrogate()
		&& singleCharacter.size() >= 2
		&& singleCharacter.at(1).isLowSurrogate()) {
		unicodeCodepoint = QChar::surrogateToUcs4(firstChar, singleCharacter.at(1));
	} else {
		unicodeCodepoint = static_cast<char32_t>(firstChar.unicode());
	}

	const quint32 mapKey = static_cast<quint32>(unicodeCodepoint);
	uchar outputByte = 0;
	if(m_reverseMap.contains(mapKey)) {
		outputByte = m_reverseMap.value(mapKey);
	} else if(m_policies.m_unrepresentableMode == UnrepresentableMode::substituteWithQuestion) {
		outputByte = static_cast<uchar>(0x3F); // '?'
	} else {
		return {};
	}

	return QByteArray(1, static_cast<char>(outputByte));
}
