#include "VisualConfig.h"
#include <QFontInfo>

VisualConfig::VisualConfig() {
	applyFont(QFont{"",14});
	applyAltFont();
}

VisualConfig::VisualConfig(QFont font) {
	applyFont(std::move(font));
	applyAltFont();
}

//рассчитывает контрастный цвет к аргументу color и цвету фона
QColor VisualConfig::calcContrastColorTo(const QColor& color, double minContrastToBlock, double minContrastToBg) {

	//расчёт относительной яркости в sRGB по стандарту WCAG
	auto luminance = [](const QColor& color) {
		auto toLinear = [](double x) {
			//значения из sRGB (IEC 61966-2-1)
			return (x <= 0.03928)
				? x / 12.92
				: std::pow((x + 0.055) / 1.055, 2.4);
			};
		double r = toLinear(color.redF());
		double g = toLinear(color.greenF());
		double b = toLinear(color.blueF());
		return 0.2126 * r + 0.7152 * g + 0.0722 * b;
		};

	//расчет контраста
	auto contrast = [&](const QColor& a, const QColor& b) {
		double L1 = luminance(a), L2 = luminance(b);
		if(L1 < L2) std::swap(L1, L2);
		return (L1 + 0.05) / (L2 + 0.05);
		};

	int h, s, v;
	color.getHsv(&h, &s, &v);

	double bgLum = luminance(m_bgColor);
	bool bgIsLight = bgLum > 0.5;

	int step = bgIsLight ? -15 : +15;
	int nv = v;
	QColor cand;

	for(int i = 0; i < 8; i++) { // до 8 итераций
		nv = std::clamp(nv + step, 0, 255);
		cand = QColor::fromHsv(h, s, nv);

		if(contrast(cand, color) >= minContrastToBlock &&
			contrast(cand, m_bgColor) >= minContrastToBg)
			return cand;
	}
	return bgIsLight ? QColor(80, 80, 80) : QColor(200, 200, 200);
}

VisualConfig::callbackID VisualConfig::regFontChangedCallback(callbackType cb) {
	fontChangedCallbacks.emplace(cbID, std::move(cb));
	return cbID++;
}

void VisualConfig::unregFontChangedCallback(callbackID cbId) {
	fontChangedCallbacks.erase(cbId);
}

#include <QFontDatabase>

void VisualConfig::applyFont(QFont font) {
	font.setKerning(false);
	font.setStyle(QFont::StyleNormal);
	font.setWeight(QFont::Normal);

	//лимиты по размеру
	qint32 sz = std::clamp(font.pointSize(), MIN_FONT_SIZE, MAX_FONT_SIZE);

	font.setPointSize(sz);

	//для linux
	font.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);

	m_font = font;

	//если моноширинный - применяем
	if(QFontInfo(m_font).fixedPitch())
		return;

	//подбираем моноширинный
	for(const auto& fam : monoCandidates) {
		QFont tryFont(fam, sz);
		tryFont.setStyleHint(QFont::TypeWriter, QFont::PreferDefault);
		if(QFontInfo(tryFont).fixedPitch()) {
			m_font = tryFont;
			return;
		}
	}

	//выбираем системный моноширинный, если не подобрали
	QFont sysMono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
	sysMono.setPointSize(sz);
	m_font = sysMono;
}


void VisualConfig::invokeFontChangedCallbacks() {
	if (fontChangedCallbacks.empty()) 
		return;
	for (const auto& [id, cb] : fontChangedCallbacks) {
		cb(m_font);
	}
}

QColor VisualConfig::getRandomColor(qreal maxLightness, qreal minSaturation, QRandomGenerator* rng) {
	maxLightness = std::clamp(maxLightness, 0.0, 1.0);
	minSaturation = std::clamp(minSaturation, 0.0, 1.0);

	const qreal hue = rng->generateDouble(); 
	const qreal saturation = minSaturation + (1.0 - minSaturation) * rng->generateDouble(); //[minS,1]
	const qreal lightness = rng->bounded(maxLightness);

	QColor c = QColor::fromHslF(hue, saturation, lightness, 1.0);
	return c.toRgb();
}

void VisualConfig::setFont(QFont font) {
	if (font == m_font) 
		return;

	applyFont(std::move(font));
	applyAltFont();
	invokeFontChangedCallbacks();
}
