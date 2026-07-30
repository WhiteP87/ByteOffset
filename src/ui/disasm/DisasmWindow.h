#pragma once
#include <QWidget>
#include "ui_DisasmWindow.h"
#include "DisasmEngine.h"
#include "DisasmDelegate.h"

QT_BEGIN_NAMESPACE
namespace Ui { class DisasmWindow; };
QT_END_NAMESPACE

class DisasmWindow : public QWidget
{
	Q_OBJECT

public:
	DisasmWindow(QWidget *parent = nullptr);
	~DisasmWindow();
	void setRegion(qint64 offset, qint64 len) { m_startOffset = offset; m_len = len; };
	void applyTableStyle(const QFont& editorFont,
		const QColor& backgroundColor,
		const QColor& textColor,
		const QColor& selectionBg,
		const QColor& selectionText,
		const QColor& addressTextColor);
public slots:
	void onChunkReady(std::vector<DisasmInstr> instructions, int percent);
	void onDisasmError(const QString& message);
	void onDisasmFinished();
	void onRefreshButton();
	void onTableDblClicked(const QModelIndex& index);
	void onRowClicked(const QModelIndex& current, const QModelIndex&);
	void onDataChanged(QObject* sender, qint64 off, qint64 len);
	void onCaretChanged(qint64 offset);
private:
	Ui::DisasmWindow *ui;
	bool m_restorePending{false};
	qint64 m_restoreAddress{0};
	bool restoreSelectionIfReady();
	qint64 m_startOffset{};
	qint64 m_len{};
	DisasmDelegate* m_syntaxDelegate{nullptr};
signals:
	void refreshRequested();
	void gotoAddressRequested(qint64 address, qint32 len);
};

