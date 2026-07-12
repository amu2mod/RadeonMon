
#include "radeonmon/webserver.hpp"

#include <windows.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <cstdio>
#include <cmath>
#include <cassert>
#include <algorithm>

#include "radeonmon/globals.hpp"
#include "radeonmon/constants.hpp"
#include "radeonmon/structures.hpp"
#include "radeonmon/helpers.hpp"
#include "radeonmon/adlx.hpp"
#include "radeonmon/logging.hpp"
#include "radeonmon/ryzen.hpp"
#include "radeonmon/ryzen_sdk.hpp"
#include "radeonmon/preferences.hpp"
#include "radeonmon/version_checker.hpp"
#include "radeonmon/autostart.hpp"

#include <version.hpp>

using namespace RadeonMon::Hardware;

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "comctl32.lib")

enum MetricsIndex
{
    Temp = 0,
    Hotspot = 2,
    Vram = 4,
    FanSpeed = 6,
    Power = 8,
    Cpu = 10,
    Display = 12,
    Fps = 14
};

void SetDisplayLine(const DisplayInfo &display, HWND hwnd = nullptr)
{
    PropertyItem &prop = g_props[MetricsIndex::Display];
    const std::wstring label = L"Display " + std::to_wstring(display.index + 1);
    int widthToDisplay = display.isPortrait ? display.width : display.height;
    std::wstring resolution = widthToDisplay == 4320 ? L"8K @" : widthToDisplay == 2160 ? L"4K @"
                                                                                        : std::to_wstring(widthToDisplay) + L"p @";
    std::wstring value = resolution + std::to_wstring(display.frequency) + L"Hz";

    if (value.length() < MAXLABEL_LENGTH)
        value.append(MAXLABEL_LENGTH - value.length(), L' ');

    prop.SetLabel(label.c_str());
    prop.SetValue(value.c_str());
    prop.dirty = true;
    prop.repaintLabel = true;

    if (hwnd)
    {
        RECT r = {prop.labelRc.left, prop.labelRc.top, prop.valueRc.right, prop.valueRc.bottom};
        InvalidateRect(hwnd, &r, FALSE);
        UpdateWindow(hwnd);
    }
}

void DeleteFonts()
{

    if (g_titleFont)
    {
        DeleteObject(g_titleFont);
        g_titleFont = nullptr;
    }
    if (g_font)
    {
        DeleteObject(g_font);
        g_font = nullptr;
    }

    if (g_notificationFont)
    {
        DeleteObject(g_notificationFont);
        g_notificationFont = nullptr;
    }

    if (g_cardFont)
    {
        DeleteObject(g_cardFont);
        g_cardFont = nullptr;
    }
}

void UpdateToolTipText(HWND hwnd, UINT_PTR toolId, const std::wstring &text, int maxWidth)
{
    static std::wstring tooltipText;
    tooltipText = text;

    TOOLINFO ti = {};
    ti.cbSize = sizeof(TOOLINFO);
    ti.hwnd = hwnd;
    ti.uId = toolId;
    ti.lpszText = const_cast<LPWSTR>(tooltipText.c_str());

    SendMessage(g_hwndTooltip, TTM_UPDATETIPTEXT, 0, reinterpret_cast<LPARAM>(&ti));
    SendMessage(g_hwndTooltip, TTM_SETMAXTIPWIDTH, 0, maxWidth); // set max width large enough for multiline
}

void UpdateToolTipRect(HWND hwnd, UINT_PTR toolId, const RECT &rect)
{
    TOOLINFO ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uId = toolId;
    ti.rect = rect;

    SendMessage(g_hwndTooltip, TTM_NEWTOOLRECT, 0, reinterpret_cast<LPARAM>(&ti));
}

void InitTooltips(void)
{
    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
}

LayoutMetrics CalculateLayoutMetrics()
{
    LayoutMetrics m{};

    HDC hdc = g_backBuffer.memDC;

    SIZE sz{};

    HFONT originalFont = (HFONT)SelectObject(hdc, g_font);
    if (!originalFont)
        LOG_ERROR("SelectObject failed on g_font");

    // ------------------------------------------------------------
    // Calculate font scale from g_font (design size = FONTSIZE)
    // ------------------------------------------------------------
    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    const int fontHeight = sz.cy;

    const float fontScale = static_cast<float>(fontHeight) / static_cast<float>(FONTSIZE);

    auto ScaleFontMetric = [&](int value) -> int
    {
        return max(1, static_cast<int>(roundf(value * fontScale)));
    };

    LOG_DEBUG("[UI] fontHeight=%ld scale=%.2f", fontHeight, fontScale);

    // ------------------------------------------------------------
    // Width metrics (scaled constants only)
    // ------------------------------------------------------------
    m.border = ScaleFontMetric(BORDER);
    m.paddingSide = ScaleFontMetric(PADDING_LEFT);
    m.gap = ScaleFontMetric(GAP);

    static constexpr wchar_t LABEL[] = L"Power Consumption";
    if (!GetTextExtentPoint32W(hdc, LABEL, _countof(LABEL) - 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived: no scaling
    m.labelWidth = sz.cx + 2;

    if (!GetTextExtentPoint32W(hdc, MAXLABEL, MAXLABEL_LENGTH, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived: no scaling
    m.valueWidth = sz.cx + 2;

    // ------------------------------------------------------------
    // Shared
    // ------------------------------------------------------------
    m.paddingTop = ScaleFontMetric(PADDING_TOP);
    m.paddingBottom = ScaleFontMetric(PADDING_BOTTOM);

    // ------------------------------------------------------------
    // Title font
    // ------------------------------------------------------------
    if (!SelectObject(hdc, g_titleFont))
        LOG_ERROR("SelectObject failed on g_titleFont");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    const int titleFontHeight = sz.cy;
    const int titleFontWidth = sz.cx;

    // Font-independent spacing: scale
    m.titlePadding = ScaleFontMetric(TITLE_PADDING);

    // Font-derived: no scaling
    m.titleHeight = titleFontHeight;

    if (!GetTextExtentPoint32W(hdc, APPNAME, APPNAME_LENGTH, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived: no scaling
    m.titleWidth = sz.cx + titleFontWidth;

    // ------------------------------------------------------------
    // Body font
    // ------------------------------------------------------------
    if (!SelectObject(hdc, g_font))
        LOG_ERROR("SelectObject failed on g_font");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived
    m.lineHeight = sz.cy;

    // Constants: scale
    m.lineGap = ScaleFontMetric(LINE_GAP);
    m.separatorHeight = ScaleFontMetric(SEPARATOR_HEIGHT);
    m.spacer = ScaleFontMetric(SPACER);

    // ------------------------------------------------------------
    // Notification font
    // ------------------------------------------------------------
    if (!SelectObject(hdc, g_notificationFont))
        LOG_ERROR("SelectObject failed on g_notificationFont");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived
    m.lineHeight2 = sz.cy;

    // ------------------------------------------------------------
    // Card font
    // ------------------------------------------------------------
    if (!SelectObject(hdc, g_cardFont))
        LOG_ERROR("SelectObject failed on g_cardFont");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived
    m.cardHeight = sz.cy;

    SelectObject(hdc, originalFont);

    // ------------------------------------------------------------
    // Window
    // ------------------------------------------------------------
    m.windowWidth =
        m.border * 2 +
        m.paddingSide * 2 +
        m.labelWidth +
        m.gap +
        m.valueWidth;

    m.windowHeight =
        m.titlePadding +
        m.titleHeight +
        m.titlePadding +
        m.paddingTop +
        m.lineHeight * g_lineCount +
        m.lineGap * g_lineCount +
        m.separatorHeight * g_lineCount +
        m.lineGap * g_lineCount +
        m.spacer +
        m.lineHeight2 +
        m.lineGap +
        m.lineHeight2 +
        m.lineGap +
        m.cardHeight +
        m.paddingBottom +
        m.border;

    LOG_DEBUG("[UI] window=%dx%d", m.windowWidth, m.windowHeight);

    return m;
}

void ResetDirty()
{
    g_notification.dirty = true;
    g_cardName.dirty = true;
    g_serverSeparatorRc.dirty = true;
    g_serverStatusRc.dirty = true;
    MarkAllPropsDirty(g_props);
}

bool SetPropertyValueAtLine(int line, LPCWSTR textValue, int valueSize, bool warning = false)
{
    /**
     * line 1 = 0
     * line 2 = 2
     * line 3 = 4
     * line 4 = 6
     */
    // Each logical line occupies 2 slots: [value][separator]
    const int index = 2 * (line - 1);

    // debug-only safety check
    assert((index % 2) == 0);

    if (index < 0 || index >= g_propCount)
        // TODO: log error index
        return false;

    PropertyItem &p = g_props[index];
    lstrcpynW(p.textValue, textValue, valueSize);
    p.dirty = true;
    p.warning = warning;

    return true;
}

bool SetPropertyValueAtIndex(int index, int value, LPCWSTR textValue, int valueSize, bool warning = false)
{
    if (index < 0 || index >= g_propCount)
    {
        LOG_ERROR("Wrong index: %d", index);
        return false;
    }

    PropertyItem &p = g_props[index];
    p.value = value;
    lstrcpynW(p.textValue, textValue, valueSize);
    p.dirty = true;
    p.warning = warning;

    return true;
}

bool SetPropertyValue2OnlyAtIndex(int index, int value)
{
    if (index < 0 || index >= g_propCount)
    {
        LOG_ERROR("Wrong index: %d", index);
        return false;
    }

    PropertyItem &p = g_props[index];
    p.value2 = value;

    return true;
}

int GetPropertyValueAtIndex(int index, bool secondValue = false)
{
    if (index < 0 || index >= g_propCount)
        return -1;

    return secondValue ? g_props[index].value2 : g_props[index].value;
}

void SetAllRepaintLabelFlag(bool state)
{
    for (PropertyItem &p : g_props)
        p.repaintLabel = state;
}

void LayoutFrame(const LayoutMetrics &m)
{
    g_border.top = {0, 0, m.windowWidth, m.titleHeight + m.titlePadding * 2};
    g_border.bottom = {0, m.windowHeight - m.border, m.windowWidth, m.windowHeight};
    g_border.left = {0, m.border, m.border, m.windowHeight - m.border};
    g_border.right = {m.windowWidth - m.border, m.border, m.windowWidth, m.windowHeight - m.border};

    // center the title inside the top border
    int x = (m.windowWidth - m.titleWidth) / 2;
    int y = (g_border.top.bottom - m.titleHeight) / 2;
    g_titleRc = {x, y, x + m.titleWidth, y + m.titleHeight};
    // g_titleTextRc = g_titleRc;

#ifdef _DEBUG
    // LOG_DEBUG("LayoutFrame: window={%ld,%ld,%ld,%ld} border=%d titleHeight=%d", rc.left, rc.top, rc.right, rc.bottom, border, titleHeight);

    // DebugRect("border.top", g_border.top);
    // DebugRect("border.bottom", g_border.bottom);
    // DebugRect("border.left", g_border.left);
    // DebugRect("border.right", g_border.right);

    // DebugRect("title", g_titleRc);
#endif
}

void LayoutProperties2(const LayoutMetrics &m)
{
    LOG_DEBUG("LayoutProperties2");

    if (m.windowWidth <= 0 || m.windowHeight <= 0)
    {
        LOG_ERROR("[LayoutProperties2] incorrect metrics");
        return;
    }

    const int leftEdge = m.border + m.paddingSide;
    const int rightEdge = m.windowWidth - m.border - m.paddingSide;

    int y = m.titleHeight + m.titlePadding * 2 + m.paddingTop;

    // Window
    g_windowRc = {0, 0, m.windowWidth, m.windowHeight};

    // Frame + title
    LayoutFrame(m);

    // Properties
    for (auto &p : g_props)
    {
        if (p.type == PropertyType::Separator)
        {
            y += m.lineGap;
            p.labelRc = {leftEdge, y, rightEdge, y + m.separatorHeight};
            y += m.separatorHeight;
            y += m.lineGap;
            continue;
        }

        p.labelRc = {leftEdge, y, leftEdge + m.labelWidth, y + m.lineHeight};
        p.valueRc = {leftEdge + m.labelWidth + m.gap, y, rightEdge, y + m.lineHeight};

        y += m.lineHeight;
    }

    // Spacer
    y += m.spacer;

    // Server status
    g_serverStatusRc.valueRc = {leftEdge, y, rightEdge, y + m.lineHeight2};
    y += m.lineHeight2;

    // Notification
    y += m.lineGap;
    g_notification.valueRc = {leftEdge, y, rightEdge, y + m.lineHeight2};
    y += m.lineHeight2;
    y += m.lineGap;

    // Card name
    g_cardName.valueRc = {leftEdge, y, rightEdge, y + m.cardHeight};
    y += m.cardHeight;

#ifdef _DEBUG
    const int expectedBottom = m.windowHeight - m.border - m.paddingBottom;
    if (y != expectedBottom)
    {
        LOG_ERROR("[LayoutProperties2] Layout mismatch: y=%d expected=%d", y, expectedBottom);
    }
#endif
}

void PaintProperties(HDC hdc)
{
    static HPEN pen = CreatePen(PS_SOLID, 1, SEPARATORCOLOR);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

    // Paint regular properties
    for (auto &p : g_props)
    {
        if (p.type == PropertyType::Separator && p.dirty)
        {
            MoveToEx(hdc, p.labelRc.left, p.labelRc.top, nullptr);
            LineTo(hdc, p.labelRc.right, p.labelRc.top);

#ifdef _DEBUG
            g_gdiDrawCallCount++;
#endif

            p.dirty = false;
            continue;
        }

        if (!p.dirty)
            continue;

        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, BACKGROUNDCOLOR);
        // SetBkColor(hdc, RGB(255, 255, 255)); // test

        if (p.repaintLabel)
        {
            SetTextColor(hdc, LABELCOLOR);

            SIZE sz{};
            // int len = (int)wcslen(p.label);
            int len = static_cast<int>(p.label.length());
            GetTextExtentPoint32W(hdc, p.label.c_str(), len, &sz);

            int x = p.labelRc.left;
            int y = p.labelRc.top + ((p.labelRc.bottom - p.labelRc.top) - sz.cy) / 2;

            ExtTextOutW(hdc, x, y, ETO_CLIPPED, &p.labelRc, p.label.c_str(), len, nullptr);

            p.repaintLabel = false;

#ifdef _DEBUG
            g_gdiDrawCallCount++;
#endif
        }

        SetTextColor(hdc, p.warning ? WARNINGCOLOR : VALUECOLOR);

        SIZE sz{};
        int len = (int)wcslen(p.textValue);
        GetTextExtentPoint32W(hdc, p.label.c_str(), len, &sz);

        int x = p.valueRc.left;
        int y = p.valueRc.top + ((p.valueRc.bottom - p.valueRc.top) - sz.cy) / 2;

        ExtTextOutW(hdc, x, y, ETO_CLIPPED, &p.valueRc, p.textValue, len, nullptr);

#ifdef _DEBUG
        g_gdiDrawCallCount++;
#endif

        p.dirty = false;
    }

    if (g_notification.dirty)
    {
        g_notification.DrawTextValue(g_backBuffer.memDC, NOTIFICATIONCOLOR, g_notificationFont);
#ifdef _DEBUG
        g_gdiDrawCallCount++;
#endif
        g_notification.dirty = false;
    }

    // Paint server separator
    if (g_serverSeparatorRc.dirty)
    {
        MoveToEx(hdc, g_serverSeparatorRc.valueRc.left, g_serverSeparatorRc.valueRc.top, nullptr);
        LineTo(hdc, g_serverSeparatorRc.valueRc.right, g_serverSeparatorRc.valueRc.top);

#ifdef _DEBUG
        g_gdiDrawCallCount++;
#endif

        g_serverSeparatorRc.dirty = false;
    }

    // Paint server status
    if (g_serverStatusRc.dirty)
    {
        g_serverStatusRc.DrawTextValue(g_backBuffer.memDC, SERVERSTATUSCOLOR, g_notificationFont);

#ifdef _DEBUG
        g_gdiDrawCallCount++;
#endif

        g_serverStatusRc.dirty = false;
    }

    // Paint Card Name
    if (g_cardName.dirty)
    {
        RECT rc = g_cardName.valueRc;
        // LOG_DEBUG("card RECT = %d %d %d %d", rc.left, rc.top, rc.right, rc.bottom);

        // Fill background
        SetBkMode(hdc, OPAQUE);
        SetBkColor(hdc, BACKGROUNDCOLOR);
        ExtTextOutW(hdc, 0, 0, ETO_OPAQUE, &rc, nullptr, 0, nullptr);

        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        SelectObject(hdc, oldBrush);

        // Draw centered text
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, LABELCOLOR);

        HFONT oldFont = (HFONT)SelectObject(hdc, g_cardFont);
        DrawTextW(hdc, g_cardName.textValue, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, oldFont);

#ifdef _DEBUG
        g_gdiDrawCallCount += 3;
#endif

        g_cardName.dirty = false;
    }

    SelectObject(hdc, oldPen);
}

void SetAlwaysOnTop(HWND hWnd, bool enable)
{
    SetWindowPos(hWnd, enable ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
}

void OnResizeWindow(HWND hwnd, bool grow)
{
    UINT oldFontSize = g_fontSize;

    if (grow)
        g_fontSize = min(g_fontSize + 2, (UINT)FONTSIZE_MAX);
    else
        g_fontSize = max(g_fontSize - 2, (UINT)FONTSIZE_MIN);

    // Already at min/max
    if (g_fontSize == oldFontSize)
        return;

    RecreateFont();
    g_forceFrameRedraw = true;
    SetAllRepaintLabelFlag(true);

    PostMessage(hwnd, WM_APP + 1, 0, 0);

    InvalidateRect(hwnd, nullptr, TRUE);

    // LOG_DEBUG("new window: %dx%d, font@%dpx", rc.right - rc.left, rc.bottom - rc.top, g_fontSize);
}

void PaintFrame(HDC hdc)
{
    static bool drawn = false;
    static HBRUSH frameBrush = CreateSolidBrush(BORDERCOLOR);

    if (!g_forceFrameRedraw && drawn)
        return;

    LOG_DEBUG("Painting FRAME (border+title)");

    drawn = true;
    g_forceFrameRedraw = false;

    // Border sides
    FillRect(hdc, &g_border.top, frameBrush);
    FillRect(hdc, &g_border.bottom, frameBrush);
    FillRect(hdc, &g_border.left, frameBrush);
    FillRect(hdc, &g_border.right, frameBrush);

    // Title text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    // DrawText(hdc, APPNAME, -1, &g_titleRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    TextOutW(hdc, g_titleRc.left, g_titleRc.top, APPNAME, APPNAME_LENGTH);

#ifdef _DEBUG
    g_gdiDrawCallCount += 5;
#endif
}

void UpdateCurrentPosition(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    g_xPos = rc.left;
    g_yPos = rc.top;
}

void Cleanup(HWND hwnd)
{
    g_backBuffer.Destroy();
    DeleteFonts();
    UpdateCurrentPosition(hwnd);
    SavePreferences();
    g_networkManager.Shutdown();
    g_AdlxGPUTelemetry.Destroy();
}

// ── window procedure ─────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        LOG_DEBUG("WM_CREATE");
        RecreateFont();

        RECT rc{};
        GetClientRect(hwnd, &rc);

        HDC hdc = GetDC(hwnd);
        g_backBuffer.Create(hdc, rc.right - rc.left, rc.bottom - rc.top, BACKGROUNDCOLOR);
        ReleaseDC(hwnd, hdc);

        /////////////////////
        // Gpu Info Tooltip
        g_hwndTooltip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, GetModuleHandle(NULL), NULL);
        SetWindowPos(g_hwndTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        RECT textRect = {0, 0, 200, 200};

        TOOLINFO ti = {};
        ti.cbSize = sizeof(ti);
        ti.uFlags = TTF_SUBCLASS;
        ti.hwnd = hwnd;
        ti.uId = 1;
        ti.rect = textRect;
        /////////////////////

        SendMessage(g_hwndTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
        SendMessage(g_hwndTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 0); // starts immediatly

        PostMessage(hwnd, WM_APP + 1, 0, 0);

        return 0;
    }

    case WM_APP + 1:
    {
        g_layoutMetrics = CalculateLayoutMetrics();
        SetWindowPos(hwnd, nullptr, 0, 0, g_layoutMetrics.windowWidth, g_layoutMetrics.windowHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutProperties2(g_layoutMetrics);
        UpdateToolTipRect(hwnd, TOOLID_GPUINFO, g_cardName.valueRc);

        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        if (!g_backBuffer.memDC)
        {
            EndPaint(hwnd, &ps);
            return 0;
        }

        // RECT rc{};
        // GetClientRect(hwnd, &rc);

        HDC memDC = g_backBuffer.memDC;

        // Clear backbuffer
        // FillRect(memDC, &rc, g_backgroundBrush);

        SetBkMode(memDC, TRANSPARENT);

        if (g_font)
            SelectObject(memDC, g_font);

        // Window chrome
        PaintFrame(memDC);

        // Content
        PaintProperties(memDC);

        // Present
        // BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, memDC, 0, 0, SRCCOPY);
        BitBlt(hdc, 0, 0, g_backBuffer.width, g_backBuffer.height, memDC, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);

        static bool firstPaintDone = false;
        if (!firstPaintDone)
        {
            firstPaintDone = true;
            SetAllRepaintLabelFlag(false);
        }

#ifdef _DEBUG
        // LOG_DEBUG("GDI draw count=%u", g_gdiDrawCallCount); // very verbose
        g_gdiDrawCallCount = 0;
#endif

        return 0;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);

        LOG_DEBUG("WM_SIZE: received params w=%d, h=%d", w, h);

        if (w > 0 && h > 0)
        {
            if (w != g_backBuffer.width || h != g_backBuffer.height)
            {
                HDC hdc = GetDC(hwnd);
                g_backBuffer.Create(hdc, w, h, BACKGROUNDCOLOR);
                ReleaseDC(hwnd, hdc);
            }

            ResetDirty();
        }

        // InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Return non-zero to tell Windows we handled erasing

    case WM_DPICHANGED:
    {
        g_dpi = HIWORD(wParam);

        RecreateFont();

        const RECT *r = reinterpret_cast<const RECT *>(lParam);

        g_width = r->right - r->left;
        g_height = r->bottom - r->top;

        SetWindowPos(hwnd, nullptr, r->left, r->top, g_width, g_height, SWP_NOZORDER | SWP_NOACTIVATE);
        SetAllRepaintLabelFlag(true); // force all labels repaint
        g_forceFrameRedraw = true;    // force new border + title

        PostMessage(hwnd, WM_APP + 1, 0, 0);

        InvalidateRect(hwnd, nullptr, TRUE);

        LOG_DEBUG("New DPI: %u (%.0f%%), %dxx%d", g_dpi, (g_dpi / 96.0) * 100.0, g_width, g_height);

        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == APP_POLLING_ID)
        {
            if (!g_AdlxGPUTelemetry.isInitialized)
            {
                LOG_WARN("adlx not init");
                g_AdlxGPUTelemetry.Init(); // retry
                return 0;
            }

            bool dirty = false;

            // START_CHRONO(adlx);
            g_AdlxGPUTelemetry.Tick();
            GpuMetricsSnapshot snapshot = g_AdlxGPUTelemetry.Get();
            // END_CHRONO(adlx, "ADLX");

            if (!snapshot.valid)
                return 0;

            // GPU Temp
            if (snapshot.temperature.isSupported && GetPropertyValueAtIndex(MetricsIndex::Temp) != snapshot.temperature.value)
            {
                dirty = true;
                wchar_t tempBuffer[16];
                FormatTemperature(tempBuffer, static_cast<int>(snapshot.temperature.value));
                SetPropertyValueAtIndex(MetricsIndex::Temp, static_cast<int>(snapshot.temperature.value), tempBuffer, 16, snapshot.temperature.value >= TEMPERATURE_THRESHOLD);
            }

            // Hotspot
            if (snapshot.hotspot.isSupported && GetPropertyValueAtIndex(MetricsIndex::Hotspot) != snapshot.hotspot.value)
            {
                dirty = true;
                wchar_t hotspotBuffer[16];
                FormatHotspot(hotspotBuffer, static_cast<int>(snapshot.temperature.value), static_cast<int>(snapshot.hotspot.value));
                SetPropertyValueAtIndex(MetricsIndex::Hotspot, static_cast<int>(snapshot.hotspot.value), hotspotBuffer, 16, snapshot.hotspot.value >= TEMPERATURE_THRESHOLD);
            }

            // VRAM Temperature
            if (snapshot.memoryTemperature.isSupported && GetPropertyValueAtIndex(MetricsIndex::Vram) != snapshot.memoryTemperature.value)
            {
                dirty = true;
                wchar_t vramBuffer[16];
                FormatTemperature(vramBuffer, static_cast<int>(snapshot.memoryTemperature.value));
                SetPropertyValueAtIndex(MetricsIndex::Vram, static_cast<int>(snapshot.memoryTemperature.value), vramBuffer, 16, snapshot.memoryTemperature.value >= TEMPERATURE_THRESHOLD);
            }

            // Fan Speed
            if (snapshot.fanSpeed.isSupported && GetPropertyValueAtIndex(MetricsIndex::FanSpeed) != snapshot.fanSpeed.value)
            {
                dirty = true;
                wchar_t fanBuffer[16];
                FormatFanSpeed(fanBuffer, snapshot.fanSpeed.value);
                SetPropertyValueAtIndex(MetricsIndex::FanSpeed, snapshot.fanSpeed.value, fanBuffer, 16);
            }

            // Power Consumption
            if (snapshot.totalBoardPower.isSupported && GetPropertyValueAtIndex(MetricsIndex::Power) != snapshot.totalBoardPower.value)
            {
                dirty = true;
                wchar_t powerBuffer[16];
                FormatPowerConsumption(powerBuffer, static_cast<int>(snapshot.totalBoardPower.value));
                SetPropertyValueAtIndex(MetricsIndex::Power, static_cast<int>(snapshot.totalBoardPower.value), powerBuffer, 16);
            }

            // CPU
            if (g_cpu.IsInitialized())
            {
                // START_CHRONO(cpu);
                RyzenMetrics cpuMetrics = g_cpu.GetMetrics();
                // END_CHRONO(cpu, "CPU");
                // round to nearest integer for more accuracy than truncating
                const int cpuIntegerTemp = static_cast<int>(std::round(cpuMetrics.dTemperature));
                const int cpuIntegerPower = static_cast<int>(std::round(cpuMetrics.dPower));
                // LOG_DEBUG("CPU Temp: %0.2f -> %d°C", cpuMetrics.dTemperature, cpuIntegerTemp);
                // LOG_DEBUG("CPU Power: %0.2f -> %dW", cpuMetrics.dPower, cpuIntegerPower);

                if ((GetPropertyValueAtIndex(MetricsIndex::Cpu) != cpuIntegerTemp) || (GetPropertyValueAtIndex(MetricsIndex::Cpu, true) != cpuIntegerPower))
                {
                    dirty = true;
                    wchar_t cpuBuffer[20];
                    FormatCpuMetrics(cpuBuffer, cpuIntegerTemp, cpuIntegerPower);
                    SetPropertyValueAtIndex(MetricsIndex::Cpu, cpuIntegerTemp, cpuBuffer, 16);
                    SetPropertyValue2OnlyAtIndex(MetricsIndex::Cpu, cpuIntegerPower);
                }
            }

            // FPS
            int old = GetPropertyValueAtIndex(MetricsIndex::Fps);
            int current = snapshot.fps;
            if (current == -1 && old != -2)
            {
                g_props[MetricsIndex::Fps].ClearRC(g_backBuffer.memDC, BACKGROUNDCOLOR);
                SetPropertyValueAtIndex(MetricsIndex::Fps, -2, L"-", 2);
                // LOG_DEBUG("clear");
            }

            if (current != -1)
            {
                dirty = true;
                wchar_t fpsBuffer[16];
                int delta = current - old;
                FormatFPS(fpsBuffer, current, old);
                SetPropertyValueAtIndex(MetricsIndex::Fps, snapshot.fps, fpsBuffer, 16, delta <= -10);
            }

            if (dirty)
                InvalidateRect(hwnd, nullptr, FALSE);
        }
        else if (wParam == NETWORK_TIMER_ID)
        {
            KillTimer(hwnd, NETWORK_TIMER_ID);
            g_networkManager.Refresh();
            g_networkManager.Log();
            g_networkManager.m_timer = 0;
        }
        return 0;
    }

    case WM_CONTEXTMENU:
    {
        HMENU hMenu = CreatePopupMenu();

        AppendMenu(hMenu, MF_STRING | (g_isAdmin ? MF_GRAYED : 0), IDM_RESTART_AS_ADMIN, L"Restart as Admin");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(hMenu, MF_STRING | (g_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_ALWAYS_ON_TOP, L"Always on Top");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(hMenu, MF_STRING | (g_autostart ? MF_CHECKED : MF_UNCHECKED), IDM_AUTOSTART, L"Autostart");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);

        ////////////////////////////////////////////////////////
        // Web Server submenu
        HMENU hWebServerMenu = CreatePopupMenu();
        const auto &list = g_networkManager.GetAddresses();
        const UINT flags = MF_STRING | (g_webServer.IsRunning() || !g_isAdmin) ? MF_DISABLED : 0;

        if (g_webServer.IsRunning())
        {
            AppendMenu(hWebServerMenu, MF_STRING, IDM_WEBSERVER_STOP, L"Stop server");
            AppendMenu(hWebServerMenu, MF_SEPARATOR, 0, nullptr);
        }

        if (!g_isAdmin)
        {
            AppendMenu(hWebServerMenu, MF_STRING, 0, L"Admin rights required");
            AppendMenu(hWebServerMenu, MF_SEPARATOR, 0, nullptr);
        }

        for (size_t i = 0; i < list.size(); ++i)
            AppendMenuW(hWebServerMenu, flags | (g_webServer.isBoundTo(list[i]) ? MF_CHECKED : MF_UNCHECKED), IDM_WEBSERVER_BASE + static_cast<UINT>(i), list[i].display().c_str());

        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hWebServerMenu), L"Web Server");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        ////////////////////////////////////////////////////////

        AppendMenu(hMenu, MF_STRING, IDM_CHECK_VERSION, L"Check update");
        AppendMenu(hMenu, MF_STRING, IDM_ABOUT, L"About");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(hMenu, MF_STRING, IDM_EXIT, L"Exit");

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        if (pt.x == -1 && pt.y == -1)
        {
            RECT rc;
            GetWindowRect(hwnd, &rc);
            pt.x = rc.left + 20;
            pt.y = rc.top + 20;
        }

        TrackPopupMenu(hMenu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);

        DestroyMenu(hMenu);
        return 0;
    }

    case WM_COMMAND:
    {
        switch (LOWORD(wParam))
        {
        case IDM_RESTART_AS_ADMIN:
        {
            if (!g_isAdmin)
            {
                UpdateCurrentPosition(hwnd);
                SavePreferences(); // to save current position

                wchar_t exePath[MAX_PATH];

                if (GetModuleFileNameW(nullptr, exePath, MAX_PATH))
                {
                    SHELLEXECUTEINFOW sei = {};
                    sei.cbSize = sizeof(sei);
                    sei.lpVerb = L"runas";
                    sei.lpFile = exePath;
                    sei.nShow = SW_SHOWNORMAL;

                    if (ShellExecuteExW(&sei))
                    {
                        // Close the current non-admin instance
                        PostQuitMessage(0);
                    }
                }
            }

            return 0;
        }

        case IDM_ALWAYS_ON_TOP:
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            break;

        case IDM_AUTOSTART:
        {
            g_autostart = !g_autostart;

            if (g_autostart)
            {
                if (!EnableStartupShortcut())
                    g_autostart = false;
            }
            else
            {
                if (!DisableStartupShortcut())
                    g_autostart = true;
            }

            HMENU hMenu = GetMenu(hwnd);
            CheckMenuItem(hMenu, IDM_AUTOSTART, MF_BYCOMMAND | (g_autostart ? MF_CHECKED : MF_UNCHECKED));

            break;
        }

        case IDM_CHECK_VERSION:
        {
            std::wstring latestVersion;

            if (!VersionChecker::GetLatestVersion(latestVersion))
            {
                LOG_ERROR("Failed to check update");
                ShowUpdateDialog(hwnd, L"Update Check", L"Error", L"Failed to check for updates.", true);
                break;
            }

            LOG_DEBUG("Latest version found: %ls", latestVersion.c_str());

            if (latestVersion != Version::String)
            {
                std::wstring message = L"Current version: " + std::wstring(Version::String) + L"\n" +
                                       L"Latest version: " + latestVersion + L"\n" +
                                       +L"\n" +
                                       L"<a href=\"" +
                                       LATESTURL + L"\">Download latest version here</a>\n";
                ShowUpdateDialog(hwnd, L"Update Check", L"Update available", message);
            }
            else
            {
                std::wstring message = L"You are already running the latest version:\n" + std::wstring(Version::String);
                ShowUpdateDialog(hwnd, L"Update Check", L"No update available", message);
            }

            break;
        }

        case IDM_ABOUT:
        {
            std::wstring message = L"Version " + std::wstring(Version::String) + L"\r\n" +
                                   L"\r\n"
                                   L"Free and open source software\r\n"
                                   L"\r\n"
                                   L"© 2026 Amu\r\n"
                                   L"\r\n"
                                   L"Official project: "
                                   L"<a href=\"" +
                                   ABOUTURL + L"\">GitHub repository</a>\r\n" +
                                   L"\r\n"
                                   L"Licensed under the MIT License";
            ShowUpdateDialog(hwnd, L"About", APPNAME, message);
            break;
        }

        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;

        case IDM_WEBSERVER_STOP:
        {
            g_webServer.Stop();
            g_serverStatusRc.SetValue(L"");
            g_serverStatusRc.ClearRC(g_backBuffer.memDC, BACKGROUNDCOLOR);
            InvalidateRect(hwnd, &g_serverStatusRc.valueRc, FALSE);
            UpdateWindow(hwnd);
        }

        default:
        {
            // WEB SERVER
            const auto &list = g_networkManager.GetAddresses();
            if (LOWORD(wParam) >= IDM_WEBSERVER_BASE && LOWORD(wParam) < IDM_WEBSERVER_BASE + list.size())
            {
                size_t index = LOWORD(wParam) - IDM_WEBSERVER_BASE;

                const auto &netIf = list[index];

                LOG_DEBUG("Selecting interface: %ls", netIf.display().c_str());

                g_webServer.LaunchServerOnInterface(netIf);
                auto addr = g_webServer.GetBoundInterface().value().address;
                std::wstring txt = L"http://" + addr + L":" + WEBSERVER_PORT;
                g_serverStatusRc.SetValue(txt.c_str());
                g_clickableUrlRect = GetCenteredTextRect(g_backBuffer.memDC, g_notificationFont, &g_serverStatusRc.valueRc, txt.c_str());
                g_serverStatusRc.DrawTextValue(g_backBuffer.memDC, SERVERSTATUSCOLOR, g_notificationFont);
                InvalidateRect(hwnd, &g_serverStatusRc.valueRc, FALSE);
                UpdateWindow(hwnd);
            }
        }
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        const bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool isRepeat = (lParam & (1u << 30)) != 0;

        if (ctrlDown && !isRepeat)
        {
            switch (wParam)
            {
            case VK_ADD:      // Ctrl + Numpad +
            case VK_OEM_PLUS: // Ctrl + main keyboard +
                OnResizeWindow(hwnd, true);
                return 0;

            case VK_SUBTRACT:  // Ctrl + Numpad -
            case VK_OEM_MINUS: // Ctrl + main keyboard - (QWERTY)
            case '6':          // Ctrl + main keyboard - (AZERTY)
                OnResizeWindow(hwnd, false);
                return 0;
            }
        }

        break;
    }

    case WM_LBUTTONDOWN:
    {

        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);

        if (PtInRect(&g_clickableUrlRect, pt) && g_webServer.IsRunning()) // web server url
        {
            LOG_DEBUG("Opening %ls", g_serverStatusRc.textValue);
            INT_PTR ret = OpenUrl(g_serverStatusRc.textValue);
            if (ret <= 32)
                LOG_ERROR("Failed to open URL (ShellExecuteW): %td", ret);
            return 0;
        }
        else if (PtInRect(&g_props[MetricsIndex::Display].labelRc, pt)) // display label
        {
            auto current = g_displayManager.Next();
            if (current.has_value())
            {
                SetDisplayLine(current.value(), hwnd);
                g_currentDisplayIndex = current.value().index;
            }
        }

        SetCapture(hwnd);
        g_dragging = true;
        GetCursorPos(&g_dragStart);
        GetWindowRect(hwnd, &g_wndStart);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (g_dragging)
        {
            POINT cur{};
            GetCursorPos(&cur);
            int dx = cur.x - g_dragStart.x;
            int dy = cur.y - g_dragStart.y;
            SetWindowPos(hwnd, nullptr, g_wndStart.left + dx, g_wndStart.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        else
        {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};

            if (PtInRect(&g_clickableUrlRect, pt) && g_webServer.IsRunning()) // web server url
            {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            }
            else if (PtInRect(&g_props[MetricsIndex::Display].labelRc, pt)) // display label
            {
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            }
            else
            {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
            }
        }
        return 0;
    }

    case WM_LBUTTONUP:
    {
        if (g_dragging)
        {
            g_dragging = false;
            ReleaseCapture();
        }
        return 0;
    }

    case WM_DISPLAYCHANGE:
    {
        LOG_DEBUG("WM_DISPLAYCHANGE");

        g_displayManager.Clear();
        g_displayManager.Discover();

        if (g_currentDisplayIndex < g_displayManager.Size())
            g_displayManager.SetCurrent(g_currentDisplayIndex); // resync for UI persistence
        else
            g_currentDisplayIndex = 0;

        auto &display = g_displayManager.Current();
        if (display.has_value())
            SetDisplayLine(display.value());

        return 0;
    }

    case WM_QUERYENDSESSION:
        // Windows is asking if your app can close.
        // Save critical state here.
        return TRUE;

    case WM_ENDSESSION:
    {
        if (wParam) // Session is actually ending.
            Cleanup(hwnd);

        return 0;
    }

    case WM_DESTROY:
    {
        Cleanup(hwnd);

        PostQuitMessage(0);
        return 0;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
#ifdef _DEBUG
    AllocConsole();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
    LOG_DEBUG("Starting App");
#endif
    g_isAdmin = IsRunningAsAdministrator();

    const wchar_t CLASS_NAME[] = L"radeonmon";

    WNDCLASS wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.style = CS_HREDRAW | CS_VREDRAW; // force repaint on resize

    if (!RegisterClass(&wc))
    {
        MessageBoxW(nullptr, L"RegisterClass failed", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    LoadPreferences();

    g_autostart = IsAutostartEnabled();

    POINT pt = {g_xPos, g_yPos};
    POINT pt2 = {g_xPos + g_width, g_yPos + g_height};
    if (isPointValid(pt) && isPointValid(pt2))
    {
        g_dpi = getDpiFromPoint(pt);
        g_width = DPIScale(g_width);
        g_height = DPIScale(g_height);
    }
    else
    {
        LOG_DEBUG("Invalid position from preferences, reseting the position");
        g_dpi = GetDpiForSystem();
        LOG_DEBUG("DPI: %u (%.0f%%)", g_dpi, (g_dpi / 96.0) * 100.0);
        g_width = MulDiv(g_width, g_dpi, 96);
        g_height = MulDiv(g_height, g_dpi, 96);

        // Primary screen size
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        // Center position on primary screen
        g_xPos = (screenWidth - g_width) / 2;
        g_yPos = (screenHeight - g_height) / 2;
    }

    RecreateFont();

    LOG_DEBUG("window created %dx%d at position {%d,%d}", g_width, g_height, g_xPos, g_yPos);
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, APPNAME, WS_POPUP, g_xPos, g_yPos, g_width, g_height, nullptr, nullptr, hInstance, nullptr);

    g_networkManager.Initialize(hwnd);
    g_networkManager.Log();

    SetAlwaysOnTop(hwnd, g_alwaysOnTop);

    SetTimer(hwnd, APP_POLLING_ID, APP_REFRESH_TIMER, nullptr);

    g_AdlxGPUTelemetry.Init();
    g_AdlxGPUTelemetry.Discover();

    g_displayManager.Discover();

    const RadeonMon::Hardware::DisplayManager &manager = g_displayManager;
    const auto &display = manager.Current();
    if (display.has_value())
        SetDisplayLine(display.value());

    if (g_AdlxGPUTelemetry.isInitialized)
        UpdateToolTipText(hwnd, TOOLID_GPUINFO, g_AdlxGPUTelemetry.GetGpuInfo().GetTooltip(), g_AdlxGPUTelemetry.GetGpuInfo().GetDriverPathTooltipWidth(g_hwndTooltip));

    LOG_DEBUG("Admin mode: %s", g_isAdmin ? "yes" : "no");

    if (g_isAdmin)
    {
        if (!LoadAMDDLLs())
        {
            SetPropertyValueAtIndex(MetricsIndex::Cpu, SdkRequired, L"SDK req", 19);
            g_notification.SetValue(L"Ryzen SDK required");
            g_notification.DrawTextValue(g_backBuffer.memDC, NOTIFICATIONCOLOR, g_notificationFont);
        }
        else
        {
            if (!g_cpu.Init())
            {
                SetPropertyValueAtIndex(MetricsIndex::Cpu, NotSupported, L"not supported", 14);
            }
            else
            {
                g_cpu.Start();
            }
        }
    }
    else
    {
        // SetPropertyValueAtIndex(MetricsIndex::Cpu, AdminRequired, L"admin required", 15);
        g_notification.SetValue(L"cpu requires admin rights");
    }

    ShowWindow(hwnd, nCmdShow);

    LOG_DEBUG("Entering Dispatcher loop");

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}