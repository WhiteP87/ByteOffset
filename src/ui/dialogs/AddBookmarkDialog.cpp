#include "AddBookmarkDialog.h"
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include "uiUtils.h"
#include <QMessageBox>

AddBookmarkDialog::AddBookmarkDialog(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::AddBookmarkDialog())
{
	ui->setupUi(this);

	QLineEdit* startOffEdit = ui->startOffsetLineEdit;
	QRegularExpression rx(R"((0[xX][0-9a-fA-F]+)|(0[0-7]+)|([1-9]\d*)|0)");
	startOffEdit->setValidator(new QRegularExpressionValidator(rx, startOffEdit));
	startOffEdit->setText("0x0");

	connect(ui->plusStartOffsetButton, &QPushButton::clicked, this, &AddBookmarkDialog::onPlusStartOffsetBtn);
	connect(ui->minusStartOffsetButton, &QPushButton::clicked, this, &AddBookmarkDialog::onMinusStartOffsetBtn);
	connect(ui->deleteButton, &QPushButton::clicked, this, &AddBookmarkDialog::deleteBookmark);
	connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);
	connect(ui->createButton, &QPushButton::clicked, this, &AddBookmarkDialog::onCreateButton);

	ui->deleteButton->setVisible(false);
}

AddBookmarkDialog::~AddBookmarkDialog()
{
	delete ui;
}

void AddBookmarkDialog::editExistingBookmark(qint64 startOffset, const QString& comment) {
	ui->startOffsetLineEdit->setText(QString("0x%1").arg(startOffset, 0, 16));
	ui->commentEdit->setPlainText(comment);

	ui->deleteButton->setVisible(true);
	ui->createButton->setText("Обновить");
}

void AddBookmarkDialog::setOffset(qint64 startOffset) {
	ui->startOffsetLineEdit->setText(QString("0x%1").arg(startOffset, 0, 16));
	m_startOffset = startOffset;
}

void AddBookmarkDialog::onPlusStartOffsetBtn() {
	qint64 val{}; qint32 base{};
	if(parseOffset(ui->startOffsetLineEdit->text(), val, &base)) {
		if(val + 1 > 0)
			ui->startOffsetLineEdit->setText(getFormatString(base).arg(++val, 0, base));
	}
}

void AddBookmarkDialog::onMinusStartOffsetBtn() {
	qint64 val{}; qint32 base{};
	if(parseOffset(ui->startOffsetLineEdit->text(), val, &base)) {
		if(val - 1 >= 0)
			ui->startOffsetLineEdit->setText(getFormatString(base).arg(--val, 0, base));
	}
}

void AddBookmarkDialog::onCreateButton() {

	qint64 startOffset{};
	QString comment{};

	bool parseStart = parseOffset(ui->startOffsetLineEdit->text(), startOffset);
	
	if(!parseStart || startOffset < 0) {
		QMessageBox::warning(this, "Ошибка ввода", "Проверьте начальное смещение закладки.");
		return;
	}

	m_startOffset = startOffset;
	m_comment = ui->commentEdit->toPlainText().trimmed();

	accept();
}