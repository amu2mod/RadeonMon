#include "radeonmon/linechart.hpp"
#include "radeonmon/logging.hpp"

#include <algorithm>

void LineChart::Init(const RECT &rect, COLORREF lineColor, COLORREF middleLineColor, COLORREF backgroundColor, COLORREF windowbackgroundColor, COLORREF fillColor, int maxValue)
{
    // Important: rect must be reseted before m_lastY
    m_rect = rect;

    m_lastValue = 0;
    m_lastY = valueToY(0);

    m_lineColor = lineColor;
    m_middleLineColor = middleLineColor;
    m_backgroundColor = backgroundColor;
    m_windowBackgroundColor = windowbackgroundColor;
    m_fillColor = fillColor;
    m_maxValue = maxValue;

    m_scrollPixel = (rect.right - rect.left) / SAMPLE_COUNT;

    if (m_maxValue <= 0)
        m_maxValue = 1;

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

LineChart::~LineChart()
{
    if (m_linePen)
        DeleteObject(m_linePen);

    if (m_middleLinePen)
        DeleteObject(m_middleLinePen);

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

void LineChart::DrawMiddleLine(HDC hdc)
{
    const int height = m_rect.bottom - m_rect.top;
    const int middleY = m_rect.top + height / 2;
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, m_middleLinePen));
    MoveToEx(hdc, m_rect.left + 1, middleY, nullptr);
    LineTo(hdc, m_rect.right - 1, middleY);
    SelectObject(hdc, oldPen);
}

void LineChart::Update(COLORREF line, COLORREF midLine, COLORREF bg, COLORREF windowBg, COLORREF fill)
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

void LineChart::UpdateMaxValue(int maxvalue)
{
    if (maxvalue <= 0 || maxvalue == m_maxValue)
        return;

    m_maxValue = maxvalue;
}

void LineChart::DrawFrame(HDC hdc)
{
    if (!hdc)
        return;

    const int width = m_rect.right - m_rect.left;
    const int height = m_rect.bottom - m_rect.top;

    if (width <= 0 || height <= 0)
        return;

    RECT innerRect = {m_rect.left + 1, m_rect.top + 1, m_rect.right - 1, m_rect.bottom - 1};

    FillRect(hdc, &innerRect, m_windowBackgroundBrush);
    DrawMiddleLine(hdc);
}

void LineChart::Update(RECT &rect)
{
    m_rect = rect;
    m_scrollPixel = (rect.right - rect.left) / SAMPLE_COUNT;
}

void LineChart::Draw(HDC hdc, const int *history, int currentIndex, int accumulatedCount)
{
    if (!hdc || !history)
        return;

    const int width = m_rect.right - m_rect.left;
    const int height = m_rect.bottom - m_rect.top;

    if (width <= 2 || height <= 2)
        return;

    currentIndex = ((currentIndex % SAMPLE_COUNT) + SAMPLE_COUNT) % SAMPLE_COUNT;
    accumulatedCount = std::clamp(accumulatedCount, 0, SAMPLE_COUNT);

    if (accumulatedCount <= 0)
        return;

    //
    // First sample ever drawn.
    //
    if (m_lastAccumulatedCount == 0)
    {
        Redraw(hdc, history, currentIndex, accumulatedCount);
        m_lastHistoryIndex = currentIndex;
        m_lastAccumulatedCount = accumulatedCount;
        return;
    }

    //
    // Detect how far the ring-buffer cursor has moved.
    //
    const int indexDelta = (currentIndex - m_lastHistoryIndex + SAMPLE_COUNT) % SAMPLE_COUNT;
    const int countDelta = accumulatedCount - m_lastAccumulatedCount;

    //
    // Nothing changed.
    //
    if (indexDelta == 0 && countDelta == 0)
        return;

    //
    // Exactly one sample was added.
    //
    const bool oneNewSample = indexDelta == 1 && (countDelta == 1 || (accumulatedCount == SAMPLE_COUNT && m_lastAccumulatedCount == SAMPLE_COUNT));

    //
    // If we can't prove this is exactly one new sample,
    // reconstruct the chart from the ring buffer.
    //
    if (!oneNewSample)
    {
        Redraw(hdc, history, currentIndex, accumulatedCount);
        m_lastHistoryIndex = currentIndex;
        m_lastAccumulatedCount = accumulatedCount;
        return;
    }

    //
    // Scroll the existing chart.
    //
    const int scroll = min(max(m_scrollPixel, 1), width - 2);

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

    //
    // Clear the newly exposed area.
    //
    const int previousX = m_rect.right - 2 - scroll;

    RECT eraseRect = {previousX + 1, m_rect.top + 1, m_rect.right - 1, m_rect.bottom - 1};
    FillRect(hdc, &eraseRect, m_backgroundBrush);

    //
    // currentIndex is the NEXT write position.
    // Therefore the sample we just added is at currentIndex - 1.
    //
    const int newestIndex = (currentIndex - 1 + SAMPLE_COUNT) % SAMPLE_COUNT;
    const int value = std::clamp(history[newestIndex], 0, m_maxValue);
    const int newY = valueToY(value);
    const int newX = m_rect.right - 2;

    //
    // m_lastY is the endpoint of the previously drawn sample.
    //
    if (previousX != newX)
    {
        POINT fillPoints[4] = {
            {previousX, m_lastY},
            {newX + 1, newY},
            {newX + 1, m_rect.bottom - 1},
            {previousX, m_rect.bottom - 1}};

        HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, GetStockObject(NULL_PEN)));
        HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, m_fillBrush));
        Polygon(hdc, fillPoints, 4);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }

    //
    // Draw new waveform segment.
    //
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, m_linePen));
    MoveToEx(hdc, previousX, m_lastY, nullptr);
    LineTo(hdc, newX, newY);
    SelectObject(hdc, oldPen);

    DrawMiddleLine(hdc);

    //
    // Save state.
    //
    m_lastValue = value;
    m_lastY = newY;

    m_lastHistoryIndex = currentIndex;
    m_lastAccumulatedCount = accumulatedCount;
}

void LineChart::Redraw(HDC hdc, const int *history, int currentIndex, int accumulatedCount)
{
    if (!hdc || !history || SAMPLE_COUNT <= 0 || accumulatedCount <= 0)
    {
        LOG_ERROR("[CHART] Redraw: bad args (hdc=%p, history=%p, SAMPLE_COUNT=%d, currentIndex=%d, accumulatedCount=%d)", hdc, history, SAMPLE_COUNT, currentIndex, accumulatedCount);
        return;
    }

    const int width = m_rect.right - m_rect.left;
    const int height = m_rect.bottom - m_rect.top;

    if (width <= 2 || height <= 2)
    {
        LOG_ERROR("[CHART] Redraw: rect too small, bailing (width=%d height=%d)", width, height);
        return;
    }

    // currentIndex is the NEXT write position in the ring buffer.
    currentIndex = ((currentIndex % SAMPLE_COUNT) + SAMPLE_COUNT) % SAMPLE_COUNT;

    // Number of valid samples currently stored in the ring buffer.
    const int plotCount = min(accumulatedCount, SAMPLE_COUNT);

    // Clear chart interior.
    RECT innerRect = {
        m_rect.left + 1,
        m_rect.top + 1,
        m_rect.right - 1,
        m_rect.bottom - 1};

    FillRect(hdc, &innerRect, m_backgroundBrush);

    if (plotCount <= 0)
    {
        m_lastValue = 0;
        m_lastY = valueToY(0);

        DrawMiddleLine(hdc);
        return;
    }

    // currentIndex is the NEXT write position.
    // Therefore the newest sample is currentIndex - 1.
    const int startIdx =
        ((currentIndex - plotCount) % SAMPLE_COUNT + SAMPLE_COUNT) %
        SAMPLE_COUNT;

    const int plotLeft = m_rect.left + 1;
    const int plotRight = m_rect.right - 1;
    const int plotWidth = max(plotRight - plotLeft, 0);

    /*
     * Horizontal scaling:
     *
     *   Partial history:
     *       Keep the normal fixed m_scrollPixel spacing.
     *
     *   Full history:
     *       Fit ALL samples into the available chart width.
     *
     * This prevents a small number of samples from being stretched
     * across the entire chart while still allowing a full ring buffer
     * to survive a resize.
     */
    const bool fullHistory = (plotCount == SAMPLE_COUNT);

    const double step = fullHistory && plotCount > 1 ? static_cast<double>(plotWidth) / (plotCount - 1) : static_cast<double>(max(m_scrollPixel, 1));

    // std::vector<POINT> points;
    // points.reserve(plotCount);
    POINT points[SAMPLE_COUNT];

    for (int i = 0; i < plotCount; ++i)
    {
        const int idx = (startIdx + i) % SAMPLE_COUNT;

        const int value = std::clamp(history[idx], 0, m_maxValue);
        const int y = valueToY(value);

        int x;

        if (fullHistory && plotCount > 1)
        {
            // Full history: oldest at left, newest at right.
            x = plotLeft + static_cast<int>(i * step);
        }
        else
        {
            // Partial history: preserve the normal fixed sample spacing
            // and keep the newest sample at the right side.
            x = plotRight - (plotCount - 1 - i) * max(m_scrollPixel, 1);
        }

        points[i] = {x, y};
    }

    //
    // Clip waveform/fill to chart interior.
    //
    const int savedDC = SaveDC(hdc);

    IntersectClipRect(
        hdc,
        m_rect.left + 1,
        m_rect.top + 1,
        m_rect.right - 1,
        m_rect.bottom - 1);

    //
    // Fill under waveform.
    //
    POINT fillPoints[SAMPLE_COUNT + 2];

    if (plotCount >= 2)
    {
        fillPoints[0] = {
            points[0].x,
            m_rect.bottom - 1};

        for (int i = 0; i < plotCount; ++i)
            fillPoints[i + 1] = points[i];

        fillPoints[plotCount + 1] = {
            points[plotCount - 1].x,
            m_rect.bottom - 1};

        HPEN oldPen =
            static_cast<HPEN>(
                SelectObject(hdc, GetStockObject(NULL_PEN)));

        HBRUSH oldBrush =
            static_cast<HBRUSH>(
                SelectObject(hdc, m_fillBrush));

        Polygon(
            hdc,
            fillPoints,
            plotCount + 2);

        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
    }

    //
    // Waveform.
    //
    HPEN oldLinePen =
        static_cast<HPEN>(SelectObject(hdc, m_linePen));

    MoveToEx(hdc, points[0].x, points[0].y, nullptr);

    for (size_t i = 1; i < plotCount; ++i)
    {
        LineTo(hdc, points[i].x, points[i].y);
    }

    SelectObject(hdc, oldLinePen);

    RestoreDC(hdc, savedDC);

    //
    // middle line.
    //
    DrawMiddleLine(hdc);

    //
    // Save newest sample state.
    //
    const int lastRingIdx =
        (currentIndex - 1 + SAMPLE_COUNT) % SAMPLE_COUNT;

    m_lastValue = history[lastRingIdx];
    m_lastY = points[plotCount - 1].y;
}

void LineChart::Reset()
{
    m_lastHistoryIndex = -1;
    m_lastAccumulatedCount = 0;
    m_lastValue = 0;
    m_lastY = 0;
}