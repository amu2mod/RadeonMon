#pragma once

#define WIN32_LEAN_AND_MEAN

#include "radeonmon/logging.hpp"
#include "GamePad.hpp"

#include <windows.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef LOGXBX
#define LOGXBX_D(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#define LOGXBX_E(fmt, ...) LOG_IMPL("E", COLOR_RED, fmt, ##__VA_ARGS__)
#else
#define LOGXBX_D(fmt, ...) ((void)0)
#define LOGXBX_E(fmt, ...) ((void)0)
#endif

/**
 * Windows API: Raw Input + WinRT
 */
class XboxWirelessController : public GamePad
{
public:
	enum class Button : uint8_t
	{
		View,
		Menu,
		XBOX,
		Share,

		Count
	};

	XboxWirelessController();
	~XboxWirelessController() override;
	XboxWirelessController(const XboxWirelessController &) = delete;
	XboxWirelessController &operator=(const XboxWirelessController &) = delete;

	bool Start() override;
	void Stop() override;
	int BatteryLevel() const override;
	bool IsCharging() const override;
	Transport GetTransport() const override;
	void SetOnButtonPressed(Callback callback) override;
	void SetOnConnected(Callback callback) override;
	void SetOnDisconnected(Callback callback) override;
	inline void SetButton(Button b) { m_button = b; }
	inline Button GetButton() { return m_button; }

	void Debug(); // Prints controller/HID information.

private:
	static constexpr USHORT XBOX_VID = 0x045E;
	static constexpr USHORT XBOX_PID_BLUETOOTH = 0x0B13;
	static constexpr USHORT XBOX_PID_USB = 0x02FF;
	static constexpr UINT WM_DEBUG = WM_APP + 1;

	static constexpr ButtonInfo BUTTON_MAPPING[] = {
		{11, 0x40, "View"},
		{11, 0x80, "Menu"},
		{12, 0x04, "XBOX"},
		{12, 0x08, "Share"},
	};

	static_assert(static_cast<size_t>(Button::Count) == _countof(BUTTON_MAPPING), "Button::Count does not match BUTTON_MAPPING");

	Button m_button = Button::Share;

	static const wchar_t *WindowClassName() { return L"XboxWirelessControllerRawInputWindow"; }

	std::atomic<bool> m_running{false};
	std::atomic<int> m_batteryLevel{-1};
	std::atomic<bool> m_connected{false};
	std::atomic<DWORD> m_inputValidSince{0};
	std::thread m_worker;
	DWORD m_workerThreadId = 0;
	HWND m_hwnd = nullptr;
	Transport m_transport = Transport::None;
	mutable std::mutex m_stateMutex;
	std::condition_variable m_stateCv;
	bool m_initialized = false;
	bool m_initSuccess = false;
	uint64_t m_bluetoothAddress = 0;
	HANDLE m_hidDevice = nullptr;
	HidInfo m_hidInfo{};

	winrt::Windows::Devices::Bluetooth::BluetoothLEDevice m_bluetoothDevice{nullptr};
	winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic m_batteryCharacteristic{nullptr};
	winrt::event_token m_batteryValueChangedToken{};

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
	static bool IsXboxDevice(const HidInfo &info);
	static void DumpHex(const BYTE *data, UINT size, UINT intervalMs = 100);
	void ParseSpecialButtons(const BYTE *report, UINT size);
	static bool ExtractBluetoothAddress(const std::wstring &path, uint64_t &address);
	HWND GetWindowHandle() const;
	void SetConnected(bool connected, Transport transport);
	void FireCreateButtonPressed();
	void FireConnected();
	void FireDisconnected();
	bool InitializeBattery(uint64_t address);
	int ReadBatteryValue();
	void ShutdownBattery();
	void OnBatteryValueChanged(winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic const &sender, winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattValueChangedEventArgs const &args);
};
