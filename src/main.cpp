#include "radeonmon/webserver.hpp"
#include "radeonmon/colors.hpp"
#include "radeonmon/resource_ids.h"

#include <windows.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <mmsystem.h>

#include <cstdio>
#include <cmath>
#include <cassert>
#include <algorithm>

#include "radeonmon/globals.hpp"
#include "radeonmon/constants.hpp"
#include "radeonmon/structures.hpp"
#include "radeonmon/helpers.hpp"
#include "radeonmon/adlx.hpp"
#include "radeonmon/ryzen.hpp"
#include "radeonmon/ryzen_sdk.hpp"
#include "radeonmon/preferences.hpp"
#include "radeonmon/autostart.hpp"
#include "radeonmon/logging.hpp"
#include "radeonmon/version_checker.hpp"

#include <version.hpp>

using namespace RadeonMon::Hardware;

#pragma comment(lib, "Shcore.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "winmm.lib")

RECT GetGamepadIconRect()
{
    const int dotDiameter = MulDiv(6, g_fontSize, FONTSIZE);
    const int dotGap = MulDiv(3, g_fontSize, FONTSIZE);

    RECT rect;
    UnionRect(&rect, &g_border.gamepadIcon, &g_border.gamepadStatus);

    // Include the transport dot.
    rect.left -= dotDiameter + dotGap + 1;

    return rect;
}

void PaintGamepadStatus(HDC hdc)
{
    static bool wasVisible = false;
    static int lastBatteryLevel = -2;
    static bool lastIsCharging = false;

    const bool isVisible =
        g_isDualSenseEnabled &&
        g_dualsense.GetTransport() == DualSense::Transport::Bluetooth;

    // Status is no longer visible (disabled, USB, etc.).
    if (!isVisible)
    {
        if (wasVisible || g_forceFrameRedraw)
        {
            HBRUSH brush = CreateSolidBrush(BORDERCOLOR);
            FillRect(hdc, &g_border.gamepadStatus, brush);
            DeleteObject(brush);
            wasVisible = false;
        }

        return;
    }

    const int batteryLevel = g_dualsense.m_batteryLevel;
    const bool isCharging = g_dualsense.m_isCharging;

    if (batteryLevel == -1)
    {
        LOG_WARN("[App] DualSense Battery Status: unavailable (short HID report)");
        return;
    }

    // Nothing changed and the entire UI isn't being forced to redraw.
    if (!g_forceFrameRedraw && wasVisible && batteryLevel == lastBatteryLevel && isCharging == lastIsCharging)
        return;

    LOG_DEBUG("[App] DualSense Battery Status: %d%%, charging: %s", batteryLevel * 10, isCharging ? "yes" : "no");

    HBRUSH brush = CreateSolidBrush(BORDERCOLOR);
    FillRect(hdc, &g_border.gamepadStatus, brush);
    DeleteObject(brush);

    HFONT oldFont = (HFONT)SelectObject(hdc, g_titleFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, GAMEPAD_NEUTRAL);

    wchar_t buffer[10];
    swprintf_s(buffer, L"%d%%%ls", batteryLevel * 10, isCharging ? L"⚡" : L" "); // TODO: custom formatter

    TextOutW(hdc, g_border.gamepadStatus.left, g_border.gamepadStatus.top, buffer, static_cast<int>(wcslen(buffer)));
    SelectObject(hdc, oldFont);

    lastBatteryLevel = batteryLevel;
    lastIsCharging = isCharging;
    wasVisible = true;
}

bool InitScreenshotSound()
{
    HMODULE hModule = GetModuleHandleW(nullptr);

    HRSRC hResource = FindResourceW(hModule, MAKEINTRESOURCEW(IDR_SCREENSHOT_WAV), RT_RCDATA);

    if (!hResource)
        return false;

    HGLOBAL hLoaded = LoadResource(hModule, hResource);
    if (!hLoaded)
        return false;

    g_screenshotSoundData = LockResource(hLoaded);

    return g_screenshotSoundData != nullptr;
}

void PlayScreenshotSound()
{
    if (!g_screenshotSoundData)
        return;

    PlaySoundW(static_cast<LPCWSTR>(g_screenshotSoundData), nullptr, SND_MEMORY | SND_ASYNC);
}

// void DrawGamepadIcon(HDC hdc)
// {
//     HFONT oldFont = (HFONT)SelectObject(hdc, g_titleFont);
//     SetBkMode(hdc, TRANSPARENT);

//     // switch (g_dualsense.GetTransport())
//     // {
//     // case DualSense::Transport::USB:
//     //     SetTextColor(hdc, GAMEPAD_USB);
//     //     break;

//     // case DualSense::Transport::Bluetooth:
//     //     SetTextColor(hdc, GAMEPAD_BT);
//     //     break;

//     // case DualSense::Transport::None:
//     //     SetTextColor(hdc, GAMEPAD_NEUTRAL);
//     //     break;
//     // }

//     SetTextColor(hdc, GAMEPAD_NEUTRAL);
//     TextOutW(hdc, g_border.gamepadIcon.left, g_border.gamepadIcon.top, GAMEPAD_ICON, 2);
//     SelectObject(hdc, oldFont);
// }

void DrawGamepadIcon(HDC hdc)
{
    static DualSense::Transport lastTransport =
        DualSense::Transport::None;

    const auto transport = g_dualsense.GetTransport();

    const int dotDiameter = MulDiv(6, g_fontSize, FONTSIZE);
    const int dotGap = MulDiv(3, g_fontSize, FONTSIZE);

    // If the previous state had a transport dot, clear its area.
    if (lastTransport != DualSense::Transport::None)
    {
        RECT dotRect = g_border.gamepadIcon;

        dotRect.left =
            g_border.gamepadIcon.left - dotGap - dotDiameter - 1;

        HBRUSH brush = CreateSolidBrush(BORDERCOLOR);
        FillRect(hdc, &dotRect, brush);
        DeleteObject(brush);
    }

    lastTransport = transport;

    HFONT oldFont = (HFONT)SelectObject(hdc, g_titleFont);
    SetBkMode(hdc, TRANSPARENT);

    COLORREF transportColor = GAMEPAD_NEUTRAL;
    bool showTransportDot = false;

    switch (transport)
    {
    case DualSense::Transport::USB:
        transportColor = GAMEPAD_USB;
        showTransportDot = true;
        break;

    case DualSense::Transport::Bluetooth:
        transportColor = GAMEPAD_BT;
        showTransportDot = true;
        break;

    case DualSense::Transport::None:
        break;
    }

    if (showTransportDot)
    {
        const int iconHeight =
            g_border.gamepadIcon.bottom -
            g_border.gamepadIcon.top;

        const int dotY =
            g_border.gamepadIcon.top +
            (iconHeight - dotDiameter) / 2;

        const int dotX =
            g_border.gamepadIcon.left -
            dotGap -
            dotDiameter;

        HBRUSH brush = CreateSolidBrush(transportColor);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
        HPEN oldPen = (HPEN)SelectObject(
            hdc,
            GetStockObject(NULL_PEN));

        Ellipse(
            hdc,
            dotX,
            dotY,
            dotX + dotDiameter,
            dotY + dotDiameter);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(brush);
    }

    SetTextColor(hdc, GAMEPAD_NEUTRAL);

    TextOutW(
        hdc,
        g_border.gamepadIcon.left,
        g_border.gamepadIcon.top,
        GAMEPAD_ICON,
        2);

    SelectObject(hdc, oldFont);
}

void ClearGamepadIcon(HDC hdc)
{
    const int dotDiameter = MulDiv(6, g_fontSize, FONTSIZE);
    const int dotGap = MulDiv(3, g_fontSize, FONTSIZE);

    RECT rect;

    // Start with the gamepad icon.
    UnionRect(&rect, &g_border.gamepadIcon, &g_border.gamepadStatus);

    // Include the transport dot to the left of the icon.
    rect.left -= dotDiameter + dotGap + 1;

    // Small safety margin for text/ellipse rendering.
    rect.left--;
    rect.right++;
    rect.top--;
    rect.bottom++;

    HBRUSH brush = CreateSolidBrush(BORDERCOLOR);
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
}

void PaintGampepadIcon(HDC hdc)
{
    static bool lastDualSenseEnabled = false;
    static DualSense::Transport lastTransport = DualSense::Transport::None;
    static bool initialized = false;
    const auto currentTransport = g_dualsense.GetTransport();

    if (!g_forceFrameRedraw)
        if (initialized && lastDualSenseEnabled == g_isDualSenseEnabled && lastTransport == currentTransport)
            return;

    initialized = true;
    lastDualSenseEnabled = g_isDualSenseEnabled;
    lastTransport = currentTransport;

    if (g_isDualSenseEnabled)
        DrawGamepadIcon(hdc);
    else
        ClearGamepadIcon(hdc);
}

void DrawScreenshotIcon(HWND hwnd, HDC hdc)
{
    HBRUSH brush = CreateSolidBrush(BORDERCOLOR);
    FillRect(hdc, &g_border.screeshotIcon, brush);
    DeleteObject(brush);

    HFONT oldFont = (HFONT)SelectObject(hdc, g_titleFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    TextOutW(hdc, g_border.screeshotIcon.left, g_border.screeshotIcon.top, SCREENSHOT_ICON, 2);

    SelectObject(hdc, oldFont);

    InvalidateRect(hwnd, &g_border.screeshotIcon, FALSE);
}

void ClearScreenshotIcon(HDC hdc)
{
    HBRUSH brush = CreateSolidBrush(BORDERCOLOR);
    FillRect(hdc, &g_border.screeshotIcon, brush);
    DeleteObject(brush);
}

void OnScreenshotAction(HWND hwnd)
{
    if (g_screenshot.GetScreenshot())
    {
        PlayScreenshotSound();
        DrawScreenshotIcon(hwnd, g_backBuffer.memDC);
        SetTimer(hwnd, SCREENSHOT_ICON_ID, 1000, nullptr);
    }
}

void SetDisplayLine(const DisplayInfo &display, HWND hwnd = nullptr)
{
    PropertyItem &prop = g_props[MetricsIndex::Display];
    const std::wstring label = L"Display " + std::to_wstring(display.index + 1);
    int widthToDisplay = display.isPortrait ? display.width : display.height;
    std::wstring resolution = widthToDisplay == 4320 ? L"8K @" : widthToDisplay == 2160 ? L"4K @"
                                                                                        : std::to_wstring(widthToDisplay) + L"p @";
    std::wstring value = resolution + std::to_wstring(display.frequency) + L"Hz";

    if (value.length() < MAXTXTVALUE_LENGTH)
        value.append(MAXTXTVALUE_LENGTH - value.length(), L' ');

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
    LogRect("[App] Tooltip RECT", rect);
    TOOLINFO ti = {};
    ti.cbSize = sizeof(ti);
    ti.hwnd = hwnd;
    ti.uId = toolId;
    ti.rect = rect;

    SendMessage(g_hwndTooltip, TTM_NEWTOOLRECT, 0, reinterpret_cast<LPARAM>(&ti));
}

void InitTooltips()
{
    INITCOMMONCONTROLSEX icc = {sizeof(INITCOMMONCONTROLSEX), ICC_WIN95_CLASSES};
    InitCommonControlsEx(&icc);
}

LayoutMetrics CalculateLayoutMetrics(HDC hdc)
{
    LayoutMetrics m{};

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

    if (!GetTextExtentPoint32W(hdc, MAXTXTLABEL, MAXTXTLABEL_LENGTH, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived: no scaling
    m.labelWidth = sz.cx + 2;

    if (!GetTextExtentPoint32W(hdc, MAXTXTVALUE, MAXTXTVALUE_LENGTH, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived: no scaling
    m.valueWidth = sz.cx + 2;

    // ------------------------------------------------------------
    // Shared
    // ------------------------------------------------------------
    m.paddingTop = ScaleFontMetric(PADDING_TOP);
    m.paddingBottom = ScaleFontMetric(PADDING_BOTTOM);
    m.cardPaddingTopBottom = ScaleFontMetric(CARD_PADDING);

    // ------------------------------------------------------------
    // Title font
    // ------------------------------------------------------------
    if (!SelectObject(hdc, g_titleFont))
        LOG_ERROR("SelectObject failed on g_titleFont");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    const int titleFontHeight = sz.cy;
#ifdef _DEBUG
    const int titleFontWidth = sz.cx;
    LOG_DEBUG("titleFontWidth=%d", titleFontWidth);
#endif

    // Font-independent spacing: scale
    m.titlePadding = ScaleFontMetric(TITLE_PADDING);

    // Font-derived: no scaling
    m.titleHeight = titleFontHeight;

    if (!GetTextExtentPoint32W(hdc, g_appTitle.name, g_appTitle.textLength, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived: no scaling
    m.titleWidth = sz.cx;

    // ------------------------------------------------------------
    // Body font
    // ------------------------------------------------------------
    if (!SelectObject(hdc, g_font))
        LOG_ERROR("SelectObject failed on g_font");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    // Font-derived
    m.lineHeight = sz.cy;
    m.charWidth = sz.cx;

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
    // m.cardHeight = sz.cy;
    m.cardHeight = sz.cy + (m.cardPaddingTopBottom * 2);

    SelectObject(hdc, originalFont);

    // Tags (for FPS)
    m.tagGap = ScaleFontMetric(TAG_GAP);
    m.tagTopPadding = ScaleFontMetric(TAG_PADDING - 2);
    m.tagSidePadding = ScaleFontMetric(TAG_PADDING);

    if (!SelectObject(hdc, g_tagFont))
        LOG_ERROR("SelectObject failed on g_tagFont");

    if (!GetTextExtentPoint32W(hdc, L"X", 1, &sz))
        LOG_ERROR("GetTextExtentPoint32W failed");

    m.tagCharWidth = sz.cx;
    m.tagCharHeight = sz.cy;

    m.tagTextWidth = m.tagCharWidth * 2;
    m.tagWidth = m.tagTextWidth + m.tagSidePadding * 2;
    m.tagOffsetX = static_cast<int>(m.charWidth * 1.4f);

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

bool SetPropertyValueAtLine(int line, LPCWSTR textValue, int valueSize, TextLevel level = TextLevel::Neutral)
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
    p.textLevel = level;

    return true;
}

bool SetPropertyValueAtIndex(int index, int value, LPCWSTR textValue, int valueSize, TextLevel level = TextLevel::Neutral)
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
    p.textLevel = level;

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

    //// extra icons in title bar
    HDC hdc = GetDC(nullptr); // Back buffer may not be ready during startup, using a screen DC for text measurement.
    HFONT oldFont = (HFONT)SelectObject(hdc, g_titleFont);

    const int height = g_border.top.bottom - g_border.top.top;

    // Gamepad icon
    SIZE gamepadTextSize{};
    GetTextExtentPoint32W(hdc, GAMEPAD_ICON, 2, &gamepadTextSize);

    g_border.gamepadIcon.left = g_border.top.left + g_layoutMetrics.paddingSide;
    g_border.gamepadIcon.right = g_border.gamepadIcon.left + gamepadTextSize.cx;
    g_border.gamepadIcon.top = g_border.top.top + (height - gamepadTextSize.cy) / 2;
    g_border.gamepadIcon.bottom = g_border.gamepadIcon.top + gamepadTextSize.cy;

    // Gamepad status: e.g. "⚡85%"
    static constexpr wchar_t gamepadStatus[] = L"⚡85%";

    SIZE gamepadStatusTextSize{};
    GetTextExtentPoint32W(hdc, gamepadStatus, static_cast<int>(wcslen(gamepadStatus)), &gamepadStatusTextSize);

    constexpr int gamepadStatusSpacing = 2;

    g_border.gamepadStatus.left = g_border.gamepadIcon.right + gamepadStatusSpacing;
    g_border.gamepadStatus.right = g_border.gamepadStatus.left + gamepadStatusTextSize.cx;
    g_border.gamepadStatus.top = g_border.top.top + (height - gamepadStatusTextSize.cy) / 2;
    g_border.gamepadStatus.bottom = g_border.gamepadStatus.top + gamepadStatusTextSize.cy;

    // Screenshot icon
    SIZE textSize{};
    GetTextExtentPoint32W(hdc, SCREENSHOT_ICON, 2, &textSize);

    g_border.screeshotIcon.right = g_border.top.right - g_layoutMetrics.paddingSide;
    g_border.screeshotIcon.left = g_border.screeshotIcon.right - textSize.cx;
    g_border.screeshotIcon.top = g_border.top.top + (height - textSize.cy) / 2;
    g_border.screeshotIcon.bottom = g_border.screeshotIcon.top + textSize.cy;

    SelectObject(hdc, oldFont);
    ReleaseDC(nullptr, hdc);
    ////

    g_appTitle.UpdateRC(g_border.top, m);
}

void LayoutProperties2(const LayoutMetrics &m)
{
    LOG_TRACE("[App] LayoutProperties2");

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

    // Borders + Title
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

        p.textLabelRc = {leftEdge, y, leftEdge + static_cast<int>(m.charWidth * p.label.size()), y + m.lineHeight};

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

    SIZE sz{};
    HFONT oldFont = (HFONT)SelectObject(g_backBuffer.memDC, g_cardFont);

    if (!GetTextExtentPoint32W(g_backBuffer.memDC, g_cardName.textValue, g_cardName.textLength, &sz))
        LOG_ERROR("[LayoutProperties2] GetTextExtentPoint32W failed");

    g_cardName.textX = g_cardName.valueRc.left + ((g_cardName.valueRc.right - g_cardName.valueRc.left) - sz.cx) / 2;

    const int contentTop = g_cardName.valueRc.top + m.cardPaddingTopBottom;
    const int contentBottom = g_cardName.valueRc.bottom - m.cardPaddingTopBottom;

    g_cardName.textY = contentTop + ((contentBottom - contentTop) - sz.cy) / 2;
    g_cardName.textRc = {g_cardName.valueRc.left, g_cardName.valueRc.top + m.cardPaddingTopBottom, g_cardName.valueRc.right, g_cardName.valueRc.bottom - m.cardPaddingTopBottom};

    y += m.cardHeight;

    SelectObject(g_backBuffer.memDC, oldFont);

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

    UINT index = 0;

    // Paint regular properties
    for (auto &p : g_props)
    {
        if (!g_isFpsEnabled) // skip if FPS is off
            if (index == MetricsIndex::Fps || index == MetricsIndex::Fps + 1)
                continue;

        index++;

        if (p.type == PropertyType::Separator && p.dirty)
        {
            MoveToEx(hdc, p.labelRc.left, p.labelRc.top, nullptr);
            LineTo(hdc, p.labelRc.right, p.labelRc.top);

#ifdef GDIDRAW
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
            // LOG_DEBUG("[App] drawing label %d: %ls", index, p.label.c_str());
            SetTextColor(hdc, LABELCOLOR);

            SIZE sz{};
            int len = static_cast<int>(p.label.length());
            GetTextExtentPoint32W(hdc, p.label.c_str(), len, &sz);

            int x = p.labelRc.left;
            int y = p.labelRc.top + ((p.labelRc.bottom - p.labelRc.top) - sz.cy) / 2;

            ExtTextOutW(hdc, x, y, ETO_CLIPPED, &p.labelRc, p.label.c_str(), len, nullptr);
#ifdef GDIDRAW
            g_gdiDrawCallCount++;
#endif
            p.repaintLabel = false;
        }

        SetTextColor(hdc, colorMapping[p.textLevel]);

        SIZE sz{};
        int len = (int)wcslen(p.textValue);
        GetTextExtentPoint32W(hdc, p.label.c_str(), len, &sz);

        int x = p.valueRc.left;
        int y = p.valueRc.top + ((p.valueRc.bottom - p.valueRc.top) - sz.cy) / 2;

        ExtTextOutW(hdc, x, y, ETO_CLIPPED, &p.valueRc, p.textValue, len, nullptr);
#ifdef GDIDRAW
        g_gdiDrawCallCount++;
#endif

        p.dirty = false;
    }

    if (g_notification.dirty)
    {
        g_notification.DrawTextValue(g_backBuffer.memDC, NOTIFICATIONCOLOR, g_notificationFont);
#ifdef GDIDRAW
        g_gdiDrawCallCount++;
#endif
        g_notification.dirty = false;
    }

    // Paint server separator
    if (g_serverSeparatorRc.dirty)
    {
        MoveToEx(hdc, g_serverSeparatorRc.valueRc.left, g_serverSeparatorRc.valueRc.top, nullptr);
        LineTo(hdc, g_serverSeparatorRc.valueRc.right, g_serverSeparatorRc.valueRc.top);
#ifdef GDIDRAW
        g_gdiDrawCallCount++;
#endif

        g_serverSeparatorRc.dirty = false;
    }

    // Paint server status
    if (g_serverStatusRc.dirty)
    {
        g_serverStatusRc.DrawTextValue(g_backBuffer.memDC, SERVERSTATUSCOLOR, g_notificationFont);
#ifdef GDIDRAW
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
        // SetTextColor(hdc, LABELCOLOR);

        SetTextColor(hdc, VALUECOLOR);

        HFONT oldFont = (HFONT)SelectObject(hdc, g_cardFont);
        // ExtTextOutW(hdc, g_cardName.textX, g_cardName.textY, ETO_CLIPPED, &rc, g_cardName.textValue, g_cardName.textLength, nullptr);
        LOG_DEBUG("[Main] g_cardName.textLength=%u", g_cardName.textLength);
        ExtTextOutW(hdc, g_cardName.textX, g_cardName.textY, ETO_CLIPPED, &g_cardName.textRc, g_cardName.textValue, g_cardName.textLength, nullptr);

        SelectObject(hdc, oldFont);

#ifdef GDIDRAW
        g_gdiDrawCallCount += 3;
#endif

        g_cardName.dirty = false;
    }

    SelectObject(hdc, oldPen);
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

    PostMessage(hwnd, WM_APP_LAYOUT, 0, 0);

    InvalidateRect(hwnd, nullptr, TRUE);

    // LOG_DEBUG("new window: %dx%d, font@%dpx", rc.right - rc.left, rc.bottom - rc.top, g_fontSize);
}

void PaintFrame(HDC hdc)
{
    static bool drawn = false;
    static HBRUSH frameBrush = CreateSolidBrush(BORDERCOLOR);

    if (!g_forceFrameRedraw && drawn)
        return;

    LOG_DEBUG("[App] Painting FRAME (border+title)");

    drawn = true;

    // Border sides
    FillRect(hdc, &g_border.top, frameBrush);
    FillRect(hdc, &g_border.bottom, frameBrush);
    FillRect(hdc, &g_border.left, frameBrush);
    FillRect(hdc, &g_border.right, frameBrush);

    // Title text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    HFONT oldFont = (HFONT)SelectObject(hdc, g_titleFont);

    // START_CHRONO(title);
    // RECT rc = g_titleRc;
    // DrawTextW(hdc, APPNAME, APPNAME_LENGTH, &rc, DT_LEFT | DT_TOP | DT_SINGLELINE); // 1.5ms
    // TextOutW(hdc, g_titleRc.left, g_titleRc.top, APPNAME, APPNAME_LENGTH); // 57µs
    ExtTextOutW(hdc, g_appTitle.rc.left, g_appTitle.rc.top, 0, nullptr, g_appTitle.name, g_appTitle.textLength, nullptr);

    // END_CHRONO_MICRO(title, "title draw");

    SelectObject(hdc, oldFont);
#ifdef GDIDRAW
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

void CheckVersionAsync(HWND hwnd, bool showDialogs)
{
    std::thread([hwnd, showDialogs]
                {
                    std::wstring latestVersion;

                    if (!VersionChecker::GetLatestVersion(latestVersion))
                    {
                        LOG_ERROR("[Version] Failed to check update");

                        if (showDialogs)
                        {
                            PostMessage(hwnd, WM_APP_VERSION_ERROR, 0, 0);
                        }
                        return;
                    }

                    LOG_DEBUG("[Version] Latest version: %ls", latestVersion.c_str());

                    const bool updateAvailable = (latestVersion != Version::String);

                    if (updateAvailable)
                        LOG_WARN("[Version] New update available: %ls", latestVersion.c_str());

                    auto *result = new VersionCheckResult{std::move(latestVersion), updateAvailable, showDialogs};

                    PostMessage(hwnd, WM_APP_VERSION_RESULT, 0, reinterpret_cast<LPARAM>(result)); })
        .detach();
}

void DrawTag(HDC hdc, int x, int y, LPCWSTR text, int topPadding, int sidePadding, COLORREF color)
{
    const int textLen = lstrlenW(text);

    RECT tagRc = {
        x - sidePadding,
        y - topPadding,
        x + g_layoutMetrics.tagCharWidth * textLen + sidePadding,
        y + g_layoutMetrics.tagCharHeight + topPadding};

    HPEN pen = CreatePen(PS_SOLID, 1, color);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH)); // clear brush for background

    SetTextColor(hdc, color);

    Rectangle(hdc, tagRc.left, tagRc.top, tagRc.right, tagRc.bottom);
    TextOutW(hdc, x, y, text, textLen);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void PaintFpsTags(HDC hdc)
{

    if (!g_AdlxGPUTelemetry.isInitialized || !g_isFpsEnabled || !g_presentMonManager.IsInitialized())
        return;

    const int adlxCurrentFPS = g_AdlxGPUTelemetry.GetSnapshotFPS();
    int pmFps = -1;

    // Clear previous tags
    const RECT &rTags = g_props[MetricsIndex::Fps].textLabelRc;
    RECT clearRc = {rTags.right, rTags.top, g_props[MetricsIndex::Fps].valueRc.left, rTags.bottom};
    HBRUSH brush = CreateSolidBrush(BACKGROUNDCOLOR);
    FillRect(hdc, &clearRc, brush);
    DeleteObject(brush);

    if (adlxCurrentFPS == -1)
    {
        if (g_presentMonManager.IsQueryOpened())
            g_presentMonManager.ClosePMMetric();

        if (g_presentMonManager.IsTracking())
            g_presentMonManager.StopPMTracking();

        // hack: extra start/stop tracking to clear etw state of presentmon to prevent cpu leak
        if (g_presentMonManager.IsSessionOpened())
        {
            g_presentMonManager.StartPMTracking(g_appPID);
            g_presentMonManager.StopPMTracking();
        }

        if (g_presentMonManager.IsSessionOpened())
            g_presentMonManager.ClosePMSession();

        if (g_presentMonManager.IsHooking())
            g_presentMonManager.StopEventHook();

        return;
    }
    else
    {
        if (!g_presentMonManager.IsHooking())
        {

            if (!g_presentMonManager.StartEventHook())
            {
                LOG_ERROR("[App] PresentMon: StartEventHook failed");
                return;
            }

            // Open PM session immediately after hooking
            if (!g_presentMonManager.IsSessionOpened())
            {
                if (g_presentMonManager.OpenPMSession() != 0)
                {
                    LOG_ERROR("[App] PresentMon: OpenPMSession failed");
                    g_presentMonManager.StopEventHook();
                    return;
                }
            }
            else
                LOG_WARN("[App] PresentMon: session already opened");

            if (g_presentMonManager.IsTracking())
            {
                LOG_WARN("[App] PresentMon: tracking is active, stopping previous tracking");
                g_presentMonManager.StopPMTracking();
                LOG_DEBUG("[App] PresentMon: previous tracking stopped");
            }

            DWORD currentPID = GetForegroundPID();
            LOG_DEBUG("[App] PresentMon: foreground PID=%lu", currentPID);

            g_presentMonManager.StartPMTracking(currentPID);

            if (!g_presentMonManager.IsQueryOpened())
            {
                if (!g_presentMonManager.OpenFPSMetric())
                {
                    LOG_ERROR("[App] PresentMon: OpenFPSMetric failed");
                    return;
                }
            }
            else
                LOG_WARN("[App] PresentMon: FPS metric query already opened");
        }

        // Always poll FPS, whether hooking was just started or already active.
        pmFps = g_presentMonManager.PollFPSMetric();
    }

    if (pmFps == -1)
    {
        LOG_ERROR("[App] PresentMon: PollFPSMetric failed");
        return;
    }

    double ratio = static_cast<double>(adlxCurrentFPS) / static_cast<double>(pmFps);

    // Tolerance
    if (ratio <= 1.90 || ratio >= 2.05)
    {
#ifdef LOGFPS
        LOG_WARN("[PMON] PM FPS=%d, ADLX FPS=%d, ratio=%.2fx, Skipping", pmFps, adlxCurrentFPS, ratio);
#endif
        return;
    }
#ifdef LOGFPS
    else
        LOG_DEBUG("[PMON] PM FPS=%d, ADLX FPS=%d, ratio=%.2fx", pmFps, adlxCurrentFPS, ratio);
#endif

    const RECT &r = g_props[MetricsIndex::Fps].textLabelRc;
    [[maybe_unused]] const int tagPadding = g_layoutMetrics.tagSidePadding;
    const int cy = (r.top + r.bottom) / 2;

    //// FG Tag

    HFONT oldFont = (HFONT)SelectObject(hdc, g_tagFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(0, 0, 0));

    constexpr wchar_t fgTag[] = L"FG";
    const int tagLeft = r.right + g_layoutMetrics.tagOffsetX;
    const int tagHeight = g_layoutMetrics.tagCharHeight;
    const int tagTop = cy - tagHeight / 2;
    const int textX = tagLeft + (g_layoutMetrics.tagWidth - g_layoutMetrics.tagTextWidth) / 2;
    const int textY = tagTop;

    // Main text.
    SetTextColor(hdc, BRIGHT_GREEN);
    TextOutW(hdc, textX, textY, fgTag, 2);

    // Border.
    HPEN hPen = CreatePen(PS_SOLID, 1, BRIGHT_GREEN);
    HGDIOBJ oldPen = SelectObject(hdc, hPen);
    HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Rectangle(hdc, tagLeft, tagTop, tagLeft + g_layoutMetrics.tagWidth, tagTop + g_layoutMetrics.tagCharHeight);

    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(hPen);

    SelectObject(hdc, oldFont);
}

void PaintDisplayTags(HDC hdc)
{
    if (!g_isVRREnabled || !g_vrrDetector.IsRunning())
        return;

    const RECT &labelRc = g_props[MetricsIndex::Display].textLabelRc;

    int x = labelRc.right + g_layoutMetrics.charWidth + g_layoutMetrics.tagGap;
    int y = labelRc.top + (labelRc.bottom - labelRc.top - g_layoutMetrics.tagCharHeight) / 2;

    HFONT oldFont = (HFONT)SelectObject(hdc, g_tagFont);

    const int topPadding = g_layoutMetrics.tagTopPadding;
    const int sidePadding = g_layoutMetrics.tagSidePadding;

    static HBRUSH bgBrush = CreateSolidBrush(BACKGROUNDCOLOR);

    static bool vrrTagCleared = false;

    if (g_vrrDetector.IsVRROn())
    {
        DrawTag(hdc, x, y, L"VRR", topPadding, sidePadding, BRIGHT_BLUE);
        vrrTagCleared = false;
    }
    else if (!vrrTagCleared)
    {
        RECT vrrTagRc = {x - sidePadding, y - topPadding, x + g_layoutMetrics.tagCharWidth * 3 + 2 * sidePadding, y + g_layoutMetrics.tagCharHeight + topPadding};
        FillRect(hdc, &vrrTagRc, bgBrush);
        vrrTagCleared = true;
    }

    x += g_layoutMetrics.tagCharWidth * 3 + g_layoutMetrics.tagGap + 2 + 2 * sidePadding;

    static bool lfcTagCleared = false;
    bool lfcActive = false;

    const int fps = g_AdlxGPUTelemetry.GetSnapshotFPS();
    if (fps > 0)
    {
        const double ratio = static_cast<double>(g_vrrDetector.CurrentHz()) / static_cast<double>(fps);
        lfcActive = ratio >= 1.80; // large jitter tolerance without average smoothing
    }

    if (lfcActive && g_vrrDetector.IsVRROn())
    {
        DrawTag(hdc, x, y, L"LFC", topPadding, sidePadding, BRIGHT_ORANGE);
        lfcTagCleared = false;
    }
    else if (!lfcTagCleared)
    {
        RECT lfcTagRc = {x - sidePadding, y - topPadding, x + g_layoutMetrics.tagCharWidth * 3 + 2 * sidePadding, y + g_layoutMetrics.tagCharHeight + topPadding};
        FillRect(hdc, &lfcTagRc, bgBrush);
        lfcTagCleared = true;
    }

    SelectObject(hdc, oldFont);
}

std::wstring SelectFolder(HWND hOwner)
{
    IFileDialog *pFileDialog = nullptr;

    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFileDialog));

    if (FAILED(hr))
        return {};

    DWORD options = 0;
    pFileDialog->GetOptions(&options);

    pFileDialog->SetOptions(options | FOS_PICKFOLDERS);

    hr = pFileDialog->Show(hOwner);

    if (FAILED(hr))
    {
        pFileDialog->Release();
        return {};
    }

    IShellItem *pItem = nullptr;
    hr = pFileDialog->GetResult(&pItem);

    std::wstring folder;

    if (SUCCEEDED(hr))
    {
        PWSTR path = nullptr;

        hr = pItem->GetDisplayName(SIGDN_FILESYSPATH, &path);

        if (SUCCEEDED(hr))
        {
            folder = path;
            CoTaskMemFree(path);
        }

        pItem->Release();
    }

    pFileDialog->Release();

    return folder;
}

// ── window procedure ─────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        LOG_WM("[App] WM_CREATE");

        RecreateFont();

        HDC measureDC = CreateCompatibleDC(nullptr);
        if (!measureDC)
        {
            LOG_ERROR("CreateCompatibleDC failed");
            return -1;
        }
        g_layoutMetrics = CalculateLayoutMetrics(measureDC);
        DeleteDC(measureDC);

        LayoutProperties2(g_layoutMetrics);

        g_layoutMetrics.Log();

        RECT rc{};
        GetClientRect(hwnd, &rc);

        HDC hdc = GetDC(hwnd);
        g_backBuffer.Create(hdc, g_layoutMetrics.windowWidth, g_layoutMetrics.windowHeight, BACKGROUNDCOLOR);
        ReleaseDC(hwnd, hdc);

        /////////////////////
        // Gpu Info Tooltip
        g_hwndTooltip = CreateWindowEx(0, TOOLTIPS_CLASS, NULL, WS_POPUP | TTS_ALWAYSTIP, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hwnd, NULL, GetModuleHandle(NULL), NULL);
        if (!g_hwndTooltip)
            LOG_ERROR("Failed to create tooltip window");
        else
        {
            SetWindowPos(g_hwndTooltip, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            RECT textRect = {0, 0, 200, 200};

            TOOLINFO ti = {};
            ti.cbSize = sizeof(ti);
            ti.uFlags = TTF_SUBCLASS;
            ti.hwnd = hwnd;
            ti.uId = TOOLID_GPUINFO;
            ti.rect = textRect;

            SendMessage(g_hwndTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);
            SendMessage(g_hwndTooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 0); // starts immediatly
            UpdateToolTipRect(hwnd, TOOLID_GPUINFO, g_cardName.valueRc);
        }
        /////////////////////

        // Send new updated size to dispatcher to trigger a WM_SIZE
        SetWindowPos(hwnd, HWND_TOPMOST, g_xPos, g_yPos, g_layoutMetrics.windowWidth, g_layoutMetrics.windowHeight, SWP_NOACTIVATE);

        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);

        if (!g_backBuffer.memDC)
        {
            LOG_ERROR("No back buffer");
            EndPaint(hwnd, &ps);
            return 0;
        }

        HDC memDC = g_backBuffer.memDC;

        SetBkMode(memDC, TRANSPARENT);

        if (g_font)
            SelectObject(memDC, g_font);

        // Window chrome
        PaintFrame(memDC);

        // Content
        PaintProperties(memDC);

        PaintFpsTags(memDC);

        PaintDisplayTags(memDC);

        PaintGampepadIcon(memDC);
        PaintGamepadStatus(memDC);

        g_forceFrameRedraw = false;

        // Present
        BitBlt(hdc, 0, 0, g_backBuffer.width, g_backBuffer.height, memDC, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);

#ifdef GDIDRAW
        LOG_TRACE("[App] GDI draw count=%u", g_gdiDrawCallCount); // very verbose
        g_gdiDrawCallCount = 0;
#endif

        return 0;
    }

    case WM_SIZE:
    {
        LOG_WM("[App] WM_SIZE");

        int w = LOWORD(lParam);
        int h = HIWORD(lParam);

        LOG_DEBUG("WM_SIZE: type=%d, w=%d, h=%d", (int)wParam, w, h);

        if (w > 0 && h > 0)
        {
            if (w != g_backBuffer.width || h != g_backBuffer.height)
            {
                HDC hdc = GetDC(hwnd);
                g_backBuffer.Create(hdc, w, h, BACKGROUNDCOLOR);
                g_width = w;
                g_height = h;
                ReleaseDC(hwnd, hdc);
            }

            ResetDirty();
        }

        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_ERASEBKGND:
        return 1; // Return non-zero to tell Windows we handled erasing

    case WM_DPICHANGED:
    {
        LOG_WM("[App] WM_DPICHANGED");

        g_dpi = HIWORD(wParam);

        const RECT *r = reinterpret_cast<const RECT *>(lParam);

        g_width = r->right - r->left;
        g_height = r->bottom - r->top;

        LOG_DEBUG("New DPI: %u (%.0f%%), %dxx%d", g_dpi, (g_dpi / 96.0) * 100.0, g_width, g_height);

        RecreateFont();
        PostMessage(hwnd, WM_APP_LAYOUT, 0, 0);

        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == APP_POLLING_ID)
        {
            // START_CHRONO(process);
            // END_CHRONO(process, "process Poll()");
            // g_processWatcher.Log();

            // if (!g_AdlxGPUTelemetry.isInitialized)
            // {
            //     LOG_WARN("adlx not init");
            //     g_AdlxGPUTelemetry.Init(hwnd); // retry
            //     return 0;
            // }

            bool dirty = false;

            // START_CHRONO(adlx);
            g_AdlxGPUTelemetry.Tick();
            GpuMetricsSnapshot snapshot = g_AdlxGPUTelemetry.Get();
            if (g_webServer.IsRunning() || (g_cpu.IsInitialized() && g_cpuGraph.isActive() && g_cpuGraph.isViewProcessesEnabled()))
            {
                g_processWatcher.Poll();
                // g_processWatcher.Log();
            }
            // END_CHRONO(adlx, "ADLX");

            if (!snapshot.valid)
                return 0;

            // GPU Temp
            if (snapshot.temperature.isSupported && GetPropertyValueAtIndex(MetricsIndex::Temp) != snapshot.temperature.value)
            {
                dirty = true;
                wchar_t tempBuffer[16];
                FormatTemperature(tempBuffer, static_cast<int>(snapshot.temperature.value));
                TextLevel level = snapshot.temperature.value >= TEMPERATURE_ALERT_THRESHOLD ? TextLevel::Alert : snapshot.temperature.value >= TEMPERATURE_WARNING_THRESHOLD ? TextLevel::Warning
                                                                                                                                                                             : TextLevel::Neutral;
                SetPropertyValueAtIndex(MetricsIndex::Temp, static_cast<int>(snapshot.temperature.value), tempBuffer, 16, level);
            }

            // Hotspot
            if (snapshot.hotspot.isSupported && GetPropertyValueAtIndex(MetricsIndex::Hotspot) != snapshot.hotspot.value)
            {
                dirty = true;
                wchar_t hotspotBuffer[16];
                FormatHotspot(hotspotBuffer, static_cast<int>(snapshot.temperature.value), static_cast<int>(snapshot.hotspot.value));
                TextLevel level = snapshot.hotspot.value >= TEMPERATURE_ALERT_THRESHOLD ? TextLevel::Alert : snapshot.hotspot.value >= TEMPERATURE_WARNING_THRESHOLD ? TextLevel::Warning
                                                                                                                                                                     : TextLevel::Neutral;
                SetPropertyValueAtIndex(MetricsIndex::Hotspot, static_cast<int>(snapshot.hotspot.value), hotspotBuffer, 16, level);
            }

            // VRAM Temperature
            if (snapshot.memoryTemperature.isSupported && GetPropertyValueAtIndex(MetricsIndex::Vram) != snapshot.memoryTemperature.value)
            {
                dirty = true;
                wchar_t vramBuffer[16];
                FormatTemperature(vramBuffer, static_cast<int>(snapshot.memoryTemperature.value));
                TextLevel level = snapshot.memoryTemperature.value >= TEMPERATURE_ALERT_THRESHOLD ? TextLevel::Alert : snapshot.memoryTemperature.value >= TEMPERATURE_WARNING_THRESHOLD ? TextLevel::Warning
                                                                                                                                                                                         : TextLevel::Neutral;
                SetPropertyValueAtIndex(MetricsIndex::Vram, static_cast<int>(snapshot.memoryTemperature.value), vramBuffer, 16, level);
            }

            // Fan Speed
            if (snapshot.fanSpeed.isSupported && GetPropertyValueAtIndex(MetricsIndex::FanSpeed) != snapshot.fanSpeed.value)
            {
                dirty = true;
                wchar_t fanBuffer[16];
                FormatFanSpeed(fanBuffer, snapshot.fanSpeed.value);

                // Level detection based on hotspot and fanspeed
                TextLevel level = TextLevel::Neutral;
                if (snapshot.hotspot.value >= 80 && snapshot.fanSpeed.value == 0)
                    level = TextLevel::Alert;
                else if (snapshot.hotspot.value >= TEMPERATURE_ALERT_THRESHOLD && snapshot.fanSpeed.value <= 500)
                    level = TextLevel::Alert;
                else if (snapshot.hotspot.value >= 90 && snapshot.fanSpeed.value <= 500)
                    level = TextLevel::Warning;

                SetPropertyValueAtIndex(MetricsIndex::FanSpeed, snapshot.fanSpeed.value, fanBuffer, 16, level);
            }

            // Power Consumption
            if (snapshot.totalBoardPower.isSupported && GetPropertyValueAtIndex(MetricsIndex::Power) != snapshot.totalBoardPower.value)
            {
                dirty = true;
                wchar_t powerBuffer[16];
                if (!snapshot.powerLimit.isSupported)
                    FormatPowerConsumption(powerBuffer, static_cast<int>(snapshot.totalBoardPower.value));
                else
                {
                    int percent = static_cast<int>(static_cast<int>(snapshot.totalBoardPower.value) * 100.0 / snapshot.powerLimitWatts.value);
                    FormatPowerConsumption(powerBuffer, static_cast<int>(snapshot.totalBoardPower.value), percent);
                }
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
                    TextLevel level = cpuIntegerTemp >= TEMPERATURE_ALERT_THRESHOLD ? TextLevel::Alert : cpuIntegerTemp >= TEMPERATURE_WARNING_THRESHOLD ? TextLevel::Warning
                                                                                                                                                         : TextLevel::Neutral;
                    SetPropertyValueAtIndex(MetricsIndex::Cpu, cpuIntegerTemp, cpuBuffer, 16, level);
                    SetPropertyValue2OnlyAtIndex(MetricsIndex::Cpu, cpuIntegerPower);
                }
            }

            // FPS
            int old = GetPropertyValueAtIndex(MetricsIndex::Fps);
            int current = snapshot.fps;
            if (current == -1 && old != -2)
            {
                g_props[MetricsIndex::Fps].ClearValueRC(g_backBuffer.memDC, BACKGROUNDCOLOR);
                SetPropertyValueAtIndex(MetricsIndex::Fps, -2, L"-", 2);
                // LOG_DEBUG("clear");
            }

            if (current != -1)
            {
                dirty = true;
                wchar_t fpsBuffer[16];
                int delta = current - old;
                FormatFPS(fpsBuffer, current, old);
                TextLevel level = delta <= -20 ? TextLevel::Alert : delta <= -10 ? TextLevel::Warning
                                                                                 : TextLevel::Neutral;
                SetPropertyValueAtIndex(MetricsIndex::Fps, snapshot.fps, fpsBuffer, 16, level);
            }

            if (dirty)
            {
                InvalidateRect(hwnd, nullptr, FALSE);

                if (g_cpuGraph.isActive())
                    g_cpuGraph.Update();

                if (g_gpuGraph.isActive())
                    g_gpuGraph.Update();
            }
        }
        else if (wParam == NETWORK_TIMER_ID)
        {
            KillTimer(hwnd, NETWORK_TIMER_ID);
            g_networkManager.Refresh();
            g_networkManager.Log();
            g_networkManager.m_timer = 0;
        }
        else if (wParam == SCREENSHOT_ICON_ID)
        {
            KillTimer(hwnd, SCREENSHOT_ICON_ID);
            ClearScreenshotIcon(g_backBuffer.memDC);
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
        HMENU hStartStopMenu = CreatePopupMenu();
        HMENU hTemplateMenu = CreatePopupMenu();
        const auto &list = g_networkManager.GetAddresses();
        const UINT flags = MF_STRING | (g_webServer.IsRunning() || !g_isAdmin) ? MF_DISABLED : 0;

        if (g_webServer.IsRunning())
        {
            AppendMenu(hStartStopMenu, MF_STRING, IDM_WEBSERVER_STOP, L"Stop server");
            AppendMenu(hStartStopMenu, MF_SEPARATOR, 0, nullptr);
        }

        if (!g_isAdmin)
        {
            AppendMenu(hStartStopMenu, MF_STRING, 0, L"Admin rights required");
            AppendMenu(hStartStopMenu, MF_SEPARATOR, 0, nullptr);
        }

        for (size_t i = 0; i < list.size(); ++i)
            AppendMenuW(hStartStopMenu, flags | (g_webServer.isBoundTo(list[i]) ? MF_CHECKED : MF_UNCHECKED), IDM_WEBSERVER_BASE + static_cast<UINT>(i), list[i].display().c_str());

        AppendMenu(hTemplateMenu, MF_STRING | (g_currentWebTemplate == IDM_WEBSERVER_TEMPLATE_LIGHT ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED), IDM_WEBSERVER_TEMPLATE_LIGHT, L"Mobile (light text)");
        AppendMenu(hTemplateMenu, MF_STRING | (g_currentWebTemplate == IDM_WEBSERVER_TEMPLATE_HEAVY ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED), IDM_WEBSERVER_TEMPLATE_HEAVY, L"PC (heavy CSS)");

        AppendMenuW(hWebServerMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hStartStopMenu), g_webServer.IsRunning() ? L"Stop" : L"Start");
        AppendMenuW(hWebServerMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hTemplateMenu), L"Template");

        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hWebServerMenu), L"Web Server");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        ////////////////////////////////////////////////////////

        /////////////////////////////////
        // FPS submenu
        HMENU hFPSMenu = CreatePopupMenu();

        AppendMenuW(hFPSMenu, MF_STRING | g_isFpsEnabled ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED, IDM_ENABLEFPS_BASE, L"On");
        AppendMenuW(hFPSMenu, MF_STRING | g_isFpsEnabled ? MF_UNCHECKED : MF_CHECKED | MF_DISABLED, IDM_ENABLEFPS_BASE + 1, L"Off");

        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hFPSMenu), L"FPS metric");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        /////////////////////////////////

        /////////////////////////////////
        // VRR submenu
        HMENU hVRRMenu = CreatePopupMenu();

        AppendMenuW(hVRRMenu, MF_STRING | g_isVRREnabled ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED, IDM_ENABLEVRR_BASE, L"On");
        AppendMenuW(hVRRMenu, MF_STRING | g_isVRREnabled ? MF_UNCHECKED : MF_CHECKED | MF_DISABLED, IDM_ENABLEVRR_BASE + 1, L"Off");

        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hVRRMenu), L"VRR detection");
        AppendMenu(hMenu, MF_SEPARATOR, 0, nullptr);
        /////////////////////////////////

        ///////////////////////////////
        // Screenshot submenu
        HMENU hScreenshotMenu = CreatePopupMenu();

        AppendMenuW(hMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hScreenshotMenu), L"Screenshot");

        // Key binder
        // HMENU hKeyMenu = CreatePopupMenu();
        // wchar_t keyName[64]{};

        // UINT scanCode = MapVirtualKeyW(static_cast<UINT>(g_screenshotKey), MAPVK_VK_TO_VSC);

        // if (scanCode != 0)
        // {
        //     LONG lp = static_cast<LONG>(scanCode << 16);
        //     if (GetKeyNameTextW(lp, keyName, ARRAYSIZE(keyName)) == 0)
        //         wcscpy_s(keyName, L"Unknown");
        // }

        // AppendMenuW(hKeyMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, keyName);
        // AppendMenuW(hKeyMenu, MF_SEPARATOR, 0, nullptr);
        // AppendMenuW(hKeyMenu, MF_STRING, IDM_SCREENSHOT_BINDKEY, L"Select Key...");

        // AppendMenuW(hScreenshotMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hKeyMenu), L"Bind key");

        // Format submenu
        HMENU hFormatMenu = CreatePopupMenu();

        AppendMenuW(hScreenshotMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hFormatMenu), L"Format");

        AppendMenuW(hFormatMenu, MF_STRING | (g_screenshot.m_format == Screenshot::Format::BMP ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED), IDM_SCREENSHOT_FORMAT_BMP, L"BMP");
        AppendMenuW(hFormatMenu, MF_STRING | (g_screenshot.m_format == Screenshot::Format::JPEG ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED), IDM_SCREENSHOT_FORMAT_JPEG, L"JPEG");
        AppendMenuW(hFormatMenu, MF_STRING | (g_screenshot.m_format == Screenshot::Format::PNG ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED), IDM_SCREENSHOT_FORMAT_PNG, L"PNG");

        // Save Folder submenu
        HMENU hSaveFolderMenu = CreatePopupMenu();

        AppendMenuW(hScreenshotMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hSaveFolderMenu), L"Save Folder");

        AppendMenuW(hSaveFolderMenu, MF_STRING | MF_DISABLED | MF_GRAYED, 0, g_screenshot.IsPathEmpty() ? L"(no folder selected)" : g_screenshot.GetPath());
        AppendMenuW(hSaveFolderMenu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(hSaveFolderMenu, MF_STRING, IDM_SCREENSHOT_SAVE_FOLDER, L"Select Folder...");

        // DualSense submenu
        HMENU hDualSenseMenu = CreatePopupMenu();

        AppendMenuW(hScreenshotMenu, MF_POPUP, reinterpret_cast<UINT_PTR>(hDualSenseMenu), L"DualSense Capture");
        AppendMenuW(hDualSenseMenu, MF_STRING | g_isDualSenseEnabled ? MF_CHECKED | MF_DISABLED : MF_UNCHECKED, IDM_ENABLEDUALSENSE_BASE, L"On");
        AppendMenuW(hDualSenseMenu, MF_STRING | g_isDualSenseEnabled ? MF_UNCHECKED : MF_CHECKED | MF_DISABLED, IDM_ENABLEDUALSENSE_BASE + 1, L"Off");

        ///////////////////////////////

        AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        ///////////////////////////////

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
        {
            g_alwaysOnTop = !g_alwaysOnTop;
            SetAlwaysOnTop(hwnd, g_alwaysOnTop);
            break;
        }

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
            CheckVersionAsync(hwnd, true);
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
                                   L"Contact: "
                                   L"<a href=\"mailto:amu2mod@gmail.com\">amu2mod@gmail.com</a>\r\n"
                                   L"\r\n"
                                   L"Licensed under the MIT License";
            ShowUpdateDialog(hwnd, L"About", APPNAME, message);
            break;
        }

        case IDM_EXIT:
        {
            DestroyWindow(hwnd);
            break;
        }

        case IDM_WEBSERVER_STOP:
        {
            g_webServer.Stop();
            g_serverStatusRc.SetValue(L"");
            g_serverStatusRc.ClearValueRC(g_backBuffer.memDC, BACKGROUNDCOLOR);
            InvalidateRect(hwnd, &g_serverStatusRc.valueRc, FALSE);
            UpdateWindow(hwnd);
            break;
        }

        case IDM_WEBSERVER_TEMPLATE_LIGHT:
        {
            g_currentWebTemplate = IDM_WEBSERVER_TEMPLATE_LIGHT;
            break;
        }

        case IDM_WEBSERVER_TEMPLATE_HEAVY:
        {
            g_currentWebTemplate = IDM_WEBSERVER_TEMPLATE_HEAVY;
            break;
        }

        case IDM_SCREENSHOT_FORMAT_BMP:
        {
            g_screenshot.m_format = Screenshot::Format::BMP;
            break;
        }

        case IDM_SCREENSHOT_FORMAT_JPEG:
        {
            g_screenshot.m_format = Screenshot::Format::JPEG;
            break;
        }

        case IDM_SCREENSHOT_FORMAT_PNG:
        {
            g_screenshot.m_format = Screenshot::Format::PNG;
            break;
        }

        case IDM_SCREENSHOT_SAVE_FOLDER:
        {
            std::wstring folder = SelectFolder(hwnd);

            if (!folder.empty())
            {
                LOG_INFO("[App] Setting screenshot folder to: %ls", folder.c_str());
                g_screenshot.SetPath(folder.c_str());
            }
            break;
        }

        case IDM_SCREENSHOT_BINDKEY:
        {
            // g_screenshotKeyBinder.Show();
            break;
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

                if (!g_webServer.LaunchServerOnInterface(netIf))
                {
                    LOG_ERROR("[App] Failed to launch the server on interface %ls (%ls)",
                              netIf.adapterName.c_str(), netIf.address.c_str());

                    std::wstring message =
                        L"Failed to launch the web server on interface\n\n"
                        L"Interface: " +
                        netIf.adapterName + L"\n"
                                            L"Address: " +
                        netIf.address + L"\n"
                                        L"Port: " +
                        WEBSERVER_PORT;

                    TaskDialog(
                        nullptr,
                        nullptr,
                        L"Server Launch Error",
                        L"Unable to start the web server.",
                        message.c_str(),
                        TDCBF_OK_BUTTON,
                        TD_ERROR_ICON,
                        nullptr);

                    break;
                }

                if (!g_DontShowHttpsWebServerWarning)
                {
                    std::wstring message =
                        L"The web server has started successfully.\n\n"
                        L"URL:\n"
                        L"https://" +
                        netIf.address + L":" + WEBSERVER_PORT + L"\n\n"
                                                                L"The server uses a self-signed HTTPS certificate. "
                                                                L"Your web browser may display a security warning the first time you connect. "
                                                                L"This is expected because the certificate is not issued by a public Certificate Authority.\n\n"
                                                                L"Before proceeding, verify that the browser is connecting to the address shown above.";

                    TASKDIALOGCONFIG config = {};
                    config.cbSize = sizeof(config);
                    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION;
                    config.pszWindowTitle = L"Web Server Started";
                    config.pszMainInstruction = L"HTTPS Web Server";
                    config.pszContent = message.c_str();
                    config.pszVerificationText = L"Don't show this message again";
                    config.dwCommonButtons = TDCBF_OK_BUTTON;

                    BOOL checked = FALSE;

                    TaskDialogIndirect(&config, nullptr, nullptr, &checked);

                    if (checked)
                        g_DontShowHttpsWebServerWarning = true;
                }

                auto addr = g_webServer.GetBoundInterface().value().address;
                std::wstring txt = L"https://" + addr + L":" + WEBSERVER_PORT;
                g_serverStatusRc.SetValue(txt.c_str());
                g_clickableUrlRect = GetCenteredTextRect(g_backBuffer.memDC, g_notificationFont, &g_serverStatusRc.valueRc, txt.c_str());
                g_serverStatusRc.DrawTextValue(g_backBuffer.memDC, SERVERSTATUSCOLOR, g_notificationFont);
                InvalidateRect(hwnd, &g_serverStatusRc.valueRc, FALSE);
                UpdateWindow(hwnd);
            }

            // FPS On/Off
            else if (LOWORD(wParam) == IDM_ENABLEFPS_BASE || LOWORD(wParam) == (IDM_ENABLEFPS_BASE + 1))
            {
                // Update state
                g_isFpsEnabled = (LOWORD(wParam) - IDM_ENABLEFPS_BASE) == 0 ? true : false;

                // Update UI
                auto &fpsProp = g_props[MetricsIndex::Fps];
                const int separatorIndex = MetricsIndex::Fps + 1;
                static_assert(separatorIndex < g_propCount);
                auto &sepProp = g_props[separatorIndex];
                const RECT rc1 = fpsProp.GetUnionRC();
                const RECT rc2 = sepProp.labelRc;

                RECT result;
                UnionRect(&result, &rc1, &rc2); // merges the fps line with the next separator

                if (g_isFpsEnabled)
                {
                    fpsProp.repaintLabel = true;
                    fpsProp.dirty = true;
                    fpsProp.repaintLabel = true;
                    sepProp.dirty = true;
                }
                else
                {
                    HBRUSH brush = CreateSolidBrush(BACKGROUNDCOLOR);
                    FillRect(g_backBuffer.memDC, &result, brush);
                    DeleteObject(brush);
                }

                InvalidateRect(hwnd, &result, FALSE);
                // UpdateWindow(hwnd);

                LOG_DEBUG("FPS %s", g_isFpsEnabled ? "On" : "Off");
            }
            // VRR On/Off
            else if (LOWORD(wParam) == IDM_ENABLEVRR_BASE || LOWORD(wParam) == (IDM_ENABLEVRR_BASE + 1))
            {
                g_isVRREnabled = (LOWORD(wParam) - IDM_ENABLEVRR_BASE) == 0 ? true : false;

                LOG_DEBUG("[App] VRR detection %s", g_isVRREnabled ? "On" : "Off");

                if (g_isVRREnabled)
                {
                    g_vrrDetector.Start();
                }
                else
                {
                    g_vrrDetector.Stop();

                    // clear ui
                    const RECT &labelRc = g_props[MetricsIndex::Display].textLabelRc;
                    int x = labelRc.right + g_layoutMetrics.charWidth + g_layoutMetrics.tagGap;
                    int y = labelRc.top + (labelRc.bottom - labelRc.top - g_layoutMetrics.tagCharHeight) / 2;
                    const int topPadding = g_layoutMetrics.tagTopPadding;
                    const int sidePadding = g_layoutMetrics.tagSidePadding;
                    HBRUSH bgBrush = CreateSolidBrush(BACKGROUNDCOLOR);
                    RECT vrrTagRc = {x - sidePadding, y - topPadding, x + g_layoutMetrics.tagCharWidth * 3 + 2 * sidePadding, y + g_layoutMetrics.tagCharHeight + topPadding};

                    FillRect(g_backBuffer.memDC, &vrrTagRc, bgBrush);
                    DeleteObject(bgBrush);

                    InvalidateRect(hwnd, &vrrTagRc, FALSE);
                }
            }
            // DualSense Screenshot Capture
            else if (LOWORD(wParam) == IDM_ENABLEDUALSENSE_BASE || LOWORD(wParam) == (IDM_ENABLEDUALSENSE_BASE + 1))
            {
                g_isDualSenseEnabled = (LOWORD(wParam) - IDM_ENABLEDUALSENSE_BASE) == 0 ? true : false;

                LOG_DEBUG("[App] DualSense Screenshot Capture %s", g_isDualSenseEnabled ? "On" : "Off");

                if (g_isDualSenseEnabled)
                    g_dualsense.Start();
                else
                    g_dualsense.Stop();

                RECT rect = GetGamepadIconRect();
                InvalidateRect(hwnd, &rect, FALSE);
            }
            return 0;
        }
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

    case WM_WINDOWPOSCHANGING:
    {
        auto *wp = reinterpret_cast<WINDOWPOS *>(lParam);

        // prevents the window going above the top edge
        if (g_dragging)
        {
            HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
            MONITORINFO mi{};
            mi.cbSize = sizeof(mi);

            if (GetMonitorInfo(monitor, &mi))
            {
                const int top = mi.rcWork.top;
                if (wp->y < top)
                    wp->y = top;
            }
        }

        break;
    }

    case WM_LBUTTONDOWN:
    {

        POINT pt;
        pt.x = LOWORD(lParam);
        pt.y = HIWORD(lParam);

        if (PtInRect(&g_props[MetricsIndex::Cpu].textLabelRc, pt) && g_cpu.IsInitialized()) // cpu
        {
            if (g_cpuGraph.isActive()) // already exists
            {
                g_cpuGraph.Close();
                LOG_DEBUG("Closing CPU Graph");
                return 0;
            }

            g_cpuGraph.Create(hwnd);
            g_cpuGraph.Show();
            SetAlwaysOnTop(hwnd, g_alwaysOnTop);

            LOG_DEBUG("Showing CPU Graph");
            return 0;
        }

        else if (PtInRect(&g_props[MetricsIndex::Temp].textLabelRc, pt) && g_AdlxGPUTelemetry.isInitialized) // gpu
        {
            if (g_gpuGraph.isActive())
            {
                g_gpuGraph.Close();
                return 0;
            }

            g_gpuGraph.Create(hwnd);
            g_gpuGraph.Show();
            SetAlwaysOnTop(hwnd, g_alwaysOnTop);

            LOG_DEBUG("Showing GPU Graph");
            return 0;
        }

        else if (PtInRect(&g_clickableUrlRect, pt) && g_webServer.IsRunning()) // web server url
        {
            LOG_DEBUG("Opening %ls", g_serverStatusRc.textValue);
            INT_PTR ret = OpenUrl(g_serverStatusRc.textValue);
            if (ret <= 32)
                LOG_ERROR("Failed to open URL (ShellExecuteW): %td", ret);
            return 0;
        }
        else if (PtInRect(&g_props[MetricsIndex::Display].textLabelRc, pt)) // display label
        {
            auto current = g_displayManager.Next();
            if (current.has_value())
            {
                SetDisplayLine(current.value(), hwnd);
                g_currentDisplayIndex = current.value().index;
            }
        }
        else if (PtInRect(&g_props[MetricsIndex::Display].valueRc, pt)) // display value
        {
            LOG_DEBUG("[App] show display window");
        }

        SetCapture(hwnd);
        g_dragging = true;
        RECT rc;
        if (!GetWindowRect(hwnd, &rc))
            LOG_ERROR("[App] Failed GetWindowRect");
        else
        {
            g_draggingX = rc.left;
            g_draggingY = rc.top;
        }

        GetCursorPos(&g_dragStart);
        GetWindowRect(hwnd, &g_wndStart);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        if (g_dragging)
        {
            SetCursor(LoadCursor(nullptr, IDC_SIZEALL));

            POINT cur{};
            GetCursorPos(&cur);
            int dx = cur.x - g_dragStart.x;
            int dy = cur.y - g_dragStart.y;
            SetWindowPos(hwnd, nullptr, g_wndStart.left + dx, g_wndStart.top + dy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
        else
        {
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};

            if (g_cpu.IsInitialized() && PtInRect(&g_props[MetricsIndex::Cpu].textLabelRc, pt)) // cpu
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            else if (g_AdlxGPUTelemetry.isInitialized && PtInRect(&g_props[MetricsIndex::Temp].textLabelRc, pt)) // gpu temp
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            else if (PtInRect(&g_clickableUrlRect, pt) && g_webServer.IsRunning()) // web server url
                SetCursor(LoadCursor(nullptr, IDC_HAND));
            else if (PtInRect(&g_props[MetricsIndex::Display].textLabelRc, pt) || PtInRect(&g_props[MetricsIndex::Display].valueRc, pt)) // display label
                SetCursor(LoadCursor(nullptr, IDC_HAND));

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

            RECT rc;
            if (!GetWindowRect(hwnd, &rc))
                LOG_ERROR("[App] Failed to get RECT of the window");
            else
            {
                if (g_draggingX != rc.left || g_draggingY != rc.top)
                    LOG_DEBUG("[App] moved to {%d,%d}", rc.left, rc.top);
            }
        }
        return 0;
    }

    case WM_DISPLAYCHANGE:
    {
        LOG_WM("[App] WM_DISPLAYCHANGE");

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

    // Custom WMs
    case WM_APP_LAYOUT:
    {
        LOG_WM("[App] WM_APP_LAYOUT");

        g_layoutMetrics = CalculateLayoutMetrics(g_backBuffer.memDC);
        LayoutProperties2(g_layoutMetrics);

        g_layoutMetrics.Log();

        // commit new repaint
        SetAllRepaintLabelFlag(true); // force all labels repaint
        g_forceFrameRedraw = true;    // force new border + title
        SetWindowPos(hwnd, nullptr, 0, 0, g_layoutMetrics.windowWidth, g_layoutMetrics.windowHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

        UpdateToolTipRect(hwnd, TOOLID_GPUINFO, g_cardName.valueRc);

        // update url rect
        if (g_webServer.IsRunning())
        {
            auto addr = g_webServer.GetBoundInterface().value().address;
            std::wstring txt = L"https://" + addr + L":" + WEBSERVER_PORT;
            g_clickableUrlRect = GetCenteredTextRect(g_backBuffer.memDC, g_notificationFont, &g_serverStatusRc.valueRc, txt.c_str());
        }
        return 0;
    }

    case WM_APP_VERSION_RESULT:
    {
        auto result = reinterpret_cast<VersionCheckResult *>(lParam);

        if (result->updateAvailable)
        {
            g_appTitle.SetTitle(APPNAME_UPDATE, APPNAME_UPDATE_LENGTH);
            g_appTitle.UpdateRC(g_backBuffer.memDC, g_layoutMetrics, g_border.top, g_titleFont);
            g_forceFrameRedraw = true;
            InvalidateRect(hwnd, &g_appTitle.rc, FALSE);

            if (result->showDialogs)
            {
                std::wstring message = L"Current version: " + std::wstring(Version::String) + L"\n" +
                                       L"Latest version: " + result->latestVersion + L"\n" +
                                       +L"\n" +
                                       L"<a href=\"" +
                                       LATESTURL + L"\">Download latest version here</a>\n";
                ShowUpdateDialog(hwnd, L"Update Check", L"Update available", message);
                LOG_WARN("[App] New update available: %ls", result->latestVersion.c_str());
            }
        }
        else if (result->showDialogs)
        {
            std::wstring message = L"You are already running the latest version:\n" + std::wstring(Version::String);
            ShowUpdateDialog(hwnd, L"Update Check", L"No update available", message);
        }

        delete result;
        return 0;
    }

    case WM_APP_VERSION_ERROR:
    {
        LOG_ERROR("Failed to check update");
        ShowUpdateDialog(hwnd, L"Update Check", L"Error", L"Failed to check for updates.", true);
        return 0;
    }

    case WM_APP_GPU_PWR_TUNING_CHANGE:
    {
        wchar_t powerBuffer[16];

        auto snapshot = g_AdlxGPUTelemetry.Get();

        if (!snapshot.powerLimit.isSupported)
            FormatPowerConsumption(powerBuffer, static_cast<int>(snapshot.totalBoardPower.value));
        else
        {
            int percent = static_cast<int>(static_cast<int>(snapshot.totalBoardPower.value) * 100.0 / snapshot.powerLimitWatts.value);
            FormatPowerConsumption(powerBuffer, static_cast<int>(snapshot.totalBoardPower.value), percent);
        }
        SetPropertyValueAtIndex(MetricsIndex::Power, static_cast<int>(snapshot.totalBoardPower.value), powerBuffer, 16);
        return 0;
    }

    case WM_APP_APPLY_TOPMOST:
    {
        SetAlwaysOnTop(hwnd, g_alwaysOnTop);
        return 0;
    }

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, [[maybe_unused]] int nCmdShow)
{
#ifdef _DEBUG
    AllocConsole();
    EnableConsoleColors();
    FILE *f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$", "r", stdin);
    LOG_INFO("[Main] Starting App");
#endif

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    g_isAdmin = IsRunningAsAdministrator();
    g_appPID = GetCurrentProcessId();

    if (!InitScreenshotSound())
    {
        LOG_ERROR("[App] Failed to init screenshot sound");
    }

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

    // Register F12 as a global hotkey
    if (!RegisterHotKey(nullptr, HOTKEY_SCREENSHOT, 0, g_screenshotKey))
        LOG_ERROR("[App] Failed to register hotkey: %d", GetLastError());
    else
        LOG_DEBUG("[App] Hotkey registered successfully (VK_SCROLL)");

    g_autostart = IsAutostartEnabled();

    POINT pt = {g_xPos, g_yPos};

    if (isPointValid(pt))
    {
        g_dpi = getDpiFromPoint(pt);
        g_width = DPIScale(g_width);
        g_height = DPIScale(g_height);
    }
    else
    {
        LOG_ERROR("[Main] Invalid position from preferences, reseting the position");
        g_dpi = GetDpiForSystem();
        LOG_DEBUG("[Main] DPI: %u (%.0f%%)", g_dpi, (g_dpi / 96.0) * 100.0);
        g_width = MulDiv(g_width, g_dpi, 96);
        g_height = MulDiv(g_height, g_dpi, 96);

        // Primary screen size
        int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        int screenHeight = GetSystemMetrics(SM_CYSCREEN);

        // Center position on primary screen
        g_xPos = (screenWidth - g_width) / 2;
        g_yPos = (screenHeight - g_height) / 2;
    }

    InitTooltips();

    LOG_DEBUG("[Main] window created %dx%d at position {%d,%d}", g_width, g_height, g_xPos, g_yPos);
    HWND hwnd = CreateWindowEx(0, CLASS_NAME, APPNAME, WS_POPUP, g_xPos, g_yPos, g_width, g_height, nullptr, nullptr, hInstance, nullptr);

    if (!hwnd)
    {
        LOG_ERROR("CreateWindowEx failed");
        return 1;
    }

    // DualSense
    g_dualsense.SetOnConnected([]()
                               { LOG_INFO("[APP] DualSense connected"); });
    g_dualsense.SetOnDisconnected([]()
                                  { LOG_INFO("[APP] DualSense disconnected"); });
    g_dualsense.SetOnCreateButtonPressed([hwnd]()
                                         { OnScreenshotAction(hwnd); });
    if (g_isDualSenseEnabled)
        g_dualsense.Start();

    ShowWindow(hwnd, SW_SHOWNOACTIVATE);

    g_networkManager.Initialize(hwnd);
    g_networkManager.Log();

    SetTimer(hwnd, APP_POLLING_ID, APP_REFRESH_TIMER, nullptr);

    g_AdlxGPUTelemetry.Init(hwnd);
    g_AdlxGPUTelemetry.Discover();
    g_AdlxGPUTelemetry.Probe();

    g_displayManager.Discover();

    const RadeonMon::Hardware::DisplayManager &manager = g_displayManager;
    const auto &display = manager.Current();
    if (display.has_value())
        SetDisplayLine(display.value());

    if (g_isVRREnabled)
        g_vrrDetector.Start();

    if (g_AdlxGPUTelemetry.isInitialized)
        UpdateToolTipText(hwnd, TOOLID_GPUINFO, g_AdlxGPUTelemetry.GetGpuInfo().GetTooltip(), g_AdlxGPUTelemetry.GetGpuInfo().GetDriverPathTooltipWidth(g_hwndTooltip));

    LOG_DEBUG("[Main] Admin mode: %s", g_isAdmin ? "yes" : "no");

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
                SetPropertyValueAtIndex(MetricsIndex::Cpu, NotSupported, L"not supported", 14);
            else
                g_cpu.Start();
        }
    }
    else
    {
        // SetPropertyValueAtIndex(MetricsIndex::Cpu, AdminRequired, L"admin required", 15);
        g_notification.SetValue(L"cpu requires admin rights");
    }

    CheckVersionAsync(hwnd, false);

    // ShowWindow(hwnd, nCmdShow);
    // UpdateWindow(hwnd);
    PostMessage(hwnd, WM_APP_APPLY_TOPMOST, 0, 0);

    if (g_isCpuGraphEnabled && g_cpu.IsInitialized())
    {
        g_cpuGraph.Create(hwnd);
        g_cpuGraph.Show();
        SetAlwaysOnTop(hwnd, g_alwaysOnTop);
    }

    if (g_isGpuGraphEnabled && g_AdlxGPUTelemetry.isInitialized)
    {
        g_gpuGraph.Create(hwnd);
        g_gpuGraph.Show();
        SetAlwaysOnTop(hwnd, g_alwaysOnTop);
    }

    // PresentMon
    {
        const int i = g_presentMonManager.Init();
        switch (i)
        {
        case -1:
        {
            TaskDialog(
                nullptr, nullptr,
                L"PresentMon SDK Not Found",
                L"Unable to load the PresentMon SDK.",
                L"The PresentMon API DLL could not be found or loaded. Some FPS monitoring features will be unavailable.",
                TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
            break;
        }
        case -2:
        {
            TaskDialog(
                nullptr, nullptr,
                L"PresentMon Service Not Running",
                L"Unable to connect to the PresentMon service.",
                L"The PresentMon service is not running. Some FPS monitoring features will be unavailable.",
                TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
            break;
        }
        case -3:
        {
            TaskDialog(
                nullptr, nullptr,
                L"Unable to Open Session",
                L"The PresentMon session could not be opened.",
                L"Some FPS monitoring features will be unavailable.",
                TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
            break;
        }

        case -4:
        {
            TaskDialog(
                nullptr, nullptr,
                L"PresentMon API Unavailable",
                L"The PresentMon API could not be initialized.",
                L"The required PresentMon API function could not be found in the DLL. Some FPS monitoring features will be unavailable.",
                TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
            break;
        }
        default:
            break;
        };
    }

    LOG_INFO("[Main] Entering Dispatcher loop");

    MSG msg{};
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (msg.message == WM_HOTKEY)
            if (msg.wParam == HOTKEY_SCREENSHOT)
            {
                LOG_DEBUG("VK_SCROLL pressed");
                OnScreenshotAction(hwnd);
            }

        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnregisterHotKey(nullptr, HOTKEY_SCREENSHOT);
    CoUninitialize();

    return static_cast<int>(msg.wParam);
}