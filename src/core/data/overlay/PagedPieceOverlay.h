#pragma once
#include "IOverlay.h"
#include "ISrcReader.h"
#include "IPageStore.h"
#include "TempMMapPageStore.h"

#include <algorithm>
#include <QtCore>
#include <QMap>
#include <QVector>
#include <QHash>
#include <QBitArray>

class PagedPieceOverlay final: public IOverlay {
public:
	//типы публичного интерфейса
	struct Config {
		qint64 m_logicalPageSize{0}; //0 - системная гранулярность
		double m_fullRatio{-1.0};    //доля страницы для преобразования в FULL
	};

	struct PieceInfo { bool isBase{true}; qint64 srcOffset{0}; qint64 length{0}; QByteArray insertBytes; };
	struct FullPageInfo { qint64 pageIndex{0}; QByteArray pageBytes; };
	struct SparseEdit { quint32 inPageOffset{0}; quint8 newByte{0}; };
	struct SparsePageInfo { qint64 pageIndex{0}; QVector<SparseEdit> edits; };

	//конструктор/деструктор
	explicit PagedPieceOverlay(const ISrcReader& baseReader, const Config& cfg);
	~PagedPieceOverlay() override = default;

	//доступ к конфигурации
	const Config config() const { return Config{m_logicalPageSize, m_fullRatio}; }

	//api чтения
	qint64 logicalSize() const override;
	quint8 readByte(qint64 logicalOffset) const override;
	qint64 readRange(qint64 logicalOffset, qint64 length, QByteArray& out) const override;
	qint64 readRange(qint64 logicalOffset, qint64 length, char* destination) const override;

	//api записи
	bool overwriteByte(qint64 logicalOffset, quint8 value, quint8 mask = 0xFF) override;
	qint64 overwriteRange(qint64 logicalOffset, QByteArrayView data) override;
	qint64 insertRange(qint64 logicalOffset, QByteArrayView data) override;
	qint64 removeRange(qint64 logicalOffset, qint64 length) override;
	qint64 fillRange(qint64 logicalOffset, qint64 length, QByteArrayView pattern) override;

	//проверки модификаций
	bool hasAnyModification() const override;
	bool hasStructuralChanges() const override { return m_structuralChanges; }
	QVector<OverwriteSpan> enumerateOverwriteSpans() const override;
	bool isModified(qint64 logicalOffset) const override;

	//служебный вывод состояния (импорт/экспорт)
	QVector<PieceInfo> enumeratePieces() const;
	QVector<FullPageInfo> listFullPages() const;
	QVector<SparsePageInfo> listSparsePages() const;

	//импорт состояния
	bool beginImport();
	bool appendBaseSlice(qint64 srcOffset, qint64 length);
	bool appendAddSlice(const QByteArray& data);
	bool restoreFullPage(qint64 pageIndex, const QByteArray& pageBytes);
	bool restoreSparsePage(qint64 pageIndex, const QVector<QPair<quint32, quint8>>& edits);
	bool finalizeAfterImport();

private:
	//внутренние типы
	enum class PieceSrc: quint8 { Base, Add };

	struct Piece {
		PieceSrc m_src{PieceSrc::Base};
		qint64 m_srcOff{0};   //для Base — физический оффсет в исходнике, для Add — оффсет внутри handle
		qint64 m_len{0};
		PageHandle m_handle;  //валиден только для Add
	};

	//страница sparse-патчей (позиция внутри логической страницы → значение байта)
	struct SparsePage {
		QMap<int, quint8> m_edits;
	};

	//адресация страниц
	static qint64 systemGranularity();
	qint64 logicalPageIndex(qint64 logicalOffset) const { return logicalOffset / m_logicalPageSize; }
	int inLogicalPage(qint64 logicalOffset) const { return int(logicalOffset % m_logicalPageSize); }
	qint64 logicalPageBase(qint64 pageIndex) const { return pageIndex * m_logicalPageSize; }
	qint64 logicalPageLen(qint64 pageIndex) const {
		const qint64 base = logicalPageBase(pageIndex);
		const qint64 remain = m_logicalSize - base;
		return remain <= 0 ? 0 : std::min(remain, m_logicalPageSize);
	}

	//чтение/копирование
	int findPiece(qint64 logicalOffset, qint64* inPieceOffset = nullptr) const;
	qint64 readFromPiece(const Piece& piece, qint64 inPieceOffset, qint64 length, char* dst) const;
	void applyLogicalPatches(qint64 absOffset, qint64 length, char* dst, bool ignoreFull = false) const;
	void buildPageBuffer(qint64 pageIndex, QByteArray& out) const;
	qint64 copyFromPieces(qint64 logicalOffset, qint64 length, char* dst) const;

	//операции с piece-table
	int splitAt(qint64 logicalOffset);
	void insertPieceAt(qint64 logicalOffset, Piece newPiece);
	void mergeAround(qint32 aroundIndex);
	bool tryFoldInsertedAsBase(qint64 logicalOffset, qint64 length);
	void markPiecesChanged(int fromPieceIndex);

	//корректировка патчей при изменении геометрии
	void shiftPatches(qint64 fromLogical, qint64 delta);
	void erasePatches(qint64 logicalOffset, qint64 length);

	//управление FULL-страницами
	bool hasFullPage(qint64 pageIndex) const { return m_fullPages.contains(pageIndex); }
	void dropFullPages();
	bool updateFull(qint64 pageIndex);
	void checkNeedPromote(qint64 pageIndex);
	qsizetype calcPromoteSize(qint64 pageIndex) const;
	void buildFullDiffBits(qint64 pageIndex) const;
	void updateFullDiffBitsOnWrite(qint64 pageIndex, qint64 logicalStart, qint64 length);
	void checkDemoteIfClean(qint64 pageIndex);

	//кэш переходов по кускам (контрольные точки)
	struct Checkpoint {
		qint64 logicalPrefix{0}; //сумма длин всех кусков до pieceIndex
		int pieceIndex{0};       //индекс куска, с которого начинается эта контрольная точка
	};
	static constexpr qint32 CHECKPOINT_STEPS = 256; //каждые 256 кусков — контрольная точка
	void rebuildCheckpoints() const;

	//учёт overwrite-диапазонов
	void recordOverwrite(qint64 logicalOffset, qint64 length);
	static QVector<OverwriteSpan> mergeSpans(QVector<OverwriteSpan> spans);

	//друзья
	friend class OverlayChangesWriter;
	friend class OverlayChangesReader;

	Q_DISABLE_COPY_MOVE(PagedPieceOverlay)


	//зависимости и конфигурация
	const ISrcReader& m_base;                 //базовый источник (только чтение)
	std::unique_ptr<IPageStore> m_store;      //стор для Add/FULL и временных буферов
	const qint64 m_logicalPageSize;           //размер логической страницы
	const double m_fullRatio;                 //доля патчей страницы для преобразования в FULL, <0 — выкл

	//геометрия и куски
	QVector<Piece> m_pieces;                  //piece-table
	qint64 m_logicalSize{0};                  //логический размер

	//sparse/full страницы
	QHash<qint64, SparsePage> m_patchPages;   //логическая страница → sparse-патчи
	QHash<qint64, PageHandle> m_fullPages;    //логическая страница → materialized FULL

	//биты отличий для FULL-страниц
	mutable QHash<qint64, QBitArray> m_fullDiffBits; //страница → биты отличий
	mutable QHash<qint64, int> m_fullDiffCount;      //страница → число отличающихся байтов
	mutable QMutex m_fullDiffMutex;                  //защита m_fullDiffBits/m_fullDiffCount

	//контрольные точки и кэш индексов
	mutable QVector<Checkpoint> m_checkpoints;
	mutable bool m_checkpointsDirty{true};
	mutable int m_cachedPieceIndex{-1};
	mutable qint64 m_cachedPieceLogicalBase{0};
	mutable QMutex m_indexMutex;                   //защита m_checkpointsDirty/m_checkpoints/m_cachedPiece*

	//флаги и агрегаты модификаций
	bool m_structuralChanges{false};
	QVector<OverwriteSpan> m_overwriteSpans;
};
