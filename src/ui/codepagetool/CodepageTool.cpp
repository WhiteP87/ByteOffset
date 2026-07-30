#include "CodepageTool.h"
#include <QMenuBar>
#include <QHeaderView>
#include <QVBoxLayout>
#include <cstring>

static inline bool isValidUnicodeScalar(quint32 cp) {
	// Допустимые скаляры: <= U+10FFFF, не суррогаты
	return (cp <= 0x10FFFFu) && !(cp >= 0xD800u && cp <= 0xDFFFu);
}

CodepageTool::CodepageTool(QWidget* parent)
	: QWidget(parent)
	, ui(new Ui::CodepageTool()) {
	ui->setupUi(this);
	configureTable();
	createMainMenu();
	// отображение u+ в реальный символ
	connect(ui->cpTable, &QTableWidget::itemChanged,
		this, &CodepageTool::onCellEdited);
}

CodepageTool::~CodepageTool() {
	delete ui;
}

void CodepageTool::onLoadJson() {
	const QString filePath = QFileDialog::getOpenFileName(
		this, tr("Открыть JSON"), QString(), tr("JSON (*.json)")
	);
	if(filePath.isEmpty()) return;

	SingleByteTable tableModel;
	QString errorText;
	if(!loadJsonFile(filePath, tableModel, &errorText)) {
		QMessageBox::critical(this, tr("Ошибка"), errorText);
		return;
	}
	applyTable(tableModel);
}

void CodepageTool::onSaveJson() {
	SingleByteTable tableModel;
	QString errorText;
	if(!collectTable(tableModel, &errorText)) {
		QMessageBox::warning(this, tr("Проверка"), errorText);
		return;
	}

	const QString filePath = QFileDialog::getSaveFileName(
		this, tr("Сохранить JSON"), tableModel.id + ".json", tr("JSON (*.json)")
	);
	if(filePath.isEmpty()) return;

	if(!saveJsonFile(filePath, tableModel, &errorText)) {
		QMessageBox::critical(this, tr("Ошибка"), errorText);
		return;
	}
}

void CodepageTool::onLoadSbc() {
	const QString filePath = QFileDialog::getOpenFileName(
		this, tr("Открыть SBC"), QString(), tr("SBC (*.sbc)"));
	if(filePath.isEmpty()) return;

	SingleByteTable tableModel;
	QString errorText;
	if(!loadSbcFile(filePath, tableModel, &errorText)) {
		QMessageBox::critical(this, tr("Ошибка"), errorText);
		return;
	}
	applyTable(tableModel);
}

void CodepageTool::onSaveSbc() {
	SingleByteTable tableModel;
	QString errorText;
	if(!collectTable(tableModel, &errorText)) {
		QMessageBox::warning(this, tr("Проверка"), errorText);
		return;
	}

	const QString filePath = QFileDialog::getSaveFileName(
		this, tr("Сохранить SBC"), tableModel.id + ".sbc", tr("SBC (*.sbc)"));
	if(filePath.isEmpty()) return;

	if(!saveSbcFile(filePath, tableModel, &errorText)) {
		QMessageBox::critical(this, tr("Ошибка"), errorText);
		return;
	}
}

void CodepageTool::onAutoAscii() {
	QTableWidget* table = ui->cpTable;
	for(int byteValue = 0; byteValue < 256; ++byteValue) {
		const int row = byteValue / 16;
		const int col = byteValue % 16;

		// если ячейка пустая — проставляем ASCII для 0x20..0x7E
		QTableWidgetItem* item = table->item(row, col);
		const QString currentText = item ? item->text().trimmed() : QString();
		if(!currentText.isEmpty())
			continue;

		if(byteValue >= 0x20 && byteValue <= 0x7E) {
			const char32_t codepoint = static_cast<char32_t>(byteValue);
			table->item(row, col)->setText(formatCellValue(codepoint));
		}
	}
}

void CodepageTool::onClearTable() {
	QTableWidget* table = ui->cpTable;
	for(int row = 0; row < table->rowCount(); ++row) {
		for(int col = 0; col < table->columnCount(); ++col) {
			QTableWidgetItem* item = table->item(row, col);
			if(item) item->setText(QString());
		}
	}
	ui->idEdit->clear();
	ui->labelEdit->clear();
}

void CodepageTool::configureTable() {
	auto* table = ui->cpTable;

	//16x16
	table->clear();
	table->setRowCount(16);
	table->setColumnCount(16);
	for(int r = 0; r < 16; ++r) {
		for(int c = 0; c < 16; ++c) {
			auto* it = new QTableWidgetItem;
			it->setTextAlignment(Qt::AlignCenter);
			table->setItem(r, c, it);
		}
	}

	//квадратные ячейки (фиксированные размеры)
	const int cellSize = 30;
	table->horizontalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
	for(int c = 0; c < 16; ++c) table->setColumnWidth(c, cellSize);
	for(int r = 0; r < 16; ++r) table->setRowHeight(r, cellSize);

	//белая таблица в любой теме
	table->setStyleSheet(
		"QTableWidget { background-color: white; color: black; }"
		"QTableWidget::item:selected { background-color: #cfe8ff; color: black; }"
	);

	//легенда 0..F
	QStringList labels;
	for(int i = 0; i < 16; ++i)
		labels << QString::number(i, 16).toUpper();
	table->horizontalHeader()->setVisible(true);
	table->verticalHeader()->setVisible(true);
	table->setHorizontalHeaderLabels(labels);
	table->setVerticalHeaderLabels(labels);

	//без растягивания последней секции, без сортировки
	table->horizontalHeader()->setStretchLastSection(false);
	table->verticalHeader()->setStretchLastSection(false);
	table->setShowGrid(true);
	table->setAlternatingRowColors(false);
	table->setSelectionBehavior(QAbstractItemView::SelectItems);
	table->setSelectionMode(QAbstractItemView::SingleSelection);
	table->setEditTriggers(QAbstractItemView::AllEditTriggers);
}

void CodepageTool::createMainMenu() {
	//получаем или создаем для виджет вертикальный лэйаут
	QVBoxLayout* rootLayout = qobject_cast<QVBoxLayout*>(this->layout());
	if(!rootLayout) {
		rootLayout = new QVBoxLayout(this);
		this->setLayout(rootLayout);
	}

	//создаем меню
	QMenuBar* menuBar = new QMenuBar(this);
	rootLayout->setMenuBar(menuBar);

	//файл
	QMenu* fileMenu = menuBar->addMenu(tr("&Файл"));

	QAction* actionOpenJson = fileMenu->addAction(tr("Открыть &JSON…"));
	actionOpenJson->setShortcut(QKeySequence::Open);
	connect(actionOpenJson, &QAction::triggered, this, &CodepageTool::onLoadJson);

	QAction* actionSaveJson = fileMenu->addAction(tr("Сохранить &JSON…"));
	actionSaveJson->setShortcut(QKeySequence::Save);
	connect(actionSaveJson, &QAction::triggered, this, &CodepageTool::onSaveJson);

	fileMenu->addSeparator();

	QAction* actionOpenSbc = fileMenu->addAction(tr("Открыть S&BC…"));
	actionOpenSbc->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
	connect(actionOpenSbc, &QAction::triggered, this, &CodepageTool::onLoadSbc);

	QAction* actionSaveSbc = fileMenu->addAction(tr("Сохранить S&BC…"));
	actionSaveSbc->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
	connect(actionSaveSbc, &QAction::triggered, this, &CodepageTool::onSaveSbc);

	fileMenu->addSeparator();

	QAction* actionClose = fileMenu->addAction(tr("&Закрыть"));
	actionClose->setShortcut(QKeySequence::Quit);
	connect(actionClose, &QAction::triggered, this, &QWidget::close);

	//таблица
	QMenu* toolsMenu = menuBar->addMenu(tr("&Таблица"));

	QAction* actionAutoAscii = toolsMenu->addAction(tr("Заполнить &ASCII"));
	actionAutoAscii->setShortcut(QKeySequence(Qt::ALT | Qt::Key_A));
	connect(actionAutoAscii, &QAction::triggered, this, &CodepageTool::onAutoAscii);

	QAction* actionClearTable = toolsMenu->addAction(tr("Очистить"));
	connect(actionClearTable, &QAction::triggered, this, &CodepageTool::onClearTable);
}

bool CodepageTool::collectTable(SingleByteTable& outTable, QString* errorText) const {
	const QString idText = ui->idEdit->text().trimmed();
	static const QRegularExpression idRegex(QStringLiteral("^[a-z0-9._-]+$"));

	if(!idRegex.match(idText).hasMatch()) {
		if(errorText) *errorText = tr("Некорректный id: допустимы [a-z0-9._-]");
		return false;
	}

	outTable.id = idText;
	outTable.label = ui->labelEdit->text().trimmed();

	QTableWidget* table = ui->cpTable;
	for(int byteValue = 0; byteValue < 256; ++byteValue) {
		const int row = byteValue / 16;
		const int col = byteValue % 16;
		const QString cellText = table->item(row, col)->text();

		char32_t codepoint = 0;
		if(!cellText.isEmpty()) {
			if(!parseCellValue(cellText, codepoint) || !isValidUnicodeScalar(codepoint)) {
				if(errorText) {
					*errorText = tr("Ячейка %1: неверное значение")
						.arg(QString("0x%1").arg(byteValue, 2, 16, QLatin1Char('0')).toUpper());
				}
				return false;
			}
		}
		outTable.codepointMap[byteValue] = static_cast<quint32>(codepoint);
	}
	return true;
}

void CodepageTool::applyTable(const SingleByteTable& tableModel) {
	ui->idEdit->setText(tableModel.id);
	ui->labelEdit->setText(tableModel.label);

	QTableWidget* table = ui->cpTable;
	for(int byteValue = 0; byteValue < 256; ++byteValue) {
		const int row = byteValue / 16;
		const int col = byteValue % 16;
		const quint32 u = tableModel.codepointMap[byteValue];
		table->item(row, col)->setText(u ? formatCellValue(u) : QString());
	}
}

bool CodepageTool::parseCellValue(const QString& text, char32_t& outCodepoint) {
	if(text.isEmpty()) { outCodepoint = 0; return true; }

	// ВАЖНО: одиночный символ (включая пробел) — воспринимаем буквально, без trimmed()
	if(text.size() == 1) {
		outCodepoint = text.at(0).unicode();
		return true;
	}

	// Для техно-форматов можно обрезать пробелы вокруг
	const QString t = text.trimmed();
	if(t.isEmpty()) {
		// Например, ввели несколько пробелов подряд — считаем ошибкой
		return false;
	}

	// U+XXXX / U+XXXXXX
	static const QRegularExpression rxU(QStringLiteral("^[Uu]\\+([0-9A-Fa-f]{1,6})$"));
	auto m = rxU.match(t);
	if(m.hasMatch()) { outCodepoint = m.captured(1).toUInt(nullptr, 16); return true; }

	// 0xXXXX / 0xXXXXXX
	static const QRegularExpression rxH(QStringLiteral("^0x([0-9A-Fa-f]{1,6})$"));
	m = rxH.match(t);
	if(m.hasMatch()) { outCodepoint = m.captured(1).toUInt(nullptr, 16); return true; }

	// десятичное
	bool ok = false;
	uint v = t.toUInt(&ok, 10);
	if(ok) { outCodepoint = v; return true; }

	return false;
}

QString CodepageTool::formatCellValue(char32_t codepoint) const {
	if(codepoint == 0)
		return QString();
	const quint32 cp = static_cast<quint32>(codepoint);
	if(!isValidUnicodeScalar(cp)) return QString();
	const char32_t u = static_cast<char32_t>(cp);
	return QString::fromUcs4(&u, 1); // покажем сам символ
}

bool CodepageTool::loadJsonFile(const QString& filePath,
	SingleByteTable& outTable,
	QString* errorText) {
	QFile file(filePath);
	if(!file.open(QIODevice::ReadOnly)) {
		if(errorText) *errorText = file.errorString();
		return false;
	}

	const QByteArray raw = file.readAll();
	const QJsonDocument jsonDoc = QJsonDocument::fromJson(raw);
	if(!jsonDoc.isObject()) {
		if(errorText) *errorText = tr("Некорректный JSON: верхний уровень должен быть объектом");
		return false;
	}

	const QJsonObject root = jsonDoc.object();
	outTable.id = root.value(QStringLiteral("id")).toString();
	outTable.label = root.value(QStringLiteral("label")).toString(outTable.id);

	const QJsonArray mapArray = root.value(QStringLiteral("map")).toArray();
	if(mapArray.size() != 256) {
		if(errorText) *errorText = tr("Поле \"map\" должно содержать 256 элементов");
		return false;
	}
	for(int i = 0; i < 256; ++i) {
		const int v = mapArray[i].toInt();
		const quint32 u = static_cast<quint32>(v);
		if(!isValidUnicodeScalar(u) && u != 0) {
			if(errorText) *errorText = tr("Недопустимый кодпоинт в JSON (index %1)").arg(i);
			return false;
		}
		outTable.codepointMap[i] = u;
	}
	return true;
}

bool CodepageTool::saveJsonFile(const QString& filePath,
	const SingleByteTable& tableModel,
	QString* errorText) const {
	QJsonObject root;
	root.insert(QStringLiteral("id"), tableModel.id);
	root.insert(QStringLiteral("label"), tableModel.label);

	QJsonArray mapArray;
	for(int i = 0; i < 256; ++i) {
		mapArray.append(int(tableModel.codepointMap[i])); // JSON хранит число
	}
	root.insert(QStringLiteral("map"), mapArray);

	const QJsonDocument jsonDoc(root);

	QFile file(filePath);
	if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if(errorText) *errorText = file.errorString();
		return false;
	}
	const qint64 written = file.write(jsonDoc.toJson(QJsonDocument::Indented));
	if(written <= 0) {
		if(errorText) *errorText = tr("Ошибка записи файла");
		return false;
	}
	return true;
}

bool CodepageTool::saveSbcFile(const QString& filePath,
	const SingleByteTable& tableModel,
	QString* errorText) const {
	if(!isValidCodecId(tableModel.id)) {
		if(errorText) *errorText = tr("Некорректный id: допустимы [a-z0-9._-]");
		return false;
	}

	const QByteArray idUtf8 = tableModel.id.toUtf8();
	const QByteArray labelUtf8 = tableModel.label.toUtf8();

	// таблица 256*4 + id + label
	QByteArray body;
	body.resize(256 * 4);
	for(int index = 0; index < 256; ++index) {
		const quint32 u32 = tableModel.codepointMap[index];
		const quint32 le = qToLittleEndian<quint32>(u32);
		std::memcpy(body.data() + index * 4, &le, 4);
	}
	body.append(idUtf8);
	body.append(labelUtf8);

	SbcHeaderLite header{};
	std::memcpy(header.magic, "SBC1", 4);
	header.headerSize = qToLittleEndian<quint16>(24);
	header.version = qToLittleEndian<quint16>(0x0002); // оставлено как было
	header.idLen = qToLittleEndian<quint32>(quint32(idUtf8.size()));
	header.labelLen = qToLittleEndian<quint32>(quint32(labelUtf8.size()));
	header.fileSize = qToLittleEndian<quint32>(quint32(sizeof(SbcHeaderLite) + body.size()));
	header.reserved = 0;

	QFile file(filePath);
	if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
		if(errorText) *errorText = file.errorString();
		return false;
	}
	if(file.write(reinterpret_cast<const char*>(&header), sizeof(header)) != sizeof(header) ||
		file.write(body) != body.size()) {
		if(errorText) *errorText = tr("Ошибка записи файла");
		return false;
	}
	return true;
}

bool CodepageTool::loadSbcFile(const QString& filePath,
	SingleByteTable& outTable,
	QString* errorText) {
	QFile file(filePath);
	if(!file.open(QIODevice::ReadOnly)) {
		if(errorText) *errorText = file.errorString();
		return false;
	}
	const QByteArray blob = file.readAll();
	if(blob.size() < int(sizeof(SbcHeaderLite) + 256 * 4)) {
		if(errorText) *errorText = tr("Некорректный размер файла");
		return false;
	}

	SbcHeaderLite header{};
	std::memcpy(&header, blob.constData(), sizeof(header));

	if(std::memcmp(header.magic, "SBC1", 4) != 0) {
		if(errorText) *errorText = tr("Неверная сигнатура");
		return false;
	}
	const quint16 headerSize = qFromLittleEndian(header.headerSize);
	const quint16 version = qFromLittleEndian(header.version);
	const quint32 idLen = qFromLittleEndian(header.idLen);
	const quint32 labelLen = qFromLittleEndian(header.labelLen);
	const quint32 fileSize = qFromLittleEndian(header.fileSize);

	if(headerSize != 24 || version != 0x0002) {
		if(errorText) *errorText = tr("Неподдерживаемая версия/заголовок");
		return false;
	}
	if(fileSize != quint32(blob.size())) {
		if(errorText) *errorText = tr("Некорректный размер файла");
		return false;
	}

	const int mapOffset = sizeof(SbcHeaderLite);
	const int idOffset = mapOffset + 256 * 4;
	const int labelOffset = idOffset + int(idLen);
	const int minSize = labelOffset + int(labelLen);
	if(blob.size() < minSize) {
		if(errorText) *errorText = tr("Некорректный размер файла");
		return false;
	}

	// таблица 256×u32 LE
	for(int index = 0; index < 256; ++index) {
		quint32 le;
		std::memcpy(&le, blob.constData() + mapOffset + index * 4, 4);
		const quint32 u = qFromLittleEndian(le);
		if(!isValidUnicodeScalar(u) && u != 0) {
			if(errorText) *errorText = tr("Таблица содержит недопустимый кодпоинт");
			return false;
		}
		outTable.codepointMap[index] = u;
	}

	// id и label UTF-8 без терминатора
	outTable.id = QString::fromUtf8(QByteArray::fromRawData(
		blob.constData() + idOffset, int(idLen)));
	outTable.label = QString::fromUtf8(QByteArray::fromRawData(
		blob.constData() + labelOffset, int(labelLen)));

	return true;
}

void CodepageTool::onCellEdited(QTableWidgetItem* item) {
	if(m_isNormalizing || !item) return;

	const QString rawText = item->text(); // БЕЗ trimmed()
	if(rawText.isEmpty()) {
		item->setBackground(Qt::NoBrush);
		item->setToolTip(QString());
		return;
	}

	char32_t codepoint = 0;
	const bool ok = parseCellValue(rawText, codepoint);
	if(!ok || !isValidUnicodeScalar(codepoint)) {
		item->setBackground(QColor(255, 230, 230));
		item->setToolTip(tr("Неверное значение. Допустимы: 1 символ, U+XXXX, 0xXXXX, десятичное (≤ U+10FFFF)."));
		return;
	}

	const QString normalized = formatCellValue(codepoint); // вернёт " " для U+0020
	if(normalized != rawText) {
		m_isNormalizing = true;
		item->setText(normalized);
		m_isNormalizing = false;
	}
	item->setBackground(Qt::NoBrush);
	item->setToolTip(QString());
}
