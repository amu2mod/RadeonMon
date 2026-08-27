#include "radeonmon/VRRDetector.hpp"
#include "radeonmon/logging.hpp"

#include <windows.h>
#include <dxgi.h>

VRRDetector::VRRDetector(RadeonMon::Hardware::DisplayManager &displayManager) : m_displayManager(displayManager) {};

void VRRDetector::Start()
{
    if (m_running) // singleton
        return;

    m_running = true;
    m_thread = std::thread(&VRRDetector::VBlankThread, this);
}

void VRRDetector::Stop()
{
    m_running = false;

    if (m_thread.joinable())
        m_thread.join();
}

void VRRDetector::VBlankThread()
{
    IDXGIFactory *factory = nullptr;
    IDXGIAdapter *adapter = nullptr;
    IDXGIOutput *output = nullptr;

    if (FAILED(CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void **>(&factory))))
        return;

    if (FAILED(factory->EnumAdapters(0, &adapter)))
    {
        factory->Release();
        return;
    }

    LARGE_INTEGER qpcFreq;
    QueryPerformanceFrequency(&qpcFreq);

    LONGLONG previousTimestamp = 0;
    ULONGLONG lastLogTime = GetTickCount64();

    int currentOutputIndex = -1;

    while (m_running)
    {
        // Check which display we're supposed to monitor.
        const auto current = m_displayManager.Current();

        if (!current)
        {
            if (output)
            {
                output->Release();
                output = nullptr;
            }

            currentOutputIndex = -1;
            previousTimestamp = 0;

            std::this_thread::yield();
            continue;
        }

        const int newOutputIndex = current->index;

        // Display changed, or we haven't acquired one yet.
        if (newOutputIndex != currentOutputIndex)
        {
            if (output)
            {
                output->Release();
                output = nullptr;
            }

            if (FAILED(adapter->EnumOutputs(newOutputIndex, &output)))
            {
                currentOutputIndex = -1;
                previousTimestamp = 0;

                std::this_thread::yield();
                continue;
            }

            currentOutputIndex = newOutputIndex;

            // Don't use a timestamp from the previous display.
            previousTimestamp = 0;

            lastLogTime = GetTickCount64();
        }

        // This blocks until the next vblank on the current display.
        if (FAILED(output->WaitForVBlank()))
        {
            output->Release();
            output = nullptr;
            currentOutputIndex = -1;
            previousTimestamp = 0;
            continue;
        }

        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);

        if (previousTimestamp != 0)
        {
            double elapsedSec = double(now.QuadPart - previousTimestamp) / double(qpcFreq.QuadPart);
            const int hz = elapsedSec > 0.0 ? static_cast<int>((1.0 / elapsedSec) + 0.5) : 0;
            m_currentHz.store(hz, std::memory_order_relaxed);
        }

        previousTimestamp = now.QuadPart;
    }

    if (output)
        output->Release();

    adapter->Release();
    factory->Release();
}

bool VRRDetector::IsVRROn() const
{
    const int currentHz = m_currentHz.load(std::memory_order_relaxed);
    const int maxHz = m_displayManager.Current().value().frequency;
    const bool isOn = currentHz < (maxHz - c_tolerance);

#ifdef LOGVRR
    if (isOn)
        LOG_DEBUG("[VRR] On: %d / %d Hz", currentHz, maxHz);
    else
        LOG_TRACE("[VRR] Off: %d / %d Hz", currentHz, maxHz);
#endif

    return isOn;
}
