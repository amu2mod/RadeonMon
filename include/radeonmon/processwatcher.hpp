#pragma once
#include "radeonmon/ryzen.hpp"

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

struct ProcessInfo
{
    std::string name;
    double cpu;
};

/**
 * ProcessWatcher class
 */
class ProcessWatcher
{
public:
    RyzenMetrics m_ryzenMetrics;

public:
    ProcessWatcher(RyzenCpu &cpuRef) : m_cpu(cpuRef)
    {
        Initialize();
    }

    void Initialize();
    std::vector<ProcessInfo> Poll();
    int BuildJson(char *buffer, int bufferSize) const;

    inline std::vector<ProcessInfo> GetProcessList() const { return m_LastTop; }

#ifdef _DEBUG
    void Log() const;
#endif

private:
    using NtQuerySystemInformation_t = NTSTATUS(NTAPI *)(SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    NtQuerySystemInformation_t pNtQuerySystemInformation = nullptr;
    std::vector<uint8_t> m_Buffer; // reuse buffer
    uint64_t m_LastSystemTime = 0;
    std::unordered_map<DWORD, uint64_t> m_ProcessTimes;
    std::vector<ProcessInfo> m_LastTop;
    RyzenCpu &m_cpu;
};