#pragma once

#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <iphlpapi.h>
#include <netioapi.h>

#include <vector>
#include <mutex>

#include "radeonmon/structures.hpp"

class NetworkManager
{
public:
    bool Initialize(HWND);
    void Shutdown();
    std::vector<NetworkInterface> GetAddresses() const;
    void Log() const;
    void Refresh();

private:
    void ScheduleRefresh();
    std::vector<NetworkInterface> Discover() const;
    static VOID CALLBACK InterfaceChanged(PVOID context, PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type);

public:
    UINT_PTR m_timer = 0;

private:
    HWND m_hwnd;
    mutable std::mutex m_mutex;
    std::vector<NetworkInterface> m_addresses;
    HANDLE m_notifyHandle = nullptr;
    int m_pendingChanges = 0;
};