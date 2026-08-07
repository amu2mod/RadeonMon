#include "radeonmon/cpugraph.hpp"
#include "radeonmon/logging.hpp"
#include "radeonmon/helpers.hpp"
#include "radeonmon/preferences.hpp"

#include <assert.h>

#include <string>

bool CpuGraphWindow::Create(HWND hParent)
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        return true;
    }

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = CpuGraphWindow::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"CpuGraphWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassEx(&wc);

    if (!LoadSettings())
    {
        LOG_ERROR("[CPU] Failed to load settings");

        m_x = -1;
        m_y = -1;
        m_width = 400;
        m_height = 300;
        m_savedDpi = 96;
    }

    bool validSize = m_width >= 100 && m_width <= 10000 && m_height >= 100 && m_height <= 10000;
    bool validDpi = m_savedDpi >= 72 && m_savedDpi <= 1000;

    RECT windowRect{m_x, m_y, m_x + m_width, m_y + m_height};
    HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONULL);

    bool validPosition = (monitor != nullptr);

    UINT dpiX = 96;
    UINT dpiY = 96;

    if (validPosition && validDpi)
    {
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    }

    if (validSize && validDpi && validPosition)
    {
        m_width = MulDiv(m_width, dpiX, m_savedDpi);
        m_height = MulDiv(m_height, dpiY, m_savedDpi);
    }
    else if (!validSize || !validDpi)
    {
        LOG_ERROR("[CPU] Invalid size/dpi settings, resetting window size");

        HMONITOR fallbackMonitor = validPosition ? monitor : MonitorFromPoint(POINT{m_x, m_y}, MONITOR_DEFAULTTONEAREST);
        GetDpiForMonitor(fallbackMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

        m_width = MulDiv(400, dpiX, 96);
        m_height = MulDiv(300, dpiY, 96);
    }

    if (!validPosition)
    {
        LOG_ERROR("[CPU] Invalid position settings, resetting window position");

        POINT cursor;
        GetCursorPos(&cursor);

        HMONITOR cursorMonitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);
        GetDpiForMonitor(cursorMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

        int xScale = MulDiv(60, dpiX, 96);
        int yScale = MulDiv(15, dpiY, 96);

        m_x = cursor.x - xScale;
        m_y = cursor.y - yScale;
    }

    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST,
        L"CpuGraphWindow",
        Utf8ToWide(m_cpu.GetMetrics().name).c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        m_x,
        m_y,
        m_width,
        m_height,
        hParent,
        nullptr,
        GetModuleHandle(nullptr),
        this);

    if (!m_hwnd)
    {
        LOG_ERROR("CreateWindowEx failed: {%d}", GetLastError());
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    UpdateWindow(m_hwnd);

    return true;
}

void CpuGraphWindow::Show()
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
    }
}

void CpuGraphWindow::Update()
{
    if (m_hwnd)
    {
        InvalidateRect(m_hwnd, nullptr, FALSE);
        UpdateWindow(m_hwnd);
    }
}

void CpuGraphWindow::Close()
{
    if (m_hwnd)
        SendMessage(m_hwnd, WM_CLOSE, 0, 0);
}

LRESULT CALLBACK CpuGraphWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CpuGraphWindow *self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCT *>(lParam);
        self = static_cast<CpuGraphWindow *>(cs->lpCreateParams);

        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));

        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<CpuGraphWindow *>(
            GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CpuGraphWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
        CreateUIFont();
        UpdateWindowSize();
        return 0;
    case WM_CLOSE:
        DestroyWindow(m_hwnd);
        return 0;

    case WM_DESTROY:
        if (!SaveSettings())
            LOG_ERROR("[CPU] Failed to save settings");
        m_hwnd = nullptr;
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        SelectObject(hdc, m_hFont);

        if (m_FontHeight == 0)
        {
            TEXTMETRIC tm{};
            GetTextMetricsW(hdc, &tm);
            m_FontHeight = tm.tmHeight;
            m_FontAscent = tm.tmAscent;
        }

        RECT rc;
        GetClientRect(m_hwnd, &rc);

        static HBRUSH bgBrush = CreateSolidBrush(RGB(30, 30, 30));
        FillRect(hdc, &rc, bgBrush);

        DrawCoreBarGraph(hdc, rc);

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_DPICHANGED:
    {
        // UINT dpi = HIWORD(wParam);

        CreateUIFont();
        UpdateWindowSize();
        // RecreatePensBrushesIfNeeded(dpi);

        const RECT *rc = reinterpret_cast<RECT *>(lParam);
        SetWindowPos(m_hwnd, nullptr, rc->left, rc->top, rc->right - rc->left, rc->bottom - rc->top, SWP_NOZORDER | SWP_NOACTIVATE);

        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_EXITSIZEMOVE:
        InvalidateRect(m_hwnd, NULL, TRUE); // request repaint
        UpdateWindow(m_hwnd);               // force immediate WM_PAINT
        return 0;
    }

    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}

void CpuGraphWindow::DrawCoreBarGraph(HDC hdc, const RECT &rc)
{
    auto cpu = m_cpu.GetMetrics();

    const UINT dpi = GetDpiForWindow(m_hwnd);

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int barLeft = m_MarginLeftRight + m_LabelWidth + m_BarLeftMargin;
    const int maxBarWidth = rc.right - barLeft - m_MarginLeftRight;

    constexpr COLORREF dimColor = RGB(136, 136, 136);
    constexpr COLORREF textColor = RGB(225, 228, 234);
    constexpr COLORREF headerTextColor = RGB(56, 189, 248);

    int y = m_MarginTopBottom;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, m_hFont);

    // Header line.
    {
        wchar_t buf[32];

        int textY = y;

        SetTextColor(hdc, dimColor);

        TextOutW(hdc, m_MarginLeftRight, textY, L"CPU", 3);

        SIZE sz;
        GetTextExtentPoint32W(hdc, L"CPU", 3, &sz);

        int x = m_MarginLeftRight + sz.cx + m_BarLeftMargin;

        auto DrawHeaderPart = [&](const wchar_t *text, COLORREF color)
        {
            SetTextColor(hdc, color);
            TextOutW(hdc, x, textY, text, lstrlenW(text));

            SIZE s;
            GetTextExtentPoint32W(hdc, text, lstrlenW(text), &s);
            x += s.cx;
        };

        // Usage
        FormatUsage(buf, cpu.usage);
        DrawHeaderPart(buf, headerTextColor);
        DrawHeaderPart(L"% ", dimColor);

        // Temperature
        FormatTemperatureWithoutUnit(buf, cpu.dTemperature);
        DrawHeaderPart(buf, headerTextColor);
        DrawHeaderPart(L"°C ", dimColor);

        // Power
        swprintf_s(buf, L"%.1f", cpu.dPower);
        DrawHeaderPart(buf, headerTextColor);
        DrawHeaderPart(L" W", dimColor);

        y += m_FontHeight + m_Spacing / 2;

        // Horizontal separator.
        HPEN separatorPen = CreatePen(PS_SOLID, Scale(1), RGB(80, 80, 80));
        HPEN oldPen = (HPEN)SelectObject(hdc, separatorPen);

        MoveToEx(hdc, m_MarginLeftRight, y, nullptr);
        LineTo(hdc, rc.right - m_MarginLeftRight, y);

        SelectObject(hdc, oldPen);
        DeleteObject(separatorPen);

        y += m_Spacing;
    }

    for (size_t i = 0; i < cpu.cores.size(); ++i)
    {
        const RyzenCoreMetrics &core = cpu.cores[i];

        // Center the bar vertically within the text line.
        int barY = y + (m_FontHeight - m_BarHeight) / 2;

        RECT background{barLeft, barY, barLeft + maxBarWidth, barY + m_BarHeight};

        FillRect(hdc, &background, chartBgBrush);

        int barWidth = static_cast<int>(maxBarWidth * (core.dUsage / 100.0));

        RECT bar{barLeft, barY, barLeft + barWidth, barY + m_BarHeight};

        FillRect(hdc, &bar, barBrush);

        const int markerAreaWidth = rc.right - barLeft - m_MarginLeftRight - m_MarkerWidth;

        RECT startMarker{barLeft, barY, barLeft + m_MarkerWidth, barY + m_BarHeight};
        RECT middleMarker{barLeft + markerAreaWidth / 2, barY, barLeft + markerAreaWidth / 2 + m_MarkerWidth, barY + m_BarHeight};
        RECT endMarker{barLeft + markerAreaWidth, barY, barLeft + markerAreaWidth + m_MarkerWidth, barY + m_BarHeight};

        FillRect(hdc, &startMarker, markerBrush);
        FillRect(hdc, &middleMarker, markerBrush);
        FillRect(hdc, &endMarker, markerBrush);

        int textY = y;

        // Core number.
        wchar_t coreText[8];
        swprintf_s(coreText, L"%02zu", i);

        SetTextColor(hdc, dimColor);
        TextOutW(hdc, m_MarginLeftRight, textY, coreText, lstrlenW(coreText));

        SIZE sz;
        GetTextExtentPoint32W(hdc, coreText, lstrlenW(coreText), &sz);

        int x = m_MarginLeftRight + sz.cx;

        auto DrawPart = [&](const wchar_t *text, COLORREF color)
        {
            SetTextColor(hdc, color);
            TextOutW(hdc, x, textY, text, lstrlenW(text));

            SIZE s;
            GetTextExtentPoint32W(hdc, text, lstrlenW(text), &s);
            x += s.cx;
        };

        wchar_t buf[32];

        // Temperature
        FormatTemperatureWithoutUnit(buf, core.dTemperature);
        DrawPart(buf, textColor);
        DrawPart(L"°C ", dimColor);

        // Frequency
        FormatFrequency(buf, core.dCurrentFreq);
        DrawPart(buf, textColor);
        DrawPart(L" MHz", dimColor);

        // Usage
        FormatUsage(buf, core.dUsage);
        DrawPart(buf, textColor);
        DrawPart(L"%", dimColor);

        if (i + 1 != cpu.cores.size())
            y += m_FontHeight + m_Spacing;
        else
            y += m_FontHeight;
    }
}

void CpuGraphWindow::CreateUIFont()
{
    if (m_hFont)
    {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }

    const UINT dpi = GetDpiForWindow(m_hwnd);

    const int height = -MulDiv(16, dpi, USER_DEFAULT_SCREEN_DPI);

    m_hFont = CreateFontW(
        height,
        0, // Width
        0, // Escapement
        0, // Orientation
        FW_NORMAL,
        FALSE, // Italic
        FALSE, // Underline
        FALSE, // Strikeout
        DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY,
        FIXED_PITCH | FF_MODERN,
        L"Consolas");

    HDC hdc = GetDC(m_hwnd);
    HFONT oldFont = (HFONT)SelectObject(hdc, m_hFont);

    TEXTMETRIC tm{};
    GetTextMetrics(hdc, &tm);

    m_FontHeight = tm.tmHeight;
    m_FontAscent = tm.tmAscent;

    // Consolas is fixed-pitch, so one character defines the grid size.
    SIZE charSize{};
    GetTextExtentPoint32W(hdc, L"0", 1, &charSize);

    m_FontWidth = charSize.cx;

    SelectObject(hdc, oldFont);
    ReleaseDC(m_hwnd, hdc);

    //
    // Layout metrics.
    //
    // Reserve enough columns for:
    // "00 105°C  5200 MHz  100%"
    //
    constexpr int labelColumns = 24;

    m_LabelWidth = m_FontWidth * labelColumns;

    // One character of breathing room between text and graph.
    m_BarLeftMargin = m_FontWidth;

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    m_MarginTopBottom = Scale(c_MarginTopBottom);
    m_MarginLeftRight = Scale(c_MarginLeftRight);
    m_Spacing = Scale(c_LineSpace);
    m_OnePxScaled = Scale(1);
    m_BarHeight = m_FontAscent - m_OnePxScaled;
    m_MarkerWidth = Scale(2);
}

int CpuGraphWindow::GetRequiredClientHeight() const
{
    auto cpu = m_cpu.GetMetrics();

    UINT dpi = GetDpiForWindow(m_hwnd);

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int headerSpacing = m_Spacing / 2;
    const int separatorHeight = m_OnePxScaled;

    const int headerHeight = m_FontHeight + headerSpacing + separatorHeight + m_Spacing;

    int coreHeight = 0;

    if (!cpu.cores.empty())
    {
        coreHeight = static_cast<int>(cpu.cores.size()) * m_FontHeight;
        coreHeight += static_cast<int>(cpu.cores.size() - 1) * m_Spacing;
    }

    return m_MarginTopBottom + headerHeight + coreHeight + m_MarginTopBottom;
}

void CpuGraphWindow::UpdateWindowSize()
{
    int desiredClientHeight = GetRequiredClientHeight();

    RECT adjust{0, 0, 0, desiredClientHeight};

    AdjustWindowRectExForDpi(&adjust, GetWindowLong(m_hwnd, GWL_STYLE), FALSE, GetWindowLong(m_hwnd, GWL_EXSTYLE), GetDpiForWindow(m_hwnd));

    int newHeight = adjust.bottom - adjust.top;

    SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, newHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool CpuGraphWindow::SaveSettings()
{
    LOG_DEBUG("[CPU] Saving Settings");

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';

    wcscat_s(path, SETTINGS_FILE);

    RECT rc{};
    if (!GetWindowRect(m_hwnd, &rc))
        return false;

    int x = rc.left;
    int y = rc.top;
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    wchar_t buffer[32];

    swprintf_s(buffer, L"%d", x);
    WritePrivateProfileStringW(L"CPU", L"X", buffer, path);

    swprintf_s(buffer, L"%d", y);
    WritePrivateProfileStringW(L"CPU", L"Y", buffer, path);

    swprintf_s(buffer, L"%d", width);
    WritePrivateProfileStringW(L"CPU", L"Width", buffer, path);

    swprintf_s(buffer, L"%d", height);
    WritePrivateProfileStringW(L"CPU", L"Height", buffer, path);

    UINT dpi = GetDpiForWindow(m_hwnd);
    swprintf_s(buffer, L"%d", dpi);
    WritePrivateProfileStringW(L"CPU", L"DPI", buffer, path);

    return true;
}

bool CpuGraphWindow::LoadSettings()
{
    LOG_DEBUG("[CPU] Loading Settings");

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';

    wcscat_s(path, SETTINGS_FILE);

    m_x = GetPrivateProfileIntW(L"CPU", L"X", -1, path);
    m_y = GetPrivateProfileIntW(L"CPU", L"Y", -1, path);

    m_width = GetPrivateProfileIntW(L"CPU", L"Width", 400, path);
    m_height = GetPrivateProfileIntW(L"CPU", L"Height", 300, path);

    m_savedDpi = GetPrivateProfileIntW(L"CPU", L"DPI", 96, path);

    return true;
}