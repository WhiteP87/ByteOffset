#include "TranslatorFactory.h"
#include "TranslatorsImpl.h"

#include "CodepageRegistry.h"
#include "SbcLoader.h"

#include <QDebug>

namespace {
	inline bool isBuiltinId(const QString& idLower) {
		return idLower == QStringLiteral("utf8")
			|| idLower == QStringLiteral("utf16le")
			|| idLower == QStringLiteral("utf16be")
			|| idLower == QStringLiteral("ascii");
	}
}

TranslatorFactory::TranslatorFactory(CodepageRegistry& registry)
	: m_registry(registry) {
	//дефолтные политики - ".", overflow=truncate, unrepresentable=reject
}

TranslatorFactory::~TranslatorFactory() = default;

void TranslatorFactory::setDefaultPolicies(const TranslatorPolicies& policies) {
	m_defaultPolicies = policies;
}

std::shared_ptr<DataTextTranslator> TranslatorFactory::create(
	const QString& codecId,
	std::shared_ptr<IDataProvider> dataProvider) {
	if(!dataProvider) {
		qWarning().noquote() << "[TranslatorFactory] create: null dataProvider";
		return nullptr;
	}

	const QString idLower = codecId.toLower();

	std::shared_ptr<DataTextTranslator> translator;

	//встроенные кодеки
	if(idLower == QStringLiteral("utf8")) {
		translator = std::make_shared<Utf8Translator>(dataProvider);
	} else if(idLower == QStringLiteral("utf16le")) {
		translator = std::make_shared<Utf16LETranslator>(dataProvider);
	} else if(idLower == QStringLiteral("utf16be")) {
		translator = std::make_shared<Utf16BETranslator>(dataProvider);
	} else if(idLower == QStringLiteral("ascii")) {
		translator = std::make_shared<AsciiTranslator>(dataProvider);
	} else {
		//SBC через реестр
		auto tablePtr = getOrLoadSingleByteTable(idLower);
		if(!tablePtr) {
			qWarning().noquote() << "[TranslatorFactory] create: cannot load SBC for id" << codecId;
			return nullptr;
		}
		translator = std::make_shared<SingleByteCodepageTranslator>(dataProvider, tablePtr);
	}

	//применяем дефолтные политики
	if(translator) {
		translator->setPolicies(m_defaultPolicies);
	}

	return translator;
}

std::shared_ptr<ITextCodec> TranslatorFactory::createCodec(const QString& codecId) {
	const QString idLower = codecId.toLower();

	std::shared_ptr<DataTextTranslator> codec;

	if(idLower == QStringLiteral("utf8")) {
		codec = std::make_shared<Utf8Translator>();
	} else if(idLower == QStringLiteral("utf16le")) {
		codec = std::make_shared<Utf16LETranslator>();
	} else if(idLower == QStringLiteral("utf16be")) {
		codec = std::make_shared<Utf16BETranslator>();
	} else if(idLower == QStringLiteral("ascii")) {
		codec = std::make_shared<AsciiTranslator>();
	} else {
		auto tablePtr = getOrLoadSingleByteTable(idLower);
		if(!tablePtr) {
			qWarning().noquote() << "[TranslatorFactory] createCodec: cannot load SBC for id" << codecId;
			return nullptr;
		}
		codec = std::make_shared<SingleByteCodepageTranslator>(tablePtr);
	}

	if(codec)
		codec->setPolicies(m_defaultPolicies);

	return codec;
}

std::shared_ptr<const SingleByteTable> TranslatorFactory::getOrLoadSingleByteTable(
	const QString& codecId) {
	//проверяем кеш
		{
			QMutexLocker cacheLock(&m_cacheMutex);
			auto found = m_tablesCache.find(codecId);
			if(found != m_tablesCache.end()) {
				std::shared_ptr<const SingleByteTable> locked = found->second.lock();
				if(locked) {
					return locked;
				}
			}
		}

		//узнаём путь из реестра
		const auto pathOpt = m_registry.pathById(codecId);
		if(!pathOpt.has_value()) {
			qWarning().noquote() << "[TranslatorFactory] getOrLoadSingleByteTable: no path for id" << codecId;
			return nullptr;
		}
		const QString tableFilePath = pathOpt.value();

		//загружаем таблицу с диска
		QString errorText;
		std::shared_ptr<const SingleByteTable> loadedTable =
			loadSingleByteTableFromFile(tableFilePath, &errorText);

		if(!loadedTable) {
			qWarning().noquote() << "[TranslatorFactory] SBC load failed:" << tableFilePath << "-" << errorText;
			return nullptr;
		}

		//кладём в кеш (id из таблицы должен совпадать с codecId)
		{
			QMutexLocker cacheLock(&m_cacheMutex);
			m_tablesCache[loadedTable->m_id] = loadedTable;
		}
		return loadedTable;
}

std::shared_ptr<const SingleByteTable> TranslatorFactory::loadSingleByteTableFromFile(
	const QString& filePath,QString* errorText) {

	return SbcLoader::loadSingleByteTable(filePath, errorText);
}
