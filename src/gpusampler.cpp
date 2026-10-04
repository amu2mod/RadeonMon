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

	// Helper: extract PID from a GPU Process Memory instance name.
	// Typical form: "pid_12345_luid_0x00000000_0x00001234_phys_0"
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

	// -------------------------------------------------
	// Helper – detect Desktop Window Manager
	// -------------------------------------------------
	static bool IsDwmProcess(DWORD pid)
	{
		HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
		if (!h)
			return false;

		wchar_t path[MAX_PATH]{};
		DWORD size = MAX_PATH;
		bool isDwm = false;

		if (QueryFullProcessImageNameW(h, 0, path, &size))
		{
			const wchar_t *name = wcsrchr(path, L'\\');
			name = name ? name + 1 : path;
			isDwm = (_wcsicmp(name, L"dwm.exe") == 0);
		}

		CloseHandle(h);
		return isDwm;
	}

	ProcessGpuUsage Sample(const std::vector<DWORD> &pids)
	{
		ProcessGpuUsage result;
		if (!queryStatistics || adapters.empty() || pids.empty())
			return result;

		// -------------------------------------------------
		// 1. Deduplicate & prepare
		// -------------------------------------------------
		std::unordered_set<DWORD> uniquePids;
		uniquePids.reserve(pids.size());
		for (DWORD pid : pids)
			if (pid != 0)
				uniquePids.insert(pid);

		std::vector<DWORD> failedPids; // PIDs that D3DKMT could not open / query
		failedPids.reserve(uniquePids.size());

		// -------------------------------------------------
		// 2. Primary path – D3DKMT (accurate numbers)
		// -------------------------------------------------
		for (DWORD pid : uniquePids)
		{
			// Prefer limited rights first (works for more protected processes)
			HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
			if (!process)
				process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

			if (!process)
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
				processAdapterQuery.hProcess = process;

				NTSTATUS status = queryStatistics(&processAdapterQuery);
				if (status < 0)
					continue;

				const ULONG segmentCount = processAdapterQuery.QueryResult.ProcessAdapterInformation.NbSegments;

				for (ULONG segment = 0; segment < segmentCount; ++segment)
				{
					D3DKMT_QUERYSTATISTICS segmentQuery{};
					segmentQuery.Type = D3DKMT_QUERYSTATISTICS_PROCESS_SEGMENT;
					segmentQuery.AdapterLuid = adapter.luid;
					segmentQuery.hProcess = process;
					segmentQuery.QueryProcessSegment.SegmentId = segment;

					status = queryStatistics(&segmentQuery);
					if (status < 0)
						continue;

					const auto &segmentInfo = segmentQuery.QueryResult.ProcessSegmentInformation;

					// Check whether this segment is dedicated (Aperture == 0)
					D3DKMT_QUERYSTATISTICS adapterSegmentQuery{};
					adapterSegmentQuery.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
					adapterSegmentQuery.AdapterLuid = adapter.luid;
					adapterSegmentQuery.QuerySegment.SegmentId = segment;

					status = queryStatistics(&adapterSegmentQuery);
					if (status < 0)
						continue;

					const auto &adapterSegmentInfo = adapterSegmentQuery.QueryResult.SegmentInformation;

					if (adapterSegmentInfo.Aperture == 0)
						totalDedicated += segmentInfo.BytesCommitted;
				}
			}

			CloseHandle(process);

			if (totalDedicated > 0)
				result.emplace(pid, totalDedicated);
			else
				// Handle opened but zero dedicated → only fall back if NOT DWM
				if (!IsDwmProcess(pid))
					failedPids.push_back(pid); // got a handle but zero / failed queries → try PDH
		}

		// -------------------------------------------------
		// 3. Fallback – PDH only for the PIDs that failed
		// -------------------------------------------------
		if (!failedPids.empty())
		{
			std::unordered_set<DWORD> wanted(failedPids.begin(), failedPids.end());

			PDH_HQUERY query = nullptr;
			if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS)
			{
				PDH_HCOUNTER counter = nullptr;
				const wchar_t *path = L"\\GPU Process Memory(*)\\Dedicated Usage";

				if (PdhAddEnglishCounterW(query, path, 0, &counter) == ERROR_SUCCESS)
				{
					// Two collects are required for the first sample after adding a counter
					PdhCollectQueryData(query);
					Sleep(30); // small delay helps on some systems
					PdhCollectQueryData(query);

					DWORD bufSize = 0;
					DWORD itemCount = 0;
					PDH_STATUS st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &bufSize, &itemCount, nullptr);

					if (st == PDH_MORE_DATA && bufSize > 0)
					{
						std::vector<BYTE> buffer(bufSize);
						auto items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W *>(buffer.data());

						st = PdhGetFormattedCounterArrayW(counter, PDH_FMT_LARGE, &bufSize, &itemCount, items);

						if (st == ERROR_SUCCESS)
						{
							for (DWORD i = 0; i < itemCount; ++i)
							{
								DWORD pid = PidFromInstance(items[i].szName);
								if (pid == 0 || wanted.find(pid) == wanted.end())
									continue;

								uint64_t bytes = static_cast<uint64_t>(items[i].FmtValue.largeValue);
								if (bytes > 0)
									result[pid] += bytes; // sum across phys adapters
							}
						}
					}
				}
				PdhCloseQuery(query);
			}
		}

		return result;
	}
};

GpuSampler::GpuSampler() : m_impl(new Impl()) { m_impl->Initialize(); }

GpuSampler::~GpuSampler()
{
	delete m_impl;
	m_impl = nullptr;
}

GpuSampler::ProcessGpuUsage GpuSampler::Sample(const std::vector<DWORD> &pids)
{
	if (!m_impl)
		return {};

	return m_impl->Sample(pids);
}
