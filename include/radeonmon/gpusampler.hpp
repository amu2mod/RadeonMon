#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>
#include <windows.h>

/**
 * GpuSampler
 *
 * Samples per-process dedicated GPU memory usage.
 *
 * The caller supplies only the PIDs it is interested in, allowing the
 * sampler to avoid maintaining results for every process.
 */
class GpuSampler
{
  public:
	using ProcessGpuUsage = std::unordered_map<DWORD, uint64_t>;

  public:
	GpuSampler();
	~GpuSampler();

	GpuSampler(const GpuSampler &) = delete;
	GpuSampler &operator=(const GpuSampler &) = delete;

	/**
	 * Query dedicated GPU memory usage for the specified processes.
	 *
	 * @param pids Process IDs to query.
	 * @return Map of PID -> dedicated GPU memory usage in bytes.
	 *
	 * Processes for which GPU usage cannot be obtained are omitted.
	 */
	ProcessGpuUsage Sample(const std::vector<DWORD> &pids);

  private:
	struct Impl;
	Impl *m_impl = nullptr;
};
