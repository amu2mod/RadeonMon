#pragma once

#include <windows.h>

#include <cstring>

#include "radeonmon/structures.hpp"
#include "radeonmon/adlx.hpp"
#include "AMD/ADLX-1.5/SDK/Include/IPerformanceMonitoring3.h"
#include "radeonmon/ryzen.hpp"
#include "radeonmon/constants.hpp"

inline UINT g_dpi = 96;
inline HFONT g_font = nullptr;
inline GdiBackBuffer g_backBuffer;
inline PropertyItem g_props[] =
    {
        {L"GPU Temperature", 0, 0, L""},
        {nullptr, 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"GPU Hotspot", 0, 0, L""},
        {nullptr, 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"VRAM Temperature", 0, 0, L""},
        {nullptr, 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"Fan Speed", -1, 0, L""},
        {nullptr, 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"Power Consumption", 0, 0, L""},
        {nullptr, 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"CPU", 0, 0, L""}};

constexpr int g_propCount = _countof(g_props);
inline PropertyItem g_cardName = {L"", 0, 0, L"no card detected"};

inline ADLXGpuTelemetry g_AdlxGPUTelemetry;
inline RyzenCpu g_cpu;

inline bool g_alwaysOnTop = false;
inline UINT g_fontSize = FONTSIZE;
inline int g_width = APPWIDTH;
inline int g_height = APPHEIGHT;
inline int g_xPos = CW_USEDEFAULT;
inline int g_yPos = CW_USEDEFAULT;
inline RECT g_windowRc, g_titleRc, g_titleTextRc;
inline WindowBorder g_border;
inline bool g_forceFrameRedraw = false;
inline bool g_isAdmin = false;
#ifdef _DEBUG
inline UINT g_gdiDrawCallCount = 0;
#endif

// custom mouse-drag handling to avoid dwm management
inline POINT g_dragStart{};
inline RECT g_wndStart{};
inline bool g_dragging = false;