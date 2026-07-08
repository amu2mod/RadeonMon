#pragma once

#include <windows.h>
#include <string>

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

    // swprintf_s(buffer, L"%d", g_width);
    // WritePrivateProfileStringW(L"Window", L"Width", buffer, path);

    // swprintf_s(buffer, L"%d", g_height);
    // WritePrivateProfileStringW(L"Window", L"Height", buffer, path);

    swprintf_s(buffer, L"%d", g_alwaysOnTop ? 1 : 0);
    WritePrivateProfileStringW(L"Window", L"AlwaysOnTop", buffer, path);

    // swprintf_s(buffer, L"%u", g_fontSize);
    // WritePrivateProfileStringW(L"Window", L"FontSize", buffer, path);
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

    // g_width = GetPrivateProfileIntW(L"Window", L"Width", APPWIDTH, path);
    // g_height = GetPrivateProfileIntW(L"Window", L"Height", APPHEIGHT, path);

    g_alwaysOnTop = GetPrivateProfileIntW(L"Window", L"AlwaysOnTop", 0, path) != 0;

    // g_fontSize = static_cast<UINT>(GetPrivateProfileIntW(L"Window", L"FontSize", FONTSIZE, path));
}