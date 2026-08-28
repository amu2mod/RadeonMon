#pragma once

#ifdef _DEBUG
constexpr wchar_t APPNAME[] = L"RadeonMon (Debug)";
#else
constexpr wchar_t APPNAME[] = L"RadeonMon";
#endif
constexpr wchar_t APPNAME_UPDATE[] = L"RadeonMon - Update available";
constexpr uint8_t APPNAME_LENGTH = static_cast<uint8_t>(_countof(APPNAME) - 1);
constexpr uint8_t APPNAME_UPDATE_LENGTH = static_cast<uint8_t>(_countof(APPNAME_UPDATE) - 1);
static_assert(APPNAME_LENGTH <= UINT8_MAX);
static_assert(APPNAME_UPDATE_LENGTH <= UINT8_MAX);

// Polling rate
constexpr int APP_REFRESH_TIMER = 2000;

constexpr int APPWIDTH = 310;
constexpr int APPHEIGHT = 370;

// Spacing
constexpr int WINDOW_RESIZE_STEP = 20;
constexpr int PADDING_LEFT = 14;
constexpr int PADDING_TOP = 9;
constexpr int PADDING_BOTTOM = 9;
constexpr int LABEL_WIDTH = 170;
constexpr int LINE_HEIGHT = 24;
constexpr int LINE_GAP = 6;
constexpr int GAP = 9;
constexpr int BORDER = 1;
constexpr int TITLE_PADDING = 4;
constexpr int SEPARATOR_HEIGHT = 1;
constexpr int SPACER = 10;
constexpr int CARD_PADDING = 2;
constexpr int TAG_GAP = 6;
constexpr int TAG_PADDING = 3;

// Fonts
constexpr wchar_t FONT_FAMILY[] = L"Consolas";
constexpr wchar_t NOTIFICATION_FONT_FAMILY[] = L"Lucida Console";
constexpr wchar_t MAXTXTLABEL[] = L"Power Consumption";
constexpr int MAXTXTLABEL_LENGTH = _countof(MAXTXTLABEL) - 1;
constexpr wchar_t MAXTXTVALUE[] = L"1440p @1000Hz";
constexpr int MAXTXTVALUE_LENGTH = _countof(MAXTXTVALUE) - 1;

constexpr int TITLE_FONTSIZE = 15;
constexpr int FONTSIZE = 16;
constexpr int NOTIFICATION_FONTSIZE = 11;
constexpr int CARD_FONTSIZE = 13;
constexpr int TAG_FONTSIZE = 10;

constexpr UINT FONTSIZE_MIN = 10;
constexpr UINT FONTSIZE_MAX = 26;

constexpr int TEMPERATURE_WARNING_THRESHOLD = 95;
constexpr int TEMPERATURE_ALERT_THRESHOLD = 100;

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
constexpr int IDM_WEBSERVER_TEMPLATE_LIGHT = 1106;
constexpr int IDM_WEBSERVER_TEMPLATE_HEAVY = 1107;
constexpr int IDM_ENABLEFPS_BASE = 1108; // on & off
constexpr int IDM_ENABLEVRR_BASE = 1110; // on & off
constexpr int IDM_CHECK_VERSION = 1112;
constexpr int IDM_ABOUT = 1113;
constexpr int IDM_EXIT = 1114;

// WM
constexpr int WM_APP_LAYOUT = WM_APP + 1;
constexpr int WM_APP_VERSION_RESULT = WM_APP + 2;
constexpr int WM_APP_VERSION_ERROR = WM_APP + 3;
constexpr int WM_APP_GPU_PWR_TUNING_CHANGE = WM_APP + 4;
constexpr int WM_APP_APPLY_TOPMOST = WM_APP + 5;

constexpr wchar_t REPOURL[] = L"https://api.github.com/repos/amu2mod/RadeonMon/releases/latest";
constexpr wchar_t LATESTURL[] = L"https://github.com/amu2mod/RadeonMon/releases/latest";
constexpr wchar_t ABOUTURL[] = L"https://github.com/amu2mod/RadeonMon";

constexpr wchar_t WEBSERVER_PORT[] = L"9090";
constexpr int WEBSERVER_PORT_NUM = 9090;

// Tooltips
constexpr UINT_PTR TOOLID_GPUINFO = 1;

constexpr int GPU_JSON_BUFFER_SIZE = 4096;