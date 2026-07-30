#pragma once
#include <memory>
#include <QString>

struct SingleByteTable;

class SbcLoader {
public:
	//валидация и чтение .sbc в память.
	//возвращает shared_ptr<SingleByteTable> или nullptr при ошибке
	static std::shared_ptr<const SingleByteTable> loadSingleByteTable(
		const QString& filePath, QString* errorText);
};
