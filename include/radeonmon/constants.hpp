#pragma once

constexpr wchar_t APPNAME[] = L"RadeonMon";
constexpr int APP_REFRESH_TIMER = 2000;
constexpr int APPWIDTH = 400;
constexpr int APPHEIGHT = 400;
constexpr int FONTSIZE = 16;
constexpr int PADDING_LEFT = 16;
constexpr int PADDING_TOP = 14;
constexpr int LABEL_WIDTH = 160;
constexpr int LINE_HEIGHT = 24;
constexpr int GAP = 8;

constexpr int TEMPERATURE_THRESHOLD = 95;

constexpr wchar_t FONT_FAMILY[] = L"Consolas";

constexpr COLORREF BACKGROUNDCOLOR = RGB(30, 30, 30);
constexpr COLORREF LABELCOLOR = RGB(180, 180, 180);
constexpr COLORREF VALUECOLOR = RGB(240, 240, 240);
constexpr COLORREF WARNINGCOLOR = RGB(255, 165, 0);
constexpr COLORREF SEPARATORCOLOR = RGB(60, 60, 60);

// IDM
#define IDM_ALWAYS_ON_TOP 1001