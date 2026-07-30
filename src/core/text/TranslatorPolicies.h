#pragma once
#include <QString>

enum class OverflowMode {
	truncate,
	reject
};

enum class UnrepresentableMode {
	reject,                 //не писать непредставимые символы (0)
	substituteWithQuestion  //писать 0x3F ('?')
};

struct TranslatorPolicies {
	QString m_nonPrintablePlaceholder{"."};
	QString m_continuationPlaceholder{"."};
	OverflowMode m_overflowMode{OverflowMode::truncate};
	UnrepresentableMode m_unrepresentableMode{UnrepresentableMode::reject};
};
