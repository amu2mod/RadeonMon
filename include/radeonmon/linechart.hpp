#pragma once

#include <windows.h>

class LineChart
{
public:
    static constexpr int SCROLL_PIXELS = 5;

    LineChart() {};
    ~LineChart();

    LineChart(const LineChart &) = delete;
    LineChart &operator=(const LineChart &) = delete;

    void Init(const RECT &rect, COLORREF lineColor, COLORREF middleLineColor, COLORREF backgroundColor, COLORREF windowbackgroundColor, COLORREF borderColor, COLORREF fillColor, int maxValue, int periodSeconds, int intervalMs);

    // Add a sample and incrementally update the chart.
    void draw(HDC hdc, int value);

    // Change line color.
    void update(COLORREF line, COLORREF midLine, COLORREF bg, COLORREF windowBg, COLORREF fill);

    // Change maximum value.
    void updatemaxvalue(int maxvalue);

    // Paint background, border and middle line
    void drawFrame(HDC hdc);

private:
    void drawBorder(HDC hdc);
    void drawMiddleLine(HDC hdc);
    int valueToY(int value) const;

private:
    RECT m_rect{};

    COLORREF m_lineColor{};
    COLORREF m_middleLineColor{};
    COLORREF m_backgroundColor{};
    COLORREF m_windowBackgroundColor{};
    COLORREF m_borderColor{};
    COLORREF m_fillColor{};

    int m_maxValue{};
    int m_sampleCount{};

    HPEN m_linePen{nullptr};
    HPEN m_middleLinePen{nullptr};
    HPEN m_borderPen{nullptr};

    HBRUSH m_backgroundBrush{nullptr};
    HBRUSH m_windowBackgroundBrush{nullptr};
    HBRUSH m_fillBrush{nullptr};

    int m_lastY{};
    int m_lastValue{};
};