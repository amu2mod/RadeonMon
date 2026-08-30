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
    if (lastScreenshotTime != 0 && (now - lastScreenshotTime) < minIntervalMs)
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
    UpdateFilenameWithForegroundProcess();
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

void Screenshot::UpdateFilenameWithForegroundProcess()
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t processName[MAX_PATH] = L"unknown";

    HWND hwnd = GetForegroundWindow();
    if (hwnd)
    {
        DWORD processId = 0;

        if (GetWindowThreadProcessId(hwnd, &processId) != 0)
        {
            HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

            if (process)
            {
                wchar_t processPath[MAX_PATH];
                DWORD pathSize = _countof(processPath);

                if (QueryFullProcessImageNameW(
                        process, 0, processPath, &pathSize))
                {
                    const wchar_t *name = wcsrchr(processPath, L'\\');
                    name = name ? name + 1 : processPath;

                    wcscpy_s(processName, _countof(processName), name);

                    // Strip ".exe"
                    wchar_t *extension = wcsrchr(processName, L'.');
                    if (extension && _wcsicmp(extension, L".exe") == 0)
                        *extension = L'\0';
                }

                CloseHandle(process);
            }
        }
    }

    // Format: process_YYYYMMDD_HHMMSS_mmm.bmp
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

bool Screenshot::EncodeFileAsJPEG(const wchar_t *filePath)
{
    return m_jpegEncoder.Queue(filePath);
}

bool Screenshot::EncodeFileAsPNG(const wchar_t *filePath)
{
    return m_pngEncoder.Queue(filePath);
}
