#include "DisasmEngine.h"

#include <QMetaType>
#include "IDataProvider.h"
#include <algorithm>
#include "DisasmWorker.h"

DisasmEngine::DisasmEngine(QObject* parent,
	std::shared_ptr<const IDataProvider> dataProvider,
	qint64 fileOffset,
	qint64 byteLength,
	Arch architecture,
	bool bigEndian,
	int chunkSizeBytes)
	: QObject(parent),
	m_dataProvider(dataProvider),
	m_fileOffset(fileOffset),
	m_byteLength(byteLength),
	m_baseAddress(static_cast<quint64>(fileOffset)),
	m_architecture(architecture),
	m_bigEndian(bigEndian),
	m_chunkSizeBytes(chunkSizeBytes),
	m_carryTailBytes(computeCarryTail(architecture)) {
	qRegisterMetaType<std::vector<DisasmInstr>>("std::vector<DisasmInstr>");
}

DisasmEngine::~DisasmEngine() {
	cancel();
}

void DisasmEngine::start() {
	if(m_thread != nullptr) {
		return;
	}

	m_cancelRequested.store(false, std::memory_order_relaxed);

	m_thread = new QThread(this);
	auto* worker = new DisasmWorker();
	worker->dataProvider = m_dataProvider;
	worker->fileOffset = m_fileOffset;
	worker->byteLength = m_byteLength;
	worker->baseAddress = m_baseAddress;
	worker->architecture = m_architecture;
	worker->bigEndian = m_bigEndian;
	worker->chunkSizeBytes = m_chunkSizeBytes;
	worker->carryTailBytes = m_carryTailBytes;
	worker->cancelFlag = &m_cancelRequested;

	worker->moveToThread(m_thread);

	QObject::connect(m_thread, &QThread::started, worker, &DisasmWorker::run);
	QObject::connect(worker, &DisasmWorker::chunkReady, this, &DisasmEngine::chunkReady, Qt::QueuedConnection);
	QObject::connect(worker, &DisasmWorker::error, this, &DisasmEngine::error, Qt::QueuedConnection);
	QObject::connect(worker, &DisasmWorker::finished, this, [this] {
		emit finished();
		if(m_thread != nullptr) {
			m_thread->quit();
		}
		}, Qt::QueuedConnection);


	QObject::connect(m_thread, &QThread::finished, worker, &QObject::deleteLater);
	QObject::connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
	QObject::connect(m_thread, &QThread::finished, this, [this] {
		//обнуляем указатель на поток
		m_thread = nullptr;

		//если был запрошен перезапуск — сбрасываем флаг и запускаем снова
		if(m_restartPending) {
			m_restartPending = false;
			start();
		}
		});

	m_thread->start();
}

void DisasmEngine::cancel() {
	m_cancelRequested.store(true, std::memory_order_relaxed);
	if(m_thread != nullptr) {
		m_thread->quit();
		m_thread->wait();
		m_thread = nullptr;
	}
}

int DisasmEngine::computeCarryTail(Arch architecture) {
	switch(architecture) {
	case Arch::X86_16:
	case Arch::X86_32:
	case Arch::X86_64:
		return 15; //максимальная длина x86-инструкции
	case Arch::ARM:
	case Arch::ARM_THUMB:
	case Arch::ARM64:
		return 4;  //достаточно для кода ARM/A64
	}
	return 16;
}

void DisasmEngine::refresh() {
	//если уже идёт дизасм — помечаем перезапуск и отменяем текущий прогон
	if(m_thread != nullptr) {
		m_restartPending = true;
		cancel();
		return;
	}
	//ничего не идёт — стартуем сразу
	start();
}
