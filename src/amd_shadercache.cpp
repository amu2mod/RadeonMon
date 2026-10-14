#include "radeonmon/amd_shadercache.hpp"

#include <cmath>
#include <cwchar>
#include <vector>

constexpr DWORD NotificationBufferSize = 64 * 1024;
constexpr double BytesPerGB = 1024.0 * 1024.0 * 1024.0;

bool IsDirectory(const std::wstring &path)
{
	const DWORD attributes = GetFileAttributesW(path.c_str());

	return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool AmdShaderCache::IsCachePath(const std::wstring &relativePath)
{
	const std::size_t separator = relativePath.find_first_of(L"\\/");

	const std::wstring firstComponent = relativePath.substr(0, separator);

	for (const wchar_t *folder : CacheFolders)
	{
		if (_wcsicmp(firstComponent.c_str(), folder) == 0)
			return true;
	}

	return false;
}

double RoundGB(double bytes) { return std::round((bytes / BytesPerGB) * 10.0) / 10.0; }

AmdShaderCache::AmdShaderCache() = default;

AmdShaderCache::~AmdShaderCache() { Stop(); }

bool AmdShaderCache::ResolveCachePath()
{
	const DWORD required = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);

	if (required == 0)
		return false;

	std::vector<wchar_t> buffer(required);

	const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));

	if (length == 0 || length >= buffer.size())
		return false;

	m_cachePath.assign(buffer.data(), length);
	m_cachePath += L"\\AMD";

	return IsDirectory(m_cachePath);
}

bool AmdShaderCache::Start(ChangeCallback callback)
{
	if (m_running.load())
		return false;

	if (m_watchThread.joinable())
		Stop();

	if (!callback || !ResolveCachePath())
		return false;

	HANDLE handle = CreateFileW(m_cachePath.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);

	if (handle == INVALID_HANDLE_VALUE)
		return false;

	m_callback = std::move(callback);
	m_directoryHandle = handle;

	m_stopRequested.store(false);
	m_running.store(true);

	m_lastScanTime = {};
	m_hasScanned = false;
	m_hasNotified = false;

	try
	{
		m_watchThread = std::thread(&AmdShaderCache::WatchThread, this);
	}
	catch (...)
	{
		CloseHandle(m_directoryHandle);
		m_directoryHandle = INVALID_HANDLE_VALUE;
		m_running.store(false);
		return false;
	}

	return true;
}

void AmdShaderCache::Stop()
{
	m_stopRequested.store(true);

	if (m_watchThread.joinable())
	{
		// Interrupt the synchronous ReadDirectoryChangesW call.
		CancelSynchronousIo(static_cast<HANDLE>(m_watchThread.native_handle()));

		m_watchThread.join();
	}

	if (m_directoryHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_directoryHandle);
		m_directoryHandle = INVALID_HANDLE_VALUE;
	}

	m_running.store(false);
	m_callback = nullptr;
}

bool AmdShaderCache::IsRunning() const { return m_running.load(); }

AmdShaderCache::CacheArray AmdShaderCache::GetCacheSizes() const
{
	std::lock_guard<std::mutex> lock(m_cacheMutex);
	return m_cacheSizes;
}

double AmdShaderCache::CalculateDirectorySizeGB(const std::wstring &directory) const
{
	// Iterative traversal avoids deep recursive C++ calls.
	std::vector<std::wstring> pending;
	pending.push_back(directory);

	long double totalBytes = 0.0L;

	while (!pending.empty())
	{
		std::wstring current = std::move(pending.back());
		pending.pop_back();

		const std::wstring searchPath = current + L"\\*";

		WIN32_FIND_DATAW data{};
		HANDLE findHandle = FindFirstFileW(searchPath.c_str(), &data);

		if (findHandle == INVALID_HANDLE_VALUE)
			continue;

		do
		{
			if (wcscmp(data.cFileName, L".") == 0 || wcscmp(data.cFileName, L"..") == 0)
			{
				continue;
			}

			const std::wstring childPath = current + L"\\" + data.cFileName;

			// Avoid following junctions and symbolic links.
			if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
				continue;

			if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			{
				pending.push_back(childPath);
			}
			else
			{
				ULARGE_INTEGER fileSize{};
				fileSize.HighPart = data.nFileSizeHigh;
				fileSize.LowPart = data.nFileSizeLow;

				totalBytes += fileSize.QuadPart;
			}

		} while (FindNextFileW(findHandle, &data));

		FindClose(findHandle);
	}

	return std::round(static_cast<double>(totalBytes / BytesPerGB) * 100.0) / 100.0;
}

AmdShaderCache::CacheArray AmdShaderCache::ScanCacheSizes() const
{
	CacheArray result{};

	for (std::size_t i = 0; i < FOLDER_COUNT; ++i)
	{
		result[i].type = types[i];
		result[i].folderName = CacheFolders[i];
		result[i].APIName = apis[i];

		const std::wstring directory = m_cachePath + L"\\" + CacheFolders[i];

		result[i].sizeGB = IsDirectory(directory) ? CalculateDirectorySizeGB(directory) : 0.0;
	}

	return result;
}

bool AmdShaderCache::HasCacheSizesChanged(const CacheArray &current, const CacheArray &previous) const
{
	for (std::size_t i = 0; i < current.size(); ++i)
	{
		// Sizes are already rounded to one decimal GB.
		if (current[i].sizeGB != previous[i].sizeGB)
			return true;
	}

	return false;
}

void AmdShaderCache::UpdateCacheSizes(const CacheArray &sizes)
{
	std::lock_guard<std::mutex> lock(m_cacheMutex);
	m_cacheSizes = sizes;
}

void AmdShaderCache::NotifyCallback(HWND hwnd)
{
	if (!m_callback)
		return;

	try
	{
		m_callback(hwnd);
	}
	catch (...)
	{
		// Prevent application callback exceptions from terminating
		// the worker thread.
	}
}

void AmdShaderCache::WatchThread()
{
	// Initial snapshot and notification.
	CacheArray previous = ScanCacheSizes();

	UpdateCacheSizes(previous);

	m_lastScanTime = std::chrono::steady_clock::now();
	m_hasScanned = true;
	m_hasNotified = true;

	NotifyCallback(m_hwnd);

	alignas(DWORD) BYTE buffer[NotificationBufferSize];

	while (!m_stopRequested.load())
	{
		DWORD bytesReturned = 0;

		const BOOL success = ReadDirectoryChangesW(m_directoryHandle, buffer, sizeof(buffer),
												   TRUE, // Watch subdirectories recursively.
												   FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION, &bytesReturned, nullptr, nullptr);

		if (!success)
		{
			if (m_stopRequested.load())
				break;

			// The watcher cannot reliably continue after an error.
			break;
		}

		if (bytesReturned == 0)
			continue;

		bool relevantChange = false;

		BYTE *current = buffer;

		for (;;)
		{
			const auto *info = reinterpret_cast<const FILE_NOTIFY_INFORMATION *>(current);

			std::wstring relativePath(info->FileName, info->FileNameLength / sizeof(WCHAR));

			if (IsCachePath(relativePath))
				relevantChange = true;

			if (info->NextEntryOffset == 0)
				break;

			current += info->NextEntryOffset;
		}

		if (!relevantChange)
			continue;

		const auto now = std::chrono::steady_clock::now();

		// Never scan more than once per 60-second interval.
		if (m_hasScanned && now - m_lastScanTime < ScanInterval)
		{
			continue;
		}

		CacheArray currentSizes = ScanCacheSizes();

		// Compare the rounded values against the last published snapshot.
		const bool changed = HasCacheSizesChanged(currentSizes, previous);

		// Publish a complete snapshot before notifying the application.
		UpdateCacheSizes(currentSizes);

		previous = currentSizes;
		m_lastScanTime = std::chrono::steady_clock::now();
		m_hasScanned = true;

		if (changed || !m_hasNotified)
		{
			m_hasNotified = true;
			NotifyCallback(m_hwnd);
		}
	}

	m_running.store(false);
}