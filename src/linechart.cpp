#include "radeonmon/linechart.hpp"
#include "radeonmon/logging.hpp"

#include <algorithm>

void LineChart::Init(const RECT &rect, COLORREF lineColor, COLORREF middleLineColor, COLORREF backgroundColor, COLORREF windowbackgroundColor, COLORREF borderColor, COLORREF fillColor, int maxValue, int periodSeconds, int intervalMs)
{
    // Important: rect must be reseted before m_lastY
    m_rect = rect;

    m_lastValue = 0;
    m_lastY = valueToY(0);

    m_lineColor = lineColor;
    m_middleLineColor = middleLineColor;
    m_backgroundColor = backgroundColor;
    m_windowBackgroundColor = windowbackgroundColor;
    m_borderColor = borderColor;
    m_fillColor = fillColor;
    m_maxValue = maxValue;

    if (m_maxValue <= 0)
        m_maxValue = 1;

    if (periodSeconds <= 0)
        periodSeconds = 1;

    if (intervalMs <= 0)
        intervalMs = 1;

    m_sampleCount = max(10, periodSeconds * 1000 / intervalMs);

    if (m_linePen)
        DeleteObject(m_linePen);

    if (m_middleLinePen)
        DeleteObject(m_middleLinePen);

    if (m_borderPen)
        DeleteObject(m_borderPen);

    if (m_backgroundBrush)
        DeleteObject(m_backgroundBrush);

    if (m_windowBackgroundBrush)
        DeleteObject(m_windowBackgroundBrush);

    if (m_fillBrush)
        DeleteObject(m_fillBrush);

    m_linePen = CreatePen(PS_SOLID, 2, m_lineColor);
    m_middleLinePen = CreatePen(PS_SOLID, 1, m_middleLineColor);
    m_borderPen = CreatePen(PS_SOLID, 1, m_borderColor);
    m_backgroundBrush = CreateSolidBrush(m_backgroundColor);
    m_windowBackgroundBrush = CreateSolidBrush(m_windowBackgroundColor);
    m_fillBrush = CreateSolidBrush(m_fillColor);
}

LineChart::~LineChart()
{
    if (m_linePen)
        DeleteObject(m_linePen);

    if (m_middleLinePen)
        DeleteObject(m_middleLinePen);

    if (m_borderPen)
        DeleteObject(m_borderPen);

    if (m_backgroundBrush)
        DeleteObject(m_backgroundBrush);

    if (m_fillBrush)
        DeleteObject(m_fillBrush);

    if (m_windowBackgroundBrush)
        DeleteObject(m_windowBackgroundBrush);
}

int LineChart::valueToY(int value) const
{
    const int height = m_rect.bottom - m_rect.top;

    if (height <= 1)
        return m_rect.bottom - 1;

    const int clampedValue = std::clamp(value, 0, m_maxValue);

    return m_rect.bottom - 1 - static_cast<int>((static_cast<double>(clampedValue) / static_cast<double>(m_maxValue)) * static_cast<double>(height - 1));
}

void LineChart::draw(HDC hdc, int value)
{
    // Debug
    // {
    //     FillRect(hdc, &m_rect, m_fillBrush);
    //     return;
    // }

    if (!hdc)
        return;

    if (!&m_rect)
        return;

    const int width = m_rect.right - m_rect.left;
    const int height = m_rect.bottom - m_rect.top;

    // LOG_DEBUG("[CHART] draw: %dx%d", width, height);

    if (width <= 2 || height <= 2)
        return;

    value = std::clamp(value, 0, m_maxValue);

    // LOG_DEBUG("[CHART] value=%d", value);

    const int scroll = min(SCROLL_PIXELS, width - 2);

    /////
    // Scroll existing chart pixels

    if (width - 2 > scroll)
    {
        BitBlt(
            hdc,
            m_rect.left + 1,
            m_rect.top + 1,
            width - 2 - scroll,
            height - 2,
            hdc,
            m_rect.left + 1 + scroll,
            m_rect.top + 1,
            SRCCOPY);
    }

    /////
    // Clear newly exposed area

    const int previousX = m_rect.right - 2 - scroll;
    RECT eraseRect = {previousX + 1, m_rect.top + 1, m_rect.right - 1, m_rect.bottom - 1};
    FillRect(hdc, &eraseRect, m_backgroundBrush);

    const int newY = valueToY(value);
    const int newX = m_rect.right - 2;

    /////
    // Fill under the new waveform segment

    // if (m_historyCount <= 2)
    //     return;

    if (previousX != newX)
    {
        POINT fillPoints[4] = {{previousX, m_lastY}, {newX + 1, newY}, {newX + 1, m_rect.bottom - 1}, {previousX, m_rect.bottom - 1}};

        // LOG_DEBUG("[CHART] Polygon Point: {%ld,%ld}, {%ld,%ld}, {%ld,%ld}, {%ld,%ld}", fillPoints[0].x, fillPoints[0].y, fillPoints[1].x, fillPoints[1].y, fillPoints[2].x, fillPoints[2].y, fillPoints[3].x, fillPoints[3].y);

        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, m_fillBrush));

        Polygon(hdc, fillPoints, 4);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }

    /////
    // Waveform

    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, m_linePen));
    MoveToEx(hdc, previousX, m_lastY, nullptr);
    LineTo(hdc, newX, newY);
    SelectObject(hdc, oldPen);

    // LOG_DEBUG("[CHART] waveform: {%d,%d}", newX, newY);

    /////
    // Border

    drawBorder(hdc);

    /////
    // Middle line

    drawMiddleLine(hdc);
    m_lastValue = value;
    m_lastY = newY;
}

void LineChart::drawBorder(HDC hdc)
{
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, m_borderPen));
    MoveToEx(hdc, m_rect.left, m_rect.top, nullptr);
    LineTo(hdc, m_rect.right - 1, m_rect.top);
    LineTo(hdc, m_rect.right - 1, m_rect.bottom - 1);
    LineTo(hdc, m_rect.left, m_rect.bottom - 1);
    LineTo(hdc, m_rect.left, m_rect.top);

    SelectObject(hdc, oldPen);
}

void LineChart::drawMiddleLine(HDC hdc)
{
    const int height = m_rect.bottom - m_rect.top;
    const int middleY = m_rect.top + height / 2;
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, m_middleLinePen));
    MoveToEx(hdc, m_rect.left + 1, middleY, nullptr);
    LineTo(hdc, m_rect.right - 1, middleY);
    SelectObject(hdc, oldPen);
}

void LineChart::update(COLORREF line, COLORREF midLine, COLORREF bg, COLORREF windowBg, COLORREF fill)
{
    m_lineColor = line;
    m_middleLineColor = midLine;
    m_backgroundColor = bg;
    m_fillColor = fill;
    m_windowBackgroundColor = windowBg;

    if (m_linePen)
        DeleteObject(m_linePen);

    if (m_middleLinePen)
        DeleteObject(m_middleLinePen);

    if (m_backgroundBrush)
        DeleteObject(m_backgroundBrush);

    if (m_windowBackgroundBrush)
        DeleteObject(m_windowBackgroundBrush);

    if (m_fillBrush)
        DeleteObject(m_fillBrush);

    m_linePen = CreatePen(PS_SOLID, 2, m_lineColor);
    m_middleLinePen = CreatePen(PS_SOLID, 1, m_middleLineColor);
    m_backgroundBrush = CreateSolidBrush(m_backgroundColor);
    m_windowBackgroundBrush = CreateSolidBrush(m_windowBackgroundColor);
    m_fillBrush = CreateSolidBrush(m_fillColor);
}

void LineChart::updatemaxvalue(int maxvalue)
{
    if (maxvalue <= 0 || maxvalue == m_maxValue)
        return;

    m_maxValue = maxvalue;
}

void LineChart::drawFrame(HDC hdc)
{
    if (!hdc)
        return;

    const int width = m_rect.right - m_rect.left;
    const int height = m_rect.bottom - m_rect.top;

    if (width <= 0 || height <= 0)
        return;

    FillRect(hdc, &m_rect, m_windowBackgroundBrush);
    drawBorder(hdc);
    drawMiddleLine(hdc);
}
