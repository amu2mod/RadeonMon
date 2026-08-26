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

constexpr COLORREF BRIGHT_GREEN = rgb(57, 255, 20);    // Neon/Electric Green
constexpr COLORREF BRIGHT_BLUE = rgb(0, 210, 255);     // Cyan/Electric Blue
constexpr COLORREF BRIGHT_PURPLE = rgb(216, 112, 255); // Vivid Neon Purple

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

namespace GpuTheme
{
    enum class Type : int
    {
        RadeonRed = 0,
        GeForceGreen,
        ArcBlue,
        COUNT
    };

    struct Colors
    {
        COLORREF windowBackground;
        COLORREF separator;
        COLORREF dimSeparator;

        COLORREF barBackground;
        COLORREF bar;
        COLORREF marker;

        COLORREF dim;
        COLORREF text;
        COLORREF header;

        COLORREF titlebar;
    };

    constexpr Colors RadeonRed{
        .windowBackground = BACKGROUNDCOLOR,
        .separator = rgb(80, 80, 80),
        .dimSeparator = rgb(39, 39, 39),

        .barBackground = rgb(28, 24, 25),
        .bar = rgb(227, 24, 55),
        .marker = rgb(75, 65, 68),

        .dim = rgb(136, 136, 136),
        .text = rgb(238, 232, 233),
        .header = rgb(227, 24, 55),

        .titlebar = rgb(48, 32, 35)};

    constexpr Colors GeForceGreen{
        .windowBackground = BACKGROUNDCOLOR,
        .separator = rgb(80, 80, 80),
        .dimSeparator = rgb(39, 39, 39),

        .barBackground = rgb(23, 28, 23),
        .bar = rgb(118, 185, 0),
        .marker = rgb(65, 75, 65),

        .dim = rgb(136, 136, 136),
        .text = rgb(230, 235, 230),
        .header = rgb(118, 185, 0),

        .titlebar = rgb(32, 45, 32)};

    constexpr Colors ArcBlue{
        .windowBackground = BACKGROUNDCOLOR,
        .separator = rgb(80, 80, 80),
        .dimSeparator = rgb(39, 39, 39),

        .barBackground = rgb(22, 26, 35),
        .bar = rgb(0, 199, 255),
        .marker = rgb(60, 70, 85),

        .dim = rgb(136, 136, 136),
        .text = rgb(230, 235, 242),
        .header = rgb(0, 199, 255),

        .titlebar = rgb(28, 38, 55)};

    constexpr const Colors &Get(Type type)
    {
        switch (type)
        {
        case Type::RadeonRed:
            return RadeonRed;

        case Type::GeForceGreen:
            return GeForceGreen;

        case Type::ArcBlue:
            return ArcBlue;
        }

        return GeForceGreen;
    }
}