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

    m_width = minWidth;
    m_height = minHeight;

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

    UpdateLayoutRects();

    BuildStatRows();

    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
    UpdateWindow(m_hwnd);

    m_forceFullRedraw = false;

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
        InvalidateRect(m_hwnd, &m_Column1ValuesRc, FALSE);
        InvalidateRect(m_hwnd, &m_Column2Rc, FALSE);
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

        GPU_ROW_ID rowId = HitTestRow(pt);
        if (rowId != m_selected && rowId != GpuGraphWindow::GPU_ROW_ID::Count)
        {
            LOG_DEBUG("[GOUGRAPH] label selected: %d", rowId);
            m_selected = rowId;
            const StatRow *selectedRow = FindRow(m_selected);
            if (selectedRow)
            {
                int maxValue;

                if (selectedRow->isInt)
                    maxValue = selectedRow->getInt(m_adlx.Get()).max;
                else
                    maxValue = static_cast<int>(selectedRow->getDouble(m_adlx.Get()).max);

                m_ring.UpdateMaxValue(maxValue);
                LOG_DEBUG("[GPUGRAGH] Setting max range = %d", maxValue);
                m_ring.UpdateUnit(selectedRow->unit);
            }
            m_forceFullRedraw = true;
            RECT rc{0, 0, m_width, m_height};
            InvalidateRect(m_hwnd, &rc, false);
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
        m_width = LOWORD(lParam);
        m_height = HIWORD(lParam);
        UpdateLayoutRects();
        BuildStatRows();

        HDC hdc = GetDC(m_hwnd);
        if (hdc)
        {
            EnsureBackBuffer(hdc, m_width, m_height);
        }
#ifdef _DEBUG
        else
        {
            LOG_ERROR("[GPUGRAPH] Failed to get DC for back buffer");
        }
#endif

        const auto &colors = GpuTheme::Get(m_currentTheme);
        auto snapshot = m_adlx.Get();
        const StatRow *row = FindRow(m_selected);
        int maxRange = row->isInt ? row->getInt(m_adlx.Get()).max : static_cast<int>(row->getDouble(m_adlx.Get()).max);

        m_ring.Init(m_backDC, m_ringRc, m_ringFont, row->unit, maxRange, colors.bar, colors.barBackground, colors.windowBackground, colors.text);
        ReleaseDC(m_hwnd, hdc);

        m_forceFullRedraw = true;
        InvalidateRect(m_hwnd, nullptr, TRUE);
        return 0;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(m_hwnd, &ps);
        RECT rc{};
        GetClientRect(m_hwnd, &rc);

#ifdef GDIDRAW
        m_GdiCount = 0;
#endif

        if (Intersects(m_TitleBarRc, ps.rcPaint))
        {
            GDI_COUNT(this);

#ifdef GPUGRAPHRECT
            HBRUSH brush = CreateSolidBrush(rgb(130, 210, 221));
            FillRect(m_backDC, &rc, brush); // full background

            HBRUSH cbrush = CreateSolidBrush(rgb(239, 173, 241));
            FillRect(m_backDC, &m_Column1Rc, cbrush);
            FillRect(m_backDC, &m_Column2Rc, cbrush);

            DeleteObject(brush);
#else
            FillRect(m_backDC, &rc, bgBrush);
#endif

            PaintFrame(m_backDC);
            PaintLabels(m_backDC);
            PaintSeparator(m_backDC);
            PaintValues(m_backDC);
        }
        else if (Intersects(m_Column1ValuesRc, ps.rcPaint))
            PaintValues(m_backDC);

        BitBlt(hdc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left, ps.rcPaint.bottom - ps.rcPaint.top, m_backDC, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);

#ifdef GDIDRAW
        LOG_TRACE("[GPUGRAPH] GDI draw = %d", m_GdiCount);
#endif

        m_forceFullRedraw = false;

        EndPaint(m_hwnd, &ps);
        break;
    }

    case WM_DPICHANGED:
    {
        m_dpi = HIWORD(wParam);
        CreateUIFont(m_dpi);
        UpdateLayoutRects();

        RECT *suggestedRect = reinterpret_cast<RECT *>(lParam);

        SetWindowPos(m_hwnd, nullptr, suggestedRect->left, suggestedRect->top, GetMinRequiredClientWidth(m_dpi), GetRequiredClientHeight(m_dpi), SWP_NOZORDER | SWP_NOACTIVATE);

        m_forceFullRedraw = true;

        InvalidateRect(m_hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_EXITSIZEMOVE:
        InvalidateRect(m_hwnd, &m_ContentRc, FALSE); // request repaint
        UpdateWindow(m_hwnd);                        // force immediate WM_PAINT
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
            ApplyTheme(GpuTheme::Type::RadeonRed);
            break;
        case 3:
            ApplyTheme(GpuTheme::Type::GeForceGreen);
            break;
        case 4:
            ApplyTheme(GpuTheme::Type::ArcBlue);
            break;
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

        GPU_ROW_ID rowId = HitTestRow(pt);
        if (rowId != GpuGraphWindow::GPU_ROW_ID::Count)
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }

        // if (PtInRect(&m_processRC, pt))
        // {
        //     SetCursor(LoadCursor(nullptr, IDC_HAND));
        //     return TRUE;
        // }

        break;
    }

    case WM_ERASEBKGND:
    {
        return true;
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
    const int ringFontSize = MulDiv(-MulDiv(m_ringFontSize, dpi, USER_DEFAULT_SCREEN_DPI), m_fontSize, c_defaultFontSize);

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
    m_LabelValueOffset = m_FontWidth * GPU_COLUMN1_LABEL_MAXLENGTH;
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

    m_titlePadding = Scale(c_TitlePaddingTopBottom, m_dpi);
    m_titleBarHeight = m_FontHeight + m_titlePadding * 2;

    m_borderSize = Scale(c_borderWidth, m_dpi);

    m_TitleBarRc = RECT{m_borderSize, m_borderSize, rc.right - m_borderSize, m_borderSize + m_titleBarHeight};

    m_ContentRc.left = rc.left + m_borderSize;
    m_ContentRc.right = rc.right - m_borderSize;
    m_ContentRc.top = rc.top + m_borderSize + m_titleBarHeight;
    m_ContentRc.bottom = rc.bottom - m_borderSize;

    m_BodyRc.left = m_ContentRc.left + m_MarginLeftRight;
    m_BodyRc.right = m_ContentRc.right - m_MarginLeftRight;
    m_BodyRc.top = m_ContentRc.top + m_MarginTopBottom;
    m_BodyRc.bottom = m_ContentRc.bottom - m_MarginTopBottom;

    m_Column1Rc.left = m_BodyRc.left;
    m_Column1Rc.right = m_BodyRc.left + GPU_COLUMN1_LENGTH * m_FontWidth;
    m_Column1Rc.top = m_BodyRc.top;
    m_Column1Rc.bottom = m_BodyRc.bottom;

    m_Column1ValuesRc.left = m_BodyRc.left + GPU_COLUMN1_LABEL_MAXLENGTH * m_FontWidth;
    m_Column1ValuesRc.right = m_Column1Rc.right;
    m_Column1ValuesRc.top = m_BodyRc.top;
    m_Column1ValuesRc.bottom = m_BodyRc.bottom;

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

    m_MinLabelY = m_ringRc.bottom + m_FontHeight + m_Spacing;
    m_MaxLabelY = m_MinLabelY + m_FontHeight + m_Spacing;
    m_RangeLabelY = m_MaxLabelY + m_FontHeight + m_Spacing;
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

void GpuGraphWindow::PaintLabels(HDC hdc)
{
    const auto &colors = GpuTheme::Get(m_currentTheme);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colors.dim);
    SelectObject(hdc, m_hFont);

    int x = m_BodyRc.left;
    for (const auto &row : m_statRows)
    {
        wchar_t label[20];
        FormatLabel(label, row.name, 18);
        SetTextColor(hdc, row.id == m_selected ? colors.text : colors.dim);
        TextOutW(hdc, x, row.y, label, static_cast<int>(wcslen(label)));
    }
    GDI_COUNT_N(this, static_cast<int>(m_statRows.size()));

    // Column2 static labels
    SetTextColor(hdc, colors.dim);
    TextOutW(hdc, m_Column2Rc.left, m_MinLabelY, L"Min  : ", 7);
    TextOutW(hdc, m_Column2Rc.left, m_MaxLabelY, L"Max  : ", 7);
    TextOutW(hdc, m_Column2Rc.left, m_RangeLabelY, L"Range: ", 7);
    GDI_COUNT_N(this, 3);
}

void GpuGraphWindow::PaintValues(HDC hdc)
{
    const auto &snapshot = m_adlx.Get();
    const auto &colors = GpuTheme::Get(m_currentTheme);

    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, colors.windowBackground);
    SelectObject(hdc, m_hFont);

    const int valueX = m_BodyRc.left + m_LabelValueOffset;

    for (const auto &row : m_statRows)
    {
        if (row.isInt)
        {
            auto &metric = row.getInt(snapshot);
            if (!metric.hasChanged && !m_forceFullRedraw)
                continue;

            SetTextColor(hdc, row.id == m_selected ? colors.text : colors.dim);

            wchar_t text[16];
            FormatValue(text, metric.value, row.unit);
            TextOutW(hdc, valueX, row.y, text, static_cast<int>(wcslen(text)));
#ifdef GDIDRAW
            GDI_COUNT(this);
#endif
            m_adlx.ClearChanged(row.intMember);
        }
        else
        {
            auto &metric = row.getDouble(snapshot);
            if (!metric.hasChanged && !m_forceFullRedraw)
                continue;

            SetTextColor(hdc, row.id == m_selected ? colors.text : colors.dim);

            wchar_t text[16];
            FormatValue(text, metric.RoundedValue(), row.unit);
            TextOutW(hdc, valueX, row.y, text, static_cast<int>(wcslen(text)));
#ifdef GDIDRAW
            GDI_COUNT(this);
#endif
            m_adlx.ClearChanged(row.doubleMember);
        }
    }

    GDI_COUNT_N(this, 4);

    // Min/Max/Range values
    const auto gpuTemp = snapshot.usage; // matches your original code's variable naming
    SetBkMode(hdc, OPAQUE);
    SetBkColor(hdc, colors.windowBackground);
    SetTextColor(hdc, colors.text);

    const StatRow *selectedRow = FindRow(m_selected);
    if (selectedRow)
    {
        double minValue, maxValue;
        int minInt, maxInt;
        const wchar_t *unit = selectedRow->unit;

        if (selectedRow->isInt)
        {
            const auto &metric = selectedRow->getInt(snapshot);
            minValue = metric.minValue;
            maxValue = metric.maxValue;
            minInt = metric.min;
            maxInt = metric.max;
            m_ring.Draw(hdc, metric.value);
        }
        else
        {
            const auto &metric = selectedRow->getDouble(snapshot);
            minValue = metric.minValue;
            maxValue = metric.maxValue;
            minInt = metric.min;
            maxInt = metric.max;
            m_ring.Draw(hdc, metric.RoundedValue());
        }

        wchar_t minText[32];
        FormatValue2(minText, minValue, unit, GPU_COLUMN2_MAXVALUE_LENGTH);
        TextOutW(hdc, m_Column2Rc.left + m_Colmun2LabelWidth, m_MinLabelY, minText, GPU_COLUMN2_MAXVALUE_LENGTH);

        wchar_t maxText[32];
        FormatValue2(maxText, maxValue, unit, GPU_COLUMN2_MAXVALUE_LENGTH);
        TextOutW(hdc, m_Column2Rc.left + m_Colmun2LabelWidth, m_MaxLabelY, maxText, GPU_COLUMN2_MAXVALUE_LENGTH);

        wchar_t rangeText[64];
        FormatRange(rangeText, minInt, maxInt, unit, GPU_COLUMN2_MAXVALUE_LENGTH);
        TextOutW(hdc, m_Column2Rc.left + m_Colmun2LabelWidth, m_RangeLabelY, rangeText, GPU_COLUMN2_MAXVALUE_LENGTH);

        GDI_COUNT_N(this, 3);
    }
}

void GpuGraphWindow::BuildStatRows()
{
    m_statRows.clear();
    const auto snapshot = m_adlx.Get(); // used only to check isSupported flags

    int y = m_BodyRc.top;
    const int lineHeight = m_FontHeight;

    auto AddDouble = [&](GPU_ROW_ID id, GPU_CAPS cap, const wchar_t *name, const wchar_t *unit, DoubleGetter getter, RadeonMon::Hardware::MetricDouble GpuMetricsSnapshot::*member)
    {
        m_statRows.push_back({id, cap, name, unit, false, y, getter, nullptr, member, nullptr});
        y += lineHeight;
    };

    auto AddInt = [&](GPU_ROW_ID id, GPU_CAPS cap, const wchar_t *name, const wchar_t *unit, IntGetter getter, RadeonMon::Hardware::MetricInt GpuMetricsSnapshot::*member)
    {
        m_statRows.push_back({id, cap, name, unit, true, y, nullptr, getter, nullptr, member});
        y += lineHeight;
    };

    if (snapshot.usage.isSupported)
        AddDouble(GPU_ROW_ID::Usage, GPU_CAP_USAGE, L"GPU Usage", L"%", GetUsage, &RadeonMon::Hardware::GpuMetricsSnapshot::usage);
    if (snapshot.clockSpeed.isSupported)
        AddInt(GPU_ROW_ID::ClockSpeed, GPU_CAP_CLOCK, L"GPU Clock", L"MHz", GetClockSpeed, &RadeonMon::Hardware::GpuMetricsSnapshot::clockSpeed);
    if (snapshot.vramClockSpeed.isSupported)
        AddInt(GPU_ROW_ID::VramClockSpeed, GPU_CAP_VRAM_CLOCK, L"VRAM Clock", L"MHz", GetVramClockSpeed, &RadeonMon::Hardware::GpuMetricsSnapshot::vramClockSpeed);
    if (snapshot.temperature.isSupported)
        AddDouble(GPU_ROW_ID::Temperature, GPU_CAP_TEMP, L"Temperature", L"°C", GetTemperature, &RadeonMon::Hardware::GpuMetricsSnapshot::temperature);
    if (snapshot.hotspot.isSupported)
        AddDouble(GPU_ROW_ID::Hotspot, GPU_CAP_HOTSPOT, L"Hotspot", L"°C", GetHotspot, &RadeonMon::Hardware::GpuMetricsSnapshot::hotspot);
    if (snapshot.memoryTemperature.isSupported)
        AddDouble(GPU_ROW_ID::MemoryTemperature, GPU_CAP_MEM_TEMP, L"Memory Temperature", L"°C", GetMemoryTemperature, &RadeonMon::Hardware::GpuMetricsSnapshot::memoryTemperature);
    if (snapshot.intakeTemperature.isSupported)
        AddDouble(GPU_ROW_ID::IntakeTemperature, GPU_CAP_INTAKE_TEMP, L"Intake Temperature", L"°C", GetIntakeTemperature, &RadeonMon::Hardware::GpuMetricsSnapshot::intakeTemperature);
    if (snapshot.power.isSupported)
        AddDouble(GPU_ROW_ID::Power, GPU_CAP_POWER, L"Power", L"W", GetPower, &RadeonMon::Hardware::GpuMetricsSnapshot::power);
    if (snapshot.totalBoardPower.isSupported)
        AddDouble(GPU_ROW_ID::TotalBoardPower, GPU_CAP_BOARD_POWER, L"Total Board Power", L"W", GetTotalBoardPower, &RadeonMon::Hardware::GpuMetricsSnapshot::totalBoardPower);
    if (snapshot.voltage.isSupported)
        AddInt(GPU_ROW_ID::Voltage, GPU_CAP_VOLTAGE, L"Voltage", L"mV", GetVoltage, &RadeonMon::Hardware::GpuMetricsSnapshot::voltage);
    if (snapshot.powerLimit.isSupported)
        AddInt(GPU_ROW_ID::PowerLimitPercent, GPU_CAP_MANUAL_POWER_TUNING, L"Power Limit", L"%", GetPowerLimit, &RadeonMon::Hardware::GpuMetricsSnapshot::powerLimit);
    if (snapshot.powerLimitWatts.isSupported)
        AddInt(GPU_ROW_ID::PowerLimitWatts, GPU_CAP_MANUAL_POWER_TUNING, L"Power Limit Watts", L"W", GetPowerLimitWatts, &RadeonMon::Hardware::GpuMetricsSnapshot::powerLimitWatts);
    if (snapshot.fanSpeed.isSupported)
        AddInt(GPU_ROW_ID::FanSpeed, GPU_CAP_FAN_SPEED, L"Fan Speed", L"RPM", GetFanSpeed, &RadeonMon::Hardware::GpuMetricsSnapshot::fanSpeed);
    if (snapshot.fanDuty.isSupported)
        AddInt(GPU_ROW_ID::FanDuty, GPU_CAP_FAN_DUTY, L"Fan Duty", L"%", GetFanDuty, &RadeonMon::Hardware::GpuMetricsSnapshot::fanDuty);
    if (snapshot.vram.isSupported)
        AddInt(GPU_ROW_ID::Vram, GPU_CAP_VRAM_USAGE, L"VRAM", L"MB", GetVram, &RadeonMon::Hardware::GpuMetricsSnapshot::vram);
    if (snapshot.sharedMemory.isSupported)
        AddInt(GPU_ROW_ID::SharedMemory, GPU_CAP_SHARED_MEMORY, L"Shared Memory", L"MB", GetSharedMemory, &RadeonMon::Hardware::GpuMetricsSnapshot::sharedMemory);
    if (snapshot.npuFrequency.isSupported)
        AddInt(GPU_ROW_ID::NpuFrequency, GPU_CAP_NPU_FREQ, L"NPU Frequency", L"MHz", GetNpuFrequency, &RadeonMon::Hardware::GpuMetricsSnapshot::npuFrequency);
    if (snapshot.npuActivityLevel.isSupported)
        AddInt(GPU_ROW_ID::NpuActivityLevel, GPU_CAP_NPU_ACTIVITY, L"NPU Activity", L"%", GetNpuActivityLevel, &RadeonMon::Hardware::GpuMetricsSnapshot::npuActivityLevel);
}

void GpuGraphWindow::PaintTitleBar(HDC hdc)
{
    const auto &colors = GpuTheme::Get(m_currentTheme);

    GDI_COUNT(this);
    FillRect(hdc, &m_TitleBarRc, borderBrush);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, colors.text);
    SelectObject(hdc, m_titleFont);

    if (m_FontHeight == 0)
    {
        TEXTMETRIC tm{};
        GetTextMetricsW(hdc, &tm);
        m_FontHeight = tm.tmHeight;
        m_FontAscent = tm.tmAscent;
    }

    const int titleTextY = m_borderSize + m_titlePadding;
    GDI_COUNT(this);
    TextOutW(hdc, m_MarginLeftRight, titleTextY, m_title.c_str(), static_cast<int>(m_title.size()));
}

void GpuGraphWindow::PaintFrame(HDC hdc)
{
    RECT rc{0, 0, m_width, m_height};

    RECT topEdge{rc.left, rc.top, rc.right, rc.top + m_borderSize};
    RECT bottomEdge{rc.left, rc.bottom - m_borderSize, rc.right, rc.bottom};
    RECT leftEdge{rc.left, rc.top, rc.left + m_borderSize, rc.bottom};
    RECT rightEdge{rc.right - m_borderSize, rc.top, rc.right, rc.bottom};

    FillRect(hdc, &topEdge, borderBrush);
    FillRect(hdc, &bottomEdge, borderBrush);
    FillRect(hdc, &leftEdge, borderBrush);
    FillRect(hdc, &rightEdge, borderBrush);

    GDI_COUNT_N(this, 4);

    PaintTitleBar(hdc);
}

void GpuGraphWindow::PaintSeparator(HDC hdc)
{
    const auto &colors = GpuTheme::Get(m_currentTheme);
    const int separatorX = m_Column1Rc.right + m_SeparatorMargin;
    HPEN pen = CreatePen(PS_SOLID, 1, colors.separator);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, separatorX, m_ContentRc.top + m_MarginTopBottom, nullptr);
    GDI_COUNT(this);
    LineTo(hdc, separatorX, m_ContentRc.bottom - m_MarginTopBottom);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
}

void GpuGraphWindow::ApplyTheme(GpuTheme::Type theme)
{
    m_currentTheme = theme;
    const auto &colors = GpuTheme::Get(m_currentTheme);
    m_ring.UpdateColors(colors.bar, colors.barBackground, colors.windowBackground, colors.text);
    RebuildBrushes();
    m_forceFullRedraw = true;
    InvalidateRect(m_hwnd, nullptr, TRUE);
}

void GpuGraphWindow::FormatLabel(wchar_t *dst, const wchar_t *name, int length)
{
    int i = 0;

    // Copy up to `length` characters.
    for (; i < length && name[i]; ++i)
        dst[i] = name[i];

    // Pad to exactly `length` characters.
    for (; i < length; ++i)
        dst[i] = L' ';

    dst[length] = L':';
    dst[length + 1] = L'\0';
}

void GpuGraphWindow::FormatValue(wchar_t *dst, int value, const wchar_t *unit)
{
    wchar_t *p = dst;

    if (value >= 0)
    {
        unsigned int v = static_cast<unsigned int>(value);
        unsigned int t = v;
        int digits = 1;

        while (t >= 10)
        {
            t /= 10;
            ++digits;
        }

        for (int i = digits; i < 6; ++i)
            *p++ = L' ';

        p += digits;
        wchar_t *q = p;

        do
        {
            *--q = L'0' + (v % 10);
            v /= 10;
        } while (v);

        *p++ = L' ';
    }
    else
    {
        // Safe for INT_MIN.
        unsigned int v = 0u - static_cast<unsigned int>(value);
        unsigned int t = v;
        int digits = 1;

        while (t >= 10)
        {
            t /= 10;
            ++digits;
        }

        // Sign consumes one character of the 6-wide field.
        for (int i = digits + 1; i < 6; ++i)
            *p++ = L' ';

        *p++ = L'-';

        p += digits;
        wchar_t *q = p;

        do
        {
            *--q = L'0' + (v % 10);
            v /= 10;
        } while (v);

        *p++ = L' ';
    }

    while (*unit)
        *p++ = *unit++;

    *p = L'\0';
}

const GpuGraphWindow::StatRow *GpuGraphWindow::FindRow(GPU_ROW_ID id) const
{
    for (const auto &row : m_statRows)
    {
        if (row.id == id)
            return &row;
    }
    return nullptr;
}

void GpuGraphWindow::FormatValue2(wchar_t *dst, double value, const wchar_t *unit, int maxLength)
{
    int unitLength = 0;
    while (unit[unitLength])
        ++unitLength;

    // Round like %.0f.
    long long rounded = static_cast<long long>(value >= 0.0 ? value + 0.5 : value - 0.5);

    bool negative = rounded < 0;

    unsigned long long v = negative ? 0ULL - static_cast<unsigned long long>(rounded) : static_cast<unsigned long long>(rounded);

    wchar_t digits[32];
    int digitCount = 0;

    do
    {
        digits[digitCount++] = L'0' + static_cast<int>(v % 10);
        v /= 10;
    } while (v);

    const int valueLength = digitCount + (negative ? 1 : 0);
    const int unitPartLength = 1 + unitLength; // ' ' + unit

    const int totalLength = valueLength + unitPartLength;
    const int padding = maxLength - totalLength;

    for (int i = 0; i < padding; ++i)
        *dst++ = L' ';

    if (negative)
        *dst++ = L'-';

    for (int i = digitCount - 1; i >= 0; --i)
        *dst++ = digits[i];

    *dst++ = L' ';

    while (*unit)
        *dst++ = *unit++;

    *dst = L'\0';
}

void GpuGraphWindow::FormatRange(wchar_t *dst, int minInt, int maxInt, const wchar_t *unit, size_t maxlength)
{
    wchar_t *p = dst;

    auto appendInt = [&p](int value)
    {
        unsigned int n;

        if (value < 0)
        {
            *p++ = L'-';
            n = 0u - (unsigned int)value;
        }
        else
        {
            n = (unsigned int)value;
        }

        wchar_t digits[10];
        int count = 0;

        do
        {
            digits[count++] = wchar_t(L'0' + n % 10);
            n /= 10;
        } while (n > 0);

        while (count > 0)
            *p++ = digits[--count];
    };

    // Special case.
    if (minInt == 0 && maxInt == 0)
    {
        *p++ = L'n';
        *p++ = L'/';
        *p++ = L'a';
    }
    else
    {
        *p++ = L' ';

        appendInt(minInt);

        *p++ = L' ';
        *p++ = L'-';
        *p++ = L' ';

        appendInt(maxInt);

        *p++ = L' ';

        while (*unit)
            *p++ = *unit++;
    }

    size_t len = (size_t)(p - dst);

    // If maxlength is larger, shift the complete string right
    // and add padding at the beginning.
    if (len < maxlength)
    {
        size_t padding = maxlength - len;

        for (size_t i = len; i-- > 0;)
            dst[i + padding] = dst[i];

        for (size_t i = 0; i < padding; ++i)
            dst[i] = L' ';

        len = maxlength;
    }

    // The final character is always the last character of the unit
    // (or 'a' for n/a).
    dst[len] = L'\0';
}

GpuGraphWindow::GPU_ROW_ID GpuGraphWindow::HitTestRow(POINT pt) const
{
    for (const auto &row : m_statRows)
    {
        RECT rowRc{m_Column1Rc.left, row.y, m_Column1ValuesRc.left, row.y + m_FontHeight};
        if (PtInRect(&rowRc, pt))
            return row.id;
    }
    return GpuGraphWindow::GPU_ROW_ID::Count; // no change if click missed all rows
}

void GpuGraphWindow::EnsureBackBuffer(HDC hdc, int width, int height)
{
    if (width <= 0 || height <= 0)
        return;

    if (m_backDC &&
        m_backBitmap &&
        m_backBufferSize.cx == width &&
        m_backBufferSize.cy == height)
    {
        return;
    }

    DestroyBackBuffer();

    m_backDC = CreateCompatibleDC(hdc);
    if (!m_backDC)
        return;

    m_backBitmap = CreateCompatibleBitmap(hdc, width, height);
    if (!m_backBitmap)
    {
        DeleteDC(m_backDC);
        m_backDC = nullptr;
        return;
    }

    m_backOldBitmap =
        static_cast<HBITMAP>(SelectObject(m_backDC, m_backBitmap));

    m_backBufferSize.cx = width;
    m_backBufferSize.cy = height;

    // Optional but generally useful: initialize the entire buffer.
    RECT rc{0, 0, width, height};
    FillRect(m_backDC, &rc, bgBrush);
}

void GpuGraphWindow::DestroyBackBuffer()
{
    if (m_backDC)
    {
        if (m_backOldBitmap)
        {
            SelectObject(m_backDC, m_backOldBitmap);
            m_backOldBitmap = nullptr;
        }

        if (m_backBitmap)
        {
            DeleteObject(m_backBitmap);
            m_backBitmap = nullptr;
        }

        DeleteDC(m_backDC);
        m_backDC = nullptr;
    }

    m_backBufferSize = {};
}
