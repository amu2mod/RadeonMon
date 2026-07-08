#pragma once

#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>

#include "globals.hpp"

static std::wstring GetCurrentExecutablePath()
{
    wchar_t path[MAX_PATH];

    DWORD len = GetModuleFileNameW(nullptr, path, MAX_PATH);

    if (len == 0 || len == MAX_PATH)
        return L"";

    return std::wstring(path, len);
}

inline bool EnableStartupShortcut()
{
    std::wstring exePath = GetCurrentExecutablePath();

    if (exePath.empty())
        return false;

    wchar_t startupDir[MAX_PATH];

    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupDir)))
        return false;

    std::wstring shortcutPath = std::wstring(startupDir) + L"\\" + APPNAME + L".lnk";

    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    bool comInitialized = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;

    IShellLinkW *link = nullptr;
    IPersistFile *file = nullptr;

    hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, reinterpret_cast<void **>(&link));

    if (FAILED(hr))
    {
        if (comInitialized)
            CoUninitialize();

        return false;
    }

    link->SetPath(exePath.c_str());

    hr = link->QueryInterface(IID_IPersistFile, reinterpret_cast<void **>(&file));

    bool result = false;

    if (SUCCEEDED(hr))
    {
        result = SUCCEEDED(file->Save(shortcutPath.c_str(), TRUE));
        file->Release();
    }

    link->Release();

    if (comInitialized)
        CoUninitialize();

    return result;
}

bool DisableStartupShortcut()
{
    wchar_t startupDir[MAX_PATH];

    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupDir)))
        return false;

    std::wstring shortcutPath = std::wstring(startupDir) + L"\\" + APPNAME + L".lnk";

    return DeleteFileW(shortcutPath.c_str()) != FALSE;
}

bool IsAutostartEnabled()
{
    wchar_t startupDir[MAX_PATH];

    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, 0, startupDir)))
        return false;

    std::wstring shortcutPath = std::wstring(startupDir) + L"\\" + APPNAME + L".lnk";

    DWORD attributes = GetFileAttributesW(shortcutPath.c_str());

    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}