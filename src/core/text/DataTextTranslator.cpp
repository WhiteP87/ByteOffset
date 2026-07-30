#include "DataTextTranslator.h"
#include "TextCache.h"
#include "IDataProvider.h"
#include "EncodingUtils.h"

DataTextTranslator::DataTextTranslator() = default;

DataTextTranslator::DataTextTranslator(std::shared_ptr<IDataProvider> provider)
	: m_provider(std::move(provider))
	, m_cache(std::make_unique<TextCache>(6000)) //дефолтный размер кэша
{
}

DataTextTranslator::~DataTextTranslator() = default;

void DataTextTranslator::setDataProvider(std::shared_ptr<IDataProvider> provider) {
	m_provider = std::move(provider);
	clearCache();
}

void DataTextTranslator::setCacheSize(qint32 bytes) {
	if(m_cache)
		m_cache->setCacheBlockSize(bytes);
}

void DataTextTranslator::clearCache() {
	if(m_cache)
		m_cache->clearCache();
}

void DataTextTranslator::invalidateCache(qint64 startOffset, qint64 len) {
	if(!m_cache || !m_provider) return;
	m_cache->invalidateCache(startOffset, len, this, m_provider->size());
}

void DataTextTranslator::setPolicies(const TranslatorPolicies& policies) {
	m_policies = policies;
	clearCache();
}

const TranslatorPolicies& DataTextTranslator::policies() const {
	return m_policies;
}

QString DataTextTranslator::readAt(qint64 byteOffset) {
	if(!m_cache || !m_provider)
		return m_policies.m_nonPrintablePlaceholder;

	if(byteOffset < m_cache->start() || byteOffset >= m_cache->end())
		m_cache->updateCache(byteOffset, m_provider->size(), this);

	const qint32 index = static_cast<int>(byteOffset - m_cache->start());
	if(index >= 0 && index < m_cache->cacheSize())
		return m_cache->cacheArray()[index];

	return m_policies.m_nonPrintablePlaceholder;
}

QPair<QString, qint32> DataTextTranslator::decodeAt(qint64 byteOffset) {
	if(!m_provider || byteOffset < 0 || byteOffset >= m_provider->size())
		return {QString(), 1};

	const qint64 avail = m_provider->size() - byteOffset;
	const int readLen = static_cast<int>(qMin(avail, qint64(8)));
	char buf[8] = {};
	m_provider->readRange(byteOffset, readLen, buf);
	return decodeBytes(QByteArrayView(buf, readLen), 0);
}

int DataTextTranslator::encodeAndWriteAt(qint64 byteOffset, const QString& oneChar) {
	if(!m_provider || oneChar.isEmpty())
		return 0;

	const QByteArray bytes = encodeToBytes(oneChar);
	if(bytes.isEmpty())
		return 0;

	const int written = writeBytes(m_provider, byteOffset, bytes, m_policies.m_overflowMode);
	if(written > 0)
		invalidateCache(byteOffset, written);
	return written;
}
