#include "radeonmon/Screenshot.hpp"
#include "radeonmon/logging.hpp"

bool Screenshot::GetScreenshot()
{
    if (IsPathEmpty())
    {
        LOG_ERROR("[Screenshot] path empty");
        return false;
    }

    const DWORD now = GetTickCount();
    if (lastScreenshotTime != 0 && (now - lastScreenshotTime) < MIN_INTERVAL_MS)
    {
        LOG_WARN("[Screenhot] Antispam triggered");
        return false; // Too soon;
    }

    // Get the foreground window
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
    {
        LOG_ERROR("[Screenhot] GetForegroundWindow failed");
        return false;
    }

    // Get the client area dimensions
    RECT clientRect{};
    if (!GetClientRect(hwnd, &clientRect))
    {
        LOG_ERROR("[Screenhot] GetClientRect failed. Error: %d", GetLastError());
        return false;
    }

    const int width = clientRect.right - clientRect.left;
    const int height = clientRect.bottom - clientRect.top;

    if (width <= 0 || height <= 0)
    {
        LOG_ERROR("[Screenhot] Invalid client dimensions: %dx%d", width, height);
        return false;
    }

    // Convert client (0,0) to screen coordinates
    POINT screenPos{clientRect.left, clientRect.top};

    if (!ClientToScreen(hwnd, &screenPos))
    {
        LOG_ERROR("[Screenhot] ClientToScreen failed. Error: %d", GetLastError());
        return false;
    }

    // Get screen DC
    HDC hScreenDC = GetDC(nullptr);
    if (!hScreenDC)
    {
        LOG_ERROR("[Screenhot] GetDC failed");
        return false;
    }

    // Create memory DC
    HDC hMemDC = CreateCompatibleDC(hScreenDC);
    if (!hMemDC)
    {
        LOG_ERROR("[Screenhot] CreateCompatibleDC failed. Error: %d", GetLastError());
        ReleaseDC(nullptr, hScreenDC);
        return false;
    }

    // Create bitmap matching the client area
    HBITMAP hBitmap = CreateCompatibleBitmap(hScreenDC, width, height);

    if (!hBitmap)
    {
        LOG_ERROR("[Screenhot] CreateCompatibleBitmap failed. Error: %d", GetLastError());
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        return false;
    }

    // Select bitmap into memory DC
    HGDIOBJ oldBitmap = SelectObject(hMemDC, hBitmap);

    if (!oldBitmap)
    {
        LOG_ERROR("[Screenhot] SelectObject failed. Error: %d", GetLastError());
        DeleteObject(hBitmap);
        DeleteDC(hMemDC);
        ReleaseDC(nullptr, hScreenDC);
        return false;
    }

    // Capture only the client/game area
    BOOL result = BitBlt(hMemDC, 0, 0, width, height, hScreenDC, screenPos.x, screenPos.y, SRCCOPY | CAPTUREBLT);

    auto end = std::chrono::steady_clock::now();

    if (!result)
        LOG_ERROR("[Screenhot] BitBlt failed. Error: %d", GetLastError());

    // Save bitmap
    UpdateFilenameWithForegroundProcess(hwnd);
    std::wstring fullPath = std::wstring(path) + filename;

    if (!SaveBitmapToFile(hBitmap, fullPath.c_str()))
        LOG_ERROR("[Screenhot] SaveBitmapToFile failed");
    else
    {
        if (m_format == JPEG)
        {
            if (!EncodeFileAsJPEG(fullPath.c_str()))
                LOG_ERROR("Failed to queue JPEG encoding: %ls", fullPath.c_str());
        }
        else if (m_format == PNG)
        {
            if (!EncodeFileAsPNG(fullPath.c_str()))
                LOG_ERROR("Failed to queue JPEG encoding: %ls", fullPath.c_str());
        }
    }

    // Restore original bitmap
    SelectObject(hMemDC, oldBitmap);

    // Cleanup
    DeleteObject(hBitmap);
    DeleteDC(hMemDC);
    ReleaseDC(nullptr, hScreenDC);

    lastScreenshotTime = now;

    return true;
}

bool Screenshot::SetPath(const wchar_t *newPath)
{
    if (newPath == nullptr || newPath[0] == L'\0')
        return false;

    const size_t len = wcslen(newPath);

    // Check that the path exists and is a directory.
    const DWORD attributes = GetFileAttributesW(newPath);
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        !(attributes & FILE_ATTRIBUTE_DIRECTORY))
        return false;

    // Check write access before modifying the path.
    if (_waccess_s(newPath, 2) != 0)
        return false;

    // Copy the path, appending '\' if necessary.
    if (len > 0 && (newPath[len - 1] == L'\\' || newPath[len - 1] == L'/'))
    {
        if (wcscpy_s(path, _countof(path), newPath) != 0)
            return false;
    }
    else
    {
        if (wcscpy_s(path, _countof(path), newPath) != 0)
            return false;

        if (wcscat_s(path, _countof(path), L"\\") != 0)
            return false;
    }

    return true;
}

bool Screenshot::SaveBitmapToFile(HBITMAP hBitmap, const wchar_t *filePath)
{
    if (!hBitmap || !filePath)
        return false;

    BITMAP bmp{};
    if (GetObject(hBitmap, sizeof(BITMAP), &bmp) == 0)
    {
        LOG_ERROR("[Screenhot] GetObject failed. Error: {%d}", GetLastError());
        return false;
    }

    const int width = bmp.bmWidth;
    const int height = bmp.bmHeight;

    if (width <= 0 || height <= 0)
    {
        LOG_ERROR("[Screenhot] Invalid bitmap dimensions: %dx%d", width, height);
        return false;
    }

    // LOG_DEBUG("[Screenhot] Saving bitmap: %dx%d", width, height);

    // 32-bit top-down bitmap.
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // Top-down
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    const DWORD rowSize = static_cast<DWORD>(width) * 4;
    const DWORD imageSize = rowSize * static_cast<DWORD>(height);

    BYTE *lpBits = new BYTE[imageSize];

    // Get a DC for GetDIBits.
    HDC hDC = GetDC(nullptr);
    if (!hDC)
    {
        LOG_ERROR("[Screenhot] GetDC failed. Error: {%d}", GetLastError());
        delete[] lpBits;
        return false;
    }

    // Extract bitmap pixels.
    int scanLines = GetDIBits(hDC, hBitmap, 0, height, lpBits, reinterpret_cast<BITMAPINFO *>(&bi), DIB_RGB_COLORS);

    ReleaseDC(nullptr, hDC);

    if (scanLines == 0)
    {
        LOG_ERROR("[Screenhot] GetDIBits failed. Error: {%d}", GetLastError());
        delete[] lpBits;
        return false;
    }

    // Create output file.
    HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (hFile == INVALID_HANDLE_VALUE)
    {
        LOG_ERROR("[Screenhot] CreateFileW failed. Error: {%d}", GetLastError());
        delete[] lpBits;
        return false;
    }

    BITMAPFILEHEADER bmfHeader{};
    bmfHeader.bfType = 0x4D42; // "BM"

    bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    bmfHeader.bfSize = bmfHeader.bfOffBits + imageSize;

    DWORD written = 0;
    bool success = true;

    // Write BMP file header.
    if (!WriteFile(hFile, &bmfHeader, sizeof(bmfHeader), &written, nullptr) || written != sizeof(bmfHeader))
        success = false;

    // Write DIB header.
    if (success)
        if (!WriteFile(hFile, &bi, sizeof(bi), &written, nullptr) || written != sizeof(bi))
            success = false;

    // Write pixel data.
    if (success)
        if (!WriteFile(hFile, lpBits, imageSize, &written, nullptr) || written != imageSize)
            success = false;

    CloseHandle(hFile);
    delete[] lpBits;

    if (!success)
    {
        LOG_ERROR("[Screenhot] WriteFile failed. Error: {%d}", GetLastError());
        return false;
    }

    LOG_INFO("[Screenhot] Successfully saved as %ls", filename);

    return true;
}

bool Screenshot::EncodeFileAsJPEG(const wchar_t *filePath)
{
    return m_jpegEncoder.Queue(filePath);
}

bool Screenshot::EncodeFileAsPNG(const wchar_t *filePath)
{
    return m_pngEncoder.Queue(filePath);
}

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

    swprintf_s(filename,
               L"%ls_%04d-%02d-%02d_%02d-%02d-%02d-%03d.bmp",
               processName,
               st.wYear,
               st.wMonth,
               st.wDay,
               st.wHour,
               st.wMinute,
               st.wSecond,
               st.wMilliseconds);
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
    } suffixes[] =
        {
            {L"-Win64-Shipping", _countof(L"-Win64-Shipping") - 1},
            {L"-Win64-Test", _countof(L"-Win64-Test") - 1},
            {L"-Win64-Development", _countof(L"-Win64-Development") - 1},
            {L"-Win64-DebugGame", _countof(L"-Win64-DebugGame") - 1},
            {L"-Win64-Debug", _countof(L"-Win64-Debug") - 1},
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
