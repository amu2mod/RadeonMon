// ryzen.hpp
#pragma once

#pragma warning(push)
#pragma warning(disable : 4091 4201)
#include "AMD/RyzenMasterMonitoringSDK/include/IDevice.h"
#pragma warning(pop)
#include "AMD/RyzenMasterMonitoringSDK/include/IPlatform.h"
#include "AMD/RyzenMasterMonitoringSDK/include/ICPUEx.h"
#include "AMD/RyzenMasterMonitoringSDK/include/IDeviceManager.h"
#include "radeonmon/logging.hpp"

class RyzenCpu
{
public:
    RyzenCpu() = default;

    ~RyzenCpu()
    {
        Shutdown();
    }

    RyzenCpu(const RyzenCpu &) = delete;
    RyzenCpu &operator=(const RyzenCpu &) = delete;

    // Initializes the platform and grabs the CPU device.
    // Requires admin privileges to succeed.
    bool Init()
    {
        if (m_isInitialized)
            return true;

        IPlatform &rPlatform = GetPlatform();
        if (!rPlatform.Init())
        {
            LOG_DEBUG("RyzenCpu: Platform init failed");
            return false;
        }

        m_pPlatform = &rPlatform;

        IDeviceManager &rDeviceManager = m_pPlatform->GetIDeviceManager();
        m_pCpu = (ICPUEx *)rDeviceManager.GetDevice(dtCPU, 0);

        if (!m_pCpu)
        {
            LOG_DEBUG("RyzenCpu: Failed to get CPU device");
            m_pPlatform->UnInit();
            m_pPlatform = nullptr;
            return false;
        }

        // Warm-up read: first read after Init() can return stale/garbage values.
        CPUParameters warmup = {};
        m_pCpu->GetCPUParameters(warmup);

        m_isInitialized = true;
        return true;
    }

    bool GetRyzenMetrics(RyzenMetrics &outMetrics)
    {
        if (!m_isInitialized || !m_pCpu)
        {
            LOG_DEBUG("RyzenCpu: GetRyzenMetrics called before successful Init()");
            outMetrics.dTemperature = static_cast<double>(RyzenMetricState::Error);
            outMetrics.dPower = static_cast<double>(RyzenMetricState::Error);
            return false;
        }

        CPUParameters stData = {};
        int iRet = m_pCpu->GetCPUParameters(stData);

        switch (iRet)
        {
        case 0:
            // Success, fall through to populate outMetrics below
            break;

        case 4:
            LOG_DEBUG("RyzenCpu: GetCPUParameters unsupported on this platform");
            outMetrics.dTemperature = static_cast<double>(RyzenMetricState::NotSupported);
            outMetrics.dPower = static_cast<double>(RyzenMetricState::NotSupported);
            return false;

        case -1:
        default:
            LOG_DEBUG("RyzenCpu: GetCPUParameters failed, err=%d", iRet);
            outMetrics.dTemperature = static_cast<double>(RyzenMetricState::Error);
            outMetrics.dPower = static_cast<double>(RyzenMetricState::Error);
            return false;
        }

        outMetrics.dTemperature = stData.dTemperature;
        outMetrics.dPower = static_cast<double>(stData.fVDDCR_VDD_Power) + static_cast<double>(stData.fVDDCR_SOC_Power);

        return true;
    }

    // Returns true and fills outTemp (Celsius) on success.
    bool GetTemp(double &outTemp)
    {
        if (!m_isInitialized || !m_pCpu)
        {
            LOG_DEBUG("RyzenCpu: GetTemp called before successful Init()");
            return false;
        }

        CPUParameters stData = {};
        int iRet = m_pCpu->GetCPUParameters(stData);
        if (iRet != 0)
        {
            LOG_DEBUG("RyzenCpu: GetCPUParameters failed, err=%d", iRet);
            return false;
        }

        outTemp = stData.dTemperature;
        return true;
    }

    bool GetCorePower(double &outPower)
    {
        if (!m_isInitialized || !m_pCpu)
            return false;

        CPUParameters stData = {};
        if (m_pCpu->GetCPUParameters(stData) != 0)
            return false;

        outPower = stData.fVDDCR_VDD_Power;
        return true;
    }

    // Returns SoC rail power (VDDCR_SOC)
    bool GetSocPower(double &outPower)
    {
        if (!m_isInitialized || !m_pCpu)
            return false;

        CPUParameters stData = {};
        if (m_pCpu->GetCPUParameters(stData) != 0)
            return false;

        outPower = stData.fVDDCR_SOC_Power;
        return true;
    }

    // Returns true and fills outPower (Watts) on success.
    bool GetPower(double &outPower)
    {
        if (!m_isInitialized || !m_pCpu)
        {
            LOG_DEBUG("RyzenCpu: GetPower called before successful Init()");
            return false;
        }

        CPUParameters stData = {};
        int iRet = m_pCpu->GetCPUParameters(stData);
        if (iRet != 0)
        {
            LOG_DEBUG("RyzenCpu: GetCPUParameters failed, err=%d", iRet);
            return false;
        }

        // Prefer PPT as it represents total CPU package power
        if (stData.fPPTValue > 0.0f)
        {
            outPower = static_cast<double>(stData.fPPTValue);
        }
        else
        {
            // Fallback: sum the two power rails
            outPower = static_cast<double>(stData.fVDDCR_VDD_Power) +
                       static_cast<double>(stData.fVDDCR_SOC_Power);
        }

        return true;
    }

    bool IsInitialized() const { return m_isInitialized; }

private:
    void Shutdown()
    {
        if (m_pPlatform)
        {
            m_pPlatform->UnInit();
            m_pPlatform = nullptr;
        }
        m_pCpu = nullptr;
        m_isInitialized = false;
    }

    IPlatform *m_pPlatform = nullptr;
    ICPUEx *m_pCpu = nullptr;
    bool m_isInitialized = false;
};