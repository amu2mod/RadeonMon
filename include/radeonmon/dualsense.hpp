#pragma once

#include "radeonmon/logging.hpp"

#include <windows.h>

#include <functional>
#include <mutex>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")

#ifdef LOGDS
#define LOGDS_D(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#define LOGDS_E(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#else
#define LOGDS_D(fmt, ...) ((void)0)
#define LOGDS_E(fmt, ...) ((void)0)
#endif

class DualSense
{
public:
    enum class Transport
    {
        None,
        USB,
        Bluetooth
    };

    static const char *TransportName(Transport transport);

public:
    using Callback = std::function<void()>; // callback alias

    DualSense() = default;
    inline ~DualSense() { Stop(); }
    DualSense(const DualSense &) = delete;
    DualSense &operator=(const DualSense &) = delete;

    bool Start();
    void Stop();

    // API
    void SetOnCreateButtonPressed(Callback callback);
    void SetOnConnected(Callback callback);
    void SetOnDisconnected(Callback callback);
    Transport GetTransport() const;

private:
    enum class ReadResult
    {
        Disconnected,
        SwitchTransport,
        Stopped
    };

private:
    static constexpr USHORT DUALSENSE_VID = 0x054C;
    static constexpr USHORT DUALSENSE_PID = 0x0CE6;
    static constexpr DWORD REPORT_THROTTLE_MS = 50;
    static constexpr DWORD SCREENSHOT_COOLDOWN_MS = 500; // TODO: sync with Screenshot constexpr
    static constexpr UINT WM_DUALSENSE_DEVICE_CHANGE = WM_APP + 1;
    static constexpr WPARAM DEVICE_CHANGE_GENERIC = 0;
    static constexpr WPARAM DEVICE_CHANGE_BT_CONNECTED = 1;
    static constexpr WPARAM DEVICE_CHANGE_BT_DISCONNECTED = 2;

    mutable std::mutex m_stateMutex;
    bool m_running = false;
    std::thread m_worker;
    HANDLE m_stopEvent = nullptr;

    // HID device
    mutable std::mutex m_deviceMutex;
    HANDLE m_device = INVALID_HANDLE_VALUE;
    HANDLE m_readEvent = nullptr;
    OVERLAPPED m_overlapped{};
    std::vector<BYTE> m_buffer;
    DWORD m_reportSize = 0;
    Transport m_transport = Transport::None;
    bool m_switchTransportRequested = false;

    HWND m_hwnd = nullptr;

    //// Windows device notification
    // HID (app level)
    HDEVNOTIFY m_notificationHandle = nullptr;
    // HCI (Bluetooth connection level)
    HDEVNOTIFY m_bluetoothNotificationHandle = nullptr;
    HANDLE m_bluetoothRadio = nullptr;

    // Callbacks
    mutable std::mutex m_callbackMutex;
    Callback m_onCreateButtonPressed;
    Callback m_onConnected;
    Callback m_onDisconnected;

private:
    bool IsRunning() const;
    bool IsConnected() const;
    void WorkerThread();
    HANDLE FindDualSense(Transport &selectedTransport);
    bool TryConnect();
    void Disconnect();
    ReadResult ReadInputReports();
    bool IsSwitchTransportRequested() const;
    void HandleDeviceChange(WPARAM changeType);
    bool CreateNotificationWindow();
    void DestroyNotificationWindow();
    void WaitForDeviceOrStop();
    inline bool ShouldStop() const { return m_stopEvent && WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0; }

    // Callbacks
    void InvokeCreateButton();
    void InvokeConnected();
    void InvokeDisconnected();

    // win32 proc
    static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};