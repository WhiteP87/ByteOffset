//.h
#pragma once
#include <QtCore>
#include "IBinaryConfig.h"
#include "UnifiedProjectFormat.h"

class UnifiedProjectWriter final {
public:
	static bool writeFile(const QString& filePath,
		const QVector<BinaryConfig>& sections,
		QString* errorText);
};
