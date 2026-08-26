#include "radeonmon/logging.hpp"
#include "radeonmon/PresentMonManager.hpp"

#include <windows.h>

PresentMonManager *PresentMonManager::s_instance = nullptr;

PresentMonManager::~PresentMonManager()
{
    ClosePMSession();
    StopPMTracking();
    StopEventHook();

    if (m_presentMonHandle)
    {
        FreeLibrary(m_presentMonHandle);
        m_presentMonHandle = nullptr;
    }
}

bool PresentMonManager::LoadPresentMonDLL()
{
    m_presentMonHandle = LoadLibraryW(c_presentMonPath);

    if (!m_presentMonHandle)
    {
        LOG_ERROR("[PMON] Failed to load PresentMon DLL.");
        return false;
    }

    m_pmOpenSession = reinterpret_cast<PM_OpenSessionFn>(GetProcAddress(m_presentMonHandle, "pmOpenSession"));
    m_pmCloseSession = reinterpret_cast<PM_CloseSessionFn>(GetProcAddress(m_presentMonHandle, "pmCloseSession"));
    m_pmStartTrackingProcess = reinterpret_cast<PM_StartTrackingProcessFn>(GetProcAddress(m_presentMonHandle, "pmStartTrackingProcess"));
    m_pmStopTrackingProcess = reinterpret_cast<PM_StopTrackingProcessFn>(GetProcAddress(m_presentMonHandle, "pmStopTrackingProcess"));
    m_pmRegisterDynamicQuery = reinterpret_cast<PM_RegisterDynamicQueryFn>(GetProcAddress(m_presentMonHandle, "pmRegisterDynamicQuery"));
    m_pmFreeDynamicQuery = reinterpret_cast<PM_FreeDynamicQueryFn>(GetProcAddress(m_presentMonHandle, "pmFreeDynamicQuery"));
    m_pmPollDynamicQuery = reinterpret_cast<PM_PollDynamicQueryFn>(GetProcAddress(m_presentMonHandle, "pmPollDynamicQuery"));
    m_pmSetEtwFlushPeriod = reinterpret_cast<PM_SetEtwFlushPeriodFn>(GetProcAddress(m_presentMonHandle, "pmSetEtwFlushPeriod"));

    LOG_DEBUG("[PMON] PresentMon DLL loaded.");
    return true;
}

bool PresentMonManager::IsPresentMonServiceRunning() const
{
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);

    if (!scm)
    {
        LOG_ERROR("[PMON] OpenSCManagerW failed: error=%lu", GetLastError());
        return false;
    }

    SC_HANDLE service = OpenServiceW(scm, c_presentMonServiceName, SERVICE_QUERY_STATUS);

    if (!service)
    {
        DWORD error = GetLastError();

        if (error == ERROR_SERVICE_DOES_NOT_EXIST)
            LOG_ERROR("[PMON] PresentMon service is not installed.");
        else if (error == ERROR_ACCESS_DENIED)
            LOG_ERROR("[PMON] Access denied while querying PresentMon service.");
        else
            LOG_ERROR("[PMON] OpenServiceW failed: error=%lu", error);

        CloseServiceHandle(scm);
        return false;
    }

    SERVICE_STATUS_PROCESS status{};
    DWORD bytesNeeded = 0;

    if (!QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&status), sizeof(status), &bytesNeeded))
    {
        LOG_ERROR("[PMON] QueryServiceStatusEx failed: error=%lu", GetLastError());

        CloseServiceHandle(service);
        CloseServiceHandle(scm);
        return false;
    }

    CloseServiceHandle(service);
    CloseServiceHandle(scm);

    if (status.dwCurrentState != SERVICE_RUNNING)
    {
        LOG_WARN("[PMON] PresentMon service is not running. State=%lu", status.dwCurrentState);
        return false;
    }

    LOG_INFO("[PMON] PresentMon service is running.");
    return true;
}

bool PresentMonManager::IsInitialized() const { return m_isInitialized; }

int PresentMonManager::Init()
{
    // 1. Load API DLL
    if (!LoadPresentMonDLL())
        return -1;

    // 2. Service check
    if (!IsPresentMonServiceRunning())
        return -2;

    m_isInitialized = true;

    return true;
}

int PresentMonManager::OpenPMSession()
{
    if (m_session)
        return 0;

    if (!m_pmOpenSession)
    {
        LOG_ERROR("[PMON] Failed to resolve pmOpenSession.");
        return -1;
    }

    PM_STATUS status = m_pmOpenSession(&m_session);

    if (status != PM_STATUS_SUCCESS)
    {
        LOG_ERROR("[PMON] pmOpenSession failed: status=%d", static_cast<int>(status));
        m_session = nullptr;
        return -2;
    }

    m_isSessionOpened = true;

    // Set Flush
    if (!m_pmSetEtwFlushPeriod)
        LOG_ERROR("[PMON] Failed to resolve pmOpenSession.");
    else
    {
        // flush every x ms
        // lower = faster fresh data to process but also higher cpu overhead
        status = m_pmSetEtwFlushPeriod(m_session, 250);
        if (status == PM_STATUS_SUCCESS)
            LOG_DEBUG("[PMON] pmSetEtwFlushPeriod: status=%d", static_cast<int>(status));
        else
            LOG_ERROR("[PMON] pmSetEtwFlushPeriod failed: status=%d", static_cast<int>(status));
    }

    LOG_INFO("[PMON] PresentMon session opened.");
    return 0;
}

void PresentMonManager::ClosePMSession()
{
    if (!m_session)
        return;

    if (!m_pmCloseSession)
    {
        LOG_ERROR("[PMON] Failed to resolve pmCloseSession.");
        m_session = nullptr;
        return;
    }

    PM_STATUS status = m_pmCloseSession(m_session);

    if (status != PM_STATUS_SUCCESS)
    {
        LOG_ERROR("[PMON] pmCloseSession failed: status=%d\n", static_cast<int>(status));
        return;
    }
    else
        LOG_INFO("[PMON] PresentMon session closed.");

    m_session = nullptr;
}

void CALLBACK PresentMonManager::OnForegroundChanged([[maybe_unused]] HWINEVENTHOOK hook, [[maybe_unused]] DWORD event, HWND hwnd, [[maybe_unused]] LONG idObject, [[maybe_unused]] LONG idChild, [[maybe_unused]] DWORD eventThread, [[maybe_unused]] DWORD eventTime)
{
    if (s_instance)
        s_instance->HandleForegroundChanged(hwnd);
}

void PresentMonManager::HandleForegroundChanged(HWND hwnd)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    if (pid != 0 && pid != m_foregroundPID)
    {
        m_foregroundPID = pid;
        LOG_DEBUG("[PMON] Forground change detect: New PID=%d", m_foregroundPID);
    }
}

bool PresentMonManager::StartEventHook()
{
    s_instance = this;
    m_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT);

    if (!m_foregroundHook)
    {
        LOG_ERROR("[PMON] Failed to set event hook");
        return false;
    }

    LOG_INFO("[PMON] Event Hook set successfully.");
    return true;
}

void PresentMonManager::StopEventHook()
{
    if (m_foregroundHook)
    {
        if (!UnhookWinEvent(m_foregroundHook))
            LOG_ERROR("[PMON] Failed to unhook foreground event.");
        else
            LOG_INFO("[PMON] Event Hook unset sucessfully.");

        m_foregroundHook = nullptr;
    }
}

bool PresentMonManager::IsHooking() const { return m_foregroundHook; }

bool PresentMonManager::StartPMTracking(DWORD pid)
{
    if (!m_session)
    {
        LOG_ERROR("[PMON] No PresentMon session.");
        return false;
    }

    if (!m_pmStartTrackingProcess)
    {
        LOG_ERROR("[PMON] Failed to resolved pmStartTrackingProcess");
        return false;
    }

    PM_STATUS status = m_pmStartTrackingProcess(m_session, pid);

    if (status != PM_STATUS_SUCCESS)
    {
        LOG_ERROR("[PMON] pmStartTrackingProcess failed: status=%d pid=%u", static_cast<int>(status), pid);
        return false;
    }

    LOG_INFO("[PMON] Started tracking PID %u.", pid);

    m_trackingPID = pid;
    m_isTracking = true;

    return true;
}

void PresentMonManager::StopPMTracking()
{
    if (!m_session)
        return;

    if (!m_pmStopTrackingProcess)
    {
        LOG_ERROR("[PMON] Failed to resolved pmStopTrackingProcess");
        return;
    }

    PM_STATUS status = m_pmStopTrackingProcess(m_session, m_trackingPID);

    if (status != PM_STATUS_SUCCESS)
    {
        LOG_ERROR("[PMON] pmStopTrackingProcess failed: status=%d pid=%u", static_cast<int>(status), m_trackingPID);
        return;
    }

    LOG_INFO("[PMON] Stopped tracking PID %u.", m_trackingPID);
    m_isTracking = false;
}

bool PresentMonManager::OpenFPSMetric()
{
    m_queryElement.metric = PM_METRIC_DISPLAYED_FPS;
    m_queryElement.stat = PM_STAT_AVG;
    // m_queryElement.metric = PM_METRIC_PRESENTED_FPS;
    // m_queryElement.stat = PM_STAT_NEWEST_POINT;

    m_queryElement.deviceId = 0;
    m_queryElement.arrayIndex = 0;

    if (!m_pmRegisterDynamicQuery)
    {
        LOG_ERROR("[PMON] Failed to resolved pmRegisterDynamicQuery");
        return false;
    }

    PM_STATUS status = m_pmRegisterDynamicQuery(
        m_session,
        &m_query,
        &m_queryElement,
        1,
        1000.0, // window
        0.0);   // offset

    LOG_INFO("[PMON] Metric query opened (pmRegisterDynamicQuery): status=%d offset=%llu size=%llu", static_cast<int>(status), static_cast<unsigned long long>(m_queryElement.dataOffset), static_cast<unsigned long long>(m_queryElement.dataSize));

    if (status != PM_STATUS_SUCCESS)
    {
        m_query = nullptr;
        return false;
    }

    return true;
}

void PresentMonManager::ClosePMMetric()
{
    if (!m_query)
        return;

    if (!m_pmFreeDynamicQuery)
    {
        LOG_ERROR("[PMON] Failed to resolved pmFreeDynamicQuery.");
        return;
    }

    PM_STATUS status = m_pmFreeDynamicQuery(m_query);
    if (status != PM_STATUS_SUCCESS)
    {
        LOG_ERROR("[PMON] pmFreeDynamicQuery failed: status=%d", static_cast<int>(status));
        return;
    }

    m_query = nullptr;
    LOG_INFO("[PMON] FPS Metric closed.");
}

int PresentMonManager::PollFPSMetric()
{
    if (!m_query)
        return -1;

    if (!m_pmPollDynamicQuery)
    {
        LOG_ERROR("[PMON] Failed to resolved pmPollDynamicQuery.");
        return -1;
    }

    constexpr uint32_t MAX_SWAP_CHAINS = 8;

    // Each swap chain gets a blob.
    // 256 is more than enough for our current one-metric query.
    uint8_t blobs[MAX_SWAP_CHAINS][256] = {};
    uint32_t numSwapChains = MAX_SWAP_CHAINS;

    PM_STATUS status = m_pmPollDynamicQuery(m_query, m_trackingPID, reinterpret_cast<uint8_t *>(blobs), &numSwapChains);

    if (status != PM_STATUS_SUCCESS)
    {
        LOG_ERROR("[PMON] Polling failed (pmPollDynamicQuery): status=%d", static_cast<int>(status));
        return -1;
    }

    uint32_t roundedFps = 0;

    for (uint32_t i = 0; i < numSwapChains; ++i)
    {
        double fps = *reinterpret_cast<double *>(blobs[i] + m_queryElement.dataOffset);
        roundedFps = static_cast<uint32_t>(std::round(fps));

        // LOG_DEBUG("[PMON] SwapChain[%u] FPS: %.2f (rounded: %u)", i, fps, roundedFps);
    }

    return roundedFps;
}