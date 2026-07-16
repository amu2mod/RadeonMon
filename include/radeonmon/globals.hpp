#pragma once

#include <windows.h>

#include <cstring>
#include <vector>
#include <mutex>

#include "radeonmon/structures.hpp"
#include "radeonmon/adlx.hpp"
#include "AMD/ADLX-1.5/SDK/Include/IPerformanceMonitoring3.h"
#include "radeonmon/ryzen.hpp"
#include "radeonmon/constants.hpp"
#include "radeonmon/networking.hpp"
#include "radeonmon/webserver.hpp"
#include "radeonmon/processwatcher.hpp"

inline UINT g_dpi = 96;
inline GdiBackBuffer g_backBuffer;
inline PropertyItem g_props[] =
    {
        {L"GPU Temperature", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"GPU Hotspot", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"VRAM Temperature", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"Fan Speed", -1, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"Power Consumption", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"CPU", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"Display", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
        {L"FPS", 0, 0, L"-"},
        {L"", 0, 0, L"", {}, {}, PropertyType::Separator},
};

constexpr int g_propCount = _countof(g_props);
constexpr int g_lineCount = g_propCount / 2;
inline PropertyItem g_cardName = {L"", 0, 0, L"no card detected"};

inline bool g_isFpsEnabled = false;
inline ADLXGpuTelemetry g_AdlxGPUTelemetry(g_isFpsEnabled);
inline RyzenCpu g_cpu;

inline bool g_alwaysOnTop = false;

// Fonts
inline HFONT g_titleFont = nullptr;
inline HFONT g_font = nullptr;
inline HFONT g_notificationFont = nullptr;
inline HFONT g_cardFont = nullptr;

inline UINT g_titleFontSize = TITLE_FONTSIZE;
inline UINT g_fontSize = FONTSIZE;
inline UINT g_notificationFontSize = NOTIFICATION_FONTSIZE;
inline UINT g_cardFontSize = CARD_FONTSIZE;

inline int g_width = APPWIDTH;
inline int g_height = APPHEIGHT;
inline int g_xPos = CW_USEDEFAULT;
inline int g_yPos = CW_USEDEFAULT;
inline RECT g_windowRc;
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

inline PropertyItem g_notification;

inline bool g_autostart;
inline WebServer g_webServer;
inline NetworkManager g_networkManager;
inline PropertyItem g_serverSeparatorRc, g_serverStatusRc;

inline LayoutMetrics g_layoutMetrics;

inline HWND g_hwndTooltip;
inline RECT g_clickableUrlRect = {0};

inline RadeonMon::Hardware::DisplayManager g_displayManager;
inline int g_currentDisplayIndex = 0;

inline int g_currentWebTemplate = IDM_WEBSERVER_TEMPLATE_LIGHT;

inline ProcessWatcher g_processWatcher{g_cpu, g_webServer};

inline AppTitle g_appTitle(APPNAME, APPNAME_LENGTH);