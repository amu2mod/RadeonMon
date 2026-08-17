#pragma once

#include "radeonmon/structures.hpp"
#include "radeonmon/adlx.hpp"
#include "radeonmon/colors.hpp"
#include "radeonmon/ProgressRing.hpp"

#include <Windows.h>

#include <string>
#include <algorithm>
#include <array>

class GpuGraphWindow
{
public:
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

private:
    HWND m_hwnd = nullptr;
    UINT m_dpi;
    ADLXGpuTelemetry &m_adlx;
    HFONT m_hFont = nullptr;
    HFONT m_titleFont = nullptr;
    HFONT m_ringFont = nullptr;
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
    int m_Spacing = 0;
    int m_OnePxScaled = 1;
    int m_MarkerWidth = 2;
    std::wstring m_title;
    int m_metricsCount;
    int m_ringSize;
    int m_borderSize;
    int m_SeparatorMargin = 0;
    int m_Column2Width;
    int m_Colmun2LabelWidth;

    GPU_CAPS m_selected = GPU_CAP_USAGE;

    RECT m_ContentRc{}; // borders + margins + body
    RECT m_BodyRc{};    // labels/value + inner gap + ring
    RECT m_Column1Rc{}; // ring + min/max values
    RECT m_Column2Rc{}; // ring + min/max values
    RECT m_ringRc{};

    const int c_defaultFontSize = 16;
    const int c_defaultTileFontSize = 14;
    const int c_defaultRingFontSize = 14;

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
    const int c_LineSpace = 5;
    const int c_borderWidth = 1;
    const int c_TitlePaddingTopBottom = 5;
    const int c_MinBarGraphWidth = 100;
    const int c_MaxFontSize = 24;
    const int c_MinFontSize = 10;
    const int c_minTitleFontSize = 8;
    const int c_maxTitleFontSize = 22;
    const int c_ringSize = 150;
    const int c_SeparatorMargin = 15;
    inline static constexpr wchar_t COLUMN1_MAXTEXT[] = L"Memory Temperature: 9999 Mhz";
    inline static constexpr int GPU_COLUMN1_LENGTH = _countof(COLUMN1_MAXTEXT) - 1;
    inline static constexpr wchar_t COLUMN2_MAXTEXT[] = L"Range: 0 - 9999 Mhz";
    inline static constexpr int GPU_COLUMN2_MAXTEXT_LENGTH = _countof(COLUMN2_MAXTEXT) - 1;
    inline static constexpr wchar_t COLUMN2_MAXLABEL[] = L"Range: ";
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
};