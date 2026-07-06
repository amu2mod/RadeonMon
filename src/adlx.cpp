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

    const char *amdGpuName = nullptr;
    const char *amdVendorId = nullptr;

    res = ADLXHelp.Initialize();
    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("ADLX initialization failed");
        g_cardName.SetValue("ADLX init failed");
        return;
    }

    LOG_DEBUG("ADLX Init succeeded");

    res = ADLXHelp.GetSystemServices()->GetPerformanceMonitoringServices(&perfMonitoringService);
    if (!ADLX_SUCCEEDED(res) || !perfMonitoringService)
    {
        LOG_ERROR("Get performance monitoring services failed");
        g_cardName.SetValue("ADLX perf services failed");
        return;
    }

    LOG_DEBUG("ADLX GetPerformanceMonitoringServices succeeded");

    IADLXGPUListPtr gpus;
    res = ADLXHelp.GetSystemServices()->GetGPUs(&gpus);
    if (!ADLX_SUCCEEDED(res) || !gpus || gpus->Empty())
    {
        LOG_ERROR("Failed to get the GPU list");
        g_cardName.SetValue("No GPU found");
        return;
    }

    LOG_DEBUG("ADLX Traversing GPU list");

    adlx_uint count = gpus->Size();

    for (adlx_uint i = 0; i < count; i++)
    {
        IADLXGPUPtr gpu = nullptr;
        const char *vendorId = nullptr;
        const char *gpuName = nullptr;

        res = gpus->At(i, &gpu);
        if (!ADLX_SUCCEEDED(res) || !gpu)
            continue;

        gpu->VendorId(&vendorId);

        if (IsAMDVendor(vendorId))
        {
            gpu->Name(&gpuName);

            selectedGPU = gpu;
            amdGpuName = gpuName;
            amdVendorId = vendorId;

            break;
        }
    }

    if (!selectedGPU)
    {
        LOG_WARN("No AMD GPU found");
        g_cardName.SetValue("No AMD GPU found");
        return;
    }

    g_cardName.SetValue(amdGpuName ? amdGpuName : "Unknown AMD GPU");

    LOG_INFO("Selected AMD GPU: %s", amdGpuName ? amdGpuName : "Unknown");
    LOG_INFO("Vendor ID: %s", amdVendorId ? amdVendorId : "Unknown");

    res = perfMonitoringService->GetSupportedGPUMetrics(selectedGPU, &gpuMetricsSupport);
    if (!ADLX_SUCCEEDED(res) || !gpuMetricsSupport)
    {
        LOG_ERROR("gpuMetricsSupport failed");
        g_cardName.SetValue("GPU metrics not supported");
        gpuMetricsSupport = nullptr;
        return;
    }

    gpuMetricsSupport1 = IADLXGPUMetricsSupport1Ptr(gpuMetricsSupport);
    gpuMetricsSupport2 = IADLXGPUMetricsSupport2Ptr(gpuMetricsSupport);
    gpuMetricsSupport3 = IADLXGPUMetricsSupport3Ptr(gpuMetricsSupport);

    LOG_DEBUG("gpuMetricsSupport OK");

    isInitialized = true;
}

void ADLXGpuTelemetry::Discover()
{
    if (!gpuMetricsSupport)
    {
        LOG_INFO("GPU Metrics Support not initialized");
        return;
    }

    gpuCaps = 0;

    adlx_bool supported = false;
    ADLX_RESULT res = ADLX_FAIL;

    LOG_INFO("=== ADLX GPU Metrics Support Discovery ===");

    // ===== V0 =====

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUUsage(&supported);
    LOG_INFO("GPU Usage: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_USAGE;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUClockSpeed(&supported);
    LOG_INFO("GPU Clock Speed: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_CLOCK;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUVRAMClockSpeed(&supported);
    LOG_INFO("VRAM Clock Speed: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_VRAM_CLOCK;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUTemperature(&supported);
    LOG_INFO("GPU Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_TEMP;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUHotspotTemperature(&supported);
    LOG_INFO("GPU Hotspot Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_HOTSPOT;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUPower(&supported);
    LOG_INFO("GPU Power: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_POWER;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUTotalBoardPower(&supported);
    LOG_INFO("GPU Total Board Power: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_BOARD_POWER;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUFanSpeed(&supported);
    LOG_INFO("GPU Fan Speed: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_FAN_SPEED;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUVRAM(&supported);
    LOG_INFO("GPU VRAM Usage: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_VRAM_USAGE;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUVoltage(&supported);
    LOG_INFO("GPU Voltage: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_VOLTAGE;

    supported = false;
    res = gpuMetricsSupport->IsSupportedGPUIntakeTemperature(&supported);
    LOG_INFO("GPU Intake Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
    if (ADLX_SUCCEEDED(res) && supported)
        gpuCaps |= GPU_CAP_INTAKE_TEMP;

    // ===== V1 =====

    if (gpuMetricsSupport1)
    {
        supported = false;
        res = gpuMetricsSupport1->IsSupportedGPUMemoryTemperature(&supported);
        LOG_INFO("GPU Memory Temperature: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
        if (ADLX_SUCCEEDED(res) && supported)
            gpuCaps |= GPU_CAP_MEM_TEMP;

        supported = false;
        res = gpuMetricsSupport1->IsSupportedNPUFrequency(&supported);
        LOG_INFO("NPU Frequency: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
        if (ADLX_SUCCEEDED(res) && supported)
            gpuCaps |= GPU_CAP_NPU_FREQ;

        supported = false;
        res = gpuMetricsSupport1->IsSupportedNPUActivityLevel(&supported);
        LOG_INFO("NPU Activity Level: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
        if (ADLX_SUCCEEDED(res) && supported)
            gpuCaps |= GPU_CAP_NPU_ACTIVITY;
    }

    // ===== V2 =====

    if (gpuMetricsSupport2)
    {
        supported = false;
        res = gpuMetricsSupport2->IsSupportedGPUSharedMemory(&supported);
        LOG_INFO("GPU Shared Memory: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
        if (ADLX_SUCCEEDED(res) && supported)
            gpuCaps |= GPU_CAP_SHARED_MEMORY;
    }

    // ===== V3 =====

    if (gpuMetricsSupport3)
    {
        supported = false;
        res = gpuMetricsSupport3->IsSupportedGPUFanDuty(&supported);
        LOG_INFO("GPU Fan Duty: %s", (ADLX_SUCCEEDED(res) && supported) ? "SUPPORTED" : "NOT SUPPORTED");
        if (ADLX_SUCCEEDED(res) && supported)
            gpuCaps |= GPU_CAP_FAN_DUTY;
    }

    LOG_INFO("=== End of Discovery ===");
}

void ADLXGpuTelemetry::Destroy()
{
    ADLX_RESULT res = ADLXHelp.Terminate();
    if (ADLX_FAILED(res))
    {
        LOG_ERROR("Failed to terminate ADLX Helper: %d", res);
    }
}

template <typename T, typename SupportFn, typename MetricFn>
int ADLXGpuTelemetry::ReadMetric(const char *name, SupportFn supportFn, MetricFn metricFn, IADLXGPUMetrics *metrics)
{
    adlx_bool supported = false;

    ADLX_RESULT res = (gpuMetricsSupport->*supportFn)(&supported);

    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("%s support query failed: %d", name, res);
        return AdlxStates::Error;
    }

    if (!supported)
    {
        LOG_WARN("%s not supported", name);
        return AdlxStates::NotSupported;
    }

    T value{};

    res = (metrics->*metricFn)(&value);

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

template <typename T, typename SupportFn, typename MetricFn>
int ADLXGpuTelemetry::ReadMetricV1(const char *name, SupportFn supportFn, MetricFn metricFn, IADLXGPUMetrics *gpuMetrics)
{
    adlx_bool supported = false;

    IADLXGPUMetrics1Ptr metrics1(gpuMetrics);

    if (!gpuMetricsSupport1 || !metrics1)
    {
        LOG_WARN("%s v1 interface unavailable", name);
        return AdlxStates::Error;
    }

    ADLX_RESULT res = (gpuMetricsSupport1->*supportFn)(&supported);

    if (!ADLX_SUCCEEDED(res))
    {
        LOG_ERROR("%s support query failed: %d", name, res);
        return AdlxStates::Error;
    }

    if (!supported)
    {
        LOG_WARN("%s not supported", name);
        return AdlxStates::NotSupported;
    }

    T value{};

    res = (metrics1->*metricFn)(&value);

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
    GpuMetricsSnapshot snapshot;

    if (perfMonitoringService == nullptr)
    {
        LOG_ERROR("perfMonitoringService not initialized");
        return snapshot;
    }

    // Get current GPU metrics
    IADLXGPUMetricsPtr gpuMetrics;
    ADLX_RESULT res = perfMonitoringService->GetCurrentGPUMetrics(selectedGPU, &gpuMetrics);

    if (ADLX_SUCCEEDED(res))
    {
        bool anyMetricRead = false;

        // GPU temp
        if (IsEnabled(GPU_CAP_TEMP))
        {
            snapshot.temperature = ReadMetric<adlx_double>("GPUTemperature", &IADLXGPUMetricsSupport::IsSupportedGPUTemperature, &IADLXGPUMetrics::GPUTemperature, gpuMetrics);
            anyMetricRead = true;
        }

        // Hotspot
        if (IsEnabled(GPU_CAP_HOTSPOT))
        {
            snapshot.hotspot = ReadMetric<adlx_double>("HotspotTemperature", &IADLXGPUMetricsSupport::IsSupportedGPUHotspotTemperature, &IADLXGPUMetrics::GPUHotspotTemperature, gpuMetrics);
            anyMetricRead = true;
        }

        // VRAM Temp
        if (IsEnabled(GPU_CAP_MEM_TEMP))
        {
            snapshot.vram = ReadMetricV1<adlx_double>("GPUMemoryTemperature", &IADLXGPUMetricsSupport1::IsSupportedGPUMemoryTemperature, &IADLXGPUMetrics1::GPUMemoryTemperature, gpuMetrics);
            anyMetricRead = true;
        }

        // Fan Speed
        if (IsEnabled(GPU_CAP_FAN_SPEED))
        {
            snapshot.fanSpeed = ReadMetric<adlx_int>("GPUFanSpeed", &IADLXGPUMetricsSupport::IsSupportedGPUFanSpeed, &IADLXGPUMetrics::GPUFanSpeed, gpuMetrics);
            anyMetricRead = true;
        }

        // Power Consumption
        if (IsEnabled(GPU_CAP_BOARD_POWER))
        {
            snapshot.power = ReadMetric<adlx_double>("GPUTotalBoardPower", &IADLXGPUMetricsSupport::IsSupportedGPUTotalBoardPower, &IADLXGPUMetrics::GPUTotalBoardPower, gpuMetrics);
            anyMetricRead = true;
        }

        snapshot.valid = anyMetricRead;
    }
    else
    {
        snapshot.valid = false;
        LOG_ERROR("GetCurrentGPUMetrics failed");
        g_cardName.SetValue("Current metrics failed");
    }

    return snapshot;
}
