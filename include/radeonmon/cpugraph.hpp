#pragma once

#include "radeonmon/structures.hpp"
#include "radeonmon/ryzen.hpp"
#include "radeonmon/colors.hpp"

#include <Windows.h>

#include <string>

class CpuGraphWindow
{
public:
    CpuGraphWindow(RyzenCpu &cpu) : m_cpu(cpu) {}

    ~CpuGraphWindow()
    {
        DeleteObject(bgBrush);
        DeleteObject(chartBgBrush);
        DeleteObject(barBrush);
        DeleteObject(markerBrush);
        DeleteObject(borderBrush);
        DeleteObject(m_hFont);
        DeleteObject(m_titleFont);
    }

    void inline RebuildBrushes()
    {
        const auto &colors = Theme::Get(m_currentTheme);
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
    void DrawCoreBarGraph(HDC hdc, const RECT &rc);
    void CreateUIFont();
    int GetRequiredClientHeight() const;
    void UpdateWindowSize();
    bool SaveSettings();
    bool LoadSettings();
    int GetMinRequiredClientWidth() const;
    void UpdateLayoutRects();

private:
    HWND m_hwnd = nullptr;
    RyzenCpu &m_cpu;
    HFONT m_hFont = nullptr;
    HFONT m_titleFont = nullptr;
    bool m_userClose = false;
    int m_FontHeight = 16;
    int m_FontAscent = 0;
    int m_FontWidth = 0;
    int m_LabelWidth = 0;
    int m_BarLeftMargin = 0;
    int m_MarginTopBottom = 0;
    int m_MarginLeftRight = 0;
    int m_BarHeight = 16;
    int m_Spacing = 0;
    int m_OnePxScaled = 1;
    int m_MarkerWidth = 2;
    std::wstring m_title;
    RECT m_GraphRc{};

    int m_fontSize = 16;
    int m_titleFontSize = 14;

    // window related
    int m_x = -1;
    int m_y = -1;
    int m_width, m_height;
    UINT m_savedDpi;

    // Spacing constants
    const int c_MarginTopBottom = 9;
    const int c_MarginLeftRight = 14;
    const int c_LineSpace = 7;
    const int c_borderWidth = 1;
    const int c_TitlePaddingTopBottom = 5;
    const int c_MinBarGraphWidth = 100;

    Theme::Type m_currentTheme = Theme::Type::SkyBlue;

    HBRUSH bgBrush;
    HBRUSH chartBgBrush;
    HBRUSH barBrush;
    HBRUSH markerBrush;
    HBRUSH borderBrush;
};