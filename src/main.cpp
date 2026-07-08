#include <windows.h>
#include <shellscalingapi.h>
#include <windowsx.h>

#include <cstdio>
#include <cmath>
#include <cassert>

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

#pragma comment(lib, "Shcore.lib")

enum MetricsIndex
{
    Temp = 0,
    Hotspot = 2,
    Vram = 4,
    FanSpeed = 6,
    Power = 8,
    Cpu = 10,
};

void ResetDirty()
{
    g_notification.dirty = true;
    g_cardName.dirty = true;
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

void SetAllPropertiesInitFlag(bool state)
{
    for (PropertyItem &p : g_props)
        p.initialized = state;
}

void LayoutFrame(RECT rc)
{
    float fontScale = static_cast<float>(g_fontSize) / FONTSIZE;

    auto ScaleFont = [&](int value)
    {
        return DPIScale(static_cast<int>(value * fontScale));
    };

    int border = ScaleFont(BORDER);
    int titleHeight = ScaleFont(TITLE_HEIGHT);

    g_border.top = {0, 0, rc.right, border};
    g_border.bottom = {0, rc.bottom - border, rc.right, rc.bottom};
    g_border.left = {0, border, border, rc.bottom - border};
    g_border.right = {rc.right - border, border, rc.right, rc.bottom - border};
    g_titleRc = {border, border, rc.right - border, border + titleHeight};
    g_titleTextRc = g_titleRc;

#ifdef _DEBUG
    LOG_DEBUG("LayoutFrame: window={%ld,%ld,%ld,%ld} border=%d titleHeight=%d", rc.left, rc.top, rc.right, rc.bottom, border, titleHeight);

    DebugRect("border.top", g_border.top);
    DebugRect("border.bottom", g_border.bottom);
    DebugRect("border.left", g_border.left);
    DebugRect("border.right", g_border.right);

    DebugRect("title", g_titleRc);
    DebugRect("titleText", g_titleTextRc);
#endif
}

void LayoutProperties(RECT rc)
{
    LOG_DEBUG("LayoutProperties rc={%ld,%ld,%ld,%ld}",
              rc.left, rc.top, rc.right, rc.bottom);

    if (rc.right <= 0 || rc.bottom <= 0)
    {
        LOG_DEBUG("LayoutProperties skipped invalid rect");
        return;
    }

    const int border = DPIScale(BORDER);
    const int titleHeight = DPIScale(TITLE_HEIGHT);
    const int paddingLeft = DPIScale(PADDING_LEFT);
    const int paddingTop = DPIScale(PADDING_TOP);
    const int labelWidth = DPIScale(LABEL_WIDTH);
    const int lineHeight = DPIScale(LINE_HEIGHT);
    const int gap = DPIScale(GAP);

    const int x = border + paddingLeft;
    int y = border + titleHeight + paddingTop;

    const int right = rc.right - border - paddingLeft;

    // Window frame
    g_windowRc = {0, 0, rc.right, rc.bottom};
    LayoutFrame(rc);

    // Title
    g_titleTextRc = g_titleRc;

    // Properties
    for (auto &p : g_props)
    {
        if (p.type == PropertyType::Separator)
        {
            y += gap;
            p.labelRc = {x, y, right, y + DPIScale(1)};
            y += gap;
            continue;
        }

        p.labelRc = {x, y, x + labelWidth, y + lineHeight};
        p.valueRc = {x + labelWidth + gap, y, right, y + lineHeight};

        y += lineHeight;
    }

    // notification + card name
    const int gap2 = DPIScale(3);
    const int cardNameY = rc.bottom - border - paddingTop - lineHeight;
    const int notificationY = cardNameY - gap2 - lineHeight;
    g_notification.valueRc = {x, notificationY, right, notificationY + lineHeight};
    g_cardName.valueRc = {x, cardNameY, right, cardNameY + lineHeight};
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

        if (!p.initialized)
        {
            SetTextColor(hdc, LABELCOLOR);
            DrawTextW(hdc, p.label, -1, &p.labelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
#ifdef _DEBUG
            g_gdiDrawCallCount++;
#endif
        }

        SetTextColor(hdc, p.warning ? WARNINGCOLOR : VALUECOLOR);
        DrawTextW(hdc, p.textValue, -1, &p.valueRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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

    // Paint Card Name
    if (g_cardName.dirty)
    {
        RECT rc = g_cardName.valueRc;

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

        DrawTextW(hdc, g_cardName.textValue, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

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

void ResizeWindow(HWND hwnd, int delta)
{
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    SetWindowPos(hwnd, nullptr, rc.left, rc.top, rc.right - rc.left + delta, rc.bottom - rc.top + delta, SWP_NOZORDER | SWP_NOACTIVATE);
}

void OnResizeWindow(HWND hwnd, bool grow)
{
    UINT oldFontSize = g_fontSize;

    if (grow)
        g_fontSize = min(g_fontSize + 1, (UINT)FONTSIZE_MAX);
    else
        g_fontSize = max(g_fontSize - 1, (UINT)FONTSIZE_MIN);

    // Already at min/max
    if (g_fontSize == oldFontSize)
        return;

    RecreateFont();
    g_forceFrameRedraw = true;
    ResizeWindow(hwnd, grow ? WINDOW_RESIZE_STEP : -WINDOW_RESIZE_STEP);
    RECT rc{};
    GetClientRect(hwnd, &rc);
    LayoutProperties(rc);
    SetAllPropertiesInitFlag(false);
    InvalidateRect(hwnd, nullptr, TRUE);

    LOG_DEBUG("new window: %dx%d, font@%dpx", rc.right - rc.left, rc.bottom - rc.top, g_fontSize);
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

    // Title bar
    FillRect(hdc, &g_titleRc, frameBrush);

    // Title text
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));

    DrawText(hdc, APPNAME, -1, &g_titleTextRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

#ifdef _DEBUG
    g_gdiDrawCallCount += 6;
#endif
}

void UpdateCurrentPosition(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    g_xPos = rc.left;
    g_yPos = rc.top;
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
            SetAllPropertiesInitFlag(true);
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

            LayoutProperties({0, 0, w, h});
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
        SetAllPropertiesInitFlag(false); // force all labels repaint
        g_forceFrameRedraw = true;       // force new border + title

        InvalidateRect(hwnd, nullptr, TRUE);

        LOG_DEBUG("New DPI: %u (%.0f%%), %dxx%d", g_dpi, (g_dpi / 96.0) * 100.0, g_width, g_height);

        return 0;
    }

    case WM_TIMER:
    {
        if (wParam == 1)
        {
            if (!g_AdlxGPUTelemetry.isInitialized)
            {
                LOG_WARN("adlx not init");
                g_AdlxGPUTelemetry.Init(); // retry
                return 0;
            }

            bool dirty = false;

            g_AdlxGPUTelemetry.Tick();
            GpuMetricsSnapshot snapshot = g_AdlxGPUTelemetry.Get();

            if (!snapshot.valid)
                return 0;

            // GPU Temp
            if (GetPropertyValueAtIndex(MetricsIndex::Temp) != snapshot.temperature)
            {
                dirty = true;
                wchar_t tempBuffer[16];
                FormatTemperature(tempBuffer, snapshot.temperature);
                SetPropertyValueAtIndex(MetricsIndex::Temp, snapshot.temperature, tempBuffer, 16, snapshot.temperature >= TEMPERATURE_THRESHOLD);
            }

            // Hotspot
            if (GetPropertyValueAtIndex(MetricsIndex::Hotspot) != snapshot.hotspot)
            {
                dirty = true;
                wchar_t hotspotBuffer[16];
                FormatHotspot(hotspotBuffer, snapshot.temperature, snapshot.hotspot);
                SetPropertyValueAtIndex(MetricsIndex::Hotspot, snapshot.hotspot, hotspotBuffer, 16, snapshot.hotspot >= TEMPERATURE_THRESHOLD);
            }

            // VRAM Temp
            if (GetPropertyValueAtIndex(MetricsIndex::Vram) != snapshot.vram)
            {
                dirty = true;
                wchar_t vramBuffer[16];
                FormatTemperature(vramBuffer, snapshot.vram);
                SetPropertyValueAtIndex(MetricsIndex::Vram, snapshot.vram, vramBuffer, 16, snapshot.vram >= TEMPERATURE_THRESHOLD);
            }

            // Fan Speed
            if (GetPropertyValueAtIndex(MetricsIndex::FanSpeed) != snapshot.fanSpeed)
            {
                dirty = true;
                wchar_t fanBuffer[16];
                FormatFanSpeed(fanBuffer, snapshot.fanSpeed);
                SetPropertyValueAtIndex(MetricsIndex::FanSpeed, snapshot.fanSpeed, fanBuffer, 16);
            }

            // Power Consumption
            if (GetPropertyValueAtIndex(MetricsIndex::Power) != snapshot.power)
            {
                dirty = true;
                wchar_t powerBuffer[16];
                FormatPowerConsumption(powerBuffer, snapshot.power);
                SetPropertyValueAtIndex(MetricsIndex::Power, snapshot.power, powerBuffer, 16);
            }

            if (g_cpu.IsInitialized())
            {
                RyzenMetrics cpuMetrics;

                if (g_cpu.GetRyzenMetrics(cpuMetrics))
                {
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
            }

            if (dirty)
                InvalidateRect(hwnd, nullptr, FALSE);
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
        }

        return 0;
    }

    case WM_KEYDOWN:
    {
        bool ctrlDown = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

        if (ctrlDown)
        {
            switch (wParam)
            {
            case VK_ADD: // Numpad +
                OnResizeWindow(hwnd, true);
                return 0;

            case VK_SUBTRACT: // Numpad -
                OnResizeWindow(hwnd, false);
                return 0;
            }
        }

        break;
    }

    case WM_LBUTTONDOWN:
    {
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

    case WM_DESTROY:
    {
        g_backBuffer.Destroy();

        if (g_font)
        {
            DeleteObject(g_font);
            g_font = nullptr;
        }

        UpdateCurrentPosition(hwnd);
        SavePreferences();

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
        g_width = MulDiv(APPWIDTH, g_dpi, 96);
        g_height = MulDiv(APPHEIGHT, g_dpi, 96);

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

    SetAlwaysOnTop(hwnd, g_alwaysOnTop);

    SetTimer(hwnd, 1, APP_REFRESH_TIMER, nullptr);

    g_AdlxGPUTelemetry.Init();
    g_AdlxGPUTelemetry.Discover();

    LOG_DEBUG("Admin mode: %s", g_isAdmin ? "yes" : "no");

    if (g_isAdmin)
    {
        if (!LoadAMDDLLs())
        {
            SetPropertyValueAtIndex(MetricsIndex::Cpu, SdkRequired, L"Ryzen SDK required", 19);
        }
        else
        {
            if (!g_cpu.Init())
            {
                SetPropertyValueAtIndex(MetricsIndex::Cpu, NotSupported, L"not supported", 14);
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