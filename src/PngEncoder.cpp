#include "radeonmon/PngEncoder.hpp"

using Microsoft::WRL::ComPtr;

PngEncoder::PngEncoder()
{
    LOG_DEBUG("[PNG] Starting PNG worker");
    m_worker = std::thread(&PngEncoder::Worker, this);
}

PngEncoder::~PngEncoder()
{
    LOG_DEBUG("[PNG] Stopping PNG worker");

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }

    m_cv.notify_one();

    if (m_worker.joinable())
        m_worker.join();

    LOG_DEBUG("[PNG] PNG worker stopped");
}

bool PngEncoder::Queue(const wchar_t *filePath)
{
    if (!filePath || !*filePath)
    {
        LOG_ERROR("[PNG] EncodeFileAsPNG: invalid file path");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_shutdown)
        {
            LOG_ERROR("[PNG] EncodeFileAsPNG: encoder is shutting down");
            return false;
        }

        m_queue.emplace(filePath);

        // LOG_DEBUG("[PNG] PNG queued: %ls (queue=%zu)", filePath, m_queue.size());
    }

    m_cv.notify_one();
    return true;
}

void PngEncoder::Worker()
{
    LOG_DEBUG("[PNG] PNG worker started");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (FAILED(hr))
    {
        LOG_ERROR("[PNG] CoInitializeEx failed: 0x%08lX", static_cast<unsigned long>(hr));
        return;
    }

    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);

    ComPtr<IWICImagingFactory> factory;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

    if (FAILED(hr))
    {
        LOG_ERROR("[PNG] CoCreateInstance(WIC) failed: 0x%08lX", static_cast<unsigned long>(hr));
        CoUninitialize();
        return;
    }

    for (;;)
    {
        std::wstring path;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_cv.wait(lock, [this]
                      { return m_shutdown || !m_queue.empty(); });

            if (m_shutdown && m_queue.empty())
                break;

            path = std::move(m_queue.front());
            m_queue.pop();
        }

        if (!Encode(factory.Get(), path.c_str()))
            LOG_ERROR("[PNG] PNG encode failed: %ls", path.c_str());
    }

    CoUninitialize();

    LOG_DEBUG("[PNG] PNG worker stopped");
}

bool PngEncoder::Encode(IWICImagingFactory *factory, const wchar_t *bmpPath)
{
    // LOG_DEBUG("[PNG] Encoding PNG: %ls", bmpPath);

    ComPtr<IWICBitmapDecoder> decoder;

    HRESULT hr = factory->CreateDecoderFromFilename(bmpPath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);

    if (FAILED(hr))
    {
        LOG_ERROR("[PNG] CreateDecoderFromFilename failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;

    hr = decoder->GetFrame(0, &frame);

    if (FAILED(hr))
        return false;

    UINT width = 0;
    UINT height = 0;

    frame->GetSize(&width, &height);

    // Keep PNG lossless.
    ComPtr<IWICFormatConverter> converter;

    hr = factory->CreateFormatConverter(&converter);

    if (FAILED(hr))
        return false;

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    if (FAILED(hr))
        return false;

    // -------------------------------------------------------------------------
    // Output filename
    // -------------------------------------------------------------------------

    std::wstring pngPath = bmpPath;

    const size_t dot = pngPath.find_last_of(L'.');

    if (dot != std::wstring::npos)
        pngPath.resize(dot);

    pngPath += L".png";

    const std::wstring tempPath = pngPath + L".tmp";

    // -------------------------------------------------------------------------
    // Output stream
    // -------------------------------------------------------------------------

    ComPtr<IWICStream> stream;

    hr = factory->CreateStream(&stream);

    if (FAILED(hr))
        return false;

    hr = stream->InitializeFromFilename(tempPath.c_str(), GENERIC_WRITE);

    if (FAILED(hr))
        return false;

    // -------------------------------------------------------------------------
    // PNG encoder
    // -------------------------------------------------------------------------

    ComPtr<IWICBitmapEncoder> encoder;

    hr = factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder);

    if (FAILED(hr))
        return false;

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    if (FAILED(hr))
        return false;

    ComPtr<IWICBitmapFrameEncode> outputFrame;

    hr = encoder->CreateNewFrame(&outputFrame, nullptr);

    if (FAILED(hr))
        return false;

    hr = outputFrame->Initialize(nullptr);

    if (FAILED(hr))
        return false;

    hr = outputFrame->SetSize(width, height);

    if (FAILED(hr))
        return false;

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

    hr = outputFrame->SetPixelFormat(&format);

    if (FAILED(hr))
        return false;

    // -------------------------------------------------------------------------
    // Encode
    // -------------------------------------------------------------------------

    hr = outputFrame->WriteSource(converter.Get(), nullptr);

    if (FAILED(hr))
        return false;

    hr = outputFrame->Commit();

    if (FAILED(hr))
        return false;

    hr = encoder->Commit();

    if (FAILED(hr))
        return false;

    // Important: release everything holding the output file.
    outputFrame.Reset();
    encoder.Reset();
    stream.Reset();
    converter.Reset();
    frame.Reset();
    decoder.Reset();

    // -------------------------------------------------------------------------
    // Replace destination
    // -------------------------------------------------------------------------

    if (!MoveFileExW(tempPath.c_str(), pngPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        LOG_ERROR("[PNG] MoveFileEx failed: %ls -> %ls (error=%lu)", tempPath.c_str(), pngPath.c_str(), GetLastError());
        DeleteFileW(tempPath.c_str());
        return false;
    }

    LOG_INFO("[PNG] PNG created: %ls", pngPath.c_str());

    if (!DeleteFileW(bmpPath))
        LOG_ERROR("[PNG] DeleteFile failed: %ls (error=%lu)", bmpPath, GetLastError());

    return true;
}
