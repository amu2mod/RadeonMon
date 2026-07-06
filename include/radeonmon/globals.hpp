#pragma once

#include <windows.h>

#include <cstring>

#include "radeonmon/structures.hpp"
#include "radeonmon/adlx.hpp"
#include "AMD/ADLX-1.5/SDK/Include/IPerformanceMonitoring3.h"

inline UINT g_dpi = 96;
inline HFONT g_font = nullptr;
inline GdiBackBuffer g_backBuffer;
inline PropertyItem g_props[] =
    {
        {L"GPU Temperature", 0, L""},
        {nullptr, 0, L"", {}, {}, PropertyType::Separator},
        {L"GPU Hotspot", 0, L""},
        {nullptr, 0, L"", {}, {}, PropertyType::Separator},
        {L"VRAM Temperature", 0, L""},
        {nullptr, 0, L"", {}, {}, PropertyType::Separator},
        {L"Fan Speed", -1, L""},
        {nullptr, 0, L"", {}, {}, PropertyType::Separator},
        {L"Power Consumption", 0, L""}};
constexpr int g_propCount = _countof(g_props);
inline PropertyItem g_cardName = {L"", 0, L"no card detected"};

inline ADLXGpuTelemetry g_AdlxGPUTelemetry;

inline bool IsAMDVendor(const char *vendorId)
{
    if (vendorId == nullptr)
        return false;

    return std::strcmp(vendorId, "1002") == 0;
}
