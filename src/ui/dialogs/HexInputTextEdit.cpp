#include "HexInputTextEdit.h"
#include <QMessageBox>
#include <QTextCursor>
#include <QTextDocument>
#include <QTimer>

HexInputTextEdit::HexInputTextEdit(QWidget* parent /*= nullptr*/):AutoIncreaseEdit(parent) {
	connect(this, &QPlainTextEdit::selectionChanged, this, &HexInputTextEdit::normalizeSelection);
	new QuestionHighlighter(document());
}

//вставка ниббла в зависимости от положения курсора относительно токена: 
//если курсор перед токеном, то заменяем или вставляем первый ниббл, если внутри, то 
//перезаписываем или вставляем второй, если после, то вставляем новый токен
void HexInputTextEdit::inputHexNibble(QChar ch) {

	auto c = textCursor();
	auto overwriteOne = [&] {
		if(!c.hasSelection())
			c.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 1);
		c.insertText(QString(ch));
		};

	switch(PosInToken pos = posInTokenFromCursor(c);pos)
	{
		case PosInToken::Before:
			overwriteOne();
			if(c.atEnd() || isSep(document()->characterAt(c.position()))) {
				c.insertText("?");
				c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
			}
			break;
		case PosInToken::Inside:
			overwriteOne();
			break;
		case PosInToken::After:
			c.insertText(QString(" %1?").arg(ch));
			c.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor, 1);
			break;
		default: break;
	}
	setTextCursor(c);
}

//начало токена в позиции pos
int HexInputTextEdit::tokenStartPos(int pos) {
	int start = pos;
	while(start > 0 && !isSep(document()->characterAt(start - 1))) --start;
	return start;
}

//конец токена в позиции pos
int HexInputTextEdit::tokenEndPos(int pos) {
	int end = pos;
	auto doc = document();
	while(!doc->characterAt(end).isNull() && !isSep(doc->characterAt(end))) ++end;
	return end;
}

//удаление лишних символов, вставка пробелов между токенами, перевод в верхний регистр
QString HexInputTextEdit::normalizeHexPattern(const QString& pattern) {
	QString out;
	out.reserve(pattern.size() * 2);

	int tokenPos = 0;
	for(QChar ch : pattern) {
		ch = ch.toUpper();
		if(!isAllowed(ch)) {
			if(isSep(ch)) continue;
			ch = '?';
		}		

		if(tokenPos == 0) {
			if(!out.isEmpty())
				out.append(u' ');
			out.append(ch);
			tokenPos = 1;
		} else {
			out.append(ch);
			tokenPos = 0;
		}
	}
	return out;
}

//нормализация текста в поле
void HexInputTextEdit::normalizeInput() {
	auto pos = textCursor().position();
	QTimer::singleShot(0, this, [this,pos] {
		QSignalBlocker b(this);
		const QString raw = toPlainText();
		const QString normalized = normalizeHexPattern(raw);
		if(normalized != raw)
			setPlainText(normalized);

		QTextCursor c = textCursor();
		const int clamped = qMin(pos, document()->characterCount() - 1);

		c.setPosition(tokenEndPos(clamped));
		setTextCursor(c);
		});
}

//расширение выделения по границы токена
void HexInputTextEdit::normalizeSelection() {
	static bool fixing = false;
	if(fixing) return;

	QTextCursor c = textCursor();
	if(!c.hasSelection()) return;

	const int selStart = c.selectionStart();
	const int selEnd = c.selectionEnd();
	const bool caretAtEnd = (c.position() == selEnd);

	int startTok = tokenStartPos(selStart);
	int endTok = tokenEndPos(selEnd);

	fixing = true;

	if(caretAtEnd) {
		c.setPosition(startTok);
		c.setPosition(endTok, QTextCursor::KeepAnchor);
	} else {
		c.setPosition(endTok);
		c.setPosition(startTok, QTextCursor::KeepAnchor);
	}
	setTextCursor(c);

	fixing = false;
}

//определение положения курсора относительно токена
PosInToken HexInputTextEdit::posInTokenFromCursor(const QTextCursor& c) {
	const int pos = c.hasSelection() ? c.selectionStart() : c.position();
	const int inside = pos - tokenStartPos(pos);
	if(inside <= 0) return PosInToken::Before;//перед токеном
	if(inside == 1) return PosInToken::Inside;//в середине токена
	if(inside == 2) return PosInToken::After;//после токена
	return PosInToken::Before;
}

//обработка нажатия клавиши
void HexInputTextEdit::keyPressEvent(QKeyEvent* e) {
	//обрабатываем текст
	if(const QString text = e->text(); text.length() == 1) {
		const QChar ch = e->text().at(0).toUpper();

		if(isAllowed(ch)) {
			inputHexNibble(ch);
			return;
		}
	}
	e->ignore();
}

//обработка вставки из буфера обмена, включая drag&drop. 
//вставляем как есть, а потом нормализуем весь текст
void HexInputTextEdit::insertFromMimeData(const QMimeData* source) {
	auto cur = textCursor();
	if(posInTokenFromCursor(cur) == PosInToken::Inside) {
		cur.setPosition(tokenEndPos(cur.position()));
		setTextCursor(cur);
	}
	
	QPlainTextEdit::insertFromMimeData(source);
	normalizeInput();
}