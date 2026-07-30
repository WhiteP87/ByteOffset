#pragma once
#include <QFont>
#include <QColor>
#include <QRandomGenerator>
#include <QtGlobal>

class VisualConfig {
public:
	
	using callbackID = quint32;
	using callbackType = std::function<void(const QFont&)>;

	//конструкторы
	VisualConfig();
	VisualConfig(QFont font);

	//геттеры
	QColor bgColor() const { return m_bgColor; }
	QColor hexColor(qint32 colorIdx = 0) const {
		if(isHexAltEnabled() && colorIdx)
			return m_hexColorAlt;
		return m_hexColor;
	}
	QColor hexColorAlt() const { return m_hexColorAlt; }
	QColor textColor() const { return m_textColor; }
	QColor offsetColor() const { return m_offsetColor; }
	QColor selectionColor() const { return m_selectionColor; }
	QColor caretFillColor() const { return m_caretFillColor; }
	QColor caretFontColor() const { return m_caretFontColor; }
	QColor caretAltFillColor() const { return m_caretAltFillColor; }
	QColor caretAltFontColor() const { return m_caretAltFontColor; }
	QColor caretSelectedAltFillColor() const { return m_selAltFillCaretColor; }
	QColor bordersColor() const { return m_bordersColor; }
	QColor captionsColor() const { return m_captionsColor; }
	QColor modifiedColor() const { return m_modifiedColor; }
	QColor bookmarkBulletColor() const { return m_bookmarkBulletColor; }
	QColor bookmarkAreaBgColor() const { return m_bookmarkAreaBgColor; }
	QColor fieldBulletColor() const { return m_fieldBulletColor; }
	QColor caretCrosslineColor() const { return m_caretCrosslineColor; }
	QFont font() const { return m_font; }
	QFont altFont() const { return m_altFont; }
	double blckGuideLineAlpha() const { return m_blckGuideLineAlpha; }
	Qt::PenStyle partialBlckLineStyle() const { return m_partialBlckLineStyle; }
	bool isHexAltEnabled() const { return m_altHexColorEnabled; }
	QColor getRandomColor(qreal maxLightness = 0.6,
		qreal minSaturation = 1,
		QRandomGenerator* rng = QRandomGenerator::global());

	//сеттеры
	void setBgColor(QColor color) { m_bgColor = std::move(color); }
	void setHexColor(QColor color) { m_hexColor = std::move(color); }
	void setHexColorAlt(QColor color) { m_hexColorAlt = std::move(color); }
	void setTextColor(QColor color) { m_textColor = std::move(color); }
	void setOffsetColor(QColor color) { m_offsetColor = std::move(color); }
	void setCaretCrosslineColor(QColor color) { m_caretCrosslineColor = std::move(color); }

	const double BLEND_RATIO = 0.25;

	void setSelectionColor(QColor color) {
		m_selectionColor = std::move(color); 
		m_selAltFillCaretColor = linInterpColors(m_selectionColor, m_caretAltFillColor, BLEND_RATIO);
	}
	void setCaretFillColor(QColor color) { m_caretFillColor = std::move(color); }
	void setCaretFontColor(QColor color) { m_caretFontColor = std::move(color); }
	void setCaretAltFillColor(QColor color) { 
		m_caretFillColor = std::move(color);
		m_selAltFillCaretColor = linInterpColors(m_selectionColor, m_caretAltFillColor, BLEND_RATIO);
	}
	void setCaretAltFontColor(QColor color) { m_caretFontColor = std::move(color); }
	void setBordersColor(QColor color) { m_bordersColor = std::move(color); }
	void setCaptionsColor(QColor color) { m_captionsColor = std::move(color); }
	void setModifiedColor(QColor color) { m_modifiedColor = std::move(color); }
	void setBookmarkBulletColor(QColor color) {m_bookmarkBulletColor = std::move(color);}
	void setFieldBulletColor(QColor color) { m_fieldBulletColor = std::move(color); }
	void setBookmarkAreaBgColor(QColor color) { m_bookmarkAreaBgColor = std::move(color); }
	void setFont(QFont font);
	void setBlckGuideLineAlpha(double factor) { m_blckGuideLineAlpha = factor; }
	void setPartialBlckLineStyle(Qt::PenStyle style) { m_partialBlckLineStyle = style; }
	void enableAltHexColor(bool enable) { m_altHexColorEnabled = enable; }
	

	//вспомогательные
	QColor calcContrastColorTo(const QColor& color, double minContrastToBlock = 2.0, double minContrastToBg = 3.0);

	callbackID regFontChangedCallback(callbackType cb);
	void unregFontChangedCallback(callbackID cbId);

	void applyAltFont() {
		m_altFont = m_font;
		m_altFont.setPointSize(m_font.pointSize() - 2);
	}

	//линейная интерполяция цветов по весу
	QColor linInterpColors(const QColor& c1, const QColor& c2, double t) {
		int r = c1.red() * (1 - t) + c2.red() * t;
		int g = c1.green() * (1 - t) + c2.green() * t;
		int b = c1.blue() * (1 - t) + c2.blue() * t;
		int a = c1.alpha() * (1 - t) + c2.alpha() * t;
		return QColor(r, g, b, a);
	}

private:
	const qint32 MIN_FONT_SIZE = 7;
	const qint32 MAX_FONT_SIZE = 50;

	const QStringList monoCandidates = {
		"Monospace",//alias на Linux
		"Consolas", //Windows
		"Courier New", //Windows
		"DejaVu Sans Mono",   //Linux
		"Liberation Mono"     //Linux
	};

	// цвета редактора
	QColor m_bgColor{Qt::white};
	QColor m_hexColor{Qt::black};
	QColor m_hexColorAlt{Qt::black};
	QColor m_textColor{Qt::black};
	QColor m_offsetColor{Qt::blue};
	QColor m_selectionColor{QColor{0, 0, 255, 45}};
	QColor m_caretFillColor{Qt::blue};
	QColor m_caretFontColor{Qt::white};
	QColor m_caretAltFillColor{QColor{192, 192, 192}};
	QColor m_caretAltFontColor{Qt::black};
	QColor m_bordersColor{QColor(235, 235, 235)};
	QColor m_captionsColor{Qt::blue};
	QColor m_modifiedColor{Qt::red};
	QColor m_bookmarkBulletColor{QColor{255, 0, 0, 128}};
	QColor m_fieldBulletColor{QColor{0,0,0,128}};
	QColor m_bookmarkAreaBgColor{QColor{192,192,192}};
	double m_blckGuideLineAlpha{0.6};
	Qt::PenStyle m_partialBlckLineStyle{Qt::DashLine};
	bool m_altHexColorEnabled{false};
	QColor m_selAltFillCaretColor{QColor{48, 48, 239, 82}};
	QColor m_caretCrosslineColor{QColor{128, 128, 128, 64}};


	//шрифты редактора
	QFont m_font{};
	QFont m_altFont{};
	void applyFont(QFont font);

	std::unordered_map<callbackID, callbackType> fontChangedCallbacks;
	callbackID cbID{1};
	void invokeFontChangedCallbacks();


};