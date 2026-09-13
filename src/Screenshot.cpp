#include "radeonmon/Screenshot.hpp"
#include "radeonmon/logging.hpp"

bool Screenshot::CaptureScreenshot(ScreenshotBuffer &output)
{
	HWND hwnd = GetForegroundWindow();

	if (!hwnd)
		return false;

	RECT clientRect{};

	if (!GetClientRect(hwnd, &clientRect))
		return false;

	const int width = clientRect.right - clientRect.left;
	const int height = clientRect.bottom - clientRect.top;

	if (width <= 0 || height <= 0)
		return false;

	POINT screenPos{clientRect.left, clientRect.top};

	if (!ClientToScreen(hwnd, &screenPos))
		return false;

	HDC hScreenDC = GetDC(nullptr);

	if (!hScreenDC)
		return false;

	HDC hMemDC = CreateCompatibleDC(hScreenDC);

	if (!hMemDC)
	{
		ReleaseDC(nullptr, hScreenDC);
		return false;
	}

	HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);

	if (!hBitmap)
	{
		DeleteDC(hMemDC);
		ReleaseDC(nullptr, hScreenDC);
		return false;
	}

	HGDIOBJ oldBitmap = SelectObject(hMemDC, hBitmap);

	const BOOL result = BitBlt(hMemDC, 0, 0, width, height, hScreenDC, screenPos.x, screenPos.y, SRCCOPY | CAPTUREBLT);

	if (!result)
	{
		DeleteObject(hBitmap);
		DeleteDC(hMemDC);
		ReleaseDC(nullptr, hScreenDC);
		return false;
	}

	BITMAPINFOHEADER bi{};
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = -height;
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;

	const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;

	output.pixels.resize(imageSize);

	const int scanLines = GetDIBits(hScreenDC, hBitmap, 0, height, output.pixels.data(), reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS);

	SelectObject(hMemDC, oldBitmap);
	DeleteObject(hBitmap);
	DeleteDC(hMemDC);
	ReleaseDC(nullptr, hScreenDC);

	if (scanLines != height)
	{
		output.pixels.clear();
		return false;
	}

	output.width = width;
	output.height = height;

	UpdateFilenameWithForegroundProcess(hwnd);

	output.filename = m_filename;

	return true;
}

bool Screenshot::SetPath(const wchar_t *newPath)
{
	if (newPath == nullptr || newPath[0] == L'\0')
		return false;

	const size_t len = wcslen(newPath);

	// Check that the path exists and is a directory.
	const DWORD attributes = GetFileAttributesW(newPath);
	if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	// Check write access before modifying the path.
	if (_waccess_s(newPath, 2) != 0)
		return false;

	// Copy the path, appending '\' if necessary.
	if (len > 0 && (newPath[len - 1] == L'\\' || newPath[len - 1] == L'/'))
	{
		if (wcscpy_s(m_path, _countof(m_path), newPath) != 0)
			return false;
	}
	else
	{
		if (wcscpy_s(m_path, _countof(m_path), newPath) != 0)
			return false;

		if (wcscat_s(m_path, _countof(m_path), L"\\") != 0)
			return false;
	}

	return true;
}

bool Screenshot::SaveBitmapToFile(const ScreenshotBuffer &screenshot, const wchar_t *filePath)
{
	if (filePath == nullptr)
		return false;

	const int width = screenshot.width;
	const int height = screenshot.height;

	if (width <= 0 || height <= 0)
	{
		LOG_ERROR("[Screenshot] Invalid bitmap dimensions: %dx%d", width, height);
		return false;
	}

	const size_t rowSize = static_cast<size_t>(width) * 4;
	const size_t imageSize = rowSize * static_cast<size_t>(height);

	if (screenshot.pixels.size() != imageSize)
	{
		LOG_ERROR("[Screenshot] Invalid pixel buffer size: %zu, expected %zu", screenshot.pixels.size(), imageSize);
		return false;
	}

	BITMAPINFOHEADER bi{};
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = -height; // Top-down
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = static_cast<DWORD>(imageSize);

	BITMAPFILEHEADER bmfHeader{};
	bmfHeader.bfType = 0x4D42; // "BM"
	bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	bmfHeader.bfSize = bmfHeader.bfOffBits + static_cast<DWORD>(imageSize);

	HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		LOG_ERROR("[Screenshot] CreateFileW failed. Error: %lu", GetLastError());
		return false;
	}

	bool success = true;
	DWORD written = 0;

	// Write BMP file header.
	if (!WriteFile(hFile, &bmfHeader, sizeof(bmfHeader), &written, nullptr) || written != sizeof(bmfHeader))
	{
		success = false;
	}

	// Write DIB header.
	if (success)
		if (!WriteFile(hFile, &bi, sizeof(bi), &written, nullptr) || written != sizeof(bi))
			success = false;

	// Write pixel data.
	if (success)
		if (!WriteFile(hFile, screenshot.pixels.data(), static_cast<DWORD>(imageSize), &written, nullptr) || written != imageSize)
			success = false;

	const DWORD error = success ? ERROR_SUCCESS : GetLastError();

	CloseHandle(hFile);

	if (!success)
	{
		LOG_ERROR("[Screenshot] WriteFile failed. Error: %lu", error);
		return false;
	}

	LOG_INFO("[Screenshot] Successfully saved as %ls", screenshot.filename.c_str());

	return true;
}

bool Screenshot::EncodeFileAsJPEG(const wchar_t *filePath) { return m_jpegEncoder.Queue(filePath); }

bool Screenshot::EncodeFileAsPNG(const wchar_t *filePath) { return m_pngEncoder.Queue(filePath); }

void Screenshot::UpdateFilenameWithForegroundProcess(HWND hwnd)
{
	SYSTEMTIME st;
	GetLocalTime(&st);

	if (hwnd == nullptr)
	{
		LOG_ERROR("[Screenshot] handle parameter is null");
		return;
	}

	// START_CHRONO(getname);
	const wchar_t *processName = GetCachedProcessName(hwnd);
	// END_CHRONO(getname, "GetCachedProcessName");

	swprintf_s(m_filename, L"%ls_%04d-%02d-%02d_%02d-%02d-%02d-%03d.bmp", processName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

Screenshot::Screenshot()
{
	HMODULE win32u = LoadLibraryW(L"win32u.dll");

	if (!win32u)
	{
		LOG_ERROR("win32u.dll not found");
		return;
	}

	m_NtUserQueryWindow = reinterpret_cast<NtUserQueryWindow_t>(GetProcAddress(win32u, "NtUserQueryWindow"));

	if (!m_NtUserQueryWindow)
	{
		LOG_ERROR("NtUserQueryWindow not found");
		return;
	}
}

DWORD Screenshot::GetProcessIdFromWindow(HWND hwnd)
{
	DWORD pid = 0;

	// Fast (documented path)
	if (GetWindowThreadProcessId(hwnd, &pid) && pid)
		return pid;

	// Undocumented Fallback
	if (m_NtUserQueryWindow)
	{
		pid = static_cast<DWORD>(m_NtUserQueryWindow(hwnd, 0)); // 0 for window PID

		if (pid)
			return pid;
	}

	return 0;
}

const wchar_t *Screenshot::GetCachedProcessName(HWND hwnd)
{
	if (hwnd == nullptr)
		return L"unknown";

	// Same window as last time.
	if (hwnd == m_lastHwnd)
		return m_lastProcessName;

	m_lastHwnd = hwnd;

	DWORD processId = GetProcessIdFromWindow(hwnd);

	wcscpy_s(m_lastProcessName, LASTPROCESSNAMECOUNT, L"unknown");

	if (processId == 0)
	{
		LOG_ERROR("[Screenshot] GetProcessIdFromWindow failed: error %lu", GetLastError());
		return m_lastProcessName;
	}

	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

	if (!process)
		return m_lastProcessName;

	wchar_t processPath[MAX_PATH] = {};
	DWORD pathSize = _countof(processPath);

	if (QueryFullProcessImageNameW(process, 0, processPath, &pathSize))
	{
		const wchar_t *name = wcsrchr(processPath, L'\\');
		name = name ? name + 1 : processPath;

		wcscpy_s(m_lastProcessName, LASTPROCESSNAMECOUNT, name);

		// Strip ".exe"
		wchar_t *extension = wcsrchr(m_lastProcessName, L'.');

		if (extension && _wcsicmp(extension, L".exe") == 0)
			*extension = L'\0';

		StripUnrealSuffix(m_lastProcessName);
	}

	CloseHandle(process);

	return m_lastProcessName;
}

void Screenshot::StripUnrealSuffix(wchar_t *name)
{
	static constexpr struct
	{
		const wchar_t *value;
		size_t length;
	} suffixes[] = {
		{L"-Win64-Shipping", _countof(L"-Win64-Shipping") - 1}, {L"-Win64-Test", _countof(L"-Win64-Test") - 1}, {L"-Win64-Development", _countof(L"-Win64-Development") - 1}, {L"-Win64-DebugGame", _countof(L"-Win64-DebugGame") - 1}, {L"-Win64-Debug", _countof(L"-Win64-Debug") - 1},
	};

	static constexpr size_t suffixCount = _countof(suffixes);
	static constexpr size_t minSuffixLength = 11; // "-Win64-Test"

	const size_t nameLength = wcslen(name);

	if (nameLength < minSuffixLength)
		return;

	for (size_t i = 0; i < suffixCount; ++i)
	{
		const auto &suffix = suffixes[i];

		if (nameLength >= suffix.length && _wcsicmp(name + nameLength - suffix.length, suffix.value) == 0)
		{
			name[nameLength - suffix.length] = L'\0';
			return;
		}
	}
}

bool Screenshot::BurstScreenshot(int n)
{
	const DWORD now = GetTickCount();

	// antispam
	if (m_lastBurstTime != 0 && (now - m_lastBurstTime) < BURST_INTERVAL_MS)
	{
		LOG_WARN("[Screenshot] Burst antispam triggered");
		return false;
	}

	if (n <= 0)
	{
		LOG_ERROR("[Screenshot] Invalid burst count: %d", n);
		return false;
	}

	if (IsPathEmpty())
	{
		LOG_ERROR("[Screenshot] path empty");
		return false;
	}

	// Prevent unreasonable memory usage.
	// Adjust this limit as appropriate for your application.
	constexpr int MAX_BURST_SCREENSHOTS = 60;

	if (n > MAX_BURST_SCREENSHOTS)
	{
		LOG_ERROR("[Screenshot] Burst count too large: %d (max %d)", n, MAX_BURST_SCREENSHOTS);
		return false;
	}

	std::vector<ScreenshotBuffer> screenshots;
	screenshots.reserve(n);

	// ---------------------------------------------------------
	// 1. CAPTURE ALL FRAMES INTO MEMORY
	// ---------------------------------------------------------

	const auto start = std::chrono::steady_clock::now();

	// Use a floating-point duration so 60 FPS is ~16.667 ms
	// rather than being truncated to 16 ms.
	const auto interval = std::chrono::duration<double>(1.0 / static_cast<double>(n));

	for (int i = 0; i < n; ++i)
	{
		ScreenshotBuffer screenshot;

		if (!CaptureScreenshot(screenshot))
		{
			LOG_ERROR("[Screenshot] Burst capture failed at frame %d/%d", i + 1, n);
			return false;
		}

		screenshots.emplace_back(std::move(screenshot));

		// Keep capture timing independent from previous capture duration.
		if (i + 1 < n)
		{
			const auto target = start + interval * static_cast<double>(i + 1);

			std::this_thread::sleep_until(target);
		}
	}

	// ---------------------------------------------------------
	// 2. WRITE ALL BMPs
	// ---------------------------------------------------------

	for (size_t i = 0; i < screenshots.size(); ++i)
	{
		const auto &screenshot = screenshots[i];

		const std::wstring fullPath = std::wstring(m_path) + screenshot.filename;

		if (!SaveBitmapToFile(screenshot, fullPath.c_str()))
		{
			LOG_ERROR("[Screenshot] Failed to save BMP %zu/%zu: %ls", i + 1, screenshots.size(), fullPath.c_str());

			return false;
		}
	}

	// ---------------------------------------------------------
	// 3. QUEUE ALL ENCODING
	// ---------------------------------------------------------

	for (size_t i = 0; i < screenshots.size(); ++i)
	{
		const auto &screenshot = screenshots[i];

		const std::wstring fullPath = std::wstring(m_path) + screenshot.filename;

		if (m_format == JPEG)
		{
			if (!EncodeFileAsJPEG(fullPath.c_str()))
			{
				LOG_ERROR("[Screenshot] Failed to queue JPEG %zu/%zu: %ls", i + 1, screenshots.size(), fullPath.c_str());
			}
		}
		else if (m_format == PNG)
		{
			if (!EncodeFileAsPNG(fullPath.c_str()))
			{
				LOG_ERROR("[Screenshot] Failed to queue PNG %zu/%zu: %ls", i + 1, screenshots.size(), fullPath.c_str());
			}
		}
	}

	m_lastScreenshotTime = GetTickCount();

	return true;
}

bool Screenshot::GetScreenshot()
{
	if (IsPathEmpty())
	{
		LOG_ERROR("[Screenshot] path empty");
		return false;
	}

	const DWORD now = GetTickCount();

	if (m_lastScreenshotTime != 0 && (now - m_lastScreenshotTime) < MIN_INTERVAL_MS)
	{
		LOG_WARN("[Screenshot] Antispam triggered");
		return false;
	}

	ScreenshotBuffer screenshot;

	if (!CaptureScreenshot(screenshot))
	{
		LOG_ERROR("[Screenshot] CaptureScreenshot failed");
		return false;
	}

	const std::wstring fullPath = std::wstring(m_path) + screenshot.filename;

	// Always dump the BMP first.
	if (!SaveBitmapToFile(screenshot, fullPath.c_str()))
	{
		LOG_ERROR("[Screenshot] SaveBitmapToFile failed: %ls", fullPath.c_str());
		return false;
	}

	// Then queue the encoder.
	if (m_format == JPEG)
	{
		if (!EncodeFileAsJPEG(fullPath.c_str()))
		{
			LOG_ERROR("[Screenshot] Failed to queue JPEG encoding: %ls", fullPath.c_str());
			return false;
		}
	}
	else if (m_format == PNG)
	{
		if (!EncodeFileAsPNG(fullPath.c_str()))
		{
			LOG_ERROR("[Screenshot] Failed to queue PNG encoding: %ls", fullPath.c_str());
			return false;
		}
	}

	m_lastScreenshotTime = GetTickCount();

	return true;
}
