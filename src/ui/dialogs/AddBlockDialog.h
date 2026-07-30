#pragma once

#include <QDialog>
#include "ui_AddBlockDialog.h"
#include "BlockManagerTypes.h"
#include "FieldManagerTypes.h"

enum class DialogType{Block, Field};

QT_BEGIN_NAMESPACE
namespace Ui { class AddBlockDialog; };
QT_END_NAMESPACE

class AddBlockDialog : public QDialog
{
	Q_OBJECT

public:
	AddBlockDialog(DialogType dlgType, QWidget *parent = nullptr);
	~AddBlockDialog();
	qint64 startOffset() const { return m_startOffset; }
	qint64 endOffset() const { return m_endOffset; }
	QString name() const { return m_name; }
	QString comment() const { return m_comment; }
	QColor color() const { return m_color; }
	BlockType blockType() const { return m_blockType; }
	FieldType fieldType() const { return m_fieldType; }
	bool collapsed() const { return m_collapsed; }
	bool isBigEndian() const { return m_byteOrder; }
	void editExistingBlock(const BlockInfo& block);
	void editExistingField(const FieldInfo& field);
	void setOffsets(qint64 startOffset, qint64 endOffset);
	void setFieldModeUI(bool editMode);
	void setBlockModeUI(bool editMode);
private:
	Ui::AddBlockDialog *ui;
	qint64 m_startOffset{};
	qint64 m_endOffset{};
	QString m_name{};
	QString m_comment{};
	QColor m_color{};
	bool m_collapsed{};
	bool m_byteOrder{false};
	BlockType m_blockType{BlockType::raw};
	FieldType m_fieldType{FieldType::raw};
	bool m_fieldMode{false};
private slots:
	void onCreateButton();
	void onPlusStartOffsetBtn();
	void onMinusStartOffsetBtn();
	void onPlusEndOffsetBtn();
	void onMinusEndOffsetBtn();
	void onChangeColorBtn();
protected:
	bool eventFilter(QObject* obj, QEvent* event);
signals:
	void deleteBlock();
	void deleteField();
};

