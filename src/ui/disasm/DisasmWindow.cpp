#include "DisasmWindow.h"
#include <QHeaderView>
#include <QFontDatabase>
#include "DisasmModel.h"
#include <QAbstractSlider>
#include <QScrollBar>
#include <QStyleFactory>

DisasmWindow::DisasmWindow(QWidget* parent)
	: QWidget(parent)
	, ui(new Ui::DisasmWindow()) {
	ui->setupUi(this);

	setWindowFlag(Qt::Window, true);            //делаем топ-левел окно
	setAttribute(Qt::WA_DeleteOnClose, true);   //самоудаление при закрытии
	setWindowModality(Qt::NonModal);            //не блокирует главное окно

	auto* model = new DisasmModel(this);
	ui->disasmView->setModel(model);
	ui->disasmView->setSelectionBehavior(QAbstractItemView::SelectRows);
	ui->disasmView->setSelectionMode(QAbstractItemView::SingleSelection);
	ui->disasmView->verticalHeader()->setVisible(false);
	ui->disasmView->setAlternatingRowColors(true);
	ui->disasmView->setEditTriggers(QAbstractItemView::NoEditTriggers);
	ui->disasmView->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
	ui->disasmView->setWordWrap(false);
	ui->disasmView->setTextElideMode(Qt::ElideRight);
	ui->disasmView->horizontalHeader()->setSectionResizeMode(DisasmModel::Address, QHeaderView::ResizeToContents);
	ui->disasmView->horizontalHeader()->setSectionResizeMode(DisasmModel::Bytes, QHeaderView::Interactive);
	ui->disasmView->horizontalHeader()->setStretchLastSection(true);
	//уменьшаем высоту строки, фиксируем размер по метрике шрифта
	auto* verticalHeader = ui->disasmView->verticalHeader();
	verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
	const QFontMetrics viewMetrics(ui->disasmView->font());
	const int compactRowHeight = viewMetrics.height() + 2; //по 1px
	verticalHeader->setDefaultSectionSize(compactRowHeight);
	verticalHeader->setMinimumSectionSize(compactRowHeight);

	//переставляем столбцы: «Байты» вперед, «Адрес» на вторую позицию
	auto* header = ui->disasmView->horizontalHeader();
	header->setSectionsMovable(true);
	header->moveSection(DisasmModel::Bytes, 0);
	header->setSectionsMovable(false);
	ui->disasmView->setShowGrid(false);

	//делегат подсветки синтаксиса
	m_syntaxDelegate = new DisasmDelegate(ui->disasmView);
	ui->disasmView->setItemDelegate(m_syntaxDelegate);
	m_syntaxDelegate->setCellPadding(6, 6); //по 6 пикселей слева/справа
	//только горизонтальные линии по строкам
	m_syntaxDelegate->setRowSeparatorEnabled(true);
	m_syntaxDelegate->setRowSeparatorThickness(1);
	m_syntaxDelegate->setRowSeparatorColor(QColor{0,0,0,32});

	//вертикальные разделители
	m_syntaxDelegate->setVerticalSeparatorsEnabled(true);
	m_syntaxDelegate->setVerticalSeparatorThickness(0);
	m_syntaxDelegate->setVerticalSeparatorColor(QColor{0,0,0,64});
	m_syntaxDelegate->setVerticalSeparatorColumns(QVector<int>{
		DisasmModel::Bytes,
			DisasmModel::Address
	});


	connect(ui->refreshButton, &QPushButton::clicked, this, &DisasmWindow::onRefreshButton);
	connect(ui->disasmView, &QTableView::doubleClicked, this, &DisasmWindow::onTableDblClicked);

	auto* selectionModel = ui->disasmView->selectionModel();
	connect(selectionModel, &QItemSelectionModel::currentRowChanged, this, &DisasmWindow::onRowClicked);
}

DisasmWindow::~DisasmWindow() {
	delete ui;
}

void DisasmWindow::onChunkReady(std::vector<DisasmInstr> instructions, int /*percent*/) {
	auto* model = qobject_cast<DisasmModel*>(ui->disasmView->model());
	if(model == nullptr) {
		return;
	}
	if(!instructions.empty()) {
		model->appendRows(std::move(instructions));
	}

	//пытаемся восстановить выделение по сохранённому адресу, как только строка появится
	if(m_restorePending) {
		if(restoreSelectionIfReady()) {
			return; //восстановили — не автопрокручиваем вниз
		}
	}

	//автопрокрутка вниз
	const int lastRow = model->rowCount({});
	if(lastRow > 0) {
		ui->disasmView->scrollTo(model->index(lastRow - 1, 0));
	}
}


void DisasmWindow::onDisasmError(const QString& message) {
	//минимально сообщаем об ошибке в заголовке окна
	setWindowTitle(QStringLiteral("DisasmWindow — error: ") + message);
}

void DisasmWindow::onDisasmFinished() {
	//минимальная пометка о завершении
	setWindowTitle(QStringLiteral("DisasmWindow — done"));
	restoreSelectionIfReady();
}

void DisasmWindow::onRefreshButton() {
	auto* model = qobject_cast<DisasmModel*>(ui->disasmView->model());
	if(model != nullptr) {

		//сохраняем адрес текущей строки (если есть)
		const QModelIndex current = ui->disasmView->currentIndex();
		if(current.isValid()) {
			const QModelIndex addrIdx = current.sibling(current.row(), DisasmModel::Address);
			const QVariant va = model->data(addrIdx, DisasmModel::AddressRole);
			if(va.isValid()) {
				m_restoreAddress = va.toLongLong();
				m_restorePending = true;
			} else {
				m_restorePending = false;
			}
		} else {
			m_restorePending = false;
		}
		//очищаем таблицу перед новым прогоном
		model->setRows({});
	}
	emit refreshRequested();
}

void DisasmWindow::onTableDblClicked(const QModelIndex& index) {
	if(!index.isValid()) {
		return;
	}

	qint32 len{0};

	//берём адрес из модели
	const QModelIndex addrIndex = index.sibling(index.row(), DisasmModel::Address);
	const QVariant vAddr = ui->disasmView->model()->data(addrIndex, DisasmModel::AddressRole);
	if(!vAddr.isValid()) {
		return;
	}
	const qint64 address = vAddr.toLongLong();

	if(ui->selectInEditorCheck->isChecked()) {
		const QVariant vLen = ui->disasmView->model()->data(addrIndex, DisasmModel::SizeRole);
		if(vLen.isValid()) {
			len = vLen.toInt();
		}
	}
	emit gotoAddressRequested(address, len);
}

void DisasmWindow::onRowClicked(const QModelIndex& current, const QModelIndex& prev) {
	if(ui->autoFollowCheck->isChecked()) {
		onTableDblClicked(current);
	}
}

void DisasmWindow::onDataChanged(QObject* sender, qint64 off, qint64 len) {
	if(off >= m_startOffset && (off + len) <= (m_startOffset + m_len)) {
		onRefreshButton();
	}
}

bool DisasmWindow::restoreSelectionIfReady() {
	auto* model = qobject_cast<DisasmModel*>(ui->disasmView->model());
	if(model == nullptr) {
		m_restorePending = false;
		return false;
	}

	const int row = model->findRowContaining(m_restoreAddress);
	if(row < 0 || row >= model->rowCount({})) {
		//нужной строки ещё нет в модели — ждём следующие порции
		return false;
	}

	m_restorePending = false;

	const QModelIndex idx = model->index(row, DisasmModel::Address);
	if(!idx.isValid()) {
		return false;
	}

	if(auto* sel = ui->disasmView->selectionModel()) {
		QSignalBlocker blk(sel); //глушим currentRowChanged
		sel->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
	}

	ui->disasmView->scrollTo(idx, QAbstractItemView::PositionAtCenter);
	return true;
}

void DisasmWindow::onCaretChanged(qint64 offset) {
	if(!ui->caretSyncChk->isChecked()) {
		return;
	}


	//проверяем, входит ли каретка в текущий диапазон дизасма окна
	if(offset < m_startOffset || offset >= (m_startOffset + m_len)) {
		return;
	}

	auto* model = qobject_cast<DisasmModel*>(ui->disasmView->model());
	if(model == nullptr) {
		return;
	}

	//ищем строку, которая содержит этот байт
	const int row = model->findRowContaining(static_cast<quint64>(offset));
	if(row >= 0 && row < model->rowCount({})) {
		const QModelIndex idx = model->index(row, DisasmModel::Address);
		if(!idx.isValid()) {
			return;
		}
		if(auto* sel = ui->disasmView->selectionModel()) {
			QSignalBlocker blk(sel);
			sel->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
		}
		ui->disasmView->scrollTo(idx, QAbstractItemView::EnsureVisible);
		ui->disasmView->viewport()->update();
		return;
	}

	//порции не пришли - восстановим, когда появится
	m_restoreAddress = static_cast<quint64>(offset);
	m_restorePending = true;
}

void DisasmWindow::applyTableStyle(const QFont& editorFont, const QColor& backgroundColor,
	const QColor& textColor, const QColor& selectionBg, const QColor& selectionText,
	const QColor& addressTextColor) {

	ui->disasmView->setStyle(QStyleFactory::create("Fusion"));
	ui->disasmView->setFont(editorFont);

	QPalette tablePalette = ui->disasmView->palette();
	tablePalette.setColor(QPalette::Base, backgroundColor);
	tablePalette.setColor(QPalette::AlternateBase, backgroundColor);
	tablePalette.setColor(QPalette::Window, backgroundColor);
	tablePalette.setColor(QPalette::Text, textColor);
	tablePalette.setColor(QPalette::WindowText, textColor);

	for(QPalette::ColorGroup group : { QPalette::Active, QPalette::Inactive }) {
		tablePalette.setColor(group, QPalette::Highlight, selectionBg);
		tablePalette.setColor(group, QPalette::HighlightedText, selectionText);
	}

	ui->disasmView->setPalette(tablePalette);
	ui->disasmView->viewport()->setPalette(tablePalette);

	ui->disasmView->setAlternatingRowColors(false);

	if(auto* header = ui->disasmView->horizontalHeader()) {
		header->setFont(editorFont);
	}

	if(m_syntaxDelegate != nullptr) {
		const QColor effectiveAddress = addressTextColor.isValid() ? addressTextColor : textColor;
		m_syntaxDelegate->setAddressTextColor(effectiveAddress);
		ui->disasmView->viewport()->update();
	}

	//поджимаем строки под шрифт
// 	{
// 		auto* verticalHeader = ui->disasmView->verticalHeader();
// 		verticalHeader->setSectionResizeMode(QHeaderView::Fixed);
// 		const QFontMetrics editorMetrics(editorFont);
// 		const int compactRowHeight = editorMetrics.height();
// 		verticalHeader->setDefaultSectionSize(compactRowHeight);
// 		verticalHeader->setMinimumSectionSize(compactRowHeight);
// 	}

}

