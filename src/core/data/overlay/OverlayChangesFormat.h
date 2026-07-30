#pragma once
#include <QtCore>

//внутренние константы секции правок
namespace OverlayChangesFormat {
	static constexpr char MAGIC[6] = {'H','X','O','V','1','\0'};
	static constexpr quint16 VERSION = 1;
	enum class PieceTag: quint8 { Base = 0, Add = 1 };
}
