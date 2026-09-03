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

public:
    Format m_format = BMP;

    bool GetScreenshot();
    inline const wchar_t *GetPath() const { return path; }
    inline bool IsPathEmpty() const { return path[0] == '\0'; }
    bool SetPath(const wchar_t *newPath);
    static constexpr DWORD MIN_INTERVAL_MS = 500; // Minimum 500 ms between shots (2 per second max)

private:
    wchar_t path[MAX_PATH] = {};
    wchar_t filename[256];
    DWORD lastScreenshotTime = 0;
    JpegEncoder m_jpegEncoder;
    PngEncoder m_pngEncoder;

    bool SaveBitmapToFile(HBITMAP hBitmap, const wchar_t *filePath);
    void UpdateFilenameWithForegroundProcess();
    bool EncodeFileAsJPEG(const wchar_t *filePath); // encode and deletes the raw file;
    bool EncodeFileAsPNG(const wchar_t *filePath);  // encode and deletes the raw file;

    // TODO: HDR-SDR tonemapper
};