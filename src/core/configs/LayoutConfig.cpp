#include "LayoutConfig.h"
#include <QFontMetrics>

LayoutConfig::LayoutConfig(QFont font, qint64 dataSize): m_dataSize(dataSize) {
	m_offsetDigits = calcDigitsCount(dataSize);
	updateLayout(font);
}

qint32 LayoutConfig::guideLineXPos(qint32 level) const {
	const auto xOfBtn = blckCollapseBtnX(level);
	return xOfBtn + m_blockCollapseBtnWidthHeight / 2;
}

//установка заголовка для столбца смещения
void LayoutConfig::setOffsetCaption(const QString& caption) {
	m_offsetCaption = caption;
	m_offsetCaptionWidth = m_curFontMetrics->horizontalAdvance(m_offsetCaption);
	updateAreasLayout();
}

//установка заголовка для столбца декодированного текста
void LayoutConfig::setTextCaption(const QString& caption) {

	m_textCaption = m_accomodatedTextCaption = caption;
	m_textCaptionWidth = m_accTextCaptionWidth = m_curFontMetrics->horizontalAdvance(m_textCaption);

	qint32 textHeaderWidth = m_bytesPerRow * m_charWidth + m_xMargin * 2;
	if(textHeaderWidth < m_textCaptionWidth) {
		qint32 sub = (m_textCaptionWidth - textHeaderWidth)/m_charWidth;
		m_accomodatedTextCaption = m_textCaption.left(m_textCaption.length() - sub - 3 - (m_xMargin*4)/m_charWidth) + "...";
		m_accTextCaptionWidth = m_curFontMetrics->horizontalAdvance(m_accomodatedTextCaption);
	}

	updateAreasLayout();
}

//установка количества отображаемых байт в строке
void LayoutConfig::setBytesPerRow(qint32 bytesPerRow) {
	m_bytesPerRow = std::clamp(bytesPerRow, MIN_BYTES_PER_ROW, MAX_BYTES_PER_ROW);
	updateAreasLayout();
}

//установка количества байт в блоке
void LayoutConfig::setBytesPerBlock(qint32 hexBlockSize) {
	if(hexBlockSize == 0) {
		hexBlockSize = MAX_BYTES_PER_ROW;
	} else if(hexBlockSize > MAX_HEX_BLOCK_SIZE) {
		hexBlockSize = MAX_HEX_BLOCK_SIZE;
	}
	m_bytesPerBlock = hexBlockSize;
	updateAreasLayout();
}

//установка минимального количества отображаемых разрядов смещения 
void LayoutConfig::setMinOffsetDigits(qint32 minDigits) {
	m_minOffsetDigits = minDigits;
	m_offsetDigits = calcDigitsCount(m_dataSize);
	updateAreasLayout();
}

//установка системы счисления для отображаемого смещения
void LayoutConfig::setOffsetBase(qint32 base) {
	//так как calcDigitsCount берёт базу из m_offsetBase - важно установить её до вызова
	m_offsetBase = std::clamp(base, MIN_OFFSET_BASE, MAX_OFFSET_BASE);
	m_offsetDigits = calcDigitsCount(m_dataSize);
	updateAreasLayout();
}

//указание размера данных, для которого формируется раскладка окна
void LayoutConfig::setDataSize(qint64 dataSize) {
	m_dataSize = dataSize;
	m_offsetDigits = calcDigitsCount(dataSize);
	updateAreasLayout();
}

//установка горизонтального и вертикального отступов
void LayoutConfig::setMargins(qint32 xMargin, qint32 yMargin) {
	if(xMargin >= 0 && xMargin <= MAX_MARGINS) m_xMargin = xMargin;
	if(yMargin >= 0 && yMargin <= MAX_MARGINS) m_yMargin = yMargin;
	m_manualMarginsEnabled = true;
	updateAreasLayout();
}

//установка расстояния между блоками байтов
//указывается как относительный к расстоянию м/у байтами (0 - нет отступа совсем, 1 - равно отступу м/у байтами)
void LayoutConfig::setHexBlockSpacing(double byteSpacingFactor) {
	if(byteSpacingFactor < 0) m_hexBlockSpacingFactor = 0.;
	else m_hexBlockSpacingFactor = byteSpacingFactor;
	updateAreasLayout();
}

void LayoutConfig::setBlckHeadlineWidthFactor(double factor) {
	//TODO
}

void LayoutConfig::setMaxLevel(qint32 level) {
	m_maxLevel = level;
	updateAreasLayout();
}

//пересчёт параметров раскладки отображения зависящих от шрифта
void LayoutConfig::updateLayout(const QFont& font) {
	m_curFontMetrics = std::make_unique<QFontMetrics>(QFontMetrics{font});

	m_charHeight = m_curFontMetrics->capHeight();
	m_charWidth = m_curFontMetrics->maxWidth();
	m_byteWidth = m_charWidth * 2;

	qint32 tmpBS = m_charWidth / 1.5;
	m_byteSpacing = tmpBS - tmpBS % 2;

	qint32 tmpLS = m_charHeight / 1.5;
	m_lineSpacing = tmpLS - tmpLS % 2;//для корректного выделения должно быть чётным

	m_xStep = m_byteWidth + m_byteSpacing;
	m_yStep = m_charHeight + m_lineSpacing;

	m_headerHeight = m_charHeight * 2;

	m_headerTextBaseLine = m_headerHeight - m_charHeight / 3;

	m_offsetCaptionWidth = m_curFontMetrics->horizontalAdvance(m_offsetCaption);
	m_textCaptionWidth = m_curFontMetrics->horizontalAdvance(m_textCaption);
	m_accTextCaptionWidth = m_curFontMetrics->horizontalAdvance(m_accomodatedTextCaption);

	m_blockHeaderLineWidth = m_charHeight * m_blockHeadlineWidthFactor;
	m_blockCollapseBtnWidthHeight = m_charHeight;

	m_entropyLineWidth = m_byteWidth / 2;
	m_entropyHeatmapWidth = m_byteWidth / 2;

	updateAreasLayout();
}

//обновление раскладки для зон окна редактора
void LayoutConfig::updateAreasLayout() {
	if(!m_manualMarginsEnabled) {
		m_xMargin = m_curFontMetrics->horizontalAdvance("0")/2+2;
		m_yMargin = m_xMargin;
	}
	
	if(!m_dataSize) {
		m_offsetColumnWidth = m_curFontMetrics->horizontalAdvance(m_offsetCaption);
	} else {
		QString tmp = QString("%1").arg(0, m_offsetDigits, m_offsetBase, QChar('0')).toUpper();
		m_offsetColumnWidth = m_curFontMetrics->horizontalAdvance(tmp);
	}
	m_offsetColumnWidth += m_xMargin * 2 + m_bordersWidth - m_curFontMetrics->rightBearing('F');

	m_bookmarksStartX = 0;
	m_bookmarksColumnWidth = m_curFontMetrics->horizontalAdvance("0") + m_xMargin + m_bordersWidth;
	m_bookmarkBulletWidth = m_bookmarksColumnWidth - m_xMargin;
	m_markersStartX = m_bookmarksStartX + m_bookmarksColumnWidth + m_xMargin/2;

	m_markersColumnWidth = 0;

	if(m_maxLevel != -1) {
		m_markersColumnWidth = m_blockCollapseBtnWidthHeight + (m_blockCollapseBtnWidthHeight *0.8) * (m_maxLevel) + m_xMargin;
	}

	m_bookmarkBulletX = (m_bookmarksColumnWidth - m_bookmarkBulletWidth)/2;
	m_offsetStartX = m_bookmarksColumnWidth + m_markersColumnWidth + m_xMargin;

	m_hexBlockSpacing = m_byteSpacing * m_hexBlockSpacingFactor - m_byteSpacing;
	m_hexStartX = m_bookmarksColumnWidth + m_markersColumnWidth + m_offsetColumnWidth + m_xMargin;
	m_hexColumnWidth = m_xMargin * 2 + m_bytesPerRow * m_xStep +
		(((m_bytesPerRow - 1) / m_bytesPerBlock) * m_hexBlockSpacing) - m_byteSpacing + m_bordersWidth;
	m_textStartX = m_bookmarksColumnWidth + m_markersColumnWidth + m_offsetColumnWidth + m_hexColumnWidth + m_xMargin;

	m_textColumnWidth = m_bytesPerRow * m_charWidth + m_xMargin * 2;

	m_offsetCaptionXPos = ((m_offsetStartX - m_xMargin) * 2 + m_offsetColumnWidth) / 2 - m_offsetCaptionWidth / 2;

	m_textCaptionXPos = (m_textStartX - m_xMargin) + m_textColumnWidth / 2 - m_accTextCaptionWidth / 2;

	m_blockCaptionX = m_hexStartX;
	m_blockCollapseButtonX = m_markersStartX;
}


//возвращает зону окна по позиции в окне
EditorArea LayoutConfig::getArea(const QPoint& pos) {
	EditorArea tmp = EditorArea::NON_SPECIFIC;
	if(isOffsetArea(pos)) {
		tmp = EditorArea::OFFSET_AREA;
	} else if(isHexArea(pos)) {
		tmp = EditorArea::HEX_AREA;
	} else if(isTextArea(pos)) {
		tmp = EditorArea::TEXT_AREA;
	} else if(isBookmarksArea(pos)) {
		tmp = EditorArea::BOOKMARKS_AREA;
	} else if(isBlockMarkersArea(pos)) {
		tmp = EditorArea::BLOCK_GUIDES_AREA;
	}

	if(isHeaderArea(pos)) {
		tmp = (EditorArea)((int)tmp + 1);
	}
	return tmp;
}

//высота области отображения контента (только данные без заголовка)
qint32 LayoutConfig::getContentHeight(qint32 viewportHeight) const {
	return viewportHeight - m_headerHeight - m_yMargin;
}

//ширина контента
qint32 LayoutConfig::getContentWidth() const {
	return m_textStartX + m_textColumnWidth;
}

bool LayoutConfig::isHeaderArea(const QPoint& pos) const {
	return pos.y() < m_headerHeight;
}

bool LayoutConfig::isHexArea(const QPoint& pos) const {
	const auto startOfColumn = m_hexStartX - m_xMargin;
	return (pos.x() >= startOfColumn) && (pos.x() < (startOfColumn + m_hexColumnWidth));
}

bool LayoutConfig::isOffsetArea(const QPoint& pos) const {
	const auto startOfColumn = m_offsetStartX - m_xMargin;
	return (pos.x() >= startOfColumn) && (pos.x() < (startOfColumn + m_offsetColumnWidth));
}

bool LayoutConfig::isTextArea(const QPoint& pos) const {
	const auto startOfColumn = m_textStartX - m_xMargin;
	return (pos.x() >= startOfColumn) && (pos.x() < (startOfColumn + m_textColumnWidth));
}

bool LayoutConfig::isBookmarksArea(const QPoint& pos) const {
	return (pos.x() >= m_bookmarksStartX) && (pos.x() < (m_bookmarksStartX + m_bookmarksColumnWidth));
}

bool LayoutConfig::isBlockMarkersArea(const QPoint& pos) const {
	if(m_maxLevel < 0) return false;
	const auto startOfColumn = m_markersStartX - (m_xMargin/2);
	return (pos.x() >= startOfColumn) && (pos.x() < (startOfColumn + m_markersStartX - m_bordersWidth*2) + m_maxLevel * m_blockCollapseBtnWidthHeight);
}

QRect LayoutConfig::getRectOfRow(qint32 rowIdx) {
	QRect rowRect{};
	if(rowIdx >= 0) {
		qint32 yPosBottom = m_yMargin + m_charHeight + m_headerHeight + rowIdx * m_yStep;
		qint32 yPosTop = yPosBottom - m_yStep;
		qint32 xLeft = 0;
		qint32 xRight = m_textStartX - m_xMargin + m_textColumnWidth;
		rowRect.setCoords(xLeft, yPosTop, xRight, yPosBottom);
	} 
	return rowRect;
}

//возвращает количество строк, которые вместятся во vieport при текущих параметрах раскладки
qint32 LayoutConfig::getViewportRowsCount(qint32 viewportHeight) const {
	//если области контента достаточно для отображения половины строки в конце - отображаем
	int vis = ((getContentHeight(viewportHeight) % m_yStep) >= (m_yStep / 2)) ? 1 : 0;
	return getContentHeight(viewportHeight) / m_yStep + vis;
}

//возвращает координату x, с которой начинается байт col в окне
qint32 LayoutConfig::getXPosOfCol(qint32 col) const {
	if(col < 0 || col > m_bytesPerRow) 
		return -1;

	return m_hexStartX + (m_xStep * col) + (m_hexBlockSpacing * (col / m_bytesPerBlock));
}

//возвращает координату y строки (базовая линия текста)
qint32 LayoutConfig::getYPosOfRow(qint32 row, qint32 viewportHeight) const {
	if(row < 0 || row > getViewportRowsCount(viewportHeight))
		return -1;

	return m_yMargin + m_charHeight + m_headerHeight + row * m_yStep;
}

//возвращает полные координаты x и y для строки и столбца
QPoint LayoutConfig::getPosOfRowCol(qint32 row, qint32 col, qint32 viewportHeight) const {
	return QPoint{getXPosOfCol(col), getYPosOfRow(row, viewportHeight)};
}

//возвращает координату начала блока сгруппированных байтов
qint32 LayoutConfig::getHexBlockStartX(qint32 hexBlockNum) const {
	return m_hexStartX + ((m_xStep * m_bytesPerBlock) * hexBlockNum) + (m_hexBlockSpacing * hexBlockNum);
}

//возвращает пару (байт, ниббл) по позиции в окне
std::pair<qint32, qint32> LayoutConfig::getColIndexAt(const QPoint& pos) const {
	if(!isOffsetArea(pos)) {

		qint32 x = pos.x(), y = pos.y();

		if(isHexArea(pos)) {

			//если на границах - возвращаем 0 или m_bytesperrow
			if(x < m_hexStartX) 
				return std::make_pair(0, 0);

			if(x > m_textStartX - m_xMargin * 2)
				return std::make_pair(m_bytesPerRow - 1, 1);

			auto hexBlockNum = (x - m_hexStartX) / (m_xStep * m_bytesPerBlock + m_hexBlockSpacing);
			auto nextBlockStartX = getHexBlockStartX(hexBlockNum + 1);
			auto blockSpacingCenter = nextBlockStartX - (m_hexBlockSpacing + m_byteSpacing) / 2;

			//обрабатываем позицию между блоками hex - возвращаем ближайший слева или справа
			if(x >= blockSpacingCenter && x < nextBlockStartX) {
				return std::make_pair((hexBlockNum + 1) * m_bytesPerBlock, 0);
			}
			if(x < blockSpacingCenter && x > blockSpacingCenter - (m_hexBlockSpacing + m_byteSpacing) / 2) {
				return std::make_pair(hexBlockNum * m_bytesPerBlock + m_bytesPerBlock - 1, 1);
			}

			//считаем индекс байта в строке
			auto relX = (x - getHexBlockStartX(hexBlockNum) + m_byteSpacing / 2);
			auto byteIdxInBlock = relX / m_xStep;
			auto byteIdx = byteIdxInBlock + hexBlockNum * m_bytesPerBlock;

			//считаем индекс полубайта (разряда) в байте
			auto byteCenterX = getHexBlockStartX(hexBlockNum) + byteIdxInBlock * m_xStep + m_charWidth;
			auto halfByteIdx = x < byteCenterX ? 0 : 1;

			return std::make_pair(byteIdx, halfByteIdx);

		} else if(isTextArea(pos)) {

			if(x < m_textStartX) {
				return std::make_pair(0, 0);
			}
			auto byteIdx = (x - m_textStartX) / m_charWidth;
			if(byteIdx >= m_bytesPerRow) {
				byteIdx = m_bytesPerRow - 1;
			}
			return std::make_pair(byteIdx, 0);
		}
	}
	return std::make_pair(-1, -1);
}

//возвращает индекс строки по позиции в окне
qint32 LayoutConfig::getRowIndexAt(const QPoint& pos, qint32 lastRow) const {
	qint32 topLine = m_headerHeight + m_yMargin - m_lineSpacing / 2;
	int bottomLine = topLine + ((lastRow)*m_yStep);

	if(pos.y() > bottomLine) {
		return  -1;
	}
	if(pos.y() > topLine/* && pos.y() < bottomLine*/) {
		return (pos.y() - topLine) / m_yStep;
	}

	return qint32(-1);
}

//возвращает пару (строка, столбец) для указанным координатам в окне
std::pair<qint32, qint32> LayoutConfig::getRowColAt(const QPoint& pos, qint32 lastRow) const {
	qint32 row = getRowIndexAt(pos, lastRow);
	qint32 byte = getColIndexAt(pos).first;

	return std::make_pair(row, byte);
}

//считает количество разрядов, необходимое для отображения числа
qint32 LayoutConfig::calcDigitsCount(qint64 number) {
	if(number == 0) 
		return 1;
	return qMax(static_cast<qint32>(std::floor(std::log(number) / std::log(m_offsetBase)) + 1), m_minOffsetDigits);
}
