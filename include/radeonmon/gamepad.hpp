#pragma once

#include <functional>

class GamePad
{
  public:
	enum class Type
	{
		None,
		DualSense,
		XboxWirelessController,
		NintendoSwitch2ProController
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
	virtual bool IsConnected() const = 0;

	// Callbacks
	virtual void SetOnCreateButtonPressed(Callback callback) = 0;
	virtual void SetOnConnected(Callback callback) = 0;
	virtual void SetOnDisconnected(Callback callback) = 0;
};