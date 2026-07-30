//EditorUndoController.cpp
#include "EditorUndoController.h"
#include <unordered_set>
#include <QUndoCommand>
#include <QByteArray>
#include "HexEditor.h"

//вспомогательная функция чтения диапазона байт из провайдера
static QByteArray readProviderRange(const IDataProvider& provider, qint64 off, qint64 len) {
	if(off < 0 || len <= 0) {
		return {};
	}
	QByteArray buf;
	buf.resize(static_cast<qint32>(len));
	const qint64 n = provider.readRange(off, len, buf.data());
	if(n <= 0) {
		return {};
	}
	if(n != len) {
		buf.resize(static_cast<qint32>(n));
	}
	return buf;
}

class PasteInsertCommand final: public QUndoCommand {
public:
	PasteInsertCommand(HexEditor* ed, qint64 off, QByteArray data)
		: editor(ed), m_off(off), m_data(std::move(data)) {
		setText(QStringLiteral("Вставка"));
	}

	void redo() override {
		if(!editor || m_data.isEmpty()) return;
		if(!m_inited) {
			m_area = editor->getActiveInputArea();
			m_inited = true;
		}
		editor->setInputArea(m_area);
		editor->setCaret(m_off+m_data.size(), 0);

		editor->dataProvider().insert(m_off, QByteArrayView(m_data));
		editor->textDecoder().invalidateCache(m_off, 0); // геометрия изменилась
	}

	void undo() override {
		if(!editor || m_data.isEmpty()) return;
		editor->setInputArea(m_area);
		editor->setCaret(m_off, 0);

		editor->dataProvider().remove(m_off, m_data.size());
		editor->textDecoder().invalidateCache(m_off, 0);
	}

private:
	HexEditor* editor{};
	qint64 m_off{0};
	QByteArray m_data;
	EditorArea m_area{};
	bool m_inited{false};
};

class PasteReplaceSelectionCommand final: public QUndoCommand {
public:
	PasteReplaceSelectionCommand(HexEditor* ed, qint64 off, qint64 selLen, QByteArray data)
		: editor(ed), m_off(off), m_selLen(selLen), m_new(std::move(data)) {
		setText(QStringLiteral("Вставка"));
	}

	void redo() override {
		if(!editor) return;

		if(!m_inited) {
			if(m_selLen > 0)
				m_old = readProviderRange(editor->dataProvider(), m_off, m_selLen);

		
			if(m_selLen != m_new.size()) {
				collectAffectedBlocks(m_off, m_off + m_selLen);
				collectAffectedBookmarks();
				collectAffectedFields(m_off, m_off + m_selLen);
			}

			m_area = editor->getActiveInputArea();
			m_inited = true;
		}

		editor->setInputArea(m_area);
		editor->setCaret(m_off+m_new.size(), 0);

		const qint64 pastedSize = m_new.size();
		if(pastedSize == m_selLen) {
			if(pastedSize > 0) {
				editor->dataProvider().writeRange(m_off, QByteArrayView(m_new));
				editor->textDecoder().invalidateCache(m_off, pastedSize);
			}
			return;
		}

		if(pastedSize > m_selLen) {
			//перезаписать выбранную часть
			if(m_selLen > 0) {
				editor->dataProvider().writeRange(m_off, QByteArrayView(m_new.constData(), m_selLen));
				editor->textDecoder().invalidateCache(m_off, m_selLen);
			}
			//вставить хвост за концом выделения
			const qint64 tailLen = pastedSize - m_selLen;
			const char* tail = m_new.constData() + m_selLen;
			editor->dataProvider().insert(m_off + m_selLen, QByteArrayView(tail, tailLen));
			editor->textDecoder().invalidateCache(m_off, 0); // геометрия изменилась
		} else {
			//перезаписать L байт
			if(pastedSize > 0) {
				editor->dataProvider().writeRange(m_off, QByteArrayView(m_new));
				editor->textDecoder().invalidateCache(m_off, pastedSize);
			}
			//удалить остаток выделения
			const qint64 rest = m_selLen - pastedSize;
			editor->dataProvider().remove(m_off + pastedSize, rest);
			editor->textDecoder().invalidateCache(m_off, 0); // геометрия изменилась
		}
	}

	void undo() override {
		if(!editor) return;

		editor->setInputArea(m_area);
		editor->setCaret(m_off, 0);

		const qint64 pastedSize = m_new.size();
		if(pastedSize == m_selLen) {
			if(!m_old.isEmpty()) {
				editor->dataProvider().writeRange(m_off, QByteArrayView(m_old));
				editor->textDecoder().invalidateCache(m_off, m_old.size());
			}
			return;
		}

		if(pastedSize > m_selLen) {
			//убрать вставленный хвост
			const qint64 tailLen = pastedSize - m_selLen;
			editor->dataProvider().remove(m_off + m_selLen, tailLen);
			editor->textDecoder().invalidateCache(m_off, 0);

			//восстановить исходные байты выделения
			if(!m_old.isEmpty()) {
				editor->dataProvider().writeRange(m_off, QByteArrayView(m_old));
				editor->textDecoder().invalidateCache(m_off, m_old.size());
			}
		} else { 
			//вернуть отрезанный хвост выделения
			if(m_selLen > pastedSize) {
				const QByteArray tail = m_old.mid(static_cast<int>(pastedSize)); // хвост оригинала
				if(!tail.isEmpty()) {
					editor->dataProvider().insert(m_off + pastedSize, QByteArrayView(tail));
					editor->textDecoder().invalidateCache(m_off, 0);
				}
			}
			//восстановить голову выделения
			if(pastedSize > 0) {
				editor->dataProvider().writeRange(m_off, QByteArrayView(m_old.constData(), pastedSize));
				editor->textDecoder().invalidateCache(m_off, pastedSize);
			}
		}

		restoreAffectedBlocks();
		restoreAffectedBookmarks();
		restoreAffectedFields();
	}

private:

	void collectAffectedFields(qint64 from, qint64 to) {
		const auto fields = editor->fieldManager().getFieldsFromTo(from, to);
		for(auto& x : fields) {
			m_fieldsAffected.push_back(*x);
		}
	}

	void restoreAffectedFields() {
		if(!m_fieldsAffected.empty() && editor) {
			const auto endOff = m_off + m_selLen/* - 1*/;
			for(const auto& field : m_fieldsAffected) {
				if(field.startOffset >= m_off && field.endOffset <= endOff) {
					editor->fieldManager().addField(field);
					continue;
				}
				editor->fieldManager().editField(field);
			}
		}
	}

	//сбор/восстановление блоков и закладок
	void collectAffectedBlocks(qint64 from, qint64 to) {
		m_blocksAffected.clear();
		const auto& map = editor->blockManager().getBlockMarkersMap();
		const auto bound = map.upper_bound(to);

		using BlockId = decltype(m_blocksAffected[0].blockId);
		std::unordered_set<BlockId> seen;

		for(auto it = map.lower_bound(from); it != bound; ++it) {
			for(const auto& bi : it->second) {
				if(seen.insert(bi.blockId).second) {
					m_blocksAffected.push_back(bi);
				}
			}
		}
	}

	void restoreAffectedBlocks() {
		if(m_blocksAffected.empty()) return;
		const qint64 endOff = m_off + m_selLen;
		for(const auto& b : m_blocksAffected) {
			if(b.startOffset >= m_off && b.endOffset <= endOff) {
				editor->blockManager().addBlock(b.startOffset, b.endOffset, b.name, b.comment, b.blockType, b.isBigEndian,
					b.color, b.collapsed, b.blockId);
			} else {
				editor->blockManager().editBlock(b.blockId, b.startOffset, b.endOffset,
					b.name, b.comment, b.blockType, b.isBigEndian, b.color, b.collapsed);
			}
		}
	}


	void collectAffectedBookmarks() {
		m_bookmarksAffected.clear();
		editor->bookmarkManager().getBookmarksFromTo(m_off, m_off + m_selLen, m_bookmarksAffected);
	}

	void restoreAffectedBookmarks() {
		if(m_bookmarksAffected.empty()) return;
		for(const auto& bm : m_bookmarksAffected)
			editor->bookmarkManager().addBookmark(bm.offset, bm.comment);
	}

private:
	HexEditor* editor{};
	qint64 m_off{0};
	qint64 m_selLen{0};
	QByteArray m_new;
	QByteArray m_old;

	std::vector<BlockInfo> m_blocksAffected{};
	std::vector<BookmarkData> m_bookmarksAffected{};
	std::vector<FieldInfo> m_fieldsAffected{};
	EditorArea m_area{};
	bool m_inited{false};
};

class PasteOverwriteCommand final: public QUndoCommand {
public:
	PasteOverwriteCommand(HexEditor* ed, qint64 off, QByteArray data)
		: editor(ed), m_off(off), m_new(std::move(data)) {
		setText(QStringLiteral("Вставка"));
	}

	void redo() override {
		if(!editor || m_new.isEmpty()) return;

		if(!m_inited) {
			const qint64 sz = editor->dataProvider().size();
			m_nWrite = std::max<qint64>(0, std::min<qint64>(m_new.size(), sz - m_off));
			if(m_nWrite > 0)
				m_old = readProviderRange(editor->dataProvider(), m_off, m_nWrite);

			m_area = editor->getActiveInputArea();
			m_inited = true;
		}

		if(m_nWrite <= 0) return;

		editor->setInputArea(m_area);
		editor->setCaret(m_off + m_new.size(), 0);

		editor->dataProvider().writeRange(m_off, QByteArrayView(m_new.constData(), m_nWrite));
		editor->textDecoder().invalidateCache(m_off, m_nWrite);
	}

	void undo() override {
		if(!editor || m_old.isEmpty()) return;

		editor->setInputArea(m_area);
		editor->setCaret(m_off, 0);

		editor->dataProvider().writeRange(m_off, QByteArrayView(m_old));
		editor->textDecoder().invalidateCache(m_off, m_old.size());
	}

private:
	HexEditor* editor{};
	qint64 m_off{0};
	QByteArray m_new;
	QByteArray m_old;
	qint64 m_nWrite{0};   // пишем не больше конца файла

	EditorArea m_area{};
	bool m_inited{false};
};



//команда побайтной записи, агрегирующая нибблы
//при необходимости сначала вставляет 1 байт
class WriteByteCommand final:public QUndoCommand {
public:
	//создание команды
	//nibbleIndex: -1=целый байт; 0=старший ниббл; 1=младший ниббл
	//insertBefore: true если нужно insert(1) перед записью (режим вставки)
	WriteByteCommand(HexEditor * editorPtr, qint64 off, quint8 value, qint32 nibbleIndex, bool insertBefore)
		: editor(editorPtr)
		, m_off(off)
		, m_insertBefore(insertBefore){

		//инициализируем «набор изменений» по нибблам
		if(nibbleIndex < 0) {
			m_highChanged = true; m_highNibble = static_cast<quint8>((value >> 4) & 0x0F);
			m_lowChanged = true; m_lowNibble = static_cast<quint8>(value & 0x0F);
		} else if(nibbleIndex == 0) {
			m_highChanged = true; m_highNibble = static_cast<quint8>(value & 0x0F);
		} else { //nibbleIndex == 1
			m_lowChanged = true; m_lowNibble = static_cast<quint8>(value & 0x0F);
		}
		setText(m_insertBefore ? QStringLiteral("вставка и запись байта")
			: QStringLiteral("запись байта"));
	}

	//раскомментировать для побайтовой отмены
// 	//идентификатор для возможности mergeWith с соседней командой
// 	int id() const override {
// 		return 0xB17E0101; //произвольный id команды
// 	}
// 
// 	//слияние с последующей командой, если тот же offset и тот же тип команды
// 	bool mergeWith(const QUndoCommand* other) override {
// 		auto o = dynamic_cast<const WriteByteCommand*>(other);
// 		if(!o) return false;
// 		if(o->m_off != m_off) return false;
// 
// 		//если первая команда вставляла, оставляем insert у первой
// 		//вторая может быть обычной записью того же байта
// 		if(o->m_highChanged) { m_highChanged = true; m_highNibble = o->m_highNibble; }
// 		if(o->m_lowChanged) { m_lowChanged = true; m_lowNibble = o->m_lowNibble; }
// 		return true;
// 	}

	void redo() override {
		//читаем старый байт
		if(!m_initializedOld) {
			m_oldByte = editor->dataProvider().readByte(m_off);
			m_initializedOld = true;
		}

		//если нужно вставляем байт
		if(m_insertBefore) {
			QByteArray one(1, '\0');
			if(!m_inserted) {
				m_inserted = true;
			}
			editor->dataProvider().insert(m_off, QByteArrayView(one));
		}

		if(m_highChanged) {
			editor->dataProvider().writeByte(m_off, m_highNibble, 0); //старший ниббл
			if(m_areaSaved) editor->setInputArea(EditorArea::HEX_AREA);
			editor->setCaret(m_off, 1);
		}
		if(m_lowChanged) {
			editor->dataProvider().writeByte(m_off, m_lowNibble, 1); //младший ниббл
			if(m_areaSaved) editor->setInputArea(EditorArea::HEX_AREA);
			editor->setCaret(m_off + 1, 0);		
		}

		if(!m_areaSaved) {
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}

		editor->textDecoder().invalidateCache(m_off,m_insertBefore?0:1);
	}

	void undo() override {
		//если байт был вставлен этой командой — просто удаляем его
		if(m_insertBefore) {
			if(m_inserted) {
				editor->dataProvider().remove(m_off, 1);
				editor->setInputArea(m_area);
				editor->setCaret(m_off, m_highChanged ? 0 : 1);
			}
			return;
		}

		//иначе восстанавливаем исходный байт целиком
		char b = static_cast<char>(m_oldByte);
		editor->dataProvider().writeRange(m_off, QByteArrayView(&b, 1));
		editor->textDecoder().invalidateCache(m_off, m_inserted ? 0 : 1);
		editor->setInputArea(m_area);
		editor->setCaret(m_off, m_highChanged ? 0 : 1);
	}

private:
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
	qint64 m_off{0};

	bool m_insertBefore{false};
	bool m_inserted{false};

	bool m_highChanged{false};
	bool m_lowChanged{false};
	quint8 m_highNibble{0};
	quint8 m_lowNibble{0};

	quint8 m_oldByte{0};
	bool m_initializedOld{false};
};


//команда записи диапазона
class WriteRangeCommand final:public QUndoCommand {
public:
	//создание команды записи диапазона
	WriteRangeCommand(HexEditor * editorPtr, qint64 off, QByteArray newData)
		: editor(editorPtr)
		, m_off(off)
		, m_newData(std::move(newData)) {
		setText(QStringLiteral("запись диапазона"));
	}

	//выполнение записи
	void redo() override {
		if(!m_initializedOld) {
			m_oldData = readProviderRange(editor->dataProvider(), m_off, m_newData.size());
			m_initializedOld = true;
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		editor->setInputArea(m_area);
		editor->setCaret(m_off, 0);
		editor->dataProvider().writeRange(m_off, QByteArrayView(m_newData));
		editor->textDecoder().invalidateCache(m_off, m_newData.size());
	}

	//отмена записи
	void undo() override {
		if(!m_oldData.isEmpty()) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().writeRange(m_off, QByteArrayView(m_oldData));
			editor->textDecoder().invalidateCache(m_off, m_oldData.size());
		}
	}

private:
	EditorArea m_area{};
	bool m_areaSaved{false};
	//смещение
	qint64 m_off{0};
	//новые данные
	QByteArray m_newData;
	//старые данные
	QByteArray m_oldData;
	//флаг инициализации
	bool m_initializedOld{false};
	HexEditor* editor;
};

//команда вставки диапазона
class InsertRangeCommand final:public QUndoCommand {
public:
	//создание команды вставки диапазона
	InsertRangeCommand(HexEditor * editorPtr, qint64 off, QByteArray data)
		: editor(editorPtr)
		, m_off(off)
		, m_data(std::move(data)){
		setText(QStringLiteral("вставка диапазона"));
	}

	//выполнение вставки
	void redo() override {
		if(!m_inserted) {
			const qint64 n = editor->dataProvider().insert(m_off, QByteArrayView(m_data));
			m_lenInserted = n > 0 ? n : 0;
			m_inserted = true;
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		} else {
			//повторный redo после undo
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().insert(m_off, QByteArrayView(m_data));
			editor->textDecoder().invalidateCache( m_off, 0);
		}
	}

	//отмена вставки
	void undo() override {
		if(m_lenInserted > 0) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().remove(m_off, m_lenInserted);
			editor->textDecoder().invalidateCache( m_off, 0);
		}
	}

private:
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
	//смещение
	qint64 m_off{0};
	//данные для вставки
	QByteArray m_data;
	//длина действительно вставленного диапазона
	qint64 m_lenInserted{0};
	//флаг первичного redo
	bool m_inserted{false};
};

//команда удаления диапазона
class RemoveRangeCommand final :public QUndoCommand {
public:
	//создание команды удаления диапазона
	RemoveRangeCommand(HexEditor * editorPtr, qint64 off, qint64 len)
		: editor(editorPtr)
		, m_off(off)
		, m_len(len){
		setText(QStringLiteral("удаление диапазона"));
	}

	//выполнение удаления
	void redo() override {
		if(!m_initializedOld) {
			m_oldData = readProviderRange(editor->dataProvider(), m_off, m_len);
			m_initializedOld = true;
			collectAffectedBlockMarkers(m_off, m_off + m_len);
			collectAffectedBookmarks(m_off, m_off + m_len);
			collectAffectedFields(m_off, m_off + m_len);
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		if(m_len > 0) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().remove(m_off, m_len);
			editor->textDecoder().invalidateCache( m_off, 0);
		}
	}

	//отмена удаления (вставка старых данных назад)
	void undo() override {
		if(!m_oldData.isEmpty()) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().insert(m_off, QByteArrayView(m_oldData));
			editor->textDecoder().invalidateCache(m_off, 0);
			restoreAffectedBlocks();
			restoreAffectedBookmarks();
			restoreAffectedFields();
		}
	}

private:

	void collectAffectedFields(qint64 from, qint64 to) {
		const auto fields = editor->fieldManager().getFieldsFromTo(from, to);
		for(auto& x : fields) {
			m_fieldsAffected.push_back(*x);
		}
	}

	void restoreAffectedFields() {
		if(!m_fieldsAffected.empty() && editor) {
			const auto endOff = m_off + m_len/* - 1*/;
			for(const auto& field : m_fieldsAffected) {
				if(field.startOffset >= m_off && field.endOffset <= endOff) {
					editor->fieldManager().addField(field);
					continue;
				}
				editor->fieldManager().editField(field);
			}
		}
	}

	void collectAffectedBlockMarkers(qint64 from, qint64 to) {

		m_blocksAffected.clear();

		if(!editor) return;

		const auto& map = editor->blockManager().getBlockMarkersMap();
		const auto bound = map.upper_bound(to);//маркер конца за смещением конца

		using BlockId = decltype(m_blocksAffected[0].blockId);
		std::unordered_set<BlockId> seenIds;

		for(auto it = map.lower_bound(from); it != bound; ++it) {
			for(const auto& blockInfo : it->second) {
				if(seenIds.insert(blockInfo.blockId).second) {
					m_blocksAffected.push_back(blockInfo); //только первое вхождение
				}
			}
		}
	}

	void restoreAffectedBlocks() {
		if(!m_blocksAffected.empty() && editor) {
			const auto endOff = m_off + m_len/* - 1*/;
			for(const auto& block : m_blocksAffected) {
				if(block.startOffset >= m_off && block.endOffset <= endOff) {
					editor->blockManager().addBlock(block);
					continue;
				}
				editor->blockManager().editBlock(block);
			}
		}
	}

	void collectAffectedBookmarks(qint64 from, qint64 to) {
		m_bookmarksAffected.clear();
		editor->bookmarkManager().getBookmarksFromTo(from, to, m_bookmarksAffected);
	}

	void restoreAffectedBookmarks() {
		if(!m_bookmarksAffected.empty() && editor) {
			for(const auto& bookmarkData : m_bookmarksAffected)
				editor->bookmarkManager().addBookmark(bookmarkData.offset, bookmarkData.comment);
		}
	}


	//смещение
	qint64 m_off{0};
	//длина удаляемого диапазона
	qint64 m_len{0};
	//старые данные для восстановления
	QByteArray m_oldData;
	//флаг инициализации
	bool m_initializedOld{false};
	std::vector<BlockInfo> m_blocksAffected{};
	std::vector<BookmarkData> m_bookmarksAffected{};
	std::vector<FieldInfo> m_fieldsAffected{};
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
};

//команда заливки диапазона паттерном
class FillRangeCommand final:public QUndoCommand {
public:
	//создание команды заливки паттерном
	FillRangeCommand(HexEditor * editorPtr, qint64 off,
		qint64 len, QByteArray pattern)
		: editor(editorPtr)
		, m_off(off)
		, m_len(len)
		, m_pattern(std::move(pattern)){
		setText(QStringLiteral("заливка диапазона"));
	}

	//выполнение заливки
	void redo() override {
		if(!m_initializedOld) {
			m_oldData = readProviderRange(editor->dataProvider(), m_off, m_len);
			m_initializedOld = true;
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		if(m_len > 0) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().fillRange(m_off, m_len, QByteArrayView(m_pattern));
			editor->textDecoder().invalidateCache( m_off, m_len);
		}
	}

	//отмена заливки
	void undo() override {
		if(!m_oldData.isEmpty()) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().writeRange(m_off, QByteArrayView(m_oldData));
			editor->textDecoder().invalidateCache( m_off, m_oldData.size());
		}
	}

private:
	//смещение
	qint64 m_off{0};
	//длина заливки
	qint64 m_len{0};
	//паттерн заливки
	QByteArray m_pattern;
	//старые данные
	QByteArray m_oldData;
	//флаг инициализации
	bool m_initializedOld{false};
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
};

//команда: кодировать символ и записать по смещению
class EncodeWriteCharCommand final:public QUndoCommand {
public:
	//создание команды записи символа с кодированием
	EncodeWriteCharCommand(HexEditor * editorPtr, qint64 off, QString ch, qint32 charLen)
		: editor(editorPtr)
		, m_off(off)
		, m_ch(std::move(ch))
		, m_charLen(charLen) {
		setText(QStringLiteral("запись символа"));
	}

	//выполнение кодирования и записи
	void redo() override {
		if(!m_initializedOld) {
			if(m_charLen > 0) {
				m_oldData = readProviderRange(editor->dataProvider(), m_off, m_charLen);
			}
			m_initializedOld = true;
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		editor->setInputArea(m_area);
		editor->setCaret(m_off, 0);
		editor->textDecoder().encodeAndWriteAt(m_off, m_ch);
		editor->textDecoder().invalidateCache(m_off, m_charLen);
	}

	//отмена записи символа
	void undo() override {
		if(!m_oldData.isEmpty()) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().writeRange(m_off, QByteArrayView(m_oldData));
			editor->textDecoder().invalidateCache(m_off, m_oldData.size());
		}
	}

private:
	//смещение
	qint64 m_off{0};
	//символ
	QString m_ch;
	//ожидаемая длина в байтах
	qint32 m_charLen{0};
	//старые данные
	QByteArray m_oldData;
	//флаг инициализации
	bool m_initializedOld{false};
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};

};

//команда: кодировать символ и вставить (атомарная операция)
class EncodeInsertCharCommand final:public QUndoCommand {
public:
	//создание команды вставки символа
	EncodeInsertCharCommand(HexEditor * editorPtr,qint64 off, QString ch, qint32 charLen)
		: editor(editorPtr)
		, m_off(off)
		, m_ch(std::move(ch))
		, m_charLen(charLen) {
		setText(QStringLiteral("вставка символа"));
	}

	//выполнение вставки и записи
	void redo() override {
		if(m_charLen <= 0) {
			return;
		}
		if(!m_inserted) {
			editor->dataProvider().insert(m_off, QByteArray(m_charLen, '\0'));
			m_inserted = true;			
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		editor->setInputArea(m_area);
		editor->setCaret(m_off, 0);
		editor->textDecoder().encodeAndWriteAt(m_off, m_ch);
		editor->textDecoder().invalidateCache(m_off, 0);
	}

	//отмена вставки символа
	void undo() override {
		if(m_inserted && m_charLen > 0) {
			editor->setInputArea(m_area);
			editor->setCaret(m_off, 0);
			editor->dataProvider().remove(m_off, m_charLen);
			editor->textDecoder().invalidateCache(m_off, 0);
		}
	}

private:
	//смещение
	qint64 m_off{0};
	//символ
	QString m_ch;
	//ожидаемая длина в байтах
	qint32 m_charLen{0};
	//флаг сделанной вставки
	bool m_inserted{false};
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
};

//команда создания блока
class AddBlockCommand final:public QUndoCommand {
public:
	//создание команды добавления блока
	AddBlockCommand(HexEditor * editorPtr, const BlockInfo& block)
		: editor(editorPtr)
		, m_newBlock{std::move(block)} {
		setText(QStringLiteral("создание блока «%1»").arg(m_newBlock.name));
	}

	//выполнение создания блока
	void redo() override {
		if(m_createdId == 0) {
			m_createdId = editor->blockManager().addBlock(m_newBlock);
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		} else {
			//после undo пересоздаём с исходным id
			m_newBlock.blockId = m_createdId;
			m_createdId = editor->blockManager().addBlock(m_newBlock);
			editor->setInputArea(m_area);
			editor->setCaret(m_newBlock.startOffset, 0);
		}
	}

	//отмена создания блока
	void undo() override {
		if(m_createdId != 0) {			
			editor->blockManager().removeBlockById(m_createdId);
			editor->setInputArea(m_area);
			editor->setCaret(m_newBlock.startOffset, 0);
		}
	}

	//получение созданного id
	qint32 createdId() const { return m_createdId; }

private:
	BlockInfo m_newBlock{};
	//фактически созданный id
	qint32 m_createdId{0};
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
	bool m_isbe{false};
};

//команда редактирования блока
class EditBlockCommand final:public QUndoCommand {
public:
	//создание команды редактирования блока
	EditBlockCommand(HexEditor * editorPtr, const BlockInfo& newBlockData)
		: editor(editorPtr)
		, m_newDataBlock{std::move(newBlockData)} {
		setText(QStringLiteral("редактирование блока «%1»").arg(m_newDataBlock.name));
		m_id = newBlockData.blockId;
	}

	//выполнение изменения
	void redo() override {
		if(!m_initializedOld) {
			const BlockInfo* s = editor->blockManager().getBlockById(m_id, MarkerType::BlockStart);
			if(s) {
				m_oldDataBlock = *s;
			}
			m_initializedOld = true;
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		editor->setInputArea(m_area);
		editor->setCaret(m_oldDataBlock.startOffset, 0);
		editor->blockManager().editBlock(m_newDataBlock);
	}

	//отмена изменения
	void undo() override {
		editor->setInputArea(m_area);
		editor->setCaret(m_oldDataBlock.startOffset, 0);
		editor->blockManager().editBlock(m_oldDataBlock);
	}

private:
	//id блока
	qint32 m_id{0};
	//новые значения
	BlockInfo m_newDataBlock{};
	//старые значения
	BlockInfo m_oldDataBlock{};
	//флаг инициализации старых значений
	bool m_initializedOld{false};
	HexEditor* editor;
	EditorArea m_area{};
	bool m_areaSaved{false};
};

//команда удаления блока
class RemoveBlockCommand final:public QUndoCommand {
public:
	//создание команды удаления блока
	RemoveBlockCommand(HexEditor * editorPtr, qint32 id)
		: editor(editorPtr)
		, m_id(id) {
		//имя подставим при redo (после захвата старых параметров)
		setText(QStringLiteral("удаление блока"));
	}

	//выполнение удаления
	void redo() override {
		if(!m_initializedOld) {
			const BlockInfo* s = editor->blockManager().getBlockById(m_id);
			if(s) {
				m_oldBlockData = *s;
				setText(QStringLiteral("удаление блока «%1»").arg(m_oldBlockData.name));
			}
			m_initializedOld = true;
			m_area = editor->getActiveInputArea();
			m_areaSaved = true;
		}
		editor->setInputArea(m_area);
		editor->setCaret(m_oldBlockData.startOffset, 0);
		editor->blockManager().removeBlockById(m_id);
	}

	//отмена удаления (восстановление блока с тем же id)
	void undo() override {
		editor->blockManager().addBlock(m_oldBlockData);
	}

private:
	BlockInfo m_oldBlockData{};

	//id блока
	qint32 m_id{0};

	//флаг инициализации
	bool m_initializedOld{false};
	HexEditor* editor{};
	EditorArea m_area{};
	bool m_areaSaved{false};
};

//конструктор контроллера
EditorUndoController::EditorUndoController(HexEditor * editorPtr,QObject* parent)
	: QObject(parent)
	, editor(editorPtr)
	, m_undoStack(new QUndoStack(this)) {
}

//получение стека undo/redo
QUndoStack* EditorUndoController::undoStack() const {
	return m_undoStack;
}

//установка лимита глубины undo
void EditorUndoController::setUndoLimit(qint32 limit) {
	if(m_undoStack) {
		m_undoStack->setUndoLimit(limit);
	}
}

//очистка стека undo/redo
void EditorUndoController::clear() {
	if(m_undoStack) {
		m_undoStack->clear();
	}
}

//запись одного байта/полубайта по смещению
bool EditorUndoController::writeByte(qint64 off, quint8 value, qint32 nibbleIndex) {
	if(!editor) {
		return false;
	}
	auto* cmd = new WriteByteCommand(editor, off, value, nibbleIndex, /*insertBefore*/false);
	m_undoStack->push(cmd);
	return true;
}

//запись диапазона байтов по смещению
qint64 EditorUndoController::writeRange(qint64 off, QByteArrayView data) {
	if(!editor || data.isEmpty()) {
		return 0;
	}
	auto* cmd = new WriteRangeCommand(editor, off, QByteArray(data));
	m_undoStack->push(cmd);
	return data.size();
}

//вставка диапазона байтов по смещению
qint64 EditorUndoController::insert(qint64 off, QByteArrayView data) {
	if(!editor || data.isEmpty()) {
		return 0;
	}
	auto* cmd = new InsertRangeCommand(editor, off, QByteArray(data));
	m_undoStack->push(cmd);
	return data.size();
}

//удаление диапазона байтов по смещению
qint64 EditorUndoController::remove(qint64 off, qint64 len) {
	if(!editor || len <= 0 || off < 0 || off > editor->dataProvider().size()-1) {
		return 0;
	}
	auto* cmd = new RemoveRangeCommand(editor, off, len);
	m_undoStack->push(cmd);
	return len;
}

//заливка диапазона заданным паттерном
qint64 EditorUndoController::fillRange(qint64 off, qint64 len, QByteArrayView pattern) {
	if(!editor || len <= 0 || pattern.isEmpty()) {
		return 0;
	}
	auto* cmd = new FillRangeCommand(editor, off, len, QByteArray(pattern));
	m_undoStack->push(cmd);
	return len;
}

//hex-ввод в режиме вставки: единая операция «вставить байт и записать старший полубайт»
bool EditorUndoController::writeInserted(qint64 off, quint8 value, qint32 nibbleIndex) {
	if(!editor) {
		return false;
	}
	auto* cmd = new WriteByteCommand(editor, off, value, nibbleIndex, /*insertBefore*/true);
	m_undoStack->push(cmd);
	return true;
}

//кодировать символ и записать по смещению (режим перезаписи)
qint32 EditorUndoController::encodeAndWriteAt(qint64 byteOffset, const QString& oneChar) {
	if(!editor || oneChar.isEmpty()) {
		return 0;
	}
	const qint32 chLen = editor->textDecoder().charLen(oneChar);
	if(chLen <= 0) {
		return 0;
	}
	auto* cmd = new EncodeWriteCharCommand(editor, byteOffset, oneChar, chLen);
	m_undoStack->push(cmd);
	return chLen;
}

//кодировать символ и вставить по смещению (режим вставки; единая операция)
qint32 EditorUndoController::encodeAndInsertAt(qint64 byteOffset, const QString& oneChar) {
	if(!editor || oneChar.isEmpty()) {
		return 0;
	}
	const qint32 chLen = editor->textDecoder().charLen(oneChar);
	if(chLen <= 0) {
		return 0;
	}
	auto* cmd = new EncodeInsertCharCommand(editor, byteOffset, oneChar, chLen);
	m_undoStack->push(cmd);
	return chLen;
}

//создание блока разметки. blockId=0 означает назначить id автоматически
qint32 EditorUndoController::addBlock(const BlockInfo& block) {
	if(!editor) {
		return 0;
	}
	auto* cmd = new AddBlockCommand(editor, std::move(block));
	m_undoStack->push(cmd);
	//команда внутри узнаёт созданный id, но наружу возвращаем 0/неиспользуем — HexEditor может запросить позже у BlockManager
	return 0;
}

//редактирование существующего блока разметки
bool EditorUndoController::editBlock(const BlockInfo& block) {
	if(!editor || block.blockId == 0) {
		return false;
	}
	auto* cmd = new EditBlockCommand(editor, block);
	m_undoStack->push(cmd);
	return true;
}

//удаление блока по id
void EditorUndoController::removeBlock(qint32 blockId) {
	if(!editor || blockId == 0) {
		return;
	}
	auto* cmd = new RemoveBlockCommand(editor, blockId);
	m_undoStack->push(cmd);
}

qint64 EditorUndoController::pasteInsert(qint64 off, QByteArrayView bytes) {
	if(!editor || off < 0 || bytes.isEmpty()) return 0;
	m_undoStack->push(new PasteInsertCommand(editor, off, QByteArray(bytes)));
	return bytes.size();
}

qint64 EditorUndoController::pasteReplaceSelection(qint64 off, qint64 selLen, QByteArrayView bytes) {
	if(!editor || off < 0 || selLen < 0) return 0;
	m_undoStack->push(new PasteReplaceSelectionCommand(editor, off, selLen, QByteArray(bytes)));
	return bytes.size();
}

qint64 EditorUndoController::pasteOverwrite(qint64 off, QByteArrayView bytes) {
	if(!editor || off < 0 || bytes.isEmpty()) return 0;
	m_undoStack->push(new PasteOverwriteCommand(editor, off, QByteArray(bytes)));
	return std::min<qint64>(bytes.size(), std::max<qint64>(0, editor->dataProvider().size() - off));
}