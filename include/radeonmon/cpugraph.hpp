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

private:
    HWND m_hwnd = nullptr;
    RyzenCpu &m_cpu;
    HFONT m_hFont = nullptr;
    int m_FontHeight = 16;
    int m_FontAscent = 0;

    // HBRUSH chartBgBrush = CreateSolidBrush(RGB(28, 27, 31));
    // HBRUSH barBrush = CreateSolidBrush(RGB(255, 122, 89));
    // HBRUSH markerBrush = CreateSolidBrush(RGB(74, 71, 78));

    // HBRUSH chartBgBrush = CreateSolidBrush(RGB(15, 23, 42)); // Dark Slate / Navy background
    // HBRUSH barBrush = CreateSolidBrush(RGB(59, 130, 246));   // Vibrant Electric Blue (primary highlight)
    // HBRUSH markerBrush = CreateSolidBrush(RGB(51, 65, 85));  // Muted Blue-Grey markers

    HBRUSH chartBgBrush = CreateSolidBrush(RGB(28, 27, 31)); // Original Dark Charcoal background
    HBRUSH barBrush = CreateSolidBrush(RGB(56, 189, 248));   // Sky Blue (high contrast against charcoal)
    HBRUSH markerBrush = CreateSolidBrush(RGB(74, 71, 78));  // Original Muted Grey markers
};