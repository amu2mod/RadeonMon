#pragma once

#include <windows.h>
#include <tlhelp32.h>
#include <winternl.h>

#include <algorithm>
#include <string>
#include <unordered_map>
#include <vector>
#include <unordered_set>

typedef NTSTATUS(NTAPI *pNtQuerySystemInformation_t)(
    SYSTEM_INFORMATION_CLASS SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength,
    PULONG ReturnLength);

#ifndef STATUS_INFO_LENGTH_MISMATCH
#define STATUS_INFO_LENGTH_MISMATCH ((NTSTATUS)0xC0000004L)
#endif

// After your includes
typedef struct _MY_SYSTEM_PROCESS_INFORMATION
{
    ULONG NextEntryOffset;
    ULONG NumberOfThreads;
    ULONGLONG Reserved1[3];
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER UserTime;
    LARGE_INTEGER KernelTime;
    UNICODE_STRING ImageName;
    KPRIORITY BasePriority;
    HANDLE UniqueProcessId;
    HANDLE InheritedFromUniqueProcessId;
    ULONG HandleCount;
    ULONG SessionId;
    ULONG_PTR PageDirectoryBase;
    // Add more fields only if needed
} MY_SYSTEM_PROCESS_INFORMATION, *PMY_SYSTEM_PROCESS_INFORMATION;

static std::string WideToUtf8(PCWSTR wstr, int length = -1)
{
    if (!wstr || length == 0)
        return {};

    // Get required size
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr, length, nullptr, 0, nullptr, nullptr);
    if (size <= 0)
        return {};

    std::string result(size, '\0');

    WideCharToMultiByte(CP_UTF8, 0, wstr, length, result.data(), size, nullptr, nullptr);

    // Remove trailing null if length was -1
    if (length == -1 && !result.empty() && result.back() == '\0')
        result.pop_back();

    return result;
}

struct ProcessInfo
{
    std::string name;
    double cpu;
};

class ProcessWatcher
{
public:
    void Initialize()
    {
        m_Buffer.resize(1024 * 1024); // 1 MB
        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
        pNtQuerySystemInformation = (NtQuerySystemInformation_t)GetProcAddress(ntdll, "NtQuerySystemInformation");
    }

    ProcessWatcher(RyzenCpu &cpuRef, WebServer &server) : m_cpu(cpuRef), m_server(server)
    {
        Initialize();
    }

    std::vector<ProcessInfo> Poll()
    {
        if (!m_server.IsRunning())
            return {};

        if (!pNtQuerySystemInformation)
            return m_LastTop;

        ULONG returnLength = 0;
        NTSTATUS status = pNtQuerySystemInformation(SystemProcessInformation, m_Buffer.data(), (ULONG)m_Buffer.size(), &returnLength);

        if (status == STATUS_INFO_LENGTH_MISMATCH)
        {
            m_Buffer.resize(returnLength + 65536); // Increased padding
            status = pNtQuerySystemInformation(SystemProcessInformation, m_Buffer.data(), (ULONG)m_Buffer.size(), &returnLength);
        }

        if (!NT_SUCCESS(status))
            return m_LastTop;

        FILETIME idle, kernel, user;
        GetSystemTimes(&idle, &kernel, &user);

        uint64_t systemTime = FileTimeToUInt64(kernel) + FileTimeToUInt64(user);
        uint64_t systemDelta = (m_LastSystemTime != 0 && systemTime >= m_LastSystemTime)
                                   ? systemTime - m_LastSystemTime
                                   : 0;

        m_LastSystemTime = systemTime;

        struct Entry
        {
            const WCHAR *namePtr;
            USHORT nameLen;
            DWORD pid;
            double cpu;
        };

        std::vector<Entry> usage;
        std::unordered_set<DWORD> activePids;

        auto *spi = (MY_SYSTEM_PROCESS_INFORMATION *)m_Buffer.data();

        while (true)
        {
            DWORD pid = (DWORD)(ULONG_PTR)spi->UniqueProcessId;

            if (pid == 0)
            {
                if (spi->NextEntryOffset == 0)
                    break;
                spi = (MY_SYSTEM_PROCESS_INFORMATION *)((BYTE *)spi + spi->NextEntryOffset);
                continue;
            }

            activePids.insert(pid);

            uint64_t procTime = spi->UserTime.QuadPart + spi->KernelTime.QuadPart;

            double cpu = 0.0;
            auto it = m_ProcessTimes.find(pid);

            if (systemDelta > 0 && it != m_ProcessTimes.end() && procTime >= it->second)
            {
                uint64_t delta = procTime - it->second;
                cpu = 100.0 * static_cast<double>(delta) / static_cast<double>(systemDelta);
                if (!std::isfinite(cpu) || cpu < 0.0)
                    cpu = 0.0;
            }

            m_ProcessTimes[pid] = procTime;

            usage.push_back({spi->ImageName.Buffer, spi->ImageName.Length, pid, cpu});

            if (spi->NextEntryOffset == 0)
                break;

            spi = (MY_SYSTEM_PROCESS_INFORMATION *)((BYTE *)spi + spi->NextEntryOffset);
        }

        // Cleanup stale process history (every 8 polls)
        static int cleanupCounter = 0;
        if (++cleanupCounter >= 8)
        {
            cleanupCounter = 0;
            for (auto it = m_ProcessTimes.begin(); it != m_ProcessTimes.end();)
            {
                if (activePids.find(it->first) == activePids.end())
                    it = m_ProcessTimes.erase(it);
                else
                    ++it;
            }
        }

        // === Improved Filtering ===
        constexpr double MIN_CPU = 0.001;

        usage.erase(std::remove_if(usage.begin(), usage.end(), [&](const Entry &e)
                                   {
        if (e.pid == 4)                    // Always show System process
            return false;
        return e.cpu < MIN_CPU; }),
                    usage.end());

        // === Improved Normalization with better precision ===
        double processTotal = 0.0;
        for (const auto &entry : usage)
            processTotal += entry.cpu;

        double realCpu = m_cpu.GetAverageUsage();

        if (processTotal > 0.0 && realCpu >= 0.0)
        {
            double scale = realCpu / processTotal;

            for (auto &entry : usage)
            {
                entry.cpu = entry.cpu * scale;

                // Preserve small differences to avoid identical values in frontend
                if (entry.cpu > 0.0)
                {
                    // Tiny PID-based bias (negligible visually, but breaks ties)
                    entry.cpu += (entry.pid % 100) * 0.00001;
                }
            }
        }

        // Sort by scaled CPU (with stable tie-breaker)
        std::sort(usage.begin(), usage.end(), [](const Entry &a, const Entry &b)
                  {
        if (std::abs(a.cpu - b.cpu) < 0.0001)
            return a.pid < b.pid;   // stable secondary sort by PID
        return a.cpu > b.cpu; });

        // Return top 5 (or adjust as needed)
        std::vector<ProcessInfo> result;
        size_t count = std::min<size_t>(5, usage.size());
        result.reserve(count);

        for (size_t i = 0; i < count; ++i)
        {
            std::string name;

            if (usage[i].namePtr && usage[i].nameLen > 0)
            {
                name = WideToUtf8(usage[i].namePtr, usage[i].nameLen / sizeof(WCHAR));
            }
            else if (usage[i].pid == 4)
                name = "System";
            else
                name = "<unknown>";

            result.push_back({std::move(name), usage[i].cpu});
        }

        m_LastTop = result;
        return result;
    }

#ifdef _DEBUG
    void Log() const
    {
        for (const auto &process : m_LastTop)
        {
            LOG_DEBUG("%s (%.1f%%)", process.name.c_str(), process.cpu);
        }
    }
#endif

    int BuildJson(char *buffer, int bufferSize) const
    {
        if (bufferSize < 64)
            return -1;

        char *p = buffer;
        char *end = buffer + bufferSize - 1;

        auto write = [&](const char *s)
        {
            while (*s && p < end)
                *p++ = *s++;
        };

        auto writeJsonString = [&](const char *s)
        {
            if (p >= end)
                return;

            *p++ = '"';

            while (*s && p < end)
            {
                switch (*s)
                {
                case '"':
                case '\\':
                    if (p + 2 >= end)
                        break;
                    *p++ = '\\';
                    *p++ = *s;
                    break;

                case '\n':
                    if (p + 2 >= end)
                        break;
                    *p++ = '\\';
                    *p++ = 'n';
                    break;

                case '\r':
                    if (p + 2 >= end)
                        break;
                    *p++ = '\\';
                    *p++ = 'r';
                    break;

                case '\t':
                    if (p + 2 >= end)
                        break;
                    *p++ = '\\';
                    *p++ = 't';
                    break;

                default:
                    *p++ = *s;
                    break;
                }

                ++s;
            }

            if (p < end)
                *p++ = '"';
        };

        auto writeDouble = [&](double value)
        {
            char tmp[32];
            snprintf(tmp, sizeof(tmp), "%.1f", value);
            write(tmp);
        };

        write("[");

        for (size_t i = 0; i < m_LastTop.size(); ++i)
        {
            if (i)
                write(",");

            write("{\"name\":");
            writeJsonString(m_LastTop[i].name.c_str());

            write(",\"cpu\":");
            writeDouble(m_LastTop[i].cpu);

            write("}");
        }

        write("]");

        *p = '\0';

        return static_cast<int>(p - buffer);
    }

private:
    static uint64_t FileTimeToUInt64(const FILETIME &ft)
    {
        ULARGE_INTEGER li;
        li.LowPart = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        return li.QuadPart;
    }

private:
    using NtQuerySystemInformation_t = NTSTATUS(NTAPI *)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    NtQuerySystemInformation_t pNtQuerySystemInformation = nullptr;
    std::vector<uint8_t> m_Buffer; // reuse buffer
    uint64_t m_LastSystemTime = 0;
    std::unordered_map<DWORD, uint64_t> m_ProcessTimes;
    std::vector<ProcessInfo> m_LastTop;
    RyzenCpu &m_cpu;
    WebServer &m_server;
};