#include "SbcLoader.h"
#include "TranslatorsImpl.h" // для SingleByteTable
#include <QFile>
#include <QDataStream>
#include <QFileInfo>
#include <QRegularExpression>

namespace {

	//длина id и label указана в UTF-8 символах
	struct SbcHeaderLite {
		char magic[4]; //"SBC1"
		quint16 headerSize;
		quint16 version;
		quint32 idLen;
		quint32 labelLen;
		quint32 fileSize;
		quint32 reserved;
	};

	inline bool isValidCodecId(const QString& codecId) {
		static const QRegularExpression idRegex(QStringLiteral("^[a-z0-9._-]+$"));
		return idRegex.match(codecId).hasMatch();
	}

	inline bool isValidUnicodeScalar(quint32 cp) {
		return (cp <= 0x10FFFFu) && !(cp >= 0xD800u && cp <= 0xDFFFu);
	}

	static constexpr quint16 HEADER_SIZE = 24;
	static constexpr quint16 SBC_VERSION = 0x0002;
	static constexpr quint32 SIZE_OF_CHAR = 4;
}

std::shared_ptr<const SingleByteTable> SbcLoader::loadSingleByteTable(
	const QString& filePath, QString* errorText) {

	QFile file(filePath);
	if(!file.open(QIODevice::ReadOnly)) {
		if(errorText) 
			*errorText = QStringLiteral("open failed: %1").arg(file.errorString());
		return nullptr;
	}

	QDataStream dataStream(&file);
	dataStream.setByteOrder(QDataStream::LittleEndian);

	SbcHeaderLite header{};
	if(dataStream.readRawData(header.magic, 4) != 4) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (magic)");
		return nullptr;
	}

	if(QByteArray(header.magic, 4) != QByteArrayLiteral("SBC1")) {
		if(errorText) 
			*errorText = QStringLiteral("bad magic");
		return nullptr;
	}

	dataStream >> header.headerSize;
	dataStream >> header.version;
	dataStream >> header.idLen;
	dataStream >> header.labelLen;
	dataStream >> header.fileSize;
	dataStream >> header.reserved;

	if(dataStream.status() != QDataStream::Ok) {
		if(errorText) 
			*errorText = QStringLiteral("corrupted header");
		return nullptr;
	}

	if(header.headerSize < HEADER_SIZE) {
		if(errorText) 
			*errorText = QStringLiteral("unsupported header size %1").arg(header.headerSize);
		return nullptr;
	}
	if(header.version != SBC_VERSION) {
		if(errorText) 
			*errorText = QStringLiteral("unsupported version %1").arg(header.version);
		return nullptr;
	}

	//полная минимальная длина - header + таблица (256 * размер знака) + id + label
	const qint64 realFileSize = file.size();
	const quint64 tableSize = 256u * SIZE_OF_CHAR;
	const quint64 minimalExpectedSize =
		static_cast<quint64>(header.headerSize) + tableSize
		+ static_cast<quint64>(header.idLen)
		+ static_cast<quint64>(header.labelLen);

	if(realFileSize < static_cast<qint64>(minimalExpectedSize)) {
		if(errorText) 
			*errorText = QStringLiteral("file too small");
		return nullptr;
	}

	if(header.fileSize != 0 && header.fileSize != static_cast<quint32>(realFileSize)) {
		if(errorText) 
			*errorText = QStringLiteral("file size mismatch");
		return nullptr;
	}

	//переход к началу полезных данных - заголовок, padding, таблица
	constexpr int headerFixedPart = HEADER_SIZE;
	const int headerPadding = static_cast<int>(header.headerSize) - headerFixedPart;
	if(headerPadding > 0) {
		if(dataStream.skipRawData(headerPadding) != headerPadding) {
			if(errorText) 
				*errorText = QStringLiteral("unexpected EOF (header padding)");
			return nullptr;
		}
	}

	//nаблица 256×размер знака (LE)
	std::array<quint32, 256> codepointMap{};
	for(int index = 0; index < 256; ++index) {
		quint32 cp = 0;
		dataStream >> cp;

		if(dataStream.status() != QDataStream::Ok) {
			if(errorText) 
				*errorText = QStringLiteral("unexpected EOF (table)");
			return nullptr;
		}

		if(cp != 0 && !isValidUnicodeScalar(cp)) {
			if(errorText) 
				*errorText = QStringLiteral("invalid codepoint in table");
			return nullptr;
		}

		codepointMap[static_cast<size_t>(index)] = cp;
	}

	//id в UTF-8 (без терминатора)
	QByteArray idBytes;
	idBytes.resize(static_cast<int>(header.idLen));

	if(dataStream.readRawData(idBytes.data(), idBytes.size()) != idBytes.size()) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (id)");
		return nullptr;
	}

	const QString codecId = QString::fromUtf8(idBytes);
	if(codecId.isEmpty() || !isValidCodecId(codecId)) {
		if(errorText) 
			*errorText = QStringLiteral("invalid id '%1'").arg(codecId);
		return nullptr;
	}

	//label в UTF-8 (без терминатора)
	QByteArray labelBytes;
	labelBytes.resize(static_cast<int>(header.labelLen));

	if(dataStream.readRawData(labelBytes.data(), labelBytes.size()) != labelBytes.size()) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (label)");
		return nullptr;
	}
	const QString codecLabel = QString::fromUtf8(labelBytes);

	//готовим таблицу
	auto table = std::make_shared<SingleByteTable>();
	table->m_id = codecId;
	table->m_label = codecLabel;
	table->m_codepointMap = codepointMap;
	return table;
}
