#include <cmath>
#include <map>
#ifdef TESTMODE
#include <random>
#endif

#include "radeonmon/adlx.hpp"
#include "AMD/ADLX-1.5/SDK/Include/ADLX.h"
#include "AMD/ADLX-1.5/SDK/Include/ISystem3.h"
#include "AMD\ADLX-1.5\SDK\Include\IDisplays.h"
#include "AMD\ADLX-1.5\SDK\Include\IGPUManualPowerTuning.h"
#include "AMD\ADLX-1.5\SDK\Include\IGPUTuning.h"

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

    // Create block event
    m_blockEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

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
    g_cardName.textLength = static_cast<UINT>(gpuInfo.name.size());

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

    LOG_DEBUG("[ADLX] gpuMetricsSupport OK");

    // Tuning
    res = ADLXHelp.GetSystemServices()->GetGPUTuningServices(&gpuTuningService);
    if (ADLX_FAILED(res))
        LOG_ERROR("[ADLX] Get GPU tuning services failed");
    else
    {
        LOG_DEBUG("[ADLX] gpuTuningService OK");

        // Get Change handle
        IADLXGPUTuningChangedHandlingPtr changeHandle;
        res = gpuTuningService->GetGPUTuningChangedHandling(&changeHandle);
        if (ADLX_SUCCEEDED(res))
        {
            // Create call back
            // IADLXGPUTuningChangedListener *call = new CallBackGPUTuningChanged;

            // Add call back
            changeHandle->AddGPUTuningEventListener(this);
        }
    }

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
    // discoveredMetrics = {};
    GpuMetricsSnapshot &discoveredMetrics = m_snapshot;

    LOGLN();
    LOG_INFO("=== ADLX GPU Metrics Support Discovery ===");

    auto discoverV0 = [&](const char *name, auto isSupportedFn, auto getRangeFn, uint32_t cap, auto &metric)
    {
        adlx_bool supported = false;
        ADLX_RESULT res = (gpuMetricsSupport->*isSupportedFn)(&supported);

        LOG_INFO("%s: %s", name, (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");

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

    auto discoverV1 = [&](const char *name, auto isSupportedFn, auto getRangeFn, uint32_t cap, auto &metric)
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

    auto discoverV3 = [&](const char *name, auto isSupportedFn, auto getRangeFn, uint32_t cap, auto &metric)
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

    auto discoverTuning = [&](const char *name, auto isSupportedFn, auto getRangeFn, uint32_t cap, auto &metric)
    {
        adlx_bool supported = false;
        ADLX_RESULT res = (manualPowerTuning->*isSupportedFn)(&supported);

        LOG_INFO("%s: %s", name, (ADLX_SUCCEEDED(res) && supported) ? "OK" : "NOT SUPPORTED");

        if (!ADLX_SUCCEEDED(res) || !supported)
            return;

        gpuCaps |= cap;
        metric.isSupported = true;

        adlx_int min = 0, max = 0, step = 0;
        res = (manualPowerTuning->*getRangeFn)(&min, &max, &step);

        if (ADLX_SUCCEEDED(res))
        {
            metric.min = min;
            metric.max = max;
            // metric.step = step;
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

    // ===== Tuning ======
    UpdateManualPowerTuning();

    LOG_INFO("=== End of Discovery ===");
    LOGLN();
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
        return static_cast<int>(std::round(value));
    else
        return static_cast<int>(value);
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
        return static_cast<int>(std::lround(value));
    else
        return static_cast<int>(value);
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
        return static_cast<int>(std::lround(value));
    else
        return static_cast<int>(value);
}

int ADLXGpuTelemetry::GetFPS()
{
    // Get current FPS metrics
    IADLXFPSPtr oneFPS;
    // START_CHRONO(fps);
    ADLX_RESULT res = perfMonitoringService->GetCurrentFPS(&oneFPS);
    // END_CHRONO(fps, "get oneFPS");
    if (ADLX_SUCCEEDED(res))
    {
        // adlx_int64 timeStamp = 0;
        // res = oneFPS->TimeStamp(&timeStamp);
        // if (ADLX_SUCCEEDED(res))
        //     std::cout << "The current metric time stamp is: " << timeStamp << "ms\n";
        adlx_int fpsData = 0;
        res = oneFPS->FPS(&fpsData);
        if (ADLX_SUCCEEDED(res))
            return static_cast<int>(fpsData);
        // std::cout << "The current metric FPS is: " << fpsData << std::endl;
        else if (res == ADLX_NOT_SUPPORTED)
            return -1;
        // std::cout << "Don't support FPS" << std::endl;
    }

    return -1;
}

GpuMetricsSnapshot ADLXGpuTelemetry::Query()
{
    GpuMetricsSnapshot snapshot;

#ifdef TESTMODE
    static std::mt19937 rng(std::random_device{}());

    auto randDouble = [&](double min, double max)
    {
        return std::uniform_real_distribution<double>(min, max)(rng);
    };

    auto randInt = [&](int min, int max)
    {
        return std::uniform_int_distribution<int>(min, max)(rng);
    };

    snapshot.valid = true;

    auto setDouble = [](MetricDouble &m, double value, int min, int max)
    {
        m.isSupported = true;
        m.value = value;
        m.min = min;
        m.max = max;
    };

    auto setInt = [](MetricInt &m, int value, int min, int max)
    {
        m.isSupported = true;
        m.value = value;
        m.min = min;
        m.max = max;
    };

    setDouble(snapshot.usage, randDouble(92.0, 100.0), 0, 100);

    setInt(snapshot.clockSpeed, randInt(2400, 3000), 0, 3500);
    setInt(snapshot.vramClockSpeed, randInt(2200, 2600), 0, 3000);

    setDouble(snapshot.temperature, randDouble(55.0, 90.0), 0, 110);
    setDouble(snapshot.hotspot, randDouble(90.0, 105.0), 0, 120);
    setDouble(snapshot.memoryTemperature, randDouble(80.0, 98.0), 0, 110);
    setDouble(snapshot.intakeTemperature, randDouble(35.0, 45.0), 0, 60);

    setDouble(snapshot.power, randDouble(250.0, 340.0), 0, 400);
    setDouble(snapshot.totalBoardPower, randDouble(270.0, 360.0), 0, 400);
    setInt(snapshot.voltage, randInt(950, 1150), 800, 1300);

    setInt(snapshot.fanSpeed, randInt(0, 1000), 0, 3000);
    setInt(snapshot.fanDuty, randInt(45, 90), 0, 100);

    setInt(snapshot.vram, randInt(8000, 15800), 0, 16384);
    setInt(snapshot.sharedMemory, randInt(1000, 6000), 0, 16384);

    setInt(snapshot.npuFrequency, randInt(900, 1500), 0, 2000);
    setInt(snapshot.npuActivityLevel, randInt(0, 100), 0, 100);

    snapshot.fps = isFpsEnabled ? randInt(100, 180) : -1;

    return snapshot;
#endif
    // START_CHRONO(query);
    snapshot = m_snapshot;

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
            gpuMetrics->TimeStamp(&snapshot.timestampMs);

        // FPS
        // START_CHRONO(fps);
        snapshot.fps = isFpsEnabled ? GetFPS() : -1;

        // END_CHRONO(fps, "get fps");

        snapshot.valid = anyMetricRead;
        // END_CHRONO(query, "ADLX Query");
    }
    else
    {
        snapshot.valid = false;
        LOG_ERROR("GetCurrentGPUMetrics failed");
        g_cardName.SetValue(L"Current metrics failed");
    }

    return snapshot;
}

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

static void GPUUniqueName(IADLXGPUPtr gpu, char *uniqueName)
{
    if (nullptr != gpu && nullptr != uniqueName)
    {
        const char *gpuName = nullptr;
        gpu->Name(&gpuName);
        adlx_int id;
        gpu->UniqueId(&id);
        sprintf_s(uniqueName, 128, "name:%s, id:%d", gpuName, id);
    }
}

adlx_bool ADLX_STD_CALL ADLXGpuTelemetry::OnGPUTuningChanged(IADLXGPUTuningChangedEvent *pGPUTuningChangedEvent)
{
    IADLXGPUTuningChangedEvent1 *pGPUTuningChangedEvent1 = nullptr;
    pGPUTuningChangedEvent->QueryInterface(L"IADLXGPUTuningChangedEvent1", reinterpret_cast<void **>(&pGPUTuningChangedEvent1));
    ADLX_SYNC_ORIGIN origin = pGPUTuningChangedEvent->GetOrigin();
    if (origin == SYNC_ORIGIN_EXTERNAL)
    {
        // Get GPU
        IADLXGPUPtr gpu;
        pGPUTuningChangedEvent->GetGPU(&gpu);
        char uniqueName[128] = "Unknown";
        GPUUniqueName(gpu, uniqueName);
        LOG_DEBUG("GPU: %s Get sync event, update required", uniqueName);

        if (pGPUTuningChangedEvent->IsAutomaticTuningChanged())
        {
            LOG_DEBUG("\tAutomaticTuningChanged");
        }
        else if (pGPUTuningChangedEvent->IsPresetTuningChanged())
        {
            LOG_DEBUG("\tPresetTuningChanged");
        }
        else if (pGPUTuningChangedEvent->IsManualGPUCLKTuningChanged())
        {
            LOG_DEBUG("\tManualGPUCLKTuningChanged");
        }
        else if (pGPUTuningChangedEvent->IsManualVRAMTuningChanged())
        {
            LOG_DEBUG("\tManualVRAMTuningChanged");
        }
        else if (pGPUTuningChangedEvent->IsManualFanTuningChanged())
        {
            LOG_DEBUG("\tManualFanTuningChanged");
        }
        else if (pGPUTuningChangedEvent->IsManualPowerTuningChanged())
        {
            LOG_DEBUG("\tManualPowerTuningChanged");
            UpdateManualPowerTuning();
        }
        else if (pGPUTuningChangedEvent1->IsSmartAccessMemoryChanged())
        {
            LOG_DEBUG("\tSmartAccessMemoryChanged");
        }
    }
    SetEvent(m_blockEvent);

    // Return true for ADLX to continue notifying the next listener, or false to stop notification.
    return true;
}

void ADLXGpuTelemetry::UpdateManualPowerTuning()
{
    adlx_bool supported = false;
    ADLX_RESULT res = gpuTuningService->IsSupportedManualPowerTuning(selectedGPU, &supported);
    if (ADLX_FAILED(res) || supported == false)
        LOG_INFO("GetManualPowerTuning: NOT SUPPORTED");
    else
    {
        LOG_INFO("GetManualPowerTuning: OK");
        gpuCaps |= GPU_CAP_MANUAL_POWER_TUNING;
        m_snapshot.powerLimit.isSupported = true;
        res = gpuTuningService->GetManualPowerTuning(selectedGPU, &manualPowerTuningIfc);
        if (ADLX_FAILED(res) || manualPowerTuningIfc == nullptr)
            LOG_ERROR("[ADLX] Get manual power tuning interface failed");
        else
        {
            manualPowerTuning = IADLXManualPowerTuningPtr(manualPowerTuningIfc);
            if (manualPowerTuning == nullptr)
                LOG_ERROR("[ADLX] Get manual power tuning failed");
            else
            {
                ADLX_IntRange powerRange;
                res = manualPowerTuning->GetPowerLimitRange(&powerRange);

                if (ADLX_SUCCEEDED(res))
                {
                    m_snapshot.powerLimit.min = static_cast<int>(powerRange.minValue);
                    m_snapshot.powerLimit.max = static_cast<int>(powerRange.maxValue);

                    // optional: store step

                    LOG_DEBUG("[ADLX] powerRange: min=%d, max=%d", m_snapshot.powerLimit.min, m_snapshot.powerLimit.max);
                }
                else
                    LOG_ERROR("[ADLX] GetPowerLimitRange failed");

                adlx_int powerLimit;
                res = manualPowerTuning->GetPowerLimit(&powerLimit);
                if (ADLX_SUCCEEDED(res))
                {
                    m_snapshot.powerLimit.value = static_cast<int>(powerLimit);
                    LOG_DEBUG("[ADLX] power limit=%d", m_snapshot.powerLimit.value);

                    static int powerBase = static_cast<int>(std::round(m_snapshot.totalBoardPower.max / ((100 + m_snapshot.powerLimit.max) / 100.0)));
                    m_snapshot.powerLimitWatts = static_cast<int>(std::round(powerBase * (100 + m_snapshot.powerLimit.value) / 100.0));

                    // TODO: update UI
                }
                else
                    LOG_ERROR("[ADLX] GetPowerLimit failed");
            }
        }
    }
}