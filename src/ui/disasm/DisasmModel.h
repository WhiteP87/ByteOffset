#pragma once
#include <QAbstractTableModel>
#include <vector>
#include "DisasmEngine.h"

class DisasmModel final: public QAbstractTableModel {
	Q_OBJECT
public:
	enum Col { Address, Bytes, Mnemonic, Operands, Count };
	enum Roles { AddressRole = Qt::UserRole + 1, SizeRole};
	explicit DisasmModel(QObject* p = nullptr);
	qint32 rowCount(const QModelIndex&) const override;
	qint32 columnCount(const QModelIndex&) const override;
	QVariant data(const QModelIndex&, int role) const override;
	QVariant headerData(int, Qt::Orientation, int) const override;
	void setRows(std::vector<DisasmInstr> rows);
	void appendRows(std::vector<DisasmInstr> more);
	qint32 findRowContaining(qint64 address) const;

private:
	std::vector<DisasmInstr> rows_;
};
