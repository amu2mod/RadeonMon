#define WIN32_LEAN_AND_MEAN
#define _SILENCE_EXPERIMENTAL_COROUTINE_DEPRECATION_WARNINGS

#include "radeonmon/xboxwirelesscontroller.hpp"

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

static const winrt::guid BATTERY_SERVICE_UUID{L"0000180F-0000-1000-8000-00805F9B34FB"};
static const winrt::guid BATTERY_LEVEL_UUID{L"00002A19-0000-1000-8000-00805F9B34FB"};

XboxWirelessController::XboxWirelessController() {}
XboxWirelessController::~XboxWirelessController() { Stop(); }

bool XboxWirelessController::Start()
{
	if (m_running.load())
		return true;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);

		m_initialized = false;
		m_initSuccess = false;

		m_hwnd = nullptr;
		m_workerThreadId = 0;

		m_bluetoothAddress = 0;
		m_transport = Transport::None;

		m_running.store(true);
		m_connected.store(false);
		m_batteryLevel.store(-1);
	}

	try
	{
		m_worker = std::thread(&XboxWirelessController::WorkerMain, this);
	}
	catch (...)
	{
		LOGXBX_E("[XboxWC] Failed to create worker thread");
		m_running.store(false);
		return false;
	}

	std::unique_lock<std::mutex> lock(m_stateMutex);

	m_stateCv.wait(lock, [this] { return m_initialized; });

	const bool success = m_initSuccess;

	lock.unlock();

	if (!success && m_worker.joinable())
		m_worker.join();

	return success;
}

void XboxWirelessController::Stop()
{
	m_running.store(false);
	m_liveReport.store(false);

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

bool XboxWirelessController::LiveReport()
{
	if (!m_running.load())
	{
		LOGXBX_E("[XboxWC] LiveReport(): controller is not open");
		return false;
	}

	m_liveReport.store(true);

	LOGXBX_D("[XboxWC] Live reporting enabled");

	return true;
}

void XboxWirelessController::Debug()
{
	if (!m_running.load())
	{
		LOGXBX_E("[XboxWC] Debug(): controller is not open");
		return;
	}

	const HWND hwnd = GetWindowHandle();

	if (hwnd == nullptr)
		return;

	PostMessageW(hwnd, WM_DEBUG, 0, 0);
}

void XboxWirelessController::WorkerMain()
{
	winrt::init_apartment(winrt::apartment_type::multi_threaded);

	m_workerThreadId = GetCurrentThreadId();

	const HINSTANCE instance = GetModuleHandleW(nullptr);

	WNDCLASSW wc{};

	wc.lpfnWndProc = &XboxWirelessController::WindowProc;
	wc.hInstance = instance;
	wc.lpszClassName = WindowClassName();

	const ATOM atom = RegisterClassW(&wc);

	if (atom == 0)
	{
		const DWORD error = GetLastError();

		if (error != ERROR_CLASS_ALREADY_EXISTS)
		{
			LOGXBX_E("[XboxWC] RegisterClassW failed: %lu", error);

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

	HWND hwnd = CreateWindowExW(0, WindowClassName(), L"Xbox Wireless Controller", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, instance, this);

	if (hwnd == nullptr)
	{
		LOGXBX_E("[XboxWC] CreateWindowExW failed: %lu", GetLastError());

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
		LOGXBX_E("[XboxWC] RegisterRawInputDevices failed: %lu", GetLastError());

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

	// Find Xbox controller / Bluetooth address
	EnumerateDevices();

	// Worker message loop
	RunMessageLoop();

	ShutdownBattery();

	RAWINPUTDEVICE rid{};

	rid.usUsagePage = 0x01;
	rid.usUsage = 0x05;
	rid.dwFlags = RIDEV_REMOVE;
	rid.hwndTarget = nullptr;

	if (!RegisterRawInputDevices(&rid, 1, sizeof(rid)))
		LOGXBX_E("[XboxWC] Failed to remove Raw Input registration: %lu", GetLastError());

	// Cleanup window
	if (hwnd != nullptr)
		DestroyWindow(hwnd);

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_hwnd = nullptr;
	}

	m_running.store(false);
	m_liveReport.store(false);

	winrt::uninit_apartment();
}

void XboxWirelessController::RunMessageLoop()
{
	MSG msg{};

	while (m_running.load())
	{
		const BOOL result = GetMessageW(&msg, nullptr, 0, 0);

		if (result == -1)
		{
			LOGXBX_E("[XboxWC] GetMessageW failed: %lu", GetLastError());
			break;
		}

		if (result == 0)
			break;

		TranslateMessage(&msg);
		DispatchMessageW(&msg);
	}
}

LRESULT CALLBACK XboxWirelessController::WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	auto *self = reinterpret_cast<XboxWirelessController *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

	if (msg == WM_NCCREATE)
	{
		auto *create = reinterpret_cast<CREATESTRUCTW *>(lParam);
		self = reinterpret_cast<XboxWirelessController *>(create->lpCreateParams);
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

bool XboxWirelessController::RegisterRawInput()
{
	RAWINPUTDEVICE rid{};
	rid.usUsagePage = 0x01;
	rid.usUsage = 0x05;
	rid.dwFlags = RIDEV_INPUTSINK | RIDEV_DEVNOTIFY;
	rid.hwndTarget = GetWindowHandle();
	return RegisterRawInputDevices(&rid, 1, sizeof(rid)) != FALSE;
}

void XboxWirelessController::HandleRawInput(HRAWINPUT hRawInput)
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

	for (UINT i = 0; i < hid.dwCount; ++i)
	{
		const BYTE *report = hid.bRawData + (i * hid.dwSizeHid);
		ParseSpecialButtons(report, hid.dwSizeHid);
	}
}

bool XboxWirelessController::GetHidInfo(HANDLE device, HidInfo &info)
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
			{
				info.deviceName = name.data();
				ExtractBluetoothAddress(info.deviceName, info.bluetoothAddress);
			}
		}
	}

	return true;
}

void XboxWirelessController::EnumerateDevices()
{
	LOGXBX_D("[XboxWC] ========================================");
	LOGXBX_D("[XboxWC] XboxWirelessController Debug");
	LOGXBX_D("[XboxWC] ========================================");

	LOGXBX_D("[XboxWC] Target VID: %04X Target USB PID: %04X Target BT PID: %04X", XBOX_VID, XBOX_PID_USB, XBOX_PID_BLUETOOTH);

	UINT deviceCount = 0;
	uint64_t bluetoothAddress = 0;

	if (GetRawInputDeviceList(nullptr, &deviceCount, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
	{
		LOGXBX_E("[XboxWC] GetRawInputDeviceList failed: %lu", GetLastError());
		return;
	}

	LOGXBX_D("[XboxWC] Raw Input devices: %u", deviceCount);

	if (deviceCount == 0)
		return;

	std::vector<RAWINPUTDEVICELIST> devices(deviceCount);

	UINT count = deviceCount;

	if (GetRawInputDeviceList(devices.data(), &count, sizeof(RAWINPUTDEVICELIST)) == static_cast<UINT>(-1))
	{
		LOGXBX_E("[XboxWC] GetRawInputDeviceList failed: %lu", GetLastError());
		return;
	}

	unsigned int xboxCount = 0;

	for (UINT i = 0; i < count; ++i)
	{
		if (devices[i].dwType != RIM_TYPEHID)
			continue;

		HidInfo info{};

		if (!GetHidInfo(devices[i].hDevice, info))
		{
			continue;
		}

		if (!IsXboxDevice(info))
			continue;

		if (info.pid == XBOX_PID_BLUETOOTH && info.bluetoothAddress != 0)
		{
			bluetoothAddress = info.bluetoothAddress;
			m_bluetoothAddress = info.bluetoothAddress;
		}

		m_hidDevice = devices[i].hDevice;
		m_hidInfo = info;

		++xboxCount;

		LOGXBX_D("[XboxWC] HID collection #%u", xboxCount);

		LOGXBX_D("[XboxWC] VID: %04X PID: %04X Usage Page: %04X Usage: %04X Input report: %u bytes Output report: %u bytes Feature report: %u bytes Feature values: %u", info.vid, info.pid, info.usagePage, info.usage, info.inputReportLength, info.outputReportLength, info.featureReportLength, info.featureValueCaps);

		if (!info.deviceName.empty())
			LOGXBX_D("[XboxWC] Device: %ls", info.deviceName.c_str());
	}

	if (xboxCount == 0)
		LOGXBX_D("[XboxWC] No Xbox controller HID collections found");
	else
		LOGXBX_D("[XboxWC] Xbox controller HID collections found: %u", xboxCount);

	LOGXBX_D("[XboxWC] ========================================");

	(void)bluetoothAddress;
}

void XboxWirelessController::DumpHex([[maybe_unused]] const BYTE *data, UINT size)
{
	for (UINT i = 0; i < size; ++i)
		LOGXBX_D("[XboxWC] %02X", data[i]);
}

bool XboxWirelessController::IsXboxDevice(const HidInfo &info)
{
	if (info.vid != XBOX_VID)
		return false;

	return info.pid == XBOX_PID_BLUETOOTH || info.pid == XBOX_PID_USB;
}

HWND XboxWirelessController::GetWindowHandle() const
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_hwnd;
}

bool XboxWirelessController::ExtractBluetoothAddress(const std::wstring &path, uint64_t &address)
{
	const wchar_t *marker = L"&";

	size_t first = path.find(marker);

	while (first != std::wstring::npos)
	{
		const size_t start = first + 1;
		const size_t end = path.find(L"&", start);

		if (end == std::wstring::npos)
			break;

		const size_t length = end - start;

		if (length == 12)
		{
			const std::wstring value = path.substr(start, length);
			bool validHex = true;

			for (wchar_t c : value)
			{
				if (!iswxdigit(c))
				{
					validHex = false;
					break;
				}
			}

			if (validHex)
			{
				try
				{
					address = std::stoull(value, nullptr, 16);
					return true;
				}
				catch (...)
				{
					return false;
				}
			}
		}

		first = path.find(marker, start);
	}

	return false;
}

bool XboxWirelessController::InitializeBattery(uint64_t address)
{
	try
	{
		using namespace winrt::Windows::Devices::Bluetooth;
		using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

		LOGXBX_D("[XboxWC] Opening Bluetooth device");

		auto device = BluetoothLEDevice::FromBluetoothAddressAsync(address).get();

		if (!device)
		{
			LOGXBX_E("[XboxWC] Failed to open Bluetooth device");
			return false;
		}

		LOGXBX_D("[XboxWC] Battery device: %ls", device.Name().c_str());
		LOGXBX_D("[XboxWC] Battery address: %012llX", static_cast<unsigned long long>(device.BluetoothAddress()));
		LOGXBX_D("[XboxWC] Battery connection status: %d", static_cast<int>(device.ConnectionStatus()));

		m_bluetoothDevice = device;

		auto serviceResult = m_bluetoothDevice.GetGattServicesForUuidAsync(BATTERY_SERVICE_UUID, BluetoothCacheMode::Cached).get();

		if (serviceResult.Status() != GattCommunicationStatus::Success)
		{
			LOGXBX_E("[XboxWC] Failed to find battery service");
			return false;
		}

		auto services = serviceResult.Services();

		if (services.Size() == 0)
		{
			LOGXBX_E("[XboxWC] Battery service not found");
			return false;
		}

		LOGXBX_D("[XboxWC] Battery service found");

		auto batteryService = services.GetAt(0);

		auto characteristicResult = batteryService.GetCharacteristicsForUuidAsync(BATTERY_LEVEL_UUID, BluetoothCacheMode::Cached).get();

		if (characteristicResult.Status() != GattCommunicationStatus::Success)
		{
			LOGXBX_E("[XboxWC] Failed to find battery level characteristic");
			return false;
		}

		auto characteristics = characteristicResult.Characteristics();

		if (characteristics.Size() == 0)
		{
			LOGXBX_E("[XboxWC] Battery level characteristic not found");
			return false;
		}

		LOGXBX_D("[XboxWC] Battery level characteristic found");

		m_batteryCharacteristic = characteristics.GetAt(0);
		m_batteryValueChangedToken = m_batteryCharacteristic.ValueChanged([this](auto const &characteristic, auto const &args) { OnBatteryValueChanged(characteristic, args); });

		LOGXBX_D("[XboxWC] Battery notifications subscribed");

		auto cccdStatus = m_batteryCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue::Notify).get();

		if (cccdStatus != GattCommunicationStatus::Success)
			LOGXBX_E("[XboxWC] Failed to enable battery notifications. Status: %d", static_cast<int>(cccdStatus));
		else
			LOGXBX_D("[XboxWC] Battery notifications enabled");

		int level = ReadBatteryValue();

		if (level >= 0)
		{
			m_batteryLevel.store(level);
			LOGXBX_D("[XboxWC] Initial battery level: %d%%", level);
		}
		else
			LOGXBX_E("[XboxWC] Initial battery read failed");

		return true;
	}
	catch ([[maybe_unused]] const winrt::hresult_error &e)
	{
		LOGXBX_E("[XboxWC] Battery WinRT error: 0x%08X", static_cast<unsigned>(e.code().value));
		LOGXBX_E("[XboxWC] Battery error message: %ls", e.message().c_str());
		return false;
	}
}

void XboxWirelessController::OnBatteryValueChanged(winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCharacteristic const &, winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattValueChangedEventArgs const &args)
{
	try
	{
		auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(args.CharacteristicValue());

		if (reader.UnconsumedBufferLength() < 1)
			return;

		int level = static_cast<int>(reader.ReadByte());

		if (level > 100)
			level = 100;

		m_batteryLevel.store(level);

		LOGXBX_D("[XboxWC] Battery level changed: %d%%", level);
	}
	catch ([[maybe_unused]] const winrt::hresult_error &e)
	{
		LOGXBX_E("[XboxWC] Battery ValueChanged error: 0x%08X", static_cast<unsigned>(e.code().value));
	}
}

void XboxWirelessController::ShutdownBattery()
{
	try
	{
		if (m_batteryCharacteristic)
		{
			m_batteryCharacteristic.ValueChanged(m_batteryValueChangedToken);

			auto status = m_batteryCharacteristic.WriteClientCharacteristicConfigurationDescriptorAsync(winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattClientCharacteristicConfigurationDescriptorValue::None).get();

			if (status != winrt::Windows::Devices::Bluetooth::GenericAttributeProfile::GattCommunicationStatus::Success)
			{
				LOGXBX_E("[XboxWC] Failed to disable battery notifications. Status: %d", static_cast<int>(status));
			}
		}
	}
	catch ([[maybe_unused]] const winrt::hresult_error &e)
	{
		LOGXBX_E("[XboxWC] Battery shutdown error: 0x%08X", static_cast<unsigned>(e.code().value));
	}
	catch (...)
	{
		LOGXBX_E("[XboxWC] Unknown battery shutdown error");
	}

	m_batteryValueChangedToken = {};
	m_batteryCharacteristic = nullptr;
	m_bluetoothDevice = nullptr;

	m_batteryLevel.store(-1);
}

int XboxWirelessController::ReadBatteryValue()
{
	try
	{
		using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;
		using namespace winrt::Windows::Storage::Streams;

		if (!m_batteryCharacteristic)
			return -1;

		auto result = m_batteryCharacteristic.ReadValueAsync().get();

		if (result.Status() != GattCommunicationStatus::Success)
		{
			return -1;
		}

		auto buffer = result.Value();

		if (buffer.Length() == 0)
			return -1;

		auto reader = DataReader::FromBuffer(buffer);

		BYTE level = reader.ReadByte();

		if (level > 100)
			return -1;

		return static_cast<int>(level);
	}
	catch ([[maybe_unused]] const winrt::hresult_error &e)
	{
		LOGXBX_E("[XboxWC] Battery read error: 0x%08X", static_cast<unsigned>(e.code().value));

		return -1;
	}
}

void XboxWirelessController::ParseSpecialButtons(const BYTE *report, UINT size)
{
	if (report == nullptr || size < 13)
		return;

	const BYTE byte11 = report[11];
	const BYTE byte12 = report[12];

	if (byte12 & 0x08)
		FireCreateButtonPressed();

	// if (byte11 & 0x40)
	// 	LOGXBX_D("[XboxWC] View pressed");

	// if (byte11 & 0x80)
	// 	LOGXBX_D("[XboxWC] Menu pressed");

	(void)byte11;
}

void XboxWirelessController::SetConnected(bool connected, Transport transport)
{
	const bool wasConnected = m_connected.exchange(connected);

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		m_transport = connected ? transport : Transport::None;
	}

	if (connected && !wasConnected)
	{
		LOGXBX_D("[XboxWC] Connected");
		FireConnected();
	}
	else if (!connected && wasConnected)
	{
		LOGXBX_D("[XboxWC] Disconnected");
		FireDisconnected();
	}
}

GamePad::Transport XboxWirelessController::GetTransport() const
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	return m_transport;
}

bool XboxWirelessController::IsCharging() const
{
	// Not currently available
	return false;
}

int XboxWirelessController::BatteryLevel() const { return m_batteryLevel.load(); }

void XboxWirelessController::HandleDeviceChange(WPARAM wParam, LPARAM lParam)
{
	const HANDLE device = reinterpret_cast<HANDLE>(lParam);

	if (wParam == GIDC_ARRIVAL)
	{
		HidInfo info{};

		if (!GetHidInfo(device, info))
			return;

		if (!IsXboxDevice(info))
			return;

		m_hidDevice = device;
		m_hidInfo = info;

		Transport transport = Transport::None;

		if (info.pid == XBOX_PID_BLUETOOTH)
		{
			transport = Transport::Bluetooth;
			m_bluetoothAddress = info.bluetoothAddress;
		}
		else if (info.pid == XBOX_PID_USB)
			transport = Transport::USB;

		SetConnected(true, transport);

		if (transport == Transport::Bluetooth && m_bluetoothAddress != 0)
			InitializeBattery(m_bluetoothAddress);
	}
	else if (wParam == GIDC_REMOVAL)
	{
		if (device == m_hidDevice)
		{
			m_hidDevice = nullptr;
			m_hidInfo = {};
		}

		SetConnected(false, Transport::None);
		ShutdownBattery();
		m_bluetoothAddress = 0;
	}
}

void XboxWirelessController::SetOnButtonPressed(Callback callback)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_onCreateButtonPressed = std::move(callback);
}

void XboxWirelessController::SetOnConnected(Callback callback)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_onConnected = std::move(callback);
}

void XboxWirelessController::SetOnDisconnected(Callback callback)
{
	std::lock_guard<std::mutex> lock(m_stateMutex);
	m_onDisconnected = std::move(callback);
}

void XboxWirelessController::FireCreateButtonPressed()
{
	Callback callback;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		callback = m_onCreateButtonPressed;
	}

	if (callback)
		callback();
}

void XboxWirelessController::FireConnected()
{
	Callback callback;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		callback = m_onConnected;
	}

	if (callback)
		callback();
}

void XboxWirelessController::FireDisconnected()
{
	Callback callback;

	{
		std::lock_guard<std::mutex> lock(m_stateMutex);
		callback = m_onDisconnected;
	}

	if (callback)
		callback();
}
