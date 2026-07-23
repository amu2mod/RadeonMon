#include "radeonmon/cpugraph.hpp"
#include "radeonmon/logging.hpp"
#include "radeonmon/helpers.hpp"

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

    POINT cursor;
    GetCursorPos(&cursor);

    UINT dpiX = 96;
    UINT dpiY = 96;

    HMONITOR monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST);

    GetDpiForMonitor(
        monitor,
        MDT_EFFECTIVE_DPI,
        &dpiX,
        &dpiY);

    int width = MulDiv(500, dpiX, 96);
    int x = MulDiv(60, dpiX, 96);
    int y = MulDiv(15, dpiX, 96);

    m_hwnd = CreateWindowEx(
        WS_EX_TOPMOST,
        L"CpuGraphWindow",
        Utf8ToWide(m_cpu.GetMetrics().name).c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
        cursor.x - x,
        cursor.y - y,
        width,
        300,
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

    const int margin = Scale(20);
    const int labelWidth = Scale(220);
    const int barGap = Scale(10);

    const int barHeight = Scale(15);
    const int spacing = Scale(8);

    const int barLeft = margin + labelWidth + barGap;
    const int maxBarWidth = rc.right - barLeft - margin;

    constexpr COLORREF dimColor = RGB(136, 136, 136);
    constexpr COLORREF textColor = RGB(225, 228, 234);
    constexpr COLORREF headerTextColor = RGB(56, 189, 248);

    int y = margin;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, m_hFont);

    // Header line.
    {
        wchar_t buf[32];

        int textY = y;

        SetTextColor(hdc, dimColor);

        TextOutW(hdc, margin, textY, L"CPU", 3);

        SIZE sz;
        GetTextExtentPoint32W(hdc, L"CPU", 3, &sz);

        int x = margin + sz.cx + Scale(10);

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

        y += m_FontHeight + spacing / 2;

        // Horizontal separator.
        HPEN separatorPen = CreatePen(PS_SOLID, Scale(1), RGB(80, 80, 80));
        HPEN oldPen = (HPEN)SelectObject(hdc, separatorPen);

        MoveToEx(hdc, margin, y, nullptr);
        LineTo(hdc, rc.right - margin, y);

        SelectObject(hdc, oldPen);
        DeleteObject(separatorPen);

        y += spacing;
    }

    for (size_t i = 0; i < cpu.cores.size(); ++i)
    {
        const RyzenCoreMetrics &core = cpu.cores[i];

        // Center the bar vertically within the text line.
        int barY = y + (m_FontHeight - barHeight) / 2;

        RECT background{barLeft, barY, barLeft + maxBarWidth, barY + barHeight};

        FillRect(hdc, &background, chartBgBrush);

        int barWidth = static_cast<int>(maxBarWidth * (core.dUsage / 100.0));

        RECT bar{barLeft, barY, barLeft + barWidth, barY + barHeight};

        FillRect(hdc, &bar, barBrush);

        const int markerWidth = Scale(2);

        const int maxBarWidth = rc.right - barLeft - margin - markerWidth;

        RECT startMarker{barLeft, barY, barLeft + markerWidth, barY + barHeight};
        RECT middleMarker{barLeft + maxBarWidth / 2, barY, barLeft + maxBarWidth / 2 + markerWidth, barY + barHeight};
        RECT endMarker{barLeft + maxBarWidth, barY, barLeft + maxBarWidth + markerWidth, barY + barHeight};

        FillRect(hdc, &startMarker, markerBrush);
        FillRect(hdc, &middleMarker, markerBrush);
        FillRect(hdc, &endMarker, markerBrush);

        int textY = y;

        // Core number.
        wchar_t coreText[8];
        swprintf_s(coreText, L"%02zu", i);

        SetTextColor(hdc, dimColor);
        TextOutW(hdc, margin, textY, coreText, lstrlenW(coreText));

        SIZE sz;
        GetTextExtentPoint32W(hdc, coreText, lstrlenW(coreText), &sz);

        int x = margin + sz.cx;

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

        y += m_FontHeight + spacing;
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

    // 16 px at 96 DPI, scaled proportionally.
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

    TEXTMETRIC tm;
    GetTextMetrics(hdc, &tm);
    m_FontHeight = tm.tmHeight;

    SelectObject(hdc, oldFont);
    ReleaseDC(m_hwnd, hdc);
}

int CpuGraphWindow::GetRequiredClientHeight() const
{
    auto cpu = m_cpu.GetMetrics();

    UINT dpi = GetDpiForWindow(m_hwnd);

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int margin = Scale(20);
    const int spacing = Scale(8);

    const int headerSpacing = spacing / 2;
    const int separatorHeight = Scale(1);

    const int headerHeight = m_FontHeight + headerSpacing + separatorHeight + spacing;

    const int coreHeight = static_cast<int>(cpu.cores.size()) * (m_FontHeight + spacing);

    return margin + headerHeight + coreHeight + margin;
}

void CpuGraphWindow::UpdateWindowSize()
{
    RECT rc{};
    GetWindowRect(m_hwnd, &rc);

    RECT client{};
    GetClientRect(m_hwnd, &client);

    int desiredClientHeight = GetRequiredClientHeight();

    RECT adjust{0, 0, client.right, desiredClientHeight};

    AdjustWindowRectExForDpi(&adjust, GetWindowLong(m_hwnd, GWL_STYLE), FALSE, GetWindowLong(m_hwnd, GWL_EXSTYLE), GetDpiForWindow(m_hwnd));

    SetWindowPos(m_hwnd, nullptr, 0, 0, adjust.right - adjust.left, adjust.bottom - adjust.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}