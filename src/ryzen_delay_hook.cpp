#include <windows.h>
#include "AMD\RyzenMasterMonitoringSDK\include\IPlatform.h"

typedef IPlatform &(__stdcall *GetPlatformFunc)();

IPlatform *LoadPlatform()
{
    HMODULE hMod = LoadLibraryW(L"C:\\Program Files\\AMD\\RyzenMasterMonitoringSDK\\bin\\Platform.dll");
    if (!hMod)
        return nullptr;

    GetPlatformFunc pGetPlatform =
        reinterpret_cast<GetPlatformFunc>(GetProcAddress(hMod, "GetPlatform"));
    if (!pGetPlatform)
        return nullptr;

    return &pGetPlatform();
}