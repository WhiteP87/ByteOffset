#include "CodepageRegistry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDataStream>
#include <QRegularExpression>
#include <QtEndian>
#include <QDebug>

namespace {
	//логирование 
	void logParseWarning(const QString& filePath, const QString& reason) {
		qWarning().noquote() << "[CodepageRegistry] skip" << filePath << "-" << reason;
	}

	bool pathIsDirExisting(const QString& dirPath) {
		QDir dir(dirPath);
		return dir.exists();
	}

	bool ensureWatcherHasPath(QFileSystemWatcher* watcher, const QString& dirPath) {
		//addPath() возвращает bool; при false — каталог не будет отслеживаться
		return watcher && watcher->addPath(dirPath);
	}
}

// ---------------------- Public API ----------------------

CodepageRegistry::CodepageRegistry(const QString& encodingsDir, QObject* parent)
	: QObject(parent)
	, m_encodingsDir(encodingsDir)
	, m_watcher(new QFileSystemWatcher(this))
	, m_debounceTimer(new QTimer(this)) {

	//таймер для серии событий изменения каталога
	m_debounceTimer->setSingleShot(true);
	m_debounceTimer->setInterval(m_debounceMs);
	connect(m_debounceTimer, &QTimer::timeout, this, &CodepageRegistry::onDebounceTimeout);

	//по умолчанию watcher не включаем (вкл через enableWatching(true))
	connect(m_watcher, &QFileSystemWatcher::directoryChanged,
		this, &CodepageRegistry::onDirectoryChanged);

	// Начальная загрузка снимка
	refresh();
}

CodepageRegistry::~CodepageRegistry() {}

void CodepageRegistry::refresh() {
	//сканируем директорию
	scanDirectory();
}

QVector<CodepageEntry> CodepageRegistry::list() const {
	QReadLocker readLock(&m_lock);
	return m_entriesList;
}

std::optional<QString> CodepageRegistry::pathById(const QString& id) const {
	QReadLocker readLock(&m_lock);
	const auto it = m_entriesById.find(id);
	if(it == m_entriesById.end())
		return std::nullopt;
	return it->second.m_path;
}

bool CodepageRegistry::contains(const QString& id) const {
	QReadLocker readLock(&m_lock);
	return m_entriesById.find(id) != m_entriesById.end();
}

void CodepageRegistry::enableWatching(bool enable) {
	if(enable == m_watching)
		return;

	if(enable) {
		if(!pathIsDirExisting(m_encodingsDir)) {
			qWarning().noquote() << "[CodepageRegistry] enableWatching: directory does not exist:"
				<< m_encodingsDir;
			m_watching = false;
			return;
		}
		if(!ensureWatcherHasPath(m_watcher, m_encodingsDir)) {
			qWarning().noquote() << "[CodepageRegistry] enableWatching: failed to watch"
				<< m_encodingsDir;
			m_watching = false;
			return;
		}
		m_watching = true;
		//синхронизируем снимок
		refresh();
		qInfo().noquote() << "[CodepageRegistry] watching" << m_encodingsDir;
	} else {
		// Отключаем
		if(m_watcher) {
			m_watcher->removePath(m_encodingsDir);
		}
		m_watching = false;
		qInfo().noquote() << "[CodepageRegistry] watching disabled";
	}
}

QString CodepageRegistry::encodingsDir() const {
	QReadLocker readLock(&m_lock);
	return m_encodingsDir;
}

// ---------------------- Slots ----------------------

void CodepageRegistry::onDirectoryChanged(const QString& changedPath) {
	if(changedPath != m_encodingsDir)
		return;
	//пауза серии событий
	m_debounceTimer->start(m_debounceMs);
}

void CodepageRegistry::onDebounceTimeout() {
	refresh();
}

// ---------------------- Private helpers ----------------------

void CodepageRegistry::scanDirectory() {
	//формируем временные структуры
	QVector<CodepageEntry> tempEntriesList;
	std::unordered_map<QString, CodepageEntry> tempEntriesById;

	QDir dir(m_encodingsDir);
	if(!dir.exists()) {
		if(!QDir().mkpath(m_encodingsDir)) {
			qWarning() << "Не удалось создать директорию:" << m_encodingsDir;
			return;
		}
		//пустой снимок
		QWriteLocker writeLock(&m_lock);
		m_entriesList.swap(tempEntriesList);
		m_entriesById.swap(tempEntriesById);
		return;
	}

	dir.setFilter(QDir::Files | QDir::NoSymLinks | QDir::Readable);
	dir.setNameFilters({QStringLiteral("*.sbc")});
	dir.setSorting(QDir::Name | QDir::IgnoreCase);

	const QFileInfoList fileInfoList = dir.entryInfoList();
	for(int fileIndex = 0; fileIndex < fileInfoList.size(); ++fileIndex) {
		const QFileInfo& fileInfo = fileInfoList.at(fileIndex);
		const QString filePath = fileInfo.absoluteFilePath();

		CodepageEntry entry;
		QString errorText;
		if(!tryReadSbcMeta(filePath, entry, &errorText)) {
			logParseWarning(filePath, errorText);
			continue;
		}

		//защита от дубликатов id. первый попавшийся имеет приоритет
		if(tempEntriesById.find(entry.m_id) != tempEntriesById.end()) {
			logParseWarning(filePath, QStringLiteral("duplicate id '%1'").arg(entry.m_id));
			continue;
		}

		tempEntriesById.emplace(entry.m_id, entry);
		tempEntriesList.push_back(entry);
	}

	//публикуем снимок
	{
		QWriteLocker writeLock(&m_lock);
		m_entriesList.swap(tempEntriesList);
		m_entriesById.swap(tempEntriesById);
	}
}

bool CodepageRegistry::tryReadSbcMeta(const QString& filePath,
	CodepageEntry& outEntry,
	QString* errorText) const {
	QFile file(filePath);
	if(!file.open(QIODevice::ReadOnly)) {
		if(errorText)
			*errorText = QStringLiteral("failed to open: %1").arg(file.errorString());
		return false;
	}

	QDataStream dataStream(&file);
	dataStream.setByteOrder(QDataStream::LittleEndian);

	// header
	char magicChars[4] = {};
	if(dataStream.readRawData(magicChars, 4) != 4) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (magic)");
		return false;
	}
	if(QByteArray(magicChars, 4) != QByteArrayLiteral("SBC1")) {
		if(errorText) 
			*errorText = QStringLiteral("bad magic");
		return false;
	}

	quint16 headerSize = 0;
	quint16 version = 0;
	quint32 idLen = 0;
	quint32 labelLen = 0;
	quint32 fileSizeFromHeader = 0;
	quint32 reserved = 0;

	dataStream >> headerSize;
	dataStream >> version;
	dataStream >> idLen;
	dataStream >> labelLen;
	dataStream >> fileSizeFromHeader;
	dataStream >> reserved;

	if(dataStream.status() != QDataStream::Ok) {
		if(errorText) 
			*errorText = QStringLiteral("corrupted header");
		return false;
	}

	const qint64 realFileSize = file.size();
	if(headerSize < m_expectedHeaderSize) {
		if(errorText) 
			*errorText = QStringLiteral("unsupported header size %1").arg(headerSize);
		return false;
	}
	if(version != m_supportedVersion) {
		if(errorText)
			*errorText = QStringLiteral("unsupported version %1").arg(version);
		return false;
	}
	if(idLen > m_maxIdLen || labelLen > m_maxLabelLen) {
		if(errorText)
			*errorText = QStringLiteral("id/label too long (%1/%2)").arg(idLen).arg(labelLen);
		return false;
	}

	//минимально ожидаемый размер header + table(256*4) + id + label
	const quint64 tableSize = 256u * 4u;
	const quint64 minimalExpectedSize =
		static_cast<quint64>(headerSize) + tableSize
		+ static_cast<quint64>(idLen)
		+ static_cast<quint64>(labelLen);
	if(realFileSize < static_cast<qint64>(minimalExpectedSize)) {
		if(errorText) 
			*errorText = QStringLiteral("file too small");
		return false;
	}
	if(fileSizeFromHeader != 0 &&
		fileSizeFromHeader != static_cast<quint32>(realFileSize)) {
		if(errorText) 
			*errorText = QStringLiteral("file size mismatch (%1 != %2)").arg(fileSizeFromHeader).arg(realFileSize);
		return false;
	}

	//перепрыгиваем расширение заголовка и таблицу
	constexpr int headerFixedPart = 24;
	const int headerPadding = static_cast<int>(headerSize) - headerFixedPart;
	if(headerPadding > 0) {
		if(dataStream.skipRawData(headerPadding) != headerPadding) {
			if(errorText) 
				*errorText = QStringLiteral("unexpected EOF (header padding)");
			return false;
		}
	}
	if(dataStream.skipRawData(static_cast<int>(tableSize)) != static_cast<int>(tableSize)) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (table)");
		return false;
	}

	//читаем id и label
	QByteArray idBytes;    
	idBytes.resize(static_cast<int>(idLen));

	QByteArray labelBytes;
	labelBytes.resize(static_cast<int>(labelLen));

	if(dataStream.readRawData(idBytes.data(), idBytes.size()) != idBytes.size()) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (id)");
		return false;
	}
	if(dataStream.readRawData(labelBytes.data(), labelBytes.size()) != labelBytes.size()) {
		if(errorText) 
			*errorText = QStringLiteral("unexpected EOF (label)");
		return false;
	}

	const QString idString = QString::fromUtf8(idBytes);
	const QString labelString = QString::fromUtf8(labelBytes);
	if(!isValidCodecId(idString)) {
		if(errorText) 
			*errorText = QStringLiteral("invalid id '%1'").arg(idString);
		return false;
	}

	outEntry.m_id = idString;
	outEntry.m_label = labelString;
	outEntry.m_path = file.fileName();
	return true;
}

bool CodepageRegistry::isValidCodecId(const QString& codecId) {
	static const QRegularExpression idRegex(QStringLiteral("^[a-z0-9._-]+$"));
	return idRegex.match(codecId).hasMatch();
}
