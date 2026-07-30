#include "TempMMapPageStore.h"

#ifdef Q_OS_WIN
#  define NOMINMAX
#  include <windows.h>
#  include <io.h>//_get_osfhandle
#endif
#ifdef Q_OS_UNIX
#  include <unistd.h>//pread,pwrite
#  include <fcntl.h>
#  include <errno.h>//errno, EINTR
#endif

//системная гранулярность
static qint64 sysGran() {
#ifdef Q_OS_WIN
	SYSTEM_INFO si; ::GetSystemInfo(&si);
	return qint64(si.dwAllocationGranularity);
#else
	long ps = ::sysconf(_SC_PAGESIZE);
	return ps > 0 ? qint64(ps) : qint64(4096);
#endif
}

qint64 TempMMapPageStore::systemGranularity() { 
	return sysGran(); 
}

TempMMapPageStore::TempMMapPageStore()
	: m_gran(systemGranularity()) {

	//создаём temp-файл
	const QString path = QDir::temp().absoluteFilePath(
		QUuid::createUuid().toString(QUuid::WithoutBraces) + ".hexpages");
	m_file.setFileName(path);
	if(!m_file.open(QIODevice::ReadWrite)) {
		m_fileSize = 0;
		m_nextOff = 0;
		return;
	}

#ifdef Q_OS_UNIX
	m_posixFd = m_file.handle();//qt даёт актуальный fd
#endif
#ifdef Q_OS_WIN
	//открываем нативный OVERLAPPED handle к тому же файлу
	const QString nativePath = m_file.fileName();
	const std::wstring widePath = nativePath.toStdWString();
	HANDLE overlappedHandle = ::CreateFileW(
		widePath.c_str(),
		GENERIC_READ | GENERIC_WRITE,
		FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED | FILE_FLAG_RANDOM_ACCESS,
		nullptr);
	if(overlappedHandle == INVALID_HANDLE_VALUE) {
		//если не удалось — закрываем файл и помечаем стор неготовым
		m_file.close();
		m_fileSize = 0;
		m_nextOff = 0;
		m_winHandle = nullptr;
		return;
	}
	m_winHandle = overlappedHandle;
#endif

	//выравниваем следующий оффсет по гранулярности
	m_fileSize = m_file.size();
	const qint64 rem = (m_fileSize % m_gran);
	m_nextOff = rem ? (m_fileSize + (m_gran - rem)) : m_fileSize;
}

TempMMapPageStore::~TempMMapPageStore() {
	QMutexLocker resizeLock(&m_resizeMutex);

	const QString name = m_file.fileName();
	if(m_file.isOpen()) 
		m_file.close();

#ifdef Q_OS_WIN
	if(m_winHandle) {
		::CloseHandle(reinterpret_cast<HANDLE>(m_winHandle));
		m_winHandle = nullptr;
	}
#endif
#ifdef Q_OS_UNIX
	m_posixFd = -1;//qt сам закроет через m_file
#endif

	if(!name.isEmpty()) 
		QFile::remove(name);
}

PageHandle TempMMapPageStore::allocate(qint64 size, const char* initData) {

	if(size <= 0) 
		return {};

	//рост файла и разметка выполняются под отдельным локом
	QMutexLocker lock(&m_resizeMutex);

	const qint64 aligned = ((size + (m_gran - 1)) / m_gran) * m_gran;
	const qint64 off = m_nextOff;
	const qint64 end = off + aligned;

	if(end > m_fileSize) {
		if(!m_file.resize(end)) 
			return {};
		m_fileSize = end;
	}
	m_nextOff = end;

	if(initData) {
		const qint64 absoluteOffset = off;

#ifdef Q_OS_UNIX
		if(m_posixFd < 0) {
			//откат разметки
			m_nextOff = off;
			if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
			return {};
		}
		const char* sourcePtr = initData;
		qint64 bytesRemaining = size;
		qint64 currentOffset = absoluteOffset;

		while(bytesRemaining > 0) {
			ssize_t writeResult = ::pwrite(m_posixFd, sourcePtr, size_t(bytesRemaining), off_t(currentOffset));
			if(writeResult < 0) {
				if(errno == EINTR) continue;//повторить попытку
				//неожиданная ошибка
				m_nextOff = off;
				if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
				return {};
			}
			if(writeResult == 0) {
				//нулевая запись — считаем ошибкой
				m_nextOff = off;
				if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
				return {};
			}
			sourcePtr += writeResult;
			currentOffset += writeResult;
			bytesRemaining -= writeResult;
		}
#endif

#ifdef Q_OS_WIN
		if(!m_winHandle) {
			m_nextOff = off;
			if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
			return {};
		}

		const char* sourcePtr = initData;
		qint64 bytesRemaining = size;
		qint64 currentOffset = absoluteOffset;
		const DWORD maxChunkBytes = 1u << 20;//1 МБ

		while(bytesRemaining > 0) {
			const DWORD chunkBytes = DWORD(std::min<qint64>(bytesRemaining, qint64(maxChunkBytes)));

			OVERLAPPED overlapped{};
			overlapped.Offset = DWORD(currentOffset & 0xFFFFFFFFULL);
			overlapped.OffsetHigh = DWORD((currentOffset >> 32) & 0xFFFFFFFFULL);

			DWORD bytesWritten = 0;
			const BOOL okWrite = ::WriteFile(
				reinterpret_cast<HANDLE>(m_winHandle),
				sourcePtr,
				chunkBytes,
				&bytesWritten,
				&overlapped);

			if(!okWrite) {
				const DWORD lastError = ::GetLastError();
				if(lastError == ERROR_IO_PENDING) {
					if(!::GetOverlappedResult(reinterpret_cast<HANDLE>(m_winHandle), &overlapped, &bytesWritten, TRUE)) {
						m_nextOff = off;
						if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
						return {};
					}
				} else {
					m_nextOff = off;
					if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
					return {};
				}
			}

			if(bytesWritten == 0) {
				m_nextOff = off;
				if(end == m_fileSize) { m_file.resize(off); m_fileSize = off; }
				return {};
			}

			sourcePtr += bytesWritten;
			currentOffset += bytesWritten;
			bytesRemaining -= bytesWritten;
		}
#endif
	}
	return PageHandle{off, size};
}

qint64 TempMMapPageStore::read(const PageHandle& pageHandle, qint64 pageOffset, qint64 lengthToRead, char* destination) const {
	if(!pageHandle.isValid() || !destination || pageOffset < 0 || lengthToRead <= 0 || pageOffset >= pageHandle.m_size)
		return 0;

	const qint64 takeBytes = std::min<qint64>(lengthToRead, pageHandle.m_size - pageOffset);
	const qint64 absoluteOffset = pageHandle.m_offset + pageOffset;

#ifdef Q_OS_UNIX
	if(m_posixFd < 0) return 0;
	ssize_t readResult = ::pread(m_posixFd, destination, size_t(takeBytes), off_t(absoluteOffset));
	return (readResult > 0) ? qint64(readResult) : 0;
#endif

#ifdef Q_OS_WIN
	if(!m_winHandle) return 0;
	OVERLAPPED overlapped{};
	overlapped.Offset = DWORD(absoluteOffset & 0xFFFFFFFFULL);
	overlapped.OffsetHigh = DWORD((absoluteOffset >> 32) & 0xFFFFFFFFULL);

	DWORD bytesRead = 0;
	const BOOL okRead = ::ReadFile(
		reinterpret_cast<HANDLE>(m_winHandle),
		destination,
		DWORD(takeBytes),
		&bytesRead,
		&overlapped);
	if(!okRead) {
		const DWORD err = ::GetLastError();
		if(err == ERROR_IO_PENDING) {
			//синхронно дожидаемся завершения операции
			if(::GetOverlappedResult(reinterpret_cast<HANDLE>(m_winHandle), &overlapped, &bytesRead, TRUE))
				return qint64(bytesRead);
			return 0;
		}
		return 0;
	}
	return qint64(bytesRead);
#endif
}


qint64 TempMMapPageStore::write(const PageHandle& pageHandle, qint64 pageOffset, QByteArrayView source) {
	if(!pageHandle.isValid() || source.isEmpty() || pageOffset < 0 || pageOffset >= pageHandle.m_size)
		return 0;

	const qint64 takeBytes = std::min<qint64>(qint64(source.size()), pageHandle.m_size - pageOffset);
	const qint64 absoluteOffset = pageHandle.m_offset + pageOffset;

#ifdef Q_OS_UNIX
	if(m_posixFd < 0) return 0;
	ssize_t writeResult = ::pwrite(m_posixFd, source.data(), size_t(takeBytes), off_t(absoluteOffset));
	return (writeResult > 0) ? qint64(writeResult) : 0;
#endif

#ifdef Q_OS_WIN
	if(!m_winHandle) return 0;
	OVERLAPPED overlapped{};
	overlapped.Offset = DWORD(absoluteOffset & 0xFFFFFFFFULL);
	overlapped.OffsetHigh = DWORD((absoluteOffset >> 32) & 0xFFFFFFFFULL);

	DWORD bytesWritten = 0;
	const BOOL okWrite = ::WriteFile(
		reinterpret_cast<HANDLE>(m_winHandle),
		source.data(),
		DWORD(takeBytes),
		&bytesWritten,
		&overlapped);
	if(!okWrite) {
		const DWORD err = ::GetLastError();
		if(err == ERROR_IO_PENDING) {
			if(::GetOverlappedResult(reinterpret_cast<HANDLE>(m_winHandle), &overlapped, &bytesWritten, TRUE))
				return qint64(bytesWritten);
			return 0;
		}
		return 0;
	}
	return qint64(bytesWritten);
#endif
}


qint64 TempMMapPageStore::fill(const PageHandle& pageHandle, qint64 pageOffset, qint64 lengthToFill, QByteArrayView pattern) {
	if(!pageHandle.isValid() || lengthToFill <= 0 || pattern.isEmpty() || pageOffset < 0 || pageOffset >= pageHandle.m_size)
		return 0;

	const qint64 takeBytes = std::min<qint64>(lengthToFill, pageHandle.m_size - pageOffset);
	const qsizetype patternSize = pattern.size();

	QByteArray stagingBuffer(qsizetype(std::min<qint64>(takeBytes, 1ll << 16)), Qt::Uninitialized);
	qint64 totalWritten = 0;

	while(totalWritten < takeBytes) {
		const qint64 chunkBytes = std::min<qint64>(takeBytes - totalWritten, qint64(stagingBuffer.size()));

		for(qint64 elementIndex = 0; elementIndex < chunkBytes; ++elementIndex) {
			stagingBuffer[qsizetype(elementIndex)] = pattern[qsizetype((totalWritten + elementIndex) % patternSize)];
		}

		const qint64 absoluteOffset = pageHandle.m_offset + pageOffset + totalWritten;

#ifdef Q_OS_UNIX
		if(m_posixFd < 0) break;
		ssize_t writeResult = ::pwrite(m_posixFd, stagingBuffer.constData(), size_t(chunkBytes), off_t(absoluteOffset));
		if(writeResult <= 0) break;
		totalWritten += qint64(writeResult);
#endif

#ifdef Q_OS_WIN
		if(!m_winHandle) break;

		OVERLAPPED overlapped{};
		overlapped.Offset = DWORD(absoluteOffset & 0xFFFFFFFFULL);
		overlapped.OffsetHigh = DWORD((absoluteOffset >> 32) & 0xFFFFFFFFULL);

		DWORD bytesWritten = 0;
		const BOOL okWrite = ::WriteFile(
			reinterpret_cast<HANDLE>(m_winHandle),
			stagingBuffer.constData(),
			DWORD(chunkBytes),
			&bytesWritten,
			&overlapped);

		if(!okWrite) {
			const DWORD lastError = ::GetLastError();
			if(lastError == ERROR_IO_PENDING) {
				if(!::GetOverlappedResult(reinterpret_cast<HANDLE>(m_winHandle), &overlapped, &bytesWritten, TRUE))
					break;
			} else {
				break;
			}
		}

		if(bytesWritten == 0)
			break;

		totalWritten += qint64(bytesWritten);
#endif

	}

	return totalWritten;
}


void TempMMapPageStore::free(const PageHandle& /*h*/) {
	//temp-файл будет удалён в деструкторе
}
