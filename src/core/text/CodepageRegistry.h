#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include <QReadWriteLock>
#include <QFileSystemWatcher>
#include <QTimer>
#include <optional>
#include <unordered_map>

struct CodepageEntry {
	QString m_id;
	QString m_label;
	QString m_path;
};

class CodepageRegistry: public QObject {
	Q_OBJECT
public:
	explicit CodepageRegistry(const QString& encodingsDir = QStringLiteral("./encodings/"),
		QObject* parent = nullptr);
	~CodepageRegistry() override;

	//полная пересборка снимка каталога
	void refresh();

	//снимок записей для UI
	QVector<CodepageEntry> list() const;

	//быстрый поиск пути по id
	std::optional<QString> pathById(const QString& id) const;

	bool contains(const QString& id) const;

	//включить/выключить наблюдение за каталогом
	void enableWatching(bool enable);

	//получить текущий каталог кодировок
	QString encodingsDir() const;

private slots:
	void onDirectoryChanged(const QString& changedPath);
	void onDebounceTimeout();

private:
	//собирает и публикует снимок
	void scanDirectory(); 

	//чтение метаданных .sbc (id/label) без загрузки таблицы
	bool tryReadSbcMeta(const QString& filePath,CodepageEntry& outEntry,QString* errorText) const;

	//проверка корректности ID 
	static bool isValidCodecId(const QString& codecId);

	QString m_encodingsDir;

	mutable QReadWriteLock m_lock;
	QVector<CodepageEntry> m_entriesList;
	std::unordered_map<QString, CodepageEntry> m_entriesById;

	bool m_watching{false};
	QFileSystemWatcher* m_watcher{nullptr};
	QTimer* m_debounceTimer{nullptr};

	//константы валидации данных в файле таблицы
	static constexpr quint16 m_expectedHeaderSize = 24;
	static constexpr quint16 m_supportedVersion = 0x0002;
	static constexpr quint32 m_maxIdLen = 4096;
	static constexpr quint32 m_maxLabelLen = 4096;
	static constexpr qint32 m_debounceMs = 300;
};
