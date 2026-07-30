//.cpp
#include "UnifiedProjectReader.h"
using namespace UnifiedProjectFormat;

bool UnifiedProjectReader::readFile(const QString& filePath,
	QVector<BinaryConfig>& outSections,
	QString* errorText) {
	outSections.clear();
	QFile in(filePath);
	if(!in.open(QIODevice::ReadOnly)) { if(errorText) *errorText = in.errorString(); return false; }
	QDataStream s(&in); s.setByteOrder(QDataStream::LittleEndian);

	char magic[6];
	if(s.readRawData(magic, 6) != 6 || ::memcmp(magic, kMagic, 6) != 0) {
		if(errorText) *errorText = QStringLiteral("неверный формат файла проекта");
		return false;
	}
	quint16 ver = 0; s >> ver;
	if(ver != kVersion) {
		if(errorText) *errorText = QStringLiteral("неподдерживаемая версия файла проекта");
		return false;
	}

	while(!s.atEnd()) {
		quint16 tagLen = 0; s >> tagLen;
		QByteArray tag; tag.resize(tagLen);
		if(tagLen && s.readRawData(tag.data(), tagLen) != tagLen) {
			if(errorText) *errorText = QStringLiteral("ошибка чтения тега секции");
			return false;
		}
		quint16 secVer = 0, flags = 0; quint64 payloadLen = 0;
		s >> secVer >> flags >> payloadLen;

		QByteArray payload; payload.resize(int(payloadLen));
		if(payloadLen > 0 && s.readRawData(payload.data(), payload.size()) != payload.size()) {
			if(errorText) *errorText = QStringLiteral("ошибка чтения секции");
			return false;
		}
		if(s.status() != QDataStream::Ok) {
			if(errorText) *errorText = QStringLiteral("ошибка чтения файла проекта");
			return false;
		}

		BinaryConfig sec;
		sec.tag = std::move(tag);
		sec.version = secVer;
		sec.payload = std::move(payload);
		outSections.push_back(std::move(sec));
	}
	return true;
}
