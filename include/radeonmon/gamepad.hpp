#pragma once

#include <windows.h>

#include <string>
#include <functional>

class GamePad
{
public:
	enum class Type
	{
		None,
		DualSense,
		XboxWirelessController,
		NintendoSwitchProController,
		NintendoSwitch2ProController
	};

	struct HidInfo
	{
		DWORD vid = 0;
		DWORD pid = 0;

		USHORT usagePage = 0;
		USHORT usage = 0;

		USHORT inputReportLength = 0;
		USHORT outputReportLength = 0;
		USHORT featureReportLength = 0;
		USHORT featureValueCaps = 0;

		std::wstring deviceName;

		uint64_t bluetoothAddress = 0;
	};

	enum class Transport
	{
		None,
		USB,
		Bluetooth
	};

	using Callback = std::function<void()>; // callback alias

	virtual ~GamePad() = default;

	virtual bool Start() = 0;
	virtual void Stop() = 0;

	// Returns battery level in [-1, 100]
	// -1 means battery level is unknown/unavailable
	virtual int BatteryLevel() const = 0;

	virtual bool IsCharging() const = 0;
	virtual Transport GetTransport() const = 0;

	// Callbacks
	virtual void SetOnButtonPressed(Callback callback) = 0;
	virtual void SetOnConnected(Callback callback) = 0;
	virtual void SetOnDisconnected(Callback callback) = 0;
};