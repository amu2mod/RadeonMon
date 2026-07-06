#pragma once

#include <Windows.h>
#include <cstdint>

extern UINT g_dpi;

enum class PropertyType
{
    Text,
    Separator
};

struct PropertyItem
{
    LPCWSTR label;
    int value;
    WCHAR textValue[32];
    RECT labelRc;
    RECT valueRc;
    PropertyType type = PropertyType::Text;
    bool dirty = true;
    bool warning = false;
    bool initialized = false;

    void SetValue(const char *src)
    {
        if (!src)
        {
            textValue[0] = L'\0';
            dirty = true;
            return;
        }

        MultiByteToWideChar(CP_UTF8, 0, src, -1, textValue, 128);

        textValue[31] = L'\0';
        dirty = true;
    }
};

struct GdiBackBuffer
{
    HDC memDC = nullptr;
    HBITMAP bitmap = nullptr;
    HBITMAP oldBitmap = nullptr;
    HBRUSH bgBrush = nullptr;

    int width = 0;
    int height = 0;

    void Create(HDC referenceDC, int w, int h, COLORREF bgColor)
    {
        Destroy(); // safe re-init

        width = w;
        height = h;

        memDC = CreateCompatibleDC(referenceDC);

        bitmap = CreateCompatibleBitmap(referenceDC, w, h);
        oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

        bgBrush = CreateSolidBrush(bgColor);

        RECT rc{0, 0, w, h};
        FillRect(memDC, &rc, bgBrush);
    }

    void Destroy()
    {
        if (!memDC)
            return;

        if (memDC)
        {
            if (oldBitmap)
                SelectObject(memDC, oldBitmap);

            if (bitmap)
            {
                DeleteObject(bitmap);
                bitmap = nullptr;
            }

            DeleteDC(memDC);
            memDC = nullptr;
        }

        if (bgBrush)
        {
            DeleteObject(bgBrush);
            bgBrush = nullptr;
        }

        oldBitmap = nullptr;
        width = height = 0;
    }

    ~GdiBackBuffer()
    {
        Destroy();
    }
};

enum AdlxStates
{
    Error = -1,
    NotSupported = -2
};

struct GpuMetricsSnapshot
{
    bool valid = false;

    int temperature = NotSupported;
    int hotspot = NotSupported;
    int vram = NotSupported;
    int power = NotSupported;
    int fanSpeed = NotSupported;
};

enum GPU_CAPS : uint32_t
{
    // V0
    GPU_CAP_USAGE = 1 << 0,
    GPU_CAP_CLOCK = 1 << 1,
    GPU_CAP_VRAM_CLOCK = 1 << 2,
    GPU_CAP_TEMP = 1 << 3,
    GPU_CAP_HOTSPOT = 1 << 4,
    GPU_CAP_POWER = 1 << 5,
    GPU_CAP_BOARD_POWER = 1 << 6,
    GPU_CAP_FAN_SPEED = 1 << 7,
    GPU_CAP_VRAM_USAGE = 1 << 8,
    GPU_CAP_VOLTAGE = 1 << 9,
    GPU_CAP_INTAKE_TEMP = 1 << 10,

    // V1
    GPU_CAP_MEM_TEMP = 1 << 11,
    GPU_CAP_NPU_FREQ = 1 << 12,
    GPU_CAP_NPU_ACTIVITY = 1 << 13,

    // V2
    GPU_CAP_SHARED_MEMORY = 1 << 14,

    // V3
    GPU_CAP_FAN_DUTY = 1 << 15
};