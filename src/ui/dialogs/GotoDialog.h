#pragma once

#include <QDialog>
#include "ui_GotoDialog.h"

QT_BEGIN_NAMESPACE
namespace Ui { class GotoDialog; };
QT_END_NAMESPACE

class GotoDialog : public QDialog
{
	Q_OBJECT

public:
	GotoDialog(QWidget *parent = nullptr);
	~GotoDialog();
private:
	Ui::GotoDialog *ui;
	bool parseOffset(const QString& text, quint64& outValue);
private slots:
	void onGoBtn();
	void onCancelBtn();
signals:
	void gotoRequest(qint64 offset, bool setCaret);
};

