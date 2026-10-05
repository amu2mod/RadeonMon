#include "radeonmon/processwatcher.hpp"

/// Helpers
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

static uint64_t FileTimeToUInt64(const FILETIME &ft)
{
	ULARGE_INTEGER li;
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	return li.QuadPart;
}

/// Class Methods
void ProcessWatcher::Initialize()
{
	m_Buffer.resize(1024 * 1024); // 1 MB
	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	pNtQuerySystemInformation = (NtQuerySystemInformation_t)GetProcAddress(ntdll, "NtQuerySystemInformation");
}

std::vector<ProcessInfo> ProcessWatcher::Poll()
{
	if (!pNtQuerySystemInformation)
		return m_LastTop;

	ULONG returnLength = 0;

	NTSTATUS status = pNtQuerySystemInformation(SystemProcessInformation, m_Buffer.data(), (ULONG)m_Buffer.size(), &returnLength);

	if (status == STATUS_INFO_LENGTH_MISMATCH)
	{
		m_Buffer.resize(returnLength + 65536);

		status = pNtQuerySystemInformation(SystemProcessInformation, m_Buffer.data(), (ULONG)m_Buffer.size(), &returnLength);
	}

	if (!NT_SUCCESS(status))
		return m_LastTop;

	// System CPU time
	FILETIME idle, kernel, user;
	GetSystemTimes(&idle, &kernel, &user);

	uint64_t systemTime = FileTimeToUInt64(kernel) + FileTimeToUInt64(user);

	uint64_t systemDelta = (m_LastSystemTime != 0 && systemTime >= m_LastSystemTime) ? systemTime - m_LastSystemTime : 0;

	m_LastSystemTime = systemTime;

	// Temporary process entry
	struct Entry
	{
		DWORD pid;
		std::string name;
		double cpu;
		uint64_t ramUsage;
	};

	std::vector<Entry> usage;
	std::unordered_set<DWORD> activePids;

	auto *spi = reinterpret_cast<MY_SYSTEM_PROCESS_INFORMATION *>(m_Buffer.data());

	// Enumerate processes
	while (true)
	{
		DWORD pid = static_cast<DWORD>(reinterpret_cast<ULONG_PTR>(spi->UniqueProcessId));

		if (pid == 0)
		{
			if (spi->NextEntryOffset == 0)
				break;

			spi = reinterpret_cast<MY_SYSTEM_PROCESS_INFORMATION *>(reinterpret_cast<BYTE *>(spi) + spi->NextEntryOffset);

			continue;
		}

		activePids.insert(pid);

		// Process CPU time
		uint64_t procTime = static_cast<uint64_t>(spi->UserTime.QuadPart) + static_cast<uint64_t>(spi->KernelTime.QuadPart);

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

		// Resolve process name now.
		std::string name;

		if (spi->ImageName.Buffer && spi->ImageName.Length > 0)
			name = WideToUtf8(spi->ImageName.Buffer, spi->ImageName.Length / sizeof(WCHAR));
		else if (pid == 4)
			name = "System";
		else
			name = "<unknown>";

		usage.push_back({pid, std::move(name), cpu, static_cast<uint64_t>(spi->WorkingSetSize)});

		if (spi->NextEntryOffset == 0)
			break;

		spi = reinterpret_cast<MY_SYSTEM_PROCESS_INFORMATION *>(reinterpret_cast<BYTE *>(spi) + spi->NextEntryOffset);
	}

	// Cleanup stale process history
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

	// Filter processes
	constexpr double MIN_CPU = 0.001;

	usage.erase(std::remove_if(usage.begin(), usage.end(),
							   [](const Entry &e)
							   {
								   // Always keep System
								   if (e.pid == 4)
									   return false;

								   return e.cpu < MIN_CPU;
							   }),
				usage.end());

	// CPU normalization
	double processTotal = 0.0;

	for (const auto &entry : usage)
		processTotal += entry.cpu;

	m_ryzenMetrics = m_cpu.GetMetrics();

	double &realCpu = m_ryzenMetrics.usage;

	if (processTotal > 0.0 && realCpu >= 0.0)
	{
		double scale = realCpu / processTotal;

		for (auto &entry : usage)
		{
			entry.cpu *= scale;

			if (entry.cpu > 0.0)
				entry.cpu += (entry.pid % 100) * 0.00001; // Preserve small visual differences
		}
	}

	// Sort by CPU
	std::sort(usage.begin(), usage.end(),
			  [](const Entry &a, const Entry &b)
			  {
				  if (std::abs(a.cpu - b.cpu) < 0.0001)
					  return a.pid < b.pid;

				  return a.cpu > b.cpu;
			  });

	// 5 top processes
	constexpr size_t MAX_PROCESSES = 5;
	const size_t count = std::min<size_t>(MAX_PROCESSES, usage.size());

	// GPU VRAM
	std::vector<GpuSampler::GpuProcess> gpuProcesses;
	gpuProcesses.reserve(count);

	for (size_t i = 0; i < count; ++i)
	{
		gpuProcesses.push_back({usage[i].pid, usage[i].name});
	}

	std::unordered_map<DWORD, uint64_t> gpuUsage;

	// START_CHRONO(gpusampler);
	if (!gpuProcesses.empty())
		gpuUsage = m_gpuSampler.Sample(gpuProcesses);
	// END_CHRONO(gpusampler, "Gpu Sampler");

	// Build final result
	std::vector<ProcessInfo> result;
	result.reserve(count);

	for (size_t i = 0; i < count; ++i)
	{
		const Entry &entry = usage[i];

		uint64_t gpuVramUsage = 0;

		auto gpuIt = gpuUsage.find(entry.pid);

		if (gpuIt != gpuUsage.end())
			gpuVramUsage = gpuIt->second;

		result.push_back({entry.name, entry.cpu, entry.ramUsage, gpuVramUsage});
	}

	m_LastTop = result;

	// Log();

	return result;
}

#ifdef _DEBUG
void ProcessWatcher::Log() const
{
	for (const auto &process : m_LastTop)
	{
		double ramMB = static_cast<double>(process.ramUsage) / (1024.0 * 1024.0);
		double gpuVramMB = static_cast<double>(process.gpuVramUsage) / (1024.0 * 1024.0);

		LOG_DEBUG("%s, RAM %.1fMB, VRAM %.1fMB (%.1f%%)", process.name.c_str(), ramMB, gpuVramMB, process.cpu);
	}
}

#endif

int ProcessWatcher::BuildJson(char *buffer, int bufferSize) const
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