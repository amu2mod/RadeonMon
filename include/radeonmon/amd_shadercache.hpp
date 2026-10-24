#pragma once

#include <Windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <cstdint>

class AmdShaderCache
{
public:
	enum class CacheType : uint8_t
	{
		DX9,
		DX11,
		DX12,
		OpenGL,
		Vulkan,

		Count
	};

	struct CacheInfo
	{
		CacheType type;
		std::wstring folderName;
		std::wstring APIName;
		double sizeGB = 0.0;
	};

	using CacheArray = std::array<CacheInfo, 5>;
	using ChangeCallback = std::function<void(HWND)>;

	AmdShaderCache();
	~AmdShaderCache();

	AmdShaderCache(const AmdShaderCache &) = delete;
	AmdShaderCache &operator=(const AmdShaderCache &) = delete;

	inline void Init(HWND hwnd) { m_hwnd = hwnd; };

	// Starts monitoring the AMD shader cache directories.
	bool Start(ChangeCallback callback);

	// Stops monitoring and joins the worker thread.
	void Stop();

	// Returns whether the monitoring worker is running.
	bool IsRunning() const;

	// Returns a thread-safe snapshot of the latest cache sizes.
	CacheArray GetCacheSizes() const;

private:
	static constexpr std::chrono::seconds ScanInterval{60};

	static constexpr const wchar_t *c_cacheFolders[] = {L"dx9cache", L"dxcache", L"dxccache", L"oglcache", L"vkcache"};
	static constexpr int c_folderCount = _countof(c_cacheFolders);
	static constexpr CacheType c_types[] = {CacheType::DX9, CacheType::DX11, CacheType::DX12, CacheType::OpenGL, CacheType::Vulkan};
	static constexpr const wchar_t *c_apis[] = {L"DirectX 9", L"DirectX 11", L"DirectX 12", L"OpenGL", L"Vulkan"};

	static_assert(_countof(c_types) == _countof(c_apis), "Cache c_types and API names must have the same size");
	static_assert(_countof(c_types) == static_cast<uint8_t>(CacheType::Count), "CacheType enum and arrays are out of sync");

	// Worker thread entry point.
	void WatchThread();

	// Resolves %LOCALAPPDATA%\AMD.
	bool ResolveCachePath();

	// Calculates a directory's total size in decimal GB.
	double CalculateDirectorySizeGB(const std::wstring &directory) const;

	// Scans all five cache folders.
	CacheArray ScanCacheSizes() const;

	// Compares sizes rounded to one decimal place in GB.
	bool HasCacheSizesChanged(const CacheArray &current, const CacheArray &previous) const;

	// Publishes a complete snapshot under the mutex.
	void UpdateCacheSizes(const CacheArray &sizes);

	// Notifies the application when required.
	void NotifyCallback(HWND);

	// Helpers
	bool IsCachePath(const std::wstring &relativePath);

	HWND m_hwnd;

	std::wstring m_cachePath;
	ChangeCallback m_callback;

	HANDLE m_directoryHandle = INVALID_HANDLE_VALUE;
	std::thread m_watchThread;

	std::atomic<bool> m_running{false};
	std::atomic<bool> m_stopRequested{false};

	// Protects the published cache snapshot.
	mutable std::mutex m_cacheMutex;
	CacheArray m_cacheSizes{};

	// Worker-thread state.
	std::chrono::steady_clock::time_point m_lastScanTime{};
	bool m_hasScanned = false;
	bool m_hasNotified = false;
};