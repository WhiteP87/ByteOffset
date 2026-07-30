//.h
#pragma once
#include <QtCore>
#include "IBinaryConfig.h"
#include "UnifiedProjectFormat.h"

class UnifiedProjectReader final {
public:
	static bool readFile(const QString& filePath,
		QVector<BinaryConfig>& outSections,
		QString* errorText);
};
