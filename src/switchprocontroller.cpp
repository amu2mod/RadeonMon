#define WIN32_LEAN_AND_MEAN
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS

#include "radeonmon/switchprocontroller.hpp"
#include "radeonmon/Screenshot.hpp"

#include <hidsdi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Storage.Streams.h>

#include <cstdio>
#include <cwctype>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "windowsapp.lib")

SwitchProController::SwitchProController() {}
SwitchProController::~SwitchProController() { Stop(); }

bool SwitchProController::Start()
{
	if (m_running.load())
		return true;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);

		m_initialized = false;
		m_initSuccess = false;

		m_hwnd = nullptr;
		m_workerThreadId = 0;

		m_transport = Transport::None;

		m_running.store(true);
		m_batteryLevel.store(-1);
		m_charging.store(false);
	}

	try
	{
		m_worker = std::thread(&SwitchProController::WorkerMain, this);
	}
	catch (...)
	{
		LOGSW_E("[SwitchProCtl] Failed to create worker thread");
		m_running.store(false);
		return false;
	}

	std::unique_lock<std::mutex> lock(m_stateMutex);

	m_stateCv.wait(lock, [this]
				   { return m_initialized; });

	const bool success = m_initSuccess;

	lock.unlock();

	if (!success && m_worker.joinable())
		m_worker.join();

	return success;
}

void SwitchProController::Stop()
{
	m_running.store(false);
	m_liveReport.store(false);
	m_batteryLevel.store(-1);
	m_charging.store(false);

	const HWND hwnd = GetWindowHandle();

	if (hwnd != nullptr)
		PostMessageW(hwnd, WM_CLOSE, 0, 0);

	if (m_worker.joinable())
		m_worker.join();

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_hwnd = nullptr;
		m_workerThreadId = 0;
		m_initialized = false;
		m_initSuccess = false;
	}
}

bool SwitchProController::LiveReport()
{
	if (!m_running.load())
	{
		LOGSW_E("[SwitchProCtl] LiveReport(): controller is not open");
		return false;
	}

	m_liveReport.store(true);

	LOGSW_D("[SwitchProCtl] Live reporting enabled");

	return true;
}

void SwitchProController::Debug()
{
	if (!m_running.load())
	{
		LOGSW_E("[SwitchProCtl] Debug(): controller is not open");
		return;
	}

	const HWND hwnd = GetWindowHandle();

	if (hwnd == nullptr)
		return;

	PostMessageW(hwnd, WM_DEBUG, 0, 0);
}

void SwitchProController::WorkerMain()
{
	winrt::init_apartment(winrt::apartment_type::multi_threaded);

	m_workerThreadId = GetCurrentThreadId();

	const HINSTANCE instance = GetModuleHandleW(nullptr);

	WNDCLASSW wc{};

	wc.lpfnWndProc = &SwitchProController::WindowProc;
	wc.hInstance = instance;
	wc.lpszClassName = WindowClassName();

	const ATOM atom = RegisterClassW(&wc);

	if (atom == 0)
	{
		const DWORD error = GetLastError();

		if (error != ERROR_CLASS_ALREADY_EXISTS)
		{
			LOGSW_E("[SwitchProCtl] RegisterClassW failed: %lu", error);

			{
				std::lock_guard<std::mutex> lock(m_stateMutex);
				m_initSuccess = false;
				m_initialized = true;
			}

			m_running.store(false);
			m_stateCv.notify_one();

			return;
		}
	}

	HWND hwnd = CreateWindowExW(0, WindowClassName(), L"Switch Pro Controller", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);

	if (hwnd == nullptr)
	{
		LOGSW_E("[SwitchProCtl] CreateWindowExW failed: %lu", GetLastError());

		{
			std::lock_guard<std::mutex> lock(m_stateMutex);
			m_initSuccess = false;
			m_initialized = true;
		}

		m_running.store(false);
		m_stateCv.notify_one();

		return;
	}

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_hwnd = hwnd;
	}

	if (!RegisterRawInput())
	{
		LOGSW_E("[SwitchProCtl] RegisterRawInputDevices failed: %lu", GetLastError());

		DestroyWindow(hwnd);

		{
			std::lock_guard<std::mutex> lock(m_stateMutex);
			m_hwnd = nullptr;
			m_initSuccess = false;
			m_initialized = true;
		}

		m_running.store(false);
		m_stateCv.notify_one();

		return;
	}

	// Initialization complete

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_initSuccess = true;
		m_initialized = true;
	}

	m_stateCv.notify_one();

	// Find Switch Pro Controller / Bluetooth address
	EnumerateDevices();

	// Worker message loop
	RunMessageLoop();

	// Remove registration post loop
	RAWINPUTDEVICE rid[2]{};

	rid[0].usUsagePage = 0x01;
	rid[0].usUsage = 0x04;
	rid[0].dwFlags = RIDEV_REMOVE;
	rid[0].hwndTarget = nullptr;

	rid[1].usUsagePage = 0x01;
	rid[1].usUsage = 0x05;
	rid[1].dwFlags = RIDEV_REMOVE;
	rid[1].hwndTarget = nullptr;

	if (!RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)))
		LOGSW_E("[SwitchProCtl] Failed to remove Raw Input registration: %lu", GetLastError());

	// Cleanup window
	if (hwnd != nullptr)
		DestroyWindow(hwnd);

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_hwnd = nullptr;
	}

	m_running.store(false);
	m_liveReport.store(false);
}

void SwitchProController::RunMessageLoop()
{
	MSG msg{};

	while (m_running.load())
	{
		const BOOL result = GetMessageW(&msg, nullptr, 0, 0);

		if (result == -1)
		{
			LOGSW_E("[SwitchProCtl] GetMessageW failed: %lu", GetLastError());
			break;
		}

		if (result == 0)
			break;

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

LRESULT CALLBACK SwitchProController::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto *self = reinterpret_cast<SwitchProController *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	if (msg == WM_NCCREATE)
	{
		auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
		self = reinterpret_cast<SwitchProController *>(create->lpCreateParams);
		SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
	}

	if (self == nullptr)
		return DefWindowProcW(hwnd, msg, wParam, lParam);

	switch (msg)
	{
	case WM_INPUT:
		self->HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
		return 0;

	case WM_DEBUG:
		self->EnumerateDevices();
		return 0;

	case WM_INPUT_DEVICE_CHANGE:
		self->HandleDeviceChange(wParam, lParam);
		return 0;

	case WM_CLOSE:
		self->m_running.store(false);
		DestroyWindow(hwnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	default:
		return DefWindowProcW(hwnd, msg, wParam, lParam);
	}
}

bool SwitchProController::RegisterRawInput()
{
	RAWINPUTDEVICE rid[2]{};

	// USB
	rid[0].usUsagePage = 0x01;
	rid[0].usUsage = 0x04; // Joystick
	rid[0].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
	rid[0].hwndTarget = GetWindowHandle();

	// Bluetooth
	rid[1].usUsagePage = 0x01;
	rid[1].usUsage = 0x05; // GamePad
	rid[1].dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
	rid[1].hwndTarget = GetWindowHandle();

	return RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)) != FALSE;
}

void SwitchProController::HandleRawInput(HRAWINPUT hRawInput)
{
	if (!m_running.load())
		return;

	UINT size = 0;

	if (GetRawInputData(hRawInput, RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1))
		return;

	if (size == 0)
		return;

	std::vector<BYTE> buffer(size);

	UINT bytes = GetRawInputData(hRawInput, RID_INPUT, buffer.data(), &size, sizeof(RAWINPUTHEADER));

	if (bytes == static_cast<UINT>(-1))
		return;

	auto *raw = reinterpret_cast<RAWINPUT *>(buffer.data());

	if (raw->header.dwType != RIM_TYPEHID)
		return;

	if (raw->header.hDevice != m_hidDevice)
		return;

	const RAWHID &hid = raw->data.hid;

	// std::printf("[RAW INPUT] "
	// 			"VID=%04X "
	// 			"PID=%04X "
	// 			"UsagePage=0x%04X "
	// 			"Usage=0x%04X "
	// 			"ReportSize=%u "
	// 			"Count=%u\n",
	// 			info.vid, info.pid, info.usagePage, info.usage, hid.dwSizeHid, hid.dwCount);

	for (UINT i = 0; i < hid.dwCount; ++i)
	{
		const BYTE *report = hid.bRawData + (i * hid.dwSizeHid);
		// DumpHex(report, hid.dwSizeHid);

		if (hid.dwSizeHid == SWITCH_PRO_USB_INPUT_REPORT_LENGTH)
			ParseUsbReport(report, hid.dwSizeHid);
		else if (hid.dwSizeHid == SWITCH_PRO_BLUETOOTH_INPUT_REPORT_LENGTH)
			ParseBluetoothReport(report, hid.dwSizeHid, m_hidInfo);
		else
			LOGSW_D("[SwitchProCtl] Unknown report size: %u", hid.dwSizeHid);
	}
}

bool SwitchProController::GetHidInfo(HANDLE device, HidInfo &info)
{
	UINT size = 0;

	if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, nullptr, &size) == static_cast<UINT>(-1))
		return false;

	if (size == 0)
		return false;

	std::vector<BYTE> preparsed(size);

	if (GetRawInputDeviceInfoW(device, RIDI_PREPARSEDDATA, preparsed.data(), &size) == static_cast<UINT>(-1))
		return false;

	auto *ppd = reinterpret_cast<PHIDP_PREPARSED_DATA>(preparsed.data());

	HIDP_CAPS caps{};

	if (HidP_GetCaps(ppd, &caps) != HIDP_STATUS_SUCCESS)
		return false;

	info.usagePage = caps.UsagePage;
	info.usage = caps.Usage;

	info.inputReportLength = caps.InputReportByteLength;
	info.outputReportLength = caps.OutputReportByteLength;
	info.featureReportLength = caps.FeatureReportByteLength;
	info.featureValueCaps = caps.NumberFeatureValueCaps;

	RID_DEVICE_INFO deviceInfo{};
	deviceInfo.cbSize = sizeof(deviceInfo);
	UINT deviceInfoSize = sizeof(deviceInfo);

	if (GetRawInputDeviceInfoW(device, RIDI_DEVICEINFO, &deviceInfo, &deviceInfoSize) == static_cast<UINT>(-1))
		return false;

	if (deviceInfo.dwType != RIM_TYPEHID)
		return false;

	info.vid = deviceInfo.hid.dwVendorId;
	info.pid = deviceInfo.hid.dwProductId;

	// Device path

	UINT nameSize = 0;

	if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, nullptr, &nameSize) != static_cast<UINT>(-1))
	{
		if (nameSize > 0)
		{
			std::vector<wchar_t> name(nameSize + 1);
			UINT actualSize = nameSize;

			if (GetRawInputDeviceInfoW(device, RIDI_DEVICENAME, name.data(), &actualSize) != static_cast<UINT>(-1))
				info.deviceName = name.data();
		}
	}

	return true;
}

void SwitchProController::EnumerateDevices()
{
	LOGSW_D("[SwitchProCtl] ========================================");
	LOGSW_D("[SwitchProCtl] SwitchProController Debug");
	LOGSW_D("[SwitchProCtl] ========================================");

	LOGSW_D("[SwitchProCtl] Target VID: %04X Target USB PID: %04X Target BT PID: %04X", SWITCH_PRO_CONTROLLER_VID, SWITCH_PRO_CONTROLLER_PID, SWITCH_PRO_CONTROLLER_PID);

	UINT deviceCount = 0;

	if (GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
	{
		LOGSW_E("[SwitchProCtl] GetRawInputDeviceList failed: %lu", GetLastError());
		return;
	}

	LOGSW_D("[SwitchProCtl] Raw Input devices: %u", deviceCount);

	if (deviceCount == 0)
		return;

	std::vector<RAWINPUTDEVICELIST> devices(deviceCount);

	UINT count = deviceCount;

	if (GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
	{
		LOGSW_E("[SwitchProCtl] GetRawInputDeviceList failed: %lu", GetLastError());
		return;
	}

	unsigned int switchCount = 0;

	for (UINT i = 0; i < count; ++i)
	{
		if (devices[i].dwType != RIM_TYPEHID)
			continue;

		HidInfo info{};

		if (!GetHidInfo(devices[i].hDevice, info))
			continue;

		if (!IsSwitchProControllerDevice(info))
			continue;

		m_hidDevice = devices[i].hDevice;
		m_hidInfo = info;

		++switchCount;

		LOGSW_D("[SwitchProCtl] HID collection #%u", switchCount);
		LOGSW_D("[SwitchProCtl] VID: %04X PID: %04X Usage Page: %04X Usage: %04X Input report: %u bytes Output report: %u bytes Feature report: %u bytes Feature values: %u", info.vid, info.pid, info.usagePage, info.usage, info.inputReportLength, info.outputReportLength, info.featureReportLength, info.featureValueCaps);

		if (!info.deviceName.empty())
			LOGSW_D("[SwitchProCtl] Device: %ls", info.deviceName.c_str());
	}

	if (switchCount == 0)
		LOGSW_D("[SwitchProCtl] No Switch Pro Controller HID collections found");
	else
		LOGSW_D("[SwitchProCtl] Switch Pro Controller HID collections found: %u", switchCount);

	LOGSW_D("[SwitchProCtl] ========================================");
}

void SwitchProController::DumpHex(const BYTE *data, UINT size, UINT intervalMs)
{
	if (data == nullptr || size == 0)
		return;

	static auto lastDump = std::chrono::steady_clock::now();
	const auto now = std::chrono::steady_clock::now();

	if (now - lastDump < std::chrono::milliseconds(intervalMs))
		return;

	lastDump = now;

	UINT actualSize = size;

	////// DEBUG
	// Zero Trimming: ignore trailing zero padding
	while (actualSize > 0 && data[actualSize - 1] == 0x00)
		--actualSize;

	if (actualSize == 0)
		return;
	//////

	std::string hex;
	hex.reserve(actualSize * 3);

	char byte[4];

	for (UINT i = 0; i < actualSize; ++i)
	{
		if (i != 0)
			hex += ' ';

		std::snprintf(byte, sizeof(byte), "%02X", data[i]);
		hex += byte;
	}

	LOGSW_D("[%u bytes] %s", actualSize, hex.c_str());
}

bool SwitchProController::IsSwitchProControllerDevice(const HidInfo &info)
{
	if (info.vid != SWITCH_PRO_CONTROLLER_VID)
		return false;

	return info.pid == SWITCH_PRO_CONTROLLER_PID || info.pid == SWITCH_PRO_CONTROLLER_PID;
}

HWND SwitchProController::GetWindowHandle() const
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_hwnd;
}

void SwitchProController::ParseUsbReport(const BYTE *report, UINT size)
{
	if (report == nullptr || size < 5)
		return;

	// Byte 2: battery level + connection state.
	const BYTE batteryByte = report[2];
	const BYTE batteryLevel = (batteryByte >> 4) & 0x0F;

	m_charging.store((batteryByte & 0x01) != 0);

	if (batteryLevel <= 8)
		m_batteryLevel.store((batteryLevel * 100) / 8);
	else
		m_batteryLevel.store(-1);

	const BYTE buttons = report[4];

	const auto now = std::chrono::steady_clock::now();

	if (buttons & 0x20)
	{
		if (now - m_lastCapture >= std::chrono::milliseconds(Screenshot::MIN_INTERVAL_MS))
		{
			m_lastCapture = now;
			LOGSW_D("[SwitchProCtl] Capture pressed");
			FireCaptureButtonPressed();
		}
	}

	// if (buttons & 0x10)
	// 	LOGSW_D("[SwitchProCtl] Home pressed");

	// if (buttons & 0x01)
	// 	LOGSW_D("[SwitchProCtl] - pressed");

	// if (buttons & 0x02)
	// 	LOGSW_D("[SwitchProCtl] + pressed");
}

void SwitchProController::ParseBluetoothReport(const BYTE *report, UINT size, const HidInfo &info)
{
	if (report == nullptr || size < 3)
		return;

	const BYTE previousReport = m_lastBluetoothReport;
	m_lastBluetoothReport = report[0];

	switch (report[0])
	{
	// Simple report
	case 0x3F:
	{
		const BYTE buttons = report[2];

		// We need that check because Steam resets the controller mode to 3F when out of focus
		if (previousReport != 0x3F)
		{
			LOGSW_D("[SwitchProCtl] Controller is in 0x3F mode, restoring 0x30");
			if (!InitializeBluetoothInputMode(info))
				LOGSW_E("[SwitchProCtl] Failed to init Full Report mode");
		}

		// Battery status is not available in 0x3F mode.
		m_batteryLevel.store(-1);
		m_charging.store(false);

		const auto now = std::chrono::steady_clock::now();

		if (buttons & 0x20)
		{
			if (now - m_lastCapture >= std::chrono::milliseconds(Screenshot::MIN_INTERVAL_MS))
			{
				m_lastCapture = now;
				LOGSW_D("[SwitchProCtl] Capture pressed");
				FireCaptureButtonPressed();
			}
		}

		// if (buttons & 0x10)
		// 	LOGSW_D("[SwitchProCtl] Home pressed");

		// if (buttons & 0x01)
		// 	LOGSW_D("[SwitchProCtl] - pressed");

		// if (buttons & 0x02)
		// 	LOGSW_D("[SwitchProCtl] + pressed");

		break;
	}

	// Full report
	case 0x30:
	{
		if (size < 5)
			return;

		////// Battery Level
		/*
			0 = empty
			2 = 25%
			4 = 50%
			6 = 75%
			8 = 100%

			1 = empty + charging
			3 = 25% + charging
			5 = 50% + charging
			7 = 75% + charging
			9 = 100% + charging
		*/
		const BYTE batteryByte = report[2];
		const BYTE reportedBatteryLevel = (batteryByte >> 4) & 0x0F;

		const bool charging = (reportedBatteryLevel & 0x01) != 0;
		const BYTE batteryLevel = reportedBatteryLevel & 0x0E; // unsetting LSB which is presumably the chargning state

		if (batteryLevel <= 8)
			m_batteryLevel.store(batteryLevel);
		else
			m_batteryLevel.store(-1);

		m_charging.store(charging); // theorically
		//////

		const BYTE buttons = report[4];

		const auto now = std::chrono::steady_clock::now();

		if (buttons & 0x20)
		{
			if (now - m_lastCapture >= std::chrono::milliseconds(Screenshot::MIN_INTERVAL_MS))
			{
				m_lastCapture = now;
				LOGSW_D("[SwitchProCtl] Capture pressed");
				FireCaptureButtonPressed();
			}
		}

		// if (buttons & 0x10)
		// 	LOGSW_D("[SwitchProCtl] Home pressed");

		// if (buttons & 0x01)
		// 	LOGSW_D("[SwitchProCtl] - pressed");

		// if (buttons & 0x02)
		// 	LOGSW_D("[SwitchProCtl] + pressed");

		break;
	}

	default:
		break;
	}
}

void SwitchProController::SetConnected(bool connected, Transport transport)
{
	const bool wasConnected = m_connected.exchange(connected);

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_transport = connected ? transport : Transport::None;
	}

	if (connected && !wasConnected)
	{
		LOGSW_D("[SwitchProCtl] Connected");
		FireConnected();
	}
	else if (!connected && wasConnected)
	{
		LOGSW_D("[SwitchProCtl] Disconnected");
		FireDisconnected();
	}
}

GamePad::Transport SwitchProController::GetTransport() const
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_transport;
}

bool SwitchProController::IsCharging() const { return m_charging.load(); }

int SwitchProController::BatteryLevel() const
{
	// Levels reported: -1, 0, 2, 4, 6, 8
	const int level = m_batteryLevel.load();

	if (level == -1)
		return -1;

	return level * 100 / 8;
}

void SwitchProController::HandleDeviceChange(WPARAM wParam, LPARAM lParam)
{
	const HANDLE device = reinterpret_cast<HANDLE>(lParam);

	if (wParam == GIDC_ARRIVAL)
	{
		HidInfo info{};

		if (!GetHidInfo(device, info))
			return;

		if (!IsSwitchProControllerDevice(info))
			return;

		m_hidDevice = device;
		m_hidInfo = info;

		Transport transport = Transport::None;

		if (info.inputReportLength == SWITCH_PRO_BLUETOOTH_INPUT_REPORT_LENGTH)
			transport = Transport::Bluetooth;
		else if (info.inputReportLength == SWITCH_PRO_USB_INPUT_REPORT_LENGTH)
			transport = Transport::USB;

		SetConnected(true, transport);

		if (transport == Transport::Bluetooth)
			InitializeBluetoothInputMode(info);
	}
	else if (wParam == GIDC_REMOVAL)
	{
		if (device == m_hidDevice)
		{
			m_hidDevice = nullptr;
			m_hidInfo = {};
		}

		SetConnected(false, Transport::None);
	}
}

void SwitchProController::SetOnButtonPressed(Callback callback)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_onCreateButtonPressed = std::move(callback);
}

void SwitchProController::SetOnConnected(Callback callback)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_onConnected = std::move(callback);
}

void SwitchProController::SetOnDisconnected(Callback callback)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_onDisconnected = std::move(callback);
}

void SwitchProController::FireCaptureButtonPressed()
{
	Callback callback;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		callback = m_onCreateButtonPressed;
	}

	if (callback)
		callback();
}

void SwitchProController::FireConnected()
{
	Callback callback;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		callback = m_onConnected;
	}

	if (callback)
		callback();
}

void SwitchProController::FireDisconnected()
{
	Callback callback;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		callback = m_onDisconnected;
	}

	if (callback)
		callback();
}

bool SwitchProController::InitializeBluetoothInputMode(const HidInfo &info)
{
	if (info.deviceName.empty())
	{
		LOGSW_E("[SwitchProCtl] Bluetooth device path is empty");
		return false;
	}

	if (info.outputReportLength == 0)
	{
		LOGSW_E("[SwitchProCtl] Invalid Bluetooth output report length");
		return false;
	}

	HANDLE device = CreateFileW(info.deviceName.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);

	if (device == INVALID_HANDLE_VALUE)
	{
		LOGSW_E("[SwitchProCtl] Failed to open HID device for output: %lu", GetLastError());
		return false;
	}

	std::vector<BYTE> report(info.outputReportLength, 0);

	report[0] = 0x01; // Output report ID
	report[1] = 0x00; // Packet number
	// Bytes 2-9 = neutral rumble data. Already zeroed.
	report[10] = 0x03; // Set Input Report Mode
	report[11] = 0x30; // Standard full report mode

	LOGSW_D("[SwitchProCtl] Sending SetInputReportMode(0x30)");

	if (!WriteFile(device, report.data(), static_cast<DWORD>(report.size()), nullptr, nullptr))
	{
		LOGSW_E("[SwitchProCtl] Failed to send SetInputReportMode: %lu", GetLastError());
		CloseHandle(device);
		return false;
	}

	CloseHandle(device);

	LOGSW_D("[SwitchProCtl] SetInputReportMode(0x30) sent");

	return true;
}
