#pragma once
#include <QtCore>

//комментарий: метаданные секции модуля
struct BinaryConfigDescriptor {
	QByteArray tag;         //уникальный utf-8 тег, задаёт модуль (например "hexeditor/patches/v1")
	quint16    version{1};  //внутренняя версия формата секции
	int        applyOrder{0}; //меньше → применять раньше (патчи < разметка)
};

//комментарий: бинарная секция контейнера
struct BinaryConfig {
	QByteArray tag;         //тот же utf-8 тег
	quint16    version{1};
	QByteArray payload;     //blob; формат знает только модуль
};

//комментарий: участник контейнера проекта
class IBinaryConfig {
public:
	virtual ~IBinaryConfig() = default;

	virtual BinaryConfigDescriptor configDescriptor() const = 0;
	virtual bool exportBinaryConfig(QByteArray& outPayload, QString* errorText) const = 0;
	virtual bool importBinaryConfig(const QByteArray& payload, QString* errorText) = 0;

	//импорт по пути к .hecx (дефолт реализован в .cpp)
	virtual bool importFromProjectFile(const QString& filePath, QString* errorText);
};
