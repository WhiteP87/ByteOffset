//HexEditor.cpp
#include "HexEditor.h"
#include <QFontDialog>
#include <QRegularExpression>
#include <QMouseEvent>
#include <QTextOption>
#include <QMenu>
#include <QMessageBox>
#include <QActionGroup>
#include <QToolTip>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include "EntropyAnalyzer.h"
#include "DisasmWindow.h"
#include <QWheelEvent>
#include <QApplication>
#include <QFontInfo>

HexEditor::HexEditor(EditorContext ctx, QWidget* parent):m_ctx(ctx), QAbstractScrollArea{parent} {

	this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

	visConf = std::make_unique<VisualConfig>(VisualConfig());

	setFocusPolicy(Qt::StrongFocus);
	viewport()->setFocusPolicy(Qt::NoFocus);

	viewport()->setFont(visConf->font());
	initHexCache(visConf->font());

	layoutConf = std::make_unique<LayoutConfig>(LayoutConfig(visConf->font(), 0));
	screenBuf = std::make_unique<ScreenBuffer>(ScreenBuffer(&m_blockManager));

	//подключаем колбэк для уведомления при изменении шрифта
	visConf->regFontChangedCallback([this](QFont font) {
		viewport()->setFont(font);
		layoutConf->updateLayout(font);
		initHexCache(font);
		update();
		});


	QPointer<HexEditor> self(this);
	//регистрация колбэка при добавлении/удалении блоков
	m_blockManager.registerChangedCallback(
		[self](const BlockChangeData&) {
			auto apply = [self]() {
				if(!self) return;

				const bool hasAny = self->hasAnnotations();
				self->layoutConf->setMaxLevel(self->m_blockManager.getMaxLevel());

				if(self->m_annotationsModified != hasAny) {
					self->m_annotationsModified = hasAny;
					Q_EMIT self->annotationsChanged(self.data());
				}

				self->screenBuf->updateDataMap(self->layoutConf->bytesPerRow(), self->m_ctx.dataProvider->size());
				self->updateScrollBars();
				//self->screenBuf->updateView(self->m_pageStartLine, self->layoutConf->getViewportRowsCount(self->viewport()->height()));
				self->viewport()->update();
				};

			//если уже в UI-потоке — выполнить немедленно
			if(QThread::currentThread() == self->thread()) {
				apply();
			} else {
				//иначе — синхронно перебросить в UI-поток и ДОЖДАТЬСЯ завершения
				QMetaObject::invokeMethod(self.data(),
					std::move(apply),
					Qt::BlockingQueuedConnection);
			}
		}
	);

	//регистрация колбэка при изменении полей
	m_fieldManager.registerChangedCallback(
		[self](const FieldChangeData&) {
			auto apply = [self]() {
				if(!self) return;

				const bool hasAny = self->hasAnnotations();
				if(self->m_annotationsModified != hasAny) {
					self->m_annotationsModified = hasAny;
					Q_EMIT self->annotationsChanged(self.data());
				}

				self->viewport()->update();
				};

			if(QThread::currentThread() == self->thread()) {
				apply();
			} else {
				QMetaObject::invokeMethod(self.data(),
					std::move(apply),
					Qt::BlockingQueuedConnection);
			}
		}
	);

	connect(&m_caret.flash, &CaretFlash::flashChanged,
		this, [this](bool) {
			if(screenBuf->isOnScreen(m_caret.byteOffset()))
				viewport()->update();
		});

	connect(&m_caret, &Caret::caretChanged,
		this, [this](qint64, qint32) {
			viewport()->update();
		});

	//пробрасываем сигнал от каретки
	connect(&m_caret, &Caret::caretChanged, this, [this](qint64 offset, qint32 nibIdx) {
		emit caretChanged(this, offset, nibIdx);
		});

	m_zoomInShortcut = new QShortcut(QKeySequence::ZoomIn, this);
	QObject::connect(m_zoomInShortcut, &QShortcut::activated, this, [this]() { onScale(1); });

	m_zoomOutShortcut = new QShortcut(QKeySequence::ZoomOut, this);
	QObject::connect(m_zoomOutShortcut, &QShortcut::activated, this, [this]() { onScale(-1); });


	setData(ctx.dataProvider);
	setMouseTracking(true);
	viewport()->update();
	initShortcuts();
	//m_caret.flash.turnOff(true);
}

//устанавливает ширину окна так, чтобы влезли все данные
//если proportionalHeight==true - высота окна устанавливается как (ширина х propFactor)
void HexEditor::resizeToContentWidth(bool proportionalHeight /*= false*/, double propFactor /*= 0.75*/) {
	const qint32 w = layoutConf->getContentWidth() + verticalScrollBar()->sizeHint().width();
	resize(w, proportionalHeight ? w * propFactor : -1);
}

//подскролл к смещению offset 
//если смещение находится на последнем экране - смещаем к началу последнего экрана, а не к
//строке, содержащей смещение
void HexEditor::gotoOffset(qint64 offset, bool changeCaret, bool toCenter) {
	if(offset < 0 || offset >= m_ctx.dataProvider->size()) return;

	if(changeCaret)
		setCaretByte(offset);

	m_blockManager.expandAllToOffset(offset);
	auto toLine = screenBuf->offsetToLine(offset);
	if(toCenter) {
		toLine = toLine - layoutConf->getViewportRowsCount(viewport()->height()) / 2 + 1;
	}
	scrollToLine(toLine);
}

void HexEditor::changeDecoder(QString id) {
	textEncDec = m_ctx.cpFactory->create(id, m_ctx.dataProvider);
	if(!textEncDec) {
		textEncDec = m_ctx.cpFactory->create(m_ctx.curCpId, m_ctx.dataProvider);
	}
	m_ctx.curCpId = id;
	layoutConf->setTextCaption(textEncDec->name());
}

void HexEditor::copySelection(qint64 start, qint64 len) {

	if(len <= 0 || !m_ctx.dataProvider) return;

	//читаем байты выделения
	QByteArray data; data.reserve(len);
	const qint64 readed = m_ctx.dataProvider->readRange(start, len, data);
	if(data.isEmpty())
		return;


	//текстовое представление
	QString hexText;
	hexText.reserve(data.size() * 3);
	for(int i = 0; i < data.size(); ++i) {
		if(i) hexText += QLatin1Char(' ');
		hexText += QString("%1").arg(static_cast<unsigned char>(data.at(i)), 2, 16, QLatin1Char('0')).toUpper();
	}

	auto* mime = new QMimeData();
	mime->setText(hexText);
	mime->setData(appMimeType, data);
	QGuiApplication::clipboard()->setMimeData(mime);
}

void HexEditor::copySelectionAs(CopyAsType type) {
	//диспетчер форматов «копировать как…»
	switch(type) {
	case CopyAsType::CArray:
		copyAsCArray(m_selection.startOffset(), m_selection.endOffset());
		break;
	default:
		//на будущее: другие представления
		break;
	}
}

void HexEditor::pasteFromClipboard() {

	//cчитать байты из буфера обмена (MIME или текст с hex)
	const QByteArray bytes = readClipboardBytes(QStringView{appMimeType});
	if(bytes.isEmpty())
		return;

	if(!m_selection.isEmpty()) {
		undoController->pasteReplaceSelection(m_selection.startOffset(), m_selection.length(), bytes);
		m_selection.reset();
		return;
	}
	if(m_insertionMode) {
		undoController->pasteInsert(m_caret.byteOffset() + m_caret.nibbleIdx(), bytes);
		return;
	}
	undoController->pasteOverwrite(m_caret.byteOffset(), bytes);
}

void HexEditor::onPageUp() {
	const auto screenSize = layoutConf->getViewportRowsCount(viewport()->height());
	scrollToLine(m_pageStartLine - static_cast<qint64>(screenSize));
}

void HexEditor::onPageDown() {
	const auto screenSize = layoutConf->getViewportRowsCount(viewport()->height());
	scrollToLine(m_pageStartLine + screenSize);
}

void HexEditor::onScale(qint32 step) {
	auto font = visConf->font();
	font.setPointSize(font.pointSize() + step);
	visConf->setFont(font);
	updateScrollBars();
	screenBuf->updateView(m_pageStartLine, layoutConf->getViewportRowsCount(viewport()->height()));
	viewport()->update();
}

void HexEditor::setCaret(qint64 offset, qint32 nibbleIndex) {
	if(offset > m_ctx.dataProvider->size() || nibbleIndex < 0) return;
	m_caret.set(offset, nibbleIndex % 2);
	if(!screenBuf->isOnScreen(offset)) gotoOffset(offset);
}

void HexEditor::setInputArea(EditorArea area) {
	if(area != EditorArea::HEX_AREA && area != EditorArea::TEXT_AREA)
		return;
	m_activeInputArea = area;
}

//установка выделения до каретки ввода
void HexEditor::moveSelectionToCaret(qint64 start) {
	if(m_selection.isActive()) {
		m_selection.set(start, m_caret.byteOffset());
		return;
	}
	m_selection.reset();
	return;
}

//установка активной зоны редактора по позиции в окне
void HexEditor::setActiveInputAreaByPos(QPoint pos) {
	if(!layoutConf->isHeaderArea(pos)) {
		if(layoutConf->isHexArea(pos)) {
			m_activeInputArea = EditorArea::HEX_AREA;

		} else if(layoutConf->isTextArea(pos)) {
			m_activeInputArea = EditorArea::TEXT_AREA;
		}
	}
}

//заполнение кэша символов байт
//заполняется staticText для шрифта
void HexEditor::initHexCache(const QFont& font) {

	static const QChar hexChars[16] = {
	'0','1','2','3','4','5','6','7',
	'8','9','A','B','C','D','E','F'
	};

	for(int i = 0; i < 16; i++) {
		QString s(1, hexChars[i]);
		nibbleStaticCache[i] = QStaticText(s);
		nibbleStaticCache[i].setTextFormat(Qt::PlainText);
		nibbleStaticCache[i].prepare(QTransform(), font);
	}

	for(int i = 0; i < 256; ++i) {
		QString s = QString("%1").arg(i, 2, 16, QChar('0')).toUpper();
		byteStaticCache[i] = QStaticText(s);
		byteStaticCache[i].setTextFormat(Qt::PlainText);
		byteStaticCache[i].prepare(QTransform(), font);
	}
}



//сдвиг каретки по полубайтам
void HexEditor::moveCaret(qint64 nibbleDelta) {
	if(!m_ctx.dataProvider || nibbleDelta == 0) return;

	const qint64 totalHalfBytes = m_ctx.dataProvider->size() * 2;
	const qint64 newHalfBytePos = m_caret.byteOffset() * 2 + m_caret.nibbleIdx() + nibbleDelta;
	if(newHalfBytePos < 0 || newHalfBytePos >= totalHalfBytes) return;

	const qint64 newBytePos = newHalfBytePos / 2;
	const qint32 newHalf = newHalfBytePos % 2;

	// Обновить выделение, если оно совпадало с кареткой
	if(m_selection.startOffset() == m_selection.endOffset() && m_selection.startOffset() == m_caret.byteOffset()) {
		m_selection.set(newBytePos, newBytePos);
	}

	m_caret.set(newBytePos, newHalf);
	m_blockManager.expandAllToOffset(newBytePos);

	// Формула вычисления сдвига скролла в строках
	const qint64 bytesPerRow = layoutConf->bytesPerRow();
	const qint64 caretRow = screenBuf->offsetToLine(newBytePos);

	qint64 delta = caretRow - (m_pageStartLine + visibleRows - 1);
	if(delta < 0) delta = caretRow < m_pageStartLine ? caretRow - m_pageStartLine : 0;

	if(delta != 0)
		verticalScrollBar()->setValue(verticalScrollBar()->value() + static_cast<int>(delta));

	m_caret.flash.setFlashed(true);
}

//установка каретки в позицию смещения ниббла
void HexEditor::setCaret(qint64 nibbleOffset) {

	if(!m_ctx.dataProvider || nibbleOffset < 0 || nibbleOffset / 2 >(m_ctx.dataProvider->size() - 1)) {
		return;
	}

	//устанавливаем каретку ввода на текущий байт и полубайт
	m_caret.set(nibbleOffset / 2, nibbleOffset % 2);

	m_blockManager.expandAllToOffset(m_caret.byteOffset());
}

//обработка нажатия клавиш при активной hex-зоне
void HexEditor::hexKeyInput(QKeyEvent* event) {
	const auto key = event->key();
	//ввод числа в текущий полубайт
	if((key >= Qt::Key_0 && key <= Qt::Key_9) || (key >= Qt::Key_A && key <= Qt::Key_F)) {
		if((m_caret.byteOffset() >= 0 && m_caret.byteOffset() < m_ctx.dataProvider->size())
			&& (m_caret.nibbleIdx() >= 0 && m_caret.nibbleIdx() < 2)) {

			//если смещение с кареткой не на экране - делаем подскролл
			//верхней строки к видимой части до начала редактирования
			if(!screenBuf->isOnScreen(m_caret.byteOffset())) {
				gotoOffset(m_caret.byteOffset());
				update();
				return;
			}

			qint32 value = key <= Qt::Key_9 ? key - Qt::Key_0 : 10 + key - Qt::Key_A;
			bool ret{};
			if(m_caret.nibbleIdx() == 0 && m_insertionMode) {
				ret = undoController->writeInserted(m_caret.byteOffset(), value, m_caret.nibbleIdx());
			} else {
				ret = undoController->writeByte(m_caret.byteOffset(), value, m_caret.nibbleIdx());
			}
			//moveCaret(1);
			update();
			return;
		}
	}

	switch(key) {
	case Qt::Key_Left: moveCaret(-1); break;
	case Qt::Key_Right: moveCaret(1); break;
	default: break;
	}
}

//обработка клавиш для столбца текста
void HexEditor::textKeyInput(QKeyEvent* event) {

	QString txt = event->text();
	if(!txt.isEmpty()) {
		QChar c = txt.at(0);
		if(c.isPrint()) {

			//если смещение с кареткой не на экране - делаем подскролл
			//верхней строки к видимой части до начала редактирования
			if(!screenBuf->isOnScreen(m_caret.byteOffset())) {
				gotoOffset(m_caret.byteOffset());
				update();
				return;
			}

			qint32 len{0};
			if(m_insertionMode) {
				//m_ctx.dataProvider->insert(m_caretByte, QByteArray(textEncDec->charLen(c), '\0'));
				len = undoController->encodeAndInsertAt(m_caret.byteOffset(), c);
			} else {
				len = undoController->encodeAndWriteAt(m_caret.byteOffset(), c);
			}

			//auto len = textEncDec->encodeAndWriteAt(m_caretByte, c);

			moveCaretByte(len);
			update();
			return;
		}
	}

	const auto key = event->key();
	switch(key) {
	case Qt::Key_Left: moveCaretByte(-1); break;
	case Qt::Key_Right: moveCaretByte(1); break;
	default: break;
	}
}

//возвращает смещение байта по позиции, -1 если в позиции нет отображаемого байта
qint64 HexEditor::getOffsetByPos(const QPoint pos) {
	const auto totalRows = screenBuf->size();
	const auto [row, col] = layoutConf->getRowColAt(pos, static_cast<qint32>(totalRows));
	if(row >= 0 && col >= 0 && row < totalRows && screenBuf->at(row).type == rowType::data) {
		qint64 base = screenBuf->at(row).rowOffset;
		qint32 startCol = screenBuf->at(row).startColumn;
		qint32 endCol = screenBuf->at(row).endColumn;
		if(col >= startCol && col <= endCol) {
			return base + col;
		}
	}
	return -1;
}

//переключает систему счисления смещений 10/16
void HexEditor::toggleOffsetBase() {
	auto curBase = layoutConf->offsetBase();
	if(curBase == 16)
		layoutConf->setOffsetBase(10);
	else
		layoutConf->setOffsetBase(16);
	viewport()->update();
}

void HexEditor::keyPressEvent(QKeyEvent* event) {

	static qint64 tmpSel = -1;
	bool quickMove = event->modifiers() & Qt::ControlModifier ||
		(m_selection.isActive() && m_selection.length() >= 1) || m_activeInputArea == EditorArea::TEXT_AREA;

	auto goToStartSelection = [&]()->bool {
		if(!m_selection.isActive() && !m_selection.isEmpty()
			&& m_caret.byteOffset() == m_selection.endOffset() && m_selection.length() != 1) {
			m_caret.set(m_selection.startOffset(), 0);
			if(!screenBuf->isOnScreen(m_selection.startOffset()))
				gotoOffset(m_selection.startOffset());
			return true;
		} else return false;
		};

	auto goToEndSelection = [&]()->bool {
		if(!m_selection.isActive() && !m_selection.isEmpty()
			&& m_caret.byteOffset() == m_selection.startOffset() && m_selection.length() != 1) {
			m_caret.set(m_selection.endOffset(), 0);
			if(!screenBuf->isOnScreen(m_selection.endOffset())) {
				auto endLine = screenBuf->offsetToLine(m_selection.endOffset()) - screenBuf->size() + 1;
				scrollToLine(static_cast<qint32>(endLine));
			}
			return true;
		} else return false;
		};

	auto goToLineLimits = [&](qint32 key) {
		if(!screenBuf->isOnScreen(m_caret.byteOffset())) {
			gotoOffset(m_caret.byteOffset());
		}
		auto rowIdx = screenBuf->onScreenRowCol(m_caret.byteOffset()).first;
		if(rowIdx >= 0) {
			const auto& rowData = screenBuf->at(rowIdx);
			m_caret.set(rowData.rowOffset + (key == Qt::Key_Home ? (rowData.startColumn) : (rowData.endColumn)), 0);
			m_caret.flash.setFlashed(true);
		}
		};

	switch(event->key()) {
	case Qt::Key_Shift: {
		tmpSel = m_caret.byteOffset();
		if(!m_selection.isEmpty()) {
			tmpSel = ((tmpSel == m_selection.startOffset()) ? m_selection.endOffset() : m_selection.startOffset());
		}
		m_selection.setActive(true);
		m_caret.flash.setEnabled(false);
	} break;
	case Qt::Key_Up: {
		if(goToStartSelection())
			return;
		moveCaretByte(-layoutConf->bytesPerRow());
		moveSelectionToCaret(tmpSel);
		emit selectionChanged(this);
		return;
	} break;
	case Qt::Key_Down: {
		if(goToEndSelection())
			return;
		moveCaretByte(layoutConf->bytesPerRow());
		moveSelectionToCaret(tmpSel);
		emit selectionChanged(this);
		return;
	} break;
	case Qt::Key_Left: {
		if(goToStartSelection())
			return;
		moveCaret(quickMove ? -2 : -1);
		moveSelectionToCaret(tmpSel);
		emit selectionChanged(this);
		return;
	}break;
	case Qt::Key_Right: {
		if(goToEndSelection()) return;
		moveCaret(quickMove ? 2 : 1);
		moveSelectionToCaret(tmpSel);
		emit selectionChanged(this);
		return;
	}break;
	case Qt::Key_Escape: m_selection.reset(); emit selectionChanged(this); viewport()->update(); return; break;
	case Qt::Key_PageUp: onPageUp(); break;
	case Qt::Key_PageDown: onPageDown(); break;
	case Qt::Key_Backspace: if(m_insertionMode)undoController->remove(m_caret.byteOffset() - 1, 1); break;
	case Qt::Key_Delete: {
		if(!m_selection.isEmpty()) {
			undoController->remove(m_selection.startOffset(), m_selection.length());
			m_selection.reset();
			return;
		}
		if(m_insertionMode)
			undoController->remove(m_caret.byteOffset(), 1);
	} break;
	case Qt::Key_Home: case Qt::Key_End: goToLineLimits(event->key()); break;
	}

	switch(m_activeInputArea) {
	case EditorArea::HEX_AREA: hexKeyInput(event);  break;
	case EditorArea::TEXT_AREA: textKeyInput(event);  break;
	default: break;
	}
}

void HexEditor::keyReleaseEvent(QKeyEvent* event) {
	switch(event->key()) {
	case Qt::Key_Shift:
		m_selection.setActive(false);
		m_caret.flash.setEnabled(true);
		emit selectionChanged(this);
		break;
	}
}

//обработчик события отрисовки
void HexEditor::paintEvent(QPaintEvent* event) {

	QPainter painter(viewport());
	painter.fillRect(viewport()->rect(), visConf->bgColor());
	painter.setRenderHint(QPainter::Antialiasing, true);

	drawHeader(&painter);
	drawContent(&painter);
	drawEntropyLine(&painter);
}

//отрисовка заголовков редактора
void HexEditor::drawHeader(QPainter* painter) {
	painter->save();

	{
		QBrush brush{visConf->bookmarkAreaBgColor()};
		painter->setBrush(brush);
		painter->drawRect(0, 0, layoutConf->bookmarksColumnWidth(), viewport()->height());
	}

	QPen pen(visConf->bordersColor(), layoutConf->bordersWidth());
	pen.setCosmetic(true);
	painter->setPen(pen);

	//линия перед текстовой зоной
	int xOff = layoutConf->textStartX() - layoutConf->xMargin();
	painter->drawLine(xOff, 0, xOff, height());

	//линия перед зоной hex-значений
	xOff = layoutConf->hexStartX() - layoutConf->xMargin();
	painter->drawLine(xOff, 0, xOff, height());

	//линия перед смещением
	xOff = layoutConf->offsetStartX() - layoutConf->xMargin();
	painter->drawLine(xOff, 0, xOff, height());

	//линия перед маркерами
	if(layoutConf->maxBlockLevel() >= 0) {
		xOff = layoutConf->markersStartX() - layoutConf->xMargin() / 2;
		painter->drawLine(xOff, 0, xOff, height());
	}

	//рисуем линию внизу хэдера
	painter->drawLine(layoutConf->bookmarksColumnWidth(), layoutConf->headerHeight(), viewport()->width(), layoutConf->headerHeight());

	//рисуем название первого столбца
	painter->setPen(visConf->captionsColor());
	painter->drawText(layoutConf->offsetCaptionXPos(), layoutConf->captionsBaseLine(), layoutConf->offsetCaption());

	//рисуем номера байтов смещения по горизонтали
	for(qint32 i = 0, x = layoutConf->hexStartX(); i < layoutConf->bytesPerRow(); x += layoutConf->xStep(), ++i) {
		painter->drawText(x, layoutConf->captionsBaseLine(), QString("%1").arg(i, 2, layoutConf->offsetBase(), QChar(' ')).toUpper());
		if(!((i + 1) % layoutConf->hexBlockSize())) x += layoutConf->hexBlockSpacing();
	}

	//название столбца с текстовым представлением
	painter->drawText(layoutConf->textCaptionXPos(), layoutConf->captionsBaseLine(), layoutConf->accomodatedTextCaption());
	painter->restore();
}

//отрисовка контента (смещений и представлений данных)
void HexEditor::drawContent(QPainter* painter) {
	if(!m_ctx.dataProvider) return;

	const int rowTextBaseline = layoutConf->yMargin() + layoutConf->charHeight() + layoutConf->headerHeight();
	drawBlockGuideLines(painter);

	for(int r = 0, y = rowTextBaseline; r < screenBuf->size(); ++r, y += layoutConf->yStep()) {

		const auto& row = screenBuf->at(r);
		const int bytesInRow = row.endColumn - row.startColumn;
		const qint64 rowStartOffset = row.rowOffset;

		drawOffset(painter, rowStartOffset, y);

		switch(row.type) {
		case rowType::blockHeader: {
			drawBlockHeader(painter, row.blockId, y);
			continue;
		} break;
		case rowType::blockFooter: {
			drawBlockFooter(painter, row.blockId, y);
			continue;
		} break;
		case rowType::data: {
			int hexX = layoutConf->getXPosOfCol(row.startColumn);
			int textX = layoutConf->textStartX() + row.startColumn * layoutConf->charWidth();

			if(m_bookmarkManager.isLineBookmarks(row.rowOffset + row.startOffset, row.rowOffset + row.endOffset)) {
				drawBookmarkBullet(painter, y);
			}

			for(int c = row.startColumn; c <= row.endColumn; ++c, hexX += layoutConf->xStep(), textX += layoutConf->charWidth()) {
				const qint64 curByteOffset = rowStartOffset + c;

				QPoint hexAreaPoint{hexX, y};
				QPoint textAreaPoint{textX, y};

				drawFields(painter, curByteOffset, hexAreaPoint);
				drawHex(painter, curByteOffset, hexAreaPoint);
				drawText(painter, curByteOffset, textAreaPoint);
				drawSelection(painter, hexAreaPoint, curByteOffset);
				drawHexCaret(painter, hexAreaPoint, curByteOffset);
				drawCrossLines(painter, hexAreaPoint, curByteOffset);
				drawTextCaret(painter, textAreaPoint, curByteOffset);
				drawAreasBookmarks(painter, curByteOffset, hexAreaPoint);

				if(!((c + 1) % layoutConf->hexBlockSize())) {
					hexX += layoutConf->hexBlockSpacing();
				}
			}
		} break;
		default:
			break;
		}
	}
}

//отрисовка хэдера блока
void HexEditor::drawBlockHeader(QPainter* painter, qint32 blockId, int yPos) {
	painter->save();

	drawBlockCollapseButton(painter, blockId, yPos);

	const auto& blockInfo = m_blockManager.getBlockById(blockId, MarkerType::BlockStart);
	if(!blockInfo)return;
	const auto charHeight = layoutConf->charHeight();

	QPen pen = painter->pen();
	pen.setWidth(charHeight * 0.15);
	pen.setColor(blockInfo->color);
	painter->setPen(pen);

	painter->setFont(visConf->altFont());

	qint64 len = blockInfo->length();
	QString caption = blockInfo->name + QString(" (%1b)").arg(len);

	const auto startOfText = layoutConf->blckCaptionX();
	painter->drawText(startOfText, yPos - 1, caption);

	const auto rowIdx = layoutConf->getRowIndexAt(QPoint{0,yPos}, static_cast<qint32>(screenBuf->size()));
	if(screenBuf->at(rowIdx).partial) {
		QPen pen = painter->pen();
		pen.setStyle(visConf->partialBlckLineStyle());
		painter->setPen(pen);
	}

	QFontMetrics fm{visConf->altFont()};
	const auto capWidth = fm.horizontalAdvance(caption);

	const auto linePos = yPos - charHeight / 2;
	const auto stLine2 = startOfText + capWidth + 3;
	const auto endOfLine = layoutConf->textStartX() + layoutConf->charWidth() * layoutConf->bytesPerRow();

	if(blockInfo->collapsed) {
		const qint32 lineYShift = charHeight / 5;
		painter->drawLine(stLine2, linePos - lineYShift, endOfLine, linePos - lineYShift);
		painter->drawLine(stLine2, linePos + lineYShift, endOfLine, linePos + lineYShift);
	} else {
		painter->drawLine(stLine2, linePos, endOfLine, linePos);
	}

	painter->restore();
}

//отрисовка футера блока
void HexEditor::drawBlockFooter(QPainter* painter, qint32 blockId, int yPos) {
	const auto& blockInfo = m_blockManager.getBlockById(blockId, MarkerType::BlockStart);
	if(!blockInfo) return;

	painter->save();
	QPen pen = painter->pen();
	pen.setWidth(layoutConf->charHeight() * 0.15);
	pen.setColor(blockInfo->color);
	painter->setPen(pen);

	const auto start = layoutConf->hexStartX();
	const auto linePos = yPos - layoutConf->charHeight() / 2;

	const auto rowIdx = layoutConf->getRowIndexAt(QPoint{0,yPos}, static_cast<qint32>(screenBuf->size()));
	if(screenBuf->at(rowIdx).partial) {
		QPen pen = painter->pen();
		pen.setStyle(visConf->partialBlckLineStyle());
		painter->setPen(pen);
	}

	painter->drawLine(start, linePos, start + layoutConf->xMargin(), linePos);

	painter->setFont(visConf->altFont());

	const QString caption = QStringLiteral(u"\u2191 END ") + blockInfo->name;

	const auto startOfText = start + layoutConf->xMargin() + 3;
	painter->drawText(startOfText, yPos - 1, caption);

	QFontMetrics fm{visConf->altFont()};
	const auto capWidth = fm.horizontalAdvance(caption);

	const auto stLine2 = startOfText + capWidth + 3;
	const auto endOfLine = layoutConf->textStartX() + layoutConf->charWidth() * layoutConf->bytesPerRow();
	painter->drawLine(stLine2, linePos, endOfLine, linePos);

	painter->restore();
}

//отрисовка offset строки
void HexEditor::drawOffset(QPainter* painter, qint64 rowStartOffset, int yPos) {
	painter->save();
	painter->setPen(visConf->offsetColor());
	painter->drawText(layoutConf->offsetStartX(), yPos, QString("%1").arg(rowStartOffset + m_offsetShifting, layoutConf->offsetDigits(),
		layoutConf->offsetBase(), QChar('0')).toUpper());
	painter->restore();
}

//отрисовка hex представления
void HexEditor::drawHex(QPainter* painter, qint64 curByteOffset, const QPoint& pos) {
	painter->save();
	const quint8 byte = static_cast<quint8>(m_ctx.dataProvider->readByte(curByteOffset));
	const auto xPos = pos.x();
	const auto yPos = pos.y();

	//только для staticText! особенности координации отрисовки...
	qint32 baseline = yPos - layoutConf->fontAscent();

	if(m_ctx.dataProvider->isModified(curByteOffset)) {
		painter->setPen(visConf->modifiedColor());
	} else {
		painter->setPen(visConf->hexColor(curByteOffset % 2));
	}

	painter->drawStaticText(xPos, baseline, byteStaticCache[byte]);
	painter->restore();
}

//отрисовка каретки ввода в области HEX
void HexEditor::drawHexCaret(QPainter* painter, const QPoint& pos, qint64 curByteOffset) {
	if(curByteOffset == m_caret.byteOffset()) {

		const auto byteValue = m_ctx.dataProvider->readByte(curByteOffset);
		const auto xPos = pos.x();
		const auto yPos = pos.y();
		qint32 baseline = yPos - layoutConf->fontAscent();

		QPoint topleft{xPos, yPos + layoutConf->lineSpacing() / 2};
		QPoint bottomRight{topleft.x() + layoutConf->charWidth() * 2, yPos - layoutConf->charHeight() - layoutConf->lineSpacing() / 2 - 1};

		painter->save();
		if(m_activeInputArea == EditorArea::HEX_AREA) {
			if(m_caret.flash.isFlashed()) {
				//рисуем выделение полубайта каретки
				topleft.setX(xPos + layoutConf->charWidth() * m_caret.nibbleIdx());
				bottomRight.setX(topleft.x() + layoutConf->charWidth());
				painter->fillRect(QRect(topleft, bottomRight), visConf->caretFillColor());

				//рисуем значение полубайта каретки цветом для каретки
				painter->setPen(visConf->caretFontColor());
				qint32 shift = 4 * (1 - m_caret.nibbleIdx());
				painter->drawStaticText(xPos + layoutConf->charWidth() * m_caret.nibbleIdx(), baseline, nibbleStaticCache[(byteValue >> shift) & 0xF]);
			}
		} else {
			const auto caretAltColor = visConf->caretAltFillColor();
			const auto caretAltSel = visConf->caretSelectedAltFillColor();

			QColor rectColor = m_selection.isSelected(curByteOffset) ? caretAltSel : caretAltColor;
			painter->fillRect(QRect(topleft, bottomRight), rectColor);
			painter->setPen(m_ctx.dataProvider->isModified(curByteOffset) ? visConf->modifiedColor() : visConf->caretAltFontColor());
			painter->drawStaticText(xPos, baseline, byteStaticCache[byteValue]);
		}
		painter->restore();
	}
}


//отрисовка каретки в текстовой зоне
void HexEditor::drawTextCaret(QPainter* painter, const QPoint& pos, qint64 curByteOffset) {
	if(curByteOffset == m_caret.byteOffset()) {

		const auto xPos = pos.x();
		const auto yPos = pos.y();
		const auto textVal = textEncDec->readAt(curByteOffset);

		const QPoint left{xPos, yPos + layoutConf->lineSpacing() / 2};
		const QPoint right{left.x() + layoutConf->charWidth(), yPos - layoutConf->charHeight() - layoutConf->lineSpacing() / 2 - 1};

		const auto caretAltColor = visConf->caretAltFillColor();
		const auto caretAltSel = visConf->caretSelectedAltFillColor();

		QColor rectColor{m_selection.isSelected(curByteOffset) ? caretAltSel : caretAltColor};
		QColor fontColor{m_ctx.dataProvider->isModified(curByteOffset) ? visConf->modifiedColor() : visConf->caretAltFontColor()};

		painter->save();
		if(m_activeInputArea == EditorArea::TEXT_AREA && m_caret.flash.isFlashed()) {
			rectColor = visConf->caretFillColor();
			fontColor = visConf->caretFontColor();
		}
		painter->fillRect(QRect(left, right), rectColor);
		painter->setPen(fontColor);
		painter->drawText(xPos, yPos, textVal);

		painter->restore();
	}
}

//отрисовка кнопки сворачивания/разворачивания блока
void HexEditor::drawBlockCollapseButton(QPainter* painter, qint32 blockId, qint32 yPos) {
	const auto blockInfo = m_blockManager.getBlockById(blockId);
	if(!blockInfo) return;
	const auto lineWidth = layoutConf->blckHeadlineWidth();

	painter->save();
	QPen pen = painter->pen();
	pen.setWidth(lineWidth);
	pen.setColor(blockId == m_hoveredCollapseBtn ? visConf->calcContrastColorTo(blockInfo->color) : blockInfo->color);
	painter->setPen(pen);

	const auto start = layoutConf->blckCollapseBtnX(blockInfo->level);
	const auto btnWidthHeight = layoutConf->blckCollapseBtnWidth();

	//отрисовываем скругленный прямоугольник кнопки
	QBrush brush(visConf->bgColor()); //цвет заливки внутри кнопки
	painter->setBrush(brush);
	QRect rect{start, yPos - btnWidthHeight, btnWidthHeight, btnWidthHeight};
	painter->drawRoundedRect(rect, 20, 20, Qt::RelativeSize);

	//рисуем хвостик справа кнопки
	QPen oldPen = painter->pen();
	QPen newPen = oldPen;
	QColor arrowColor = blockInfo->color;
	arrowColor.setAlpha(arrowColor.alpha() * visConf->blckGuideLineAlpha());
	newPen.setColor(arrowColor);
	painter->setPen(newPen);
	const auto x1 = start + layoutConf->blckCollapseBtnWidth();
	const auto x2 = layoutConf->offsetStartX() - layoutConf->xMargin() / 2;
	const auto y = yPos - layoutConf->blckCollapseBtnWidth() / 2;
	painter->drawLine(x1, y, x2, y);
	painter->setPen(oldPen);

	//рисуем минус в кнопке
	pen.setWidth(lineWidth * 0.8);
	painter->setPen(pen);
	const qint32 fontMargin = layoutConf->charHeight() * 0.2;
	const qint32 lineY = yPos - rect.height() / 2;
	painter->drawLine(start + lineWidth + fontMargin, lineY, start + btnWidthHeight - fontMargin - lineWidth, lineY);

	//если блок свернутый, то добавляем черточку для плюса в кнопке
	if(blockInfo->collapsed) {
		const qint32 lineX = start + rect.width() / 2 + lineWidth / 2;
		painter->drawLine(lineX, yPos - fontMargin - lineWidth, lineX, yPos - btnWidthHeight + lineWidth + fontMargin);
	}
	painter->restore();
}

//отрисовка гайдов для блоков
void HexEditor::drawBlockGuideLines(QPainter* painter) {
	drawGuidesOutside(painter);
	drawGuidesOnScreen(painter);
}

//рисует гайдлинии для блоков за пределами видимого экрана
void HexEditor::drawGuidesOutside(QPainter* painter) {
	if(auto lim = screenBuf->getScreenOffsetsLimits(); lim) {

		const auto [start, end] = *lim;

		qint64 scrStartLine = m_pageStartLine;
		qint64 scrEndLine = scrStartLine + screenBuf->size();

		//ищем ближайший сверху блок такой, чтобы хэдер и футер были за пределами экрана
		const auto blocks = m_blockManager.collectStartBlocksCovering(start);
		if(blocks.empty())
			return;

		std::vector<qint32> ids(static_cast<size_t>(m_blockManager.getMaxLevel()) + 1);
		const auto size = ids.size();
		auto ready = 0;

		for(const auto& block : blocks) {
			if(ready >= size) break;
			const auto [startLine, endLine] = screenBuf->getLineNumsOfMarkers(block->blockId);
			if(startLine < scrStartLine && endLine >= scrEndLine) {
				const auto level = block->level;
				if(ids[level] == 0) {
					ids[level] = block->blockId;
					ready++;
				}
			}
		}
		painter->save();
		for(const auto& id : ids) {
			if(!id) continue;

			const auto& block = m_blockManager.getBlockById(id);
			const auto& level = block->level;
			const auto xPos = layoutConf->guideLineXPos(level);

			auto color = block->color;
			color.setAlpha(color.alpha() * visConf->blckGuideLineAlpha());

			QPen pen(color, layoutConf->blckHeadlineWidth());
			pen.setCosmetic(true);
			painter->setPen(pen);
			painter->drawLine(xPos, layoutConf->headerHeight(), xPos, height());
		}
		painter->restore();
	}
}

//отрисовка маркера наличия закладок в области закладок
void HexEditor::drawBookmarkBullet(QPainter* painter, qint32 yPos) {
	painter->save();

	QColor strokeColor = visConf->bookmarkBulletColor();
	strokeColor.setAlpha(255);
	QPen pen(strokeColor, 1);
	pen.setCosmetic(true);
	painter->setPen(pen);
	QBrush brush(visConf->bookmarkBulletColor());
	painter->setBrush(brush);

	qint32 centerX = layoutConf->bookmarkBulletX();
	qint32 centerY = (yPos - layoutConf->yStep() / 2 - layoutConf->fontDescent() / 2);
	qint32 bulletDiam = layoutConf->bookmarkBulletWidth();
	painter->drawEllipse(centerX, centerY, bulletDiam, bulletDiam);

	painter->restore();
}

//отрисовка закладок в байтовом и текстовом представлении
void HexEditor::drawAreasBookmarks(QPainter* painter, qint64 curByteOffset, const QPoint& pos) {
	if(m_bookmarkManager.isLineBookmarks(curByteOffset, curByteOffset)) {
		//рисуем подчеркивающей линией
		drawHighlight(painter, HighlightType::Underline, HighlightArea::All, pos, curByteOffset, curByteOffset, curByteOffset, visConf->bookmarkBulletColor().lighter());
	}
}

void HexEditor::drawFields(QPainter* painter, qint64 curByteOffset, const QPoint& pos) {
	painter->save();
	/*	if(!m_fieldManager.isVisible()) return;*/
	if(auto x = m_fieldManager.isOffsetIntoField(curByteOffset)) {

		const auto yPos = pos.y();
		const auto xPos = pos.x();
		const auto shift = layoutConf->lineSpacing() / 2;
		const auto bw = layoutConf->byteWidth();
		const auto ch = layoutConf->charHeight();

		for(const auto& field : *x) {
			const auto start = field->startOffset;
			const auto endOffset = field->endOffset;

			if(m_fieldManager.isVisible())
				drawHighlight(painter, HighlightType::Background, HighlightArea::All, pos, curByteOffset, start, endOffset, field->color);

			//рисуем маркеры начала и конца поля в соотв.смещениях
			QColor c = field->color;
			c.setAlpha(255);
			if(curByteOffset == field->startOffset) {
				painter->setPen(c);
				painter->drawLine(xPos - 1, yPos + shift, xPos - 1, yPos - shift + 2);
				painter->drawLine(xPos - 1, yPos + shift, xPos + shift + 1, yPos + shift);
			}
			if(curByteOffset == field->endOffset) {
				painter->setPen(c);
				painter->drawLine(xPos + bw - shift - 1, yPos - ch - shift, xPos + bw + 1, yPos - ch - shift);
				painter->drawLine(xPos + bw + 1, yPos - ch - shift + 1, xPos + bw + 1, yPos - ch + 1);
			}
		}
	}
	painter->restore();
}

void HexEditor::drawEntropyLine(QPainter* painter) {

	if(!entrAnalyzer->isEnabled()) return;

	ViewportSample s;
	if(entrAnalyzer->tryGetViewportSample(s) && s.valid) {
		const QColor c = EntropyAnalyzer::colorForEntropy(s.sample.H, s.sample.confidence, entrAnalyzer->baseAlpha());
		const auto rect = layoutConf->getEntropyLineRect(viewport()->height());
		painter->fillRect(rect, c);
	}

	const qint64 binCount = entrAnalyzer->heatmapBinCount();
	if(binCount > 0) {
		const QRect barRect = layoutConf->getEntropyHeatmapRect(static_cast<qint32>(viewport()->height()));
		const int barX = barRect.x();
		const int barW = barRect.width();
		const int barTop = barRect.top();
		const int barH = barRect.height();
		const qreal baseAlpha = entrAnalyzer->baseAlpha();

		for(qint64 binIndex = 0; binIndex < binCount; ++binIndex) {
			const int y0 = barTop + int((binIndex * barH) / binCount);
			const int y1 = (binIndex == binCount - 1)
				? (barTop + barH)
				: (barTop + int(((binIndex + 1) * barH) / binCount));
			const int h = std::max(1, y1 - y0);

			const QColor color = entrAnalyzer->heatmapColorByIndex(binIndex, baseAlpha);
			painter->fillRect(QRect(barX, y0, barW, h), color);
		}
	}
}

void HexEditor::drawCrossLines(QPainter* painter, const QPoint& pos, qint64 curByteOffset) {

	if(curByteOffset == m_caret.byteOffset() &&
		screenBuf->isOnScreen(m_caret.byteOffset())) {

		int startHX = layoutConf->offsetStartX() - layoutConf->xMargin() / 2;
		int endHX = layoutConf->offsetStartX() + layoutConf->offsetColumnWidth() - layoutConf->xMargin()*1.5;
		int startHY = pos.y() + layoutConf->lineSpacing() / 2;
		int endHY = pos.y() - layoutConf->charHeight() - layoutConf->lineSpacing() / 2 - 1;

		QPoint topleftH{startHX, startHY};
		QPoint bottomRightH{endHX,endHY};

		painter->fillRect(QRect(topleftH, bottomRightH), visConf->caretCrosslineColor());

		int startVX = pos.x() - layoutConf->byteSpacing()/2 + 1;
		int endVX = pos.x() + layoutConf->byteWidth() + layoutConf->byteSpacing() / 2 - 1;
		int startVY = 0;
		int endVY = layoutConf->headerHeight();

		QPoint topleftV{startVX, startVY};
		QPoint bottomRightV{endVX,endVY};

		painter->fillRect(QRect(topleftV, bottomRightV), visConf->caretCrosslineColor());
	}
}

void HexEditor::initEntropyAnalyzer() {
	EntropyAnalyzer::Config entropyCfg;
	entropyCfg.level1Window = 4 * 1024;
	entropyCfg.level1MaxBatchBytes = 2 * 1024 * 1024;
	entropyCfg.workers = 3;
	entropyCfg.baseAlpha = 0.6;
	entropyCfg.heatmapRefineEnabled = true;
	entropyCfg.heatmapRefineSamples = 8;

	entrAnalyzer = std::make_unique<EntropyAnalyzer>(m_ctx.dataProvider, entropyCfg, this);

	//инициализируем теплокарту под текущую высоту вьюпорта (один раз здесь)
	const int visualBins = std::clamp((layoutConf->getContentHeight(viewport()->height()) - layoutConf->yMargin()), 256, 4096);
	entrAnalyzer->initHeatmap(visualBins);

	//тонкая полоса экранной энтропии внутри viewport
	connect(entrAnalyzer.get(), &EntropyAnalyzer::viewportEntropyReady,
		this, [this](qint64 first, qint64 last) {
			Q_UNUSED(first); Q_UNUSED(last);
			viewport()->update(layoutConf->getEntropyLineRect(viewport()->height()));
		});

	//перестроение heatmap
	connect(entrAnalyzer.get(), &EntropyAnalyzer::heatmapRangeUpdated,
		this, [this](qint64 first, qint64 last) {
			Q_UNUSED(first); Q_UNUSED(last);
			const auto rect = layoutConf->getEntropyHeatmapRect(viewport()->height());
			viewport()->update(rect);
		});
}

void HexEditor::initShortcuts() {

	//быстрый блок
	auto* quickBlock = new QShortcut(QKeySequence{"Ctrl+B"}, this);
	quickBlock->setContext(Qt::WidgetWithChildrenShortcut);
	quickBlock->setAutoRepeat(false);

	QObject::connect(quickBlock, &QShortcut::activated, this, [&]() {
		if(m_selection.length() > 1) {
			this->undoController->addBlock(BlockInfo{m_selection.startOffset(), m_selection.endOffset(),"","",visConf->getRandomColor()});
		}
		});

	//быстрое поле
	auto* quickField = new QShortcut(QKeySequence{"Ctrl+Alt+F"}, this);
	quickField->setContext(Qt::WidgetWithChildrenShortcut);
	quickField->setAutoRepeat(false);

	QObject::connect(quickField, &QShortcut::activated, this, [&]() {
		this->m_fieldManager.addField(FieldInfo{m_selection.startOffset(), m_selection.endOffset(),"","",visConf->getRandomColor()});
		});
}


//отрисовка гайд линий футеров/хэдеров на экране
void HexEditor::drawGuidesOnScreen(QPainter* painter) {
	//получаем индексы строк на экране, которые
//содержат маркеры блоков, второе значение 
//означает обработан ли блок
	std::vector<std::pair<qint32, bool>> blockIdx{};
	for(int i = 0; i < screenBuf->size(); i++) {
		const auto type = screenBuf->at(i).type;
		if(type == rowType::blockHeader || type == rowType::blockFooter) {
			blockIdx.push_back({i,false});
		}
	}

	//лямбда для вычисления позиции линии по y для строки 
	auto calcGuideYPos = [&](qint32 lineNum) -> qint32 {
		qint32 yPos{layoutConf->getYPosOfRow(lineNum, height())};
		if(screenBuf->at(lineNum).type == rowType::blockHeader && !screenBuf->at(lineNum).collapsed)
			return yPos;
		else
			return yPos - layoutConf->charHeight() / 2;
		};

	//отрисовка гайдов для маркеров блоков, видимых на экране
	//как минимум один из номеров линий должен быть валидным индексом строки на экране
	//второй может быть равен -1 - тогда считаем, что линия уходит вверх
	//или вниз за пределы экрана 
	auto drawLocalGuideLine = [&](qint32 lineFrom, qint32 lineTo) {
		qint32 row{lineFrom == -1 ? lineTo : lineFrom};
		const BlockInfo* blockInfo{m_blockManager.getBlockById(screenBuf->at(row).blockId)};

		if(!blockInfo) return;

		auto color = blockInfo->color;
		color.setAlpha(color.alpha() * visConf->blckGuideLineAlpha());

		painter->save();
		auto pen = painter->pen();
		pen.setWidth(layoutConf->blckHeadlineWidth());
		pen.setColor(color);
		painter->setPen(pen);

		const auto x = layoutConf->guideLineXPos(screenBuf->at(row).level);
		qint32 y1{}, y2{};
		const qint32 xArrowTo{layoutConf->offsetStartX() - layoutConf->xMargin() / 2};
		if(lineFrom == -1) {
			y1 = layoutConf->headerHeight();
			y2 = calcGuideYPos(lineTo);
		} else if(lineTo == -1) {
			y1 = calcGuideYPos(lineFrom);
			y2 = height();
		} else {
			y1 = calcGuideYPos(lineFrom);
			y2 = calcGuideYPos(lineTo);
		}
		painter->drawLine(x, y2, xArrowTo, y2);
		painter->drawLine(x, y1, x, y2);
		painter->restore();
		};

	//рисуем гайды для блоков на экране
	if(!blockIdx.empty()) {
		for(int i = 0; i < blockIdx.size(); ++i) {
			const auto& curRowInfo = screenBuf->at(blockIdx[i].first);
			if(curRowInfo.type == rowType::blockHeader) {
				if(!curRowInfo.collapsed) {
					int c = i;
					for(; c < blockIdx.size(); ++c) { //для каждого хэдера проходим вниз по экрану
						const auto& viewedRow = screenBuf->at(blockIdx[c].first);
						//ищем футер с тем же blockId
						if(viewedRow.type == rowType::blockFooter && viewedRow.blockId == curRowInfo.blockId) {
							drawLocalGuideLine(blockIdx[i].first, blockIdx[c].first);
							blockIdx[c].second = true; //помечаем футер обработанным
							break;
						}
						if(viewedRow.type == rowType::blockHeader && viewedRow.collapsed && viewedRow.level <= curRowInfo.level) {
							const auto curRowEnd = screenBuf->at(blockIdx[i].first).endOffset;
							if(viewedRow.endOffset > curRowEnd) {
								drawLocalGuideLine(blockIdx[i].first, blockIdx[c].first);
								blockIdx[i].second = true;
								break;
							}
						}
					}
					if(c == blockIdx.size()) { //если не нашли футер для хэдера	
						drawLocalGuideLine(blockIdx[i].first, -1); //рисуем от хэдера вниз за пределы экрана
					}
				} else { //для свернутых блоков

				}
			} else { //для футеров
				if(!blockIdx[i].second) {
					const auto [firstBlock, secondBlock] = m_blockManager.getBoundBlocks(screenBuf->at(blockIdx[i].first).endOffset, CollapseFilter::CollapsedOnly);
					drawLocalGuideLine(-1, blockIdx[i].first);
				}
			}
		}
	}
}

// отрисовка текстового представления
void HexEditor::drawText(QPainter* painter, qint64 curByteOffset, const QPoint& pos) {
	painter->save();

	const auto xPos = pos.x();
	const auto yPos = pos.y();

	if(m_ctx.dataProvider->isModified(curByteOffset))painter->setPen(visConf->modifiedColor());
	else painter->setPen(visConf->textColor());

	painter->drawText(xPos, yPos, textEncDec->readAt(curByteOffset));
	painter->restore();
}

//рисуем выделение байтов
void HexEditor::drawSelection(QPainter* painter, const QPoint& pos, qint64 curByteOffset) {
	if(m_selection.isSelected(curByteOffset)) {
		const qint64 startSelection{qMin(m_selection.startOffset(),m_selection.endOffset())};
		const qint64 endSelection{qMax(m_selection.startOffset(),m_selection.endOffset())};
		drawHighlight(painter, HighlightType::Background, HighlightArea::All, pos, curByteOffset, startSelection, endSelection, visConf->selectionColor());
	}
}

//диспетчер отрисовки подсветки байтов
//type задает конкретный тип подсветки
void HexEditor::drawHighlight(QPainter* painter, HighlightType type, HighlightArea area, const QPoint& pos, qint64 curByteOffset,
	qint64 startOffset, qint64 endOffset, const QColor& color) {

	const qint32 col = layoutConf->getColIndexAt(pos).first;
	const auto row = layoutConf->getRowIndexAt(pos, static_cast<qint32>(screenBuf->size()));
	const qint32 firstCol = screenBuf->at(row).startColumn;

	if(curByteOffset == startOffset || (col == firstCol)) {

		//получаем смещение в последнем столбце
		qint64 lastCol = screenBuf->at(row).endColumn;
		const qint64 lastColOffset = screenBuf->at(row).rowOffset + lastCol;

		//определяем - рисуем до конца строки или до конечного смещения подсветки
		lastCol = std::min(col + endOffset - curByteOffset, col + lastColOffset - curByteOffset);
		qint32 lastColX = layoutConf->getXPosOfCol(lastCol);

		HighlightGeometry hg{};
		//заполняем геометрию выделения для Hex
		hg.topLeftHex = {pos.x(), pos.y() - layoutConf->charHeight() - layoutConf->lineSpacing() / 2};
		hg.bottomRightHex = {lastColX + layoutConf->byteWidth(), pos.y() + layoutConf->lineSpacing() / 2 - 1};

		hg.topLeftText = {layoutConf->textStartX() + col * layoutConf->charWidth(), hg.topLeftHex.y()};
		hg.bottomRightText = {layoutConf->textStartX() + layoutConf->charWidth() * ((int)lastCol + 1),hg.bottomRightHex.y()};

		hg.area = area;

		switch(type) {
		case HighlightType::Background:
			drawHighlightBackground(painter, hg, color); break;
		case HighlightType::Underline:
		case HighlightType::Overline:
			drawHighlightLine(painter, type, hg, color); break;
		case HighlightType::Fullline:
			drawHighlightLine(painter, HighlightType::Underline, hg, color);
			drawHighlightLine(painter, HighlightType::Overline, hg, color); break;
		default:
			break;
		}
	}
}

//отрисовка подсветки выделением фона байта цветом
void HexEditor::drawHighlightBackground(QPainter* painter, const HighlightGeometry& hg, const QColor& color) {
	painter->save();

	if(hg.area == HighlightArea::Hex || hg.area == HighlightArea::All) {
		painter->fillRect(QRect(hg.topLeftHex, hg.bottomRightHex), color);
	}
	//выделяем байты в текстовом представлении
	if(hg.area == HighlightArea::Text || hg.area == HighlightArea::All) {
		painter->fillRect(QRect(hg.topLeftText, hg.bottomRightText), color);
	}

	painter->restore();
}

//отрисовка подсветки выделения подчеркивающей линией
void HexEditor::drawHighlightLine(QPainter* painter, HighlightType type, const HighlightGeometry& hg, const QColor& color) {
	painter->save();

	auto pen = painter->pen();
	pen.setColor(QColor(color.red(), color.green(), color.blue()));//исходный цвет без альфа-канала
	painter->setPen(pen);

	qint32 yPos{};
	if(type == HighlightType::Underline) {
		yPos = hg.bottomRightHex.y();
	} else {
		yPos = hg.bottomRightHex.y() - layoutConf->charHeight() - layoutConf->fontDescent() / 3;
	}

	//выделяем байты в hex-представлении
	QPoint left{hg.topLeftHex.x(), yPos};
	QPoint right{hg.bottomRightHex.x(), left.y()};

	if(hg.area == HighlightArea::Hex || hg.area == HighlightArea::All) {
		painter->drawLine(left, right);
	}

	//выделяем байты в текстовом представлении
	if(hg.area == HighlightArea::Text || hg.area == HighlightArea::All) {
		left.setX(hg.topLeftText.x());
		right.setX(hg.bottomRightText.x());
		painter->drawLine(left, right);
	}
	painter->restore();
}

void HexEditor::setData(std::shared_ptr<IDataProvider> provider) {

	if(!provider || provider->size() <= 0) {
		throw std::invalid_argument("setData: Incorrect data");
	}
	m_ctx.dataProvider = provider;
	resetContext();
	scrollToLine(0);
}

//обработчик сдвига скролла
void HexEditor::scrollContentsBy(int dx, int dy) {
	QAbstractScrollArea::scrollContentsBy(dx, dy);
	scrollToLine(verticalScrollBar()->value());
}

//обновление параметров скроллбара
void HexEditor::updateScrollBars() {
	if(!m_ctx.dataProvider || screenBuf->empty()) return;

	visibleRows = layoutConf->getViewportRowsCount(viewport()->height());  // Видимые строки
	const auto totalRows = screenBuf->getTotalRows();
	const int maxScroll = qMax(0, totalRows - visibleRows);  // Максимальный скролл в строках
	if(m_pageStartLine > totalRows) m_pageStartLine = totalRows - 1;

	verticalScrollBar()->setRange(0, maxScroll);
	verticalScrollBar()->setPageStep(visibleRows);
	verticalScrollBar()->setSingleStep(1);
	textEncDec->setCacheSize(static_cast<qint32>(layoutConf->bytesPerRow() * (screenBuf->size() + 2)));
}

qint32 HexEditor::getBlockIdByPos(const QPoint pos) const {
	const auto rowIdx = layoutConf->getRowIndexAt(pos, static_cast<qint32>(screenBuf->size()));
	if(rowIdx >= 0 && rowIdx < screenBuf->size()) {
		return screenBuf->at(rowIdx).blockId;
	}
	return 0;
}

//проверяет, находится ли в точке на экране хэдер блока.
//если передан валидный указатель blockId и в точке хэдер, то
//возвращает blockId этого блока
bool HexEditor::isBlockHeader(QPoint pos) {
	return isTypeOfRow(rowType::blockHeader, pos);
}

//проверяет, находится ли в точке на экране футер блока
//если передан валидный указатель blockId и в точке футер, то
//возвращает blockId блока
bool HexEditor::isBlockFooter(QPoint pos) {
	return isTypeOfRow(rowType::blockFooter, pos);
}

//проверяет, находится ли в позиции определенный тип строки
bool HexEditor::isTypeOfRow(rowType type, QPoint pos) {
	if(const auto row = getRowType(pos)) {
		return type == row;
	}
	return false;
}

std::optional<rowType> HexEditor::getRowType(QPoint pos) {
	if(const auto rowData = getRowInfo(pos)) {
		return rowData->type;
	}
	return std::nullopt;
}

std::optional<rowInfo> HexEditor::getRowInfo(QPoint pos) {
	auto row = layoutConf->getRowIndexAt(pos, static_cast<qint32>(screenBuf->size()));
	if(row < 0 || row >(screenBuf->size() - 1)) {
		return std::nullopt;
	}
	return screenBuf->at(row);
}

//проверяет, находится ли в точке кнопка сворачивания блока
bool HexEditor::isCollapseButton(QPoint pos) {

	if(isBlockHeader(pos)) {
		const auto rowIdx = layoutConf->getRowIndexAt(pos, static_cast<qint32>(screenBuf->size()));
		const auto level = screenBuf->at(rowIdx).level;
		const auto xPos = pos.x();
		const auto btnX = layoutConf->blckCollapseBtnX(level);
		const auto btnWidth = layoutConf->blckCollapseBtnWidth();
		if(xPos >= btnX && xPos <= (btnX + btnWidth)) {
			return true;
		}
	}
	return false;
}

//выбор и создание контекстного меню в позиции
void HexEditor::showHexAreaCtxMenu(QPoint pos) {

	const auto rowData = getRowInfo(pos);
	if(!rowData) return;

	QPoint globalPos = mapToGlobal(pos);

	QMenu menu(this);
	const auto posOffset = getOffsetByPos(pos);

	//Для строк данных
	if(posOffset >= 0 && rowType::data == rowData->type) {

		//Копировать/вставить
		QAction* copy = menu.addAction("Копировать");
		{
			copy->setEnabled(false);
			connect(copy, &QAction::triggered, this, [this]() {
				this->copySelection(m_selection.startOffset(), m_selection.length());
				});

			QAction* paste = menu.addAction("Вставить", this, [this]() {
				this->pasteFromClipboard();
				});
			paste->setEnabled(clipboardHasPasteData(appMimeType));

			QMenu* copyAs = new QMenu("Копировать как", &menu);
			copyAs->addAction("Массив C", this, [this]() {
				this->copyAsCArray(m_selection.startOffset(), m_selection.length());
				});
			menu.addMenu(copyAs);
			menu.addSeparator();
		}

		//Для выделенного
		if(m_selection.isSelected(posOffset)) {

			copy->setEnabled(true);
			menu.addMenu(disasmSubMenu(&menu, m_selection.startOffset(), m_selection.length()));

			if(m_selection.length() > 1) {
				menu.addAction("Блок из выделенного...", this, [this]() {
					this->onCreateBlockTriggered(m_selection.startOffset(), m_selection.endOffset());
					});
				menu.addSeparator();
			}
			menu.addAction("Поле из выделенного...", this, [this]() {
				this->onCreateFieldTriggered(m_selection.startOffset(), m_selection.endOffset());
				});
			menu.addSeparator();
			menu.addAction("Залить 0x00", this, [this]() {
				undoController->fillRange(m_selection.startOffset(), m_selection.length(), QByteArray{m_selection.length(),'\0'});
				});
			menu.addAction("Удалить выделенное", this, [this]() {
				undoController->remove(m_selection.startOffset(), m_selection.length());
				});
		}

		if(m_bookmarkManager.isLineBookmarks(posOffset, posOffset)) {
			menu.addAction("Редактировать закладку...", this, [this, posOffset]() {
				this->onEditBookmarkTriggered(posOffset);
				});
			menu.addAction("Удалить закладку", this, [this, posOffset]() {
				this->onDeleteBookmarkTriggered(posOffset);
				});
		} else {
			menu.addAction("Быстрая закладка", this, [this, posOffset]() {
				this->m_bookmarkManager.addBookmark(posOffset, "");
				});
			menu.addSeparator();
			menu.addAction("Добавить в закладки...", this, [this, posOffset]() {
				this->onCreateBookmarkTriggered(posOffset);
				});
		}
	}
	if(rowData->type == rowType::blockHeader || rowData->type == rowType::blockFooter) {
		const auto& blockData = m_blockManager.getBlockById(rowData->blockId);

		if(blockData) {
			if(auto disType = mapBlockTypeToDisasmType(blockData->blockType)) {
				menu.addAction("Дизассемблировать", this, [this, blockData, disType]() {
					onDisasmRequested(blockData->startOffset, blockData->length(), disType.value(), blockData->isBigEndian);
					});
			} else if(blockData->blockType == BlockType::raw || blockData->blockType == BlockType::unkCode) {
				menu.addMenu(disasmSubMenu(&menu, blockData->startOffset, blockData->length()));
			}
		}
		menu.addAction("Редактировать блок...", this, [this, rowData]() {
			this->onEditBlockTriggered(rowData->blockId);
			});
		menu.addAction("Удалить блок", this, [this, rowData]() {
			this->onDeleteBlockTriggered(rowData->blockId);
			});
	}

	if(auto fields = m_fieldManager.isOffsetIntoField(posOffset)) {
		if(fields->size() > 1) {
			QMenu* editFields = menu.addMenu("Редактировать поле");
			QMenu* deleteField = menu.addMenu("Удалить поле");
			for(const auto& x : fields.value()) {
				editFields->addAction(x->name, this, [this, x]() {
					this->onEditFieldTriggered(x->fieldId);
					});
				deleteField->addAction(x->name, this, [this, x]() {
					this->onDeleteFieldTriggered(x->fieldId);
					});
			}
			menu.addMenu(editFields);
		} else if(!fields->empty()) {
			const auto fieldId = fields.value().back()->fieldId;
			menu.addAction("Редактировать поле " + fields.value().at(0)->name, this, [this, fieldId]() {
				this->onEditFieldTriggered(fieldId);
				});
			menu.addAction("Удалить поле " + fields.value().at(0)->name, this, [this, fieldId]() {
				this->onDeleteFieldTriggered(fieldId);
				});
		}
	}
	menu.exec(globalPos);
	return;
}

QMenu* HexEditor::disasmSubMenu(QMenu* parent, qint64 offStart, qint64 len) {

	QMenu* menu = new QMenu("Дизассемблировать как", parent);
	QMenu* x86menu = new QMenu("x86", menu);
	QMenu* armMenu = new QMenu("ARM", menu);

	x86menu->addAction("Код x86-64", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::X86_64, false);
		});
	x86menu->addAction("Код x86-32", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::X86_32, false);
		});
	x86menu->addAction("Код x86-16", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::X86_32, false);
		});

	armMenu->addAction("Код ARM32 (LE)", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::ARM, false);
		});
	armMenu->addAction("Код ARM64 (LE)", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::ARM64, false);
		});
	armMenu->addAction("Код ARM64 (BE)", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::ARM64, true);
		});
	armMenu->addAction("Код ARM32 (BE)", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::ARM, true);
		});
	armMenu->addAction("Код ARMThumb (LE)", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::ARM_THUMB, false);
		});
	armMenu->addAction("Код ARMThumb (BE)", this, [this, offStart, len]() {
		onDisasmRequested(offStart, len, DisasmEngine::Arch::ARM_THUMB, true);
		});

	menu->addMenu(x86menu);
	menu->addMenu(armMenu);
	return menu;
}

//контекстное меню для заголовка текстового представления (выбор кодировки)
void HexEditor::showTextAreaHeaderCtxMenu(QPoint pos) {
	QMenu menu(this);

	QPoint globalPos = mapToGlobal(pos);

	QAction* cur = menu.addAction(textEncDec->name());
	cur->setCheckable(true);
	cur->setChecked(true);
	cur->setEnabled(false);
	menu.addSeparator();

	auto add = [&](QMenu* menu, const QString& title, const QString& id) -> QAction* {
		if(id != textEncDec->id()) {
			QAction* a = menu->addAction(title);
			a->setData(id);
			return a;
		}
		return nullptr;
		};

	add(&menu, "ANSI ASCII", "ascii");
	add(&menu, "UTF-8", "utf8");
	add(&menu, "UTF-16LE", "utf16le");
	add(&menu, "UTF-16BE", "utf16be");

	const auto items = m_ctx.cpRegistry->list();
	if(!items.isEmpty()) {
		QMenu* custom = menu.addMenu(QStringLiteral("Другие"));
		for(const auto& cp : items) {
			add(custom, cp.m_label, cp.m_id);
		}
	}

	connect(&menu, &QMenu::triggered, this, &HexEditor::dispatchEncodingMenu);

	menu.exec(globalPos);
	return;
}


void HexEditor::dispatchEncodingMenu(QAction* act) {

	if(!act)
		return;
	const QString id = act->data().toString();
	if(id.isEmpty() || id == m_ctx.curCpId)
		return;

	changeDecoder(id);
	update();
}

void HexEditor::onEditBookmarkTriggered(qint64 offset) {

	AddBookmarkDialog dlg(this);
	const auto bookmark = m_bookmarkManager.getBookmark(offset);

	if(bookmark.has_value()) {
		dlg.editExistingBookmark(offset, bookmark->comment);

		connect(&dlg, &AddBookmarkDialog::deleteBookmark, &dlg,
			[this, offset, &dlg] {
				onDeleteBookmarkTriggered(offset);
				dlg.reject();                    // закрыть диалог
			});

		if(runAndCheckAddBookmarkDlgData(&dlg)) {
			m_bookmarkManager.deleteBookmark(offset);
			m_bookmarkManager.addBookmark(dlg.startOffset(), dlg.comment());
		}
	}
}

void HexEditor::onDeleteBookmarkTriggered(qint64 offset) {
	m_bookmarkManager.deleteBookmark(offset);
}

void HexEditor::onDisasmRequested(qint64 offset, qint64 len, DisasmEngine::Arch arch, bool isBE) {
	{
		auto* disasmWindow = new DisasmWindow(this);
		disasmWindow->setRegion(offset, len);
		disasmWindow->applyTableStyle(visConf->altFont(), visConf->bgColor(),
			visConf->hexColor(), visConf->selectionColor(), visConf->hexColor(),
			visConf->offsetColor());
		disasmWindow->show();
		disasmWindow->raise();
		disasmWindow->activateWindow();

		const int chunkSizeBytes = 64 * 1024;

		auto* engine = new DisasmEngine(disasmWindow,
			m_ctx.dataProvider,
			offset,
			len,
			arch,
			isBE,
			chunkSizeBytes);

		connect(engine, &DisasmEngine::chunkReady,
			disasmWindow, &DisasmWindow::onChunkReady);
		connect(engine, &DisasmEngine::error,
			disasmWindow, &DisasmWindow::onDisasmError);
		connect(engine, &DisasmEngine::finished,
			disasmWindow, &DisasmWindow::onDisasmFinished);
		connect(disasmWindow, &DisasmWindow::refreshRequested,
			engine, &DisasmEngine::refresh);
		connect(disasmWindow, &DisasmWindow::gotoAddressRequested,
			this, [this](qint64 offset, qint32 len) {
				if(len)
					m_selection.set(offset, offset + len - 1);
				else
					m_selection.reset();
				gotoOffset(offset, false);
				m_caret.setByteOffset(offset);
			});
		connect(this, &HexEditor::dataChanged, disasmWindow, &DisasmWindow::onDataChanged);
		connect(this, &HexEditor::caretChanged,
			disasmWindow, [disasmWindow](HexEditor*, qint64 offset, qint64) { disasmWindow->onCaretChanged(offset); });

		engine->start();
	}

}

void HexEditor::onEditFieldTriggered(FieldID fieldId) {
	AddBlockDialog dlg(DialogType::Field, this);
	const auto& bi = m_fieldManager.getFieldById(fieldId);
	dlg.editExistingField(*bi);

	connect(&dlg, &AddBlockDialog::deleteField, &dlg,
		[this, fieldId, &dlg] {
			onDeleteFieldTriggered(fieldId); // передаём нужный id
			dlg.reject();                    // закрыть диалог
		});

	if(runAndCheckAddBlockDlgData(&dlg)) {
		FieldInfo bi{dlg.startOffset(), dlg.endOffset()};
		bi.name = std::move(dlg.name()); bi.comment = std::move(dlg.comment()); bi.type = dlg.fieldType();
		bi.color = dlg.color(); bi.isBigEndian = dlg.isBigEndian();
		bi.fieldId = fieldId;
		m_fieldManager.editField(std::move(bi));
	}
}

void HexEditor::onDeleteFieldTriggered(FieldID fieldId) {
	if(fieldId) {
		m_fieldManager.removeFieldById(fieldId);
	}
}

//проверка на ховер контролов
void HexEditor::hoverControls(QPoint pos) {
	qint32 blockId = getBlockIdByPos(pos);
	qint32 old = m_hoveredCollapseBtn;

	m_hoveredCollapseBtn = (isCollapseButton(pos)) ? blockId : 0;
	if(m_hoveredCollapseBtn != old)
		viewport()->update();

	const auto rect = layoutConf->getEntropyHeatmapRect(viewport()->height());
	if(rect.contains(pos)) {
		if(entrAnalyzer->baseAlpha() != 1) {
			layoutConf->setHeatmapHovered(true);
			entrAnalyzer->setBaseAlpha(1);
			viewport()->update();
		}
	} else {
		if(entrAnalyzer->baseAlpha() != 0.6) {
			layoutConf->setHeatmapHovered(false);
			entrAnalyzer->setBaseAlpha(0.6);
			viewport()->update();
		}
	}
}

//хиттест для контролов
void HexEditor::LMBHitTest(QPoint pos) {
	switch(EditorArea area = layoutConf->getArea(pos)) {
	case EditorArea::OFFSET_AREA: {
		toggleOffsetBase();
		return;
	}break;
	case EditorArea::OFFSET_HEADER_AREA:
		break;
	case EditorArea::HEX_AREA:
		break;
	case EditorArea::HEX_HEADER_AREA:
		break;
	case EditorArea::TEXT_AREA:
		break;
	case EditorArea::TEXT_HEADER_AREA: {
		showTextAreaHeaderCtxMenu(pos);
		return;
	}break;
	case EditorArea::BOOKMARKS_AREA:
		break;
	case EditorArea::BOOKMARKS_HEADER_AREA:
		break;
	case EditorArea::BLOCK_GUIDES_AREA: {
		qint32 blockId = getBlockIdByPos(pos);
		if(isCollapseButton(pos)) {
			m_blockManager.toggleCollapse(blockId);
			return;
		}
	}break;
	case EditorArea::BLOCK_GUIDES_HEADER_AREA:
		break;
	case EditorArea::NON_SPECIFIC: {
		checkHeatMapClick(pos);

	}break;
	default:
		break;
	}
}

void HexEditor::RMBHitTest(QPoint pos) {
	switch(EditorArea area = layoutConf->getArea(pos)) {
	case EditorArea::OFFSET_AREA:
		break;
	case EditorArea::OFFSET_HEADER_AREA:
		break;
	case EditorArea::HEX_AREA: {
		showHexAreaCtxMenu(pos);
		return;
	}break;
	case EditorArea::HEX_HEADER_AREA:
		break;
	case EditorArea::TEXT_AREA:
		showHexAreaCtxMenu(pos);
		break;
	case EditorArea::TEXT_HEADER_AREA:
		break;
	case EditorArea::NON_SPECIFIC:
		break;
	default:
		break;
	}
}

void HexEditor::onCreateBookmarkTriggered(qint64 start) {
	AddBookmarkDialog dlg(this);
	dlg.setOffset(start);

	if(runAndCheckAddBookmarkDlgData(&dlg)) {
		m_bookmarkManager.addBookmark(dlg.startOffset(), dlg.comment());
	}
}

void HexEditor::onCreateFieldTriggered(qint64 start, qint64 end) {
	AddBlockDialog dlg(DialogType::Field, this);
	dlg.setOffsets(start, end);

	if(runAndCheckAddBlockDlgData(&dlg)) {
		FieldInfo bi{dlg.startOffset(), dlg.endOffset()};
		bi.name = dlg.name(); bi.comment = dlg.comment(); bi.type = dlg.fieldType();
		bi.color = dlg.color(); bi.isBigEndian = dlg.isBigEndian();
		m_fieldManager.addField(std::move(bi));
	}
}

bool HexEditor::runAndCheckAddBlockDlgData(AddBlockDialog* dlg) {
	while(true) {
		if(dlg->exec() != QDialog::Accepted)
			return false;

		qint64 startOffset = dlg->startOffset();
		qint64 endOffset = dlg->endOffset();
		const auto size = m_ctx.dataProvider->size();

		if(startOffset >= size || endOffset >= size) {

			const auto ans = QMessageBox::question(this, "Ошибка ввода",
				"Блок выходит за пределы файла. Расширить файл?",
				QMessageBox::Yes | QMessageBox::No);

			if(ans == QMessageBox::Yes) {
				qint64 len = endOffset - size + 1;
				m_ctx.dataProvider->insert(size, QByteArray(len, '\0'));
			} else {
				dlg->setOffsets(0, size - 1);
				continue; // повторить ввод
			}
		}
		return true; // готово
	}
}

bool HexEditor::runAndCheckAddBookmarkDlgData(AddBookmarkDialog* dlg) {
	while(true) {
		if(dlg->exec() != QDialog::Accepted)
			return false;

		qint64 startOffset = dlg->startOffset();
		const auto size = m_ctx.dataProvider->size();

		if(startOffset >= size) {
			QMessageBox::warning(this, "Ошибка ввода",
				"Смещение закладки выходит за пределы файла. Повторите ввод.");
			dlg->setOffset(size - 1);
			continue; // повторить ввод
		}
		return true;
	}
}

void HexEditor::initUndoController() {
	undoController = new EditorUndoController(this, this);
}

void HexEditor::copyAsCArray(qint64 start, qint64 len) {

	if(len <= 0 || !m_ctx.dataProvider) return;

	//читаем байты выделения
	QByteArray data; data.reserve(len);
	const qint64 readed = m_ctx.dataProvider->readRange(start, len, data);
	if(data.isEmpty())
		return;

	const int bytesPerLine = 16;
	QString out;
	out.reserve(64 + data.size() * 6 + (data.size() / bytesPerLine + 2) * 2);

	out += QStringLiteral("//Длина %1 байт\n").arg(data.size());
	out += QStringLiteral("static const unsigned char data[%1] = {\n").arg(data.size());

	for(int i = 0; i < data.size(); ++i) {
		if(i % bytesPerLine == 0)
			out += QLatin1String("    ");

		out += QLatin1String("0x");
		out += QString("%1")
			.arg(static_cast<unsigned char>(data.at(i)), 2, 16, QLatin1Char('0'))
			.toUpper();

		if(i != data.size() - 1)
			out += QLatin1String(", ");

		if(((i + 1) % bytesPerLine) == 0 || i == data.size() - 1)
			out += QLatin1Char('\n');
	}

	out += QLatin1String("};\n");

	auto* mime = new QMimeData();
	mime->setText(out);
	mime->setData(appMimeType, data);
	QGuiApplication::clipboard()->setMimeData(mime);
}



void HexEditor::checkHeatMapClick(const QPoint pos) {
	//проверяем готовность виджета/провайдера
	if(!m_ctx.dataProvider) return;

	//прямоугольник «карты файла» в координатах viewport
	const QRect heatmapRect = layoutConf->getEntropyHeatmapRect(static_cast<qint32>(viewport()->height()));
	if(!heatmapRect.contains(pos)) return;

	//локальные координаты клика и высота полосы
	const int localY = pos.y() - heatmapRect.top();
	const int H = heatmapRect.height();
	if(H <= 0) return;

	//диапазон скролла в строках: totalRows - screenRows + 1 дискретных позиций
	const qint64 totalRows = screenBuf->getTotalRows();
	const qint32 screenRows = layoutConf->getViewportRowsCount(viewport()->height());
	if(screenRows <= 0) return;

	const qint64 maxStartLine = (totalRows > screenRows) ? (totalRows - screenRows) : 0;
	const qint64 totalSteps = maxStartLine + 1; //число дискретных положений скролла
	if(totalSteps <= 1) {
		scrollToLine(0);
		return;
	}

	//кламп по высоте и точное пропорциональное отображение (центр пикселя)
	const int yClamped = std::clamp(localY, 0, H - 1);
	const qint64 numer = (qint64(yClamped) * 2 + 1) * (totalSteps - 1);
	const qint64 denom = 2LL * H;
	const qint64 targetStartLine = (numer + denom / 2) / denom; //0..totalSteps-1

	//скроллим ровно на рассчитанную стартовую строку
	scrollToLine(static_cast<qint32>(targetStartLine));
}


//слот для создания блока
void HexEditor::onCreateBlockTriggered(qint64 start, qint64 end) {
	AddBlockDialog dlg(DialogType::Block, this);
	dlg.setOffsets(start, end);

	if(runAndCheckAddBlockDlgData(&dlg)) {
		BlockInfo bi{dlg.startOffset(), dlg.endOffset()};
		bi.name = dlg.name(); bi.comment = dlg.comment(); bi.blockType = dlg.blockType();
		bi.color = dlg.color(); bi.collapsed = dlg.collapsed(); bi.isBigEndian = dlg.isBigEndian();
		undoController->addBlock(std::move(bi));
	}
}

void HexEditor::onEditBlockTriggered(qint32 blockId) {

	AddBlockDialog dlg(DialogType::Block, this);
	const auto& bi = m_blockManager.getBlockById(blockId);
	dlg.editExistingBlock(*bi);

	connect(&dlg, &AddBlockDialog::deleteBlock, &dlg,
		[this, blockId, &dlg] {
			onDeleteBlockTriggered(blockId); // передаём нужный id
			dlg.reject();                    // закрыть диалог
		});

	if(runAndCheckAddBlockDlgData(&dlg)) {
		BlockInfo bi{dlg.startOffset(), dlg.endOffset()};
		bi.name = std::move(dlg.name()); bi.comment = std::move(dlg.comment()); bi.blockType = dlg.blockType();
		bi.color = dlg.color(); bi.collapsed = dlg.collapsed(); bi.isBigEndian = dlg.isBigEndian();
		bi.blockId = blockId;
		undoController->editBlock(std::move(bi));
	}
}

void HexEditor::onDeleteBlockTriggered(qint32 blockId) {
	if(blockId) {
		undoController->removeBlock(blockId);
	}
}

//обработчик изменения размера окна
void HexEditor::resizeEvent(QResizeEvent* event) {
	QAbstractScrollArea::resizeEvent(event);
	updateScrollBars();

	if(event->size().height() != event->oldSize().height()) {
		if(event->size().height() > event->oldSize().height()) {
			screenBuf->updateView(m_pageStartLine, layoutConf->getViewportRowsCount(viewport()->height()));
		}
		const int visualBins = std::clamp((layoutConf->getContentHeight(viewport()->height()) - layoutConf->yMargin()), 256, 4096);
		entrAnalyzer->initHeatmap(visualBins); //перестраиваем бины под новую высоту
	}
	if(entrAnalyzer) {
		const auto lim = screenBuf->getScreenOffsetsLimits();
		if(lim.has_value())
			entrAnalyzer->requestViewportCenter(lim->first, lim->second);
	}
	viewport()->update();
}

void HexEditor::mouseDoubleClickEvent(QMouseEvent* event) {
	const auto pos = event->pos();
	if(event->button() == Qt::LeftButton) {
		//рдактирование блока 
		if(isBlockHeader(pos) && (layoutConf->isHexArea(pos) || layoutConf->isTextArea(pos))) {
			onEditBlockTriggered(getBlockIdByPos(pos));
		}
		if(isBlockFooter(pos) && (layoutConf->isHexArea(pos) || layoutConf->isTextArea(pos))) {
			qint32 id = getBlockIdByPos(pos);
			const auto block = m_blockManager.getBlockById(id);
			if(block) {
				gotoOffset(block->startOffset);
			}
			return;
		}
		if(layoutConf->isBookmarksArea(pos)) {
			auto row = layoutConf->getRowIndexAt(pos, static_cast<qint32>(screenBuf->size()));
			if(row >= 0 && row < screenBuf->size()) {
				const auto rowData = screenBuf->at(row);
				const auto rowStart = rowData.rowOffset;
				const auto startOff = rowStart + rowData.startColumn;
				const auto endOff = rowStart + rowData.endColumn;
				if(rowData.type == rowType::data) {
					if(auto bookmarks = m_bookmarkManager.getBookmarksFromTo(startOff, endOff)) {
						if(bookmarks->size() == 1)
							m_bookmarkManager.deleteBookmark(bookmarks->at(0).offset);
					} else {
						m_bookmarkManager.addBookmark(startOff, "");
					}
					viewport()->update();
				}
				return;
			}
		}
	}
}

void HexEditor::mousePressEvent(QMouseEvent* event) {
	const auto pos = event->pos();

	if(event->button() == Qt::LeftButton) {

		qint64 offset{getOffsetByPos(pos)};
		if(offset >= 0) {
			if(!hasFocus())
				setFocus(Qt::MouseFocusReason);
			//устанавливаем активную зону ввода, чтобы ввод производился в нужном формате
			setActiveInputAreaByPos(pos);

			//если клик с шифтом - выделяем от каретки до смещения в позиции клика
			if(event->modifiers() & Qt::ShiftModifier) {
				m_selection.set(m_caret.byteOffset(), offset);
			} else {

				//на время выделения останавливаем мигание каретки
				m_caret.flash.setEnabled(false);
				m_selection.reset();
				//начинаем выделение
				m_selection.setActive(true);

				//устанавливаем каретку ввода в позицию клика
				setCaret(offset * 2 + layoutConf->getColIndexAt(pos).second);
			}
			viewport()->update();
		}


		LMBHitTest(pos);
		return;
	} else if(event->button() == Qt::RightButton) {
		RMBHitTest(pos);
		return;
	}
	QAbstractScrollArea::mousePressEvent(event);
}

void HexEditor::mouseMoveEvent(QMouseEvent* event) {

	const auto pos = event->pos();
	qint64 offset = getOffsetByPos(pos);

	static qint64 lastHover = -2;
	if(offset != lastHover) {
		lastHover = offset;
		emit mouseMoved(this, offset);
	}

	hoverControls(pos);

	if((event->buttons() & Qt::LeftButton)) {
		if(m_selection.isActive()) {
			if(offset >= 0) {

				if(offset != m_caret.byteOffset() || m_caret.nibbleIdx() != layoutConf->getColIndexAt(pos).second)
					m_selection.set(m_caret.byteOffset(), offset);
				else
					m_selection.reset();

				emit selectionChanged(this);
			}

			//прокручиваем на шаг вниз/вверх, если выделение подошло к низу/верху экрана
			static QElapsedTimer autoscrollTick;
			const auto scrollDelayMs = 16;
			if(!autoscrollTick.isValid() || autoscrollTick.elapsed() > scrollDelayMs) {
				if(pos.y() > viewport()->height() - layoutConf->charHeight() * 1.2) {
					verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepAdd);
				} else if(pos.y() < layoutConf->headerHeight() + layoutConf->charHeight() * 1.2) {
					verticalScrollBar()->triggerAction(QAbstractSlider::SliderSingleStepSub);
				}
				autoscrollTick.start();
			}
			viewport()->update();
		}
		checkHeatMapClick(pos);
	}
	QAbstractScrollArea::mouseMoveEvent(event);
}

void HexEditor::mouseReleaseEvent(QMouseEvent* event) {
	if(event->button() == Qt::LeftButton) {

		const auto pos = event->pos();

		if(m_selection.isActive()) {

			if(!(event->modifiers() & Qt::ShiftModifier)) {
				m_selection.setActive(false);
				emit selectionChanged(this);
				m_selection.normalize();
				//запускаем мигание каретки при завершении выделения
				m_caret.flash.setEnabled(true);
				viewport()->update();
			}
		}
	}
	QAbstractScrollArea::mouseReleaseEvent(event);
}

void HexEditor::focusInEvent(QFocusEvent* e) {
	QAbstractScrollArea::focusInEvent(e);
	//при установке фокуса на виджет выключаем мигание каретки
	m_caret.flash.setEnabled(true);
	viewport()->update();
}

void HexEditor::focusOutEvent(QFocusEvent* e) {
	QAbstractScrollArea::focusOutEvent(e);
	m_caret.flash.setEnabled(false);
	viewport()->update();
}

void HexEditor::wheelEvent(QWheelEvent* event) {
	//увеличение/уменьшение шрифта по Ctrl+колесико
	if(event->modifiers().testFlag(Qt::ControlModifier)) {
		const QPoint angle = event->angleDelta();
		const QPoint pixel = event->pixelDelta();
		int wheelSteps = 0;

		if(!angle.isNull()) {
			//120 единиц = 1 шаг
			wheelSteps = angle.y() / 120;
			if(wheelSteps == 0) wheelSteps = (angle.y() > 0 ? 1 : -1);
		} else if(!pixel.isNull()) {
			wheelSteps = (pixel.y() > 0 ? 1 : -1);
		}

		if(wheelSteps != 0) {
			onScale(wheelSteps);
			event->accept();
			return;
		}
	}

	//обычная прокрутка
	QAbstractScrollArea::wheelEvent(event);
}

//общий обработчик при смене источника данных
void HexEditor::resetContext() {

	if(m_geomChanged) { m_ctx.dataProvider->unsubscribeGeometryChanged(m_geomChanged); m_geomChanged = 0; }
	if(m_dataChangedCbId) { m_ctx.dataProvider->unsubscribeDataChanged(m_dataChangedCbId); m_dataChangedCbId = 0; }

	textEncDec = m_ctx.cpFactory->create(m_ctx.curCpId, m_ctx.dataProvider);
	if(!textEncDec) {
		m_ctx.curCpId = QStringLiteral("utf8");
		textEncDec = m_ctx.cpFactory->create("utf8", m_ctx.dataProvider);
	}
	layoutConf->setTextCaption(textEncDec->name());
	undoController = new EditorUndoController(this, this);

	QPointer<HexEditor> self(this);

	//регистрация колбэка на изменение данных (без изменения размера)
	m_dataChangedCbId = m_ctx.dataProvider->subscribeDataChanged(
		[self](quint64 /*ver*/, const QVector<ChangedSpan>& spans, bool modifiedWhole) {
			if(!self) return;
			QMetaObject::invokeMethod(
				self.data(),
				[self, modifiedWhole, spans]() {
					if(!self) return;

					if(self->entrAnalyzer) {
						if(const auto limit = self->screenBuf->getScreenOffsetsLimits()) {
							self->entrAnalyzer->requestViewportCenter(limit->first, limit->second);
						}
					}

					self->viewport()->update();
					if(self->m_dataModified != modifiedWhole) {
						self->m_dataModified = modifiedWhole;

					}

					qint64 off{-1}, len{0};
					if(!spans.isEmpty()) {
						off = spans[0].m_offset;
						len = spans[0].m_newLen;
					}
					Q_EMIT self->dataChanged(self.data(), off, len);
				},
				Qt::QueuedConnection
			);
		}
	);

	//регистрация колбэка на изменение размера данных
	m_geomChanged = m_ctx.dataProvider->subscribeGeometryChanged(
		[self](quint64 ver, const QVector<GeometryEvent>& events) {
			// Всё, что касается UI/моделей редактора, исполняем в UI-потоке
			auto apply = [self, ver, events]() {          // ВАЖНО: events по значению!
				if(!self) return;

				if(!self->m_dataModified) { self->m_dataModified = true; Q_EMIT self->dataChanged(self.data(), 0, 0); }

				for(const auto& e : events) {
					self->layoutConf->setDataSize(e.newSize);
					self->m_blockManager.setDataSize(e.newSize);
					if(e.kind == GeometryKind::Insert) {
						self->m_blockManager.onInsert(e.at, e.length);
						self->m_bookmarkManager.onInsert(e.at, e.length);
						self->m_fieldManager.onInsert(e.at, e.length);
					} else {
						self->m_blockManager.onRemove(e.at, e.length);
						self->m_bookmarkManager.onRemove(e.at, e.length);
						self->m_fieldManager.onRemove(e.at, e.length);
					}
					Q_EMIT self->geometryChanged(self.data());
				}
				};

			if(QThread::currentThread() == self->thread()) {
				// Уже в UI-потоке — выполнить немедленно
				apply();
			} else {
				// Другой поток — синхронно перебросить в UI-поток и ДОЖДАТЬСЯ завершения
				QMetaObject::invokeMethod(self.data(), std::move(apply), Qt::BlockingQueuedConnection);
			}
		});

	layoutConf->setDataSize(this->m_ctx.dataProvider->size());
	m_blockManager.clear(false);
	m_blockManager.setDataSize(this->m_ctx.dataProvider->size());

	setCaret(0);
	m_selection.reset();
	emit selectionChanged(this);

	layoutConf->updateAreasLayout();
	initEntropyAnalyzer();

	screenBuf->updateDataMap(layoutConf->bytesPerRow(), m_ctx.dataProvider->size());
	updateScrollBars();
}

