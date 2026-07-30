#pragma once
#include <QtCore>
#include "OverlayChangesFormat.h"

class PagedPieceOverlay;

class OverlayChangesReader {
public:
	explicit OverlayChangesReader(PagedPieceOverlay* overlayOrNull): m_overlay(overlayOrNull) {}

	bool applyFromBuffer(const QByteArray& payload, QString* errorMessage) const;

private:
	void setupStream(QDataStream& s) const { s.setByteOrder(QDataStream::LittleEndian); }
	bool readAndCheckMagic(QDataStream& s, QString* errorMessage) const;

	bool resetOverlay(QString* errorMessage) const;
	bool readPieceTable(QDataStream& s, QString* errorMessage) const;
	bool readPageHints(QDataStream& s, QString* errorMessage) const;
	bool finalize(QString* errorMessage) const;

	PagedPieceOverlay* m_overlay;
};
