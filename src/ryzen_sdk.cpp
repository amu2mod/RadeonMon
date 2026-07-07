#include <windows.h>
#include <string>
#include <vector>

#include "AMD\RyzenMasterMonitoringSDK\include\IPlatform.h"
#include "radeonmon/logging.hpp"

// Function pointer types based on the SDK's API (check the SDK readme/sample for exact signatures)
typedef IPlatform &(__stdcall *GetPlatformFunc)();

HMODULE hPlatform = nullptr;
HMODULE hDevice = nullptr;

std::wstring FindSDKPath()
{
    std::wstring path;

    // === 1. Try Registry (most reliable) ===
    HKEY hKey = nullptr;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\AMD\\RyzenMasterMonitoringSDK", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
    {
        wchar_t buffer[MAX_PATH] = {0};
        DWORD size = sizeof(buffer);

        if (RegQueryValueExW(hKey, L"InstallationPath", nullptr, nullptr, (LPBYTE)buffer, &size) == ERROR_SUCCESS)
        {
            LOG_DEBUG("Registry key found for AMD Ryzen SDK");
            path = buffer;
            // Clean up trailing backslash if present
            if (!path.empty() && path.back() == L'\\')
            {
                path.pop_back();
            }
        }
        RegCloseKey(hKey);
    }

    if (!path.empty())
    {
        // Verify the DLLs actually exist there
        if (GetFileAttributesW((path + L"\\bin\\device.dll").c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW((path + L"\\device.dll").c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return path;
        }
    }

    // === 2. Fallback: Common hard-coded paths ===
    std::vector<std::wstring> candidates = {
        L"C:\\Program Files\\AMD\\RyzenMasterMonitoringSDK",
        L"C:\\Program Files (x86)\\AMD\\RyzenMasterMonitoringSDK",
        L"C:\\Program Files\\AMD\\Performance Profile Client\\RyzenMaster",
        L"C:\\Program Files\\AMD\\Ryzen Master SDK"};

    for (const auto &p : candidates)
    {
        std::wstring testPath = p;
        if (GetFileAttributesW((testPath + L"\\bin\\device.dll").c_str()) != INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW((testPath + L"\\device.dll").c_str()) != INVALID_FILE_ATTRIBUTES)
        {
            return testPath;
        }
    }

    return L""; // Not found
}

bool LoadAMDDLLs()
{
    std::wstring sdkPath = FindSDKPath();
    if (sdkPath.empty())
    {
        LOG_ERROR("AMD Ryzen Master Monitoring SDK not found");
        return false;
    }

    // !!! IMPORTANT: we must add the SDK bin folder to the DLL search path FIRST
    // old version of device.dll doesn't give correct data
    std::wstring binPath = sdkPath + L"\\bin";
    SetDllDirectoryW(binPath.c_str());

    std::wstring platformPath = sdkPath + L"\\bin\\platform.dll";
    std::wstring devicePath = sdkPath + L"\\bin\\device.dll";

    hPlatform = LoadLibraryW(platformPath.c_str());
    if (!hPlatform)
        return false;

    hDevice = LoadLibraryW(devicePath.c_str());
    if (!hDevice)
    {
        FreeLibrary(hPlatform);
        return false;
    }

    // Get function pointers, e.g.
    LOG_DEBUG("Ryzen SDK DLLs found and loaded");

    return true;
}

void UnloadAMDDLLs()
{
    if (hDevice)
        FreeLibrary(hDevice);
    if (hPlatform)
        FreeLibrary(hPlatform);
}