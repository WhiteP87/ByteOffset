#pragma once
#include <QReadWriteLock>
#include <QtCore>
#include <QColor>
#include <QStringView>

using FieldID = qint64;

enum class FieldType {
	byte,
	word,
	dword,
	qword,
	flags,
	ustring,
	astring,
	raw,
	floattype,
	doubletype,
	opcode,
	sbyte,
	sword,
	sdword,
	sqword,
};

struct FieldTypeEntry {
	FieldType   type;
	QStringView name;
	qint8 typeSize;
};

static const std::array<FieldTypeEntry, 15> FIELD_TYPE_TABLE{{
	{ FieldType::raw,       QStringView(u"Без типа"),                   0 },
	{ FieldType::byte,		QStringView(u"Байт (без знака)"),           1 },
	{ FieldType::word,      QStringView(u"Слово (без знака)"),          2 },
	{ FieldType::dword,     QStringView(u"Двойное слово(без знака)"),   4 },
	{ FieldType::qword,     QStringView(u"Четверное слово(без знака)"), 8 },
	{ FieldType::flags,     QStringView(u"Битовые флаги"),              0 },
	{ FieldType::ustring,	QStringView(u"Строка уникод"),              0 },
	{ FieldType::astring,	QStringView(u"Строка однобайтовая"),        0 },
	{ FieldType::floattype,	QStringView(u"Одинарной точности"),         4 },
	{ FieldType::doubletype,QStringView(u"Двойной точности"),           8 },
	{ FieldType::opcode,    QStringView(u"Опкод"),				        0 },
	{ FieldType::sbyte,		QStringView(u"Байт (со знаком)"),           1 },
	{ FieldType::sword,     QStringView(u"Слово (со знаком)"),          2 },
	{ FieldType::sdword,    QStringView(u"Двойное слово (со знаком)"),  4 },
	{ FieldType::sqword,    QStringView(u"Четверное слово (со знаком)"),8 }
}};

inline QStringView toString(FieldType type) noexcept {
	for(const auto& e : FIELD_TYPE_TABLE)
		if(e.type == type)
			return e.name;
	return QStringView(u"unknown");
}

inline std::optional<FieldType> fieldTypeFromString(QStringView name,
	Qt::CaseSensitivity cs = Qt::CaseSensitive) noexcept {
	for(const auto& e : FIELD_TYPE_TABLE)
		if(name.compare(e.name, cs) == 0)
			return e.type;
	return std::nullopt;
}

struct FieldInfo {
	qint64 startOffset;
	qint64 endOffset;
	QString name{};
	QString comment{};
	QColor color;
	FieldType type{FieldType::raw};
	QString typeFormat{};
	bool isBigEndian{false};
	qint32 parentBlockId{0};
	FieldID fieldId;

	qint64 length() const { return endOffset - startOffset + 1; }
};

enum class FieldEventKind { Delete, Add, Edit, StateChanged };

struct FieldChangeData {
	FieldEventKind kind;
	FieldID fieldId;
};