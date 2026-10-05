#include "radeonmon/gpusampler.hpp"

#include <windows.h>
#include <d3dkmthk.h>
#include <dxgi.h>

#include <wrl/client.h>

#include <pdh.h>
#include <pdhmsg.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>
#include <string>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "pdh.lib")

using Microsoft::WRL::ComPtr;

namespace
{
using D3DKMTQueryStatistics_t = NTSTATUS(APIENTRY *)(D3DKMT_QUERYSTATISTICS *);

struct AdapterInfo
{
	LUID luid{};
};

bool IsZeroLuid(const LUID &luid) { return luid.LowPart == 0 && luid.HighPart == 0; }

bool SameLuid(const LUID &a, const LUID &b) { return a.LowPart == b.LowPart && a.HighPart == b.HighPart; }

D3DKMTQueryStatistics_t GetQueryStatistics()
{
	static D3DKMTQueryStatistics_t fn = []() -> D3DKMTQueryStatistics_t
	{
		HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");

		if (!gdi32)
			gdi32 = LoadLibraryW(L"gdi32.dll");

		if (!gdi32)
			return nullptr;

		return reinterpret_cast<D3DKMTQueryStatistics_t>(GetProcAddress(gdi32, "D3DKMTQueryStatistics"));
	}();

	return fn;
}

std::vector<AdapterInfo> EnumerateAdapters()
{
	std::vector<AdapterInfo> adapters;
	ComPtr<IDXGIFactory1> factory;
	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

	if (FAILED(hr))
		return adapters;

	for (UINT index = 0;; ++index)
	{
		ComPtr<IDXGIAdapter1> adapter;

		hr = factory->EnumAdapters1(index, &adapter);

		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
			continue;

		DXGI_ADAPTER_DESC1 desc{};

		if (FAILED(adapter->GetDesc1(&desc)))
			continue;

		// Skip software adapters.
		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		AdapterInfo info;
		info.luid = desc.AdapterLuid;

		if (!IsZeroLuid(info.luid))
			adapters.push_back(info);
	}

	return adapters;
}
} // namespace

struct GpuSampler::Impl
{
	D3DKMTQueryStatistics_t queryStatistics = nullptr;
	std::vector<AdapterInfo> adapters;
	bool initialized = false;

	void Initialize()
	{
		if (initialized)
			return;

		initialized = true;

		queryStatistics = GetQueryStatistics();

		if (!queryStatistics)
			return;

		adapters = EnumerateAdapters();
	}

	// Helper: extract PID from a GPU Process Memory instance name. Typical form: "pid_12345_luid_0x00000000_0x00001234_phys_0"
	static DWORD PidFromInstance(const std::wstring &instance)
	{
		const std::wstring prefix = L"pid_";
		size_t pos = instance.find(prefix);
		if (pos == std::wstring::npos)
			return 0;
		pos += prefix.size();
		size_t end = instance.find(L'_', pos);
		if (end == std::wstring::npos)
			end = instance.size();
		try
		{
			return static_cast<DWORD>(std::stoul(instance.substr(pos, end - pos)));
		}
		catch (...)
		{
			return 0;
		}
	}

	ProcessGpuUsage Sample(const std::vector<GpuProcess> &processes)
	{
		ProcessGpuUsage result;

		if (!queryStatistics || adapters.empty() || processes.empty())
			return result;

		// 1. Deduplicate processes
		std::unordered_map<DWORD, std::string_view> uniqueProcesses;
		uniqueProcesses.reserve(processes.size());

		for (const GpuProcess &process : processes)
		{
			if (process.pid == 0)
				continue;

			uniqueProcesses.try_emplace(process.pid, process.name);
		}

		if (uniqueProcesses.empty())
			return result;

		// PIDs for which the primary D3DKMT path failed.
		std::vector<DWORD> failedPids;
		failedPids.reserve(uniqueProcesses.size());

		// 2. Primary path – D3DKMT
		for (const auto &[pid, name] : uniqueProcesses)
		{
			HANDLE processHandle = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);

			if (!processHandle)
				processHandle = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

			if (!processHandle)
			{
				failedPids.push_back(pid);
				continue;
			}

			uint64_t totalDedicated = 0;

			for (const AdapterInfo &adapter : adapters)
			{
				D3DKMT_QUERYSTATISTICS processAdapterQuery{};
				processAdapterQuery.Type = D3DKMT_QUERYSTATISTICS_PROCESS_ADAPTER;
				processAdapterQuery.AdapterLuid = adapter.luid;
				processAdapterQuery.hProcess = processHandle;

				NTSTATUS status = queryStatistics(&processAdapterQuery);

				if (status < 0)
					continue;

				const ULONG segmentCount = processAdapterQuery.QueryResult.ProcessAdapterInformation.NbSegments;

				for (ULONG segment = 0; segment < segmentCount; ++segment)
				{
					D3DKMT_QUERYSTATISTICS segmentQuery{};
					segmentQuery.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT;
					segmentQuery.AdapterLuid = adapter.luid;
					segmentQuery.hProcess = processHandle;
					segmentQuery.QueryProcessSegment.SegmentId = segment;

					status = queryStatistics(&segmentQuery);

					if (status < 0)
						continue;

					D3DKMT_QUERYSTATISTICS adapterSegmentQuery{};
					adapterSegmentQuery.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
					adapterSegmentQuery.AdapterLuid = adapter.luid;
					adapterSegmentQuery.QuerySegment.SegmentId = segment;

					status = queryStatistics(&adapterSegmentQuery);

					if (status < 0)
						continue;

					const auto &segmentInfo = segmentQuery.QueryResult.ProcessSegmentInformation;

					const auto &adapterSegmentInfo = adapterSegmentQuery.QueryResult.SegmentInformation;

					// Aperture == 0 means dedicated memory.
					if (adapterSegmentInfo.Aperture == 0)
						totalDedicated += segmentInfo.BytesCommitted;
				}
			}

			CloseHandle(processHandle);

			if (totalDedicated > 0)
			{
				result.emplace(pid, totalDedicated);
				continue;
			}

			// We avoid fallback for dwm as PDH can report 10GB VRAM usage
			if (name != "dwm.exe")
				failedPids.push_back(pid);
		}

		// 3. Fallback – Windows Perf counters PDH
		if (failedPids.empty())
			return result;

		std::unordered_set<DWORD> wantedPids;
		wantedPids.reserve(failedPids.size());

		for (DWORD pid : failedPids)
			wantedPids.insert(pid);

		PDH_HQUERY query = nullptr;

		if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS)
			return result;

		PDH_HCOUNTER counter = nullptr;

		constexpr const wchar_t *GPU_MEMORY_COUNTER = L"\\GPU Process Memory(*)\\Dedicated Usage";

		if (PdhAddEnglishCounterW(query, GPU_MEMORY_COUNTER, 0, &counter) != ERROR_SUCCESS)
		{
			PdhCloseQuery(query);
			return result;
		}

		// First collection establishes the counter state.
		PdhCollectQueryData(query);

		// Give the provider a small amount of time to update.
		Sleep(30);

		PdhCollectQueryData(query);

		DWORD bufferSize = 0;
		DWORD itemCount = 0;

		PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &bufferSize, &itemCount, nullptr);

		if (status != PDH_MORE_DATA || bufferSize == 0)
		{
			PdhCloseQuery(query);
			return result;
		}

		std::vector<BYTE> buffer(bufferSize);

		auto *items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());

		status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &bufferSize, &itemCount, items);

		if (status == ERROR_SUCCESS)
		{
			for (DWORD i = 0; i < itemCount; ++i)
			{
				const DWORD pid = PidFromInstance(items[i].szName);

				if (pid == 0 || wantedPids.find(pid) == wantedPids.end())
					continue;

				const uint64_t bytes = static_cast<uint64_t>(items[i].FmtValue.largeValue);

				if (bytes > 0)
					result[pid] += bytes;
			}
		}

		PdhCloseQuery(query);

		return result;
	}
};

GpuSampler::GpuSampler() : m_impl(new Impl()) { m_impl->Initialize(); }

GpuSampler::~GpuSampler()
{
	delete m_impl;
	m_impl = nullptr;
}

GpuSampler::ProcessGpuUsage GpuSampler::Sample(const std::vector<GpuProcess> &pids)
{
	if (!m_impl)
		return {};

	return m_impl->Sample(pids);
}
