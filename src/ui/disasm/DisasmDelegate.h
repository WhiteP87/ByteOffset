#pragma once

#include <QStyledItemDelegate>
#include <QColor>
#include <QRegularExpression>

class DisasmDelegate final: public QStyledItemDelegate {
public:
	explicit DisasmDelegate(QObject* parent = nullptr)
		: QStyledItemDelegate(parent) {
		//ничего не делаем
	}

	void setAddressTextColor(const QColor& addressTextColor) {
		m_addressTextColor = addressTextColor;
	}
	void setCellPadding(int leftPixels, int rightPixels) {
		m_paddingLeft = leftPixels < 0 ? 0 : leftPixels;
		m_paddingRight = rightPixels < 0 ? 0 : rightPixels;
	}

	//QStyledItemDelegate
	QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
		QSize baseSize = QStyledItemDelegate::sizeHint(option, index);
		baseSize.rwidth() += m_paddingLeft + m_paddingRight;
		return baseSize;
	}

	void setRowSeparatorEnabled(bool enabled) {
		m_rowSeparatorEnabled = enabled;
	}

	void setRowSeparatorColor(const QColor& color) {
		m_rowSeparatorColor = color;
	}

	void setRowSeparatorThickness(int pixels) {
		m_rowSeparatorThickness = pixels <= 0 ? 1 : pixels;
	}

	void setVerticalSeparatorsEnabled(bool enabled) {
		m_verticalSeparatorsEnabled = enabled;
	}

	void setVerticalSeparatorColumns(const QVector<int>& logicalColumns) {
		m_verticalSeparatorColumns.clear();
		for(int logicalIndex : logicalColumns) {
			m_verticalSeparatorColumns.insert(logicalIndex);
		}
	}

	void setVerticalSeparatorColor(const QColor& color) {
		m_verticalSeparatorColor = color;
	}

	void setVerticalSeparatorThickness(int pixels) {
		m_verticalSeparatorThickness = pixels <= 0 ? 1 : pixels;
	}


	void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
	enum class InstrGroup { ControlFlow, Arithmetic, LogicBit, Move, Other };

	static InstrGroup classifyMnemonic(const QString& mnemonicLower);
	static QColor colorForGroup(InstrGroup group, const QColor& baseColor);

	static void renderPlainColored(QPainter* painter, const QRect& rect, const QFont& font,
		const QString& text, const QColor& color, Qt::Alignment align);

	void renderOperandsHighlighted(QPainter* painter, const QRect& rect, const QFont& font,
		const QString& text, const QColor& baseColor) const;

	void initRegexIfNeeded() const {
		if(m_regexInitialized) {
			return;
		}
		m_regexInitialized = true;

		//регистры (x86/x64/arm/aarch64/sse/avx/mmx)
		m_registerRegex = QRegularExpression(
			R"(\b(?:[re]?(?:ax|bx|cx|dx|si|di|sp|bp)|(?:[abcd][lh]|[sb]pl|sil|dil)|r(?:[0-9]{1,2})(?:[bwdq])?|r(?:ip|sp|bp|si|di)|e(?:ip|sp|bp|si|di)|(?:cs|ds|es|fs|gs|ss)|xmm[0-9]+|ymm[0-9]+|zmm[0-9]+|mm[0-9]+|k[0-9]+|cr[0-9]+|dr[0-9]+|st(?:\([0-9]\)|[0-9])|x[0-9]+|w[0-9]+|q[0-9]+|d[0-9]+|s[0-9]+|v[0-9]+|sp|lr|pc)\b)",
			QRegularExpression::CaseInsensitiveOption);


		//immediate
		m_hexImmRegex = QRegularExpression(R"(\b0x[0-9a-fA-F]+\b)");
		m_decImmRegex = QRegularExpression(R"(\b-?\d+\b)");
	}

private:
	QColor m_addressTextColor;
	QColor m_registerColor{QColor(112, 183, 255).darker()};
	QColor m_immediateColor{QColor(255, 180, 120).darker()};

	int m_paddingLeft{0};
	int m_paddingRight{0};

	//горизонтальные линии по строкам
	bool  m_rowSeparatorEnabled{false};
	QColor m_rowSeparatorColor{QColor(0, 0, 0, 60)};
	int   m_rowSeparatorThickness{1};

	//вертикальные разделители после указанных логических столбцов
	bool   m_verticalSeparatorsEnabled{false};
	QSet<int> m_verticalSeparatorColumns; //храним логические индексы из DisasmModel::Column
	QColor m_verticalSeparatorColor{QColor(0, 0, 0, 60)};
	int    m_verticalSeparatorThickness{1};


	mutable bool m_regexInitialized{false};
	mutable QRegularExpression m_registerRegex;
	mutable QRegularExpression m_hexImmRegex;
	mutable QRegularExpression m_decImmRegex;
};
