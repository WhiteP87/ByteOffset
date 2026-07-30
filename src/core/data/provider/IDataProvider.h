#pragma once
#include <QtCore>
#include <QByteArrayView>
#include <functional>
#include "IBinaryConfig.h"

//изменения геометрии (влияют на размер и смещения)
enum class GeometryKind: quint8 { Insert, Remove };

struct GeometryEvent {
	GeometryKind kind;
	qint64 at;     // смещение начала
	qint64 length; // длина вставки/удаления
	qint64 newSize;// новый логический размер после операции
};

struct SourceMeta {
	QString id;        //путь/идентификатор
	bool writable;  //можно ли писать в исходник
	qint64  baseSize;  //исходный размер без правок
	QByteArray hashVal;
};

enum class SaveStatus {
	Ok,
	StructuralChangeNotAllowed, //есть insert/remove, инплейс запись невозможна
	IoError,                    //ошибка ввода/вывода
	NoSource                    //нет исходного файла/ридера
};

//идентификатор подписки
using DataChangeToken = quint64;

//диапазон изменения (логические оффсеты)
struct ChangedSpan {
	qint64 m_offset{0}; //начало
	qint64 m_oldLen{0}; //старая длина (0 для insert)
	qint64 m_newLen{0}; //новая длина (0 для remove)
};


//колбэк изменения данных (версия + набор изменённых интервалов + флаг модифицированности для всех данных)
using DataChangedCallback = std::function<void(
	quint64 newVersion, const QVector<ChangedSpan>& spans, bool modifiedWhole)>;

using GeometryChangedCallback = std::function<void(quint64 version, const QVector<GeometryEvent>& events)>;

class IDataProvider: public IBinaryConfig {
public:
	virtual ~IDataProvider() = default;

	//логический размер с учётом правок
	virtual qint64 size() const = 0;

	//чтение
	virtual quint8 readByte(qint64 off) const = 0;
	virtual qint64 readRange(qint64 off, qint64 len, QByteArray& out) const = 0;
	virtual qint64 readRange(qint64 off, qint64 len, char* destination) const = 0;

	//чтение с возвратом версии, по которой отданы данные
	virtual qint64 readRange(qint64 off, qint64 len, char* destination, quint64* outVersion) const = 0;

	//запись
	virtual bool writeByte(qint64 off, quint8 value, quint8 mask = 0xFF) = 0;
	virtual bool writeByte(qint64 off, quint8 value, int nibbleIndex) = 0;
	virtual qint64 writeRange(qint64 off, QByteArrayView data) = 0;
	virtual qint64 insert(qint64 off, QByteArrayView data) = 0;
	virtual qint64 remove(qint64 off, qint64 len) = 0;
	virtual qint64 fillRange(qint64 off, qint64 len, QByteArrayView pattern) = 0;

	//модифицированность
	virtual bool isModified(qint64 offset) const = 0;
	virtual bool isDataModified() const = 0;

	//версия данных
	virtual quint64 dataVersion() const = 0;

	//подписки
	virtual DataChangeToken subscribeDataChanged(DataChangedCallback callback) = 0;
	virtual void unsubscribeDataChanged(DataChangeToken token) = 0;
	virtual DataChangeToken subscribeGeometryChanged(GeometryChangedCallback cb) = 0;
	virtual void unsubscribeGeometryChanged(DataChangeToken token) = 0;

	//сохранение в файл
	virtual SaveStatus saveToFile(const QString& filePath, QString* errorMessage = nullptr) = 0;
	virtual std::optional<SourceMeta> sourceMeta() const { return std::nullopt; }

	virtual const QString id() const = 0;
};
