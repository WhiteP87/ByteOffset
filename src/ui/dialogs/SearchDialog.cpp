#include "SearchDialog.h"
#include "ui_SearchDialog.h"

SearchDialog::SearchDialog(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::SearchDialog())
{
	ui->setupUi(this);
	ui->hexEdit->setMaxLines(4);
}

SearchDialog::~SearchDialog()
{
	delete ui;
}

void SearchDialog::setEditsFont(const QFont& font) {
	ui->hexEdit->setFont(font);
}
