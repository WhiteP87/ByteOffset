#pragma once
#include <QtCore>
#include <QTimer>
#include <QObject>
#include <QClipboard>
#include "CodepageRegistry.h"
#include "IDataProvider.h"
#include "TranslatorFactory.h"
#include "BlockManagerTypes.h"
#include <QGuiApplication>
#include "DisasmEngine.h"

inline qint64 alignDown(qint64 offset, qint64 align) {
	return offset - (offset % align);
}

enum class rowType {
	blockHeader,
	blockFooter,
	data
};

enum class HighlightType {
	Background,
	Underline,
	Overline,
	Fullline
};

enum class HighlightArea {
	Hex, Text, All
};

struct HighlightGeometry {
	QPoint topLeftHex{};
	QPoint bottomRightHex{};
	QPoint topLeftText{};
	QPoint bottomRightText{};
	HighlightArea area{HighlightArea::All};
};

struct EditorContext {
	CodepageRegistry* cpRegistry{nullptr};
	TranslatorFactory* cpFactory{nullptr};
	std::shared_ptr<IDataProvider> dataProvider;
	QString curCpId{"utf8"};
};

struct rowInfo {
	rowType type;
	qint64 rowOffset;
	union {
		qint64 startColumn;
		qint64 startOffset;
	};
	union {
		qint64 endColumn;
		qint64 endOffset;
	};
	qint64 globalFirstLine;
	qint32 blockId;
	qint32 level;
	bool partial;
	bool collapsed;
};

enum class fileBlockType {
	HeaderBlock,
	FooterBlock,
	DataBlock
};

struct FileBlockInfo {
	fileBlockType type;
	qint64 firstLine;
	qint64 lastLine;
	qint64 startOffset;
	qint64 endOffset;
	qint32 blockId;
	qint32 level;
	bool partial;
	bool collapsed;
};

class Selection {
private:
	qint64 m_start{-1};
	qint64 m_end{-1};
	bool m_active{false};

public:
	inline qint64 length() const { return isEmpty() ? 0 : m_end - m_start + 1; }
	inline void setActive(bool isActive) { m_active = isActive; }
	inline bool isActive() const { return m_active; }
	inline bool isSelected(qint64 byteOffset) const {
		const qint64 startSelection{qMin(m_start, m_end)};
		const qint64 endSelection{qMax(m_start, m_end)};
		return byteOffset >= startSelection && byteOffset <= endSelection;
	}
	inline bool isEmpty() const { return (m_start == -1 || m_end == -1); }
	inline void reset() { m_start = -1; m_end = -1; }
	inline void set(qint64 startOff, qint64 endOff) {
		m_start = startOff; m_end = endOff;
		normalize();
	}
	inline qint64 startOffset() const { return m_start; }
	inline qint64 endOffset() const { return m_end; }

	inline void setStartOffset(qint64 offset) { m_start = offset; }
	inline void setEndOffset(qint64 offset) { m_end = offset; }
	inline void normalize() {
		if(m_start > m_end) std::swap(m_start, m_end);
	}

};

class CaretFlash: public QObject {
	Q_OBJECT
public:
	explicit CaretFlash(QObject* parent = nullptr):QObject(parent), m_timer(new QTimer(this)) {
		m_timer->setTimerType(Qt::PreciseTimer);
		connect(m_timer, &QTimer::timeout, this, &CaretFlash::toggle);
		m_timer->start(m_period);
	}
	bool isFlashed() const noexcept { return m_flashed; }
	void setPeriod(qint32 periodMs) {
		if(periodMs < 0) {
			periodMs = 0;
		} else if(periodMs > MAX_CARET_FLASH_PERIOD) {
			periodMs = MAX_CARET_FLASH_PERIOD;
		}
		m_period = periodMs;
	}
	void setFlashed(bool flashed) { m_flashed = flashed; }
signals:
	void flashChanged(bool on);
public slots:
	void setEnabled(bool isEnable) {
			m_enabled = isEnable;
			m_flashed = true;
			if(!m_enabled) {
				m_timer->stop();
			} else {
				if(!m_turnOffed)
					m_timer->start();
			}
			emit flashChanged(m_flashed);
	}
	void turnOff(bool offed) {
		m_turnOffed = offed;
		setEnabled(!offed);
	}
private slots:
	void toggle() { m_flashed = !m_flashed; emit flashChanged(m_flashed); }
private:
	const qint32 MAX_CARET_FLASH_PERIOD = 2000;
	QTimer* m_timer;
	bool m_flashed{true};
	bool m_enabled{true};
	bool m_turnOffed{false};
	qint32 m_period{600};
};

class Caret: public QObject {
	Q_OBJECT
public:
	Caret(QObject* parent = nullptr):QObject{parent} {};
	CaretFlash flash{};
	qint64 byteOffset() const { return m_byteOffset; }
	qint32 nibbleIdx() const { return m_nibbleIdx; }
	void setByteOffset(qint64 byteOffset) {
		m_byteOffset = byteOffset; if(!m_stateEmitBlocked) emit caretChanged(m_byteOffset, m_nibbleIdx);
	}
	void setNibbleIdx(qint32 nibbleIdx) {
		m_nibbleIdx = nibbleIdx; if(!m_stateEmitBlocked) emit caretChanged(m_byteOffset, m_nibbleIdx);
	}
	void set(qint64 offset, qint32 idx) { 
		setByteOffset(offset);
		setNibbleIdx(idx);
		if(!m_stateEmitBlocked) emit caretChanged(m_byteOffset, m_nibbleIdx);
	}
	void setNotifyEnabled(bool enable) { m_stateEmitBlocked = !enable; }
private:
	qint64 m_byteOffset{-1}; //байт с установленной кареткой ввода
	qint32 m_nibbleIdx{-1}; //полубайт, на котором каретка
	bool m_stateEmitBlocked{false};
signals:
	void caretChanged(qint64 byte, qint32 nibbleIdx);
};

enum class CopyAsType { CArray };
inline const QString appMimeType{"application/octet-stream"};

inline static QByteArray parseHexBytes(const QString& text) {
	QByteArray out;
	out.reserve(text.size() / 3);

	// \xHH | 0xHH | HH (ровно две hex-цифры как отдельный токен)
	static const QRegularExpression re(
		R"(\\x([0-9A-Fa-f]{2})|0[xX]([0-9A-Fa-f]{2})|\b([0-9A-Fa-f]{2})\b)"
	);

	auto it = re.globalMatch(text);
	while(it.hasNext()) {
		const auto m = it.next();
		QString g = m.captured(1);
		if(g.isEmpty()) g = m.captured(2);
		if(g.isEmpty()) g = m.captured(3);
		bool ok = false;
		const auto v = static_cast<char>(g.toUInt(&ok, 16));
		if(ok) out.append(v);
	}
	return out;
}

inline static QByteArray readClipboardBytes(QStringView appMimeType) {
	const QMimeData* md = QGuiApplication::clipboard()->mimeData();
	if(!md) return {};

	if(md->hasFormat(appMimeType.toString()))
		return md->data(appMimeType.toString());

	if(md->hasText())
		return parseHexBytes(md->text());

	return {};
}

inline static bool clipboardHasPasteData(QStringView appMimeType) {
	const QMimeData* md = QGuiApplication::clipboard()->mimeData();
	if(!md) return false;

	if(md->hasFormat(appMimeType.toString()))
		return true;

	if(md->hasText()) {
		static const QRegularExpression probe(
			R"(\\x[0-9A-Fa-f]{2}|0[xX][0-9A-Fa-f]{2}|\b[0-9A-Fa-f]{2}\b)"
		);
		// достаточно одного совпадения
		return probe.match(md->text()).hasMatch();
	}
	return false;
}

inline std::optional<DisasmEngine::Arch> mapBlockTypeToDisasmType(BlockType blockType) {
	switch(blockType) {
	case BlockType::code16:return DisasmEngine::Arch::X86_16;
	case BlockType::code86:return DisasmEngine::Arch::X86_32;
	case BlockType::code64:return DisasmEngine::Arch::X86_64;
	case BlockType::codeARM:return DisasmEngine::Arch::ARM;
	case BlockType::codeARM64:return DisasmEngine::Arch::ARM64;
	case BlockType::codeARMThumb:return DisasmEngine::Arch::ARM_THUMB;
	default: return std::nullopt;
	}
	return std::nullopt;
}