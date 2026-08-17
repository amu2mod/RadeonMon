#pragma once

#include <Windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <memory>
#include <string>

inline int FormatValue(wchar_t *buffer, int value, const wchar_t *unit)
{
    // Special / invalid cases
    if (value == -2) // error
    {
        buffer[0] = L'e';
        buffer[1] = L'r';
        buffer[2] = L'r';
        buffer[3] = L'\0';
        return 3;
    }

    if (value < 0) // -1 (undefined) or any other negative → just "-"
    {
        buffer[0] = L'-';
        buffer[1] = L'\0';
        return 1;
    }

    // Normal path (value >= 0)
    wchar_t *p = buffer;

    // Write digits in reverse
    wchar_t *start = p;
    do
    {
        *p++ = L'0' + (value % 10);
        value /= 10;
    } while (value);

    // Reverse the digits
    wchar_t *end = p - 1;
    while (start < end)
    {
        wchar_t tmp = *start;
        *start++ = *end;
        *end-- = tmp;
    }

    // Append unit string
    while (*unit)
        *p++ = *unit++;

    *p = L'\0';
    return static_cast<int>(p - buffer);
}

struct RingFontMetrics
{
    std::wstring familyName;

    Gdiplus::REAL emSize = 0.0f;
    INT style = Gdiplus::FontStyleRegular;

    Gdiplus::REAL inkOffsetY = 0.0f;
    Gdiplus::REAL inkHeight = 0.0f;

    std::shared_ptr<Gdiplus::Font> cachedFont;

    int charWidthPx = 0;
};

class ProgressRing
{
public:
    ProgressRing() {}
    ~ProgressRing() {}

    ProgressRing(const ProgressRing &) = delete;
    ProgressRing &operator=(const ProgressRing &) = delete;

    void Init(HDC hdc, const RECT &rect, HFONT font, const wchar_t *unit, int maxValue, COLORREF ringColor, COLORREF ringBgColor, COLORREF bgColor, COLORREF textColor);
    void Update(HDC hdc, const RECT &rect, HFONT font); // Call when the UI geometry or font changes.
    void Draw(HDC hdc, int value);
    void Log() const;
    void UpdateColors(COLORREF ringColor, COLORREF ringBgColor, COLORREF bgColor, COLORREF textColor);

private:
    RECT m_r{};

    HFONT m_font = nullptr;

    int m_maxValue = 1;

    COLORREF m_ringColor = RGB(0, 0, 0);
    COLORREF m_ringBgColor = RGB(0, 0, 0);
    COLORREF m_bgColor = RGB(255, 255, 255);
    COLORREF m_textColor = RGB(0, 0, 0);

    wchar_t m_unit[32]{};

    // tweak to make the arc opening larger or tighter
    float m_gapDegrees = 90.0f;

    RingFontMetrics m_fontMetrics;
    Gdiplus::StringFormat m_drawFormat;
};
