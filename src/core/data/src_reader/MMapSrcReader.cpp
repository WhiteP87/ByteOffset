#include "MMapSrcReader.h"
#include <QCryptographicHash>

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#  include <memoryapi.h>
#endif
#ifdef Q_OS_UNIX
#  include <unistd.h>
#  include <fcntl.h>
#  include <sys/mman.h>
#endif

qint64 MMapSrcReader::systemMapGranularity() {
#ifdef Q_OS_WIN
	SYSTEM_INFO si; ::GetSystemInfo(&si);
	return qint64(si.dwAllocationGranularity);
#else
	const long page = ::sysconf(_SC_PAGESIZE);
	return page > 0 ? qint64(page) : qint64(4096);
#endif
}

qint64 MMapSrcReader::alignDown(qint64 value, qint64 alignment) {
	if(alignment <= 0) 
		return value;

	const qint64 rem = value % alignment;
	return value - (rem < 0 ? rem + alignment : rem);
}

MMapSrcReader::MMapSrcReader(QString filePath, qint64 windowSizeBytes)
	: m_mapGranularity(systemMapGranularity())
	, m_windowSize((windowSizeBytes > 0) ? windowSizeBytes : m_mapGranularity)
	, m_filePath(std::move(filePath))
	, m_file(m_filePath) {
	if(m_windowSize < m_mapGranularity) {
		const_cast<qint64&>(m_windowSize) = m_mapGranularity;
	}
	if(!m_file.open(QIODevice::ReadOnly)) {
		m_fileSize = 0;
		return;
	}
	m_fileSize = m_file.size();
	//m_Hash = getHash();
	{
		QMutexLocker lk(&m_windowsMutex);
		m_windows.reserve(std::max(8, QThreadPool::globalInstance()->maxThreadCount() + 8));
	}
}

MMapSrcReader::~MMapSrcReader() {
	{
		QMutexLocker lock(&m_windowsMutex);
		for(auto it = m_windows.begin(); it != m_windows.end(); ++it) {
			Window& window = it.value();
			if(window.m_ptr) {
				QMutexLocker mapLock(&m_mapOpMutex);
				m_file.unmap(window.m_ptr);
				window.m_ptr = nullptr;
				window.m_base = 0;
				window.m_length = 0;
			}
		}
		m_windows.clear();
	}
	if(m_file.isOpen()) 
		m_file.close();
}

qint64 MMapSrcReader::size() const { return m_fileSize; }

QString MMapSrcReader::id() const {
	QFileInfo fi(m_filePath);
	const QString canon = fi.canonicalFilePath();
	return canon.isEmpty() ? fi.absoluteFilePath() : canon;
}

QByteArray MMapSrcReader::getHash() const {
	if(m_Hash.isEmpty() && m_fileSize <= (HASH_THRESHOLD)) {
		QCryptographicHash hasher(QCryptographicHash::Sha256);

		const qint64 totalSizeBytes = size();
		qint64 cursor = 0;

#ifdef Q_OS_UNIX
		const int fd = m_file.handle();
		if(fd >= 0) {
			(void)::posix_fadvise(fd, 0, 0, POSIX_FADV_SEQUENTIAL);
		}
#endif
		while(cursor < totalSizeBytes) {
			if(!ensureWindowFor(cursor)) break;

			//окно для текущего потока
			Window& window = const_cast<MMapSrcReader*>(this)->windowForCurrentThread();

			const qint64 inWindowOffset = cursor - window.m_base;
			const qint64 availableInWindow = window.m_length - inWindowOffset;
			const qint64 remaining = totalSizeBytes - cursor;
			const qint64 chunkBytes = std::min<qint64>(availableInWindow, remaining);


			const char* chunkPtr = reinterpret_cast<const char*>(window.m_ptr + inWindowOffset);
//			hasher.addData(chunkPtr, int(chunkBytes));
			hasher.addData(QByteArrayView{chunkPtr, int(chunkBytes)});

			cursor += chunkBytes;
		}
		m_Hash = std::move(hasher.result());
	}
	return m_Hash;
}

bool MMapSrcReader::writable() const {
	QFileInfo fi(m_file);
	return fi.isWritable();
}

MMapSrcReader::Window& MMapSrcReader::windowForCurrentThread() const {
	const Qt::HANDLE tid = QThread::currentThreadId();
	QMutexLocker lock(&m_windowsMutex);
	return m_windows[tid];
}

bool MMapSrcReader::ensureWindowFor(qint64 offset) const {
	if(offset < 0 || offset >= m_fileSize)
		return false;

	Window& window = windowForCurrentThread();
	if(window.m_ptr && offset >= window.m_base && offset < window.m_base + window.m_length)
		return true;

	const qint64 base = alignDown(offset, m_mapGranularity);
	qint64 len = std::min<qint64>(m_windowSize, m_fileSize - base);
	if(len <= 0) 
		return false;

	QMutexLocker mapLock(&m_mapOpMutex);

	if(window.m_ptr) {
		m_file.unmap(window.m_ptr);
		window.m_ptr = nullptr;
		window.m_base = 0;
		window.m_length = 0;
	}

	uchar* ptr = m_file.map(base, len);
	while(!ptr && len > m_mapGranularity) {
		len /= 2;

		if(len < m_mapGranularity) 
			len = m_mapGranularity;

		ptr = m_file.map(base, len);
	}
	if(!ptr) 
		return false;

	window.m_ptr = ptr;
	window.m_base = base;
	window.m_length = len;
	return true;
}

void MMapSrcReader::unmapWindow(Window& window) const {
	if(!window.m_ptr) 
		return;
	QMutexLocker mapLock(&m_mapOpMutex);
	m_file.unmap(window.m_ptr);
	window.m_ptr = nullptr;
	window.m_base = 0;
	window.m_length = 0;
}

quint8 MMapSrcReader::readByte(qint64 offset) const {
	if(!ensureWindowFor(offset)) 
		return 0;
	const Window& window = const_cast<MMapSrcReader*>(this)->windowForCurrentThread();
	const qint64 inWindow = offset - window.m_base;
	return *(window.m_ptr + inWindow);
}

qint64 MMapSrcReader::readRange(qint64 offset, qint64 length, QByteArray& out) const {
	if(length <= 0 || offset < 0 || offset >= m_fileSize) {
		out.clear();
		return 0; 
	}

	const qint64 maxLen = std::min<qint64>(length, m_fileSize - offset);
	out.resize(qsizetype(maxLen));

	qint64 remaining = maxLen;
	qint64 cursor = offset;
	qint64 copied = 0;

	while(remaining > 0) {
		if(!ensureWindowFor(cursor)) break;
		const Window& w = const_cast<MMapSrcReader*>(this)->windowForCurrentThread();
		const qint64 inWindow = cursor - w.m_base;
		const qint64 chunkSize = std::min<qint64>(remaining, w.m_length - inWindow);
		memcpy(out.data() + copied, w.m_ptr + inWindow, size_t(chunkSize));
		copied += chunkSize;
		cursor += chunkSize;
		remaining -= chunkSize;
	}

	if(copied < maxLen) 
		out.resize(qsizetype(copied));
	return copied;
}

qint64 MMapSrcReader::readRange(qint64 offset, qint64 length, char* destination) const {
	if(!destination || length <= 0) 
		return 0;

	if(offset < 0 || offset >= m_fileSize) 
		return 0;

	qint64 remaining = std::min<qint64>(length, m_fileSize - offset);
	qint64 cursor = offset;
	qint64 copied = 0;

	while(remaining > 0) {
		if(!ensureWindowFor(cursor)) 
			break;
		const Window& w = const_cast<MMapSrcReader*>(this)->windowForCurrentThread();
		const qint64 inWindow = cursor - w.m_base;
		const qint64 chunkSize = std::min<qint64>(remaining, w.m_length - inWindow);
		memcpy(destination + copied, w.m_ptr + inWindow, size_t(chunkSize));
		copied += chunkSize;
		cursor += chunkSize;
		remaining -= chunkSize;
	}

	return copied;
}

//размэпить все текущие окна (для всех потоков)
void MMapSrcReader::invalidateAllWindows() {
	//блокируем реестр окон
	QMutexLocker regLock(&m_windowsMutex);

	//проходим по всем окнам и размэпливаем
	for(auto it = m_windows.begin(); it != m_windows.end(); ++it) {
		Window& window = it.value();
		if(window.m_ptr) {
			//операции map/unmap защищаем отдельным мьютексом,
			//чтобы не пересечься с параллельным map()
			QMutexLocker mapLock(&m_mapOpMutex);
			m_file.unmap(window.m_ptr);
			window.m_ptr = nullptr;
			window.m_base = 0;
			window.m_length = 0;
		}
	}
}

