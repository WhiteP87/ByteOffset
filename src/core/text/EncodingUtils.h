#pragma once
#include "TranslatorPolicies.h"
#include <QtGlobal>
#include <QString>
#include <QChar>
#include <QByteArray>
#include <memory>


inline bool isUnicodePrintable(const QChar& ch) {
	return ch.isPrint();
}

inline qint64 availableBytes(const std::shared_ptr<IDataProvider>& provider, qint64 from) {
	if(!provider) 
		return 0;

	const qint64 totalSize = provider->size();
	if(from < 0 || from >= totalSize) 
		return 0;

	return totalSize - from;
}

inline int writeBytes(const std::shared_ptr<IDataProvider>& provider,
	qint64 offset, const QByteArray& byteBuffer, OverflowMode overflowMode) {

	if(!provider || byteBuffer.isEmpty()) 
		return 0;

	const qint64 totalSize = provider->size();
	if(offset < 0 || offset >= totalSize) 
		return 0;

	const qint64 remaining = totalSize - offset;
	qint64 bytesToWrite = static_cast<qint64>(byteBuffer.size());

	if(bytesToWrite > remaining) {
		if(overflowMode == OverflowMode::reject)
			return 0; // атомарный отказ
		bytesToWrite = remaining; // truncate
	}

	for(qint64 i = 0; i < bytesToWrite; ++i)
		provider->writeByte(offset + i, static_cast<quint8>(byteBuffer.at(i)));

	return bytesToWrite;
}
