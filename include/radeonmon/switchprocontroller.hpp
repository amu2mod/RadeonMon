#pragma once

#define WIN32_LEAN_AND_MEAN

#include "radeonmon/logging.hpp"
#include "GamePad.hpp"

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>

#ifdef LOGSW
#define LOGSW_D(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#define LOGSW_E(fmt, ...) LOG_IMPL("E", COLOR_RED, fmt, ##__VA_ARGS__)
#else
#define LOGSW_D(fmt, ...) ((void)0)
#define LOGSW_E(fmt, ...) ((void)0)
#endif

/**
 * Windows API: Raw Input
 */
class SwitchProController : public GamePad
{
public:
	SwitchProController();
	~SwitchProController() override;

	SwitchProController(const SwitchProController &) = delete;
	SwitchProController &operator=(const SwitchProController &) = delete;

	bool Start() override;
	void Stop() override;

	int BatteryLevel() const override;

	bool IsCharging() const override;
	Transport GetTransport() const override;

	void SetOnButtonPressed(Callback callback) override;
	void SetOnConnected(Callback callback) override;
	void SetOnDisconnected(Callback callback) override;

	// Debugging method: enables live HID report printing
	bool LiveReport();

	// Prints controller/HID information.
	void Debug();

private:
	static constexpr USHORT SWITCH_PRO_CONTROLLER_VID = 0x057E;
	static constexpr USHORT SWITCH_PRO_CONTROLLER_PID = 0x2009;

	static constexpr UINT SWITCH_PRO_USB_INPUT_REPORT_LENGTH = 64;
	static constexpr UINT SWITCH_PRO_BLUETOOTH_INPUT_REPORT_LENGTH = 362;

	static constexpr UINT WM_DEBUG = WM_APP + 1;

	static const wchar_t *WindowClassName() { return L"SwitchProControllerRawInputWindow"; }

	std::atomic<bool> m_running{false};
	std::atomic<bool> m_liveReport{false};
	std::atomic<int> m_batteryLevel{-1};
	std::atomic<bool> m_charging{false};
	std::atomic<bool> m_connected{false};
	BYTE m_lastBluetoothReport = 0xFF;
	std::thread m_worker;
	DWORD m_workerThreadId = 0;
	HWND m_hwnd = nullptr;
	Transport m_transport = Transport::None;
	mutable std::mutex m_stateMutex;
	std::condition_variable m_stateCv;
	bool m_initialized = false;
	bool m_initSuccess = false;
	HANDLE m_hidDevice = nullptr;
	HidInfo m_hidInfo{};
	std::chrono::steady_clock::time_point m_lastCapture{};

	Callback m_onCreateButtonPressed;
	Callback m_onConnected;
	Callback m_onDisconnected;

	static LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
	void WorkerMain();
	void RunMessageLoop();
	bool RegisterRawInput();
	void HandleRawInput(HRAWINPUT hRawInput);
	void HandleDeviceChange(WPARAM wParam, LPARAM lParam);
	static bool GetHidInfo(HANDLE device, HidInfo &info);
	void EnumerateDevices();
	bool InitializeBluetoothInputMode(const HidInfo &);
	static bool IsSwitchProControllerDevice(const HidInfo &info);
	static void DumpHex(const BYTE *data, UINT size, UINT intervalMs = 100);
	void ParseUsbReport(const BYTE *report, UINT size);
	void ParseBluetoothReport(const BYTE *report, UINT size, const HidInfo &info);
	HWND GetWindowHandle() const;
	void SetConnected(bool connected, Transport transport);
	void FireCaptureButtonPressed();
	void FireConnected();
	void FireDisconnected();
};
