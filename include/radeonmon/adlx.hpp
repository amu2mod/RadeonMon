#pragma once

#include "structures.hpp"

#include "AMD/ADLX-1.5/SDK/Include/IPerformanceMonitoring3.h"
#include "AMD/ADLX-1.5/SDK/ADLXHelper/Windows/Cpp/ADLXHelper.h"

class ADLXGpuTelemetry
{
public:
    bool isInitialized = false;

    void Init();
    void Discover(); // Discover all supported metrics functions
    void Destroy();

    void Tick()
    {
        m_snapshot = Query();
    }

    const GpuMetricsSnapshot &Get() const
    {
        return m_snapshot;
    }

    inline bool IsEnabled(uint32_t flag) const { return (gpuCaps & flag) != 0; }

private:
    ADLXHelper ADLXHelp;
    IADLXGPUPtr selectedGPU;
    IADLXPerformanceMonitoringServicesPtr perfMonitoringService;
    IADLXGPUMetricsSupportPtr gpuMetricsSupport = nullptr;
    IADLXGPUMetricsSupport1Ptr gpuMetricsSupport1 = nullptr;
    IADLXGPUMetricsSupport2Ptr gpuMetricsSupport2 = nullptr;
    IADLXGPUMetricsSupport3Ptr gpuMetricsSupport3 = nullptr;

    uint32_t gpuCaps = 0;

    GpuMetricsSnapshot m_snapshot;

    // Generic template to read ADLX metrics
    template <typename T, typename MetricFn>
    int ReadMetric(const char *name, MetricFn metricFn, IADLXGPUMetrics *metrics);

    // Use IADLXGPUMetrics1Ptr for newer metrics of ADLX 1.5
    template <typename T, typename MetricFn>
    int ReadMetricV1(const char *name, MetricFn metricFn, IADLXGPUMetrics *metrics);

    // heavy function to get all metrics
    GpuMetricsSnapshot Query();
};
