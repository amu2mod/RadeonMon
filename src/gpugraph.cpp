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

bool GpuGraphWindow::Create(HWND hParent)
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
        return true;
    }

    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&m_gdiplusToken, &gdiplusStartupInput, NULL);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = GpuGraphWindow::WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = L"GpuGraphWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassEx(&wc);

    bool sizeNotSet = false;

    if (!LoadSettings())
    {
        LOG_ERROR("[GPU] Failed to load settings");

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
        LOG_ERROR("[GPU] Invalid position settings, resetting window position");

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
    UpdateLayoutRects();

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
            LOG_ERROR("[GPU] Invalid size settings, resetting window size");

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

    m_title = m_adlx.GetGpuInfo().name;

    m_hwnd = CreateWindowEx(
        0,
        L"GpuGraphWindow",
        nullptr,
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

void GpuGraphWindow::Show()
{
    if (m_hwnd)
    {
        ShowWindow(m_hwnd, SW_SHOW);
        SetForegroundWindow(m_hwnd);
    }
}

void GpuGraphWindow::Close()
{
    SendMessage(m_hwnd, WM_CLOSE, 0, 0);
}

void GpuGraphWindow::Update()
{
    if (m_hwnd)
    {
        InvalidateRect(m_hwnd, &m_ContentRc, FALSE);
        UpdateWindow(m_hwnd);
    }
}

LRESULT CALLBACK GpuGraphWindow::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    GpuGraphWindow *self = nullptr;

    if (msg == WM_NCCREATE)
    {
        auto cs = reinterpret_cast<CREATESTRUCT *>(lParam);
        self = static_cast<GpuGraphWindow *>(cs->lpCreateParams);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->m_hwnd = hwnd;
    }
    else
    {
        self = reinterpret_cast<GpuGraphWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    }

    if (self)
        return self->HandleMessage(msg, wParam, lParam);

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT GpuGraphWindow::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        m_dpi = GetDpiForWindow(m_hwnd);
        CreateUIFont(m_dpi);
        UpdateLayoutRects();
        UpdateWindowHeight();
        HDC hdc = GetDC(m_hwnd);

        const auto &colors = GpuTheme::Get(m_currentTheme);
        m_ring.Init(hdc, m_ringRc, m_ringFont, L"%", m_adlx.Get().usage.max, colors.bar, colors.barBackground, colors.windowBackground, colors.text);
        return 0;
    }

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
            LOG_ERROR("[GPU] Failed to save settings");
        m_hwnd = nullptr;
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};

        // if (PtInRect(&m_processRC, pt))
        // {
        //     OnProcessesClicked();
        //     return 0;
        // }

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

        // TODO: move to private members
        const int titlePadding = Scale(c_TitlePaddingTopBottom, m_dpi);
        const int titleBarHeight = m_FontHeight + titlePadding * 2;

        const auto &colors = GpuTheme::Get(m_currentTheme);

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
        RECT topEdge{rc.left, rc.top, rc.right, rc.top + m_borderSize};
        RECT bottomEdge{rc.left, rc.bottom - m_borderSize, rc.right, rc.bottom};
        RECT leftEdge{rc.left, rc.top, rc.left + m_borderSize, rc.bottom};
        RECT rightEdge{rc.right - m_borderSize, rc.top, rc.right, rc.bottom};

        if (Intersects(topEdge, paintRc))
            FillRect(hdc, &topEdge, borderBrush);

        if (Intersects(bottomEdge, paintRc))
            FillRect(hdc, &bottomEdge, borderBrush);

        if (Intersects(leftEdge, paintRc))
            FillRect(hdc, &leftEdge, borderBrush);

        if (Intersects(rightEdge, paintRc))
            FillRect(hdc, &rightEdge, borderBrush);

        // Title bar.
        RECT titleRc{m_borderSize, m_borderSize, rc.right - m_borderSize, m_borderSize + titleBarHeight};

        if (Intersects(titleRc, paintRc))
        {
            FillRect(hdc, &titleRc, borderBrush);

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, colors.text);

            const int titleTextY = m_borderSize + titlePadding;

            TextOutW(hdc, m_MarginLeftRight, titleTextY, m_title.c_str(), static_cast<int>(m_title.size()));
        }

        // Graph area starts below the title bar.
        // RECT graphRc = rc;

        // graphRc.left += m_borderSize;
        // graphRc.right -= m_borderSize;
        // graphRc.top = rc.top + borderWidth + titleBarHeight;
        // graphRc.bottom = rc.bottom - borderWidth;

        // Only invoke the graph drawing code when the graph was invalidated.
        if (Intersects(m_BodyRc, paintRc))
        {
            const auto snapshot = m_adlx.Get();

#ifdef GPUGRAPHRECT
            HBRUSH brush = CreateSolidBrush(rgb(216, 190, 190));
            FillRect(hdc, &m_Column1Rc, brush);
            DeleteObject(brush);
#endif

            SelectObject(hdc, m_hFont);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, colors.text);

            int x = m_BodyRc.left;
            int y = m_BodyRc.top;
            const int lineHeight = m_FontHeight;

            wchar_t text[128];

            auto DrawTextLine = [&](GPU_CAPS cap, const wchar_t *name, double value, const wchar_t *unit)
            {
                if (m_selected == cap)
                    SetTextColor(hdc, colors.text);
                else
                    SetTextColor(hdc, colors.dim);

                const int rowY = y;

                swprintf_s(text, L"%-18s: %.1f %s", name, value, unit); // TODO: ultra fast formatter
                TextOutW(hdc, x, rowY, text, static_cast<int>(wcslen(text)));

                y += lineHeight;
            };

            auto DrawTextLineInt = [&](GPU_CAPS cap, const wchar_t *name, int value, const wchar_t *unit)
            {
                if (m_selected == cap)
                    SetTextColor(hdc, colors.text);
                else
                    SetTextColor(hdc, colors.dim);

                const int rowY = y;

                swprintf_s(text, L"%-18s: %d %s", name, value, unit); // TODO: ultra fast formatter
                TextOutW(hdc, x, rowY, text, static_cast<int>(wcslen(text)));

                y += lineHeight;
            };

            if (snapshot.usage.isSupported)
                DrawTextLine(GPU_CAP_USAGE, L"GPU Usage", snapshot.usage.value, L"%");

            if (snapshot.clockSpeed.isSupported)
                DrawTextLineInt(GPU_CAP_CLOCK, L"GPU Clock", snapshot.clockSpeed.value, L"MHz");

            if (snapshot.vramClockSpeed.isSupported)
                DrawTextLineInt(GPU_CAP_VRAM_CLOCK, L"VRAM Clock", snapshot.vramClockSpeed.value, L"MHz");

            if (snapshot.temperature.isSupported)
                DrawTextLine(GPU_CAP_TEMP, L"Temperature", snapshot.temperature.value, L"°C");

            if (snapshot.hotspot.isSupported)
                DrawTextLine(GPU_CAP_HOTSPOT, L"Hotspot", snapshot.hotspot.value, L"°C");

            if (snapshot.memoryTemperature.isSupported)
                DrawTextLine(GPU_CAP_MEM_TEMP, L"Memory Temperature", snapshot.memoryTemperature.value, L"°C");

            if (snapshot.intakeTemperature.isSupported)
                DrawTextLine(GPU_CAP_INTAKE_TEMP, L"Intake Temperature", snapshot.intakeTemperature.value, L"°C");

            if (snapshot.power.isSupported)
                DrawTextLine(GPU_CAP_POWER, L"Power", snapshot.power.value, L"W");

            if (snapshot.totalBoardPower.isSupported)
                DrawTextLine(GPU_CAP_BOARD_POWER, L"Total Board Power", snapshot.totalBoardPower.value, L"W");

            if (snapshot.voltage.isSupported)
                DrawTextLineInt(GPU_CAP_VOLTAGE, L"Voltage", snapshot.voltage.value, L"mV");

            if (snapshot.powerLimit.isSupported)
                DrawTextLineInt(GPU_CAP_MANUAL_POWER_TUNING, L"Power Limit", snapshot.powerLimit.value, L"%");

            if (snapshot.powerLimitWatts.isSupported)
                DrawTextLineInt(GPU_CAP_MANUAL_POWER_TUNING, L"Power Limit Watts", snapshot.powerLimitWatts.value, L"W");

            if (snapshot.fanSpeed.isSupported)
                DrawTextLineInt(GPU_CAP_FAN_SPEED, L"Fan Speed", snapshot.fanSpeed.value, L"RPM");

            if (snapshot.fanDuty.isSupported)
                DrawTextLineInt(GPU_CAP_FAN_DUTY, L"Fan Duty", snapshot.fanDuty.value, L"%");

            if (snapshot.vram.isSupported)
                DrawTextLineInt(GPU_CAP_VRAM_USAGE, L"VRAM", snapshot.vram.value, L"MB");

            if (snapshot.sharedMemory.isSupported)
                DrawTextLineInt(GPU_CAP_SHARED_MEMORY, L"Shared Memory", snapshot.sharedMemory.value, L"MB");

            if (snapshot.npuFrequency.isSupported)
                DrawTextLineInt(GPU_CAP_NPU_FREQ, L"NPU Frequency", snapshot.npuFrequency.value, L"MHz");

            if (snapshot.npuActivityLevel.isSupported)
                DrawTextLineInt(GPU_CAP_NPU_ACTIVITY, L"NPU Activity", snapshot.npuActivityLevel.value, L"%");

            // | separator
            const int separatorX = m_Column1Rc.right + m_SeparatorMargin;
            HPEN pen = CreatePen(PS_SOLID, 1, colors.separator);
            HGDIOBJ oldPen = SelectObject(hdc, pen);
            MoveToEx(hdc, separatorX, m_ContentRc.top + m_MarginTopBottom, nullptr);
            LineTo(hdc, separatorX, m_ContentRc.bottom - m_MarginTopBottom);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);

#ifdef GPUGRAPHRECT
            HBRUSH brush2 = CreateSolidBrush(rgb(216, 190, 190));
            FillRect(hdc, &m_Column2Rc, brush2);
            DeleteObject(brush2);
#endif

            m_ring.Draw(hdc, snapshot.usage.RoundedValue());

            // Column2
            // Progress Ring + min/max values
            {
                SetBkMode(hdc, TRANSPARENT);

                const auto gpuTemp = m_adlx.Get().usage;

                int yPos = m_ringRc.bottom + m_SeparatorMargin;

                auto DrawLabelValue = [&](const wchar_t *label, double value)
                {
                    SetTextColor(hdc, colors.dim);

                    TextOutW(hdc, m_Column2Rc.left, yPos, label, GPU_COLUMN2_MAXLABEL_LENGTH);

                    wchar_t valueText[32];
                    swprintf_s(valueText, L"%11.0f%%", value); // TODO: ultra fast formatter

                    SetTextColor(hdc, colors.text);

                    TextOutW(hdc, m_Column2Rc.left + m_Colmun2LabelWidth, yPos, valueText, GPU_COLUMN2_MAXVALUE_LENGTH);

                    yPos += m_FontHeight + m_Spacing;
                };

                DrawLabelValue(L"Min  : ", gpuTemp.minValue);
                DrawLabelValue(L"Max  : ", gpuTemp.maxValue);

                SetTextColor(hdc, colors.dim);

                const wchar_t *rangeLabel = L"Range: ";
                TextOutW(hdc, m_Column2Rc.left, yPos, rangeLabel, 7);

                wchar_t rangeText[64];
                swprintf_s(rangeText, L"  %3d - %3d%%", gpuTemp.min, gpuTemp.max); // TODO: ultra fast formatter

                SetTextColor(hdc, colors.text);

                TextOutW(hdc, m_Column2Rc.left + m_Colmun2LabelWidth, yPos, rangeText, GPU_COLUMN2_MAXVALUE_LENGTH);
            }
        }

        EndPaint(m_hwnd, &ps);
        return 0;
    }

    case WM_DPICHANGED:
    {
        m_dpi = HIWORD(wParam);
        CreateUIFont(m_dpi);
        UpdateLayoutRects();

        LOG_DEBUG("[GPUGRAPH] column2 width: %d", m_Column2Width);

        RECT *suggestedRect = reinterpret_cast<RECT *>(lParam);

        SetWindowPos(m_hwnd, nullptr, suggestedRect->left, suggestedRect->top, GetMinRequiredClientWidth(m_dpi), GetRequiredClientHeight(m_dpi), SWP_NOZORDER | SWP_NOACTIVATE);

        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_EXITSIZEMOVE:
        InvalidateRect(m_hwnd, &m_ContentRc, TRUE); // request repaint
        UpdateWindow(m_hwnd);                       // force immediate WM_PAINT
        return 0;

    case WM_CONTEXTMENU:
    {
        HMENU menu = CreatePopupMenu();

        // Theme submenu
        HMENU themeMenu = CreatePopupMenu();
        AppendMenu(themeMenu, MF_STRING | (m_currentTheme == GpuTheme::Type::RadeonRed ? MF_CHECKED : 0), 2, L"Red (AMD)");
        AppendMenu(themeMenu, MF_STRING | (m_currentTheme == GpuTheme::Type::GeForceGreen ? MF_CHECKED : 0), 3, L"Green (NVIDIA)");
        AppendMenu(themeMenu, MF_STRING | (m_currentTheme == GpuTheme::Type::ArcBlue ? MF_CHECKED : 0), 4, L"Blue (Intel)");

        AppendMenu(menu, MF_POPUP, (UINT_PTR)themeMenu, L"Theme");

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
        {
            m_currentTheme = GpuTheme::Type::RadeonRed;
            const auto &colors = GpuTheme::Get(m_currentTheme);
            m_ring.UpdateColors(colors.bar, colors.barBackground, colors.windowBackground, colors.text);
            RebuildBrushes();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;
        }

        case 3:
        {
            m_currentTheme = GpuTheme::Type::GeForceGreen;
            const auto &colors = GpuTheme::Get(m_currentTheme);
            m_ring.UpdateColors(colors.bar, colors.barBackground, colors.windowBackground, colors.text);
            RebuildBrushes();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;
        }

        case 4:
        {
            m_currentTheme = GpuTheme::Type::ArcBlue;
            const auto &colors = GpuTheme::Get(m_currentTheme);
            m_ring.UpdateColors(colors.bar, colors.barBackground, colors.windowBackground, colors.text);
            RebuildBrushes();
            InvalidateRect(m_hwnd, nullptr, TRUE);
            break;
        }
        }

        return 0;
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

        // if (PtInRect(&m_processRC, pt))
        // {
        //     SetCursor(LoadCursor(nullptr, IDC_HAND));
        //     return TRUE;
        // }

        break;
    }
    }

    return DefWindowProc(m_hwnd, msg, wParam, lParam);
}

void GpuGraphWindow::CreateUIFont(UINT dpi)
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

    if (m_ringFont)
    {
        DeleteObject(m_ringFont);
        m_ringFont = nullptr;
    }

    const int height = -MulDiv(m_fontSize, dpi, USER_DEFAULT_SCREEN_DPI);
    const int titleHeight = -MulDiv(m_titleFontSize, dpi, USER_DEFAULT_SCREEN_DPI);
    const int ringFontSize = -MulDiv(m_ringFontSize, dpi, USER_DEFAULT_SCREEN_DPI);

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

    m_ringFont = CreateFontW(
        ringFontSize,
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
    m_Column2Width = m_FontWidth * GPU_COLUMN2_MAXTEXT_LENGTH;
    m_Colmun2LabelWidth = m_FontWidth * GPU_COLUMN2_MAXLABEL_LENGTH;

    // One character of breathing room between text and graph.
    m_BarLeftMargin = m_FontWidth;

    const double fontScaling = static_cast<double>(m_fontSize) / static_cast<double>(c_defaultFontSize);

    const auto scale = [&](double value) -> int
    {
        return static_cast<int>(std::lround(value * dpi / 96.0));
    };

    const auto scaleFont = [&](double value) -> int
    {
        return static_cast<int>(
            std::lround(value * dpi / 96.0 * fontScaling));
    };

    m_MarginTopBottom = scaleFont(c_MarginTopBottom);
    m_MarginLeftRight = scaleFont(c_MarginLeftRight);
    m_Spacing = scaleFont(c_LineSpace);
    m_OnePxScaled = scale(1.0);
    m_BarHeight = m_FontAscent - m_OnePxScaled;
    m_MarkerWidth = scale(2.0);
    m_SeparatorMargin = scaleFont(c_SeparatorMargin);
}

int GpuGraphWindow::GetRequiredClientHeight(UINT dpi) const
{
    const int titlePadding = Scale(c_TitlePaddingTopBottom, dpi);
    const int titleBarHeight = m_FontHeight + titlePadding * 2;
    const int contentHeight = m_metricsCount * m_FontHeight + m_MarginTopBottom * 2;
    const int column1Height = titleBarHeight + contentHeight + m_borderSize;
    const int column2Height = m_ringRc.bottom + m_SeparatorMargin + m_FontHeight * 3 + m_Spacing * 2 + m_MarginTopBottom;

    return max(column1Height, column2Height);
}

void GpuGraphWindow::UpdateWindowHeight()
{
    m_metricsCount = CountSupportedMetrics();
    int desiredClientHeight = GetRequiredClientHeight(m_dpi);
    SetWindowPos(m_hwnd, nullptr, 0, 0, m_width, desiredClientHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool GpuGraphWindow::SaveSettings()
{
    LOG_DEBUG("[GPU] Saving Settings");

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
    WritePrivateProfileStringW(L"GPU", L"X", buffer, path);

    swprintf_s(buffer, L"%d", y);
    WritePrivateProfileStringW(L"GPU", L"Y", buffer, path);

    swprintf_s(buffer, L"%d", width);
    WritePrivateProfileStringW(L"GPU", L"Width", buffer, path);

    swprintf_s(buffer, L"%d", height);
    WritePrivateProfileStringW(L"GPU", L"Height", buffer, path);

    UINT dpi = m_dpi;
    swprintf_s(buffer, L"%d", dpi);
    WritePrivateProfileStringW(L"GPU", L"DPI", buffer, path);

    swprintf_s(buffer, L"%d", m_userClose ? 0 : 1);
    WritePrivateProfileStringW(L"Window", L"GpuGraphActive", buffer, path);

    swprintf_s(buffer, L"%d", m_currentTheme);
    WritePrivateProfileStringW(L"GPU", L"Theme", buffer, path);

    swprintf_s(buffer, L"%d", m_fontSize);
    WritePrivateProfileStringW(L"GPU", L"FontSize", buffer, path);

    return true;
}

bool GpuGraphWindow::LoadSettings()
{
    LOG_DEBUG("[GPU] Loading Settings");

    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';

    wcscat_s(path, SETTINGS_FILE);

    m_x = GetPrivateProfileIntW(L"GPU", L"X", -1, path);
    m_y = GetPrivateProfileIntW(L"GPU", L"Y", -1, path);

    m_width = GetPrivateProfileIntW(L"GPU", L"Width", -1, path);
    m_height = GetPrivateProfileIntW(L"GPU", L"Height", -1, path);

    m_savedDpi = GetPrivateProfileIntW(L"GPU", L"DPI", 96, path);

    const auto value = GetPrivateProfileIntW(L"GPU", L"Theme", 0, path);
    if (value >= 0 && value < static_cast<UINT>(Theme::Type::COUNT))
        m_currentTheme = static_cast<GpuTheme::Type>(value);
    else
        m_currentTheme = GpuTheme::Type::RadeonRed;

    m_fontSize = GetPrivateProfileIntW(L"GPU", L"FontSize", c_defaultFontSize, path);

    return true;
}

int GpuGraphWindow::GetMinRequiredClientWidth(UINT dpi) const
{
    return GPU_COLUMN1_LENGTH * m_FontWidth + m_MarginLeftRight * 2 + m_borderSize * 2 + m_Column2Width + m_SeparatorMargin * 2 + 1;
}

void GpuGraphWindow::UpdateLayoutRects()
{
    RECT rc;
    GetClientRect(m_hwnd, &rc);

    m_borderSize = Scale(c_borderWidth, m_dpi);
    const int titlePadding = Scale(c_TitlePaddingTopBottom, m_dpi);
    const int titleBarHeight = m_FontHeight + titlePadding * 2;

    m_ContentRc.left = rc.left + m_borderSize;
    m_ContentRc.right = rc.right - m_borderSize;
    m_ContentRc.top = rc.top + m_borderSize + titleBarHeight;
    m_ContentRc.bottom = rc.bottom - m_borderSize;

    m_BodyRc.left = m_ContentRc.left + m_MarginLeftRight;
    m_BodyRc.right = m_ContentRc.right - m_MarginLeftRight;
    m_BodyRc.top = m_ContentRc.top + m_MarginTopBottom;
    m_BodyRc.bottom = m_ContentRc.bottom - m_MarginTopBottom;

    m_Column1Rc.left = m_BodyRc.left;
    m_Column1Rc.right = m_BodyRc.left + GPU_COLUMN1_LENGTH * m_FontWidth;
    m_Column1Rc.top = m_BodyRc.top;
    m_Column1Rc.bottom = m_BodyRc.bottom;

    m_ringSize = MulDiv(Scale(c_ringSize, m_dpi), m_fontSize, c_defaultFontSize);

    m_Column2Rc.left = m_Column1Rc.right + m_SeparatorMargin * 2 + 1;
    m_Column2Rc.right = m_Column2Rc.left + max(m_ringSize, m_Column2Width);
    m_Column2Rc.top = m_BodyRc.top;
    m_Column2Rc.bottom = m_BodyRc.bottom;

    m_ringRc.left = m_Column2Rc.left + ((m_Column2Rc.right - m_Column2Rc.left) - m_ringSize) / 2;
    m_ringRc.right = m_ringRc.left + m_ringSize;
    m_ringRc.top = m_Column2Rc.top;
    m_ringRc.bottom = m_ringRc.top + m_ringSize;

    m_ring.Update(GetDC(m_hwnd), m_ringRc, m_ringFont);
}

void GpuGraphWindow::OnResizeWindow(bool grow)
{
    int oldFontSize = m_fontSize;

    RECT rc{};
    GetClientRect(m_hwnd, &rc);

    if (grow)
        m_fontSize = min(m_fontSize + 2, c_MaxFontSize);
    else
        m_fontSize = max(m_fontSize - 2, c_MinFontSize);

    LOG_DEBUG("[GPUGRAPH] New font size=%d", m_fontSize);

    // Already at min/max
    if (m_fontSize == oldFontSize)
        return;

    m_titleFontSize = GetScaledTitleFontSize();

    const UINT dpi = m_dpi;

    CreateUIFont(dpi);
    UpdateLayoutRects();

    int desiredClientHeight = GetRequiredClientHeight(dpi);

    int minWidth = GetMinRequiredClientWidth(dpi);

    SetWindowPos(m_hwnd, nullptr, 0, 0, minWidth, desiredClientHeight, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    RECT newRc{};
    GetClientRect(m_hwnd, &newRc);

    // LOG_DEBUG("[GPUGRAPH] requested client=%dx%d actual client=%dx%d", minWidth, desiredClientHeight, newRc.right - newRc.left, newRc.bottom - newRc.top);

    InvalidateRect(m_hwnd, nullptr, TRUE);
}

int GpuGraphWindow::GetScaledTitleFontSize() const
{
    const int scaled = static_cast<int>(std::lround(m_fontSize * static_cast<float>(c_defaultTileFontSize) / c_defaultFontSize));
    return std::clamp(scaled, c_minTitleFontSize, c_maxTitleFontSize);
}

int GpuGraphWindow::CountSupportedMetrics() const
{
    auto snapshot = m_adlx.Get();

    int count = 0;
    count += snapshot.usage.isSupported;
    count += snapshot.clockSpeed.isSupported;
    count += snapshot.vramClockSpeed.isSupported;
    count += snapshot.temperature.isSupported;
    count += snapshot.hotspot.isSupported;
    count += snapshot.memoryTemperature.isSupported;
    count += snapshot.intakeTemperature.isSupported;
    count += snapshot.power.isSupported;
    count += snapshot.totalBoardPower.isSupported;
    count += snapshot.voltage.isSupported;
    count += snapshot.powerLimit.isSupported;
    count += snapshot.powerLimitWatts.isSupported;
    count += snapshot.fanSpeed.isSupported;
    count += snapshot.fanDuty.isSupported;
    count += snapshot.vram.isSupported;
    count += snapshot.sharedMemory.isSupported;
    count += snapshot.npuFrequency.isSupported;
    count += snapshot.npuActivityLevel.isSupported;
    return count;
}