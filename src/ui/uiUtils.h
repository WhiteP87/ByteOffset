#pragma once

inline bool parseOffset(const QString& text, qint64& outValue, qint32* outBase = nullptr) {
	bool ok = false;
	if(text.isEmpty())
		return ok;
	if(text.startsWith("0x", Qt::CaseInsensitive)) {
		outValue = text.mid(2).toULongLong(&ok, 16);
		if(outBase) *outBase = 16;
	} else if(text.startsWith("0") && text.size() > 1) {
		outValue = text.toULongLong(&ok, 8);
		if(outBase) *outBase = 8;
	} else {
		outValue = text.toULongLong(&ok, 10);
		if(outBase) *outBase = 10;
	}
	return ok;
}

inline const QString getFormatString(qint32 base) {
	return (base == 10 ? "%1" : (base == 16 ? "0x%1" : "0%1"));
}