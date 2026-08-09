#pragma once

#include <Windows.h>

#include "structures.hpp"

#define rgb(r, g, b) RGB(r, g, b) // vscode trick
constexpr COLORREF BACKGROUNDCOLOR = rgb(30, 30, 30);
constexpr COLORREF LABELCOLOR = rgb(180, 180, 180);
constexpr COLORREF VALUECOLOR = rgb(240, 240, 240);
constexpr COLORREF WARNINGCOLOR = rgb(255, 165, 0);
constexpr COLORREF ALERTCOLOR = rgb(221, 0, 0);
constexpr COLORREF SEPARATORCOLOR = rgb(60, 60, 60);
constexpr COLORREF BORDERCOLOR = rgb(200, 35, 35);
constexpr COLORREF NOTIFICATIONCOLOR = rgb(241, 215, 5);
// constexpr COLORREF SERVERSTATUSCOLOR = rgb(0, 134, 223);
constexpr COLORREF SERVERSTATUSCOLOR = rgb(255, 193, 7);

constexpr COLORREF colorMapping[] = {
    VALUECOLOR,
    WARNINGCOLOR,
    ALERTCOLOR};

static_assert(std::size(colorMapping) == static_cast<std::size_t>(TextLevel::Count), "colorMapping and TextLevel are out of sync");

/////////////
// CPU Chart
/////////////

namespace Theme
{
    enum class Type : int
    {
        SkyBlue = 0,
        RyzenOrange,
        COUNT
    };

    struct Colors
    {
        COLORREF windowBackground;
        COLORREF separator;

        COLORREF barBackground;
        COLORREF bar;
        COLORREF marker;

        COLORREF dim;
        COLORREF text;
        COLORREF header;

        COLORREF titlebar;
    };

    constexpr Colors SkyBlue{
        .windowBackground = BACKGROUNDCOLOR,
        .separator = rgb(80, 80, 80),

        .barBackground = rgb(28, 27, 31),
        .bar = rgb(56, 189, 248),
        .marker = rgb(80, 80, 80),

        .dim = rgb(136, 136, 136),
        .text = rgb(225, 228, 234),
        .header = rgb(56, 189, 248),

        .titlebar = rgb(35, 48, 62)};

    constexpr Colors RyzenOrange{
        .windowBackground = BACKGROUNDCOLOR,
        .separator = rgb(75, 70, 68),

        .barBackground = rgb(26, 24, 23),
        .bar = rgb(240, 90, 34),
        .marker = rgb(75, 70, 68),

        .dim = rgb(140, 130, 125),
        .text = rgb(235, 232, 230),
        .header = rgb(240, 90, 34),

        .titlebar = rgb(48, 38, 35)};

    constexpr const Colors &Get(Type type)
    {
        switch (type)
        {
        case Type::SkyBlue:
            return SkyBlue;

        case Type::RyzenOrange:
            return RyzenOrange;
        }

        return RyzenOrange;
    }
}

/////////////
// GPU Chart
/////////////

constexpr COLORREF GPUCHART_WINDOW_BACKGROUND = BACKGROUNDCOLOR;
constexpr COLORREF GPUCHART_SEPARATOR = rgb(80, 80, 80);

constexpr COLORREF GPUCHART_BARBACKGROUND = rgb(28, 27, 31);
constexpr COLORREF GPUCHART_BAR = rgb(56, 189, 248);
constexpr COLORREF GPUCHART_MARKER = GPUCHART_SEPARATOR;

constexpr COLORREF GPUCHART_DIM = RGB(136, 136, 136);
constexpr COLORREF GPUCHART_TEXT = RGB(225, 228, 234);
constexpr COLORREF GPUCHART_HEADER = GPUCHART_BAR;