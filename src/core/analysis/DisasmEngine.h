#pragma once
#include <QObject>
#include <QByteArray>
#include <QString>
#include <QThread>
#include <atomic>
#include <vector>
#include <memory>
#include <capstone/capstone.h>

class IDataProvider;

struct DisasmInstr {
	quint64 address{0};
	QString mnemonic;
	QString opStr;
	uint16_t size{0};
	QByteArray bytes;
};

class DisasmEngine final: public QObject {
	Q_OBJECT
public:
	enum class Arch {
		X86_16,
		X86_32,
		X86_64,
		ARM,
		ARM_THUMB,
		ARM64
	};

public:
	explicit DisasmEngine(QObject* parent,
		std::shared_ptr<const IDataProvider> dataProvider,
		qint64 fileOffset,
		qint64 byteLength,
		Arch architecture,
		bool bigEndian,
		int chunkSizeBytes = 64 * 1024);

	~DisasmEngine();

	void start();
	void cancel();
	void refresh();
signals:
	void chunkReady(std::vector<DisasmInstr> instructions, int percent);
	void finished();
	void error(QString message);

private:
	class Worker;
	static int computeCarryTail(Arch architecture);

private:
	std::shared_ptr<const IDataProvider> m_dataProvider;
	qint64 m_fileOffset{0};
	qint64 m_byteLength{0};
	quint64 m_baseAddress{0};
	Arch m_architecture{Arch::X86_64};
	bool m_bigEndian{false};
	int m_chunkSizeBytes{64 * 1024};
	int m_carryTailBytes{15};

	QThread* m_thread{nullptr};
	std::atomic_bool m_cancelRequested{false};
	bool m_restartPending{false};
};

class CapstoneSession {
public:
	csh handle{0};

	bool open(DisasmEngine::Arch architecture, bool bigEndian) {
		cs_arch capArch = CS_ARCH_X86;
		cs_mode capMode = CS_MODE_64;

		switch(architecture) {
		case DisasmEngine::Arch::X86_16:
			capArch = CS_ARCH_X86; capMode = CS_MODE_16;
			break;
		case DisasmEngine::Arch::X86_32:
			capArch = CS_ARCH_X86; capMode = CS_MODE_32;
			break;
		case DisasmEngine::Arch::X86_64:
			capArch = CS_ARCH_X86; capMode = CS_MODE_64;
			break;
		case DisasmEngine::Arch::ARM:
			capArch = CS_ARCH_ARM;
			capMode = bigEndian ? CS_MODE_BIG_ENDIAN : CS_MODE_LITTLE_ENDIAN;
			break;
		case DisasmEngine::Arch::ARM_THUMB:
			capArch = CS_ARCH_ARM;
			capMode = static_cast<cs_mode>((bigEndian ? CS_MODE_BIG_ENDIAN : CS_MODE_LITTLE_ENDIAN) | CS_MODE_THUMB);
			break;
		case DisasmEngine::Arch::ARM64:
			capArch = CS_ARCH_ARM64;
			capMode = bigEndian ? CS_MODE_BIG_ENDIAN : CS_MODE_LITTLE_ENDIAN;
			break;
		}

		if(cs_open(capArch, capMode, &handle) != CS_ERR_OK) {
			handle = 0;
			return false;
		}

		cs_option(handle, CS_OPT_DETAIL, CS_OPT_ON);
		cs_option(handle, CS_OPT_SKIPDATA, CS_OPT_ON);
		cs_opt_skipdata skipOpt{};
		skipOpt.mnemonic = "db";
		cs_option(handle, CS_OPT_SKIPDATA_SETUP, static_cast<size_t>(reinterpret_cast<uintptr_t>(&skipOpt)));
		return true;
	}

	~CapstoneSession() {
		if(handle != 0) {
			cs_close(&handle);
		}
	}
};