#include "radeonmon/ProgressRing.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "gdiplus.lib")

using namespace Gdiplus;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

static Color ToGdiColor(COLORREF c, BYTE alpha = 255)
{
    return Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

// -----------------------------------------------------------------------------
// Font metrics
// -----------------------------------------------------------------------------

static RingFontMetrics ComputeRingFontMetrics(HDC hdc, HFONT hFont, const wchar_t *sampleText = L"0123456789%")
{
    RingFontMetrics m;

    if (!hdc || !hFont)
        return m;

    Font font(hdc, hFont);

    FontFamily family;
    font.GetFamily(&family);

    WCHAR nameBuf[LF_FACESIZE] = {};
    family.GetFamilyName(nameBuf);

    m.familyName = nameBuf;
    m.emSize = font.GetSize();
    m.style = font.GetStyle();
    Unit fontUnit = font.GetUnit();

    // Measure ink positioning (unchanged)
    StringFormat inkFormat(StringFormat::GenericTypographic());
    inkFormat.SetAlignment(StringAlignmentNear);
    inkFormat.SetLineAlignment(StringAlignmentNear);

    GraphicsPath inkPath;
    inkPath.AddString(sampleText, -1, &family, m.style, m.emSize, PointF(0.0f, 0.0f), &inkFormat);

    RectF bounds;
    inkPath.GetBounds(&bounds);

    m.inkOffsetY = bounds.Y;
    m.inkHeight = bounds.Height;

    m.cachedFont = std::make_shared<Font>(&family, m.emSize, m.style, fontUnit);

    // Monospace advance width, computed once. Select the real HFONT into the
    // real HDC so this matches whatever TextOutW/GetTextExtentPoint32W will
    // see at draw time (device metrics, not GDI+ metrics).
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);
    SIZE charSize{};
    GetTextExtentPoint32W(hdc, L"0", 1, &charSize);
    SelectObject(hdc, oldFont);

    m.charWidthPx = charSize.cx;

    return m;
}

// -----------------------------------------------------------------------------
// Init
// -----------------------------------------------------------------------------

void ProgressRing::Init(HDC hdc, const RECT &rect, HFONT font, const wchar_t *unit, int maxValue, COLORREF ringColor, COLORREF ringBgColor, COLORREF bgColor, COLORREF textColor)
{
    m_r = rect;
    m_font = font;
    m_maxValue = max(1, maxValue);
    m_ringColor = ringColor;
    m_ringBgColor = ringBgColor;
    m_bgColor = bgColor;
    m_textColor = textColor;

    if (unit)
        wcsncpy_s(m_unit, _countof(m_unit), unit, _TRUNCATE);
    else
        m_unit[0] = L'\0';

    m_fontMetrics = ComputeRingFontMetrics(hdc, m_font);

    m_drawFormat.SetAlignment(Gdiplus::StringAlignmentNear);
    m_drawFormat.SetLineAlignment(Gdiplus::StringAlignmentNear);
    const Gdiplus::StringFormat *genericType = Gdiplus::StringFormat::GenericTypographic();
    m_drawFormat.SetFormatFlags(genericType->GetFormatFlags());
}

// -----------------------------------------------------------------------------
// Update
// -----------------------------------------------------------------------------

void ProgressRing::Update(HDC hdc, const RECT &rect, HFONT font)
{
    const bool fontChanged = m_font != font;

    m_r = rect;

    if (fontChanged)
    {
        m_font = font;
        m_fontMetrics = ComputeRingFontMetrics(hdc, m_font);
    }
}

void ProgressRing::UpdateColors(COLORREF ringColor, COLORREF ringBgColor, COLORREF bgColor, COLORREF textColor)
{
    m_ringColor = ringColor;
    m_ringBgColor = ringBgColor;
    m_bgColor = bgColor;
    m_textColor = textColor;
}

// -----------------------------------------------------------------------------
// Draw
// -----------------------------------------------------------------------------

void ProgressRing::Draw(HDC hdc, int value)
{
    if (!hdc || !m_fontMetrics.cachedFont)
        return;

    const int width = m_r.right - m_r.left;
    const int height = m_r.bottom - m_r.top;

    if (width <= 0 || height <= 0)
        return;

    value = std::clamp(value, 0, m_maxValue);

    const int cx = m_r.left + width / 2;
    const int cy = m_r.top + height / 2;

    // -------------------------------------------------------------------------
    // Ring geometry
    // -------------------------------------------------------------------------

    constexpr float ringPadding = 3.0f; // pixels

    const float halfMinDimension = static_cast<float>(min(width, height)) * 0.5f;
    const float availableRadius = max(1.0f, halfMinDimension - ringPadding);

    int r = static_cast<int>(availableRadius / 1.11f);
    if (r < 1)
        r = 1;

    const float penWidth = max(4.0f, r * 0.22f);

    // -------------------------------------------------------------------------
    // Value / angles
    // -------------------------------------------------------------------------

    const float fraction = static_cast<float>(value) / static_cast<float>(m_maxValue);
    constexpr float bottomAngle = 90.0f;
    const float halfGap = m_gapDegrees / 2.0f;
    const float startAngle = bottomAngle + halfGap;
    const float fullSweep = 360.0f - m_gapDegrees;
    const float valueSweep = fraction * fullSweep;

    // -------------------------------------------------------------------------
    // Graphics setup
    // -------------------------------------------------------------------------

    Graphics graphics(hdc);
    graphics.SetSmoothingMode(SmoothingModeHighQuality); // or SmoothingModeAntiAlias

    // -------------------------------------------------------------------------
    // Background
    // -------------------------------------------------------------------------

#ifdef GPUGRAPHRECT
    HBRUSH brush = CreateSolidBrush(RGB(196, 99, 191));
#else
    HBRUSH brush = CreateSolidBrush(m_bgColor);
#endif
    FillRect(hdc, &m_r, brush);
    DeleteObject(brush);

    // -------------------------------------------------------------------------
    // Ring Rendering
    // -------------------------------------------------------------------------

    // Background ring
    Pen emptyPen(ToGdiColor(m_ringBgColor), penWidth);
    graphics.DrawArc(&emptyPen, cx - r, cy - r, r * 2, r * 2, startAngle, fullSweep);

    // Value ring
    if (value > 0)
    {
        Pen valuePen(ToGdiColor(m_ringColor), penWidth);
        graphics.DrawArc(&valuePen, cx - r, cy - r, r * 2, r * 2, startAngle, valueSweep);
    }

    // -------------------------------------------------------------------------
    // Text Rendering
    // -------------------------------------------------------------------------

    wchar_t text[32];
    const int textLen = FormatValue(text, value, m_unit); // m_unit is now a single wchar_t

    graphics.Flush(FlushIntentionSync);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, m_textColor);
    HFONT oldFont = (HFONT)SelectObject(hdc, m_font);

    const int textWidth = m_fontMetrics.charWidthPx * textLen;
    const int layoutTopY = static_cast<int>(std::lround(static_cast<float>(cy) - m_fontMetrics.inkOffsetY - m_fontMetrics.inkHeight / 2.0f));
    const int layoutLeftX = cx - textWidth / 2;

    TextOutW(hdc, layoutLeftX, layoutTopY, text, textLen);

    SelectObject(hdc, oldFont);
}

// -----------------------------------------------------------------------------
// Log
// -----------------------------------------------------------------------------

void ProgressRing::Log() const
{
    wchar_t buffer[512];
    swprintf_s(buffer, _countof(buffer), L"ProgressRing : rect=(%d,%d)-(%d,%d), max=%d, font=%p, family=%s, emSize=%.1f\n", m_r.left, m_r.top, m_r.right, m_r.bottom, m_maxValue, m_font, m_fontMetrics.familyName.c_str(), m_fontMetrics.emSize);
    OutputDebugStringW(buffer);
}