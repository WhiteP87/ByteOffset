//HexEditor.h
#pragma once
#include <QAbstractScrollArea>
#include <QPainter>
#include <QLabel>
#include <QScrollBar>
#include <QTimer>
#include <QShortcut>
#include <QKeySequence>
#include <QStaticText>
#include "DataTextTranslator.h"
#include "AddBlockDialog.h"
#include "AddBookmarkDialog.h"
#include "EditorUndoController.h"
#include "BookmarkManager.h"
#include "VisualConfig.h"
#include "LayoutConfig.h"
#include "BlockManager.h"
#include "HexEditorTypes.h"
#include "ScreenBuffer.h"
#include "FieldManager.h"
#include <memory>
#include "EntropyAnalyzer.h"
#include "DisasmEngine.h"


class IDataProvider;
enum class EditorArea;
class QWheelEvent;

class HexEditor final : public QAbstractScrollArea {
	Q_OBJECT
public:
	
	HexEditor(EditorContext ctx, QWidget* parent = nullptr);
	~HexEditor() { qDebug() << "~HexEditor"; }

	void setData(std::shared_ptr<IDataProvider> provider);
	void setOffsetShifting(qint64 shiftValue) { m_offsetShifting = shiftValue; }
	void setSelection(qint64 start, qint64 end);
	void setCaretByte(qint64 byteOffset) { setCaret(byteOffset * 2); }
	void setCaret(qint64 offset, qint32 nibbleIndex);
	void setDirtyFlag(bool val) { m_dataModified = val; }
	void setInsertionMode(bool enabled) { m_insertionMode = enabled; }
	void setInputArea(EditorArea area);

	const Selection& selection() const { return m_selection; }
	std::pair<qint64, qint32> caretPosition() const { return std::make_pair(m_caret.byteOffset(), m_caret.nibbleIdx()); }

	QUndoStack* undoStack() { return undoController ? undoController->undoStack() : nullptr; }
	VisualConfig& visualConfig() { return *visConf; }
	const VisualConfig& visualConfig() const { return *visConf; }
	LayoutConfig& layoutConfig() { return *layoutConf; }
	const LayoutConfig& layoutConfig() const { return *layoutConf; }
	IDataProvider& dataProvider() { return *m_ctx.dataProvider; }
	const IDataProvider& dataProvider() const { return *m_ctx.dataProvider; }
	BlockManager& blockManager() { return m_blockManager; }
	const BlockManager& blockManager() const { return m_blockManager; }
	BookmarkManager& bookmarkManager() { return m_bookmarkManager; }
	const BookmarkManager& bookmarkManager() const { return m_bookmarkManager; }
	EntropyAnalyzer& entropyAnalyzer() { return *entrAnalyzer; }
	const EntropyAnalyzer& entropyAnalyzer() const{ return *entrAnalyzer; }
	FieldManager& fieldManager() { return m_fieldManager; }
	const FieldManager& fieldManager() const { return m_fieldManager; }
	DataTextTranslator& textDecoder() { return *textEncDec; }
	const DataTextTranslator& textDecoder() const { return *textEncDec; }
	EditorUndoController* undoOpController() { return undoController; }

	void resizeToContentWidth(bool proportionalHeight = false, double propFactor = 0.75);
	void gotoOffset(qint64 offset, bool setCaret = false, bool toCenter = true);

	bool isDataModified() { return m_dataModified; }
	bool isAnnotationsModified() { return m_annotationsModified; }
	bool isInsertionModeEnabled() const { return m_insertionMode; }

	void changeDecoder(QString id);
	EditorArea getActiveInputArea() const { return m_activeInputArea; }
	void updateEntropyLine() {
		if(screenBuf && !screenBuf->empty() && entrAnalyzer) {
			const auto lim = screenBuf->getScreenOffsetsLimits();
			if(entrAnalyzer) entrAnalyzer->requestViewportCenter(lim->first, lim->second);
		}
	};
private:
	void drawHeader(QPainter* painter);
	void drawContent(QPainter* painter);
	void drawBlockHeader(QPainter* painter, qint32 blockId, int yPos);
	void drawBlockFooter(QPainter* painter, qint32 blockId, int yPos);
	void drawOffset(QPainter* painter, qint64 rowStart, int yPos);
	void drawHex(QPainter* painter, qint64 curByteOffset, const QPoint& pos);
	void drawText(QPainter* painter, qint64 curByteOffset, const QPoint& pos);
	void drawSelection(QPainter* painter, const QPoint& pos, qint64 curByteOffset);
	void drawHighlight(QPainter* painter, HighlightType type, HighlightArea area, const QPoint& pos, qint64 curByteOffset, qint64 startOffset, qint64 endOffset, const QColor& color);
	void drawHighlightBackground(QPainter* painter, const HighlightGeometry& hg, const QColor& color);
	void drawHighlightLine(QPainter* painter, HighlightType type, const HighlightGeometry& hg, const QColor& color);
	void drawHexCaret(QPainter* painter, const QPoint& pos, qint64 curByteOffset);
	void drawTextCaret(QPainter* painter, const QPoint& pos, qint64 curByteOffset);
	void drawBlockCollapseButton(QPainter* painter, qint32 blockId, qint32 yPos);
	void drawBlockGuideLines(QPainter* painter);
	void drawGuidesOnScreen(QPainter* painter);
	void drawGuidesOutside(QPainter* painter);
	void drawBookmarkBullet(QPainter* painter, qint32 yPos);
	void drawAreasBookmarks(QPainter* painter, qint64 curByteOffset, const QPoint& pos);
	void drawFields(QPainter* painter, qint64 curByteOffset, const QPoint& pos);
	void drawEntropyLine(QPainter* painter);
	void drawCrossLines(QPainter* painter, const QPoint& pos, qint64 curByteOffset);

	QShortcut* m_zoomInShortcut{nullptr};
	QShortcut* m_zoomOutShortcut{nullptr};


	std::unique_ptr<VisualConfig> visConf;
	std::unique_ptr<LayoutConfig> layoutConf;
	std::unique_ptr<ScreenBuffer> screenBuf;
	std::shared_ptr<DataTextTranslator> textEncDec;

	std::unique_ptr<EntropyAnalyzer> entrAnalyzer;
	void initEntropyAnalyzer();
	void initShortcuts();	

	EditorUndoController* undoController;//QObject владение
	BlockManager m_blockManager{};
	BookmarkManager m_bookmarkManager{};
	FieldManager m_fieldManager{};

	EditorContext m_ctx{};
	Selection m_selection{};
	Caret m_caret{};

	void moveSelectionToCaret(qint64 start);
	void moveCaret(qint64 nibbleDelta);
	void moveCaretByte(qint64 byteDelta) { moveCaret(byteDelta * 2); }
	void setCaret(qint64 nibbleOffset);

	void initHexCache(const QFont& font);

	std::array<QStaticText, 16> nibbleStaticCache;
	std::array<QStaticText, 256> byteStaticCache;

	DataChangeToken m_dataChangedCbId{0};
	DataChangeToken m_geomChanged{0};

	qint64 m_pageStartLine{0};
	qint32 m_currenPageIdx{0};
	qint64 m_offsetShifting{0};

	bool m_insertionMode{false};
	bool m_dataModified{false};
	bool m_annotationsModified{false};

	bool hasAnnotations() const {
		return m_blockManager.hasBlocks()
			|| m_fieldManager.hasFields()
			|| m_bookmarkManager.hasBookmarks();
	}


	double m_base{0.2};

	EditorArea m_activeInputArea{EditorArea::HEX_AREA};
	void setActiveInputAreaByPos(QPoint pos);

	void hexKeyInput(QKeyEvent* event);
	void textKeyInput(QKeyEvent* event);

	qint32 visibleRows{};

	qint64 getOffsetByPos(const QPoint pos);

	void toggleOffsetBase();
	bool runAndCheckAddBlockDlgData(AddBlockDialog* dlg);
	bool runAndCheckAddBookmarkDlgData(AddBookmarkDialog* dlg);
	void initUndoController();

	void copyAsCArray(qint64 start, qint64 len);
	void checkHeatMapClick(const QPoint pos);


protected:
	void keyPressEvent(QKeyEvent* event) override;
	void keyReleaseEvent(QKeyEvent* event) override;
	void paintEvent(QPaintEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;
	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void focusInEvent(QFocusEvent* e) override;
	void focusOutEvent(QFocusEvent* e) override;
	void wheelEvent(QWheelEvent* event) override;

	void scrollToLine(qint32 lineNum) {
		if(lineNum == m_pageStartLine) 
			return;

		const auto totalRows = screenBuf->getTotalRows();
		const auto screenSize = layoutConf->getViewportRowsCount(viewport()->height());

		lineNum = std::max(0, lineNum);
		m_pageStartLine = std::min(static_cast<qint64>(lineNum), std::max(totalRows, static_cast<qint64>(screenSize)) - screenSize);
		verticalScrollBar()->setValue(m_pageStartLine);
		screenBuf->updateView(m_pageStartLine, screenSize);

		updateEntropyLine();

		viewport()->update();
		const qint32 rows = layoutConf->getViewportRowsCount(viewport()->height()); 
		const auto totalPages = totalRows / screenSize;
		m_currenPageIdx = m_pageStartLine/screenSize;
	}
	void scrollContentsBy(int dx, int dy) override;
	void updateScrollBars();

	qint32 getBlockIdByPos(const QPoint pos) const;

	bool isBlockHeader(QPoint pos);
	bool isBlockFooter(QPoint pos);
	bool isTypeOfRow(rowType type, QPoint pos);
	bool isCollapseButton(QPoint pos);
	std::optional<rowType> getRowType(QPoint pos);
	std::optional<rowInfo> getRowInfo(QPoint pos);

	void showHexAreaCtxMenu(QPoint pos);
	void showTextAreaHeaderCtxMenu(QPoint pos);	
	QMenu* disasmSubMenu(QMenu* parent, qint64 offStart, qint64 len);

	qint32 m_hoveredCollapseBtn = 0;
	void hoverControls(QPoint pos);
	void LMBHitTest(QPoint pos);
	void RMBHitTest(QPoint pos);
public slots:
	void onCreateBlockTriggered(qint64 start = 0, qint64 end = 0);
	void onCreateBookmarkTriggered(qint64 start = 0);
	void onCreateFieldTriggered(qint64 start = 0, qint64 end = 0);
	void copySelection(qint64 start, qint64 len);
	void copySelectionAs(CopyAsType type);
	void pasteFromClipboard();
	void onPageUp();
	void onPageDown();
	void onScale(qint32 step);
private slots:
	void resetContext();
	void onEditBlockTriggered(qint32 blockId);
	void onDeleteBlockTriggered(qint32 blockId);
	void dispatchEncodingMenu(QAction* act);
	void onEditBookmarkTriggered(qint64 offset);
	void onDeleteBookmarkTriggered(qint64 offset);
	void onDisasmRequested(qint64 offset, qint64 len, DisasmEngine::Arch arch, bool isBE);
	void onEditFieldTriggered(FieldID fieldId);
	void onDeleteFieldTriggered(FieldID fieldId);
signals:
	void dataChanged(HexEditor* sender, qint64 offset, qint64 len);
	void geometryChanged(HexEditor* sender);
	void annotationsChanged(HexEditor* sender);
	void mouseMoved(HexEditor* sender, qint64 offset);
	void caretChanged(HexEditor *sender, qint64 caretByte, qint32 nibbleIdx);
	void selectionChanged(HexEditor* sender);
};

inline void HexEditor::setSelection(qint64 start, qint64 end) {
	if(start < 0 || end > m_ctx.dataProvider->size() - 1) {
		m_selection.reset();
		emit selectionChanged(this);
		viewport()->update();
		return;
	}
	m_selection.set(start, end);
	emit selectionChanged(this);
}

