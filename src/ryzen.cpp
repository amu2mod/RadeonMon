#include "radeonmon/ryzen.hpp"

bool RyzenCpu::Init()
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

bool RyzenCpu::Update()
{
    if (!m_isInitialized || !m_pCpu)
    {
        m_metrics.dTemperature = static_cast<double>(RyzenMetricState::Error);
        m_metrics.dPower = static_cast<double>(RyzenMetricState::Error);
        return false;
    }

    CPUParameters data{};
    int ret = m_pCpu->GetCPUParameters(data);

    switch (ret)
    {
    case 0:
        m_metrics.dTemperature = data.dTemperature;
        m_metrics.dPower = static_cast<double>(data.fPPTValue);
        return true;

    case 4:
        m_metrics.dTemperature = static_cast<double>(RyzenMetricState::NotSupported);
        m_metrics.dPower = static_cast<double>(RyzenMetricState::NotSupported);
        return false;

    default:
        m_metrics.dTemperature = static_cast<double>(RyzenMetricState::Error);
        m_metrics.dPower = static_cast<double>(RyzenMetricState::Error);
        return false;
    }
}

void RyzenCpu::Shutdown()
{
    if (m_pPlatform)
    {
        m_pPlatform->UnInit();
        m_pPlatform = nullptr;
    }
    m_pCpu = nullptr;
    m_isInitialized = false;
}