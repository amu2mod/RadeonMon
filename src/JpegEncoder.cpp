#include "radeonmon/JpegEncoder.hpp"

#include <thread>

using Microsoft::WRL::ComPtr;

bool JpegEncoder::Queue(const wchar_t *filePath)
{
    if (!filePath || !*filePath)
    {
        LOG_ERROR("[JPG] EncodeFileAsJPEG: invalid file path");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        if (m_shutdown)
        {
            LOG_ERROR("[JPG] EncodeFileAsJPEG: encoder is shutting down");
            return false;
        }

        m_queue.emplace(filePath);

        // LOG_DEBUG("[JPG] JPEG queued: %ls (queue=%zu)", filePath, m_queue.size());
    }

    m_cv.notify_one();
    return true;
}

JpegEncoder::JpegEncoder()
{
    LOG_DEBUG("[JPG] Starting JPEG worker");
    m_worker = std::thread(&JpegEncoder::Worker, this);
}

JpegEncoder::~JpegEncoder()
{
    LOG_DEBUG("[JPG] Stopping JPEG worker");

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_shutdown = true;
    }

    m_cv.notify_one();

    if (m_worker.joinable())
        m_worker.join();

    LOG_DEBUG("[JPG] JPEG worker stopped");
}

void JpegEncoder::Worker()
{
    LOG_DEBUG("[JPG] JPEG worker started");

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CoInitializeEx failed: 0x%08lX", static_cast<unsigned long>(hr));
        return;
    }

    if (!SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL))
        LOG_ERROR("[JPG] SetThreadPriority failed: error=%lu", GetLastError());
    else
        LOG_DEBUG("[JPG] JPEG worker priority: BELOW_NORMAL");

    ComPtr<IWICImagingFactory> factory;

    hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CoCreateInstance(WIC) failed: 0x%08lX", static_cast<unsigned long>(hr));
        CoUninitialize();
        return;
    }

    LOG_DEBUG("[JPG] WIC initialized");

    for (;;)
    {
        std::wstring path;

        {
            std::unique_lock<std::mutex> lock(m_mutex);

            m_cv.wait(lock, [this]
                      { return m_shutdown || !m_queue.empty(); });

            if (m_shutdown && m_queue.empty())
            {
                LOG_DEBUG("[JPG] JPEG worker received shutdown");
                break;
            }

            path = std::move(m_queue.front());
            m_queue.pop();

            // LOG_DEBUG("[JPG] JPEG dequeued: %ls (queue=%zu)", path.c_str(), m_queue.size());
        }

        if (!Encode(factory.Get(), path.c_str()))
            LOG_ERROR("[JPG] JPEG encode failed: %ls", path.c_str());
        // else
        // LOG_DEBUG("JPEG encode complete: %ls", path.c_str());
    }

    CoUninitialize();

    LOG_DEBUG("[JPG] JPEG worker exiting");
}

bool JpegEncoder::Encode(IWICImagingFactory *factory, const wchar_t *bmpPath)
{
    // LOG_DEBUG("[JPG] Encoding BMP: %ls", bmpPath);

    // ---------------------------------------------------------------------
    // Decode BMP
    // ---------------------------------------------------------------------

    ComPtr<IWICBitmapDecoder> decoder;

    HRESULT hr = factory->CreateDecoderFromFilename(bmpPath, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CreateDecoderFromFilename failed: %ls (0x%08lX)", bmpPath, static_cast<unsigned long>(hr));
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;

    hr = decoder->GetFrame(0, &frame);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] GetFrame failed: %ls (0x%08lX)", bmpPath, static_cast<unsigned long>(hr));
        return false;
    }

    UINT width = 0;
    UINT height = 0;

    hr = frame->GetSize(&width, &height);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] GetSize failed: %ls (0x%08lX)", bmpPath, static_cast<unsigned long>(hr));
        return false;
    }

    // LOG_DEBUG("[JPG] BMP size: %ux%u", width, height);

    // ---------------------------------------------------------------------
    // Convert to 32-bit BGRA
    // ---------------------------------------------------------------------

    ComPtr<IWICFormatConverter> converter;

    hr = factory->CreateFormatConverter(&converter);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CreateFormatConverter failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] FormatConverter::Initialize failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    // ---------------------------------------------------------------------
    // Build output filename
    // ---------------------------------------------------------------------

    std::wstring jpegPath = bmpPath;

    const size_t dot = jpegPath.find_last_of(L'.');

    if (dot != std::wstring::npos)
        jpegPath.resize(dot);

    jpegPath += L".jpg";

    const std::wstring tempPath = jpegPath + L".tmp";

    // ---------------------------------------------------------------------
    // Create output stream
    // ---------------------------------------------------------------------

    ComPtr<IWICStream> stream;

    hr = factory->CreateStream(&stream);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CreateStream failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    hr = stream->InitializeFromFilename(tempPath.c_str(), GENERIC_WRITE);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] InitializeFromFilename failed: %ls (0x%08lX)", tempPath.c_str(), static_cast<unsigned long>(hr));
        return false;
    }

    // ---------------------------------------------------------------------
    // JPEG encoder
    // ---------------------------------------------------------------------

    ComPtr<IWICBitmapEncoder> encoder;

    hr = factory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &encoder);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CreateEncoder failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] Encoder::Initialize failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    // ---------------------------------------------------------------------
    // Frame
    // ---------------------------------------------------------------------

    ComPtr<IWICBitmapFrameEncode> outputFrame;
    ComPtr<IPropertyBag2> properties;

    hr = encoder->CreateNewFrame(&outputFrame, &properties);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] CreateNewFrame failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    // JPEG quality: 90%
    PROPBAG2 option = {};
    option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");

    VARIANT value;
    VariantInit(&value);

    value.vt = VT_R4;
    value.fltVal = 0.90f;

    hr = properties->Write(1, &option, &value);

    VariantClear(&value);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] Setting JPEG quality failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    hr = outputFrame->Initialize(properties.Get());

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] Frame::Initialize failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    hr = outputFrame->SetSize(width, height);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] SetSize failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;

    hr = outputFrame->SetPixelFormat(&format);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] SetPixelFormat failed: 0x%08lX", static_cast<unsigned long>(hr));
        return false;
    }

    // ---------------------------------------------------------------------
    // Encode
    // ---------------------------------------------------------------------

    // LOG_DEBUG("[JPG] Writing JPEG: %ls", jpegPath.c_str());

    hr = outputFrame->WriteSource(converter.Get(), nullptr);

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] WriteSource failed: %ls (0x%08lX)", bmpPath, static_cast<unsigned long>(hr));
        return false;
    }

    hr = outputFrame->Commit();

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] Frame::Commit failed: %ls (0x%08lX)", bmpPath, static_cast<unsigned long>(hr));
        return false;
    }

    hr = encoder->Commit();

    if (FAILED(hr))
    {
        LOG_ERROR("[JPG] Encoder::Commit failed: %ls (0x%08lX)", bmpPath, static_cast<unsigned long>(hr));
        return false;
    }

    // Release WIC objects before touching the file.
    outputFrame.Reset();
    properties.Reset();
    encoder.Reset();
    stream.Reset();
    converter.Reset();
    frame.Reset();
    decoder.Reset();

    // ---------------------------------------------------------------------
    // Replace destination
    // ---------------------------------------------------------------------

    if (!MoveFileExW(tempPath.c_str(), jpegPath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        LOG_ERROR("[JPG] MoveFileEx failed: %ls -> %ls (error=%lu)", tempPath.c_str(), jpegPath.c_str(), GetLastError());

        DeleteFileW(tempPath.c_str());
        return false;
    }

    LOG_INFO("[JPG] JPEG created: %ls", jpegPath.c_str());

    // ---------------------------------------------------------------------
    // Delete source BMP after successful encoding.
    //
    // Disabled during testing.
    // ---------------------------------------------------------------------

    if (!DeleteFileW(bmpPath))
        LOG_ERROR("DeleteFile failed: %ls (error=%lu)", bmpPath, GetLastError());

    return true;
}
