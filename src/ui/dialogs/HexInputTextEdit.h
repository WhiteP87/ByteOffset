#pragma once
#include "AutoIncreaseEdit.h"
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

enum class PosInToken {
	Before = 0,
	Inside = 1,
	After = 2
};

namespace {

	class QuestionHighlighter final: public QSyntaxHighlighter {
	public:
		explicit QuestionHighlighter(QTextDocument* doc)
			: QSyntaxHighlighter(doc) {
			fmt.setForeground(QColor(220, 80, 80));
		}

	protected:
		void highlightBlock(const QString& text) override {
			const QChar* p = text.constData();
			for(int i = 0; i < text.size(); ++i)
				if(p[i] == '?')
					setFormat(i, 1, fmt);
		}

	private:
		QTextCharFormat fmt;
	};
} // namespace

class HexInputTextEdit: public AutoIncreaseEdit {
	Q_OBJECT
public:
	explicit HexInputTextEdit(QWidget* parent = nullptr);
protected:
	virtual void keyPressEvent(QKeyEvent* e) override;
	virtual void insertFromMimeData(const QMimeData* source) override;
private:

	PosInToken posInTokenFromCursor(const QTextCursor& c);
	void inputHexNibble(QChar ch);
	int tokenStartPos(int pos);
	int tokenEndPos(int pos);
	QString normalizeHexPattern(const QString& pattern);
	void normalizeInput();

	inline bool isHex(QChar ch) {
		return (ch >= u'0' && ch <= u'9') || (ch >= u'A' && ch <= u'F') || (ch >= u'a' && ch <= u'f');
	}

	inline bool isSep(QChar ch) {
		return ch.isNull() || ch == u' ' || ch == QChar::ParagraphSeparator;
	}

	inline bool isWildcard(QChar ch) {
		return ch == '?';
	}

	inline bool isAllowed(QChar ch) {
		ch = ch.toUpper();
		return isHex(ch) || isWildcard(ch);
	}

private slots:
	void normalizeSelection();
};