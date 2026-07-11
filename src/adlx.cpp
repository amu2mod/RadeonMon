#include <cmath>

#include "radeonmon/adlx.hpp"
#include "AMD/ADLX-1.5/SDK/Include/ADLX.h"
#include "AMD/ADLX-1.5/SDK/Include/ISystem3.h"

#include "radeonmon/globals.hpp"
#include "radeonmon/helpers.hpp"
#include "radeonmon/logging.hpp"

void ADLXGpuTelemetry::Init()
{
    ADLX_RESULT res = ADLX_FAIL;

    selectedGPU = nullptr;
    gpuMetricsSupport = nullptr;
    isInitialized = false;

    res = ADLXHelp.Initialize();
    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("ADLX initialization failed");
        g_cardName.SetValue(L"ADLX init failed");
        return;
    }

    LOG_DEBUG("ADLX Init succeeded");

    res = ADLXHelp.GetSystemServices()->GetPerformanceMonitoringServices(&perfMonitoringService);
    if (!ADLX_SUCCEEDED(res) || !perfMonitoringService)
    {
        LOG_ERROR("Get performance monitoring services failed");
        g_cardName.SetValue(L"ADLX perf services failed");
        return;
    }

    LOG_DEBUG("ADLX GetPerformanceMonitoringServices succeeded");

    IADLXGPUListPtr gpus;
    res = ADLXHelp.GetSystemServices()->GetGPUs(&gpus);
    if (!ADLX_SUCCEEDED(res) || !gpus || gpus->Empty())
    {
        LOG_ERROR("Failed to get the GPU list");
        g_cardName.SetValue(L"No GPU found");
        return;
    }

    LOG_DEBUG("ADLX Traversing GPU list");

    adlx_uint count = gpus->Size();

    IADLXGPUPtr amdGpuFallback = nullptr;

    for (adlx_uint i = 0; i < count; i++)
    {
        IADLXGPUPtr gpu = nullptr;

        res = gpus->At(i, &gpu);
        if (!ADLX_SUCCEEDED(res) || !gpu)
            continue;

        const char *vendorId = nullptr;
        gpu->VendorId(&vendorId);

        if (!IsAMDVendor(vendorId))
            continue;

        ADLX_ASIC_FAMILY_TYPE asicFamily = ASIC_UNDEFINED;
        gpu->ASICFamilyType(&asicFamily);

        if (asicFamily == ASIC_RADEON)
        {
            // Discrete Radeon GPU — take it immediately.
            selectedGPU = gpu;

            break;
        }

        // Remember the first AMD GPU as a fallback.
        if (!amdGpuFallback)
        {
            amdGpuFallback = gpu;
        }
    }

    if (!selectedGPU && amdGpuFallback)
    {
        selectedGPU = amdGpuFallback;
        LOG_WARN("No discrete Radeon GPU found, falling back to integrated/embedded AMD GPU");
    }

    if (!selectedGPU)
    {
        LOG_WARN("No AMD GPU found");
        g_cardName.SetValue(L"No AMD GPU found");
        return;
    }

    PopulateGPUInfo(selectedGPU);
    gpuInfo.Log();

    g_cardName.SetValue(gpuInfo.name.empty() ? L"Unknown AMD GPU" : gpuInfo.name.c_str());

    LOG_INFO("Selected AMD GPU: %ls", gpuInfo.name.empty() ? L"Unknown" : gpuInfo.name.c_str());
    LOG_INFO("Vendor ID: %ls", gpuInfo.vendorId.empty() ? L"Unknown" : gpuInfo.vendorId.c_str());

    res = perfMonitoringService->GetSupportedGPUMetrics(selectedGPU, &gpuMetricsSupport);
    if (!ADLX_SUCCEEDED(res) || !gpuMetricsSupport)
    {
        LOG_ERROR("gpuMetricsSupport failed");
        g_cardName.SetValue(L"GPU metrics not supported");
        gpuMetricsSupport = nullptr;
        return;
    }

    gpuMetricsSupport1 = IADLXGPUMetricsSupport1Ptr(gpuMetricsSupport);
    gpuMetricsSupport2 = IADLXGPUMetricsSupport2Ptr(gpuMetricsSupport);
    gpuMetricsSupport3 = IADLXGPUMetricsSupport3Ptr(gpuMetricsSupport);

    LOG_DEBUG("gpuMetricsSupport OK");

    isInitialized = true;
}

// void ADLXGpuTelemetry::Discover()
// {
//     if (!gpuMetricsSupport)
//     {
//         LOG_INFO("GPU Metrics Support not initialized");
//         return;
//     }

//     gpuCaps = 0;

//     adlx_bool supported = false;
//     ADLX_RESULT res = ADLX_FAIL;

//     LOG_INFO("");
//     LOG_INFO("=== ADLX GPU Metrics Support Discovery ===");

//     // ===== V0 =====

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUUsage(&supported);
//     LOG_INFO("GPU Usage: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_USAGE;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUClockSpeed(&supported);
//     LOG_INFO("GPU Clock Speed: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_CLOCK;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUVRAMClockSpeed(&supported);
//     LOG_INFO("VRAM Clock Speed: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_VRAM_CLOCK;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUTemperature(&supported);
//     LOG_INFO("GPU Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_TEMP;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUHotspotTemperature(&supported);
//     LOG_INFO("GPU Hotspot Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_HOTSPOT;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUPower(&supported);
//     LOG_INFO("GPU Power: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_POWER;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUTotalBoardPower(&supported);
//     LOG_INFO("GPU Total Board Power: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_BOARD_POWER;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUFanSpeed(&supported);
//     LOG_INFO("GPU Fan Speed: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_FAN_SPEED;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUVRAM(&supported);
//     LOG_INFO("GPU VRAM Usage: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_VRAM_USAGE;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUVoltage(&supported);
//     LOG_INFO("GPU Voltage: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_VOLTAGE;

//     supported = false;
//     res = gpuMetricsSupport->IsSupportedGPUIntakeTemperature(&supported);
//     LOG_INFO("GPU Intake Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//     if (ADLX_SUCCEEDED(res) && supported)
//         gpuCaps |= GPU_CAP_INTAKE_TEMP;

//     // ===== V1 =====

//     if (gpuMetricsSupport1)
//     {
//         supported = false;
//         res = gpuMetricsSupport1->IsSupportedGPUMemoryTemperature(&supported);
//         LOG_INFO("GPU Memory Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//         if (ADLX_SUCCEEDED(res) && supported)
//             gpuCaps |= GPU_CAP_MEM_TEMP;

//         supported = false;
//         res = gpuMetricsSupport1->IsSupportedNPUFrequency(&supported);
//         LOG_INFO("NPU Frequency: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//         if (ADLX_SUCCEEDED(res) && supported)
//             gpuCaps |= GPU_CAP_NPU_FREQ;

//         supported = false;
//         res = gpuMetricsSupport1->IsSupportedNPUActivityLevel(&supported);
//         LOG_INFO("NPU Activity Level: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//         if (ADLX_SUCCEEDED(res) && supported)
//             gpuCaps |= GPU_CAP_NPU_ACTIVITY;
//     }

//     // ===== V2 =====

//     if (gpuMetricsSupport2)
//     {
//         supported = false;
//         res = gpuMetricsSupport2->IsSupportedGPUSharedMemory(&supported);
//         LOG_INFO("GPU Shared Memory: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//         if (ADLX_SUCCEEDED(res) && supported)
//             gpuCaps |= GPU_CAP_SHARED_MEMORY;
//     }

//     // ===== V3 =====

//     if (gpuMetricsSupport3)
//     {
//         supported = false;
//         res = gpuMetricsSupport3->IsSupportedGPUFanDuty(&supported);
//         LOG_INFO("GPU Fan Duty: %s", (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");
//         if (ADLX_SUCCEEDED(res) && supported)
//             gpuCaps |= GPU_CAP_FAN_DUTY;
//     }

//     LOG_INFO("=== End of Discovery ===");
//     LOG_INFO("");
// }

void ADLXGpuTelemetry::Discover()
{
    if (!gpuMetricsSupport)
    {
        LOG_INFO("GPU Metrics Support not initialized");
        return;
    }

    gpuCaps = 0;
    // discoveredMetrics = {};
    GpuMetricsSnapshot &discoveredMetrics = m_snapshot;

    LOG_INFO("");
    LOG_INFO("=== ADLX GPU Metrics Support Discovery ===");

    auto discoverV0 = [&](const char *name,
                          auto isSupportedFn,
                          auto getRangeFn,
                          uint32_t cap,
                          auto &metric)
    {
        adlx_bool supported = false;
        ADLX_RESULT res = (gpuMetricsSupport->*isSupportedFn)(&supported);

        LOG_INFO("%s: %s", name,
                 (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");

        if (!ADLX_SUCCEEDED(res) || !supported)
            return;

        gpuCaps |= cap;
        metric.isSupported = true;

        adlx_int min = 0, max = 0;
        res = (gpuMetricsSupport3->*getRangeFn)(&min, &max);

        if (ADLX_SUCCEEDED(res))
        {
            metric.min = min;
            metric.max = max;
        }
    };

    auto discoverV1 = [&](const char *name,
                          auto isSupportedFn,
                          auto getRangeFn,
                          uint32_t cap,
                          auto &metric)
    {
        if (!gpuMetricsSupport1)
            return;

        adlx_bool supported = false;
        ADLX_RESULT res = (gpuMetricsSupport1->*isSupportedFn)(&supported);

        LOG_INFO("%s: %s", name,
                 (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");

        if (!ADLX_SUCCEEDED(res) || !supported)
            return;

        gpuCaps |= cap;
        metric.isSupported = true;

        adlx_int min = 0, max = 0;
        res = (gpuMetricsSupport1->*getRangeFn)(&min, &max);

        if (ADLX_SUCCEEDED(res))
        {
            metric.min = min;
            metric.max = max;
        }
    };

    auto discoverV3 = [&](const char *name,
                          auto isSupportedFn,
                          auto getRangeFn,
                          uint32_t cap,
                          auto &metric)
    {
        if (!gpuMetricsSupport3)
            return;

        adlx_bool supported = false;
        ADLX_RESULT res = (gpuMetricsSupport3->*isSupportedFn)(&supported);

        LOG_INFO("%s: %s", name,
                 (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");

        if (!ADLX_SUCCEEDED(res) || !supported)
            return;

        gpuCaps |= cap;
        metric.isSupported = true;

        adlx_int min = 0, max = 0;
        res = (gpuMetricsSupport3->*getRangeFn)(&min, &max);

        if (ADLX_SUCCEEDED(res))
        {
            metric.min = min;
            metric.max = max;
        }
    };

    // ===== V0 =====

    discoverV0("GPU Usage",
               &IADLXGPUMetricsSupport::IsSupportedGPUUsage,
               &IADLXGPUMetricsSupport3::GetGPUUsageRange,
               GPU_CAP_USAGE,
               discoveredMetrics.usage);

    discoverV0("GPU Clock Speed",
               &IADLXGPUMetricsSupport::IsSupportedGPUClockSpeed,
               &IADLXGPUMetricsSupport3::GetGPUClockSpeedRange,
               GPU_CAP_CLOCK,
               discoveredMetrics.clockSpeed);

    discoverV0("VRAM Clock Speed",
               &IADLXGPUMetricsSupport::IsSupportedGPUVRAMClockSpeed,
               &IADLXGPUMetricsSupport3::GetGPUVRAMClockSpeedRange,
               GPU_CAP_VRAM_CLOCK,
               discoveredMetrics.vramClockSpeed);

    discoverV0("GPU Temperature",
               &IADLXGPUMetricsSupport::IsSupportedGPUTemperature,
               &IADLXGPUMetricsSupport3::GetGPUTemperatureRange,
               GPU_CAP_TEMP,
               discoveredMetrics.temperature);

    discoverV0("GPU Hotspot Temperature",
               &IADLXGPUMetricsSupport::IsSupportedGPUHotspotTemperature,
               &IADLXGPUMetricsSupport3::GetGPUHotspotTemperatureRange,
               GPU_CAP_HOTSPOT,
               discoveredMetrics.hotspot);

    discoverV0("GPU Power",
               &IADLXGPUMetricsSupport::IsSupportedGPUPower,
               &IADLXGPUMetricsSupport3::GetGPUPowerRange,
               GPU_CAP_POWER,
               discoveredMetrics.power);

    discoverV0("GPU Total Board Power",
               &IADLXGPUMetricsSupport::IsSupportedGPUTotalBoardPower,
               &IADLXGPUMetricsSupport3::GetGPUTotalBoardPowerRange,
               GPU_CAP_BOARD_POWER,
               discoveredMetrics.totalBoardPower);

    discoverV0("GPU Fan Speed",
               &IADLXGPUMetricsSupport::IsSupportedGPUFanSpeed,
               &IADLXGPUMetricsSupport3::GetGPUFanSpeedRange,
               GPU_CAP_FAN_SPEED,
               discoveredMetrics.fanSpeed);

    discoverV0("GPU VRAM",
               &IADLXGPUMetricsSupport::IsSupportedGPUVRAM,
               &IADLXGPUMetricsSupport3::GetGPUVRAMRange,
               GPU_CAP_VRAM_USAGE,
               discoveredMetrics.vram);

    discoverV0("GPU Voltage",
               &IADLXGPUMetricsSupport::IsSupportedGPUVoltage,
               &IADLXGPUMetricsSupport3::GetGPUVoltageRange,
               GPU_CAP_VOLTAGE,
               discoveredMetrics.voltage);

    discoverV0("GPU Intake Temperature",
               &IADLXGPUMetricsSupport::IsSupportedGPUIntakeTemperature,
               &IADLXGPUMetricsSupport3::GetGPUIntakeTemperatureRange,
               GPU_CAP_INTAKE_TEMP,
               discoveredMetrics.intakeTemperature);

    // ===== V1 =====

    discoverV1("GPU Memory Temperature",
               &IADLXGPUMetricsSupport1::IsSupportedGPUMemoryTemperature,
               &IADLXGPUMetricsSupport1::GetGPUMemoryTemperatureRange,
               GPU_CAP_MEM_TEMP,
               discoveredMetrics.memoryTemperature);

    discoverV1("NPU Frequency",
               &IADLXGPUMetricsSupport1::IsSupportedNPUFrequency,
               &IADLXGPUMetricsSupport1::GetNPUFrequencyRange,
               GPU_CAP_NPU_FREQ,
               discoveredMetrics.npuFrequency);

    discoverV1("NPU Activity Level",
               &IADLXGPUMetricsSupport1::IsSupportedNPUActivityLevel,
               &IADLXGPUMetricsSupport1::GetNPUActivityLevelRange,
               GPU_CAP_NPU_ACTIVITY,
               discoveredMetrics.npuActivityLevel);

    // ===== V3 =====

    discoverV3("GPU Shared Memory",
               &IADLXGPUMetricsSupport3::IsSupportedGPUSharedMemory,
               &IADLXGPUMetricsSupport3::GetGPUSharedMemoryRange,
               GPU_CAP_SHARED_MEMORY,
               discoveredMetrics.sharedMemory);

    discoverV3("GPU Fan Duty",
               &IADLXGPUMetricsSupport3::IsSupportedGPUFanDuty,
               &IADLXGPUMetricsSupport3::GetGPUFanDutyRange,
               GPU_CAP_FAN_DUTY,
               discoveredMetrics.fanDuty);

    LOG_INFO("=== End of Discovery ===");
    LOG_INFO("");
}

void ADLXGpuTelemetry::Destroy()
{
    ADLX_RESULT res = ADLXHelp.Terminate();
    if (ADLX_FAILED(res))
    {
        LOG_ERROR("Failed to terminate ADLX Helper: %d", res);
    }
}

template <typename T, typename MetricFn>
int ADLXGpuTelemetry::ReadMetric(const char *name, MetricFn metricFn, IADLXGPUMetrics *metrics)
{
    T value{};

    ADLX_RESULT res = (metrics->*metricFn)(&value);

    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("%s query failed: %d", name, res);
        return AdlxStates::Error;
    }

    // only convert at the boundary if needed
    if constexpr (std::is_floating_point_v<T>)
    {
        return static_cast<int>(std::round(value));
    }
    else
    {
        return static_cast<int>(value);
    }
}

template <typename T, typename MetricFn>
int ADLXGpuTelemetry::ReadMetricV1(const char *name, MetricFn metricFn, IADLXGPUMetrics *gpuMetrics)
{
    IADLXGPUMetrics1Ptr metrics1(gpuMetrics);

    if (!gpuMetricsSupport1 || !metrics1)
    {
        LOG_WARN("%s v1 interface unavailable", name);
        return AdlxStates::Error;
    }

    T value{};

    ADLX_RESULT res = (metrics1->*metricFn)(&value);

    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("%s query failed: %d", name, res);
        return AdlxStates::Error;
    }

    if constexpr (std::is_floating_point_v<T>)
    {
        return static_cast<int>(std::lround(value));
    }
    else
    {
        return static_cast<int>(value);
    }
}

template <typename T, typename MetricFn>
int ADLXGpuTelemetry::ReadMetricV3(const char *name, MetricFn metricFn, IADLXGPUMetrics *gpuMetrics)
{
    IADLXGPUMetrics3Ptr metrics3(gpuMetrics);

    if (!gpuMetricsSupport3 || !metrics3)
    {
        LOG_WARN("%s v3 interface unavailable", name);
        return AdlxStates::Error;
    }

    T value{};

    ADLX_RESULT res = (metrics3->*metricFn)(&value);

    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("%s query failed: %d", name, res);
        return AdlxStates::Error;
    }

    if constexpr (std::is_floating_point_v<T>)
    {
        return static_cast<int>(std::lround(value));
    }
    else
    {
        return static_cast<int>(value);
    }
}

GpuMetricsSnapshot ADLXGpuTelemetry::Query()
{
    GpuMetricsSnapshot snapshot = m_snapshot;

    if (perfMonitoringService == nullptr)
    {
        LOG_ERROR("perfMonitoringService not initialized");
        return snapshot;
    }

    IADLXGPUMetricsPtr gpuMetrics;
    ADLX_RESULT res = perfMonitoringService->GetCurrentGPUMetrics(selectedGPU, &gpuMetrics);

    if (ADLX_SUCCEEDED(res))
    {
        bool anyMetricRead = false;

        auto readDouble = [&](const char *name, GPU_CAPS cap, MetricDouble &metric, auto fn)
        {
            if (!IsEnabled(cap))
                return;

            metric.isSupported = true;
            metric.value = ReadMetric<adlx_double>(name, fn, gpuMetrics);

            anyMetricRead = true;
        };

        auto readInt = [&](const char *name, GPU_CAPS cap, MetricInt &metric, auto fn)
        {
            if (!IsEnabled(cap))
                return;

            metric.isSupported = true;
            metric.value = ReadMetric<adlx_int>(name, fn, gpuMetrics);

            anyMetricRead = true;
        };

        auto readDoubleV1 = [&](const char *name, GPU_CAPS cap, MetricDouble &metric, auto fn)
        {
            if (!IsEnabled(cap))
                return;

            metric.isSupported = true;
            metric.value = ReadMetricV1<adlx_double>(name, fn, gpuMetrics);

            anyMetricRead = true;
        };

        auto readIntV1 = [&](const char *name, GPU_CAPS cap, MetricInt &metric, auto fn)
        {
            if (!IsEnabled(cap))
                return;

            metric.isSupported = true;
            metric.value = ReadMetricV1<adlx_int>(name, fn, gpuMetrics);

            anyMetricRead = true;
        };

        auto readIntV3 = [&](const char *name, GPU_CAPS cap, MetricInt &metric, auto fn)
        {
            if (!IsEnabled(cap))
                return;

            metric.isSupported = true;
            metric.value = ReadMetricV3<adlx_int>(name, fn, gpuMetrics);

            anyMetricRead = true;
        };

        // GPU usage
        readDouble("GPUUsage", GPU_CAP_USAGE, snapshot.usage, &IADLXGPUMetrics::GPUUsage);

        // GPU clocks
        readInt("GPUClockSpeed",
                GPU_CAP_CLOCK,
                snapshot.clockSpeed,
                &IADLXGPUMetrics::GPUClockSpeed);

        readInt("GPUVRAMClockSpeed",
                GPU_CAP_VRAM_CLOCK,
                snapshot.vramClockSpeed,
                &IADLXGPUMetrics::GPUVRAMClockSpeed);

        // Temperatures
        readDouble("GPUTemperature",
                   GPU_CAP_TEMP,
                   snapshot.temperature,
                   &IADLXGPUMetrics::GPUTemperature);

        readDouble("GPUHotspotTemperature",
                   GPU_CAP_HOTSPOT,
                   snapshot.hotspot,
                   &IADLXGPUMetrics::GPUHotspotTemperature);

        // Memory temperature
        readDoubleV1("GPUMemoryTemperature",
                     GPU_CAP_MEM_TEMP,
                     snapshot.memoryTemperature,
                     &IADLXGPUMetrics1::GPUMemoryTemperature);

        // Intake temperature
        readDouble("GPUIntakeTemperature",
                   GPU_CAP_INTAKE_TEMP,
                   snapshot.intakeTemperature,
                   &IADLXGPUMetrics::GPUIntakeTemperature);

        // Power
        readDouble("GPUPower",
                   GPU_CAP_POWER,
                   snapshot.power,
                   &IADLXGPUMetrics::GPUPower);

        readDouble("GPUTotalBoardPower",
                   GPU_CAP_BOARD_POWER,
                   snapshot.totalBoardPower,
                   &IADLXGPUMetrics::GPUTotalBoardPower);

        // Fan
        readInt("GPUFanSpeed",
                GPU_CAP_FAN_SPEED,
                snapshot.fanSpeed,
                &IADLXGPUMetrics::GPUFanSpeed);

        readIntV3("GPUFanDuty",
                  GPU_CAP_FAN_DUTY,
                  snapshot.fanDuty,
                  &IADLXGPUMetrics3::GPUFanDuty);

        // Memory
        readInt("GPUVRAM",
                GPU_CAP_VRAM_USAGE,
                snapshot.vram,
                &IADLXGPUMetrics::GPUVRAM);

        readIntV3("GPUSharedMemory",
                  GPU_CAP_SHARED_MEMORY,
                  snapshot.sharedMemory,
                  &IADLXGPUMetrics3::GPUSharedMemory);

        // Voltage
        readInt("GPUVoltage",
                GPU_CAP_VOLTAGE,
                snapshot.voltage,
                &IADLXGPUMetrics::GPUVoltage);

        // NPU
        readIntV1("NPUFrequency",
                  GPU_CAP_NPU_FREQ,
                  snapshot.npuFrequency,
                  &IADLXGPUMetrics1::NPUFrequency);

        readIntV1("NPUActivityLevel",
                  GPU_CAP_NPU_ACTIVITY,
                  snapshot.npuActivityLevel,
                  &IADLXGPUMetrics1::NPUActivityLevel);

        // Timestamp
        if (gpuMetrics)
        {
            gpuMetrics->TimeStamp(&snapshot.timestampMs);
        }

        snapshot.valid = anyMetricRead;
    }
    else
    {
        snapshot.valid = false;
        LOG_ERROR("GetCurrentGPUMetrics failed");
        g_cardName.SetValue(L"Current metrics failed");
    }

    return snapshot;
}

// GpuMetricsSnapshot ADLXGpuTelemetry::Query()
// {
//     GpuMetricsSnapshot snapshot;

//     if (perfMonitoringService == nullptr)
//     {
//         LOG_ERROR("perfMonitoringService not initialized");
//         return snapshot;
//     }

//     // Get current GPU metrics
//     IADLXGPUMetricsPtr gpuMetrics;
//     ADLX_RESULT res = perfMonitoringService->GetCurrentGPUMetrics(selectedGPU, &gpuMetrics);

//     if (ADLX_SUCCEEDED(res))
//     {
//         bool anyMetricRead = false;

//         // GPU temp
//         if (IsEnabled(GPU_CAP_TEMP))
//         {
//             snapshot.temperature = ReadMetric<adlx_double>("GPUTemperature", &IADLXGPUMetrics::GPUTemperature, gpuMetrics);
//             anyMetricRead = true;
//         }

//         // Hotspot
//         if (IsEnabled(GPU_CAP_HOTSPOT))
//         {
//             snapshot.hotspot = ReadMetric<adlx_double>("HotspotTemperature", &IADLXGPUMetrics::GPUHotspotTemperature, gpuMetrics);
//             anyMetricRead = true;
//         }

//         // VRAM Temp
//         if (IsEnabled(GPU_CAP_MEM_TEMP))
//         {
//             snapshot.vram = ReadMetricV1<adlx_double>("GPUMemoryTemperature", &IADLXGPUMetrics1::GPUMemoryTemperature, gpuMetrics);
//             anyMetricRead = true;
//         }

//         // Fan Speed
//         if (IsEnabled(GPU_CAP_FAN_SPEED))
//         {
//             snapshot.fanSpeed = ReadMetric<adlx_int>("GPUFanSpeed", &IADLXGPUMetrics::GPUFanSpeed, gpuMetrics);
//             anyMetricRead = true;
//         }

//         // Power Consumption
//         if (IsEnabled(GPU_CAP_BOARD_POWER))
//         {
//             snapshot.power = ReadMetric<adlx_double>("GPUTotalBoardPower", &IADLXGPUMetrics::GPUTotalBoardPower, gpuMetrics);
//             anyMetricRead = true;
//         }

//         snapshot.valid = anyMetricRead;
//     }
//     else
//     {
//         snapshot.valid = false;
//         LOG_ERROR("GetCurrentGPUMetrics failed");
//         g_cardName.SetValue(L"Current metrics failed");
//     }

//     return snapshot;
// }

void ADLXGpuTelemetry::PopulateGPUInfo(IADLXGPU *gpu)
{
    if (!gpu)
        return;

    const char *value = nullptr;

    // Identification
    value = nullptr;
    if (gpu->VendorId(&value) == ADLX_OK)
    {
        gpuInfo.SetVendorId(value);
    }

    value = nullptr;
    if (gpu->DeviceId(&value) == ADLX_OK)
    {
        gpuInfo.SetDeviceId(value);
    }

    value = nullptr;
    if (gpu->RevisionId(&value) == ADLX_OK)
    {
        gpuInfo.SetRevisionId(value);
    }

    value = nullptr;
    if (gpu->SubSystemId(&value) == ADLX_OK)
    {
        gpuInfo.SetSubSystemId(value);
    }

    value = nullptr;
    if (gpu->SubSystemVendorId(&value) == ADLX_OK)
    {
        gpuInfo.SetSubSystemVendorId(value);
    }

    gpu->UniqueId(&gpuInfo.uniqueId);

    // General information
    gpu->ASICFamilyType(&gpuInfo.asicFamilyType);
    gpu->Type(&gpuInfo.gpuType);
    gpu->IsExternal(&gpuInfo.isExternal);

    // Descriptive information
    value = nullptr;
    if (gpu->Name(&value) == ADLX_OK)
    {
        gpuInfo.SetName(value);
    }

    value = nullptr;
    if (gpu->DriverPath(&value) == ADLX_OK)
    {
        gpuInfo.SetDriverPath(value);
    }

    value = nullptr;
    if (gpu->PNPString(&value) == ADLX_OK)
    {
        gpuInfo.SetPnpString(value);
    }

    // Memory
    gpu->TotalVRAM(&gpuInfo.totalVRAMMB);

    value = nullptr;
    if (gpu->VRAMType(&value) == ADLX_OK)
    {
        gpuInfo.SetVramType(value);
    }

    // BIOS
    const char *biosPartNumber = nullptr;
    const char *biosVersion = nullptr;
    const char *biosDate = nullptr;

    if (gpu->BIOSInfo(&biosPartNumber, &biosVersion, &biosDate) == ADLX_OK)
    {
        gpuInfo.SetBiosPartNumber(biosPartNumber);
        gpuInfo.SetBiosVersion(biosVersion);
        gpuInfo.SetBiosDate(biosDate);
    }

    // Status
    gpu->HasDesktops(&gpuInfo.hasDesktops);

    // Tooltip
    gpuInfo.BuildToolTip();
}