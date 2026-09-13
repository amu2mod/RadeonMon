#pragma once

#include "radeonmon/logging.hpp"
#include "radeonmon/Screenshot.hpp"
#include "radeonmon/gamepad.hpp"

#include <windows.h>
#include <bluetoothapis.h>
#include <cfgmgr32.h>

#include <functional>
#include <mutex>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "Cfgmgr32.lib")

#ifdef LOGDS
#define LOGDS_D(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#define LOGDS_E(fmt, ...) LOG_IMPL("E", COLOR_RED, fmt, ##__VA_ARGS__)
#else
#define LOGDS_D(fmt, ...) ((void)0)
#define LOGDS_E(fmt, ...) ((void)0)
#endif

/**
 * DualSense controller implementation using direct Windows HID access.
 *
 * Technical choice:
 * - Uses the Windows HID device interface (CreateFile/ReadFile + OVERLAPPED I/O) which is a low level access.
 * - HidD_GetPreparsedData() is used to inspect the HID report capabilities;
 *   input reports are then read directly from the HID device handle.
 *
 * Threading:
 * - HID reads are performed asynchronously on the worker thread.
 * - The worker uses MsgWaitForMultipleObjects() so the same thread can both
 *   wait for HID input and process the notification window's Windows messages.
 *
 * TODO: Switch to Raw Input API
 */
class DualSense : public GamePad
{
  public:
	static const char *TransportName(Transport transport);
	int m_batteryLevel = -1;
	bool m_isCharging = false;

  public:
	DualSense() = default;
	inline ~DualSense() { Stop(); }
	DualSense(const DualSense &) = delete;
	DualSense &operator=(const DualSense &) = delete;

	// GamePad Interface
	bool Start() override;
	void Stop() override;
	int BatteryLevel() const override;
	inline bool IsCharging() const override { return m_isCharging; };
	Transport GetTransport() const override;

	// API
	void SetOnButtonPressed(Callback callback) override;
	void SetOnConnected(Callback callback) override;
	void SetOnDisconnected(Callback callback) override;

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
	static constexpr DWORD SCREENSHOT_COOLDOWN_MS = Screenshot::MIN_INTERVAL_MS;
	static constexpr UINT WM_DUALSENSE_DEVICE_CHANGE = WM_APP + 1;
	static constexpr WPARAM DEVICE_CHANGE_GENERIC = 0;
	static constexpr WPARAM DEVICE_CHANGE_BT_CONNECTED = 1;
	static constexpr WPARAM DEVICE_CHANGE_BT_DISCONNECTED = 2;
	static constexpr UINT_PTR DEVICE_CHANGE_TIMER_ID = 3;
	static constexpr UINT DEVICE_CHANGE_DEBOUNCE_MS = 1000; // plenty room to avoid windows event spamming

	UINT_PTR m_pendingChangeType = DEVICE_CHANGE_GENERIC; // last non-disconnect reason seen

	mutable std::mutex m_stateMutex;
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
	bool InitializeDualSense(HANDLE);
	void WorkerThread();
	HANDLE FindDualSense(Transport &selectedTransport);
	bool TryConnect(HANDLE existingHandle = INVALID_HANDLE_VALUE, Transport existingTransport = Transport::None);
	void Disconnect();
	ReadResult ReadInputReports();
	bool IsSwitchTransportRequested() const;
	void HandleDeviceChange(WPARAM changeType);
	bool CreateNotificationWindow();
	void DestroyNotificationWindow();
	void WaitForDeviceOrStop();
	inline bool ShouldStop() const { return m_stopEvent && WaitForSingleObject(m_stopEvent, 0) == WAIT_OBJECT_0; }
	bool GetBluetoothAddressFromDevNode(DEVINST devInst, BLUETOOTH_ADDRESS &address);
	bool IsBluetoothDualSenseConnected(DEVINST devInst);
	inline bool IsConnected() const { return m_device != INVALID_HANDLE_VALUE; }

	// Callbacks
	void InvokeCreateButton();
	void InvokeConnected();
	void InvokeDisconnected();

	// win32 proc
	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
};