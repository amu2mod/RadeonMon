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

inline HFONT CreateAppFont(int size, LPCWSTR family)
{
    int pixelSize = -MulDiv(size, g_dpi, 96);
    return CreateFontW(pixelSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY,
                       //    DEFAULT_PITCH | FF_DONTCARE,
                       FIXED_PITCH | FF_MODERN,
                       family);
}

inline void RecreateFont()
{
    if (g_titleFont)
        DeleteObject(g_titleFont);
    if (g_font)
        DeleteObject(g_font);
    if (g_notificationFont)
        DeleteObject(g_notificationFont);
    if (g_cardFont)
        DeleteObject(g_cardFont);

    // g_fontSize is the user preference (base font size)
    const float scale = static_cast<float>(g_fontSize) / static_cast<float>(FONTSIZE);
    const int titleFontSize = max(1, static_cast<int>(roundf(TITLE_FONTSIZE * scale)));
    const int notificationFontSize = max(1, static_cast<int>(roundf(NOTIFICATION_FONTSIZE * scale)));
    const int cardFontSize = max(1, static_cast<int>(roundf(CARD_FONTSIZE * scale)));

    g_font = CreateAppFont(g_fontSize, FONT_FAMILY);
    g_titleFont = CreateAppFont(titleFontSize, FONT_FAMILY);
    g_notificationFont = CreateAppFont(notificationFontSize, NOTIFICATION_FONT_FAMILY);
    g_cardFont = CreateAppFont(cardFontSize, FONT_FAMILY);

    LOG_DEBUG("Fonts recreated (DPI=%d base=%d title=%d notification=%d card=%d)", g_dpi, g_fontSize, titleFontSize, notificationFontSize, cardFontSize);
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

template <size_t N>
inline bool FormatFPS(wchar_t (&buffer)[N], int current, int previous)
{
    constexpr auto MAXBUFSIZE = _countof(L"9999 (+9999)");
    static_assert(N >= MAXBUFSIZE, "Buffer too small for FormatFPS");

    constexpr int CONTENT_WIDTH = MAXBUFSIZE - 1;

    wchar_t *p = buffer;

    auto WriteNumber = [&](int value)
    {
        if (value >= 1000)
        {
            *p++ = static_cast<wchar_t>(L'0' + value / 1000);
            *p++ = static_cast<wchar_t>(L'0' + (value / 100) % 10);
            *p++ = static_cast<wchar_t>(L'0' + (value / 10) % 10);
            *p++ = static_cast<wchar_t>(L'0' + value % 10);
        }
        else if (value >= 100)
        {
            *p++ = static_cast<wchar_t>(L'0' + value / 100);
            *p++ = static_cast<wchar_t>(L'0' + (value / 10) % 10);
            *p++ = static_cast<wchar_t>(L'0' + value % 10);
        }
        else if (value >= 10)
        {
            *p++ = static_cast<wchar_t>(L'0' + value / 10);
            *p++ = static_cast<wchar_t>(L'0' + value % 10);
        }
        else
        {
            *p++ = static_cast<wchar_t>(L'0' + value);
        }
    };

    if (current < 0)
        current = 0;
    else if (current > 9999)
        current = 9999;

    WriteNumber(current);

    int delta = current - previous;

    if (delta != 0)
    {
        *p++ = L' ';
        *p++ = L'(';

        if (delta < 0)
        {
            *p++ = L'-';
            delta = -delta;
        }
        else
        {
            *p++ = L'+';
        }

        if (delta > 9999)
            delta = 9999;

        WriteNumber(delta);

        *p++ = L')';
    }

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

// Returns the length of the written JSON (excluding null terminator)
inline int BuildCombinedJson(char *buffer, int bufferSize)
{
    if (bufferSize < GPU_JSON_BUFFER_SIZE)
        return 0;

    char *p = buffer;
    char *end = buffer + bufferSize - 1;

    auto write = [&](const char *s)
    {
        while (*s && p < end)
            *p++ = *s++;
    };

    write("{\"gpu\":");

    if (g_AdlxGPUTelemetry.isInitialized)
    {
        int len = g_AdlxGPUTelemetry.Get().BuildJson(
            p,
            static_cast<int>(end - p + 1),
            g_AdlxGPUTelemetry.GetGpuInfo().shortName);

        if (len > 0)
            p += len;
    }
    else
    {
        write("null");
    }

    if (g_isFpsEnabled)
    {
        write(",\"fps\":");

        int fps = g_AdlxGPUTelemetry.GetSnapshotFPS();

        // LOG_DEBUG("fps=%d", fps);

        if (fps < 0)
        {
            *p++ = '-';
            fps = -fps;
        }

        char tmp[10];
        int n = 0;

        do
        {
            tmp[n++] = static_cast<char>('0' + (fps % 10));
            fps /= 10;
        } while (fps);

        while (n)
            *p++ = tmp[--n];
    }

    write(",\"cpu\":");

    if (g_cpu.IsInitialized())
    {
        int len = 0;
        if (g_webServer.IsRunning())
            len = g_processWatcher.m_ryzenMetrics.BuildJson(p, static_cast<int>(end - p + 1));
        else
            len = g_cpu.GetMetrics().BuildJson(p, static_cast<int>(end - p + 1));

        if (len > 0)
            p += len;
    }
    else
    {
        write("null");
    }

    write(",\"processes\":");

    {
        int len = g_processWatcher.BuildJson(
            p,
            static_cast<int>(end - p + 1));

        if (len > 0)
            p += len;
        else
            write("null");
    }

    *p++ = '}';
    *p = '\0';

    return static_cast<int>(p - buffer);
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

inline int GetFontHeight(HDC hdc, HFONT font)
{
    HFONT oldFont = (HFONT)SelectObject(hdc, font);
    TEXTMETRIC tm{};
    GetTextMetrics(hdc, &tm);
    SelectObject(hdc, oldFont);
    return tm.tmHeight + tm.tmExternalLeading;
}

inline RECT GetCenteredTextRect(HDC hdc, HFONT hFont, const RECT *rc, const WCHAR *text)
{
    RECT textRc = {0};
    SIZE textSize = {0};

    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    GetTextExtentPoint32W(hdc, text, lstrlenW(text), &textSize);

    SelectObject(hdc, oldFont);

    textRc.left = rc->left + ((rc->right - rc->left) - textSize.cx) / 2;
    textRc.top = rc->top + ((rc->bottom - rc->top) - textSize.cy) / 2;
    textRc.right = textRc.left + textSize.cx;
    textRc.bottom = textRc.top + textSize.cy;

    return textRc;
}

// returned value > 32 indicate success.
inline INT_PTR OpenUrl(const wchar_t *url)
{
    if (!url || !*url)
        return false;

    return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", url, nullptr, nullptr, SW_SHOWNORMAL));
}

inline std::string WideToUtf8(const std::wstring &wstr)
{
    if (wstr.empty())
        return {};

    int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.c_str(),
        -1,
        nullptr,
        0,
        nullptr,
        nullptr);

    std::string result(size - 1, '\0');

    WideCharToMultiByte(
        CP_UTF8,
        0,
        wstr.c_str(),
        -1,
        result.data(),
        size,
        nullptr,
        nullptr);

    return result;
}

#ifdef _DEBUG
inline void EnableConsoleColors()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);

    DWORD dwMode = 0;
    GetConsoleMode(hOut, &dwMode);

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;

    SetConsoleMode(hOut, dwMode);
}

inline void LogRect(const char *name, const RECT &rc)
{
    LOG_DEBUG(
        "%s: left=%ld top=%ld right=%ld bottom=%ld width=%ld height=%ld",
        name,
        rc.left,
        rc.top,
        rc.right,
        rc.bottom,
        rc.right - rc.left,
        rc.bottom - rc.top);
}
#endif
