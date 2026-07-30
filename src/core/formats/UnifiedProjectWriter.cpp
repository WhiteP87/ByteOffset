//.cpp
#include "UnifiedProjectWriter.h"
using namespace UnifiedProjectFormat;

bool UnifiedProjectWriter::writeFile(const QString& filePath,
	const QVector<BinaryConfig>& sections,
	QString* errorText) {
	QSaveFile save(filePath);
	if(!save.open(QIODevice::WriteOnly)) { if(errorText) *errorText = save.errorString(); return false; }
	QDataStream s(&save); s.setByteOrder(QDataStream::LittleEndian);

	s.writeRawData(kMagic, 6);
	s << kVersion;

	for(const auto& sec : sections) {
		const quint16 tagLen = quint16(sec.tag.size());
		s << tagLen;
		if(tagLen) s.writeRawData(sec.tag.constData(), tagLen);
		s << sec.version;
		s << quint16(0); //flags
		s << quint64(sec.payload.size());
		if(!sec.payload.isEmpty())
			s.writeRawData(sec.payload.constData(), sec.payload.size());
		if(s.status() != QDataStream::Ok) {
			if(errorText) *errorText = QStringLiteral("ошибка записи секции");
			return false;
		}
	}
	if(!save.commit()) {
		if(errorText) *errorText = QStringLiteral("ошибка записи файла");
		return false;
	}
	return true;
}
