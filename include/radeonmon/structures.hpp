#pragma once

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <cstdint>
#include <string>

#include <ifdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include "radeonmon/logging.hpp"

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
    int value2;
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

        MultiByteToWideChar(CP_UTF8, 0, src, -1, textValue, _countof(textValue));

        dirty = true;
    }

    void SetValue(const wchar_t *src)
    {
        if (src)
        {
            wcsncpy_s(textValue, _countof(textValue), src, _TRUNCATE);
        }
        else
        {
            textValue[0] = L'\0';
        }

        dirty = true;
    }

    void DrawTextValue(HDC hdc, COLORREF color, HFONT font)
    {
        HFONT oldFont = (HFONT)SelectObject(hdc, font);
        COLORREF oldColor = SetTextColor(hdc, color);
        int oldBkMode = SetBkMode(hdc, TRANSPARENT);

        DrawTextW(hdc, textValue, -1, &valueRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        SetBkMode(hdc, oldBkMode);
        SetTextColor(hdc, oldColor);
        SelectObject(hdc, oldFont);
    }

    void ClearRC(HDC hdc, COLORREF color)
    {
        HBRUSH brush = CreateSolidBrush(color);
        FillRect(hdc, &valueRc, brush);
        DeleteObject(brush);
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

        LOG_DEBUG("Creating back buffer: %dx%d", w, h);

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

    // Builds a JSON representation of this snapshot. Unsupported metrics
    // are emitted as JSON null rather than the internal NotSupported
    // sentinel value, so the frontend doesn't need to know about it.
    std::string BuildJson() const
    {
        char buffer[256];

        auto field = [](int value) -> std::string
        {
            if (value == NotSupported)
                return "null";
            return std::to_string(value);
        };

        snprintf(buffer, sizeof(buffer),
                 "{\"valid\":%s,\"temperature\":%s,\"hotspot\":%s,\"vram\":%s,\"power\":%s,\"fan_speed\":%s}",
                 valid ? "true" : "false",
                 field(temperature).c_str(),
                 field(hotspot).c_str(),
                 field(vram).c_str(),
                 field(power).c_str(),
                 field(fanSpeed).c_str());

        return std::string(buffer);
    }
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

enum RyzenCpuState
{
    AdminRequired = -3,
    SdkRequired = -4,
};

enum class RyzenMetricState
{
    NotSupported = -1,
    Error = 0,
};

struct RyzenMetrics
{
    double dTemperature = 0.0;
    double dPower = 0.0;

    std::string BuildJson() const
    {
        char buffer[128];
        snprintf(buffer, sizeof(buffer),
                 "{\"temperature\":%.1f,\"power\":%.1f}",
                 dTemperature, dPower);
        return std::string(buffer);
    }
};

struct WindowBorder
{
    RECT top{};
    RECT bottom{};
    RECT left{};
    RECT right{};
};

struct NetworkInterface
{
    NET_LUID luid{};
    std::wstring adapterName;
    std::wstring address;

    bool operator==(const NetworkInterface &other) const
    {
        return luid.Value == other.luid.Value && address == other.address;
    }

    std::wstring display() const
    {
        return adapterName + L": " + address;
    }
};