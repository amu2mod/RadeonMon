// ryzen.hpp
#pragma once

#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

#pragma warning(push)
#pragma warning(disable : 4091 4201)
#include "AMD/RyzenMasterMonitoringSDK/include/IDevice.h"
#pragma warning(pop)
#include "AMD/RyzenMasterMonitoringSDK/include/IPlatform.h"
#include "AMD/RyzenMasterMonitoringSDK/include/ICPUEx.h"
#include "AMD/RyzenMasterMonitoringSDK/include/IDeviceManager.h"

#include "radeonmon/structures.hpp"
#include "radeonmon/logging.hpp"
#include "radeonmon/constants.hpp"

class RyzenCpu
{
public:
    RyzenCpu() = default;
    ~RyzenCpu()
    {
        Stop();
        Shutdown();
    }
    RyzenCpu(const RyzenCpu &) = delete;
    RyzenCpu &operator=(const RyzenCpu &) = delete;

    bool Init();
    bool Update(); // Refreshes the cached metrics.

    // Threading
    bool Start(std::chrono::milliseconds interval = std::chrono::milliseconds(APP_REFRESH_TIMER));
    void Stop();

    inline double GetTemperature() const
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        return m_metrics.dTemperature;
    }
    inline double GetPower() const
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        return m_metrics.dPower;
    }
    inline RyzenMetrics GetMetrics() const
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);
        return m_metrics;
    }
    inline bool IsInitialized() const { return m_isInitialized; }

private:
    void Shutdown();

    mutable std::mutex m_metricsMutex;
    CPUParameters m_data;
    RyzenMetrics m_metrics{};
    ICPUEx *m_pCpu = nullptr;
    IPlatform *m_pPlatform = nullptr;
    std::atomic<bool> m_isInitialized{false};
    std::thread m_worker;
    std::atomic<bool> m_running{false};
};
