#pragma once

#include "radeonmon/logging.hpp"

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>

/**
 * Asynchronous JPEG encoder with a dedicated low-priority worker thread.
 *
 * Encoding requests are queued and processed in the background.
 *
 * Each request produces one encoded JPEG file. The source file is deleted
 * after successful encoding.
 *
 * JPEG quality is fixed at 90%.
 */
class JpegEncoder
{
public:
    JpegEncoder();
    ~JpegEncoder();
    bool Queue(const wchar_t *filePath);

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::queue<std::wstring> m_queue;
    bool m_shutdown = false;
    std::thread m_worker;

private:
    JpegEncoder(const JpegEncoder &) = delete;
    JpegEncoder &operator=(const JpegEncoder &) = delete;

    void Worker();
    bool Encode(IWICImagingFactory *factory, const wchar_t *bmpPath);
};
