#pragma once

#include <QDialog>
#include "ui_BookmarksDialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class BookmarksDialog; };
QT_END_NAMESPACE

class BookmarksDialog : public QDialog
{
	Q_OBJECT

public:
	BookmarksDialog(QWidget *parent = nullptr);
	~BookmarksDialog();

private:
	Ui::BookmarksDialog *ui;
};

