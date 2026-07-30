#include "DisasmModel.h"
#include <QColor>
#include <QString>
#include <algorithm>

DisasmModel::DisasmModel(QObject* p):QAbstractTableModel(p) {
}

qint32 DisasmModel::rowCount(const QModelIndex&) const { 
	return int(rows_.size()); 
}

qint32 DisasmModel::columnCount(const QModelIndex&) const {
	return Col::Count; 
}

QVariant DisasmModel::data(const QModelIndex& idx, int role) const {
	if(!idx.isValid()) return {};
	const auto& r = rows_[size_t(idx.row())];
	if(role == AddressRole) {
		return QVariant::fromValue<qulonglong>(static_cast<qulonglong>(r.address));
	}
	if(role == SizeRole) {
		return static_cast<qulonglong>(r.size);
	}
	if(role == Qt::TextAlignmentRole) {
		if(idx.column() == Mnemonic) {
			return int(Qt::AlignRight | Qt::AlignVCenter);
		}
	}
	if(role == Qt::DisplayRole) {
		switch(idx.column()) {
		case Address: return QStringLiteral("0x")+
			QString("%1").arg(static_cast<qulonglong>(r.address), 8, 16, QLatin1Char('0'))
			.toUpper();
		case Bytes:    return QString::fromLatin1(r.bytes.toHex(' ').toUpper());
		case Mnemonic: return r.mnemonic;
		case Operands: return r.opStr;
		}
	}
	return {};
}

QVariant DisasmModel::headerData(int s, Qt::Orientation o, int role) const {
	if(o != Qt::Horizontal || role != Qt::DisplayRole) return {};
	switch(s) {
	case Address:  return "Адрес";
	case Bytes:    return "Байты";
	case Mnemonic: return "Мнемоника";
	case Operands: return "Операнды";
	}
	return {};
}

void DisasmModel::setRows(std::vector<DisasmInstr> rows) {
	beginResetModel(); rows_ = std::move(rows); endResetModel();
}

void DisasmModel::appendRows(std::vector<DisasmInstr> more) {
	if(more.empty()) {
		return;
	}
	const int firstRow = int(rows_.size());
	const int lastRow = firstRow + int(more.size()) - 1;
	beginInsertRows(QModelIndex(), firstRow, lastRow);
	rows_.insert(rows_.end(),
		std::make_move_iterator(more.begin()),
		std::make_move_iterator(more.end()));
	endInsertRows();
}

qint32 DisasmModel::findRowContaining(qint64 address) const {
	if(rows_.empty()) {
		return -1;
	}

	//ищем первую инструкцию с addr >= address
	auto it = std::lower_bound(rows_.begin(), rows_.end(), address,
		[](const DisasmInstr& x, qint64 addr) {
			return x.address < addr;
		});

	//точное совпадение начала инструкции
	if(it != rows_.end() && it->address == address) {
		return static_cast<int>(std::distance(rows_.begin(), it));
	}

	//адрес попадает внутрь предыдущей инструкции
	if(it != rows_.begin()) {
		const auto& prev = *(it - 1);
		const quint64 start = prev.address;
		const quint64 end = start + static_cast<quint64>(prev.size);
		if(address >= start && address < end) {
			return static_cast<int>(std::distance(rows_.begin(), it - 1));
		}
	}

	return -1;
}
