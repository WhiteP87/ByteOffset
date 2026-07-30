#pragma once

#include <QDialog>
#include "ui_AddBookmarkDialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class AddBookmarkDialog; };
QT_END_NAMESPACE

class AddBookmarkDialog : public QDialog
{
	Q_OBJECT

public:
	AddBookmarkDialog(QWidget *parent = nullptr);
	~AddBookmarkDialog();
	qint64 startOffset() const { return m_startOffset; }
	QString comment() const { return m_comment; }
	void editExistingBookmark(qint64 startOffset, const QString& comment);
	void setOffset(qint64 startOffset);
private slots:
	void onCreateButton();
	void onPlusStartOffsetBtn();
	void onMinusStartOffsetBtn();
private:
	Ui::AddBookmarkDialog *ui;
	qint64 m_startOffset{};
	QString m_comment{};
signals:
	void deleteBookmark();
};

