// ryzen.hpp
#pragma once

#pragma warning(push)
#pragma warning(disable : 4091 4201)
#include "AMD/RyzenMasterMonitoringSDK/include/IDevice.h"
#pragma warning(pop)
#include "AMD/RyzenMasterMonitoringSDK/include/IPlatform.h"
#include "AMD/RyzenMasterMonitoringSDK/include/ICPUEx.h"
#include "AMD/RyzenMasterMonitoringSDK/include/IDeviceManager.h"

#include "radeonmon/structures.hpp"
#include "radeonmon/logging.hpp"

class RyzenCpu
{
public:
    RyzenCpu() = default;
    ~RyzenCpu() { Shutdown(); }
    RyzenCpu(const RyzenCpu &) = delete;
    RyzenCpu &operator=(const RyzenCpu &) = delete;

    bool Init();
    bool Update(); // Refreshes the cached metrics.
    inline double GetTemperature() const { return m_metrics.dTemperature; }
    inline double GetPower() const { return m_metrics.dPower; }
    inline const RyzenMetrics &GetMetrics() const { return m_metrics; }
    inline bool IsInitialized() const { return m_isInitialized; }

private:
    RyzenMetrics m_metrics{};
    ICPUEx *m_pCpu = nullptr;
    IPlatform *m_pPlatform = nullptr;
    bool m_isInitialized = false;

    void Shutdown();
};
