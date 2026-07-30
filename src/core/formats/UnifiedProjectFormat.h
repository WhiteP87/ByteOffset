#pragma once
#include <QtCore>

//комментарий: контейнер .hecx с произвольными utf-8 тегами
//layout:
// [magic(6)='HECX1\0'][u16 ver=1]
// many: [u16 tagLen][tagUtf8][u16 secVer][u16 flags=0][u64 payloadLen][payload...]
namespace UnifiedProjectFormat {
	static constexpr char    kMagic[6] = {'H','E','C','X','1','\0'};
	static constexpr quint16 kVersion = 1;
}

