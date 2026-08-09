#pragma once

#include "radeonmon/structures.hpp"
#include "radeonmon/ryzen.hpp"
#include "radeonmon/colors.hpp"
#include "radeonmon/processwatcher.hpp"

#include <Windows.h>

#include <string>
#include <algorithm>
#include <array>

class ProcessWatcher;

class CpuGraphWindow
{
public:
    CpuGraphWindow(RyzenCpu &cpu, ProcessWatcher &processWatcher) : m_cpu(cpu), m_processWatcher(processWatcher) {}

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
    bool isViewProcessesEnabled() { return m_showProcesses; }

    inline bool isActive() const { return m_hwnd != nullptr; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void DrawCoreBarGraph(HDC hdc, const RECT &rc);
    void CreateUIFont(UINT dpi);
    int GetRequiredClientHeight(UINT dpi) const;
    void UpdateWindowHeight();
    bool SaveSettings();
    bool LoadSettings();
    int GetMinRequiredClientWidth(UINT dpi) const;
    void UpdateLayoutRects();
    void OnResizeWindow(bool grow);
    void OnProcessesClicked();

    // Helper to be called after loading the default font size from preferences
    inline int GetScaledTitleFontSize() const
    {
        const int scaled = static_cast<int>(std::lround(m_fontSize * static_cast<float>(c_defaultTileFontSize) / c_defaultFontSize));

        return std::clamp(scaled, c_minTitleFontSize, c_maxTitleFontSize);
    }

private:
    HWND m_hwnd = nullptr;
    RyzenCpu &m_cpu;
    HFONT m_hFont = nullptr;
    HFONT m_titleFont = nullptr;
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
    RECT m_GraphRc{};

    const int c_defaultFontSize = 16;
    const int c_defaultTileFontSize = 14;

    int m_fontSize = c_defaultFontSize;
    int m_titleFontSize = c_defaultTileFontSize;
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
    const int c_LineSpace = 7;
    const int c_borderWidth = 1;
    const int c_TitlePaddingTopBottom = 5;
    const int c_MinBarGraphWidth = 100;
    const int c_MaxFontSize = 24;
    const int c_MinFontSize = 10;
    const int c_minTitleFontSize = 8;
    const int c_maxTitleFontSize = 22;
    const int c_ProcessPadding = 0;

    Theme::Type m_currentTheme = Theme::Type::SkyBlue;

    HBRUSH bgBrush;
    HBRUSH chartBgBrush;
    HBRUSH barBrush;
    HBRUSH markerBrush;
    HBRUSH borderBrush;

    // Header / process toggle.
    RECT m_processRC{};
    bool m_showProcesses = false;

    ProcessWatcher &m_processWatcher;
    int m_maxProcess = 5;
};