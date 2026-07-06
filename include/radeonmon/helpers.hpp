#pragma once

#include <Windows.h>
#include <cstddef>

#include "radeonmon/constants.hpp"
#include "radeonmon/structures.hpp"

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

    int fontSize = -MulDiv(FONTSIZE, g_dpi, 96); // 12pt base font

    g_font = CreateFontW(
        fontSize, 0,
        0, 0,
        FW_NORMAL,
        FALSE, FALSE, FALSE,
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE,
        FONT_FAMILY);
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