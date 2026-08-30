#pragma once

#include <windows.h>
#include <string>
#include <algorithm>

#include "radeonmon/globals.hpp"

inline constexpr wchar_t SETTINGS_FILE[] = L"settings.ini";

inline void SavePreferences()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';

    wcscat_s(path, SETTINGS_FILE);

    wchar_t buffer[32];

    swprintf_s(buffer, L"%d", g_xPos);
    WritePrivateProfileStringW(L"Window", L"X", buffer, path);

    swprintf_s(buffer, L"%d", g_yPos);
    WritePrivateProfileStringW(L"Window", L"Y", buffer, path);

    swprintf_s(buffer, L"%d", g_alwaysOnTop ? 1 : 0);
    WritePrivateProfileStringW(L"Window", L"AlwaysOnTop", buffer, path);

    swprintf_s(buffer, L"%u", g_fontSize);
    WritePrivateProfileStringW(L"Window", L"FontSize", buffer, path);

    swprintf_s(buffer, L"%d", g_isFpsEnabled ? 1 : 0);
    WritePrivateProfileStringW(L"Window", L"FpsEnabled", buffer, path);

    swprintf_s(buffer, L"%d", g_isVRREnabled ? 1 : 0);
    WritePrivateProfileStringW(L"Window", L"VRREnabled", buffer, path);

    swprintf_s(buffer, L"%d", g_currentWebTemplate == IDM_WEBSERVER_TEMPLATE_LIGHT ? IDM_WEBSERVER_TEMPLATE_LIGHT : IDM_WEBSERVER_TEMPLATE_HEAVY);
    WritePrivateProfileStringW(L"WebServer", L"Template", buffer, path);

    swprintf_s(buffer, L"%d", g_DontShowHttpsWebServerWarning ? 1 : 0);
    WritePrivateProfileStringW(L"WebServer", L"WebServerHttpsWarning", buffer, path);

    if (!g_screenshot.IsPathEmpty())
        WritePrivateProfileStringW(L"Screenshot", L"Path", g_screenshot.GetPath(), path);

    swprintf_s(buffer, L"%d", static_cast<int>(g_screenshot.m_format));
    WritePrivateProfileStringW(L"Screenshot", L"Format", buffer, path);
}

inline void LoadPreferences()
{
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);

    wchar_t *lastSlash = wcsrchr(path, L'\\');
    if (lastSlash)
        *(lastSlash + 1) = L'\0';

    wcscat_s(path, SETTINGS_FILE);

    g_xPos = GetPrivateProfileIntW(L"Window", L"X", CW_USEDEFAULT, path);
    g_yPos = GetPrivateProfileIntW(L"Window", L"Y", CW_USEDEFAULT, path);

    g_alwaysOnTop = GetPrivateProfileIntW(L"Window", L"AlwaysOnTop", 0, path) != 0;

    g_fontSize = std::clamp(static_cast<UINT>(GetPrivateProfileIntW(L"Window", L"FontSize", FONTSIZE, path)), FONTSIZE_MIN, FONTSIZE_MAX);

    g_isFpsEnabled = GetPrivateProfileIntW(L"Window", L"FpsEnabled", 0, path) != 0;
    g_isVRREnabled = GetPrivateProfileIntW(L"Window", L"VRREnabled", 0, path) != 0;

    g_currentWebTemplate = GetPrivateProfileIntW(L"WebServer", L"Template", IDM_WEBSERVER_TEMPLATE_LIGHT, path);

    g_DontShowHttpsWebServerWarning = GetPrivateProfileIntW(L"WebServer", L"WebServerHttpsWarning", 0, path) != 0;

    g_isCpuGraphEnabled = GetPrivateProfileIntW(L"Window", L"CpuGraphActive", 0, path) != 0;
    g_isGpuGraphEnabled = GetPrivateProfileIntW(L"Window", L"GpuGraphActive", 0, path) != 0;

    wchar_t buffer[MAX_PATH] = {};
    DWORD length = GetPrivateProfileStringW(L"Screenshot", L"Path", L"", buffer, MAX_PATH, path);

    if (length != 0)
        g_screenshot.SetPath(buffer);

    UINT f = GetPrivateProfileIntW(L"Screenshot", L"Format", Screenshot::Format::BMP, path);
    if (f < 0 || f > Screenshot::Format::PNG)
        g_screenshot.m_format = Screenshot::Format::BMP;
    else
        g_screenshot.m_format = static_cast<Screenshot::Format>(f);
}