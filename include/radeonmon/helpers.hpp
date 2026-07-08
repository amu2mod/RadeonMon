#pragma once

#include <Windows.h>
#include <cstddef>
#include "shellscalingapi.h"
#include <commctrl.h>

#include "radeonmon/constants.hpp"
#include "radeonmon/structures.hpp"

#pragma comment(lib, "Comctl32.lib")

extern UINT g_dpi;
extern HFONT g_font;
struct PropertyItem;

inline int DPIScale(int v)
{
    return MulDiv(v, g_dpi, 96);
}

inline void RecreateFont()
{
    if (g_font)
        DeleteObject(g_font);

    if (g_notificationFont)
        DeleteObject(g_notificationFont);

    int fontSize = -MulDiv(g_fontSize, g_dpi, 96);
    g_font = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, FONT_FAMILY);
    int notificationFontSize = -MulDiv(g_notificationFontSize, g_dpi, 96);
    g_notificationFont = CreateFontW(notificationFontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, FONT_FAMILY);
    LOG_DEBUG("Font created: %d (%d dpi), notification font: %d", fontSize, g_dpi, notificationFontSize);
}

template <size_t N>
void MarkAllPropsDirty(PropertyItem (&props)[N])
{
    for (auto &p : props)
        p.dirty = true;
}

inline bool FormatTemperature(wchar_t *buffer, int temp)
{
    constexpr int MAXBUFSIZE = 6; // 150°C

    if (temp < 0 || temp > 150)
    {
        buffer[0] = L'n';
        buffer[1] = L'/';
        buffer[2] = L'a';
        buffer[3] = L'\0';
        return false;
    }

    wchar_t *p = buffer;

    if (temp >= 100)
        *p++ = static_cast<wchar_t>(L'0' + temp / 100);

    if (temp >= 10)
        *p++ = L'0' + (temp / 10) % 10;

    *p++ = L'0' + temp % 10;
    *p++ = L'°';
    *p++ = L'C';

    constexpr int MAX = MAXBUFSIZE - 1;
    while (p - buffer < MAX)
        *p++ = L' ';

    *p = L'\0';
    return true;
}

inline bool FormatHotspot(wchar_t *buffer, int temp, int hotspot)
{
    constexpr int MAXBUFSIZE = 13; // 150°C (+100)

    if (hotspot < 0 || hotspot > 150)
    {
        buffer[0] = L'n';
        buffer[1] = L'/';
        buffer[2] = L'a';
        buffer[3] = L'\0';
        return false;
    }

    wchar_t *p = buffer;

    int delta = hotspot - temp;

    // format hotspot temperature first
    if (hotspot >= 100)
    {
        *p++ = static_cast<wchar_t>(L'0' + (hotspot / 100));
        *p++ = static_cast<wchar_t>(L'0' + ((hotspot / 10) % 10));
        *p++ = static_cast<wchar_t>(L'0' + (hotspot % 10));
    }
    else if (hotspot >= 10)
    {
        *p++ = static_cast<wchar_t>(L'0' + (hotspot / 10));
        *p++ = static_cast<wchar_t>(L'0' + (hotspot % 10));
    }
    else
    {
        *p++ = static_cast<wchar_t>(L'0' + hotspot);
    }

    *p++ = L'°';
    *p++ = L'C';
    *p++ = L' ';

    *p++ = L'(';

    // format delta
    if (delta < 0)
    {
        *p++ = L'-';
        delta = -delta;
    }
    else
    {
        *p++ = L'+';
    }

    if (delta >= 100)
    {
        *p++ = static_cast<wchar_t>(L'0' + (delta / 100));
        *p++ = static_cast<wchar_t>(L'0' + ((delta / 10) % 10));
        *p++ = static_cast<wchar_t>(L'0' + (delta % 10));
    }
    else if (delta >= 10)
    {
        *p++ = static_cast<wchar_t>(L'0' + (delta / 10));
        *p++ = static_cast<wchar_t>(L'0' + (delta % 10));
    }
    else
    {
        *p++ = static_cast<wchar_t>(L'0' + delta);
    }

    *p++ = L')';

    constexpr int MAX = MAXBUFSIZE - 1;
    while (static_cast<int>(p - buffer) < MAX)
    {
        *p++ = L' ';
    }

    *p = L'\0';
    return true;
}

inline bool FormatFanSpeed(wchar_t *buffer, int rpm)
{
    constexpr int MAXBUFSIZE = 9; // 9999 RPM

    if (rpm < 0 || rpm > 9999)
    {
        buffer[0] = L'n';
        buffer[1] = L'/';
        buffer[2] = L'a';
        buffer[3] = L'\0';
        return false;
    }

    wchar_t *p = buffer;

    if (rpm >= 1000)
    {
        *p++ = static_cast<wchar_t>(L'0' + (rpm / 1000));
        *p++ = static_cast<wchar_t>(L'0' + ((rpm / 100) % 10));
        *p++ = static_cast<wchar_t>(L'0' + ((rpm / 10) % 10));
        *p++ = static_cast<wchar_t>(L'0' + (rpm % 10));
    }
    else if (rpm >= 100)
    {
        *p++ = static_cast<wchar_t>(L'0' + (rpm / 100));
        *p++ = static_cast<wchar_t>(L'0' + ((rpm / 10) % 10));
        *p++ = static_cast<wchar_t>(L'0' + (rpm % 10));
    }
    else if (rpm >= 10)
    {
        *p++ = static_cast<wchar_t>(L'0' + (rpm / 10));
        *p++ = static_cast<wchar_t>(L'0' + (rpm % 10));
    }
    else
    {
        *p++ = static_cast<wchar_t>(L'0' + rpm);
    }

    *p++ = L' ';
    *p++ = L'R';
    *p++ = L'P';
    *p++ = L'M';

    constexpr int MAX = MAXBUFSIZE - 1;
    while (static_cast<int>(p - buffer) < MAX)
    {
        *p++ = L' ';
    }

    *p = L'\0';
    return true;
}

inline bool FormatPowerConsumption(wchar_t *buffer, int watts)
{
    constexpr int MAXBUFSIZE = 6; // 999 W

    if (watts < 0 || watts > 999)
    {
        buffer[0] = L'n';
        buffer[1] = L'/';
        buffer[2] = L'a';
        buffer[3] = L'\0';
        return false;
    }

    wchar_t *p = buffer;

    if (watts >= 100)
    {
        *p++ = static_cast<wchar_t>(L'0' + (watts / 100));
        *p++ = static_cast<wchar_t>(L'0' + ((watts / 10) % 10));
        *p++ = static_cast<wchar_t>(L'0' + (watts % 10));
    }
    else if (watts >= 10)
    {
        *p++ = static_cast<wchar_t>(L'0' + (watts / 10));
        *p++ = static_cast<wchar_t>(L'0' + (watts % 10));
    }
    else
    {
        *p++ = static_cast<wchar_t>(L'0' + watts);
    }

    *p++ = L' ';
    *p++ = L'W';

    constexpr int MAX = MAXBUFSIZE - 1;
    while (static_cast<int>(p - buffer) < MAX)
    {
        *p++ = L' ';
    }

    *p = L'\0';
    return true;
}

inline bool FormatCpuMetrics(wchar_t *buffer, int temp, int power)
{
    constexpr int MAXBUFSIZE = 13; // 150°C, 999 W

    if (temp < 0 || temp > 150 || power < 0 || power > 999)
    {
        buffer[0] = L'n';
        buffer[1] = L'/';
        buffer[2] = L'a';
        buffer[3] = L'\0';
        return false;
    }

    wchar_t *p = buffer;

    // Temperature
    if (temp >= 100)
        *p++ = static_cast<wchar_t>(L'0' + temp / 100);

    if (temp >= 10)
        *p++ = L'0' + (temp / 10) % 10;

    *p++ = L'0' + temp % 10;
    *p++ = L'°';
    *p++ = L'C';

    // Power
    *p++ = L',';
    *p++ = L' ';

    if (power >= 100)
        *p++ = static_cast<wchar_t>(L'0' + power / 100);

    if (power >= 10)
        *p++ = L'0' + (power / 10) % 10;

    *p++ = L'0' + power % 10;
    *p++ = L' ';
    *p++ = L'W';

    // Padding
    constexpr int MAX = MAXBUFSIZE - 1;
    while (p - buffer < MAX)
        *p++ = L' ';

    *p = L'\0';

    return true;
}

inline bool IsAMDVendor(const char *vendorId)
{
    if (vendorId == nullptr)
        return false;

    return std::strcmp(vendorId, "1002") == 0;
}

inline bool IsRunningAsAdministrator()
{
    HANDLE hToken = nullptr;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken))
        return false;

    TOKEN_ELEVATION elevation;
    DWORD size = sizeof(elevation);

    BOOL success = GetTokenInformation(
        hToken,
        TokenElevation,
        &elevation,
        sizeof(elevation),
        &size);

    CloseHandle(hToken);

    return success && elevation.TokenIsElevated;
}

inline UINT getDpiFromPoint(POINT pt)
{
    HMONITOR hMonitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);

    if (!hMonitor)
        return 96;

    UINT dpiX = 96;
    UINT dpiY = 96;

    GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

    return dpiX;
}

inline bool isPointValid(POINT pt)
{
    HMONITOR monitor = MonitorFromPoint(pt, MONITOR_DEFAULTTONULL);
    return monitor != nullptr;
}

#ifdef _DEBUG
inline void DebugRect(const char *name, const RECT &rc)
{
    LOG_DEBUG(
        "%s: {%ld, %ld, %ld, %ld}  w=%ld h=%ld",
        name,
        rc.left,
        rc.top,
        rc.right,
        rc.bottom,
        rc.right - rc.left,
        rc.bottom - rc.top);
}
#endif

inline HRESULT CALLBACK DialogCallback(HWND hwnd, UINT msg, [[maybe_unused]] WPARAM wParam, [[maybe_unused]] LPARAM lParam, [[maybe_unused]] LONG_PTR refData)
{
    if (msg == TDN_HYPERLINK_CLICKED)
    {
        ShellExecuteW(hwnd, L"open", (LPCWSTR)lParam, nullptr, nullptr, SW_SHOWNORMAL);
    }

    return S_OK;
}

inline void ShowUpdateDialog(HWND hwnd, const wchar_t *title, const wchar_t *instruction, const std::wstring &content, bool error = false)
{
    TASKDIALOGCONFIG config = {};
    config.cbSize = sizeof(config);
    config.hwndParent = hwnd;
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_ENABLE_HYPERLINKS;
    config.dwCommonButtons = TDCBF_OK_BUTTON;
    config.pszWindowTitle = title;
    config.pszMainInstruction = instruction;
    config.pszContent = content.c_str();
    config.pszMainIcon = error ? TD_ERROR_ICON : TD_INFORMATION_ICON;
    config.pfCallback = DialogCallback;

    TaskDialogIndirect(&config, nullptr, nullptr, nullptr);
}