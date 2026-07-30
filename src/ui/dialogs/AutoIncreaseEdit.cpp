#include "AutoIncreaseEdit.h"
#include <QTextBlock>
#include <QTimer>
#include <QScrollBar>
#include <QStyle>

AutoIncreaseEdit::AutoIncreaseEdit(QWidget* parent /*= nullptr*/):QPlainTextEdit(parent) {

	setLineWrapMode(QPlainTextEdit::WidgetWidth);
	setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

	connect(document(), &QTextDocument::contentsChanged, this, [this] {
		QTimer::singleShot(0, this, &AutoIncreaseEdit::updateHeight);
		});

	updateHeight();
}

int AutoIncreaseEdit::visualLineCount() const {
	int total = 0;
	for(QTextBlock txtBlock = document()->begin(); txtBlock.isValid(); txtBlock = txtBlock.next()) {
		if(auto* lay = txtBlock.layout())
			total += qMax(1, lay->lineCount());   
		else
			total += 1;
	}
	return qMax(1, total);
}

void AutoIncreaseEdit::resizeEvent(QResizeEvent* e) {
	QPlainTextEdit::resizeEvent(e);
	QTimer::singleShot(0, this, &AutoIncreaseEdit::updateHeight);
}

void AutoIncreaseEdit::changeEvent(QEvent* e) {
	QPlainTextEdit::changeEvent(e);

	switch(e->type()) {
	case QEvent::FontChange:
	case QEvent::ApplicationFontChange:
	case QEvent::StyleChange:
	case QEvent::PaletteChange:
	case QEvent::LayoutRequest:
		QTimer::singleShot(0, this, &AutoIncreaseEdit::updateHeight);
		break;
	default:
		break;
	}
}

void AutoIncreaseEdit::updateHeight() {

	const int lineCount = visualLineCount();

	const int visible = qBound(1, lineCount, m_maxLines);
	const int lineHeight = fontMetrics().lineSpacing();
	const int frame = frameWidth() * 2;
	const int docMargin = int(std::ceil(document()->documentMargin())) * 2;
	const int extra = 2;

	const int h = frame + docMargin + extra + visible * lineHeight;
	setFixedHeight(h);

	const bool needScroll = (lineCount > m_maxLines);
	setVerticalScrollBarPolicy(needScroll ? Qt::ScrollBarAsNeeded
		: Qt::ScrollBarAlwaysOff);
	if(!needScroll)
		verticalScrollBar()->setValue(0);
}
