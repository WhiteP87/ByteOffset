#include "DisasmDelegate.h"
#include "DisasmModel.h"

#include <QApplication>
#include <QPainter>
#include <QStyle>
#include <QTextLayout>

static Qt::Alignment kDefaultAlign = Qt::AlignLeft | Qt::AlignVCenter;

void DisasmDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const {
	if(!index.isValid()) {
		return;
	}

	QStyleOptionViewItem opt = option;
	initStyleOption(&opt, index);
	opt.state &= ~QStyle::State_HasFocus;
	opt.state &= ~QStyle::State_MouseOver;

	//рисуем фон стандартным стилем без текста
	const QString originalText = opt.text;
	opt.text.clear();
	const QWidget* viewWidget = opt.widget;
	QStyle* viewStyle = viewWidget ? viewWidget->style() : QApplication::style();
	viewStyle->drawControl(QStyle::CE_ItemViewItem, &opt, painter, viewWidget);

	//горизонтальный разделитель строки
	if(m_rowSeparatorEnabled) {
		painter->save();
		QPen rowPen(m_rowSeparatorColor);
		rowPen.setWidth(m_rowSeparatorThickness);
		rowPen.setCosmetic(true);
		painter->setPen(rowPen);
		const int y = opt.rect.bottom();
		painter->drawLine(opt.rect.left(), y, opt.rect.right(), y);
		painter->restore();
	}

	const QString cellText = originalText;
	const QRect textRect = viewStyle->subElementRect(QStyle::SE_ItemViewItemText, &opt, viewWidget);
	QRect paddedRect = textRect.adjusted(m_paddingLeft, 0, -m_paddingRight, 0);
	if(paddedRect.width() <= 0) {
		//ничего рисовать — нет места с учётом отступов
		return;
	}


	//используем цвет текста для выделения как базовый, но подсветку не отключаем
	const bool isSelected = option.state.testFlag(QStyle::State_Selected);
	const QColor baseTextColor = isSelected
		? opt.palette.color(QPalette::HighlightedText)
		: opt.palette.color(QPalette::Text);


	//вертикальный разделитель после заданных логических столбцов
	if(m_verticalSeparatorsEnabled) {
		if(m_verticalSeparatorColumns.contains(index.column())) {
			painter->save();
			QPen colPen(m_verticalSeparatorColor);
			colPen.setWidth(m_verticalSeparatorThickness);
			colPen.setCosmetic(true);
			painter->setPen(colPen);
			const int x = opt.rect.right();
			painter->drawLine(x, opt.rect.top(), x, opt.rect.bottom());
			painter->restore();
		}
	}


	switch(index.column()) {
	case DisasmModel::Address:
	{
		const QColor effectiveAddress = m_addressTextColor.isValid() ? m_addressTextColor : baseTextColor;
		renderPlainColored(painter, paddedRect, opt.font, cellText, effectiveAddress, opt.displayAlignment);
		return;
	}
	case DisasmModel::Bytes:
	{
		renderPlainColored(painter, paddedRect, opt.font, cellText, baseTextColor, opt.displayAlignment);
		return;
	}
	case DisasmModel::Mnemonic:
	{
		const QString mnemonicLower = cellText.toLower();
		const QColor mnemonicColor = colorForGroup(classifyMnemonic(mnemonicLower), baseTextColor);
		renderPlainColored(painter, paddedRect, opt.font, cellText, mnemonicColor, opt.displayAlignment);
		return;
	}
	case DisasmModel::Operands:
	{
		renderOperandsHighlighted(painter, paddedRect, opt.font, cellText, baseTextColor);
		return;
	}
	default:
	{
		renderPlainColored(painter, paddedRect, opt.font, cellText, baseTextColor, opt.displayAlignment);
		return;
	}
	}
}

DisasmDelegate::InstrGroup DisasmDelegate::classifyMnemonic(const QString& mnemonicLower) {
	//контроль потока
	if(mnemonicLower == "call" || mnemonicLower == "ret" || mnemonicLower == "jmp" ||
		mnemonicLower.startsWith('j') || //условные jcc
		mnemonicLower == "loop" || mnemonicLower == "loope" || mnemonicLower == "loopne" ||
		mnemonicLower == "b" || mnemonicLower == "bl" || mnemonicLower == "br" ||
		mnemonicLower == "blr" || mnemonicLower == "cbz" || mnemonicLower == "cbnz" ||
		mnemonicLower == "tbz" || mnemonicLower == "tbnz" ||
		mnemonicLower == "jal" || mnemonicLower == "jr") {
		return InstrGroup::ControlFlow;
	}
	//перемещение/адресация
	if(mnemonicLower == "mov" || mnemonicLower.startsWith("cmov") || mnemonicLower == "lea" ||
		mnemonicLower == "push" || mnemonicLower == "pop" || mnemonicLower.startsWith("movs") ||
		mnemonicLower.startsWith("movz")) {
		return InstrGroup::Move;
	}
	//арифметика/сравнение
	if(mnemonicLower == "add" || mnemonicLower == "sub" || mnemonicLower == "mul" ||
		mnemonicLower == "imul" || mnemonicLower == "div" || mnemonicLower == "idiv" ||
		mnemonicLower == "inc" || mnemonicLower == "dec" || mnemonicLower == "adc" ||
		mnemonicLower == "sbb" || mnemonicLower == "cmp") {
		return InstrGroup::Arithmetic;
	}
	//логика/биты
	if(mnemonicLower == "and" || mnemonicLower == "or" || mnemonicLower == "xor" ||
		mnemonicLower == "not" || mnemonicLower == "shl" || mnemonicLower == "shr" ||
		mnemonicLower == "sar" || mnemonicLower == "rol" || mnemonicLower == "ror" ||
		mnemonicLower == "bt" || mnemonicLower == "bts" || mnemonicLower == "btr" ||
		mnemonicLower == "btc" || mnemonicLower == "test") {
		return InstrGroup::LogicBit;
	}
	return InstrGroup::Other;
}

QColor DisasmDelegate::colorForGroup(InstrGroup group, const QColor& baseColor) {
	switch(group) {
	case InstrGroup::ControlFlow: return QColor(Qt::red);
	case InstrGroup::Arithmetic:  return QColor(Qt::darkGreen);
	case InstrGroup::LogicBit:    return QColor(Qt::blue);
	case InstrGroup::Move:        return QColor(Qt::darkYellow);
	case InstrGroup::Other:       return baseColor;
	}
	return baseColor;
}

void DisasmDelegate::renderPlainColored(QPainter* painter, const QRect& rect, const QFont& font,
	const QString& text, const QColor& color, Qt::Alignment align) {
	QFont useFont = font;

	painter->save();
	painter->setFont(useFont);
	painter->setPen(color);

	const QFontMetrics metrics(useFont);
	const QString elided = metrics.elidedText(text, Qt::ElideRight, rect.width());

	const Qt::Alignment finalAlign = (align == Qt::Alignment{}) ? kDefaultAlign : align;
	painter->drawText(rect, finalAlign, elided);
	painter->restore();
}

void DisasmDelegate::renderOperandsHighlighted(QPainter* painter, const QRect& rect, const QFont& font,
	const QString& text, const QColor& baseColor) const {
	initRegexIfNeeded();

	QFont useFont = font;
	const QFontMetrics metrics(useFont);
	const QString elided = metrics.elidedText(text, Qt::ElideRight, rect.width());

	QTextLayout layout(elided, useFont);
	QTextOption textOption;
	textOption.setWrapMode(QTextOption::NoWrap);
	layout.setTextOption(textOption);

	QVector<QTextLayout::FormatRange> formatRanges;

	//базовый цвет
	QTextCharFormat baseFormat;
	baseFormat.setForeground(baseColor);
	formatRanges.push_back(QTextLayout::FormatRange{0, (int)elided.size(), baseFormat});

	//регистры
	{
		QTextCharFormat regFormat;
		regFormat.setForeground(m_registerColor);
		QRegularExpressionMatchIterator iterator = m_registerRegex.globalMatch(elided);
		while(iterator.hasNext()) {
			const QRegularExpressionMatch match = iterator.next();
			if(match.hasMatch()) {
				QTextLayout::FormatRange range;
				range.start = match.capturedStart();
				range.length = match.capturedLength();
				range.format = regFormat;
				formatRanges.push_back(range);
			}
		}
	}

	//hex immediates
	{
		QTextCharFormat immFormat;
		immFormat.setForeground(m_immediateColor);
		QRegularExpressionMatchIterator iterator = m_hexImmRegex.globalMatch(elided);
		while(iterator.hasNext()) {
			const QRegularExpressionMatch match = iterator.next();
			if(match.hasMatch()) {
				QTextLayout::FormatRange range;
				range.start = match.capturedStart();
				range.length = match.capturedLength();
				range.format = immFormat;
				formatRanges.push_back(range);
			}
		}
	}

	//decimal immediates
	{
		QTextCharFormat immFormat;
		immFormat.setForeground(m_immediateColor);
		QRegularExpressionMatchIterator iterator = m_decImmRegex.globalMatch(elided);
		while(iterator.hasNext()) {
			const QRegularExpressionMatch match = iterator.next();
			if(match.hasMatch()) {
				QTextLayout::FormatRange range;
				range.start = match.capturedStart();
				range.length = match.capturedLength();
				range.format = immFormat;
				formatRanges.push_back(range);
			}
		}
	}

	layout.setFormats(formatRanges);
	layout.beginLayout();
	QTextLine line = layout.createLine();
	if(line.isValid()) {
		line.setLineWidth(static_cast<qreal>(rect.width()));
		layout.endLayout();

		const qreal yOffset = rect.top() + (rect.height() - line.height()) / 2.0;
		painter->save();
		painter->setClipRect(rect);
		layout.draw(painter, QPointF(rect.left(), yOffset));
		painter->restore();
	} else {
		layout.endLayout();
		renderPlainColored(painter, rect, useFont, elided, baseColor, kDefaultAlign);
	}
}
