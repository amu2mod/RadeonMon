#include "radeonmon/dualsense.hpp"

#include <dbt.h>
#include <setupapi.h>
#include <hidsdi.h>

#include <chrono>
#include <thread>
#include <vector>

#pragma comment(lib, "Cfgmgr32.lib")
#pragma comment(lib, "Bthprops.lib")

bool DualSense::GetBluetoothAddressFromDevNode(DEVINST devInst, BLUETOOTH_ADDRESS &address)
{
    address.ullLong = 0;

    for (;;)
    {
        WCHAR deviceId[MAX_DEVICE_ID_LEN]{};
        CONFIGRET cr = CM_Get_Device_IDW(devInst, deviceId, ARRAYSIZE(deviceId), 0);

        if (cr == CR_SUCCESS && _wcsnicmp(deviceId, L"BTHENUM\\", 8) == 0)
        {
            const wchar_t *lastSlash = wcsrchr(deviceId, L'\\');

            if (!lastSlash)
                return false;

            const wchar_t *instance = lastSlash + 1;

            // Find the final '&' before the Bluetooth address.
            const wchar_t *ampersand = wcsrchr(instance, L'&');

            if (!ampersand)
                return false;

            const wchar_t *addressStart = ampersand + 1;

            // Address is 12 hexadecimal characters.
            if (wcslen(addressStart) < 12)
                return false;

            wchar_t addressString[13]{};
            wcsncpy_s(addressString, addressStart, 12);

            wchar_t *end = nullptr;

            unsigned long long value = wcstoull(addressString, &end, 16);

            if (end == addressString || *end != L'\0')
                return false;

            address.ullLong = value;

            LOGDS_D("[DualSense] Bluetooth address: %ls", addressString);

            return true;
        }

        DEVINST parent = 0;

        cr = CM_Get_Parent(&parent, devInst, 0);

        if (cr != CR_SUCCESS)
            break;

        devInst = parent;
    }

    return false;
}

bool DualSense::IsBluetoothDualSenseConnected(DEVINST devInst)
{
    BLUETOOTH_ADDRESS address{};

    if (!GetBluetoothAddressFromDevNode(devInst, address))
    {
        LOGDS_D("[DualSense] Failed to obtain Bluetooth address");
        return false;
    }

    BLUETOOTH_DEVICE_INFO deviceInfo{};

    deviceInfo.dwSize = sizeof(deviceInfo);
    deviceInfo.Address = address;

    DWORD result = BluetoothGetDeviceInfo(nullptr, &deviceInfo);

    if (result != ERROR_SUCCESS)
    {
        LOGDS_D("[DualSense] BluetoothGetDeviceInfo failed: %lu", result);
        return false;
    }

    LOGDS_D("[DualSense] Bluetooth device: connected=%s remembered=%s authenticated=%s", (deviceInfo.fConnected ? "YES" : "NO"), (deviceInfo.fRemembered ? "YES" : "NO"), (deviceInfo.fAuthenticated ? "YES" : "NO"));

    return deviceInfo.fConnected != FALSE;
}

const char *DualSense::TransportName(Transport transport)
{
    switch (transport)
    {
    case Transport::USB:
        return "USB";

    case Transport::Bluetooth:
        return "Bluetooth";

    default:
        return "None";
    }
}

bool DualSense::Start()
{
    std::lock_guard<std::mutex> lock(m_stateMutex);

    if (m_running)
        return true;

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

    if (!m_stopEvent)
    {
        LOGDS_E("[DualSense] CreateEvent(stop) failed: %lu", GetLastError());
        return false;
    }

    m_running = true;
    m_worker = std::thread(&DualSense::WorkerThread, this);
    return true;
}

void DualSense::Stop()
{
    {
        std::lock_guard<std::mutex> lock(m_stateMutex);

        if (!m_running)
            return;

        m_running = false;
    }

    // Wake the worker.
    if (m_stopEvent)
        SetEvent(m_stopEvent);

    // Cancel any pending HID read.
    HANDLE device =
        INVALID_HANDLE_VALUE;

    {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        device = m_device;
    }

    if (device != INVALID_HANDLE_VALUE)
        CancelIoEx(device, nullptr);

    if (m_worker.joinable())
        m_worker.join();

    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void DualSense::SetOnCreateButtonPressed(Callback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onCreateButtonPressed = std::move(callback);
}

void DualSense::SetOnConnected(Callback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onConnected = std::move(callback);
}

void DualSense::SetOnDisconnected(Callback callback)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_onDisconnected = std::move(callback);
}

bool DualSense::IsRunning() const
{
    std::lock_guard<std::mutex> lock(m_stateMutex);
    return m_running;
}

bool DualSense::IsConnected() const
{
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    return m_device != INVALID_HANDLE_VALUE;
}

void DualSense::WorkerThread()
{
    LOGDS_D("[DualSense] DualSense service started.");

    if (!CreateNotificationWindow())
    {
        LOGDS_E("[DualSense] Failed to create device notification window.");
        return;
    }

    // Initial scan.
    TryConnect();

    while (!ShouldStop())
    {
        if (IsConnected())
        {
            ReadResult result = ReadInputReports();

            if (result == ReadResult::Stopped)
                break;

            if (result == ReadResult::SwitchTransport)
            {
                LOGDS_D("[DualSense] Switching to preferred DualSense transport.");

                Disconnect();

                if (!ShouldStop())
                    TryConnect();

                continue;
            }

            // Normal disconnect.
            Disconnect();
        }
        else
            WaitForDeviceOrStop();
    }

    Disconnect();

    DestroyNotificationWindow();

    LOGDS_D("[DualSense] DualSense service stopped.");
}

// HID discovery
HANDLE DualSense::FindDualSense(Transport &selectedTransport)
{
    selectedTransport = Transport::None;
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    HDEVINFO devices = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);

    if (devices == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;

    HANDLE usbDevice = INVALID_HANDLE_VALUE;
    HANDLE bluetoothDevice = INVALID_HANDLE_VALUE;

    // --------------------------------------------------------
    // Enumerate ALL HID interfaces.
    //
    // USB = 64 byte input reports
    // BT  = 78 byte input reports
    //
    // USB always wins.
    // --------------------------------------------------------

    for (DWORD index = 0;; ++index)
    {
        SP_DEVICE_INTERFACE_DATA interfaceData{};

        interfaceData.cbSize = sizeof(interfaceData);

        if (!SetupDiEnumDeviceInterfaces(devices, nullptr, &hidGuid, index, &interfaceData))
        {
            if (GetLastError() == ERROR_NO_MORE_ITEMS)
                break;

            continue;
        }

        DWORD requiredSize = 0;

        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, nullptr, 0, &requiredSize, nullptr);

        if (requiredSize == 0)
            continue;

        std::vector<BYTE> detailBuffer(requiredSize);
        auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(detailBuffer.data());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        SP_DEVINFO_DATA deviceInfo{};
        deviceInfo.cbSize = sizeof(deviceInfo);

        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, requiredSize, nullptr, &deviceInfo))
            continue;

        const wchar_t *path = detail->DevicePath;

        // Check VID/PID.
        HANDLE handle = CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

        if (handle == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES attributes{};

        attributes.Size = sizeof(attributes);
        bool isDualSense = false;

        if (HidD_GetAttributes(handle, &attributes))
            isDualSense = attributes.VendorID == DUALSENSE_VID && attributes.ProductID == DUALSENSE_PID;

        CloseHandle(handle);

        if (!isDualSense)
            continue;

        // Open the HID interface.
        handle = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);

        if (handle == INVALID_HANDLE_VALUE)
            continue;

        PHIDP_PREPARSED_DATA preparsed = nullptr;

        if (!HidD_GetPreparsedData(handle, &preparsed))
        {
            CloseHandle(handle);
            continue;
        }

        HIDP_CAPS caps{};
        NTSTATUS status = HidP_GetCaps(preparsed, &caps);

        HidD_FreePreparsedData(preparsed);

        if (status != HIDP_STATUS_SUCCESS)
        {
            CloseHandle(handle);
            continue;
        }

        DWORD reportSize = caps.InputReportByteLength;

        if (reportSize == 64) // USB
        {
            LOGDS_D("[DualSense] Found DualSense (USB): %ls", path);

            if (usbDevice != INVALID_HANDLE_VALUE)
                CloseHandle(usbDevice);

            usbDevice = handle;
        }

        else if (reportSize == 78) // Bluetooth
        {
            if (!IsBluetoothDualSenseConnected(deviceInfo.DevInst))
            {
                LOGDS_D("[DualSense] Ignoring disconnected Bluetooth DualSense: %ls", path);
                CloseHandle(handle);
                continue;
            }

            LOGDS_D("[DualSense] Found DualSense (Bluetooth): %ls", path);

            if (bluetoothDevice != INVALID_HANDLE_VALUE)
                CloseHandle(bluetoothDevice);

            bluetoothDevice = handle;
        }
    }

    SetupDiDestroyDeviceInfoList(devices);

    // USB has priority over Bluetooth
    if (usbDevice != INVALID_HANDLE_VALUE)
    {
        if (bluetoothDevice != INVALID_HANDLE_VALUE)
            CloseHandle(bluetoothDevice);

        selectedTransport = Transport::USB;
        return usbDevice;
    }

    // Fall back to Bluetooth.
    if (bluetoothDevice != INVALID_HANDLE_VALUE)
    {
        selectedTransport = Transport::Bluetooth;
        return bluetoothDevice;
    }

    return INVALID_HANDLE_VALUE;
}

bool DualSense::TryConnect()
{
    if (ShouldStop())
        return false;

    if (IsConnected())
        return true;

    Transport transport = Transport::None;
    HANDLE handle = FindDualSense(transport);

    if (handle == INVALID_HANDLE_VALUE)
        return false;

    DWORD reportSize = 0;
    PHIDP_PREPARSED_DATA preparsed = nullptr;

    if (!HidD_GetPreparsedData(handle, &preparsed))
    {
        LOGDS_E("[DualSense] HidD_GetPreparsedData failed: %lu", GetLastError());
        CloseHandle(handle);
        return false;
    }

    HIDP_CAPS caps{};

    NTSTATUS status = HidP_GetCaps(preparsed, &caps);

    LOGDS_D("[DualSense] Caps: input=%u output=%u feature=%u", caps.InputReportByteLength, caps.OutputReportByteLength, caps.FeatureReportByteLength);

    HidD_FreePreparsedData(preparsed);

    if (status != HIDP_STATUS_SUCCESS)
    {
        LOGDS_E("[DualSense] HidP_GetCaps failed.");
        CloseHandle(handle);
        return false;
    }

    if (!InitializeDualSense(handle))
    {
        LOGDS_E("[DualSense] Initialization failed.");
        HidD_FreePreparsedData(preparsed);
        CloseHandle(handle);
        return false;
    }

    reportSize = caps.InputReportByteLength;

    if (reportSize != 64 && reportSize != 78)
    {
        LOGDS_E("[DualSense] Unexpected DualSense report size: %lu", reportSize);
        CloseHandle(handle);
        return false;
    }

    // Set up device state.
    {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        m_device = handle;
        m_reportSize = reportSize;
        m_transport = transport;
        m_switchTransportRequested = false;
        m_buffer.resize(m_reportSize);
        m_readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);

        if (!m_readEvent)
        {
            LOGDS_E("[DualSense] CreateEvent(read) failed: %lu", GetLastError());
            CloseHandle(m_device);
            m_device = INVALID_HANDLE_VALUE;
            m_reportSize = 0;
            m_transport = Transport::None;
            m_buffer.clear();
            return false;
        }

        m_overlapped = {};
        m_overlapped.hEvent = m_readEvent;
    }

    LOGDS_D("[DualSense] DualSense connected via %s. Report size: %lu bytes", TransportName(transport), m_reportSize);

    InvokeConnected();
    return true;
}

void DualSense::Disconnect()
{
    HANDLE device = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    Transport transport = Transport::None;

    {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        device = m_device;
        event = m_readEvent;
        transport = m_transport;
        m_device = INVALID_HANDLE_VALUE;
        m_readEvent = nullptr;
        m_reportSize = 0;
        m_transport = Transport::None;
        m_switchTransportRequested = false;
        m_buffer.clear();
        m_overlapped = {};
    }

    if (device != INVALID_HANDLE_VALUE)
    {
        CancelIoEx(device, nullptr);
        CloseHandle(device);
    }

    if (event)
        CloseHandle(event);

    if (device != INVALID_HANDLE_VALUE)
    {
        LOGDS_D("[DualSense] DualSense disconnected (%s)", TransportName(transport));
        InvokeDisconnected();
    }
}

DualSense::ReadResult DualSense::ReadInputReports()
{
    auto nextProcessTime = std::chrono::steady_clock::now();
    auto screenshotCooldownUntil = std::chrono::steady_clock::now();

    while (!ShouldStop() && IsConnected())
    {
        // ----------------------------------------------------
        // Pump any messages already waiting.
        //
        // This is critical.
        //
        // The notification window lives on this worker thread.
        // ----------------------------------------------------

        MSG message;

        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                return ReadResult::Stopped;

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (ShouldStop())
            return ReadResult::Stopped;

        HANDLE device;
        HANDLE readEvent;
        DWORD reportSize;

        {
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            device = m_device;
            readEvent = m_readEvent;
            reportSize = m_reportSize;
        }

        if (device == INVALID_HANDLE_VALUE || !readEvent || reportSize == 0)
            return ReadResult::Disconnected;

        ResetEvent(readEvent);

        DWORD bytesRead = 0;

        BOOL result = ReadFile(device, m_buffer.data(), reportSize, nullptr, &m_overlapped);

        if (!result)
        {
            DWORD error = GetLastError();

            if (error != ERROR_IO_PENDING)
            {
                // ------------------------------------------------
                // If the notification handler requested a
                // transport switch, this read may have been
                // cancelled deliberately.
                // ------------------------------------------------
                if (IsSwitchTransportRequested())
                    return ReadResult::SwitchTransport;

                LOGDS_E("[DualSense] ReadFile ended: %lu", error);
                return ReadResult::Disconnected;
            }
        }

        HANDLE waitHandles[] = {readEvent, m_stopEvent};

        // ----------------------------------------------------
        // IMPORTANT:
        //
        // MsgWaitForMultipleObjects lets the thread wake for:
        //
        //   0 = HID read completed
        //   1 = Stop event
        //   2 = Windows message
        //
        // This means WM_DEVICECHANGE can now be processed
        // while a Bluetooth HID read is pending.
        // ----------------------------------------------------

        DWORD waitResult = MsgWaitForMultipleObjects(2, waitHandles, FALSE, INFINITE, QS_ALLINPUT);

        if (waitResult == WAIT_OBJECT_0)
        {
            // HID read completed.
        }
        else if (waitResult == WAIT_OBJECT_0 + 1)
        {
            // Stop requested.
            CancelIoEx(device, &m_overlapped);
            return ReadResult::Stopped;
        }
        else if (waitResult == WAIT_OBJECT_0 + 2)
        {
            // ------------------------------------------------
            // Windows message arrived.
            //
            // Pump it. WindowProc() may request a transport
            // switch, which cancels our pending read.
            // ------------------------------------------------

            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_QUIT)
                    return ReadResult::Stopped;

                TranslateMessage(&message);
                DispatchMessageW(&message);
            }

            if (ShouldStop())
                return ReadResult::Stopped;

            if (IsSwitchTransportRequested())
                return ReadResult::SwitchTransport;

            // No transport switch.
            // Continue waiting for the HID read.
            continue;
        }
        else
            return ReadResult::Disconnected;

        // HID read completed.
        if (!GetOverlappedResult(device, &m_overlapped, &bytesRead, FALSE))
        {
            DWORD error = GetLastError();

            if (IsSwitchTransportRequested())
                return ReadResult::SwitchTransport;

            // ERROR_OPERATION_ABORTED can happen during
            // shutdown/disconnect.
            if (error == ERROR_OPERATION_ABORTED)
            {
                if (ShouldStop())
                    return ReadResult::Stopped;

                return ReadResult::Disconnected;
            }

            LOGDS_E("[DualSense] GetOverlappedResult ended: %lu", error);
            return ReadResult::Disconnected;
        }

        if (bytesRead == 0)
            continue;

        {
            // -------------------------------------------------
            // DEBUG ONLY

            // printf("Report (%lu bytes): ", bytesRead);
            // for (DWORD i = 0; i < bytesRead; ++i)
            //     printf("%02X ", m_buffer[i]);
            // printf("\n");
            // -------------------------------------------------
        }

        const auto now = std::chrono::steady_clock::now();

        // Screenshot cooldown
        if (now < screenshotCooldownUntil)
            continue;

        // Report throttle
        if (now < nextProcessTime)
            continue;

        nextProcessTime = now + std::chrono::milliseconds(REPORT_THROTTLE_MS);

        size_t buttonOffset;

        /**
         * byte 0 is Report ID
         * byte0 == 0x31: extended report
         * byte0 == 0x01: short report
         *
         * TODO: to confirm
         */
        if (bytesRead == 78) // Bluetooth
            buttonOffset = m_buffer[0] == 0x31 ? 10 : 6;
        else if (bytesRead == 64) // USB
            buttonOffset = 9;
        else
            continue;

        if (buttonOffset >= bytesRead)
            continue;

        ////// Battery status
        if (bytesRead == 64 || m_buffer[0] == 0x01)
            m_batteryLevel = -1;
        else
        {
            const BYTE status = m_buffer[54];
            m_batteryLevel = status & 0x0F;
            m_isCharging = ((status >> 4) & 0x0F) == 0x2;

            // LOGDS_D("Battery: %u/10, charging status: %s (0x%X)\n", batteryLevel, chargingStatus == 0x0 ? "no" : "yes", chargingStatus);
        }
        //////

        ////// Special buttons
        const BYTE buttons = m_buffer[buttonOffset];

        // DualSense Create button
        if (buttons & 0x10)
        {
            LOGDS_D("[DualSense] Create button pressed [%s]", bytesRead == 78 ? "BT" : "USB");
            InvokeCreateButton();
            screenshotCooldownUntil = now + std::chrono::milliseconds(SCREENSHOT_COOLDOWN_MS);
        }

        // DualSense Options button is using the same byte
        // if (buttons & 0x20)
        //     LOGDS_D("[DualSense] Options button pressed");
        //////
    }

    if (ShouldStop())
        return ReadResult::Stopped;

    return ReadResult::Disconnected;
}

// Transport switch state
bool DualSense::IsSwitchTransportRequested() const
{
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    return m_switchTransportRequested;
}

// Device change handling
// --------------------------------------------------------
// Transport changed.
//
// Example:
//
//   Bluetooth active
//       +
//   USB plugged in
//
// FindDualSense() says USB is preferred.
//
// Cancel the current BT read. ReadInputReports() will
// return SwitchTransport, and WorkerThread() will then:
//
//   Disconnect BT
//   TryConnect()
//
// which opens USB.
// --------------------------------------------------------

void DualSense::HandleDeviceChange(WPARAM changeType)
{
    if (ShouldStop())
        return;

    if (changeType == DEVICE_CHANGE_BT_DISCONNECTED)
    {
        Transport transport;

        {
            std::lock_guard<std::mutex> lock(m_deviceMutex);
            transport = m_transport;
        }

        if (transport == Transport::Bluetooth)
        {
            Disconnect();
            return;
        }
    }

    Transport preferredTransport = Transport::None;
    HANDLE testHandle = FindDualSense(preferredTransport);

    if (testHandle != INVALID_HANDLE_VALUE)
        CloseHandle(testHandle);

    Transport currentTransport = Transport::None;
    HANDLE currentDevice = INVALID_HANDLE_VALUE;

    {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        currentTransport = m_transport;
        currentDevice = m_device;
    }

    if (currentDevice == INVALID_HANDLE_VALUE)
    {
        TryConnect();
        return;
    }

    if (preferredTransport == Transport::None)
        return;

    if (currentTransport == preferredTransport)
        return;

    LOGDS_D("[DualSense] Preferred transport changed: %s -> %s", TransportName(currentTransport), TransportName(preferredTransport));

    {
        std::lock_guard<std::mutex> lock(m_deviceMutex);
        m_switchTransportRequested = true;
    }

    if (currentDevice != INVALID_HANDLE_VALUE)
        CancelIoEx(currentDevice, nullptr);
}

// Device notification window
LRESULT CALLBACK DualSense::WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        auto *createStruct = reinterpret_cast<CREATESTRUCTW *>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(createStruct->lpCreateParams));
        return TRUE;
    }

    auto *self = reinterpret_cast<DualSense *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    // Windows device notification
    if (message == WM_DEVICECHANGE)
    {
        // {
        //     // DEBUG
        //     LOGDS_D("[DualSense] WM_DEVICECHANGE wParam=0x%08lX", static_cast<unsigned long>(wParam));
        //     if (lParam)
        //     {
        //         auto *header = reinterpret_cast<DEV_BROADCAST_HDR *>(lParam);
        //         LOGDS_D("[DualSense] device type=%lu", header->dbch_devicetype);

        //         if (header->dbch_devicetype == DBT_DEVTYP_HANDLE)
        //         {
        //             auto *handle = reinterpret_cast<DEV_BROADCAST_HANDLE *>(lParam);
        //             const GUID &guid = handle->dbch_eventguid;

        //             LOGDS_D(
        //                 "[DualSense] HANDLE event GUID: "
        //                 "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        //                 guid.Data1,
        //                 guid.Data2,
        //                 guid.Data3,
        //                 guid.Data4[0],
        //                 guid.Data4[1],
        //                 guid.Data4[2],
        //                 guid.Data4[3],
        //                 guid.Data4[4],
        //                 guid.Data4[5],
        //                 guid.Data4[6],
        //                 guid.Data4[7]);
        //         }
        //     }
        // }

        if (wParam == DBT_CUSTOMEVENT && lParam)
        {
            auto *header = reinterpret_cast<DEV_BROADCAST_HDR *>(lParam);

            if (header->dbch_devicetype == DBT_DEVTYP_HANDLE)
            {
                auto *event = reinterpret_cast<DEV_BROADCAST_HANDLE *>(lParam);

                if (IsEqualGUID(event->dbch_eventguid, GUID_BLUETOOTH_HCI_EVENT))
                {
                    auto *hci = reinterpret_cast<const BTH_HCI_EVENT_INFO *>(event->dbch_data);
                    LOGDS_D("[DualSense] Bluetooth HCI: address=%llX connected=%u", static_cast<unsigned long long>(hci->bthAddress), static_cast<unsigned>(hci->connected));
                    PostMessageW(hwnd, WM_DUALSENSE_DEVICE_CHANGE, hci->connected ? DEVICE_CHANGE_BT_CONNECTED : DEVICE_CHANGE_BT_DISCONNECTED, 0);
                    return TRUE;
                }
            }
        }

        if (self)
        {
            if (wParam == DBT_DEVICEARRIVAL || wParam == DBT_DEVICEREMOVECOMPLETE || wParam == DBT_DEVNODES_CHANGED)
                PostMessageW(hwnd, WM_DUALSENSE_DEVICE_CHANGE, DEVICE_CHANGE_GENERIC, 0); // Posting a message so the HID scan happens cleanly on the worker thread
        }

        return TRUE;
    }

    // Our device-change message
    if (message == WM_DUALSENSE_DEVICE_CHANGE)
    {
        if (self && !self->ShouldStop())
            self->HandleDeviceChange(wParam);

        return 0;
    }

    if (message == WM_CLOSE)
    {
        DestroyWindow(hwnd);
        return 0;
    }

    if (message == WM_DESTROY)
    {
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Notification window
bool DualSense::CreateNotificationWindow()
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    const wchar_t *className = L"DualSenseServiceWindow";
    WNDCLASSW wc{};
    wc.lpfnWndProc = &DualSense::WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = className;

    if (!RegisterClassW(&wc))
    {
        DWORD error = GetLastError();
        if (error != ERROR_CLASS_ALREADY_EXISTS)
        {
            LOGDS_E("[DualSense] RegisterClassW failed: %lu", error);
            return false;
        }
    }

    m_hwnd = CreateWindowExW(0, className, L"DualSense Service", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);

    if (!m_hwnd)
    {
        LOGDS_E("[DualSense] CreateWindowExW failed: %lu", GetLastError());
        return false;
    }

    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    filter.dbcc_classguid = hidGuid;
    m_notificationHandle = RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    if (!m_notificationHandle)
    {
        LOGDS_E("[DualSense] RegisterDeviceNotificationW failed: %lu", GetLastError());
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
        return false;
    }

    LOGDS_D("[DualSense] Device notification registered.");

    BLUETOOTH_FIND_RADIO_PARAMS params{};
    params.dwSize = sizeof(params);

    HBLUETOOTH_RADIO_FIND radioFind = BluetoothFindFirstRadio(&params, &m_bluetoothRadio);

    if (!radioFind)
        LOGDS_E("[DualSense] BluetoothFindFirstRadio failed: %lu", GetLastError());
    else
    {
        BluetoothFindRadioClose(radioFind);

        DEV_BROADCAST_HANDLE bluetoothFilter{};
        bluetoothFilter.dbch_size = sizeof(bluetoothFilter);
        bluetoothFilter.dbch_devicetype = DBT_DEVTYP_HANDLE;
        bluetoothFilter.dbch_handle = m_bluetoothRadio;

        m_bluetoothNotificationHandle = RegisterDeviceNotificationW(m_hwnd, &bluetoothFilter, DEVICE_NOTIFY_WINDOW_HANDLE);

        if (!m_bluetoothNotificationHandle)
        {
            LOGDS_E("[DualSense] Bluetooth RegisterDeviceNotification failed: %lu", GetLastError());
            CloseHandle(m_bluetoothRadio);
            m_bluetoothRadio = nullptr;
        }
        else
            LOGDS_D("[DualSense] Bluetooth device notification registered.");
    }

    return true;
}

void DualSense::DestroyNotificationWindow()
{
    if (m_notificationHandle)
    {
        UnregisterDeviceNotification(m_notificationHandle);
        m_notificationHandle = nullptr;
    }

    if (m_bluetoothRadio)
    {
        CloseHandle(m_bluetoothRadio);
        m_bluetoothRadio = nullptr;
    }

    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
}

// Wait for device
void DualSense::WaitForDeviceOrStop()
{
    MSG message;

    while (!ShouldStop())
    {
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
                return;

            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        if (IsConnected())
            return;

        // Wait for stop or a Windows message.
        HANDLE handles[] = {m_stopEvent};
        DWORD result = MsgWaitForMultipleObjects(1, handles, FALSE, INFINITE, QS_ALLINPUT);
        if (result == WAIT_OBJECT_0)
            return;

        // WAIT_OBJECT_0 + 1 means a Windows message arrived.
        // Loop around and process it.
    }
}

void DualSense::InvokeCreateButton()
{
    Callback callback;

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_onCreateButtonPressed;
    }

    if (callback)
        callback();
}

void DualSense::InvokeConnected()
{
    Callback callback;

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_onConnected;
    }

    if (callback)
        callback();
}

void DualSense::InvokeDisconnected()
{
    Callback callback;

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        callback = m_onDisconnected;
    }

    if (callback)
        callback();
}

DualSense::Transport DualSense::GetTransport() const
{
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    return m_transport;
}

bool DualSense::InitializeDualSense(HANDLE handle)
{
    BYTE calibration[41] = {};
    calibration[0] = 0x05;

    if (!HidD_GetFeature(handle, calibration, sizeof(calibration)))
    {
        LOGDS_E("HidD_GetFeature(0x05) failed: %lu\n", GetLastError());
        return false;
    }

    LOGDS_D("DualSense calibration feature read OK\n");

    return true;
}