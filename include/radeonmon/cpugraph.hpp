#pragma once

#include "radeonmon/structures.hpp"
#include "radeonmon/ryzen.hpp"

#include <Windows.h>

class CpuGraphWindow
{
public:
    CpuGraphWindow(RyzenCpu &cpu) : m_cpu(cpu) {}

    ~CpuGraphWindow()
    {
        DeleteObject(chartBgBrush);
        DeleteObject(barBrush);
        DeleteObject(markerBrush);
    }

    bool Create(HWND hParent);
    void Show();
    void Close();

    void Update(); // Called from the main timer

    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void DrawCoreBarGraph(HDC hdc, const RECT &rc);
    void CreateUIFont();
    int GetRequiredClientHeight() const;
    void UpdateWindowSize();
    bool SaveSettings();
    bool LoadSettings();

private:
    HWND m_hwnd = nullptr;
    RyzenCpu &m_cpu;
    HFONT m_hFont = nullptr;
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

    // window related
    int m_x = -1;
    int m_y = -1;
    int m_width, m_height;
    UINT m_savedDpi;

    // Spacing constants
    const int c_MarginTopBottom = 9;
    const int c_MarginLeftRight = 14;
    const int c_LineSpace = 7;

    HBRUSH chartBgBrush = CreateSolidBrush(RGB(28, 27, 31)); // Original Dark Charcoal background
    HBRUSH barBrush = CreateSolidBrush(RGB(56, 189, 248));   // Sky Blue (high contrast against charcoal)
    HBRUSH markerBrush = CreateSolidBrush(RGB(74, 71, 78));  // Original Muted Grey markers
};