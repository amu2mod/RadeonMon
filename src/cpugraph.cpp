#include "radeonmon/cpugraph.hpp"
#include "radeonmon/logging.hpp"
#include "radeonmon/helpers.hpp"
#include "radeonmon/preferences.hpp"

#include <assert.h>
#include <windowsx.h>

#include <string>
#include <array>
#include <random>
#include <algorithm>

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

    bool sizeNotSet = false;

    if (!LoadSettings())
    {
        LOG_ERROR("[CPU] Failed to load settings");

        m_x = -1;
        m_y = -1;
        sizeNotSet = true;
        m_savedDpi = 96;
    }

    if (m_width == -1 || m_height == -1)
        sizeNotSet = true;

    RebuildBrushes();

    bool validDpi = m_savedDpi >= 72 && m_savedDpi <= 1000;

    RECT windowRect{m_x, m_y, m_x + max(m_width, 0), m_y + max(m_height, 0)};
    HMONITOR monitor = MonitorFromRect(&windowRect, MONITOR_DEFAULTTONULL);

    bool validPosition = (monitor != nullptr);

    UINT dpiX = 96;
    UINT dpiY = 96;

    if (validPosition && validDpi)
    {
        GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
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

    // init font sizes
    m_titleFontSize = GetScaledTitleFontSize();
    CreateUIFont(dpiX);

    int minWidth = GetMinRequiredClientWidth(dpiX);
    int minHeight = GetRequiredClientHeight(dpiX);

    if (sizeNotSet)
    {
        m_width = minWidth;
        m_height = minHeight;
    }
    else
    {
        bool validSize = m_width >= 100 && m_width <= 10000 && m_height >= 100 && m_height <= 10000;

        if (!validSize)
        {
            LOG_ERROR("[CPU] Invalid size settings, resetting window size");

            m_width = minWidth;
            m_height = minHeight;
        }
        else if (validDpi && validPosition)
        {
            m_width = MulDiv(m_width, dpiX, m_savedDpi);
            m_height = MulDiv(m_height, dpiY, m_savedDpi);
        }

        m_width = max(m_width, minWidth);
        m_height = max(m_height, minHeight);
    }

    const char *name = m_cpu.GetMetrics().name;
    LOG_DEBUG("[CPU] Setting title");
    m_title = Utf8ToWide(name);

    m_hwnd = CreateWindowEx(
        0,
        L"CpuGraphWindow",
        m_title.c_str(),
        WS_POPUP,
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

    m_userClose = false;

    const UINT actualDpi = m_dpi;

    if (actualDpi != dpiX)
    {
        CreateUIFont(actualDpi);

        minWidth = GetMinRequiredClientWidth(actualDpi);
        minHeight = GetRequiredClientHeight(actualDpi);

        m_width = max(MulDiv(m_width, actualDpi, dpiX), minWidth);
        m_height = max(MulDiv(m_height, actualDpi, dpiX), minHeight);

        SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, m_height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    UpdateLayoutRects();

    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
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

void CpuGraphWindow::Close()
{
    SendMessage(m_hwnd, WM_CLOSE, 0, 0);
}

void CpuGraphWindow::Update()
{
    if (m_hwnd)
    {
        if (m_showProcesses)
            InvalidateRect(m_hwnd, nullptr, FALSE);
        else
            InvalidateRect(m_hwnd, &m_GraphRc, FALSE);

        UpdateWindow(m_hwnd);
    }
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
        self = reinterpret_cast<CpuGraphWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
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
        m_dpi = GetDpiForWindow(m_hwnd);
        CreateUIFont(m_dpi);
        UpdateLayoutRects();
        UpdateWindowHeight();
        return 0;

    case WM_CLOSE:
        m_userClose = true;
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return 0;

    case WM_QUERYENDSESSION:
        return TRUE;

    case WM_ENDSESSION:
    {
        if (wParam) // Session is actually ending.
            SaveSettings();
        m_hwnd = nullptr;
        return 0;
    }

    case WM_DESTROY:
        if (!SaveSettings())
            LOG_ERROR("[CPU] Failed to save settings");
        m_hwnd = nullptr;
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        if (PtInRect(&m_processRC, pt))
        {
            OnProcessesClicked();
            return 0;
        }

        ReleaseCapture();
        SendMessage(m_hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
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
                OnResizeWindow(true);
                return 0;

            case VK_SUBTRACT:  // Ctrl + Numpad -
            case VK_OEM_MINUS: // Ctrl + main keyboard - (QWERTY)
            case '6':          // Ctrl + main keyboard - (AZERTY)
                OnResizeWindow(false);
                return 0;
            }
        }

        break;
    }

    case WM_SIZE:
    {
        LOG_WM("[CPU-W] WM_SIZE");
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        UpdateLayoutRects();
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);

        SelectObject(hdc, m_titleFont);

        if (m_FontHeight == 0)
        {
            TEXTMETRIC tm{};
            GetTextMetricsW(hdc, &tm);
            m_FontHeight = tm.tmHeight;
            m_FontAscent = tm.tmAscent;
        }

        RECT rc;
        GetClientRect(m_hwnd, &rc);

        const UINT dpi = m_dpi;

        auto Scale = [dpi](int value)
        {
            return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
        };

        const int borderWidth = Scale(c_borderWidth);
        const int titlePadding = Scale(c_TitlePaddingTopBottom);
        const int titleBarHeight = m_FontHeight + titlePadding * 2;

        const auto &colors = Theme::Get(m_currentTheme);

        // The region Windows is asking us to repaint.
        const RECT &paintRc = ps.rcPaint;

        auto Intersects = [](const RECT &a, const RECT &b)
        {
            RECT intersection;
            return IntersectRect(&intersection, &a, &b);
        };

        // Background.
        // Only paint the portion that is actually invalid.
        {
            RECT dirtyRc;
            if (IntersectRect(&dirtyRc, &rc, &paintRc))
            {
                FillRect(hdc, &dirtyRc, bgBrush);
            }
        }

        // Borders.
        RECT topEdge{rc.left, rc.top, rc.right, rc.top + borderWidth};
        RECT bottomEdge{rc.left, rc.bottom - borderWidth, rc.right, rc.bottom};
        RECT leftEdge{rc.left, rc.top, rc.left + borderWidth, rc.bottom};
        RECT rightEdge{rc.right - borderWidth, rc.top, rc.right, rc.bottom};

        if (Intersects(topEdge, paintRc))
            FillRect(hdc, &topEdge, borderBrush);

        if (Intersects(bottomEdge, paintRc))
            FillRect(hdc, &bottomEdge, borderBrush);

        if (Intersects(leftEdge, paintRc))
            FillRect(hdc, &leftEdge, borderBrush);

        if (Intersects(rightEdge, paintRc))
            FillRect(hdc, &rightEdge, borderBrush);

        // Title bar.
        RECT titleRc{borderWidth, borderWidth, rc.right - borderWidth, borderWidth + titleBarHeight};

        if (Intersects(titleRc, paintRc))
        {
            FillRect(hdc, &titleRc, borderBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, colors.text);

            const int titleTextY = borderWidth + titlePadding;

            TextOutW(hdc, m_MarginLeftRight, titleTextY, m_title.c_str(), static_cast<int>(m_title.size()));
        }

        // Graph area starts below the title bar.
        RECT graphRc = rc;

        graphRc.left += borderWidth;
        graphRc.right -= borderWidth;
        graphRc.top = rc.top + borderWidth + titleBarHeight;
        graphRc.bottom = rc.bottom - borderWidth;

        // Only invoke the graph drawing code when the graph was invalidated.
        if (Intersects(graphRc, paintRc))
        {
            SelectObject(hdc, m_hFont);
            DrawCoreBarGraph(hdc, graphRc);
        }

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_DPICHANGED:
    {
        LOG_WM("[CPU-W] WM_DPICHANGED");
        m_dpi = HIWORD(wParam);
        CreateUIFont(m_dpi);
        UpdateLayoutRects();
        RECT *suggestedRect = reinterpret_cast<RECT *>(lParam);
        SetWindowPos(m_hwnd, nullptr, suggestedRect->left, suggestedRect->top, suggestedRect->right - suggestedRect->left, suggestedRect->bottom - suggestedRect->top, SWP_NOZORDER | SWP_NOACTIVATE);
        m_pendingDpiResize = true;
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_ENTERSIZEMOVE:
    {
        LOG_WM("[CPU-W] WM_ENTERSIZEMOVE");
        m_inSizeMove = true;
        break;
    }

    case WM_WINDOWPOSCHANGING:
    {
        auto *wp = reinterpret_cast<WINDOWPOS *>(lParam);

        // prevents the window going above the top edge
        if (m_inSizeMove)
        {
            HMONITOR monitor = MonitorFromWindow(m_hwnd, MONITOR_DEFAULTTONEAREST);
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

    case WM_EXITSIZEMOVE:
    {
        LOG_WM("[CPU-W] WM_EXITSIZEMOVE");

        m_inSizeMove = false;

        if (m_pendingDpiResize)
        {
            m_pendingDpiResize = false;
            RECT rc;
            GetWindowRect(m_hwnd, &rc);
            SetWindowPos(m_hwnd, nullptr, rc.left, rc.top, rc.right - rc.left, GetRequiredClientHeight(m_dpi), SWP_NOZORDER | SWP_NOACTIVATE);
        }
        else
        {
            // LogRect("[CPU-W] m_GraphRc", m_GraphRc);
            InvalidateRect(m_hwnd, &m_GraphRc, FALSE); // request repaint
        }
        break;
    }

    case WM_CONTEXTMENU:
    {
        HMENU menu = CreatePopupMenu();

        // Theme submenu
        HMENU themeMenu = CreatePopupMenu();
        AppendMenu(themeMenu, MF_STRING | (m_currentTheme == Theme::Type::SkyBlue ? MF_CHECKED | MF_DISABLED : 0), 2, L"Sky Blue (Intel)");
        AppendMenu(themeMenu, MF_STRING | (m_currentTheme == Theme::Type::RyzenOrange ? MF_CHECKED | MF_DISABLED : 0), 3, L"Orange (Ryzen)");

        AppendMenu(menu, MF_POPUP, (UINT_PTR)themeMenu, L"Theme");

        // Process Limit submenu
        HMENU processLimitMenu = CreatePopupMenu();
        AppendMenu(processLimitMenu, MF_STRING | (m_maxProcess == 3 ? MF_CHECKED : 0), 4, L"3");
        AppendMenu(processLimitMenu, MF_STRING | (m_maxProcess == 4 ? MF_CHECKED : 0), 5, L"4");
        AppendMenu(processLimitMenu, MF_STRING | (m_maxProcess == 5 ? MF_CHECKED : 0), 6, L"5");

        AppendMenu(menu, MF_POPUP, (UINT_PTR)processLimitMenu, L"Process Limit");

        AppendMenu(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenu(menu, MF_STRING, 1, L"Close");

        POINT pt;
        pt.x = GET_X_LPARAM(lParam);
        pt.y = GET_Y_LPARAM(lParam);

        SetForegroundWindow(m_hwnd);

        int cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);

        DestroyMenu(menu);

        switch (cmd)
        {
        case 1:
            SendMessage(m_hwnd, WM_CLOSE, 0, 0);
            break;

        case 2:
            m_currentTheme = Theme::Type::SkyBlue;
            RebuildBrushes();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;

        case 3:
            m_currentTheme = Theme::Type::RyzenOrange;
            RebuildBrushes();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;

        case 4:
            m_maxProcess = 3;
            UpdateWindowHeight();
            break;

        case 5:
            m_maxProcess = 4;
            UpdateWindowHeight();
            break;

        case 6:
            m_maxProcess = 5;
            UpdateWindowHeight();
            break;
        }

        return 0;
    }

    // resizable only horizontally
    case WM_NCHITTEST:
    {
        POINT pt = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        RECT rc;
        GetWindowRect(m_hwnd, &rc);

        const int resizeBorder = MulDiv(6, m_dpi, USER_DEFAULT_SCREEN_DPI);

        if (pt.x >= rc.left && pt.x < rc.left + resizeBorder)
            return HTLEFT;

        if (pt.x >= rc.right - resizeBorder && pt.x < rc.right)
            return HTRIGHT;

        return HTCLIENT;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO *mmi = reinterpret_cast<MINMAXINFO *>(lParam);
        mmi->ptMinTrackSize.x = GetMinRequiredClientWidth(m_dpi);
        return 0;
    }

    case WM_SETCURSOR:
    {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(m_hwnd, &pt);

        if (PtInRect(&m_processRC, pt))
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }

        break;
    }
    }

    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}

void CpuGraphWindow::DrawCoreBarGraph(HDC hdc, const RECT &rc)
{
    auto cpu = m_cpu.GetMetrics();

    const UINT dpi = m_dpi;

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int barLeft = m_MarginLeftRight + m_LabelWidth + m_BarLeftMargin;
    const int maxBarWidth = rc.right - barLeft - m_MarginLeftRight;

    int y = rc.top + m_MarginTopBottom;

    SetBkMode(hdc, TRANSPARENT);
    SelectObject(hdc, m_hFont);

    const auto &colors = Theme::Get(m_currentTheme);

    constexpr std::array<const wchar_t *, 5> processNames{
        L"process 1",
        L"process 2",
        L"process 3",
        L"process 4",
        L"process 5"};

    // Header line.
    {
        wchar_t buf[32];

        int textY = y;

        SetTextColor(hdc, colors.dim);

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
        DrawHeaderPart(buf, colors.header);
        DrawHeaderPart(L"% ", colors.dim);

        // Temperature
        FormatTemperatureWithoutUnit(buf, cpu.dTemperature);
        DrawHeaderPart(buf, colors.header);
        DrawHeaderPart(L"°C ", colors.dim);

        // Power
        swprintf_s(buf, L"%.1f", cpu.dPower);
        DrawHeaderPart(buf, colors.header);
        DrawHeaderPart(L" W", colors.dim);

        y += m_FontHeight + m_Spacing / 2;

        // Horizontal separator.
        HPEN separatorPen = CreatePen(PS_SOLID, Scale(1), colors.separator);
        HPEN oldPen = (HPEN)SelectObject(hdc, separatorPen);

        // Processes show/hide control.
        {
            constexpr wchar_t showText[] = L"Show processes";
            constexpr wchar_t hideText[] = L"Hide processes";

            SetTextColor(hdc, colors.dim);

            auto &text = m_showProcesses ? hideText : showText;

            HFONT oldFont = (HFONT)SelectObject(hdc, m_titleFont);
            const int text_Y = m_processRC.top + (m_processRC.bottom - m_processRC.top - m_TitleFontHeight) / 2;

            TextOutW(hdc, m_processRC.left, text_Y, text, ARRAYSIZE(text) - 1);
            SelectObject(hdc, oldFont);
        }

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

        SetTextColor(hdc, colors.dim);
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
        DrawPart(buf, colors.text);
        DrawPart(L"°C ", colors.dim);

        // Frequency
        FormatFrequency(buf, core.dCurrentFreq);
        DrawPart(buf, colors.text);
        DrawPart(L" MHz", colors.dim);

        // Usage
        FormatUsage(buf, core.dUsage);
        DrawPart(buf, colors.text);
        DrawPart(L"%", colors.dim);

        if (i + 1 != cpu.cores.size())
            y += m_FontHeight + m_Spacing;
        else
            y += m_FontHeight;
    }

    // Horizontal separator.
    if (m_showProcesses)
    {
        // Spacing above the separator.
        y += m_Spacing;

        HPEN separatorPen =
            CreatePen(PS_SOLID, Scale(1), colors.separator);

        HPEN oldPen =
            static_cast<HPEN>(SelectObject(hdc, separatorPen));

        MoveToEx(hdc, m_MarginLeftRight, y, nullptr);
        LineTo(hdc, rc.right - m_MarginLeftRight, y);

        SelectObject(hdc, oldPen);
        DeleteObject(separatorPen);

        // Spacing below the separator.
        y += m_Spacing;
    }

    // ------------------------------------------------------------
    // Processes.
    // ------------------------------------------------------------
    if (m_showProcesses)
    {
        const int processBarLeft = m_MarginLeftRight;
        const int processBarRight = rc.right - m_MarginLeftRight;
        const int processBarWidth = processBarRight - processBarLeft;
        const int processBarHeight = Scale(2);
        const int processBarSpacing = Scale(1);
        const int processRowHeight = m_FontHeight + processBarSpacing + processBarHeight;

        const auto &processList = m_processWatcher.GetProcessList();
        const size_t processCount = std::min<size_t>(m_maxProcess, processList.size());

        for (size_t i = 0; i < processCount; ++i)
        {
            const auto &process = processList[i];

            // ----------------------------------------------------
            // Process name.
            // ----------------------------------------------------
            SetTextColor(hdc, colors.text);

            std::string name = process.name;

            int len = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
            std::wstring wname(len, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wname.data(), len);

            TextOutW(hdc, m_MarginLeftRight, y, wname.c_str(), static_cast<int>(wname.size()) - 1);

            // ----------------------------------------------------
            // CPU usage text.
            // ----------------------------------------------------
            wchar_t usageText[16];

            swprintf_s(usageText, L"%0.1f%%", process.cpu);

            SIZE usageSize{};

            GetTextExtentPoint32W(hdc, usageText, lstrlenW(usageText), &usageSize);

            // ----------------------------------------------------
            // RAM usage text.
            // ----------------------------------------------------
            wchar_t ramText[32];

            FormatRam(process.ramUsage, ramText, 32);

            SIZE ramSize{};
            GetTextExtentPoint32W(hdc, ramText, lstrlenW(ramText), &ramSize);

            // ----------------------------------------------------
            // Right-aligned CPU percentage.
            // ----------------------------------------------------
            const int usageX = processBarRight - usageSize.cx;

            SetTextColor(hdc, colors.header);

            TextOutW(hdc, usageX, y, usageText, lstrlenW(usageText));

            // ----------------------------------------------------
            // Right-aligned RAM, immediately before CPU.
            // ----------------------------------------------------
            constexpr int ramCpuSpacing = 10;

            const int ramX = usageX - ramCpuSpacing - ramSize.cx;

            SetTextColor(hdc, colors.dim);

            TextOutW(hdc, ramX, y, ramText, lstrlenW(ramText));

            // ----------------------------------------------------
            // Thin background bar.
            // ----------------------------------------------------
            const int barY = y + m_FontHeight + processBarSpacing;

            RECT background{processBarLeft, barY, processBarRight, barY + processBarHeight};

            FillRect(hdc, &background, chartBgBrush);

            // ----------------------------------------------------
            // Usage bar.
            // ----------------------------------------------------
            const int barWidth = static_cast<int>(processBarWidth * process.cpu / 100.0);

            RECT bar{processBarLeft, barY, processBarLeft + barWidth, barY + processBarHeight};

            FillRect(hdc, &bar, barBrush);

            // ----------------------------------------------------
            // Space before next process.
            // ----------------------------------------------------
            if (i + 1 != processCount)
                y += processRowHeight + m_Spacing;
            else
                y += processRowHeight;
        }
    }
}

void CpuGraphWindow::CreateUIFont(UINT dpi)
{
    if (m_hFont)
    {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }

    if (m_titleFont)
    {
        DeleteObject(m_titleFont);
        m_titleFont = nullptr;
    }

    const int height = -MulDiv(m_fontSize, dpi, USER_DEFAULT_SCREEN_DPI);
    const int titleHeight = -MulDiv(m_titleFontSize, dpi, USER_DEFAULT_SCREEN_DPI);

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

    m_titleFont = CreateFontW(
        titleHeight,
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

    HDC hdc = m_hwnd ? GetDC(m_hwnd) : GetDC(nullptr);

    HFONT oldFont = (HFONT)SelectObject(hdc, m_hFont);

    TEXTMETRIC tm{};
    GetTextMetrics(hdc, &tm);

    m_FontHeight = tm.tmHeight;
    m_FontAscent = tm.tmAscent;

    // Consolas is fixed-pitch, so one character defines the grid size.
    SIZE charSize{};
    GetTextExtentPoint32W(hdc, L"0", 1, &charSize);

    m_FontWidth = charSize.cx;

    // Title font metrics, used to size the title bar itself.
    SelectObject(hdc, m_titleFont);

    TEXTMETRIC titleTm{};
    GetTextMetrics(hdc, &titleTm);

    m_TitleFontHeight = titleTm.tmHeight;
    m_TitleFontAscent = titleTm.tmAscent;

    SIZE titleCharSize{};
    GetTextExtentPoint32W(hdc, L"0", 1, &titleCharSize);

    m_TitleFontWidth = titleCharSize.cx;

    SelectObject(hdc, oldFont);
    ReleaseDC(m_hwnd, hdc); // ReleaseDC ignores a null HWND correctly

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

int CpuGraphWindow::GetRequiredClientHeight(UINT dpi) const
{
    auto cpu = m_cpu.GetMetrics();

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int borderWidth = Scale(c_borderWidth);
    const int titlePadding = Scale(c_TitlePaddingTopBottom);
    const int titleBarHeight =
        m_FontHeight + titlePadding * 2;

    const int separatorHeight = m_OnePxScaled;

    int graphHeight = m_MarginTopBottom;

    // Header.
    graphHeight += m_FontHeight;
    graphHeight += m_Spacing / 2;
    graphHeight += separatorHeight;
    graphHeight += m_Spacing;

    // CPU cores.
    if (!cpu.cores.empty())
    {
        graphHeight += static_cast<int>(cpu.cores.size()) * m_FontHeight;
        graphHeight += static_cast<int>(cpu.cores.size() - 1) * m_Spacing;
    }

    if (m_showProcesses)
    {
        const int processBarSpacing = Scale(1);
        const int processBarHeight = Scale(2);
        const int processRowHeight = m_FontHeight + processBarSpacing + processBarHeight;

        graphHeight += m_Spacing;
        graphHeight += separatorHeight;
        graphHeight += m_Spacing;
        graphHeight += m_maxProcess * processRowHeight;
        graphHeight += (m_maxProcess - 1) * m_Spacing;
    }

    // Bottom margin.
    graphHeight += m_MarginTopBottom;

    return borderWidth + titleBarHeight + graphHeight + borderWidth;
}

void CpuGraphWindow::UpdateWindowHeight()
{
    int desiredClientHeight = GetRequiredClientHeight(m_dpi);
    SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, desiredClientHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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

    UINT dpi = m_dpi;
    swprintf_s(buffer, L"%d", dpi);
    WritePrivateProfileStringW(L"CPU", L"DPI", buffer, path);

    swprintf_s(buffer, L"%d", m_userClose ? 0 : 1);
    WritePrivateProfileStringW(L"Window", L"CpuGraphActive", buffer, path);

    swprintf_s(buffer, L"%d", m_currentTheme);
    WritePrivateProfileStringW(L"CPU", L"Theme", buffer, path);

    swprintf_s(buffer, L"%d", m_fontSize);
    WritePrivateProfileStringW(L"CPU", L"FontSize", buffer, path);

    swprintf_s(buffer, L"%d", m_showProcesses);
    WritePrivateProfileStringW(L"CPU", L"ShowProcesses", buffer, path);

    swprintf_s(buffer, L"%d", m_maxProcess);
    WritePrivateProfileStringW(L"CPU", L"MaxProcess", buffer, path);

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

    m_width = GetPrivateProfileIntW(L"CPU", L"Width", -1, path);
    m_height = GetPrivateProfileIntW(L"CPU", L"Height", -1, path);

    m_savedDpi = GetPrivateProfileIntW(L"CPU", L"DPI", 96, path);

    const auto value = GetPrivateProfileIntW(L"CPU", L"Theme", 0, path);
    if (value >= 0 && value < static_cast<UINT>(Theme::Type::COUNT))
        m_currentTheme = static_cast<Theme::Type>(value);
    else
        m_currentTheme = Theme::Type::SkyBlue;

    m_fontSize = GetPrivateProfileIntW(L"CPU", L"FontSize", c_defaultFontSize, path);

    m_showProcesses = GetPrivateProfileIntW(L"CPU", L"ShowProcesses", 0, path) != 0;

    m_maxProcess = std::clamp(static_cast<int>(GetPrivateProfileIntW(L"CPU", L"MaxProcess", 5, path)), 3, 5);

    return true;
}

int CpuGraphWindow::GetMinRequiredClientWidth(UINT dpi) const
{
    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int borderWidth = Scale(c_borderWidth);
    const int minBarWidth = Scale(c_MinBarGraphWidth);
    const int processPadding = Scale(c_ProcessPadding);

    constexpr int processTextLength = ARRAYSIZE(L"Show processes") - 1;

    const int processWidth = m_TitleFontWidth * processTextLength + processPadding * 2;
    const int graphWidth = m_MarginLeftRight + m_LabelWidth + m_BarLeftMargin + minBarWidth + m_MarginLeftRight;

    // Header width:
    // CPU  <header values>       Show processes
    const int cpuHeaderWidth = m_MarginLeftRight + m_FontWidth * 3 + m_BarLeftMargin + m_LabelWidth;
    const int headerWidth = cpuHeaderWidth + m_MarginLeftRight + processWidth + m_MarginLeftRight;

    return borderWidth + (std::max)(graphWidth, headerWidth) + borderWidth;
}

void CpuGraphWindow::UpdateLayoutRects()
{
    LOG_DEBUG("[CPU-W] UpdateLayoutRects");
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    const UINT dpi = m_dpi;

    auto Scale = [dpi](int value)
    {
        return MulDiv(value, dpi, USER_DEFAULT_SCREEN_DPI);
    };

    const int borderWidth = Scale(c_borderWidth);
    const int titlePadding = Scale(c_TitlePaddingTopBottom);
    const int titleBarHeight = m_FontHeight + titlePadding * 2;

    m_GraphRc = rc;
    m_GraphRc.left += borderWidth;
    m_GraphRc.right -= borderWidth;
    m_GraphRc.top = rc.top + borderWidth + titleBarHeight;
    m_GraphRc.bottom -= borderWidth;

    // Header "Processes" control.
    const int processPadding = Scale(c_ProcessPadding);
    constexpr wchar_t processText[] = L"Show processes";
    const int processWidth = m_TitleFontWidth * (ARRAYSIZE(processText) - 1) + processPadding * 2;
    const int headerY = m_GraphRc.top + m_MarginTopBottom;

    m_processRC = {m_GraphRc.right - m_MarginLeftRight - processWidth, headerY - processPadding, m_GraphRc.right - m_MarginLeftRight, headerY + m_TitleFontHeight + processPadding};
}

void CpuGraphWindow::OnResizeWindow(bool grow)
{
    int oldFontSize = m_fontSize;

    RECT rc{};
    GetClientRect(m_hwnd, &rc);
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    float oldRatio = static_cast<float>(width) / height;

    if (grow)
        m_fontSize = min(m_fontSize + 2, c_MaxFontSize);
    else
        m_fontSize = max(m_fontSize - 2, c_MinFontSize);

    // Already at min/max
    if (m_fontSize == oldFontSize)
        return;

    m_titleFontSize = GetScaledTitleFontSize();

    const UINT dpi = m_dpi;

    CreateUIFont(dpi);
    UpdateLayoutRects();

    int desiredClientHeight = GetRequiredClientHeight(dpi);

    int minWidth = GetMinRequiredClientWidth(dpi);
    int newWidth = static_cast<int>(desiredClientHeight * oldRatio);

    SetWindowPos(m_hwnd, nullptr, 0, 0, max(minWidth, newWidth), desiredClientHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    InvalidateRect(m_hwnd, nullptr, TRUE);

    // LOG_DEBUG("new window: %dx%d, font@%dpx", rc.right - rc.left, rc.bottom - rc.top, g_fontSize);
}

void CpuGraphWindow::OnProcessesClicked()
{
    LOG_DEBUG("Toggling Processes");
    m_showProcesses = !m_showProcesses;

    UINT dpi = m_dpi;

    int newHeight = GetRequiredClientHeight(dpi);

    SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, newHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    InvalidateRect(m_hwnd, &m_processRC, FALSE);
}
