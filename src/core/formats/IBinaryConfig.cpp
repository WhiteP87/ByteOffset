#include "IBinaryConfig.h"
#include "UnifiedProjectReader.h"

bool IBinaryConfig::importFromProjectFile(const QString& filePath, QString* errorText) {
	QVector<BinaryConfig> sections;
	if(!UnifiedProjectReader::readFile(filePath, sections, errorText))
		return false;
	const QByteArray myTag = configDescriptor().tag;
	for(const auto& sec : sections) {
		if(sec.tag == myTag)
			return importBinaryConfig(sec.payload, errorText);
	}
	return true;
}
