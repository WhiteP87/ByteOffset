#pragma once

#include <QWidget>
#include "ui_CodepageTool.h"
#include <QTableWidget>
#include <array>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <QRegularExpression>

//4 байта на кодовую точку
struct SingleByteTable {
	QString id;
	QString label;
	std::array<quint32, 256> codepointMap{};

	void clear() {
		id.clear();
		label.clear();
		codepointMap.fill(0);
	}
};

// заголовок бинарного файла кодировки
#pragma pack(push,1)
struct SbcHeaderLite {
	char    magic[4];     // сигнатура "SBC1"
	quint16 headerSize;   // 24
	quint16 version;      // версия  
	quint32 idLen;        // длина id в байтах (UTF-8), без NUL
	quint32 labelLen;     // длина label в байтах (UTF-8), без NUL
	quint32 fileSize;     // общий размер файла (включая заголовок)
	quint32 reserved;     // 0
};
#pragma pack(pop)

//валидация идентификатора кодировки
static inline bool isValidCodecId(const QString& codecId) {
	static const QRegularExpression re(QStringLiteral("^[a-z0-9._-]+$"));
	return re.match(codecId).hasMatch();
}

QT_BEGIN_NAMESPACE
namespace Ui { class CodepageTool; };
QT_END_NAMESPACE

class CodepageTool: public QWidget {
	Q_OBJECT

public:
	CodepageTool(QWidget* parent = nullptr);
	~CodepageTool();
private slots:
	void onLoadJson();
	void onSaveJson();
	void onLoadSbc();
	void onSaveSbc();
	void onAutoAscii();
	void onClearTable();
	void onCellEdited(QTableWidgetItem* item);

private:
	QString formatCellValue(char32_t codepoint) const;
	void configureTable();
	void createMainMenu();
	Ui::CodepageTool* ui;

	bool collectTable(SingleByteTable& outTable, QString* errorText) const;
	void applyTable(const SingleByteTable& tableModel);

	static bool parseCellValue(const QString& text, char32_t& outCodepoint);

	bool loadJsonFile(const QString& filePath, SingleByteTable& outTable, QString* errorText);
	bool saveJsonFile(const QString& filePath, const SingleByteTable& tableModel, QString* errorText) const;

	bool saveSbcFile(const QString& filePath, const SingleByteTable& tableModel, QString* errorText) const;
	bool loadSbcFile(const QString& filePath, SingleByteTable& outTable, QString* errorText);

	bool m_isNormalizing = false;
};
