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
constexpr COLORREF SERVERSTATUSCOLOR = rgb(0, 134, 223);

constexpr COLORREF colorMapping[] = {
    VALUECOLOR,
    WARNINGCOLOR,
    ALERTCOLOR};

static_assert(std::size(colorMapping) == static_cast<std::size_t>(TextLevel::Count), "colorMapping and TextLevel are out of sync");