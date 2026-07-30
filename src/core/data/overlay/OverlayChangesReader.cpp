#include "OverlayChangesReader.h"
#include "PagedPieceOverlay.h"
#include <QBuffer>

using namespace OverlayChangesFormat;


bool OverlayChangesReader::applyFromBuffer(const QByteArray& payload, QString* errorMessage) const {
	if(!m_overlay) { if(errorMessage) *errorMessage = QStringLiteral("overlay не задан"); return false; }
	QBuffer buf(const_cast<QByteArray*>(&payload));
	if(!buf.open(QIODevice::ReadOnly)) { if(errorMessage) *errorMessage = QStringLiteral("не удалось открыть буфер"); return false; }
	QDataStream s(&buf); setupStream(s);
	if(!readAndCheckMagic(s, errorMessage)) return false;

	quint8 hashAlg = 0; s >> hashAlg; char pad[5]; s.readRawData(pad, 5);
	quint64 baseSize = 0; s >> baseSize;
	quint16 hashLen = 0; s >> hashLen;
	if(hashLen > 0) { QByteArray skip; skip.resize(int(hashLen)); s.readRawData(skip.data(), skip.size()); }
	qint64 logicalSize = 0; s >> logicalSize;
	qint64 pageSize = 0; double fullRatio = -1.0; QString baseId;
	s >> pageSize >> fullRatio >> baseId;

	if(!resetOverlay(errorMessage)) return false;
	if(!readPieceTable(s, errorMessage)) return false;
	if(!readPageHints(s, errorMessage)) return false;
	return finalize(errorMessage);
}


bool OverlayChangesReader::readAndCheckMagic(QDataStream& stream, QString* errorMessage) const {
	char magic[6];
	if(stream.readRawData(magic, 6) != 6 || ::memcmp(magic, MAGIC, 6) != 0) {
		if(errorMessage) 
			*errorMessage = QStringLiteral("неверная сигнатура файла правок");
		return false;
	}

	quint16 version = 0; 
	stream >> version;
	if(version != VERSION) {
		if(errorMessage) 
			*errorMessage = QStringLiteral("неподдерживаемая версия файла правок");
		return false;
	}
	return true;
}

bool OverlayChangesReader::resetOverlay(QString* /*errorMessage*/) const {
	return m_overlay->beginImport(); 
}

bool OverlayChangesReader::readPieceTable(QDataStream& stream, QString* errorMessage) const {
	quint32 count = 0; 
	stream >> count;
	for(quint32 i = 0; i < count; ++i) {
		quint8 tag = 0; 
		stream >> tag;
		if(tag == quint8(PieceTag::Base)) {

			qint64 off = 0, len = 0; 
			stream >> off >> len;

			if(!m_overlay->appendBaseSlice(off, len)) {
				if(errorMessage)
					*errorMessage = QStringLiteral("ошибка base-куска");
				return false; 
			}
		} else if(tag == quint8(PieceTag::Add)) {
			qint64 len = 0; 
			stream >> len; 
			QByteArray data; 
			data.resize(int(len));

			if(len > 0) 
				stream.readRawData(data.data(), data.size());

			if(!m_overlay->appendAddSlice(data)) {
				if(errorMessage) 
					*errorMessage = QStringLiteral("ошибка add-куска");
				return false;
			}
		} else { 
			if(errorMessage) 
				*errorMessage = QStringLiteral("неизвестный тег куска"); 
			return false; 
		}
	}
	return stream.status() == QDataStream::Ok;
}

bool OverlayChangesReader::readPageHints(QDataStream& stream, QString* errorMessage) const {
	//FULL
	quint32 fullCount = 0; stream >> fullCount;
	for(quint32 i = 0; i < fullCount; ++i) {
		qint64 pageIndex = 0, pageLen = 0; 
		stream >> pageIndex >> pageLen;

		QByteArray bytes; 
		bytes.resize(int(pageLen));
		if(pageLen > 0) 
			stream.readRawData(bytes.data(), bytes.size());

		if(!m_overlay->restoreFullPage(pageIndex, bytes)) { 
			if(errorMessage)
				*errorMessage = QStringLiteral("ошибка восстановления full-страницы");
			return false;
		}
	}
	//SPARSE
	quint32 sparseCount = 0; 
	stream >> sparseCount;
	for(quint32 i = 0; i < sparseCount; ++i) {

		qint64 pageIndex = 0; 
		stream >> pageIndex;

		quint32 edits = 0; 
		stream >> edits;

		QVector<QPair<quint32, quint8>> pairs;
		pairs.reserve(edits);
		for(quint32 e = 0; e < edits; ++e) { 
			quint32 off = 0;
			quint8 val = 0; 
			stream >> off >> val;
			pairs.push_back({off,val}); 
		}
		if(!m_overlay->restoreSparsePage(pageIndex, pairs)) {
			if(errorMessage) 
				*errorMessage = QStringLiteral("ошибка восстановления sparse-страницы");
			return false; 
		}
	}
	return stream.status() == QDataStream::Ok;
}

bool OverlayChangesReader::finalize(QString* /*errorMessage*/) const {
	return m_overlay->finalizeAfterImport(); 
}
