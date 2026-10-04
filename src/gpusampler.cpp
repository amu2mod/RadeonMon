#include "radeonmon/gpusampler.hpp"

#include <windows.h>
#include <d3dkmthk.h>
#include <dxgi.h>

#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <unordered_set>
#include <vector>

#pragma comment(lib, "dxgi.lib")

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

	ProcessGpuUsage Sample(const std::vector<DWORD> &pids)
	{
		ProcessGpuUsage result;

		if (!queryStatistics || adapters.empty())
			return result;

		if (pids.empty())
			return result;

		// Remove duplicate PIDs.
		std::unordered_set<DWORD> uniquePids;

		uniquePids.reserve(pids.size());

		for (DWORD pid : pids)
		{
			if (pid != 0)
				uniquePids.insert(pid);
		}

		for (DWORD pid : uniquePids)
		{
			HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);

			if (!process)
				continue;

			uint64_t totalDedicated = 0;

			// Query every physical DXGI adapter.
			for (const AdapterInfo &adapter : adapters)
			{
				// First determine how many memory segments this process has on this adapter
				D3DKMT_QUERYSTATISTICS processAdapterQuery{};
				processAdapterQuery.Type = D3DKMT_QUERYSTATISTICS_PROCESS_ADAPTER;
				processAdapterQuery.AdapterLuid = adapter.luid;
				processAdapterQuery.hProcess = process;
				NTSTATUS status = queryStatistics(&processAdapterQuery);

				if (status < 0)
					continue;

				const ULONG segmentCount = processAdapterQuery.QueryResult.ProcessAdapterInformation.NbSegments;

				// Query each process segment
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

					// We need to know whether this segment is an aperture/shared segment or actual dedicated video memory
					// Query the adapter's corresponding segment
					D3DKMT_QUERYSTATISTICS adapterSegmentQuery{};
					adapterSegmentQuery.Type = D3DKMT_QUERYSTATISTICS_SEGMENT;
					adapterSegmentQuery.AdapterLuid = adapter.luid;
					adapterSegmentQuery.QuerySegment.SegmentId = segment;
					status = queryStatistics(&adapterSegmentQuery);

					if (status < 0)
						continue;

					const auto &adapterSegmentInfo = adapterSegmentQuery.QueryResult.SegmentInformation;

					// Aperture == 0 means this is a dedicated video-memory segment
					if (adapterSegmentInfo.Aperture == 0)
						totalDedicated += segmentInfo.BytesCommitted;
				}
			}

			CloseHandle(process);

			if (totalDedicated > 0)
				result.emplace(pid, totalDedicated);
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
