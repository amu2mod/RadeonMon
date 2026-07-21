#pragma once

#include "structures.hpp"

#include "AMD/ADLX-1.5/SDK/Include/IPerformanceMonitoring3.h"
#include "AMD/ADLX-1.5/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"
#include "AMD/ADLX-1.5/SDK/Include/IGPUManualPowerTuning.h"
#include "AMD/ADLX-1.5/SDK/Include/IGPUTuning.h"
#include "AMD/ADLX-1.5/SDK/Include/IGPUTuning1.h"

using namespace RadeonMon::Hardware;

class ADLXGpuTelemetry : public IADLXGPUTuningChangedListener
{
public:
    bool isInitialized = false;
    RadeonMon::Hardware::GPUInfo gpuInfo;

    inline explicit ADLXGpuTelemetry(const bool &fpsEnabled) : isFpsEnabled(fpsEnabled) {}

    void Init(HWND hwnd);
    void Discover(); // Discover all supported metrics functions
    void Destroy();

    inline void Tick() { m_snapshot = Query(); }
    inline const GpuMetricsSnapshot &Get() const { return m_snapshot; }

    adlx_bool ADLX_STD_CALL OnGPUTuningChanged(IADLXGPUTuningChangedEvent *pGPUTuningChangedEvent) override;

    inline bool IsEnabled(uint32_t flag) const { return (gpuCaps & flag) != 0; }
    inline const RadeonMon::Hardware::GPUInfo GetGpuInfo() const { return gpuInfo; }
    inline int GetSnapshotFPS() const { return m_snapshot.fps; }

private:
    ADLXHelper ADLXHelp;
    IADLXGPUPtr selectedGPU;
    IADLXPerformanceMonitoringServicesPtr perfMonitoringService;
    IADLXGPUMetricsSupportPtr gpuMetricsSupport = nullptr;
    IADLXGPUMetricsSupport1Ptr gpuMetricsSupport1 = nullptr;
    IADLXGPUMetricsSupport2Ptr gpuMetricsSupport2 = nullptr;
    IADLXGPUMetricsSupport3Ptr gpuMetricsSupport3 = nullptr;
    IADLXGPUTuningServicesPtr gpuTuningService = nullptr;
    IADLXInterfacePtr manualPowerTuningIfc;
    IADLXManualPowerTuningPtr manualPowerTuning = nullptr;

    uint32_t gpuCaps = 0;

    GpuMetricsSnapshot m_snapshot;

    const bool &isFpsEnabled;

    HANDLE m_blockEvent = nullptr;
    HWND m_hwnd = nullptr;

    // Generic template to read ADLX metrics
    template <typename T, typename MetricFn>
    int ReadMetric(const char *name, MetricFn metricFn, IADLXGPUMetrics *metrics);

    // Use IADLXGPUMetrics1Ptr for newer metrics of ADLX 1.5
    template <typename T, typename MetricFn>
    int ReadMetricV1(const char *name, MetricFn metricFn, IADLXGPUMetrics *metrics);

    template <typename T, typename MetricFn>
    int ReadMetricV3(const char *name, MetricFn metricFn, IADLXGPUMetrics *gpuMetrics);

    // heavy function to get all metrics
    GpuMetricsSnapshot Query();

    // Retrieve all gpu info
    void PopulateGPUInfo(IADLXGPU *gpu);

    // Helper to update power limit
    void UpdateManualPowerTuning();

    int GetFPS();
};
