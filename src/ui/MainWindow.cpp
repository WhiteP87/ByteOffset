#include "MainWindow.h"
#include <QFileDialog>
#include <QFile>
#include <QTabBar>
#include <QMouseEvent>
#include "MMapDataProvider.h"
#include "MMapSrcReader.h"
#include <QMessageBox>
#include "TempMMapPageStore.h"
#include "PagedPieceOverlay.h"
#include "IDataProvider.h"
#include "CodepageTool.h"
#include "UnifiedProjectWriter.h"
#include <QClipboard>
#include <QStringBuilder>

MainWindow::MainWindow(QWidget* parent)
	: QMainWindow(parent)
	, ui(new Ui::MainWindow()) {

	ui->setupUi(this);
	m_cpRegistry = std::make_unique<CodepageRegistry>();
	m_trFactory = std::make_unique<TranslatorFactory>(*m_cpRegistry);

	ui->m_tabBar->tabBar()->installEventFilter(this);

	initConnections();
	initMainMenu();
	initInspector();
	initStatusBar();
	initUndoGroup();
}

MainWindow::~MainWindow() {

	delete ui;
}


void MainWindow::onTabCreate(QAbstractScrollArea* editorPage, const QString& title, const QString& tooltip) {
	int idx = ui->m_tabBar->addTab(editorPage, title);
	ui->m_tabBar->setCurrentIndex(idx);
	ui->m_tabBar->setTabToolTip(idx, tooltip);
	editorPage->viewport()->setFocus(Qt::OtherFocusReason);

	if(auto* he = qobject_cast<HexEditor*>(editorPage)) {
		connect(he, &HexEditor::dataChanged, this, &MainWindow::onHeDataChanged);
		connect(he, &HexEditor::geometryChanged, this, &MainWindow::onHeGeometryChanged);
		connect(he, &HexEditor::annotationsChanged, this, &MainWindow::onHeAnnotationChanged);
		connect(he, &HexEditor::mouseMoved, this, &MainWindow::onHeMove);
		connect(he, &HexEditor::caretChanged, this, &MainWindow::onHeCaretChanged);
		connect(he, &HexEditor::selectionChanged, this, &MainWindow::onHeSelectionChanged);
		rebuildMainMenu(he);
		rebuildInspector(he);

		m_undoGroup->addStack(he->undoStack());
		m_undoGroup->setActiveStack(he->undoStack());
		updateUndoRedoUi();
	}

	ui->saveAsAction->setEnabled(true);
	ui->gotoAction->setEnabled(true);
	ui->searchAction->setEnabled(true);
	ui->closeAction->setEnabled(true);
	ui->loadProjectAction->setEnabled(true);
	ui->createBlockAction->setEnabled(true);
	ui->createBookmarkAction->setEnabled(true);
	ui->createFieldAction->setEnabled(true);

	auto cb = QGuiApplication::clipboard();
	ui->pasteAction->setEnabled(cb->mimeData()->hasFormat(appMimeType));
	setStatusBarWidgetsVisibility(true);
}

void MainWindow::onOpenFileAction() {
	QString filePath = QFileDialog::getOpenFileName(this,
		"Открыть файл",
		QString(),
		"Все файлы (*)");

	if(!filePath.isEmpty()) {


		auto src = std::make_unique<MMapSrcReader>(filePath, /*размер окна равен гранулярности*/ 0);

		PagedPieceOverlay::Config ocfg;
		ocfg.m_logicalPageSize = 0;//системная гранулярность
		ocfg.m_fullRatio = 0.25;  //25% измененных байт на странице - промоут в full

		auto overlay = std::make_unique<PagedPieceOverlay>(*src, ocfg);
		auto provider = std::make_shared<MMapDataProvider>(std::move(src), std::move(overlay));

		EditorContext ctx{m_cpRegistry.get(),m_trFactory.get(), provider};

		//подключаем к HexEditor
		auto* he = new HexEditor(ctx);

		//конфигурация редактора и добавление вкладки
		he->layoutConfig().setBytesPerRow(16);
		he->layoutConfig().setBytesPerBlock(8);
		he->visualConfig().setFont(QFont("Consolas", 14));
		//he->changeDecoder("koi8-r");
		he->layoutConfig().setHexBlockSpacing(2);
		he->visualConfig().enableAltHexColor(true);
		he->visualConfig().setHexColorAlt(QColor{16,16,96});
		//he->visualConfig().setBordersColor(QColor{0,0,0});

		onTabCreate(he, QFileInfo(filePath).fileName(), filePath);
	}
}

void MainWindow::onExitAction() {
	qApp->quit();
}

//закрытие вкладки
void MainWindow::ontabClose(int index) {
	if(auto he = getCurrentHexEditor(index)) {
		if(m_undoGroup->activeStack() == he->undoStack()) {
			m_undoGroup->setActiveStack(nullptr);
			m_undoGroup->removeStack(he->undoStack());
		}
		he->entropyAnalyzer().stopAllCalculations();
		ui->m_tabBar->removeTab(index);
		he->deleteLater();
	}

	//если не осталось вкладок
	auto* tabs = ui->m_tabBar;
	if(tabs->count() <= 0 || tabs->currentIndex() < 0) {
		initMainMenu();
		initInspector();
		updateUndoRedoUi();
		setStatusBarWidgetsVisibility(false);
	}
}

//выбор вкладки
void MainWindow::onTabChanged(int index) {
	if(index < 0) return;
	if(auto* page = qobject_cast<QAbstractScrollArea*>(ui->m_tabBar->widget(index))) {
		page->viewport()->setFocus(Qt::OtherFocusReason);
		if(auto* he = qobject_cast<HexEditor*>(page)) {
			rebuildMainMenu(he);
			rebuildInspector(he);
			const auto [byte, nibble] = he->caretPosition();
			onHeCaretChanged(he, byte, nibble);
			onHeSelectionChanged(he);
			m_undoGroup->setActiveStack(he->undoStack());
		}
	} else if(auto* w = ui->m_tabBar->widget(index)) {
		w->setFocus(Qt::OtherFocusReason);
	}
}

void MainWindow::onSaveAction() {
	const int index = ui->m_tabBar->currentIndex();
	if(index < 0) return;

	auto* page = qobject_cast<HexEditor*>(ui->m_tabBar->widget(index));
	if(!page) return;

	QString dstPath;
	if(auto meta = page->dataProvider().sourceMeta())
		dstPath = meta->id;

	if(dstPath.isEmpty()) { onSaveAsAction(); return; }

	QString err;
	const auto st = page->dataProvider().saveToFile(dstPath, &err);
	switch(st) {
	case SaveStatus::Ok:
		page->setDirtyFlag(false);
		ui->saveAction->setEnabled(false);
		statusBar()->showMessage(QStringLiteral("Сохранено: %1").arg(QFileInfo(dstPath).fileName()), 3000);
		ui->m_tabBar->setTabText(index, QFileInfo(dstPath).fileName());
		ui->m_tabBar->setTabToolTip(index, QFileInfo(dstPath).absoluteFilePath());
		page->update();
		break;

	case SaveStatus::StructuralChangeNotAllowed: {
		const auto rc = QMessageBox::question(
			this, "Сохранение",
			"Изначальный размер файла был изменён.\nСохранить в другой файл?");
		if(rc == QMessageBox::Yes) onSaveAsAction();
		break;
	}

	case SaveStatus::IoError:
	case SaveStatus::NoSource:
	default:
		QMessageBox::critical(this, "Ошибка сохранения", err.isEmpty() ? "Неизвестная ошибка" : err);
		break;
	}
}

void MainWindow::onCloseDocumentAction() {
	ontabClose(ui->m_tabBar->currentIndex());
}

void MainWindow::onSaveAsAction() {

	if(auto page = getCurrentHexEditor()) {
		QString suggestedDir; QString suggestedName = "unnamed.bin";
		if(auto meta = page->dataProvider().sourceMeta(); meta && !meta->id.isEmpty()) {
			QFileInfo fi(meta->id);
			suggestedDir = fi.absolutePath();
			suggestedName = fi.fileName();
		}

		const QString dstPath = QFileDialog::getSaveFileName(
			this, "Сохранить как",
			QDir(suggestedDir).filePath(suggestedName),
			"Все файлы (*.*)");
		if(dstPath.isEmpty()) return;

		QString err;
		const auto st = page->dataProvider().saveToFile(dstPath, &err);
		if(st != SaveStatus::Ok) {
			QMessageBox::critical(this, "Ошибка сохранения", err.isEmpty() ? "Неизвестная ошибка" : err);
			return;
		}

		const auto index = ui->m_tabBar->currentIndex();
		ui->m_tabBar->setTabText(index, QFileInfo(dstPath).fileName());
		ui->m_tabBar->setTabToolTip(index, QFileInfo(dstPath).absoluteFilePath());
		statusBar()->showMessage(QStringLiteral("Сохранено: %1") % (QFileInfo(dstPath).fileName()), 3000);
		page->setDirtyFlag(false);
		ui->saveAction->setEnabled(false);
		page->update();
	}
}

void MainWindow::onCpEditorAction() {
	auto dlg = new CodepageTool(0);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	dlg->show();

}

void MainWindow::onGotoAction() {
	auto dlg = new GotoDialog(this);
	dlg->setAttribute(Qt::WA_DeleteOnClose);
	connect(dlg, &GotoDialog::gotoRequest,
		this, &MainWindow::onGotoRequestFromDialog);
	dlg->show();
}

void MainWindow::onGotoRequestFromDialog(qint64 offset, bool setCaret) {
	if(auto editor = getCurrentHexEditor()) {
		editor->gotoOffset(offset, setCaret);
	}
}

void MainWindow::onCreateBlockAction() {
	if(auto editor = getCurrentHexEditor()) {
		editor->onCreateBlockTriggered(editor->selection().startOffset(), editor->selection().endOffset());
	}
}

void MainWindow::onCreateBookmarkAction() {
	if(auto editor = getCurrentHexEditor()) {
		editor->onCreateBookmarkTriggered(editor->caretPosition().first);
	}
}

void MainWindow::onCreateFieldAction() {
	if(auto editor = getCurrentHexEditor()) {
		editor->onCreateFieldTriggered(editor->selection().startOffset(), editor->selection().endOffset());
	}
}

void MainWindow::onInsModeAction() {
	setEditorInsertionMode(true);
}

void MainWindow::onReplModeAction() {
	setEditorInsertionMode(false);
}

void MainWindow::onHeMove(HexEditor* he, qint64 off) {
	static QElapsedTimer moveUpdateTimer;
	const auto debounceMs = 16;
	if(!moveUpdateTimer.isValid() || moveUpdateTimer.elapsed() > debounceMs) {
		if(off >= 0) {
			if(he) {
				const auto blocks = he->blockManager().collectStartBlocksCovering(off);

				QString label = QStringLiteral("Внутри блоков:\n");
				label.reserve(label.size() + 32 * blocks.size()); //грубая оценка, чтобы сократить реаллокации

				for(auto it = blocks.rbegin(); it != blocks.rend(); ++it) {
					const auto& block = *it;
					label = label
						% QString((*it)->level, QLatin1Char(' '))
						% block->name
						% QLatin1Char('\n');
				}

				ui->inBlockLabel->setText(label);


				const auto fields = he->fieldManager().getFieldsFromTo(off, off);
				QString fldLbl = QStringLiteral("Внутри полей:\n");
				for(auto it = fields.begin(); it != fields.end(); ++it) {
					const auto& field = *it;
					fldLbl = fldLbl % field->name % QLatin1Char('\n');
				}
				ui->inFieldsLabel->setText(fldLbl);

			}
			QString hexPart = QString::number(off, 16).toUpper();
			QString decPart = QString::number(off, 10);
			m_offsetStatusLabel->setText(QStringLiteral("Смещение: 0x") % hexPart % " (" % decPart % ")");
		} else {
			m_offsetStatusLabel->setText(QStringLiteral("Смещение: Н/Д"));
		}
		moveUpdateTimer.start();
	}
}

void MainWindow::onHeCaretChanged(HexEditor* he, qint64 byte, qint64 nibble) {
	if(byte >= 0) {
		const QString hex = QString::number(byte, 16).toUpper();
		m_caretStatusLabel->setText(QStringLiteral("Позиция каретки: 0x") % hex % QLatin1Char(':') % QString::number(nibble));
	} else {
		m_caretStatusLabel->setText(QStringLiteral("Позиция каретки: Н/Д"));
	}

	const auto blocks = he->blockManager().collectStartBlocksCovering(byte);
	if(blocks.empty()) {
		ui->blockInfoFrame->setVisible(false);
		m_inspectorBlockId = 0;
	} else {
		setInspectorBlockInfo(*(blocks[0]));
	}

	const auto fields = he->fieldManager().getFieldsFromTo(byte, byte);
	if(fields.empty()) {
		ui->fieldInfoFrame->setVisible(false);
		m_inspectorFieldId = 0;
	} else {
		setInspectorFieldInfo(*(fields[0]));
	}
}

void MainWindow::onSaveProjectAction() {
	if(auto he = getCurrentHexEditor()) {
		const auto metaOpt = he->dataProvider().sourceMeta();
		if(!metaOpt) return;

		const QString baseDir = QFileInfo(metaOpt->id).absolutePath();
		const QString baseName = QFileInfo(metaOpt->id).completeBaseName();
		const QString dstPath = QFileDialog::getSaveFileName(
			this, "Сохранить проект",
			baseDir + QDir::separator() + baseName + ".hecx",
			"Файлы проекта (*.hecx)");
		if(dstPath.isEmpty()) return;

		QVector<BinaryConfig> sections;

		//секция патчей от IDataProvider
		{
			QByteArray payload; QString err;
			if(!he->dataProvider().exportBinaryConfig(payload, &err)) {
				QMessageBox::warning(this, "Ошибка", err); return;
			}
			const auto d = he->dataProvider().configDescriptor();
			sections.push_back(BinaryConfig{d.tag, d.version, std::move(payload)});
		}

		//BlockManager (разметка блоков)
		{
			QByteArray payload; QString err;
			const auto d = he->blockManager().configDescriptor();
			if(!he->blockManager().exportBinaryConfig(payload, &err)) { /* показать err */ return; }
			sections.push_back(BinaryConfig{d.tag, d.version, std::move(payload)});
		}

		//BookmarkManager (разметка блоков)
		{
			QByteArray payload; QString err;
			const auto d = he->bookmarkManager().configDescriptor();
			if(!he->bookmarkManager().exportBinaryConfig(payload, &err)) { /* показать err */ return; }
			sections.push_back(BinaryConfig{d.tag, d.version, std::move(payload)});
		}

		//FieldManager (разметка полей)
		{
			QByteArray payload; QString err;
			const auto d = he->fieldManager().configDescriptor();
			if(!he->fieldManager().exportBinaryConfig(payload, &err)) { /* показать err */ return; }
			sections.push_back(BinaryConfig{d.tag, d.version, std::move(payload)});
		}

		QString err;
		if(!UnifiedProjectWriter::writeFile(dstPath, sections, &err))
			QMessageBox::warning(this, "Ошибка", err);
	}
}

void MainWindow::onLoadProjectAction() {
	if(auto he = getCurrentHexEditor()) {
		const auto metaOpt = he->dataProvider().sourceMeta();
		if(!metaOpt) return;

		const QString baseDir = QFileInfo(metaOpt->id).absolutePath();
		const QString srcPath = QFileDialog::getOpenFileName(
			this, "Загрузить проект",
			baseDir, "Файлы проекта (*.hecx)");
		if(srcPath.isEmpty()) return;

		QString err;

		//сначала патчи/геометрия
		if(!he->dataProvider().importFromProjectFile(srcPath, &err)) {
			QMessageBox::warning(this, "Ошибка применения патчей", err);
			return;
		}

		//блоки
		if(!he->blockManager().importFromProjectFile(srcPath, &err)) {
			QMessageBox::warning(this, "Ошибка применения разметки блоков", err);
			return;
		}

		//закладки
		if(!he->bookmarkManager().importFromProjectFile(srcPath, &err)) {
			QMessageBox::warning(this, "Ошибка применения разметки блоков", err);
			return;
		}

		//поля
		if(!he->fieldManager().importFromProjectFile(srcPath, &err)) {
			QMessageBox::warning(this, "Ошибка применения разметки полей", err);
			return;
		}

		he->textDecoder().clearCache();
	}
}

void MainWindow::onCopyAction() {
	if(auto he = getCurrentHexEditor()) {
		he->copySelection(he->selection().startOffset(), he->selection().length());
	}
}

void MainWindow::onPasteAction() {
	if(auto he = getCurrentHexEditor()) {
		he->pasteFromClipboard();
	}
}

void MainWindow::onClipBoardChanged() {
	auto cb = QGuiApplication::clipboard();
	ui->pasteAction->setEnabled(cb->mimeData()->hasFormat(appMimeType) || cb->mimeData()->hasText());
}

void MainWindow::onEntropyMapToggled(bool on) {
	if(auto he = getCurrentHexEditor()) {
		he->entropyAnalyzer().setEnabled(on);
		if(!on)
			he->entropyAnalyzer().stopAllCalculations();
		else {
			he->entropyAnalyzer().initHeatmap();
			he->updateEntropyLine();
		}
	}
}

void MainWindow::rebuildMainMenu(HexEditor* he) {
	if(he) {
		ui->saveAction->setEnabled(he->isDataModified());
		ui->saveProjectAction->setEnabled(he->isDataModified());
		bool isInsertionMode = he->isInsertionModeEnabled();
		ui->insMode->setEnabled(!isInsertionMode);
		ui->insMode->setChecked(isInsertionMode);
		ui->replMode->setEnabled(isInsertionMode);
		ui->replMode->setChecked(!isInsertionMode);
		ui->entropyMapChk->setEnabled(true);
		ui->entropyMapChk->setChecked(he->entropyAnalyzer().isEnabled());
		ui->showFieldsChk->setEnabled(true);
		ui->showFieldsChk->setChecked(he->fieldManager().isVisible());
	}
}

void MainWindow::rebuildInspector(HexEditor* he) {
	if(he) {
		auto p = he->dataProvider().id();
		QFileInfo fi(he->dataProvider().id());
		ui->nameLabel->setText(QStringLiteral("Файл:\n") % fi.fileName());
		ui->pathLabel->setText(QStringLiteral("Путь:\n") % fi.path());

		QLocale loc(QLocale::Russian, QLocale::Russia);
		QString formattedSize = loc.toString(he->dataProvider().size());
		ui->sizeLabel->setText(QStringLiteral("Размер:\n") % formattedSize % QStringLiteral(" байт"));
	}
}

void MainWindow::initMainMenu() {
	ui->saveAction->setEnabled(false);
	ui->saveAsAction->setEnabled(false);
	ui->gotoAction->setEnabled(false);
	ui->searchAction->setEnabled(false);
	ui->closeAction->setEnabled(false);

	ui->insMode->setEnabled(false);
	ui->insMode->setChecked(false);
	ui->replMode->setEnabled(false);
	ui->replMode->setChecked(false);

	ui->loadProjectAction->setEnabled(false);
	ui->saveProjectAction->setEnabled(false);

	ui->createBlockAction->setEnabled(false);
	ui->createBookmarkAction->setEnabled(false);
	ui->createFieldAction->setEnabled(false);

	ui->pasteAction->setEnabled(false);
	setCopyMenuEnabled(false);

	ui->entropyMapChk->setChecked(true);
	ui->entropyMapChk->setEnabled(false);
}

void MainWindow::setEditorInsertionMode(bool ins) {
	if(auto he = getCurrentHexEditor()) {
		he->setInsertionMode(ins);
		rebuildMainMenu(he);
	}
}

void MainWindow::initConnections() {
	connect(qApp, &QCoreApplication::aboutToQuit,
		this, [this]() { stopEntropyForAllTabs(); });
	connect(ui->openFileAction, &QAction::triggered,
		this, &MainWindow::onOpenFileAction);
	connect(ui->exitAction, &QAction::triggered,
		this, &MainWindow::onExitAction);
	connect(ui->m_tabBar, &QTabWidget::tabCloseRequested,
		this, &MainWindow::ontabClose);
	connect(ui->gotoAction, &QAction::triggered,
		this, &MainWindow::onGotoAction);
	connect(ui->m_tabBar, &QTabWidget::currentChanged,
		this, &MainWindow::onTabChanged);
	connect(ui->saveAction, &QAction::triggered,
		this, &MainWindow::onSaveAction);
	connect(ui->saveAsAction, &QAction::triggered,
		this, &MainWindow::onSaveAsAction);
	connect(ui->closeAction, &QAction::triggered,
		this, &MainWindow::onCloseDocumentAction);
	connect(ui->cpEditorAction, &QAction::triggered,
		this, &MainWindow::onCpEditorAction);
	connect(ui->insMode, &QAction::triggered,
		this, &MainWindow::onInsModeAction);
	connect(ui->replMode, &QAction::triggered,
		this, &MainWindow::onReplModeAction);
	connect(ui->saveProjectAction, &QAction::triggered,
		this, &MainWindow::onSaveProjectAction);
	connect(ui->loadProjectAction, &QAction::triggered,
		this, &MainWindow::onLoadProjectAction);
	connect(ui->createBlockAction, &QAction::triggered,
		this, &MainWindow::onCreateBlockAction);
	connect(ui->createBookmarkAction, &QAction::triggered,
		this, &MainWindow::onCreateBookmarkAction);
	connect(ui->copyAction, &QAction::triggered,
		this, &MainWindow::onCopyAction);
	connect(ui->pasteAction, &QAction::triggered,
		this, &MainWindow::onPasteAction);
	connect(ui->createFieldAction, &QAction::triggered,
		this, &MainWindow::onCreateFieldAction);
	connect(ui->searchAction, &QAction::triggered,
		this, &MainWindow::onSearchAction);
	auto clipboard = QGuiApplication::clipboard();
	connect(clipboard, &QClipboard::dataChanged,
		this, &MainWindow::onClipBoardChanged);

	connect(ui->entropyMapChk, &QAction::toggled,
		this, &MainWindow::onEntropyMapToggled);

	connect(ui->showFieldsChk, &QAction::toggled,
		this, &MainWindow::onShowFieldsToggled);

	connect(ui->updateBlockDataBtn, &QPushButton::clicked,
		this, &MainWindow::onUpdateBlockAction);
	connect(ui->updateFIeldData, &QPushButton::clicked,
		this, &MainWindow::onUpdateFieldBtn);
	connect(ui->goUpBtn, &QPushButton::clicked,
		this, &MainWindow::onGoUpBlockBtn);
	connect(ui->deleteBlockBtn, &QPushButton::clicked,
		this, &MainWindow::onDeleteBlockBtn);
	connect(ui->deleteFieldBtn, &QPushButton::clicked,
		this, &MainWindow::onDeleteFieldBtn);

	connect(ui->copyAsCArrayAction, &QAction::triggered, this, [this]() {
		if(auto he = getCurrentHexEditor()) {
			he->copySelectionAs(CopyAsType::CArray);
		}
		});
}

void MainWindow::initStatusBar() {
	m_offsetStatusLabel = new QLabel(this);
	m_offsetStatusLabel->setFixedWidth(200);
	m_caretStatusLabel = new QLabel(this);
	m_caretStatusLabel->setFixedWidth(200);
	m_selectionStatusLabel = new QLabel(this);
	m_selectionStatusLabel->setFixedWidth(200);
	m_selectionStatusLabel->setText(QStringLiteral("Выделение пусто"));

	statusBar()->addWidget(m_offsetStatusLabel);
	statusBar()->addWidget(m_caretStatusLabel);
	statusBar()->addWidget(m_selectionStatusLabel);

	m_selectionStatusLabel->setVisible(false);
	m_caretStatusLabel->setVisible(false);
	m_offsetStatusLabel->setVisible(false);
}

void MainWindow::initInspector() {
	ui->nameLabel->setText(QStringLiteral("Файл:\nN/A"));
	ui->pathLabel->setText(QStringLiteral("Путь:\nN/A"));
	ui->sizeLabel->setText(QStringLiteral("Размер:\nN/A"));
	ui->inBlockLabel->setText(QStringLiteral("Внутри блоков:\nN/A"));

	ui->blockInfoFrame->setVisible(false);
	ui->fieldInfoFrame->setVisible(false);
}


HexEditor* MainWindow::getCurrentHexEditor(qint32 index /*= -1*/) {
	if(index == -1) {
		if((index = ui->m_tabBar->currentIndex()) < 0)
			return nullptr;
	}
	auto* he = qobject_cast<HexEditor*>(ui->m_tabBar->widget(index));
	if(!he)
		return nullptr;
	return he;
}

void MainWindow::initUndoGroup() {
	m_undoGroup = new QUndoGroup(this);

	//кнопки меню из Designer: undoAction/redoAction
	connect(ui->undoAction, &QAction::triggered, m_undoGroup, &QUndoGroup::undo);
	connect(ui->redoAction, &QAction::triggered, m_undoGroup, &QUndoGroup::redo);

	//включение/выключение
	connect(m_undoGroup, &QUndoGroup::canUndoChanged, ui->undoAction, &QAction::setEnabled);
	connect(m_undoGroup, &QUndoGroup::canRedoChanged, ui->redoAction, &QAction::setEnabled);

	connect(m_undoGroup, &QUndoGroup::undoTextChanged, this,
		[this](const QString& t) {
			ui->undoAction->setText(t.isEmpty()
				? QStringLiteral("Отменить")
				: QStringLiteral("Отменить ") + t);
		},
		Qt::QueuedConnection); //важно

	connect(m_undoGroup, &QUndoGroup::redoTextChanged, this,
		[this](const QString& t) {
			ui->redoAction->setText(t.isEmpty()
				? QStringLiteral("Повторить")
				: QStringLiteral("Повторить ") + t);
		},
		Qt::QueuedConnection); //важно

	//начальное состояние
	updateUndoRedoUi();
}

void MainWindow::setCopyMenuEnabled(bool isEnable) {
	ui->copyAction->setEnabled(isEnable);
	ui->copyAsCArrayAction->setEnabled(isEnable);
}

void MainWindow::updateUndoRedoUi() {
	ui->undoAction->setEnabled(false);
	ui->redoAction->setEnabled(false);
	ui->undoAction->setText(QStringLiteral("Отмена"));
	ui->redoAction->setText(QStringLiteral("Повторить"));
}

void MainWindow::setStatusBarWidgetsVisibility(bool isVisible) {
	if(m_caretStatusLabel)m_caretStatusLabel->setVisible(isVisible);
	if(m_offsetStatusLabel)m_offsetStatusLabel->setVisible(isVisible);
	if(m_selectionStatusLabel)m_selectionStatusLabel->setVisible(isVisible);
}

void MainWindow::stopEntropyForAllTabs() {
	QTabWidget* tabWidget = ui->m_tabBar;
	if(!tabWidget) {
		return;
	}

	const int totalTabs = tabWidget->count();
	for(int tabIndex = 0; tabIndex < totalTabs; ++tabIndex) {
		if(auto hexEditor = getCurrentHexEditor(tabIndex)) {
			//останавливаем фоновые задачи энтропии до разрушения виджетов/окон
			hexEditor->entropyAnalyzer().stopAllCalculations();
		}
	}
}

void MainWindow::setInspectorBlockInfo(const BlockInfo& bi) {
	ui->blockName->setText(bi.name);
	ui->blockStart->setText(QStringLiteral("Начало: 0x") % QString::number(bi.startOffset, 16).toUpper());
	ui->blockEnd->setText(QStringLiteral("Конец: 0x") % QString::number(bi.endOffset, 16).toUpper());
	ui->blockSize->setText(QStringLiteral("Длина: ") % QString::number(bi.length(), 10) % QStringLiteral(" байт"));
	ui->commentPlain->setPlainText(bi.comment);
	ui->blockInfoFrame->setVisible(true);
	m_inspectorBlockId = bi.blockId;

	ui->blockTypeCb->clear();
	for(const auto& entry : BLOCK_TYPE_TABLE) {
		ui->blockTypeCb->addItem(
			QString(entry.name),                 // текст, видимый пользователю
			static_cast<int>(entry.type)         // пользовательские данные
		);
	}

	const int index = ui->blockTypeCb->findData(static_cast<int>(bi.blockType));
	if(index >= 0)
		ui->blockTypeCb->setCurrentIndex(index);

}

void MainWindow::setInspectorFieldInfo(const FieldInfo& fi) {
	ui->fieldName->setText(fi.name);
	ui->fieldStart->setText(QStringLiteral("Начало: 0x") % QString::number(fi.startOffset, 16).toUpper());
	ui->fieldEnd->setText(QStringLiteral("Конец: 0x") % QString::number(fi.endOffset, 16).toUpper());
	ui->fieldLen->setText(QStringLiteral("Длина: ") % QString::number(fi.length(), 10) % QStringLiteral(" байт"));
	ui->fieldComment->setPlainText(fi.comment);
	ui->fieldInfoFrame->setVisible(true);
	m_inspectorFieldId = fi.fieldId;

	ui->fieldTypeCb->clear();
	for(const auto& entry : FIELD_TYPE_TABLE) {
		ui->fieldTypeCb->addItem(
			QString(entry.name),                 // текст, видимый пользователю
			static_cast<int>(entry.type)         // пользовательские данные
		);
	}

	const int index = ui->fieldTypeCb->findData(static_cast<int>(fi.type));
	if(index >= 0)
		ui->fieldTypeCb->setCurrentIndex(index);
}

void MainWindow::closeEvent(QCloseEvent* event) {
	//останавливаем все анализаторы заранее, чтобы не было живых потоков при деструкторе
	stopEntropyForAllTabs();
	QMainWindow::closeEvent(event);
}


bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
	// Фильтруем только на tabBar
	if(obj == ui->m_tabBar->tabBar() && ev->type() == QEvent::MouseButtonRelease) {
		auto* me = static_cast<QMouseEvent*>(ev);
		if(me->button() == Qt::MiddleButton) {
			int idx = ui->m_tabBar->tabBar()->tabAt(me->pos());
			if(idx != -1) {
				// сгенерировать сигнал закрытия
				emit ui->m_tabBar->tabCloseRequested(idx);
				return true;
			}
		}
	}
	return QWidget::eventFilter(obj, ev);
}

void MainWindow::onHeDataChanged(HexEditor* sender, qint64 off, qint64 len) {
	if(auto he = getCurrentHexEditor()) {
		if(he == sender) {
			ui->saveAction->setEnabled(sender->isDataModified());
			ui->saveProjectAction->setEnabled(sender->isDataModified());
		}
	}
}

void MainWindow::onHeAnnotationChanged(HexEditor* sender) {
	if(auto he = getCurrentHexEditor()) {
		if(he == sender) {
			ui->saveProjectAction->setEnabled(he->isAnnotationsModified());
		}
	}
}

void MainWindow::onHeSelectionChanged(HexEditor* sender) {
	if(auto he = getCurrentHexEditor()) {
		if(he == sender) {
			setCopyMenuEnabled(!he->selection().isEmpty());

			if(auto selLen = he->selection().length(); selLen == 0) {
				m_selectionStatusLabel->setText(QStringLiteral("Выделение пусто"));
			} else {
				m_selectionStatusLabel->setText(QStringLiteral("Длина выделения: ") % QString::number(selLen) % QStringLiteral(" байт"));
			}
		}
	}
}

void MainWindow::onHeGeometryChanged(HexEditor* sender) {
	if(auto he = getCurrentHexEditor()) {
		if(he == sender) {
			QLocale loc(QLocale::Russian, QLocale::Russia);
			QString formattedSize = loc.toString(he->dataProvider().size());
			ui->sizeLabel->setText(QStringLiteral("Размер:\n") % formattedSize % QStringLiteral(" байт"));
		}
	}
}

void MainWindow::onShowFieldsToggled(bool on) {
	if(auto he = getCurrentHexEditor()) {
		he->fieldManager().setVisible(on);
	}
}

void MainWindow::onUpdateBlockAction()  {
	if(auto he = getCurrentHexEditor()) {
		if(m_inspectorBlockId) {
			if(auto blockPtr = he->blockManager().getBlockById(m_inspectorBlockId)) {

				auto blockCpy = *blockPtr;

				const auto name = ui->blockName->text();
				const auto comment = ui->commentPlain->toPlainText();
				const auto type = static_cast<BlockType>(ui->blockTypeCb->currentData().toInt());

				blockCpy.name = name;
				blockCpy.comment = comment;
				blockCpy.blockType = type;				

				he->undoOpController()->editBlock(blockCpy);
				onHeCaretChanged(he, he->caretPosition().first, he->caretPosition().second);
			}
		}
	}
}

void MainWindow::onGoUpBlockBtn() {
	if(auto he = getCurrentHexEditor()) {
		if(m_inspectorBlockId) {
			if(auto blockPtr = he->blockManager().getBlockById(m_inspectorBlockId)) {
				if(const auto parent = blockPtr->parentId) {
					if(auto parentPtr = he->blockManager().getBlockById(parent)) {
						setInspectorBlockInfo(*parentPtr);
						he->gotoOffset(parentPtr->startOffset);
					}
				}
			}
		}
	}
}

void MainWindow::onUpdateFieldBtn() {
	if(auto he = getCurrentHexEditor()) {
		if(m_inspectorFieldId) {
			if(auto blockPtr = he->fieldManager().getFieldById(m_inspectorFieldId)) {

				auto fieldCpy = *blockPtr;

				const auto name = ui->fieldName->text();
				const auto comment = ui->fieldComment->toPlainText();
				const auto type = static_cast<FieldType>(ui->fieldTypeCb->currentData().toInt());

				fieldCpy.name = name;
				fieldCpy.comment = comment;
				fieldCpy.type = type;

				he->fieldManager().editField(fieldCpy);
				onHeCaretChanged(he, he->caretPosition().first, he->caretPosition().second);
			}
		}
	}
}

void MainWindow::onDeleteBlockBtn() {
	if(auto he = getCurrentHexEditor()) {
		if(m_inspectorBlockId) {
			he->undoOpController()->removeBlock(m_inspectorBlockId);
			he->viewport()->update();
			onHeCaretChanged(he, he->caretPosition().first, he->caretPosition().second);
		}
	}
}

void MainWindow::onDeleteFieldBtn() {
	if(auto he = getCurrentHexEditor()) {
		if(m_inspectorFieldId) {
			he->fieldManager().removeFieldById(m_inspectorFieldId);
			he->viewport()->update();
			onHeCaretChanged(he, he->caretPosition().first, he->caretPosition().second);
		}
	}
}

void MainWindow::onSearchAction() {
	auto dlg = new SearchDialog(this);
	if(auto he = getCurrentHexEditor()) {
		auto vc = he->visualConfig().font();
		dlg->setEditsFont(vc);
	}
	dlg->setAttribute(Qt::WA_DeleteOnClose);

	dlg->show();
}
