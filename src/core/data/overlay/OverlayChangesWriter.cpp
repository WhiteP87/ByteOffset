#include "OverlayChangesWriter.h"
#include "PagedPieceOverlay.h"
#include <QBuffer>

using namespace OverlayChangesFormat;

bool OverlayChangesWriter::writeToBuffer(QByteArray& outPayload, QString* errorMessage) const {
	outPayload.clear();
	QBuffer buf(&outPayload);
	if(!buf.open(QIODevice::WriteOnly)) { if(errorMessage) *errorMessage = QStringLiteral("не удалось открыть буфер"); return false; }
	QDataStream s(&buf); setupStream(s);
	if(!writeHeader(s, errorMessage)) return false;
	if(!writePieceTable(s, errorMessage)) return false;
	if(!writePageHints(s, errorMessage)) return false;
	return s.status() == QDataStream::Ok;
}

bool OverlayChangesWriter::writeHeader(QDataStream& stream, QString* /*errorMessage*/) const {
	stream.writeRawData(MAGIC, 6);
	stream << VERSION;

	const quint8 hashAlg = m_baseHash.isEmpty() ? 0 : 1; //0=нет хэша, 1=sha256
	stream << hashAlg;
	stream.writeRawData("\0\0\0\0\0", 5);

	stream << quint64(m_baseSize);
	stream << quint16(m_baseHash.size());
	if(!m_baseHash.isEmpty())
		stream.writeRawData(m_baseHash.constData(), m_baseHash.size());

	const auto cfg = m_overlay.config();
	stream << qint64(m_overlay.logicalSize())
		<< qint64(cfg.m_logicalPageSize)
		<< double(cfg.m_fullRatio)
		<< QString{}; //baseId по желанию
	return stream.status() == QDataStream::Ok;
}

bool OverlayChangesWriter::writePieceTable(QDataStream& stream, QString* errorMessage) const {
	const auto pieces = m_overlay.enumeratePieces();
	stream << quint32(pieces.size());
	for(const auto& curPiece : pieces) {
		if(curPiece.isBase) {
			stream << quint8(PieceTag::Base);
			stream << curPiece.srcOffset << curPiece.length;
		} else {
			stream << quint8(PieceTag::Add);
			stream << qint64(curPiece.length);
			if(curPiece.length > 0) {
				if(stream.writeRawData(curPiece.insertBytes.constData(), curPiece.insertBytes.size()) != curPiece.insertBytes.size()) {
					if(errorMessage) 
						*errorMessage = QStringLiteral("ошибка записи add-данных");
					return false;
				}
			}
		}
	}
	return stream.status() == QDataStream::Ok;
}

bool OverlayChangesWriter::writePageHints(QDataStream& stream, QString* errorMessage) const {
	//FULL
	const auto fullPages = m_overlay.listFullPages();
	stream << quint32(fullPages.size());
	for(const auto& fp : fullPages) {
		stream << qint64(fp.pageIndex) << qint64(fp.pageBytes.size());
		if(fp.pageBytes.size() > 0) {
			if(stream.writeRawData(fp.pageBytes.constData(), fp.pageBytes.size()) != fp.pageBytes.size()) {
				if(errorMessage) 
					*errorMessage = QStringLiteral("ошибка записи full-страницы");
				return false;
			}
		}
	}
	//SPARSE
	const auto sparsePages = m_overlay.listSparsePages();
	stream << quint32(sparsePages.size());
	for(const auto& sp : sparsePages) {
		stream << qint64(sp.pageIndex) << quint32(sp.edits.size());
		for(const auto& e : sp.edits) {
			stream << quint32(e.inPageOffset) << quint8(e.newByte);
		}
	}
	return stream.status() == QDataStream::Ok;
}
