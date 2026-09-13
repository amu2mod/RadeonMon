#pragma once

#include "radeonmon/JpegEncoder.hpp"
#include "radeonmon/PngEncoder.hpp"

#include <windows.h>

class Screenshot
{
  public:
	enum Format
	{
		BMP,
		JPEG,
		PNG
	};

	Format m_format = BMP;

	Screenshot();
	bool GetScreenshot();
	bool BurstScreenshot(int n);
	inline const wchar_t *GetPath() const { return m_path; }
	inline bool IsPathEmpty() const { return m_path[0] == '\0'; }
	bool SetPath(const wchar_t *newPath);
	static constexpr DWORD MIN_INTERVAL_MS = 500; // Minimum 500 ms between shots (2 per second max)

  private:
	struct ScreenshotBuffer
	{
		std::vector<BYTE> pixels;
		int width = 0;
		int height = 0;
		std::wstring filename;
	};

	wchar_t m_path[MAX_PATH] = {};
	wchar_t m_filename[256];
	DWORD m_lastScreenshotTime = 0;
	JpegEncoder m_jpegEncoder;
	PngEncoder m_pngEncoder;

	// fallback process retriever using win32u.dll
	using NtUserQueryWindow_t = ULONG_PTR(WINAPI *)(HWND, ULONG);
	NtUserQueryWindow_t m_NtUserQueryWindow = nullptr;

	// cache
	HWND m_lastHwnd = nullptr;
	wchar_t m_lastProcessName[MAX_PATH] = L"unknown";
	static constexpr size_t LASTPROCESSNAMECOUNT = _countof(m_lastProcessName);

	static constexpr DWORD BURST_INTERVAL_MS = 1000;
	DWORD m_lastBurstTime;

	bool SaveBitmapToFile(const ScreenshotBuffer &buffer, const wchar_t *filePath);
	void UpdateFilenameWithForegroundProcess(HWND);
	bool EncodeFileAsJPEG(const wchar_t *filePath); // encode and deletes the raw file;
	bool EncodeFileAsPNG(const wchar_t *filePath);	// encode and deletes the raw file;
	DWORD GetProcessIdFromWindow(HWND hwnd);		// New helper to find PID with fallback solution
	const wchar_t *GetCachedProcessName(HWND hwnd);
	void StripUnrealSuffix(wchar_t *name);
	bool CaptureScreenshot(ScreenshotBuffer &buffer);

	// TODO: HDR-SDR tonemapper
};