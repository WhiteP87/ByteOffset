#pragma once
#include <QtCore>
#include "OverlayChangesFormat.h"

class PagedPieceOverlay;

class OverlayChangesWriter {
public:
	OverlayChangesWriter(const PagedPieceOverlay& overlay,
		qint64 baseSize,
		const QByteArray& baseHash)
		: m_overlay(overlay), m_baseSize(baseSize), m_baseHash(baseHash) {
	}

	bool writeToBuffer(QByteArray& outPayload, QString* errorMessage) const;

private:
	void setupStream(QDataStream& s) const { s.setByteOrder(QDataStream::LittleEndian); }
	bool writeHeader(QDataStream& s, QString* errorMessage) const;
	bool writePieceTable(QDataStream& s, QString* errorMessage) const;
	bool writePageHints(QDataStream& s, QString* errorMessage) const;

	const PagedPieceOverlay& m_overlay;
	qint64 m_baseSize{0};
	QByteArray m_baseHash;
};
