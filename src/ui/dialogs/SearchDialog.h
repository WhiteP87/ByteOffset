#pragma once

#include <QDialog>
QT_BEGIN_NAMESPACE
namespace Ui { class SearchDialog; };
QT_END_NAMESPACE

class SearchDialog : public QDialog
{
	Q_OBJECT

public:
	SearchDialog(QWidget *parent = nullptr);
	~SearchDialog();
	void setEditsFont(const QFont& font);
private:
	Ui::SearchDialog *ui;
};
