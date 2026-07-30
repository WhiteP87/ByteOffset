//EditorUndoController.h
#pragma once
#include <QObject>
#include <QUndoStack>
#include <QByteArrayView>
#include <QString>
#include <QColor>
#include <memory>


class HexEditor;
class IDataProvider;
class BlockManager;
class DataTextTranslator;
enum class BlockType;
struct BlockInfo;

//контроллер отмены/повтора для модифицирующих операций редактора
class EditorUndoController:public QObject {
	Q_OBJECT
public:
	//создание контроллера. принимает провайдера данных, менеджер блоков и (опционально) переводчик текста
	explicit EditorUndoController(HexEditor * editor, QObject* parent = nullptr);

	//получение стека undo для привязки к QUndoGroup/меню
	QUndoStack* undoStack() const;

	//установка лимита глубины undo
	void setUndoLimit(qint32 limit);

	//очистка стека undo/redo
	void clear();

	//вставка в позицию (insert-режим) 
	qint64 pasteInsert(qint64 off, QByteArrayView bytes);

	//перезаписать выделение буфером bytes.
	//если bytes длиннее — остаток вставить, если короче — хвост выделения удалить.
	qint64 pasteReplaceSelection(qint64 off, qint64 selLen, QByteArrayView bytes);

	//перезаписать начиная с off ровно min(bytes.size, size-off) байт (overwrite-режим).
	//геометрия не меняется; хвост буфера игнорируется.
	qint64 pasteOverwrite(qint64 off, QByteArrayView bytes);

	//запись одного байта или полубайта по смещению (nibbleIndex: -1=целый байт, 0/1=старший/младший полубайт)
	bool writeByte(qint64 off, quint8 value, qint32 nibbleIndex);

	//запись диапазона байтов по смещению
	qint64 writeRange(qint64 off, QByteArrayView data);

	//вставка диапазона байтов по смещению
	qint64 insert(qint64 off, QByteArrayView data);

	//удаление диапазона байтов по смещению
	qint64 remove(qint64 off, qint64 len);

	//заливка диапазона заданным паттерном
	qint64 fillRange(qint64 off, qint64 len, QByteArrayView pattern);

	//hex-ввод в режиме вставки: единая операция «вставить байт и записать старший полубайт»
	bool writeInserted(qint64 off, quint8 value, qint32 nibbleIndex);

	//кодировать символ и записать по смещению (режим перезаписи)
	qint32 encodeAndWriteAt(qint64 byteOffset, const QString& oneChar);

	//кодировать символ и вставить по смещению (режим вставки; единая операция)
	qint32 encodeAndInsertAt(qint64 byteOffset, const QString& oneChar);

	//создание блока разметки. blockId=0 означает назначить id автоматически
	qint32 addBlock(const BlockInfo& block);

	//редактирование существующего блока разметки
	bool editBlock(const BlockInfo& block);

	//удаление блока по id
	void removeBlock(qint32 blockId);
	bool hasEdit() const { return m_undoStack->canUndo(); }
signals:
	void requestSetCaret(qint64 byteOffset, qint32 nibbleIndex);

private:
	//запрещаем копирование
	Q_DISABLE_COPY(EditorUndoController)

	HexEditor* editor{};

	//стек undo/redo (владеет контроллер)
	QUndoStack* m_undoStack = nullptr;
};

