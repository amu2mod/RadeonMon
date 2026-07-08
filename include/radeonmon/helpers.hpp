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

    int fontSize = -MulDiv(g_fontSize, g_dpi, 96);
    g_font = CreateFontW(fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, FONT_FAMILY);
    LOG_DEBUG("Font created: %d (%d dpi)", fontSize, g_dpi);
}

template <size_t N>
void MarkAllPropsDirty(PropertyItem (&props)[N])
{
    for (auto &p : props)
        p.dirty = true;
}

inline bool FormatTemperature(wchar_t *buffer, int temp)
{
    constexpr int MAXBUFSIZE = 14;

    if (temp == NotSupported)
    {
        // display "not supported"
        buffer[0] = L'n';
        buffer[1] = L'o';
        buffer[2] = L't';
        buffer[3] = L' ';
        buffer[4] = L's';
        buffer[5] = L'u';
        buffer[6] = L'p';
        buffer[7] = L'p';
        buffer[8] = L'o';
        buffer[9] = L'r';
        buffer[10] = L't';
        buffer[11] = L'e';
        buffer[12] = L'd';
        buffer[13] = L'\0';
        return false;
    }
    if (temp < NotSupported || temp == Error || temp > 150)
    {
        // display "error"
        buffer[0] = L'e';
        buffer[1] = L'r';
        buffer[2] = L'r';
        buffer[3] = L'o';
        buffer[4] = L'r';
        buffer[5] = L'\0';
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
    constexpr int MAXBUFSIZE = 14;

    if (hotspot == NotSupported)
    {
        // display "not supported"
        buffer[0] = L'n';
        buffer[1] = L'o';
        buffer[2] = L't';
        buffer[3] = L' ';
        buffer[4] = L's';
        buffer[5] = L'u';
        buffer[6] = L'p';
        buffer[7] = L'p';
        buffer[8] = L'o';
        buffer[9] = L'r';
        buffer[10] = L't';
        buffer[11] = L'e';
        buffer[12] = L'd';
        buffer[13] = L'\0';
        return false;
    }

    if (hotspot < NotSupported || hotspot == Error || hotspot > 150)
    {
        // display "error"
        buffer[0] = L'e';
        buffer[1] = L'r';
        buffer[2] = L'r';
        buffer[3] = L'o';
        buffer[4] = L'r';
        buffer[5] = L'\0';
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
    constexpr int MAXBUFSIZE = 14;

    if (rpm == AdlxStates::NotSupported)
    {
        // display "not supported"
        buffer[0] = L'n';
        buffer[1] = L'o';
        buffer[2] = L't';
        buffer[3] = L' ';
        buffer[4] = L's';
        buffer[5] = L'u';
        buffer[6] = L'p';
        buffer[7] = L'p';
        buffer[8] = L'o';
        buffer[9] = L'r';
        buffer[10] = L't';
        buffer[11] = L'e';
        buffer[12] = L'd';
        buffer[13] = L'\0';
        return false;
    }

    if (rpm < NotSupported || rpm == Error || rpm > 9999)
    {
        // display "error"
        buffer[0] = L'e';
        buffer[1] = L'r';
        buffer[2] = L'r';
        buffer[3] = L'o';
        buffer[4] = L'r';
        buffer[5] = L'\0';
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
    constexpr int MAXBUFSIZE = 14;

    if (watts == NotSupported)
    {
        // display "not supported"
        buffer[0] = L'n';
        buffer[1] = L'o';
        buffer[2] = L't';
        buffer[3] = L' ';
        buffer[4] = L's';
        buffer[5] = L'u';
        buffer[6] = L'p';
        buffer[7] = L'p';
        buffer[8] = L'o';
        buffer[9] = L'r';
        buffer[10] = L't';
        buffer[11] = L'e';
        buffer[12] = L'd';
        buffer[13] = L'\0';
        return false;
    }

    if (watts < NotSupported || watts == Error || watts > 999)
    {
        // display "error"
        buffer[0] = L'e';
        buffer[1] = L'r';
        buffer[2] = L'r';
        buffer[3] = L'o';
        buffer[4] = L'r';
        buffer[5] = L'\0';
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
    constexpr int MAXBUFSIZE = 20;

    if (temp == AdminRequired)
    {
        buffer[0] = L'a';
        buffer[1] = L'd';
        buffer[2] = L'm';
        buffer[3] = L'i';
        buffer[4] = L'n';
        buffer[5] = L' ';
        buffer[6] = L'r';
        buffer[7] = L'e';
        buffer[8] = L'q';
        buffer[9] = L'u';
        buffer[10] = L'i';
        buffer[11] = L'r';
        buffer[12] = L'e';
        buffer[13] = L'd';
        buffer[14] = L'\0';
        return false;
    }

    if (temp == SdkRequired)
    {
        buffer[0] = L's';
        buffer[1] = L'd';
        buffer[2] = L'k';
        buffer[3] = L' ';
        buffer[4] = L'r';
        buffer[5] = L'e';
        buffer[6] = L'q';
        buffer[7] = L'u';
        buffer[8] = L'i';
        buffer[9] = L'r';
        buffer[10] = L'e';
        buffer[11] = L'd';
        buffer[12] = L'\0';
        return false;
    }

    if (temp == NotSupported)
    {
        buffer[0] = L'n';
        buffer[1] = L'o';
        buffer[2] = L't';
        buffer[3] = L' ';
        buffer[4] = L's';
        buffer[5] = L'u';
        buffer[6] = L'p';
        buffer[7] = L'p';
        buffer[8] = L'o';
        buffer[9] = L'r';
        buffer[10] = L't';
        buffer[11] = L'e';
        buffer[12] = L'd';
        buffer[13] = L'\0';
        return false;
    }

    if (temp < NotSupported || temp == Error || temp > 150 || power < NotSupported || power == Error || power > 999)
    {
        buffer[0] = L'e';
        buffer[1] = L'r';
        buffer[2] = L'r';
        buffer[3] = L'o';
        buffer[4] = L'r';
        buffer[5] = L'\0';
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