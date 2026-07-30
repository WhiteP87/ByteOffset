#pragma once
#include <QMainWindow>
#include "ui_MainWindow.h"
#include "HexEditor.h"
#include <QEvent>
#include "GotoDialog.h"
#include "SearchDialog.h"
#include "DataTextTranslator.h"
#include "TranslatorFactory.h"
#include "CodepageRegistry.h"
#include <QUndoGroup>
#include "FieldManagerTypes.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; };
QT_END_NAMESPACE

class BlockInfo;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget *parent = nullptr);
	~MainWindow();
public slots:
	void onHeDataChanged(HexEditor* sender, qint64 off, qint64 len);
	void onHeAnnotationChanged(HexEditor* sender);
	void onHeSelectionChanged(HexEditor* sender);
	void onHeGeometryChanged(HexEditor* sender);
private slots:
	void onTabCreate(QAbstractScrollArea* editorPage, const QString& title, const QString& tooltip);
	void onOpenFileAction();
	void onExitAction();
	void ontabClose(int index);
	void onTabChanged(int index);
	void onSaveAction();
	void onCloseDocumentAction();
	void onSaveAsAction();
	void onCpEditorAction();
	void onGotoAction();
	void onGotoRequestFromDialog(qint64 offset, bool setCaret);
	void onCreateBlockAction();
	void onCreateBookmarkAction();
	void onCreateFieldAction();
	void onInsModeAction();
	void onReplModeAction();
	void onHeMove(HexEditor*he, qint64 off);
	void onHeCaretChanged(HexEditor* he, qint64 byte, qint64 nibble);
	void onSaveProjectAction();
	void onLoadProjectAction();
	void onCopyAction();
	void onPasteAction();
	void onClipBoardChanged();
	void onEntropyMapToggled(bool on);
	void onShowFieldsToggled(bool on);
	void onUpdateBlockAction();
	void onGoUpBlockBtn();
	void onUpdateFieldBtn();
	void onDeleteBlockBtn();
	void onDeleteFieldBtn();
	void onSearchAction();
private:
	Ui::MainWindow *ui;
	std::unique_ptr<CodepageRegistry> m_cpRegistry;
	std::unique_ptr<TranslatorFactory> m_trFactory;

	QLabel* m_offsetStatusLabel = nullptr;
	QLabel* m_caretStatusLabel = nullptr;
	QLabel* m_selectionStatusLabel = nullptr;

	qint32 m_inspectorBlockId{0};
	FieldID m_inspectorFieldId{0};

	void rebuildMainMenu(HexEditor* he);
	void rebuildInspector(HexEditor* he);

	void initMainMenu();
	void setEditorInsertionMode(bool ins);
	void initConnections();
	void initStatusBar();
	void initInspector();
	HexEditor* getCurrentHexEditor(qint32 index = -1);

	QUndoGroup* m_undoGroup;
	void initUndoGroup();
	void setCopyMenuEnabled(bool isEnable);
	void updateUndoRedoUi();
	void setStatusBarWidgetsVisibility(bool isVisible);
	void stopEntropyForAllTabs();
	void setInspectorBlockInfo(const BlockInfo& bi);
	void setInspectorFieldInfo(const FieldInfo& bi);
protected:
	bool eventFilter(QObject* obj, QEvent* ev) override;
	void closeEvent(QCloseEvent* event) override;
};

