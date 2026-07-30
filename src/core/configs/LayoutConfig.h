#pragma once
#include <QtGlobal>
#include <QFont>
#include <QWidget>

enum class EditorArea {
	OFFSET_AREA, 
	OFFSET_HEADER_AREA, 
	HEX_AREA, 
	HEX_HEADER_AREA, 
	TEXT_AREA, 
	TEXT_HEADER_AREA, 
	BOOKMARKS_AREA,
	BOOKMARKS_HEADER_AREA,
	BLOCK_GUIDES_AREA,
	BLOCK_GUIDES_HEADER_AREA,
	NON_SPECIFIC
};

class LayoutConfig {
public:
	LayoutConfig(QFont font, qint64 dataSize);

	qint32 bordersWidth() const { return m_bordersWidth; }
	qint32 guideLineXPos(qint32 level) const;
	qint32 minOffsetDigits() const { return m_minOffsetDigits; }
	qint32 offsetDigits() const { return m_offsetDigits; }
	qint32 offsetBase() const { return m_offsetBase; }
	qint32 bytesPerRow() const { return m_bytesPerRow; }
	qint32 hexBlockSize() const { return m_bytesPerBlock; }
	qint32 hexStartX() const { return m_hexStartX; }
	qint32 textStartX() const { return m_textStartX; }
	qint32 offsetStartX() const { return m_offsetStartX; }
	qint32 xMargin() const { return m_xMargin; }
	qint32 yMargin() const { return m_yMargin; }
	qint32 charWidth() const { return m_charWidth; }
	qint32 charHeight() const { return m_charHeight; }
	qint32 byteWidth() const { return m_byteWidth; }
	qint32 byteSpacing() const { return m_byteSpacing; }
	qint32 lineSpacing() const { return m_lineSpacing; }
	qint32 hexBlockSpacing() const { return m_hexBlockSpacing; }
	qint32 xStep() const { return m_xStep; }
	qint32 yStep() const { return m_yStep; }
	qint32 headerHeight() const { return m_headerHeight; }
	qint32 captionsBaseLine() const { return m_headerTextBaseLine; }
	qint32 offsetCaptionWidth() const { return m_offsetCaptionWidth; }
	qint32 textCaptionWidth() const { return m_textCaptionWidth; }
	qint32 accomodatedTextCaptionWidth() const { return m_accTextCaptionWidth; }
	qint32 offsetCaptionXPos() const { return m_offsetCaptionXPos; }
	qint32 textCaptionXPos() const { return m_textCaptionXPos; }
	qint32 fontAscent() const { return m_curFontMetrics->ascent(); }
	qint32 fontDescent() const { return m_curFontMetrics->descent(); }
	qint32 blckHeadlineWidth() const { return m_blockHeaderLineWidth; }
	double blckHeadlineWidthFactor() const { return m_blockHeadlineWidthFactor; }
	qint32 bookmarkBulletWidth() const { return m_bookmarkBulletWidth; }
	qint32 blckCollapseBtnX(qint32 level) const {
		return m_markersStartX + ((m_blockCollapseBtnWidthHeight * 0.8) * level);
	}
	qint32 markersStartX() { return m_markersStartX; }
	qint32 blckCollapseBtnWidth() const { return m_blockCollapseBtnWidthHeight; }
	qint32 blckCaptionX() const { return m_blockCaptionX; }
	qint32 maxBlockLevel()const { return m_maxLevel; }
	qint32 bookmarksStartX() const { return m_bookmarksStartX; }
	qint32 bookmarkBulletX() const { return m_bookmarkBulletX; }
	const QString& offsetCaption() const { return m_offsetCaption; }
	const QString& textCaption() const { return m_textCaption; }
	const QString& accomodatedTextCaption() const { return m_accomodatedTextCaption; }

	qint32 bookmarksColumnWidth() const { return m_bookmarksColumnWidth; }
	qint32 markersColumnWidth() const { return m_markersColumnWidth; }
	qint32 offsetColumnWidth() const { return m_offsetColumnWidth; }
	qint32 hexColumnWidth() const { return m_hexColumnWidth; }
	qint32 textColumnWidth() const { return m_textColumnWidth; }

	qint32 entropyLineX() const { return (getContentWidth()); }
	qint32 entropyLineWidth() const { return m_entropyLineWidth; }
	qint32 entropyHeatmapX() const { return entropyLineX() + m_entropyLineWidth; }
	qint32 entropyHeatmapWidth() const { return m_entropyHeatmapWidth; }

	QRect getEntropyLineRect(qint32 viewportHeight) const {
		if(!m_entropyLineWidth) return QRect{};
		return QRect(entropyLineX(), m_headerHeight, m_entropyLineWidth, getContentHeight(viewportHeight) + m_yMargin);
	}
	QRect getEntropyHeatmapRect(qint32 viewportHeight) const {
		if(!m_entropyHeatmapWidth) return QRect{};
		return QRect(entropyLineX() + m_entropyLineWidth, m_headerHeight, m_entropyHeatmapWidth, getContentHeight(viewportHeight) + m_yMargin);
	}

	void setOffsetCaption(const QString& caption);
	void setTextCaption(const QString& caption);
	void setBytesPerRow(qint32 bytesPerRow);
	void setBytesPerBlock(qint32 hexBlockSize);
	void setMinOffsetDigits(qint32 minDigits);
	void setOffsetBase(qint32 base);
	void setDataSize(qint64 dataSize);
	void setMargins(qint32 xMargin, qint32 yMargin);
	void setHexBlockSpacing(double byteSpacingFactor);
	void setBlckHeadlineWidthFactor(double factor);
	void setMaxLevel(qint32 level);
	void setBordersWidth(qint32 width) { m_bordersWidth = width; }
	void setHeatmapHovered(bool hovered) { 
		if(hovered){ m_entropyHeatmapWidth = m_byteWidth + m_byteSpacing; }
		else { m_entropyHeatmapWidth = m_byteWidth / 2; }		
	}

	void updateLayout(const QFont& font);
	void updateAreasLayout();
	EditorArea getArea(const QPoint& pos);
	qint32 getContentHeight(qint32 viewportHeight) const;//высота вьюпорта для отображения контента (без хэдера)
	qint32 getContentWidth() const;

	bool isHeaderArea(const QPoint& pos) const;
	bool isHexArea(const QPoint& pos) const;
	bool isOffsetArea(const QPoint& pos) const;
	bool isTextArea(const QPoint& pos) const;
	bool isBookmarksArea(const QPoint& pos) const;
	bool isBlockMarkersArea(const QPoint& pos) const;

	QRect getRectOfRow(qint32 rowIdx);
	qint32 getViewportRowsCount(qint32 viewportHeight) const;
	qint32 getXPosOfCol(qint32 col) const;
	qint32 getYPosOfRow(qint32 row, qint32 viewportHeight) const;
	QPoint getPosOfRowCol(qint32 row, qint32 col, qint32 viewportHeight) const;

	qint32 getHexBlockStartX(qint32 hexBlockNum) const;
	std::pair<qint32, qint32> getColIndexAt(const QPoint& pos) const;

	qint32 getRowIndexAt(const QPoint& pos, qint32 lastRow) const;
	std::pair<qint32, qint32> getRowColAt(const QPoint& pos, qint32 lastRow) const;
private:
	qint32 calcDigitsCount(qint64 number);

	const qint32 MIN_BYTES_PER_ROW = 1;
	const qint32 MAX_BYTES_PER_ROW = 64;
	const qint32 MIN_OFFSET_BASE = 8;
	const qint32 MAX_OFFSET_BASE = 36;
	const qint32 MAX_HEX_BLOCK_SIZE = MAX_BYTES_PER_ROW;
	const qint32 MAX_MARGINS = 45;

	QString m_offsetCaption{"Offset"};
	qint32 m_offsetCaptionWidth{};
	qint32 m_offsetCaptionXPos{};

	QString m_textCaption{"Decoded"};
	QString m_accomodatedTextCaption{"Decoded"};
	qint32 m_textCaptionWidth{};
	qint32 m_accTextCaptionWidth{};
	qint32 m_textCaptionXPos{};

	qint32 m_headerTextBaseLine{};
	qint32 m_xMargin{7};
	qint32 m_yMargin{7};
	qint32 m_bordersWidth{1};
	bool m_manualMarginsEnabled{false};

	qint32 m_charWidth{};
	qint32 m_charHeight{};
	qint32 m_byteWidth{};

	qint32 m_byteSpacing{};
	qint32 m_lineSpacing{};
	qint32 m_hexBlockSpacing{};
	double m_hexBlockSpacingFactor{2};

	qint32 m_xStep{};
	qint32 m_yStep{};

	qint32 m_headerHeight{};

	qint32 m_bytesPerRow{16};       //отображаемых байт в строке
	qint32 m_bytesPerBlock{8};       //отображаемых байт в блоке
	qint32 m_minOffsetDigits{8};    //минимальное количество отображаемых разрядов смещения
	qint32 m_offsetDigits{0};		//текущее количество отображаемых разрядов
	qint32 m_offsetBase{16};        //система счисления смещения

	qint32 m_hexStartX{};           //координата x начала области hex-представления
	qint32 m_textStartX{};          //координата x начала области текстового представления
	qint32 m_offsetStartX{};        //координата x начала области отображения смещения
	qint32 m_markersStartX{};		//координата x начала области отображения маркеров блоков
	qint32 m_bookmarksStartX{};
	qint32 m_bookmarkBulletWidth{};
	qint32 m_bookmarkBulletX{};

	qint32 m_bookmarksColumnWidth{};
	qint32 m_markersColumnWidth{};
	qint32 m_offsetColumnWidth{};
	qint32 m_hexColumnWidth{};
	qint32 m_textColumnWidth{};

	qint32 m_maxLevel{-1};

	qint64 m_dataSize{};
	qint32 m_offsetFieldWidth{};

	double m_blockHeadlineWidthFactor{0.15};
	qint32 m_blockHeaderLineWidth{};
	qint32 m_blockCollapseButtonX{};
	qint32 m_blockCaptionX{};
	qint32 m_blockCollapseBtnWidthHeight{};

	qint32 m_entropyLineWidth{};
	qint32 m_entropyHeatmapWidth{};

	std::unique_ptr<QFontMetrics> m_curFontMetrics{nullptr};
};
