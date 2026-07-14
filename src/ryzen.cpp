#include "radeonmon/ryzen.hpp"
#include "radeonmon/logging.hpp"

#ifdef TEST
#include <random>
#endif

/////////////////
// Helpers

static void LogOCMode(const OCMode *pMode)
{
    if (pMode == nullptr)
    {
        LOG_DEBUG("OCMode: NULL");
        return;
    }

    LOG_DEBUG("============== OCMode ==============");
    LOG_DEBUG("uOCMode              : 0x%08X", pMode->uOCMode);

    LOG_DEBUG("uAuto                : %u", pMode->uAuto);
    LOG_DEBUG("uManual              : %u", pMode->uManual);
    LOG_DEBUG("uPBOMode             : %u", pMode->uPBOMode);
    LOG_DEBUG("uAutoOverclocking    : %u", pMode->uAutoOverclocking);
    LOG_DEBUG("uEcoMode             : %u", pMode->uEcoMode);
    LOG_DEBUG("uDefault_IRM         : %u", pMode->uDefault_IRM);

    LOG_DEBUG("Reserved             : 0x%X", pMode->Reserved);
    LOG_DEBUG("====================================");
}

static void LogEffectiveFreqData(const EffectiveFreqData *pFreqData)
{
    if (pFreqData == NULL)
    {
        LOG_DEBUG("EffectiveFreqData: NULL");
        return;
    }

    LOG_DEBUG("========== EffectiveFreqData ==========");
    LOG_DEBUG("uLength           : %u", pFreqData->uLength);

    for (unsigned int i = 0; i < pFreqData->uLength; i++)
    {
        LOG_DEBUG("--- Index [%u] ---", i);

        if (pFreqData->dFreq)
            LOG_DEBUG("dFreq             : %.3f MHz", pFreqData->dFreq[i]);

        if (pFreqData->dState)
            LOG_DEBUG("dState            : %.3f %%", pFreqData->dState[i]);

        if (pFreqData->dCurrentFreq)
            LOG_DEBUG("dCurrentFreq      : %.3f MHz", pFreqData->dCurrentFreq[i]);

        if (pFreqData->dCurrentTemp)
            LOG_DEBUG("dCurrentTemp      : %.3f C", pFreqData->dCurrentTemp[i]);
    }

    LOG_DEBUG("=======================================");
}

static void LogCPUParameters(const CPUParameters *pCPU)
{
    if (pCPU == NULL)
    {
        LOG_DEBUG("CPUParameters: NULL");
        return;
    }

    LOG_DEBUG("");
    LOG_DEBUG("============== CPUParameters ==============");

    LogOCMode(&pCPU->eMode);

    LogEffectiveFreqData(&pCPU->stFreqData);

    LOG_DEBUG("dPeakCoreVoltage       : %.3f V", pCPU->dPeakCoreVoltage);
    LOG_DEBUG("dPeakCoreVoltage_1     : %.3f V", pCPU->dPeakCoreVoltage_1);
    LOG_DEBUG("dSocVoltage             : %.3f V", pCPU->dSocVoltage);

    LOG_DEBUG("dTemperature            : %.3f C", pCPU->dTemperature);

    LOG_DEBUG("dAvgCoreVoltage         : %.3f V", pCPU->dAvgCoreVoltage);
    LOG_DEBUG("dAvgCoreVoltage_1       : %.3f V", pCPU->dAvgCoreVoltage_1);

    LOG_DEBUG("dPeakSpeed              : %.3f MHz", pCPU->dPeakSpeed);

    LOG_DEBUG("---- PPT ----");
    LOG_DEBUG("fPPTLimit               : %.3f W", pCPU->fPPTLimit);
    LOG_DEBUG("fPPTValue               : %.3f W", pCPU->fPPTValue);

    LOG_DEBUG("---- VDD TDC ----");
    LOG_DEBUG("fTDCLimit_VDD           : %.3f A", pCPU->fTDCLimit_VDD);
    LOG_DEBUG("fTDCValue_VDD           : %.3f A", pCPU->fTDCValue_VDD);
    LOG_DEBUG("fTDCValue_VDD_1         : %.3f A", pCPU->fTDCValue_VDD_1);

    LOG_DEBUG("---- VDD EDC ----");
    LOG_DEBUG("fEDCLimit_VDD           : %.3f A", pCPU->fEDCLimit_VDD);
    LOG_DEBUG("fEDCValue_VDD           : %.3f A", pCPU->fEDCValue_VDD);
    LOG_DEBUG("fEDCValue_VDD_1         : %.3f A", pCPU->fEDCValue_VDD_1);

    LOG_DEBUG("---- Thermal ----");
    LOG_DEBUG("fcHTCLimit              : %.3f C", pCPU->fcHTCLimit);

    LOG_DEBUG("---- Frequency ----");
    LOG_DEBUG("fFCLKP0Freq             : %.3f MHz", pCPU->fFCLKP0Freq);
    LOG_DEBUG("fCCLK_Fmax              : %.3f MHz", pCPU->fCCLK_Fmax);

    LOG_DEBUG("---- SOC Current ----");
    LOG_DEBUG("fTDCLimit_SOC           : %.3f A", pCPU->fTDCLimit_SOC);
    LOG_DEBUG("fTDCValue_SOC           : %.3f A", pCPU->fTDCValue_SOC);
    LOG_DEBUG("fEDCLimit_SOC           : %.3f A", pCPU->fEDCLimit_SOC);
    LOG_DEBUG("fEDCValue_SOC           : %.3f A", pCPU->fEDCValue_SOC);

    LOG_DEBUG("---- Power ----");
    LOG_DEBUG("fVDDCR_VDD_Power        : %.3f W", pCPU->fVDDCR_VDD_Power);
    LOG_DEBUG("fVDDCR_SOC_Power        : %.3f W", pCPU->fVDDCR_SOC_Power);

    LOG_DEBUG("---- CCD Current ----");
    LOG_DEBUG("fTDCLimit_CCD           : %.3f A", pCPU->fTDCLimit_CCD);
    LOG_DEBUG("fTDCValue_CCD           : %.3f A", pCPU->fTDCValue_CCD);
    LOG_DEBUG("fEDCLimit_CCD           : %.3f A", pCPU->fEDCLimit_CCD);
    LOG_DEBUG("fEDCValue_CCD           : %.3f A", pCPU->fEDCValue_CCD);

    LOG_DEBUG("============================================");
    LOG_DEBUG("");
}

static std::string wcharToUtf8(const wchar_t *wstr)
{
    if (!wstr)
        return "";

    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, nullptr, 0, nullptr, nullptr);
    std::string result(size - 1, '\0'); // exclude null terminator
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, result.data(), size, nullptr, nullptr);

    return result;
}
/////////////////

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
    int ret = m_pCpu->GetCPUParameters(warmup);

    if (ret == -1)
    {
        LOG_ERROR("[Ryzen] GetCPUParameters failed at init");
        return false;
    }

    if (ret == -4)
    {
        LOG_ERROR("[Ryzen] GetCPUParameters not supported");
        return false;
    }

    unsigned int &coreCount = warmup.stFreqData.uLength;
    m_metrics.dTemperature = warmup.dTemperature;
    m_metrics.dPower = warmup.fPPTValue;
    m_metrics.cores.resize(coreCount);

    for (size_t i = 0; i < m_metrics.cores.size(); i++)
    {
        auto &core = m_metrics.cores[i];
        core.dUsage = warmup.stFreqData.dState[i]; // C0 state used for usage
        core.dTemperature = warmup.stFreqData.dCurrentTemp[i];
        core.dCurrentFreq = warmup.stFreqData.dCurrentFreq[i];
        core.dEffectiveFreq = warmup.stFreqData.dFreq[i];
    }

    LogCPUParameters(&warmup);

    // test json
    // char buffer[GPU_JSON_BUFFER_SIZE];
    // m_metrics.BuildJson(buffer, GPU_JSON_BUFFER_SIZE);
    // LOG_DEBUG("[Ryzen] Json=\n%s", buffer);

#ifdef _DEBUG
    m_metrics.Log();
#endif

    m_isInitialized = true;
    return true;
}

bool RyzenCpu::Update()
{
    if (!m_isInitialized || !m_pCpu)
    {
        std::lock_guard<std::mutex> lock(m_metricsMutex);

        m_metrics.dTemperature = static_cast<double>(RyzenMetricState::Error);
        m_metrics.dPower = static_cast<double>(RyzenMetricState::Error);

        return false;
    }

    CPUParameters data{};
    int ret = m_pCpu->GetCPUParameters(data);

    std::lock_guard<std::mutex> lock(m_metricsMutex);

#ifdef TEST
    static std::mt19937 rng(std::random_device{}());

    std::string str = wcharToUtf8(L"AMD Ryzen 9 9950X3D 16-Core Processor");
    strcpy_s(m_metrics.name, str.c_str());
    m_metrics.SetShortName(str.c_str(), static_cast<int>(m_metrics.cores.size()));

    // Simulate 8 cores
    constexpr size_t coreCount = 16;

    if (m_metrics.cores.size() != coreCount)
        m_metrics.cores.resize(coreCount);

    std::uniform_real_distribution<double> tempDist(75.0, 105.0);    // hot CPU
    std::uniform_real_distribution<double> usageDist(85.0, 100.0);   // heavy load
    std::uniform_real_distribution<double> freqDist(4200.0, 5200.0); // MHz boost
    std::uniform_real_distribution<double> effFreqDist(3500.0, 4800.0);
    std::uniform_real_distribution<double> powerDist(85.0, 140.0); // PPT watts

    for (auto &core : m_metrics.cores)
    {
        core.dTemperature = tempDist(rng);
        core.dUsage = usageDist(rng);
        core.dCurrentFreq = freqDist(rng);
        core.dEffectiveFreq = effFreqDist(rng);
    }

    m_metrics.dTemperature = tempDist(rng);
    m_metrics.dPower = powerDist(rng);

    return true;

#else

    switch (ret)
    {
    case 0:
    {
        // name
        std::string str = wcharToUtf8(m_pCpu->GetName());

        // Remove trailing spaces
        while (!str.empty() && str.back() == ' ')
            str.pop_back();

        strncpy_s(m_metrics.name, sizeof(m_metrics.name), str.c_str(), _TRUNCATE);
        m_metrics.SetShortName(str.c_str(), static_cast<int>(m_metrics.cores.size()));

        unsigned int &coreCount = data.stFreqData.uLength;
        if (coreCount != m_metrics.cores.size())
            m_metrics.cores.resize(coreCount);

        for (size_t i = 0; i < m_metrics.cores.size(); i++)
        {
            auto &core = m_metrics.cores[i];
            core.dUsage = data.stFreqData.dState[i]; // C0 state used for usage
            core.dTemperature = data.stFreqData.dCurrentTemp[i];
            core.dCurrentFreq = data.stFreqData.dCurrentFreq[i];
            core.dEffectiveFreq = data.stFreqData.dFreq[i];
        }

        m_metrics.dTemperature = data.dTemperature;
        m_metrics.dPower = static_cast<double>(data.fPPTValue);
        return true;
    }
    case 4:
        m_metrics.dTemperature = static_cast<double>(RyzenMetricState::NotSupported);
        m_metrics.dPower = static_cast<double>(RyzenMetricState::NotSupported);
        return false;

    default:
        m_metrics.dTemperature = static_cast<double>(RyzenMetricState::Error);
        m_metrics.dPower = static_cast<double>(RyzenMetricState::Error);
        return false;
    }

#endif
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

bool RyzenCpu::Start(std::chrono::milliseconds interval)
{
    if (!m_isInitialized || m_running)
        return false;

    m_running = true;

    m_worker = std::thread([this, interval]
                           {
        while (m_running)
        {
            // START_CHRONO(ryzen);
            Update();
            // END_CHRONO(ryzen, "Ryzen::Update()");
            std::this_thread::sleep_for(interval);
        } });

    return true;
}

void RyzenCpu::Stop()
{
    if (!m_running)
        return;

    m_running = false;

    if (m_worker.joinable())
        m_worker.join();
}