#pragma once
#include "DisasmEngine.h"
#include <capstone/capstone.h>
#include <QMetaType>
#include "IDataProvider.h"
#include <algorithm>

//внутренний рабочий объект, живущий в отдельном QThread
class DisasmWorker final: public QObject {
	Q_OBJECT
public:
	DisasmWorker(QObject* parent = nullptr):QObject(parent) {};
	std::shared_ptr<const IDataProvider> dataProvider;
	qint64 fileOffset{0};
	qint64 byteLength{0};
	quint64 baseAddress{0};
	DisasmEngine::Arch architecture{DisasmEngine::Arch::X86_64};
	bool bigEndian{false};
	int chunkSizeBytes{64 * 1024};
	int carryTailBytes{15};
	std::atomic_bool* cancelFlag{nullptr};

signals:
	void chunkReady(std::vector<DisasmInstr> instructions, int percent);
	void finished();
	void error(QString message);

public slots:
	void run() {
		//валидация параметров
		if(dataProvider == nullptr || chunkSizeBytes <= 0) {
			emit error("invalid parameters");
			emit finished();
			return;
		}

		const qint64 providerSize = dataProvider->size();
		if(providerSize <= 0 || fileOffset < 0 || fileOffset >= providerSize) {
			emit error("range out of provider");
			emit finished();
			return;
		}

		const qint64 maxAvailable = providerSize - fileOffset;
		const qint64 safeLength = std::clamp(byteLength, qint64(0), maxAvailable);
		if(safeLength == 0) {
			emit finished();
			return;
		}

		CapstoneSession session;
		if(!session.open(architecture, bigEndian)) {
			emit error("cs_open failed");
			emit finished();
			return;
		}

		cs_insn* tempInstruction = cs_malloc(session.handle);
		if(tempInstruction == nullptr) {
			emit error("cs_malloc failed");
			emit finished();
			return;
		}

		qint64 currentFileOffset = fileOffset;
		const qint64 endFileOffset = fileOffset + safeLength;

		QByteArray carryBuffer;
		carryBuffer.reserve(carryTailBytes);

		while(currentFileOffset < endFileOffset && !cancelFlag->load(std::memory_order_relaxed)) {
			const qint64 toRead = std::min<qint64>(chunkSizeBytes, endFileOffset - currentFileOffset);

			QByteArray chunkBytes;
			if(!dataProvider->readRange(currentFileOffset, toRead, chunkBytes)) {
				emit error("readRange failed");
				break;
			}

			const int carrySizeBefore = carryBuffer.size();

			QByteArray decodeBuffer;
			decodeBuffer.reserve(carrySizeBefore + chunkBytes.size());
			decodeBuffer.append(carryBuffer);
			decodeBuffer.append(chunkBytes);
			carryBuffer.clear();

			const uint8_t* scanPointer = reinterpret_cast<const uint8_t*>(decodeBuffer.constData());
			size_t bytesLeft = static_cast<size_t>(decodeBuffer.size());

			//адрес начала декодирования для этого цикла
			const uint64_t chunkBaseAddress =
				static_cast<uint64_t>(baseAddress) +
				static_cast<uint64_t>(currentFileOffset - fileOffset);

			uint64_t addressForDecode =
				chunkBaseAddress - static_cast<uint64_t>(carrySizeBefore);

			//признак последнего чанка и размер требуемого «хвоста»
			const bool isLastChunk = (currentFileOffset + toRead >= endFileOffset);
			const size_t tailRequired = isLastChunk ? size_t(0) : static_cast<size_t>(carryTailBytes);

			if(bytesLeft < tailRequired) {
				carryBuffer = decodeBuffer;
				currentFileOffset += toRead;
				const int percent = static_cast<int>(
					((currentFileOffset - fileOffset) * 100) / std::max<qint64>(1, safeLength));
				emit chunkReady({}, std::clamp(percent, 0, 100));
				continue;
			}

			std::vector<DisasmInstr> portion;
			portion.reserve(256);

			while(bytesLeft > tailRequired
				&& !cancelFlag->load(std::memory_order_relaxed)) {
				if(!cs_disasm_iter(session.handle, &scanPointer, &bytesLeft, &addressForDecode, tempInstruction)) {
					break;
				}

				DisasmInstr insn;
				insn.address = static_cast<quint64>(tempInstruction->address);
				insn.mnemonic = QString::fromLatin1(tempInstruction->mnemonic);
				insn.opStr = QString::fromLatin1(tempInstruction->op_str);
				insn.size = tempInstruction->size;
				insn.bytes = QByteArray(
					reinterpret_cast<const char*>(tempInstruction->bytes),
					static_cast<int>(tempInstruction->size));

				portion.push_back(std::move(insn));
			}

			if(!isLastChunk && bytesLeft > 0) {
				const int keep = static_cast<int>(
					std::min<size_t>(bytesLeft, static_cast<size_t>(carryTailBytes)));
				carryBuffer = QByteArray(reinterpret_cast<const char*>(scanPointer), keep);
			} else {
				carryBuffer.clear();
			}
			currentFileOffset += toRead;

			const int percent = static_cast<int>(
				((currentFileOffset - fileOffset) * 100) / std::max<qint64>(1, safeLength));
			emit chunkReady(std::move(portion), std::clamp(percent, 0, 100));
		}

		cs_free(tempInstruction, 1);
		emit finished();
	}
};

