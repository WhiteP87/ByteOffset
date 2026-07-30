#pragma once
#include "IBinaryConfig.h"
#include "BlockManager.h"
#include <map>
#include <unordered_map>
#include <functional>
#include "FieldManagerTypes.h"


class FieldManager final : public IBinaryConfig {
public:
	FieldManager() = default;

	FieldID addField(qint64 startOffset, qint64 endOffset, const QString& name, const QString& comment = "",
		const QColor& color = Qt::yellow, FieldType type = FieldType::raw, const QString& typeFormat = "",
		bool isBigEndian = false, FieldID fieldId = 0, qint32 parentBlockId = 0);

	FieldID addField(const FieldInfo& field);

	bool editField(FieldID fieldId, qint64 startOffset, qint64 endOffset, const QString& name, const QString& comment,
		const QColor& color, FieldType type, const QString& typeFormat, bool isBigEndian, qint32 parentBlockId = 0);

	bool editField(const FieldInfo& field);

	void removeFieldById(FieldID fieldId);

	bool isVisible() const { return m_visible; }
	bool hasFields() const { return !m_fields.empty(); }
	qint32 maxFieldLength() const { return MAX_FIELD_LENGTH; }
	void setMaxFieldLength(qint32 len) { MAX_FIELD_LENGTH = len; }

	void setVisible(bool visibleState) {
		if(m_visible != visibleState) {
			m_visible = visibleState;
			invokeChangedCallbacks({FieldEventKind::StateChanged, 0});
		}
	}

	//коллбэки
	using callbackType = std::function<void(const FieldChangeData&)>;
	using callbackID = qint32;
	callbackID registerChangedCallback(callbackType cb);
	void unregisterChangedCallback(callbackID cbId);
	void setNotifying(bool enable);

	std::optional<std::vector<const FieldInfo*>> isOffsetIntoField(qint64 offset) const;
	const FieldInfo* getFieldById(FieldID fieldId) const;
	std::vector<const FieldInfo*> getFieldsFromTo(qint64 from, qint64 to) const;

	void clear(bool invokeable = true);


	//IBinaryConfig реализация
	BinaryConfigDescriptor configDescriptor() const override;
	bool exportBinaryConfig(QByteArray& outPayload, QString* errorText) const override;
	bool importBinaryConfig(const QByteArray& payload, QString* errorText) override;

	void onInsert(qint64 at, qint64 len);
	void onRemove(qint64 at, qint64 len);
private:
	qint32 MAX_FIELD_LENGTH{1024};
	const qint32 FIELD_COLOR_ALPHA = 80;

	FieldInfo* getFieldByIdPtr(FieldID id);

	FieldID insertField(FieldInfo field);

	//вспомогатель­ная вставка + сортировка
	void emplaceSorted(qint64 key, FieldInfo bi) {
		auto& vec = m_fields[key];
		vec.emplace_back(std::move(bi));
		std::stable_sort(vec.begin(), vec.end(), [](const FieldInfo& a, const FieldInfo& b) {return a.length() > b.length(); });
	}

	mutable QReadWriteLock m_dataLock;

	std::map<qint64,std::vector<FieldInfo>> m_fields{};

	//карта id->смещение для быстрого поиска
	std::unordered_map<FieldID, qint64> m_jump{};
	FieldID m_fieldId{1};
	bool m_visible{true};

	//коллбэки уведомлений
	callbackID cbID{1};
	bool m_callbacksEnabled{true};
	std::unordered_map<callbackID, callbackType> changeCalls;
	mutable QReadWriteLock m_cbLock;
	void invokeChangedCallbacks(const FieldChangeData& changes);
};