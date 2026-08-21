#pragma once

#include "radeonmon/structures.hpp"
#include "radeonmon/adlx.hpp"
#include "radeonmon/colors.hpp"
#include "radeonmon/ProgressRing.hpp"
#include "radeonmon/linechart.hpp"

#include <Windows.h>

#include <string>
#include <algorithm>
#include <array>
#include <vector>

using namespace RadeonMon::Hardware;

class GpuGraphWindow
{
public:
    enum class GPU_ROW_ID
    {
        Usage,
        ClockSpeed,
        VramClockSpeed,
        Temperature,
        Hotspot,
        MemoryTemperature,
        IntakeTemperature,
        Power,
        TotalBoardPower,
        Voltage,
        PowerLimitPercent, // was GPU_CAP_MANUAL_POWER_TUNING (%)
        PowerLimitWatts,   // was GPU_CAP_MANUAL_POWER_TUNING (W) — now distinct
        FanSpeed,
        FanDuty,
        Vram,
        SharedMemory,
        NpuFrequency,
        NpuActivityLevel,

        Count
    };

    using DoubleGetter = const RadeonMon::Hardware::MetricDouble &(*)(const GpuMetricsSnapshot &);
    using IntGetter = const RadeonMon::Hardware::MetricInt &(*)(const GpuMetricsSnapshot &);

    struct StatRow
    {
        GPU_ROW_ID id; // UI identity — used for m_selected / hit-testing
        GPU_CAPS cap;
        const wchar_t *name;
        const wchar_t *unit;
        bool isInt;
        int y; // y position
        bool isChartEnabled = true;

        DoubleGetter getDouble = nullptr;
        IntGetter getInt = nullptr;

        RadeonMon::Hardware::MetricDouble GpuMetricsSnapshot::*doubleMember = nullptr;
        RadeonMon::Hardware::MetricInt GpuMetricsSnapshot::*intMember = nullptr;
    };

    GpuGraphWindow(ADLXGpuTelemetry &adlx) : m_adlx(adlx) {}

    ~GpuGraphWindow()
    {
        DeleteObject(bgBrush);
        DeleteObject(chartBgBrush);
        DeleteObject(barBrush);
        DeleteObject(markerBrush);
        DeleteObject(borderBrush);
        DeleteObject(m_hFont);
        DeleteObject(m_titleFont);
        Gdiplus::GdiplusShutdown(m_gdiplusToken);
        DestroyBackBuffer();
    }

    void inline RebuildBrushes()
    {
        const auto &colors = GpuTheme::Get(m_currentTheme);
        bgBrush = CreateSolidBrush(colors.windowBackground);
        chartBgBrush = CreateSolidBrush(colors.barBackground);
        barBrush = CreateSolidBrush(colors.bar);
        markerBrush = CreateSolidBrush(colors.marker);
        borderBrush = CreateSolidBrush(colors.titlebar);
    }

    bool Create(HWND hParent);
    void Show();
    void Close();
    void Update(); // Called from the main timer

    inline bool isActive() const { return m_hwnd != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void CreateUIFont(UINT dpi);
    int GetRequiredClientHeight(UINT dpi) const;
    void UpdateWindowHeight();
    bool SaveSettings();
    bool LoadSettings();
    int GetMinRequiredClientWidth(UINT dpi) const;
    void UpdateLayoutRects();
    void OnResizeWindow(bool grow);
    int GetScaledTitleFontSize() const; // Helper to be called after loading the default font size from preferences
    int CountSupportedMetrics() const;
    void PaintLabels(HDC hdc);
    void PaintValues(HDC hdc);
    void BuildStatRows();
    void PaintTitleBar(HDC hdc);
    void PaintFrame(HDC hdc);
    void PaintSeparator(HDC hdc);
    void ApplyTheme(GpuTheme::Type theme);
    void PaintLabelLines(HDC);

    static const RadeonMon::Hardware::MetricDouble &GetUsage(const GpuMetricsSnapshot &s) { return s.usage; }
    static const RadeonMon::Hardware::MetricDouble &GetTemperature(const GpuMetricsSnapshot &s) { return s.temperature; }
    static const RadeonMon::Hardware::MetricDouble &GetHotspot(const GpuMetricsSnapshot &s) { return s.hotspot; }
    static const RadeonMon::Hardware::MetricDouble &GetMemoryTemperature(const GpuMetricsSnapshot &s) { return s.memoryTemperature; }
    static const RadeonMon::Hardware::MetricDouble &GetIntakeTemperature(const GpuMetricsSnapshot &s) { return s.intakeTemperature; }
    static const RadeonMon::Hardware::MetricDouble &GetPower(const GpuMetricsSnapshot &s) { return s.power; }
    static const RadeonMon::Hardware::MetricDouble &GetTotalBoardPower(const GpuMetricsSnapshot &s) { return s.totalBoardPower; }

    static const RadeonMon::Hardware::MetricInt &GetClockSpeed(const GpuMetricsSnapshot &s) { return s.clockSpeed; }
    static const RadeonMon::Hardware::MetricInt &GetVramClockSpeed(const GpuMetricsSnapshot &s) { return s.vramClockSpeed; }
    static const RadeonMon::Hardware::MetricInt &GetVoltage(const GpuMetricsSnapshot &s) { return s.voltage; }
    static const RadeonMon::Hardware::MetricInt &GetPowerLimit(const GpuMetricsSnapshot &s) { return s.powerLimit; }
    static const RadeonMon::Hardware::MetricInt &GetPowerLimitWatts(const GpuMetricsSnapshot &s) { return s.powerLimitWatts; }
    static const RadeonMon::Hardware::MetricInt &GetFanSpeed(const GpuMetricsSnapshot &s) { return s.fanSpeed; }
    static const RadeonMon::Hardware::MetricInt &GetFanDuty(const GpuMetricsSnapshot &s) { return s.fanDuty; }
    static const RadeonMon::Hardware::MetricInt &GetVram(const GpuMetricsSnapshot &s) { return s.vram; }
    static const RadeonMon::Hardware::MetricInt &GetSharedMemory(const GpuMetricsSnapshot &s) { return s.sharedMemory; }
    static const RadeonMon::Hardware::MetricInt &GetNpuFrequency(const GpuMetricsSnapshot &s) { return s.npuFrequency; }
    static const RadeonMon::Hardware::MetricInt &GetNpuActivityLevel(const GpuMetricsSnapshot &s) { return s.npuActivityLevel; }

    void ClearChanged(RadeonMon::Hardware::MetricDouble &metric) { metric.hasChanged = false; }
    void ClearChanged(RadeonMon::Hardware::MetricInt &metric) { metric.hasChanged = false; }

    void FormatLabel(wchar_t *dst, const wchar_t *name, int length);
    void FormatValue(wchar_t *dst, int value, const wchar_t *unit);
    void FormatValue2(wchar_t *dst, double value, const wchar_t *unit, int maxLength);
    void FormatRange(wchar_t *dst, int minInt, int maxInt, const wchar_t *unit, size_t maxlength);
    GPU_ROW_ID HitTestRow(POINT pt) const;

    const StatRow *FindRow(GPU_ROW_ID id) const;
    void EnsureBackBuffer(HDC hdc, int width, int height);
    void DestroyBackBuffer();
    void AddHistoryValue(int value);
    void ResetHistory();
    int GetHistoryValueAt(int index) const;
    double GetAverage() const;
    double GetMedian() const;
    void AddCurrentMetricToHistory();
    void PaintChartBorder();
    void LogMedian() const;

private:
    HWND m_hwnd = nullptr;
    UINT m_dpi;
    ADLXGpuTelemetry &m_adlx;
    bool m_userClose = false;
    int m_FontHeight = 16;
    int m_FontAscent = 0;
    int m_FontWidth = 0;
    int m_TitleFontWidth = 0;
    int m_LabelWidth = 0;
    int m_BarLeftMargin = 0;
    int m_MarginTopBottom = 0;
    int m_MarginLeftRight = 0;
    int m_BarHeight = 16;
    int m_LineGap1 = 0;
    int m_LineGap2 = 0;
    int m_OnePxScaled = 1;
    int m_MarkerWidth = 2;
    std::wstring m_title;
    int m_metricsCount;
    int m_ringSize;
    int m_borderSize;
    int m_SeparatorMargin = 0;
    int m_Column2Width;
    int m_Colmun2LabelWidth;
    int m_titlePadding;
    int m_titleBarHeight;
    int m_MinLabelY;
    int m_MaxLabelY;
    int m_RangeLabelY;
    int m_LabelValueOffset;
    bool m_forceFullRedraw = true;
    UINT m_pendingDpi;
    RECT m_pendingSuggestedRect;
    int m_RingTopMargin;
    bool m_pendingDpiResize = false;
    int m_ChartHeight;
    bool m_RedrawChart = false;
    int m_MedianLabelY;

    GPU_ROW_ID m_selected = GPU_ROW_ID::Usage;

    RECT m_TitleBarRc{};
    RECT m_ContentRc{};       // borders + margins + body
    RECT m_BodyRc{};          // labels/value + inner gap + ring
    RECT m_Column1Rc{};       // ring + min/max values
    RECT m_Column2Rc{};       // ring + min/max values
    RECT m_Column1ValuesRc{}; // values of column 1
    RECT m_ringRc{};
    RECT m_chartRc{};

    // font variables
    const int c_defaultFontSize = 16;
    const int c_defaultTileFontSize = 14;
    const int c_defaultRingFontSize = 17;
    HFONT m_hFont = nullptr;
    HFONT m_titleFont = nullptr;
    HFONT m_ringFont = nullptr;
    int m_fontSize = c_defaultFontSize;
    int m_titleFontSize = c_defaultTileFontSize;
    int m_ringFontSize = c_defaultRingFontSize;
    int m_TitleFontHeight;
    int m_TitleFontAscent;

    // window related
    int m_x = -1;
    int m_y = -1;
    int m_width, m_height;
    UINT m_savedDpi;

    // Spacing constants
    const int c_MarginTopBottom = 9;
    const int c_MarginLeftRight = 14;
    const int c_LineGap1 = 2;
    const int c_LineGap2 = 5;
    const int c_borderWidth = 1;
    const int c_TitlePaddingTopBottom = 5;
    const int c_MinBarGraphWidth = 100;
    const int c_MaxFontSize = 24;
    const int c_MinFontSize = 10;
    const int c_minTitleFontSize = 8;
    const int c_maxTitleFontSize = 22;
    const int c_ringSize = 150;
    const int c_SeparatorMargin = 15;
    const int c_RingTopMargin = 10;
    const int c_ChartHeight = 120;
    inline static constexpr int SAMPLE_COUNT = LineChart::SAMPLE_COUNT; // number of samples, period = nb x tick rate
    inline static constexpr wchar_t COLUMN1_MAXTEXT[] = L"Memory Temperature: 99999 Mhz";
    inline static constexpr int GPU_COLUMN1_LENGTH = _countof(COLUMN1_MAXTEXT) - 1;
    inline static constexpr wchar_t COLUMN1_LABEL[] = L"Memory Temperature:";
    inline static constexpr int GPU_COLUMN1_LABEL_MAXLENGTH = _countof(COLUMN1_LABEL) - 1;
    inline static constexpr wchar_t COLUMN2_MAXTEXT[] = L"Range : -30 - 10123 MHz";
    inline static constexpr int GPU_COLUMN2_MAXTEXT_LENGTH = _countof(COLUMN2_MAXTEXT) - 1;
    inline static constexpr wchar_t COLUMN2_MAXLABEL[] = L"Median: ";
    inline static constexpr int GPU_COLUMN2_MAXLABEL_LENGTH = _countof(COLUMN2_MAXLABEL) - 1;
    inline static constexpr int GPU_COLUMN2_MAXVALUE_LENGTH = GPU_COLUMN2_MAXTEXT_LENGTH - GPU_COLUMN2_MAXLABEL_LENGTH;

    GpuTheme::Type m_currentTheme = GpuTheme::Type::RadeonRed;

    HBRUSH bgBrush;
    HBRUSH chartBgBrush;
    HBRUSH barBrush;
    HBRUSH markerBrush;
    HBRUSH borderBrush;

    ProgressRing m_ring;
    ULONG_PTR m_gdiplusToken;
    std::vector<StatRow> m_statRows;

    // Back Buffer
    HDC m_backDC = nullptr;
    HBITMAP m_backBitmap = nullptr;
    HBITMAP m_backOldBitmap = nullptr;
    SIZE m_backBufferSize{};

    LineChart m_chart;

    // History
    int m_history[SAMPLE_COUNT]; // ring buffer implementation
    int m_historyIndex = 0;      // Next position to write
    int m_historyCount = 0;      // Number of valid values
    double m_median;

#ifdef GDIDRAW
    int m_GdiCount = 0;
#endif
};

#ifdef GDIDRAW
#define GDI_COUNT(self) ((self)->m_GdiCount++)
#define GDI_COUNT_N(self, n) ((self)->m_GdiCount += (n))
#else
#define GDI_COUNT(self) ((void)0)
#define GDI_COUNT_N(self, n) ((void)0)
#endif
