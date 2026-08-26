#pragma once

#include "Intel/PresentMon-2.5.1/IntelPresentMon/PresentMonAPI2/PresentMonAPI.h"
#include "Intel/PresentMon-2.5.1/IntelPresentMon/PresentMonAPI2/PresentMonDiagnostics.h"

#include <Windows.h>

class PresentMonManager
{
public:
    ~PresentMonManager();

    int Init(); // -1=dll, -2=service
    bool IsInitialized() const;

    int OpenPMSession();
    void ClosePMSession();
    inline bool IsSessionOpened() const { return m_session != nullptr; }
    bool StartEventHook();
    void StopEventHook();
    bool IsHooking() const;
    bool StartPMTracking(DWORD pid);
    void StopPMTracking();
    inline bool IsTracking() const { return m_isTracking; }
    bool OpenFPSMetric();
    inline bool IsQueryOpened() const { return m_query != nullptr; }
    void ClosePMMetric();
    int PollFPSMetric();

private:
    bool m_isInitialized = false;
    bool m_isSessionOpened = false;
    HMODULE m_presentMonHandle = nullptr;
    DWORD m_trackingPID = 0;   // last PID used for tracking
    DWORD m_foregroundPID = 0; // new PID from event hook
    HWINEVENTHOOK m_foregroundHook = nullptr;
    static PresentMonManager *s_instance;
    bool m_isTracking = false;
    PM_DYNAMIC_QUERY_HANDLE m_query = nullptr;
    PM_QUERY_ELEMENT m_queryElement = {};

    // function pointer to presentmon calls from the DLL in the SDK folder
    // otheriwse the api calls would triger a local DLL load
    using PM_OpenSessionFn = PM_STATUS (*)(PM_SESSION_HANDLE *);
    PM_OpenSessionFn m_pmOpenSession = nullptr;
    using PM_CloseSessionFn = PM_STATUS (*)(PM_SESSION_HANDLE);
    PM_CloseSessionFn m_pmCloseSession = nullptr;
    using PM_StartTrackingProcessFn = PM_STATUS (*)(PM_SESSION_HANDLE, uint32_t);
    PM_StartTrackingProcessFn m_pmStartTrackingProcess = nullptr;
    using PM_StopTrackingProcessFn = PM_STATUS (*)(PM_SESSION_HANDLE, uint32_t);
    PM_StopTrackingProcessFn m_pmStopTrackingProcess = nullptr;
    using PM_RegisterDynamicQueryFn = PM_STATUS (*)(PM_SESSION_HANDLE, PM_DYNAMIC_QUERY_HANDLE *, PM_QUERY_ELEMENT *, uint64_t, double, double);
    PM_RegisterDynamicQueryFn m_pmRegisterDynamicQuery = nullptr;
    using PM_FreeDynamicQueryFn = PM_STATUS (*)(PM_DYNAMIC_QUERY_HANDLE);
    PM_FreeDynamicQueryFn m_pmFreeDynamicQuery = nullptr;
    using PM_PollDynamicQueryFn = PM_STATUS (*)(PM_DYNAMIC_QUERY_HANDLE, uint32_t, uint8_t *, uint32_t *);
    PM_PollDynamicQueryFn m_pmPollDynamicQuery = nullptr;
    using PM_SetEtwFlushPeriodFn = PM_STATUS (*)(PM_SESSION_HANDLE, uint32_t);
    PM_SetEtwFlushPeriodFn m_pmSetEtwFlushPeriod = nullptr;

    PM_SESSION_HANDLE m_session = nullptr;

    static constexpr wchar_t c_presentMonPath[] = L"C:\\Program Files\\Intel\\PresentMon\\SDK\\PresentMonAPI2Loader.dll";
    static constexpr wchar_t c_presentMonServiceName[] = L"PresentMonSharedService";

private:
    bool LoadPresentMonDLL();
    bool IsPresentMonServiceRunning() const;

    static void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void HandleForegroundChanged(HWND hwnd);
};