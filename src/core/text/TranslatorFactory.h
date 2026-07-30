#pragma once
#include <memory>
#include <optional>
#include <unordered_map>
#include <QString>
#include <QMutex>

#include "DataTextTranslator.h"

class IDataProvider;
class ITextCodec;
class CodepageRegistry;
struct SingleByteTable;

class TranslatorFactory {
public:
	explicit TranslatorFactory(CodepageRegistry& registry);
	~TranslatorFactory();

	//создать кодек по id. Для встроенных id — без файлов,
	//для остальных — загрузка .sbc через реестр.
	//возвращает nullptr при ошибке (неизвестный id, битый .sbc).
	std::shared_ptr<DataTextTranslator> create( const QString& codecId, std::shared_ptr<IDataProvider> dataProvider);

	//создать лёгкий кодек без провайдера/кэша (только decodeBytes/encodeToBytes).
	//для использования в диалогах поиска и т.п.
	std::shared_ptr<ITextCodec> createCodec(const QString& codecId);

	//глобальные (дефолтные) политики
	void setDefaultPolicies(const TranslatorPolicies& policies);

private:
	//попытка получить таблицу из кеша или загрузить её с диска (через реестр).
	std::shared_ptr<const SingleByteTable> getOrLoadSingleByteTable(const QString& codecId);

	//загрузка таблицы по полному пути
	static std::shared_ptr<const SingleByteTable> loadSingleByteTableFromFile(
		const QString& filePath,QString* errorText);

	CodepageRegistry& m_registry;
	TranslatorPolicies m_defaultPolicies;

	//кеш загруженных таблиц (id в weak_ptr<Table>)
	QMutex m_cacheMutex;
	std::unordered_map<QString, std::weak_ptr<const SingleByteTable>> m_tablesCache;
};
