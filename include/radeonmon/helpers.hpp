#pragma once

#include <Windows.h>
#include <cstddef>
#include "shellscalingapi.h"
#include <commctrl.h>
#include <shellapi.h>

#include "radeonmon/constants.hpp"
#include "radeonmon/structures.hpp"
#include "radeonmon/globals.hpp"

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

template <size_t N>
inline bool FormatTemperature(wchar_t (&buffer)[N], int temp)
{
    constexpr auto MAXBUFSIZE = _countof(L"150°C"); // 6
    static_assert(N >= MAXBUFSIZE, "Buffer too small for FormatTemperature");

    constexpr int CONTENT_WIDTH = MAXBUFSIZE - 1; // 5

    wchar_t *p = buffer;

    if (temp < 0 || temp > 150)
    {
        *p++ = L'n';
        *p++ = L'/';
        *p++ = L'a';
        *p++ = L' ';
        *p++ = L' ';
        *p = L'\0';
        return false;
    }

    if (temp >= 100)
        *p++ = static_cast<wchar_t>(L'0' + temp / 100);

    if (temp >= 10)
        *p++ = static_cast<wchar_t>(L'0' + (temp / 10) % 10);

    *p++ = static_cast<wchar_t>(L'0' + temp % 10);
    *p++ = L'°';
    *p++ = L'C';

    while (p - buffer < CONTENT_WIDTH)
        *p++ = L' ';

    *p = L'\0';
    return true;
}

template <size_t N>
inline bool FormatHotspot(wchar_t (&buffer)[N], int temp, int hotspot)
{
    constexpr auto MAXBUFSIZE = _countof(L"150°C (+100)"); // 13
    static_assert(N >= MAXBUFSIZE, "Buffer too small for FormatHotspot");

    constexpr int CONTENT_WIDTH = MAXBUFSIZE - 1; // 12

    wchar_t *p = buffer;

    // Invalid hotspot → "n/a         "
    if (hotspot < 0 || hotspot > 150)
    {
        *p++ = L'n';
        *p++ = L'/';
        *p++ = L'a';
        while (p - buffer < CONTENT_WIDTH)
            *p++ = L' ';
        *p = L'\0';
        return false;
    }

    // Format hotspot temperature
    if (hotspot >= 100)
    {
        *p++ = L'0' + static_cast<wchar_t>(hotspot / 100);
        *p++ = L'0' + ((hotspot / 10) % 10);
        *p++ = L'0' + (hotspot % 10);
    }
    else if (hotspot >= 10)
    {
        *p++ = L'0' + static_cast<wchar_t>(hotspot / 10);
        *p++ = L'0' + (hotspot % 10);
    }
    else
    {
        *p++ = L'0' + static_cast<wchar_t>(hotspot);
    }

    *p++ = L'°';
    *p++ = L'C';
    *p++ = L' ';
    *p++ = L'(';

    // Format delta
    int delta = hotspot - temp;

    if (delta < 0)
    {
        *p++ = L'-';
        delta = -delta;
    }
    else
    {
        *p++ = L'+';
    }

    // Limit delta to reasonable range (e.g. -999 to +999)
    if (delta > 999)
        delta = 999;

    if (delta >= 100)
    {
        *p++ = L'0' + static_cast<wchar_t>(delta / 100);
        *p++ = L'0' + ((delta / 10) % 10);
        *p++ = L'0' + (delta % 10);
    }
    else if (delta >= 10)
    {
        *p++ = L'0' + static_cast<wchar_t>(delta / 10);
        *p++ = L'0' + (delta % 10);
    }
    else
    {
        *p++ = L'0' + static_cast<wchar_t>(delta);
    }

    *p++ = L')';

    // Pad to fixed width
    while (p - buffer < CONTENT_WIDTH)
        *p++ = L' ';

    *p = L'\0';
    return true;
}

template <size_t N>
inline bool FormatFanSpeed(wchar_t (&buffer)[N], int rpm)
{
    constexpr auto MAXBUFSIZE = _countof(L"9999 RPM"); // 9 (8 chars + null)
    static_assert(N >= MAXBUFSIZE, "Buffer too small for FormatFanSpeed");

    constexpr int CONTENT_WIDTH = MAXBUFSIZE - 1; // 8

    wchar_t *p = buffer;

    if (rpm < 0 || rpm > 9999)
    {
        *p++ = L'n';
        *p++ = L'/';
        *p++ = L'a';
        while (p - buffer < CONTENT_WIDTH)
            *p++ = L' ';
        *p = L'\0';
        return false;
    }

    // Format RPM number
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

    // Pad to fixed width
    while (p - buffer < CONTENT_WIDTH)
        *p++ = L' ';

    *p = L'\0';
    return true;
}

template <size_t N>
inline bool FormatPowerConsumption(wchar_t (&buffer)[N], int watts)
{
    constexpr auto MAXBUFSIZE = _countof(L"999 W"); // 6 (5 chars + null)
    static_assert(N >= MAXBUFSIZE, "Buffer too small for FormatPowerConsumption");

    constexpr int CONTENT_WIDTH = MAXBUFSIZE - 1; // 5

    wchar_t *p = buffer;

    if (watts < 0 || watts > 999)
    {
        *p++ = L'n';
        *p++ = L'/';
        *p++ = L'a';
        while (p - buffer < CONTENT_WIDTH)
            *p++ = L' ';
        *p = L'\0';
        return false;
    }

    // Format wattage
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

    // Pad to fixed width
    while (p - buffer < CONTENT_WIDTH)
        *p++ = L' ';

    *p = L'\0';
    return true;
}

template <size_t N>
inline bool FormatCpuMetrics(wchar_t (&buffer)[N], int temp, int power)
{
    constexpr auto MAXBUFSIZE = _countof(L"150°C, 999 W"); // 14 (13 chars + null)
    static_assert(N >= MAXBUFSIZE, "Buffer too small for FormatCpuMetrics");

    constexpr int CONTENT_WIDTH = MAXBUFSIZE - 1; // 13

    wchar_t *p = buffer;

    if (temp < 0 || temp > 150 || power < 0 || power > 999)
    {
        *p++ = L'n';
        *p++ = L'/';
        *p++ = L'a';
        while (p - buffer < CONTENT_WIDTH)
            *p++ = L' ';
        *p = L'\0';
        return false;
    }

    // Temperature part
    if (temp >= 100)
        *p++ = static_cast<wchar_t>(L'0' + temp / 100);

    if (temp >= 10)
        *p++ = static_cast<wchar_t>(L'0' + (temp / 10) % 10);

    *p++ = static_cast<wchar_t>(L'0' + temp % 10);
    *p++ = L'°';
    *p++ = L'C';

    // Separator
    *p++ = L',';
    *p++ = L' ';

    // Power part
    if (power >= 100)
        *p++ = static_cast<wchar_t>(L'0' + power / 100);

    if (power >= 10)
        *p++ = static_cast<wchar_t>(L'0' + (power / 10) % 10);

    *p++ = static_cast<wchar_t>(L'0' + power % 10);
    *p++ = L' ';
    *p++ = L'W';

    // Pad to fixed width
    while (p - buffer < CONTENT_WIDTH)
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

inline std::string BuildCombinedJson()
{
    std::string gpuJson = g_AdlxGPUTelemetry.isInitialized ? g_AdlxGPUTelemetry.Get().BuildJson() : "null";
    std::string cpuJson = g_cpu.IsInitialized() ? g_cpu.GetMetrics().BuildJson() : "null";

    char buffer[512];
    snprintf(buffer, sizeof(buffer), "{\"gpu\":%s,\"cpu\":%s}", gpuJson.c_str(), cpuJson.c_str());
    return std::string(buffer);
}

inline std::string LoadResourceString(int id)
{
    HMODULE module = GetModuleHandle(nullptr);
    HRSRC res = FindResource(module, MAKEINTRESOURCE(id), RT_RCDATA);

    if (!res)
        return {};

    HGLOBAL handle = LoadResource(module, res);
    DWORD size = SizeofResource(module, res);
    const char *data = static_cast<const char *>(LockResource(handle));

    return std::string(data, data + size);
}