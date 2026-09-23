#pragma once

#include "radeonmon/structures.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#pragma comment(lib, "dxgi.lib")

class VRRDetector
{
public:
    VRRDetector(RadeonMon::Hardware::DisplayManager &);
    ~VRRDetector() { Stop(); }
    void Start();
    void Stop();
    inline bool IsRunning() const { return m_running; }
    inline int CurrentHz() const { return m_currentHz.load(std::memory_order_relaxed); }
    bool IsVRROn() const;

private:
    void VBlankThread();

private:
    std::thread m_thread;
    std::atomic<bool> m_running{false};
    std::atomic<int> m_currentHz{0};
    RadeonMon::Hardware::DisplayManager &m_displayManager;

    // Constants
    static constexpr uint8_t c_maxSamples = 2;
    static constexpr ULONGLONG c_tickRateMs = 1000;
    static constexpr uint8_t c_tolerance = 2; // jitter tolerance in Hz
};