#pragma once
#include "IDataProvider.h"
#include "ISrcReader.h"
#include "IOverlay.h"
#include "MMapSrcReader.h"
#include "PagedPieceOverlay.h"
#include <QtCore>
#include <QReadWriteLock>
#include <QMutex>
#include <QHash>
#include <atomic>
#include <memory>

class MMapDataProvider final: public IDataProvider {
public:
	explicit MMapDataProvider(std::unique_ptr<ISrcReader> srcReader,
		std::unique_ptr<IOverlay>   overlay);
	~MMapDataProvider() override;

	//чтение
	qint64 size() const override;
	quint8 readByte(qint64 off) const override;
	qint64 readRange(qint64 off, qint64 len, QByteArray& out) const override;
	qint64 readRange(qint64 off, qint64 len, char* destination) const override;
	qint64 readRange(qint64 off, qint64 len, char* destination, quint64* outVersion) const override;

	//запись
	bool writeByte(qint64 off, quint8 value, quint8 mask = 0xFF) override;
	bool writeByte(qint64 off, quint8 value, int nibbleIndex) override;
	qint64 writeRange(qint64 off, QByteArrayView data) override;
	qint64 insert(qint64 off, QByteArrayView data) override;
	qint64 remove(qint64 off, qint64 len) override;
	qint64 fillRange(qint64 off, qint64 len, QByteArrayView pattern) override;

	//модифицированность
	bool isModified(qint64 offset) const override;
	bool isDataModified() const override;
	quint64 dataVersion() const override;

	//подписки
	DataChangeToken subscribeDataChanged(DataChangedCallback callback) override;
	void unsubscribeDataChanged(DataChangeToken token) override;
	DataChangeToken subscribeGeometryChanged(GeometryChangedCallback cb) override;
	void unsubscribeGeometryChanged(DataChangeToken token)    override;

	//сохранение
	SaveStatus saveToFile(const QString& filePath, QString* errorMessage = nullptr) override;

	std::optional<SourceMeta> sourceMeta() const override;

	//для отладки
	const ISrcReader* srcReader() const noexcept { return m_srcReader.get(); }
	const IOverlay* overlay()   const noexcept { return m_overlay.get(); }

	//IBinaryConfig реализация
	BinaryConfigDescriptor configDescriptor() const override;
	bool exportBinaryConfig(QByteArray& outPayload, QString* errorText) const override;
	bool importBinaryConfig(const QByteArray& payload, QString* errorText) override;
	bool importFromProjectFile(const QString& filePath, QString* errorText) override; //по пути к .hecx
	const QString id() const override { return m_srcReader->id(); }
private:
	void notifyDataChanged(const QVector<ChangedSpan>& spans, quint64 newVersion, bool modifiedWhole);

	mutable QReadWriteLock m_rwLock{QReadWriteLock::Recursive};

	QMutex m_subscribersMutex;
	QHash<DataChangeToken, DataChangedCallback> m_subscribers;
	std::atomic<DataChangeToken> m_nextToken{0};

	//подписчики на геометрию
	QReadWriteLock m_geomCbLock;
	QHash<DataChangeToken, GeometryChangedCallback> m_geomSubs;
	DataChangeToken m_nextGeomToken{1000}; // отдельный счётчик

	void notifyGeometryChanged(const QVector<GeometryEvent>& ev, quint64 ver);

	std::atomic<quint64> m_version{0};

	std::unique_ptr<ISrcReader> m_srcReader;
	std::unique_ptr<IOverlay>   m_overlay;

	void reopenFile(const QString& path, const PagedPieceOverlay::Config& cfg, qint64 mapWindowSize);
	SaveStatus saveInPlaceSameTarget(QString* errorMessage);
	SaveStatus saveByReplaceSameTarget(QString* errorMessage);
	SaveStatus saveAsToPath(const QString& dstPath, QString* errorMessage);

	Q_DISABLE_COPY_MOVE(MMapDataProvider)
};
