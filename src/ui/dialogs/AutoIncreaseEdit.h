#pragma once
#include <QPlainTextEdit>

class AutoIncreaseEdit: public QPlainTextEdit {
	Q_OBJECT
public:
	explicit AutoIncreaseEdit(QWidget* parent = nullptr);
	void setMaxLines(int maxLines) { m_maxLines = maxLines; updateHeight(); }
private:
	int m_maxLines = 3;
	int visualLineCount() const;
protected:
	void resizeEvent(QResizeEvent* e) override;
	void changeEvent(QEvent* e) override;
private slots:
	void updateHeight();
};