#pragma once

#define rgb(r, g, b) RGB(r, g, b)

constexpr wchar_t APPNAME[] = L"RadeonMon";
constexpr uint8_t APPNAME_LENGTH = static_cast<uint8_t>(_countof(APPNAME) - 1);
static_assert(APPNAME_LENGTH <= UINT8_MAX);
constexpr int APP_REFRESH_TIMER = 2000;
constexpr int APPWIDTH = 310;
constexpr int APPHEIGHT = 370;

constexpr int WINDOW_RESIZE_STEP = 20;
constexpr int PADDING_LEFT = 14;
constexpr int PADDING_TOP = 9;
constexpr int PADDING_BOTTOM = 8;
constexpr int LABEL_WIDTH = 170;
constexpr int LINE_HEIGHT = 24;
constexpr int LINE_GAP = 7;
constexpr int GAP = 10;
constexpr int BORDER = 1;
constexpr int TITLE_PADDING = 4;
constexpr int SEPARATOR_HEIGHT = 1;
constexpr int SPACER = 40;

// Fonts
constexpr wchar_t FONT_FAMILY[] = L"Consolas";
constexpr wchar_t NOTIFICATION_FONT_FAMILY[] = L"Lucida Console";

constexpr int TITLE_FONTSIZE = 14;
constexpr int FONTSIZE = 16;
constexpr int NOTIFICATION_FONTSIZE = 11;
constexpr int CARD_FONTSIZE = 13;

constexpr UINT FONTSIZE_MIN = 10;
constexpr UINT FONTSIZE_MAX = 22;

constexpr int TEMPERATURE_THRESHOLD = 95;

constexpr COLORREF BACKGROUNDCOLOR = rgb(30, 30, 30);
constexpr COLORREF LABELCOLOR = rgb(180, 180, 180);
constexpr COLORREF VALUECOLOR = rgb(240, 240, 240);
constexpr COLORREF WARNINGCOLOR = rgb(255, 165, 0);
constexpr COLORREF SEPARATORCOLOR = rgb(60, 60, 60);
constexpr COLORREF BORDERCOLOR = rgb(200, 35, 35);
constexpr COLORREF NOTIFICATIONCOLOR = rgb(212, 175, 55);
constexpr COLORREF SERVERSTATUSCOLOR = RGB(0, 225, 255);

// Timer IDs
constexpr UINT_PTR APP_POLLING_ID = 1;
constexpr UINT_PTR NETWORK_TIMER_ID = 2;

// IDM
constexpr int IDM_RESTART_AS_ADMIN = 1001;
constexpr int IDM_ALWAYS_ON_TOP = 1002;
constexpr int IDM_AUTOSTART = 1003;
constexpr int IDM_WEBSERVER_BASE = 1004;
constexpr int IDM_WEBSERVER_MAX = 1104;
constexpr int IDM_WEBSERVER_STOP = 1105;
constexpr int IDM_CHECK_VERSION = 1106;
constexpr int IDM_ABOUT = 1107;
constexpr int IDM_EXIT = 1108;

constexpr wchar_t REPOURL[] = L"https://api.github.com/repos/amu2mod/RadeonMon/releases/latest";
constexpr wchar_t LATESTURL[] = L"https://github.com/amu2mod/RadeonMon/releases/latest";
constexpr wchar_t ABOUTURL[] = L"https://github.com/amu2mod/RadeonMon";

constexpr wchar_t WEBSERVER_PORT[] = L"9090";
