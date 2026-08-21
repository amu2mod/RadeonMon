#pragma once

#include <windows.h>

class LineChart
{
public:
    // Time to render: T = SAMPLE_COUNT x tick rate
    // for 2s tick, T = SAMPLE_COUNT x 2
    inline static constexpr int SAMPLE_COUNT = 30; // 1min to fill the chart

    LineChart() {};
    ~LineChart();

    LineChart(const LineChart &) = delete;
    LineChart &operator=(const LineChart &) = delete;

    void Init(const RECT &rect, COLORREF lineColor, COLORREF middleLineColor, COLORREF backgroundColor, COLORREF windowbackgroundColor, COLORREF fillColor, int maxValue);

    // Add a sample and incrementally update the chart.
    void Draw(HDC hdc, const int *history, int currentIndex, int accumulatedCount);

    // Change line color.
    void Update(COLORREF line, COLORREF midLine, COLORREF bg, COLORREF windowBg, COLORREF fill);

    // Change maximum value.
    void UpdateMaxValue(int maxvalue);

    // Paint background, border and middle line
    void DrawFrame(HDC hdc);

    void Redraw(HDC hdc, const int *history, int index, int count);
    void Update(RECT &rect);
    void Reset();

private:
    void DrawMiddleLine(HDC hdc);
    int valueToY(int value) const;

private:
    RECT m_rect{};

    COLORREF m_lineColor{};
    COLORREF m_middleLineColor{};
    COLORREF m_backgroundColor{};
    COLORREF m_windowBackgroundColor{};
    COLORREF m_fillColor{};

    int m_maxValue{};
    int m_scrollPixel = 5;

    HPEN m_linePen{nullptr};
    HPEN m_middleLinePen{nullptr};

    HBRUSH m_backgroundBrush{nullptr};
    HBRUSH m_windowBackgroundBrush{nullptr};
    HBRUSH m_fillBrush{nullptr};

    int m_lastY{};
    int m_lastValue{};
    int m_lastHistoryIndex{};
    int m_lastAccumulatedCount{};
};