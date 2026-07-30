#include "GotoDialog.h"
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QLineEdit>
#include "MainWindow.h"
#include "AddBlockDialog.h"

GotoDialog::GotoDialog(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::GotoDialog())
{	
	ui->setupUi(this);

	QLineEdit* edit = ui->offsetLineEdit;
	QRegularExpression rx(R"((0[xX][0-9a-fA-F]+)|(0[0-7]+)|([1-9]\d*)|0)");
	edit->setValidator(new QRegularExpressionValidator(rx, edit));

	connect(ui->goBtn, &QPushButton::clicked, this, &GotoDialog::onGoBtn);
	connect(ui->cancelBtn, &QPushButton::clicked, this, &GotoDialog::onCancelBtn);
}

GotoDialog::~GotoDialog()
{
	delete ui;
}

void GotoDialog::onCancelBtn() {
	this->close();
}

void GotoDialog::onGoBtn() {
	QString text = ui->offsetLineEdit->text();
	quint64 offset{};
	bool ok = parseOffset(text, offset);
	if(ok) emit gotoRequest(offset, ui->setCaretCheckBox->isChecked());
}

bool GotoDialog::parseOffset(const QString& text, quint64& outValue) {
	bool ok = false;
	if(text.startsWith("0x", Qt::CaseInsensitive)) {
		outValue = text.mid(2).toULongLong(&ok, 16);
	} else if(text.startsWith("0") && text.size() > 1) {
		outValue = text.toULongLong(&ok, 8);
	} else {
		outValue = text.toULongLong(&ok, 10);
	}
	return ok;
}