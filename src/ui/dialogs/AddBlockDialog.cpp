#include "AddBlockDialog.h"
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QLineEdit>
#include <QColorDialog>
#include <QMessageBox>
#include "uiUtils.h"


AddBlockDialog::AddBlockDialog(DialogType type, QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::AddBlockDialog()) {

	ui->setupUi(this);

	type == DialogType::Block ? setBlockModeUI(false) : setFieldModeUI(false);

	QLineEdit* startOffEdit = ui->startOffsetLineEdit;
	QLineEdit* endOffEdit = ui->endOffsetLineEdit;

	QRegularExpression rx(R"((0[xX][0-9a-fA-F]+)|(0[0-7]+)|([1-9]\d*)|0)");
	startOffEdit->setValidator(new QRegularExpressionValidator(rx, startOffEdit));
	endOffEdit->setValidator(new QRegularExpressionValidator(rx, endOffEdit));

	startOffEdit->setText("0x0");
	endOffEdit->setText("0x0");

	ui->colorFrame->installEventFilter(this);
	ui->colorFrame->setAutoFillBackground(true);
	QPalette pal = ui->colorFrame->palette();
	pal.setColor(QPalette::Window, Qt::black);
	ui->colorFrame->setPalette(pal);
	ui->deleteButton->setVisible(false);

	connect(ui->plusStartOffsetButton, &QPushButton::clicked, this, &AddBlockDialog::onPlusStartOffsetBtn);
	connect(ui->minusStartOffsetButton, &QPushButton::clicked, this, &AddBlockDialog::onMinusStartOffsetBtn);
	connect(ui->plusEndOffsetButton, &QPushButton::clicked, this, &AddBlockDialog::onPlusEndOffsetBtn);
	connect(ui->minusEndOffsetButton, &QPushButton::clicked, this, &AddBlockDialog::onMinusEndOffsetBtn);
	connect(ui->createButton, &QPushButton::clicked, this, &AddBlockDialog::onCreateButton);
	connect(ui->cancelButton, &QPushButton::clicked, this, &QDialog::reject);


	connect(ui->nameLineEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
		QString upper = text.toUpper();
		if(text != upper) {
			int cursorPos = ui->nameLineEdit->cursorPosition();
			ui->nameLineEdit->setText(upper);
			ui->nameLineEdit->setCursorPosition(cursorPos);
		}
		});


	ui->byteOrderCb->addItem("LittleEndian", false);
	ui->byteOrderCb->addItem("BigEndian", true);

}

AddBlockDialog::~AddBlockDialog() {
	delete ui;
}

void AddBlockDialog::editExistingBlock(const BlockInfo& block) {
	setBlockModeUI(true);

	ui->startOffsetLineEdit->setText(QString("0x%1").arg(block.startOffset, 0, 16));
	ui->endOffsetLineEdit->setText(QString("0x%1").arg(block.endOffset, 0, 16));
	ui->nameLineEdit->setText(block.name);
	ui->commentEdit->setPlainText(block.comment);

	QPalette pal = ui->colorFrame->palette();
	pal.setColor(QPalette::Window, block.color);
	ui->colorFrame->setPalette(pal);

	ui->collapsedCheckBox->setChecked(block.collapsed);
	ui->deleteButton->setVisible(true);

	const int index = ui->blockTypesCb->findData(static_cast<int>(block.blockType));
	if(index >= 0)
		ui->blockTypesCb->setCurrentIndex(index);

	ui->createButton->setText("Обновить");
}

void AddBlockDialog::editExistingField(const FieldInfo& field) {
	setFieldModeUI(true);

	ui->deleteButton->setVisible(true);

	ui->startOffsetLineEdit->setText(QString("0x%1").arg(field.startOffset, 0, 16));
	ui->endOffsetLineEdit->setText(QString("0x%1").arg(field.endOffset, 0, 16));
	ui->nameLineEdit->setText(field.name);
	ui->commentEdit->setPlainText(field.comment);

	QPalette pal = ui->colorFrame->palette();
	pal.setColor(QPalette::Window, field.color);
	ui->colorFrame->setPalette(pal);

	const int index = ui->blockTypesCb->findData(static_cast<int>(field.type));
	if(index >= 0)
		ui->blockTypesCb->setCurrentIndex(index);
}

void AddBlockDialog::setOffsets(qint64 startOffset, qint64 endOffset) {

	if(startOffset < 0 || endOffset < 0) {
		startOffset = 0, endOffset = 1;
	}

	ui->startOffsetLineEdit->setText(QString("0x%1").arg(startOffset, 0, 16));
	ui->endOffsetLineEdit->setText(QString("0x%1").arg(endOffset, 0, 16));
	m_startOffset = startOffset;
	m_endOffset = endOffset;
}

void AddBlockDialog::setFieldModeUI(bool editMode) {
	m_fieldMode = true;
	ui->collapsedCheckBox->setVisible(false);

	this->setWindowTitle(QString("%1 поле").arg(editMode ? "Редактировать" : "Создать"));
	ui->nameLabel->setText("Название поля");


	ui->blockTypesCb->clear();
	for(const auto& entry : FIELD_TYPE_TABLE) {
		ui->blockTypesCb->addItem(
			QString(entry.name),                 // текст, видимый пользователю
			static_cast<int>(entry.type)         // пользовательские данные
		);
	}

	ui->collapsedCheckBox->setVisible(false);

	if(editMode) {
		ui->deleteButton->setVisible(true);
		ui->createButton->setText("Обновить");
	}

	QObject::disconnect(ui->deleteButton, &QPushButton::clicked, this, nullptr);
	connect(ui->deleteButton, &QPushButton::clicked, this, &AddBlockDialog::deleteField);
}

void AddBlockDialog::setBlockModeUI(bool editMode) {
	m_fieldMode = false;

	ui->blockTypesCb->clear();
	for(const auto& entry : BLOCK_TYPE_TABLE) {
		ui->blockTypesCb->addItem(
			QString(entry.name),                 // текст, видимый пользователю
			static_cast<int>(entry.type)         // пользовательские данные
		);
	}

	this->setWindowTitle(QString("%1 блок").arg(editMode ? "Редактировать" : "Создать"));
	ui->nameLabel->setText("Название блока");
	ui->collapsedCheckBox->setVisible(true);

	if(editMode) {
		ui->deleteButton->setVisible(true);
		ui->createButton->setText("Обновить");
		ui->collapsedCheckBox->setText("Свёрнут");
	}

	QObject::disconnect(ui->deleteButton, &QPushButton::clicked, this, nullptr);
	connect(ui->deleteButton, &QPushButton::clicked, this, &AddBlockDialog::deleteBlock);
}

void AddBlockDialog::onPlusStartOffsetBtn() {
	qint64 val{}; qint32 base{};
	if(parseOffset(ui->startOffsetLineEdit->text(), val, &base)) {
		if(val + 1 > 0)
			ui->startOffsetLineEdit->setText(getFormatString(base).arg(++val, 0, base));
	}
}

void AddBlockDialog::onMinusStartOffsetBtn() {
	qint64 val{}; qint32 base{};
	if(parseOffset(ui->startOffsetLineEdit->text(), val, &base)) {
		if(val - 1 >= 0)
			ui->startOffsetLineEdit->setText(getFormatString(base).arg(--val, 0, base));
	}
}

void AddBlockDialog::onPlusEndOffsetBtn() {
	qint64 val{}; qint32 base{};
	if(parseOffset(ui->endOffsetLineEdit->text(), val, &base)) {
		if(val + 1 > 0)
			ui->endOffsetLineEdit->setText(getFormatString(base).arg(++val, 0, base));
	}
}

void AddBlockDialog::onMinusEndOffsetBtn() {
	qint64 val{}; qint32 base{};
	if(parseOffset(ui->endOffsetLineEdit->text(), val, &base)) {
		if(val - 1 >= 0)
			ui->endOffsetLineEdit->setText(getFormatString(base).arg(--val, 0, base));
	}
}

void AddBlockDialog::onChangeColorBtn() {

	QPalette pal = ui->colorFrame->palette();
	QColorDialog dlg(this);
	dlg.setOption(QColorDialog::ShowAlphaChannel);
	dlg.setCurrentColor(pal.window().color()); // начальный цвет
	if(dlg.exec() == QDialog::Accepted) {
		pal.setColor(QPalette::Window, dlg.selectedColor());
		ui->colorFrame->setPalette(pal);
	}
}

void AddBlockDialog::onCreateButton() {

	qint64 startOffset{}, endOffset{};
	QString name{}, comment{};

	bool parseStart = parseOffset(ui->startOffsetLineEdit->text(), startOffset);
	bool parseEnd = parseOffset(ui->endOffsetLineEdit->text(), endOffset);

	if(!parseStart || startOffset < 0) {
		QMessageBox::warning(this, "Ошибка ввода", "Проверьте начальное смещение.");
		return;
	}

	if(!parseEnd || endOffset < 0) {
		QMessageBox::warning(this, "Ошибка ввода", "Проверьте конечное смещение.");
		return;
	}

	if(startOffset > endOffset) {
		std::swap(startOffset, endOffset);
	}

	if(name = ui->nameLineEdit->text(); name.isEmpty()) {
		if((QMessageBox::question(this, "Имя не задано", "Имя не задано. Будет отображаться имя по умолчанию. Продолжить?",
			QMessageBox::Yes | QMessageBox::No)) == QMessageBox::No) {
			return;
		}
	}


	if(!m_fieldMode && startOffset == endOffset) {
		QMessageBox::warning(this, "Размер блока", "Блок длиною в один байт не поддерживается");
		return;
	}

	m_startOffset = startOffset;
	m_endOffset = endOffset;
	m_name = name.trimmed();
	m_comment = ui->commentEdit->toPlainText().trimmed();
	m_collapsed = ui->collapsedCheckBox->isChecked();
	m_color = ui->colorFrame->palette().window().color();
	if(!m_fieldMode) {
		m_blockType = static_cast<BlockType>(ui->blockTypesCb->currentData().toInt());
	} else {
		m_fieldType = static_cast<FieldType>(ui->blockTypesCb->currentData().toInt());
	}

	m_byteOrder = ui->byteOrderCb->currentData().toBool();
	accept();
}

bool AddBlockDialog::eventFilter(QObject* obj, QEvent* event) {
	if(obj == ui->colorFrame && event->type() == QEvent::MouseButtonPress) {
		auto* mouseEvent = static_cast<QMouseEvent*>(event);
		if(mouseEvent->button() == Qt::LeftButton) {
			onChangeColorBtn();
			return true;
		}
	}
	return QDialog::eventFilter(obj, event);
}