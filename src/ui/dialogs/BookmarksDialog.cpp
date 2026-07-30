#include "BookmarksDialog.h"

BookmarksDialog::BookmarksDialog(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::BookmarksDialog())
{
	ui->setupUi(this);
}

BookmarksDialog::~BookmarksDialog()
{
	delete ui;
}

