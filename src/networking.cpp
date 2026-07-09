#include <algorithm>

#include "radeonmon/networking.hpp"
#include "radeonmon/globals.hpp"
#include "radeonmon/logging.hpp"

/////////////
// Helpers
static bool ContainsInsensitive(const std::wstring &text, const wchar_t *keyword)
{
    std::wstring lowerText = text;
    std::wstring lowerKeyword = keyword;
    std::transform(lowerText.begin(), lowerText.end(), lowerText.begin(), ::towlower);
    std::transform(lowerKeyword.begin(), lowerKeyword.end(), lowerKeyword.begin(), ::towlower);
    return lowerText.find(lowerKeyword) != std::wstring::npos;
}

static bool IsVirtualAdapter(const IP_ADAPTER_ADDRESSES *adapter)
{
    if (!adapter)
        return true;

    std::wstring name;

    if (adapter->FriendlyName)
        name = adapter->FriendlyName;

    if (adapter->Description)
        name += L" " + std::wstring(adapter->Description);

    static const wchar_t *virtualNames[] =
        {
            L"Hyper-V",
            L"VMware",
            L"VirtualBox",
            L"VBox",
            L"Virtual",
            L"Docker",
            L"WSL",
            L"VPN",
            L"TAP",
            L"WireGuard",
            L"ZeroTier",
            L"Loopback"};

    for (auto keyword : virtualNames)
    {
        if (ContainsInsensitive(name, keyword))
            return true;
    }

    // Interface types commonly used for virtual adapters
    switch (adapter->IfType)
    {
    case IF_TYPE_SOFTWARE_LOOPBACK:
    case IF_TYPE_TUNNEL:
        return true;
    }

    return false;
}
/////////////

bool NetworkManager::Initialize(HWND hwnd)
{
    m_hwnd = hwnd;

    // Build the initial snapshot.
    Refresh();

    DWORD result = NotifyIpInterfaceChange(
        AF_UNSPEC,
        &NetworkManager::InterfaceChanged,
        this,
        FALSE, // already refreshed manually
        &m_notifyHandle);

    if (result != NO_ERROR)
    {
        m_notifyHandle = nullptr;
        return false;
    }

    return true;
}

std::vector<NetworkInterface> NetworkManager::Discover() const
{
    LOG_DEBUG("Discovering Network");
    std::vector<NetworkInterface> addresses;

    ULONG bufferSize = 0;

    DWORD result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, nullptr, &bufferSize);

    if (result != ERROR_BUFFER_OVERFLOW)
        return addresses;

    std::vector<BYTE> buffer(bufferSize);

    auto adapters =
        reinterpret_cast<IP_ADAPTER_ADDRESSES *>(buffer.data());

    result = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, adapters, &bufferSize);

    if (result != NO_ERROR)
        return addresses;

    for (auto adapter = adapters; adapter; adapter = adapter->Next)
    {
        if (adapter->OperStatus != IfOperStatusUp)
            continue;

        if (IsVirtualAdapter(adapter))
            continue;

        for (auto unicast = adapter->FirstUnicastAddress; unicast; unicast = unicast->Next)
        {
            auto *sockaddr = unicast->Address.lpSockaddr;

            if (!sockaddr || sockaddr->sa_family != AF_INET)
                continue;

            wchar_t ip[INET_ADDRSTRLEN] = {};

            auto *ipv4 = reinterpret_cast<sockaddr_in *>(sockaddr);

            if (!InetNtopW(AF_INET, &ipv4->sin_addr, ip, INET_ADDRSTRLEN))
                continue;

            std::wstring address(ip);

            auto it = std::find_if(addresses.begin(), addresses.end(), [&](const NetworkInterface &item)
                                   { return item.address == address; });

            if (it != addresses.end())
                continue;

            addresses.emplace_back(NetworkInterface{adapter->Luid, adapter->FriendlyName ? adapter->FriendlyName : L"Unknown", address});
        }
    }

    return addresses;
}

void NetworkManager::Refresh()
{
    auto updated = Discover();

    {
        std::scoped_lock lock(m_mutex);
        m_addresses = updated;
    }

    g_webServer.CheckInterface(updated);
}

std::vector<NetworkInterface> NetworkManager::GetAddresses() const
{
    std::scoped_lock lock(m_mutex);
    return m_addresses;
}

void NetworkManager::Shutdown()
{
    if (m_notifyHandle)
    {
        CancelMibChangeNotify2(m_notifyHandle);
        m_notifyHandle = nullptr;
    }

    std::scoped_lock lock(m_mutex);
    m_addresses.clear();
}

VOID CALLBACK NetworkManager::InterfaceChanged(PVOID context, [[maybe_unused]] PMIB_IPINTERFACE_ROW row, MIB_NOTIFICATION_TYPE type)
{
    auto *manager = static_cast<NetworkManager *>(context);

    if (!manager)
        return;

    const char *event = "Unknown";

    switch (type)
    {
    case MibParameterNotification:
        event = "Parameter changed";
        break;

    case MibAddInstance:
        event = "Interface added";
        break;

    case MibDeleteInstance:
        event = "Interface removed";
        break;
    }

    LOG_DEBUG("Network event: %s", event);
    manager->m_pendingChanges++;
    manager->ScheduleRefresh();
}

void NetworkManager::ScheduleRefresh()
{
    if (m_timer != 0)
    {
        KillTimer(m_hwnd, m_timer);
    }
    else
    {
        LOG_DEBUG("Network change detected, scheduling refresh in 2000ms");
    }

    m_timer = SetTimer(m_hwnd, NETWORK_TIMER_ID, 2000, nullptr);
}

void NetworkManager::Log() const
{
    const auto addresses = GetAddresses();

    LOG_DEBUG("Network List");
    LOG_DEBUG("-------------");
    for (const auto &ip : addresses)
        LOG_DEBUG("- IP found: %ls", ip.display().c_str());
    LOG_DEBUG("-------------");
}