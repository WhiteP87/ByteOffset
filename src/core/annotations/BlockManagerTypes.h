#pragma once
#include <array>
#include <optional>
#include <QStringView>   // QStringView
#include <QString>       // если где-то нужен QString
#include <QColor>        // QColor
#include <QtGlobal>      // qint64

enum class MarkerType {
	BlockStart,
	BlockEnd,
	Any
};

enum class BlockType {
	raw,
	code16,
	code86,
	code64,
	codeARM,
	codeARM64,
	codeARMThumb,
	unkCode,
	array,
	structure
};

struct BlockTypeEntry {
	BlockType   type;
	QStringView name;   
};

// Не делаем constexpr: с QStringView и UTF-16 это обычно и не нужно.
// Статическая таблица — ОК.
static const std::array<BlockTypeEntry, 10> BLOCK_TYPE_TABLE{{
	{ BlockType::raw,          QStringView(u"Без типа") },
	{ BlockType::code16,       QStringView(u"Код x86 (16 бит)") },
	{ BlockType::code86,       QStringView(u"Код x86") },
	{ BlockType::code64,       QStringView(u"Код x86-64") },
	{ BlockType::codeARM64,    QStringView(u"Код ARM64") },
	{ BlockType::codeARMThumb, QStringView(u"Код ARM Thumb") },
	{ BlockType::codeARM,      QStringView(u"Код ARM32") },
	{ BlockType::unkCode,      QStringView(u"Код (неизв. арх.)") },
	{ BlockType::array,        QStringView(u"Массив") },
	{ BlockType::structure,    QStringView(u"Структура") }
}};

inline QStringView toString(BlockType type) noexcept {
	for(const auto& e : BLOCK_TYPE_TABLE)
		if(e.type == type)
			return e.name;
	return QStringView(u"unknown");
}

inline std::optional<BlockType> blockTypeFromString(QStringView name,
	Qt::CaseSensitivity cs = Qt::CaseSensitive) noexcept {
	for(const auto& e : BLOCK_TYPE_TABLE)
		if(name.compare(e.name, cs) == 0)
			return e.type;
	return std::nullopt;
}

struct BlockInfo {
	
	qint64 startOffset;
	qint64 endOffset;
	QString name{};
	QString comment{};
	QColor color{};
	qint32 blockId{0};
	qint32 level{};
	bool collapsed{false};
	qint32 parentId{0};
	BlockType blockType{BlockType::raw};
	bool isBigEndian{false};
	MarkerType type{MarkerType::Any};

	qint64 length() const { return endOffset - startOffset + 1; }
};

enum class CollapseFilter: uint8_t { CollapsedOnly, NonCollapsedOnly, Any };

enum class BlockEventKind {Delete, Add, Edit, StateChanged, Collapse};

struct BlockChangeData {
	BlockEventKind kind;
	qint32 blockId;
};