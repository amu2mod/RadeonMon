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

void LayoutProperties(RECT rc)
{
    int x = DPIScale(PADDING_LEFT);
    int y = DPIScale(PADDING_TOP);

    int labelWidth = DPIScale(LABEL_WIDTH);
    int lineHeight = DPIScale(LINE_HEIGHT);
    int gap = DPIScale(GAP);

    int right = rc.right - DPIScale(PADDING_LEFT);

    // Layout regular properties
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

    // Layout Card Name at the bottom of the window
    int bottomY = rc.bottom - DPIScale(PADDING_TOP) - lineHeight;
    g_cardName.valueRc = {x, bottomY, right, bottomY + lineHeight};
}

void PaintProperties(HDC hdc)
{
    static HPEN pen = CreatePen(PS_SOLID, 1, SEPARATORCOLOR);
    HGDIOBJ oldPen = SelectObject(hdc, pen);

#ifdef _DEBUG
    int gdiCount = 0;
#endif

    // Paint regular properties
    for (auto &p : g_props)
    {
        if (p.type == PropertyType::Separator && p.dirty)
        {
            MoveToEx(hdc, p.labelRc.left, p.labelRc.top, nullptr);
            LineTo(hdc, p.labelRc.right, p.labelRc.top);

#ifdef _DEBUG
            gdiCount++;
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
            gdiCount++;
#endif
        }

        SetTextColor(hdc, p.warning ? WARNINGCOLOR : VALUECOLOR);
        DrawTextW(hdc, p.textValue, -1, &p.valueRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

#ifdef _DEBUG
        gdiCount++;
#endif

        p.dirty = false;
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
        gdiCount += 3;
#endif

        g_cardName.dirty = false;
    }

#ifdef _DEBUG
    // LOG_DEBUG("gdi count=%d", gdiCount);
#endif

    SelectObject(hdc, oldPen);
}

void SetAlwaysOnTop(HWND hWnd, bool enable)
{
    SetWindowPos(
        hWnd,
        enable ? HWND_TOPMOST : HWND_NOTOPMOST,
        0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE);
}

// ── window procedure ─────────────────────────────────────────────────────────

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_dpi = GetDpiForWindow(hwnd);
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

        RECT rc{};
        GetClientRect(hwnd, &rc);

        // draw into backbuffer
        SetBkMode(g_backBuffer.memDC, TRANSPARENT);

        if (g_font)
            SelectObject(g_backBuffer.memDC, g_font);

        PaintProperties(g_backBuffer.memDC);

        // present
        BitBlt(hdc, 0, 0, rc.right - rc.left, rc.bottom - rc.top, g_backBuffer.memDC, 0, 0, SRCCOPY);

        EndPaint(hwnd, &ps);

        // POST 1st paint hook
        static bool firstPaintDone = false;
        if (!firstPaintDone)
        {
            firstPaintDone = true;
            SetAllPropertiesInitFlag(true);
        }

        return 0;
    }

    case WM_SIZE:
    {
        int w = LOWORD(lParam);
        int h = HIWORD(lParam);

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
        LOG_DEBUG("New DPI: %u (%.0f%%)", g_dpi, (g_dpi / 96.0) * 100.0);

        RecreateFont();

        const RECT *r = reinterpret_cast<const RECT *>(lParam);

        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        SetAllPropertiesInitFlag(false); // force all labels repaint

        InvalidateRect(hwnd, nullptr, TRUE);

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
                SetPropertyValueAtIndex(MetricsIndex::Temp, snapshot.temperature, tempBuffer, 16, snapshot.temperature > TEMPERATURE_THRESHOLD);
            }

            // Hotspot
            if (GetPropertyValueAtIndex(MetricsIndex::Hotspot) != snapshot.hotspot)
            {
                dirty = true;
                wchar_t hotspotBuffer[16];
                FormatHotspot(hotspotBuffer, snapshot.temperature, snapshot.hotspot);
                SetPropertyValueAtIndex(MetricsIndex::Hotspot, snapshot.hotspot, hotspotBuffer, 16, snapshot.hotspot > TEMPERATURE_THRESHOLD);
            }

            // VRAM Temp
            if (GetPropertyValueAtIndex(MetricsIndex::Vram) != snapshot.vram)
            {
                dirty = true;
                wchar_t vramBuffer[16];
                FormatTemperature(vramBuffer, snapshot.vram);
                SetPropertyValueAtIndex(MetricsIndex::Vram, snapshot.vram, vramBuffer, 16, snapshot.vram > TEMPERATURE_THRESHOLD);
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

        AppendMenu(hMenu, MF_STRING | (g_alwaysOnTop ? MF_CHECKED : MF_UNCHECKED), IDM_ALWAYS_ON_TOP, L"Always on Top");

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        // Keyboard context menu (Shift+F10 or Menu key)
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
        if (LOWORD(wParam) == IDM_ALWAYS_ON_TOP)
        {
            g_alwaysOnTop = !g_alwaysOnTop;
            SetWindowPos(hwnd, g_alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
            return 0;
        }

        break;
    }

    case WM_DESTROY:
    {
        g_backBuffer.Destroy();

        if (g_font)
        {
            DeleteObject(g_font);
            g_font = nullptr;
        }

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

    // Center in primary screen + dpi scaling
    UINT dpi = GetDpiForSystem();
    LOG_DEBUG("DPI: %u (%.0f%%)", dpi, (dpi / 96.0) * 100.0);
    int width = MulDiv(APPWIDTH, dpi, 96);
    int height = MulDiv(APPHEIGHT, dpi, 96);

    // Primary screen size
    int screenWidth = GetSystemMetrics(SM_CXSCREEN);
    int screenHeight = GetSystemMetrics(SM_CYSCREEN);

    // Center position on primary screen
    int x = (screenWidth - width) / 2;
    int y = (screenHeight - height) / 2;

    HWND hwnd = CreateWindowEx(0, CLASS_NAME, APPNAME, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, x, y, width, height, nullptr, nullptr, hInstance, nullptr);

    LayoutProperties({0, 0, width, height}); // Initial layout

    SetTimer(hwnd, 1, APP_REFRESH_TIMER, nullptr);

    g_AdlxGPUTelemetry.Init();
    g_AdlxGPUTelemetry.Discover();

    bool isAdmin = IsRunningAsAdministrator();
    LOG_DEBUG("Admin mode: %s", isAdmin ? "yes" : "no");

    if (isAdmin)
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
        SetPropertyValueAtIndex(MetricsIndex::Cpu, AdminRequired, L"admin required", 15);
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