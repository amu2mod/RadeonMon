#include "radeonmon/Screenshot.hpp"
#include "radeonmon/logging.hpp"

#include <cmath>
#include <algorithm>

Screenshot::Screenshot()
{
	HMODULE win32u = LoadLibraryW(L"win32u.dll");

	if (!win32u)
	{
		LOG_ERROR("win32u.dll not found");
		return;
	}

	m_NtUserQueryWindow = reinterpret_cast<NtUserQueryWindow_t>(GetProcAddress(win32u, "NtUserQueryWindow"));

	if (!m_NtUserQueryWindow)
	{
		LOG_ERROR("NtUserQueryWindow not found");
		return;
	}
}

Screenshot::~Screenshot() { ShutdownDXGI(); }

bool Screenshot::SetPath(const wchar_t *newPath)
{
	if (newPath == nullptr || newPath[0] == L'\0')
		return false;

	const size_t len = wcslen(newPath);

	// Check that the path exists and is a directory.
	const DWORD attributes = GetFileAttributesW(newPath);
	if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY))
		return false;

	// Check write access before modifying the path.
	if (_waccess_s(newPath, 2) != 0)
		return false;

	// Copy the path, appending '\' if necessary.
	if (len > 0 && (newPath[len - 1] == L'\\' || newPath[len - 1] == L'/'))
	{
		if (wcscpy_s(m_path, _countof(m_path), newPath) != 0)
			return false;
	}
	else
	{
		if (wcscpy_s(m_path, _countof(m_path), newPath) != 0)
			return false;

		if (wcscat_s(m_path, _countof(m_path), L"\\") != 0)
			return false;
	}

	return true;
}

bool Screenshot::SaveBitmapToFile(const ScreenshotBuffer &screenshot, const wchar_t *filePath)
{
	if (filePath == nullptr)
		return false;

	const int width = screenshot.width;
	const int height = screenshot.height;

	if (width <= 0 || height <= 0)
	{
		LOG_ERROR("[Screenshot] Invalid bitmap dimensions: %dx%d", width, height);
		return false;
	}

	const size_t rowSize = static_cast<size_t>(width) * 4;
	const size_t imageSize = rowSize * static_cast<size_t>(height);

	if (screenshot.pixels.size() != imageSize)
	{
		LOG_ERROR("[Screenshot] Invalid pixel buffer size: %zu, expected %zu", screenshot.pixels.size(), imageSize);
		return false;
	}

	BITMAPINFOHEADER bi{};
	bi.biSize = sizeof(BITMAPINFOHEADER);
	bi.biWidth = width;
	bi.biHeight = -height; // Top-down
	bi.biPlanes = 1;
	bi.biBitCount = 32;
	bi.biCompression = BI_RGB;
	bi.biSizeImage = static_cast<DWORD>(imageSize);

	BITMAPFILEHEADER bmfHeader{};
	bmfHeader.bfType = 0x4D42; // "BM"
	bmfHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

	bmfHeader.bfSize = bmfHeader.bfOffBits + static_cast<DWORD>(imageSize);

	HANDLE hFile = CreateFileW(filePath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

	if (hFile == INVALID_HANDLE_VALUE)
	{
		LOG_ERROR("[Screenshot] CreateFileW failed. Error: %lu", GetLastError());
		return false;
	}

	bool success = true;
	DWORD written = 0;

	// Write BMP file header.
	if (!WriteFile(hFile, &bmfHeader, sizeof(bmfHeader), &written, nullptr) || written != sizeof(bmfHeader))
		success = false;

	// Write DIB header.
	if (success)
		if (!WriteFile(hFile, &bi, sizeof(bi), &written, nullptr) || written != sizeof(bi))
			success = false;

	// Write pixel data.
	if (success)
		if (!WriteFile(hFile, screenshot.pixels.data(), static_cast<DWORD>(imageSize), &written, nullptr) || written != imageSize)
			success = false;

	const DWORD error = success ? ERROR_SUCCESS : GetLastError();

	CloseHandle(hFile);

	if (!success)
	{
		LOG_ERROR("[Screenshot] WriteFile failed. Error: %lu", error);
		return false;
	}

	LOG_INFO("[Screenshot] Successfully saved as %ls", screenshot.filename.c_str());

	return true;
}

bool Screenshot::EncodeFileAsJPEG(const wchar_t *filePath) { return m_jpegEncoder.Queue(filePath); }

bool Screenshot::EncodeFileAsPNG(const wchar_t *filePath) { return m_pngEncoder.Queue(filePath); }

DWORD Screenshot::GetProcessIdFromWindow(HWND hwnd)
{
	DWORD pid = 0;

	// Fast (documented path)
	if (GetWindowThreadProcessId(hwnd, &pid) && pid)
		return pid;

	// Undocumented Fallback
	if (m_NtUserQueryWindow)
	{
		pid = static_cast<DWORD>(m_NtUserQueryWindow(hwnd, 0)); // 0 for window PID

		if (pid)
			return pid;
	}

	return 0;
}

const wchar_t *Screenshot::GetCachedProcessName(HWND hwnd)
{
	if (hwnd == nullptr)
		return L"unknown";

	// Same window as last time.
	if (hwnd == m_lastHwnd)
		return m_lastProcessName;

	m_lastHwnd = hwnd;

	DWORD processId = GetProcessIdFromWindow(hwnd);

	wcscpy_s(m_lastProcessName, LASTPROCESSNAMECOUNT, L"unknown");

	if (processId == 0)
	{
		LOG_ERROR("[Screenshot] GetProcessIdFromWindow failed: error %lu", GetLastError());
		return m_lastProcessName;
	}

	HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);

	if (!process)
		return m_lastProcessName;

	wchar_t processPath[MAX_PATH] = {};
	DWORD pathSize = _countof(processPath);

	if (QueryFullProcessImageNameW(process, 0, processPath, &pathSize))
	{
		const wchar_t *name = wcsrchr(processPath, L'\\');
		name = name ? name + 1 : processPath;

		wcscpy_s(m_lastProcessName, LASTPROCESSNAMECOUNT, name);

		// Strip ".exe"
		wchar_t *extension = wcsrchr(m_lastProcessName, L'.');

		if (extension && _wcsicmp(extension, L".exe") == 0)
			*extension = L'\0';

		StripUnrealSuffix(m_lastProcessName);
	}

	CloseHandle(process);

	return m_lastProcessName;
}

void Screenshot::StripUnrealSuffix(wchar_t *name)
{
	static constexpr struct
	{
		const wchar_t *value;
		size_t length;
	} suffixes[] = {
		{L"-Win64-Shipping", _countof(L"-Win64-Shipping") - 1}, {L"-Win64-Test", _countof(L"-Win64-Test") - 1}, {L"-Win64-Development", _countof(L"-Win64-Development") - 1}, {L"-Win64-DebugGame", _countof(L"-Win64-DebugGame") - 1}, {L"-Win64-Debug", _countof(L"-Win64-Debug") - 1},
	};

	static constexpr size_t suffixCount = _countof(suffixes);
	static constexpr size_t minSuffixLength = 11; // "-Win64-Test"

	const size_t nameLength = wcslen(name);

	if (nameLength < minSuffixLength)
		return;

	for (size_t i = 0; i < suffixCount; ++i)
	{
		const auto &suffix = suffixes[i];

		if (nameLength >= suffix.length && _wcsicmp(name + nameLength - suffix.length, suffix.value) == 0)
		{
			name[nameLength - suffix.length] = L'\0';
			return;
		}
	}
}

bool Screenshot::BurstScreenshot(int n)
{
	if (IsPathEmpty())
	{
		LOG_ERROR("[Screenshot] path empty");
		return false;
	}

	if (n <= 0 || n > 3)
	{
		LOG_ERROR("[Screenshot] Invalid burst count: %d (allowed: 1-3)", n);
		return false;
	}

	const ULONGLONG now = GetTickCount64();

	if (m_lastScreenshotTime != 0 && (now - m_lastScreenshotTime) < MIN_INTERVAL_MS)
	{
		LOG_WARN("[Screenshot] Burst antispam triggered");
		return false;
	}

	HWND hwnd = GetForegroundWindow();

	if (!hwnd)
	{
		LOG_ERROR("[Screenshot] GetForegroundWindow failed");
		return false;
	}

	HDRInfo hdrInfo{};

	if (DetectHDR(hwnd, hdrInfo) && hdrInfo.active)
	{
		const bool success = HDRCapture(hwnd, hdrInfo);

		if (success)
			m_lastScreenshotTime = GetTickCount();

		return success;
	}

	m_lastScreenshotTime = now;

	constexpr auto CAPTURE_INTERVAL = std::chrono::milliseconds(100);
	const auto burstStart = std::chrono::steady_clock::now();

	std::thread(
		[this, n, burstStart, hwnd]()
		{
			for (int i = 0; i < n; ++i)
			{
				std::this_thread::sleep_until(burstStart + CAPTURE_INTERVAL * i);

				ScreenshotBuffer screenshot;

				if (!DXGICapture(screenshot, hwnd))
				{
					LOG_ERROR("[Screenshot] Burst capture failed at frame %d/%d", i + 1, n);
					continue;
				}

				const std::wstring fullPath = std::wstring(m_path) + screenshot.filename;

				std::thread(
					[this, screenshot = std::move(screenshot), fullPath, i]() mutable
					{
						if (!SaveBitmapToFile(screenshot, fullPath.c_str()))
						{
							LOG_ERROR("[Screenshot] Failed to save burst frame %d: %ls", i + 1, fullPath.c_str());
							return;
						}

						if (m_format == JPEG)
						{
							if (!EncodeFileAsJPEG(fullPath.c_str()))
								LOG_ERROR("[Screenshot] Failed to queue JPEG for burst frame %d: %ls", i + 1, fullPath.c_str());
						}
						else if (m_format == PNG)
						{
							if (!EncodeFileAsPNG(fullPath.c_str()))
								LOG_ERROR("[Screenshot] Failed to queue PNG for burst frame %d: %ls", i + 1, fullPath.c_str());
						}
					})
					.detach();
			}
		})
		.detach();

	return true;
}

bool Screenshot::GetScreenshot()
{
	if (IsPathEmpty())
	{
		LOG_ERROR("[Screenshot] path empty");
		return false;
	}

	const DWORD now = GetTickCount();

	if (m_lastScreenshotTime != 0 && (now - m_lastScreenshotTime) < MIN_INTERVAL_MS)
	{
		LOG_WARN("[Screenshot] Antispam triggered");
		return false;
	}

	HWND hwnd = GetForegroundWindow();

	if (!hwnd)
	{
		LOG_ERROR("[Screenshot] GetForegroundWindow failed");
		return false;
	}

	HDRInfo hdrInfo{};

	if (DetectHDR(hwnd, hdrInfo) && hdrInfo.active)
		return HDRCapture(hwnd, hdrInfo);

	ScreenshotBuffer screenshot;

	if (!DXGICapture(screenshot, hwnd))
	{
		LOG_ERROR("[Screenshot] CaptureScreenshot failed");
		return false;
	}

	const std::wstring fullPath = std::wstring(m_path) + screenshot.filename;

	if (!SaveBitmapToFile(screenshot, fullPath.c_str()))
	{
		LOG_ERROR("[Screenshot] SaveBitmapToFile failed: %ls", fullPath.c_str());
		return false;
	}

	if (m_format == JPEG)
	{
		if (!EncodeFileAsJPEG(fullPath.c_str()))
		{
			LOG_ERROR("[Screenshot] Failed to queue JPEG encoding: %ls", fullPath.c_str());
			return false;
		}
	}
	else if (m_format == PNG)
	{
		if (!EncodeFileAsPNG(fullPath.c_str()))
		{
			LOG_ERROR("[Screenshot] Failed to queue PNG encoding: %ls", fullPath.c_str());
			return false;
		}
	}

	m_lastScreenshotTime = GetTickCount64();

	return true;
}

std::wstring Screenshot::CreateFilenameForWindow(HWND hwnd, const wchar_t *tag)
{
	SYSTEMTIME st{};
	GetLocalTime(&st);

	if (hwnd == nullptr)
	{
		LOG_ERROR("[Screenshot] handle parameter is null");
		return L"unknown.bmp";
	}

	const wchar_t *processName = GetCachedProcessName(hwnd);

	wchar_t filename[512]{};

	if (tag != nullptr && tag[0] != L'\0')
		swprintf_s(filename, L"%ls_%ls_%04d-%02d-%02d_%02d-%02d-%02d-%03d.bmp", processName, tag, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
	else
		swprintf_s(filename, L"%ls_%04d-%02d-%02d_%02d-%02d-%02d-%03d.bmp", processName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	return filename;
}

bool Screenshot::InitializeDXGI(HWND hwnd, bool nativeHDR)
{
	if (!hwnd)
		return false;

	const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

	if (!monitor)
		return false;

	ComPtr<IDXGIFactory1> factory;

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] CreateDXGIFactory1 failed: 0x%08X", static_cast<unsigned>(hr));

		return false;
	}

	ComPtr<IDXGIAdapter1> selectedAdapter;
	ComPtr<IDXGIOutput> selectedOutput;

	for (UINT adapterIndex = 0;; ++adapterIndex)
	{
		ComPtr<IDXGIAdapter1> adapter;

		hr = factory->EnumAdapters1(adapterIndex, &adapter);

		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
			continue;

		DXGI_ADAPTER_DESC1 adapterDesc{};

		if (FAILED(adapter->GetDesc1(&adapterDesc)))
			continue;

		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		for (UINT outputIndex = 0;; ++outputIndex)
		{
			ComPtr<IDXGIOutput> output;

			hr = adapter->EnumOutputs(outputIndex, &output);

			if (hr == DXGI_ERROR_NOT_FOUND)
				break;

			if (FAILED(hr))
				continue;

			DXGI_OUTPUT_DESC outputDesc{};

			if (FAILED(output->GetDesc(&outputDesc)))
				continue;

			if (outputDesc.Monitor == monitor)
			{
				selectedAdapter = adapter;
				selectedOutput = output;
				m_dxgiOutputRect = outputDesc.DesktopCoordinates;
				break;
			}
		}

		if (selectedAdapter && selectedOutput)
			break;
	}

	if (!selectedAdapter || !selectedOutput)
	{
		LOG_ERROR("[Screenshot] Could not find DXGI output for monitor");
		return false;
	}

	D3D_FEATURE_LEVEL featureLevel{};

	const D3D_FEATURE_LEVEL featureLevels[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};

	hr = D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, featureLevels, _countof(featureLevels), D3D11_SDK_VERSION, &m_dxgiDevice, &featureLevel, &m_dxgiContext);

	if (FAILED(hr))
	{
		// D3D_FEATURE_LEVEL_11_1 isn't available on some systems.
		const D3D_FEATURE_LEVEL fallbackLevels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0};

		hr = D3D11CreateDevice(selectedAdapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, fallbackLevels, _countof(fallbackLevels), D3D11_SDK_VERSION, &m_dxgiDevice, &featureLevel, &m_dxgiContext);
	}

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] D3D11CreateDevice failed: 0x%08X", static_cast<unsigned>(hr));

		ShutdownDXGI();
		return false;
	}

	if (nativeHDR)
	{
		ComPtr<IDXGIOutput5> output5;

		hr = selectedOutput.As(&output5);

		if (FAILED(hr))
		{
			LOG_ERROR("[Screenshot] IDXGIOutput5 query failed for HDR capture: 0x%08X", static_cast<unsigned>(hr));
			ShutdownDXGI();
			return false;
		}

		// Request FP16 scRGB first. BGRA8 must remain in the list because
		// Microsoft requires it as the common desktop fallback.
		constexpr DXGI_FORMAT hdrFormats[] = {
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			DXGI_FORMAT_B8G8R8A8_UNORM,
		};

		hr = output5->DuplicateOutput1(m_dxgiDevice.Get(), 0, _countof(hdrFormats), hdrFormats, &m_dxgiDuplication);

		if (FAILED(hr))
		{
			LOG_ERROR("[Screenshot] DuplicateOutput1 HDR failed: 0x%08X", static_cast<unsigned>(hr));

			ShutdownDXGI();
			return false;
		}
	}
	else
	{
		ComPtr<IDXGIOutput1> output1;

		hr = selectedOutput.As(&output1);

		if (FAILED(hr))
		{
			LOG_ERROR("[Screenshot] IDXGIOutput1 query failed");
			ShutdownDXGI();
			return false;
		}

		hr = output1->DuplicateOutput(m_dxgiDevice.Get(), &m_dxgiDuplication);

		if (FAILED(hr))
		{
			LOG_ERROR("[Screenshot] DuplicateOutput failed: 0x%08X", static_cast<unsigned>(hr));

			ShutdownDXGI();
			return false;
		}
	}

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] DuplicateOutput failed: 0x%08X", static_cast<unsigned>(hr));

		ShutdownDXGI();
		return false;
	}

	m_dxgiMonitor = monitor;

	LOG_DEBUG("[Screenshot] DXGI capture initialized: %ld,%ld - %ld,%ld", m_dxgiOutputRect.left, m_dxgiOutputRect.top, m_dxgiOutputRect.right, m_dxgiOutputRect.bottom);

	return true;
}

void Screenshot::ShutdownDXGI()
{
	m_dxgiStagingTexture.Reset();
	m_dxgiDuplication.Reset();
	m_dxgiContext.Reset();
	m_dxgiDevice.Reset();

	m_dxgiMonitor = nullptr;
	m_dxgiOutputRect = {};
	m_dxgiStagingWidth = 0;
	m_dxgiStagingHeight = 0;
}

bool Screenshot::CreateDXGIStagingTexture(int width, int height)
{
	if (width <= 0 || height <= 0)
		return false;

	if (m_dxgiStagingTexture && m_dxgiStagingWidth == width && m_dxgiStagingHeight == height)
		return true;

	m_dxgiStagingTexture.Reset();

	D3D11_TEXTURE2D_DESC desc{};

	desc.Width = static_cast<UINT>(width);
	desc.Height = static_cast<UINT>(height);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_STAGING;
	desc.BindFlags = 0;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	desc.MiscFlags = 0;

	const HRESULT hr = m_dxgiDevice->CreateTexture2D(&desc, nullptr, &m_dxgiStagingTexture);

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] CreateTexture2D staging failed: 0x%08X", static_cast<unsigned>(hr));

		return false;
	}

	m_dxgiStagingWidth = width;
	m_dxgiStagingHeight = height;

	return true;
}

bool Screenshot::DXGICapture(ScreenshotBuffer &output, HWND hwnd)
{
	if (!hwnd)
		return false;

	RECT clientRect{};

	if (!GetClientRect(hwnd, &clientRect))
		return false;

	const int width = clientRect.right - clientRect.left;
	const int height = clientRect.bottom - clientRect.top;

	if (width <= 0 || height <= 0)
		return false;

	POINT screenPos{0, 0};

	if (!ClientToScreen(hwnd, &screenPos))
		return false;

	const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

	if (!monitor)
		return false;

	// Initialize once, or reinitialize if the game/browser moved
	// to another monitor.
	if (!m_dxgiDuplication || m_dxgiMonitor != monitor)
	{
		ShutdownDXGI();

		if (!InitializeDXGI(hwnd))
			return false;
	}

	const LONG desktopWidth = m_dxgiOutputRect.right - m_dxgiOutputRect.left;
	const LONG desktopHeight = m_dxgiOutputRect.bottom - m_dxgiOutputRect.top;

	// Position of the client rectangle inside the DXGI output
	// in desktop coordinates.
	const LONG desktopX = screenPos.x - m_dxgiOutputRect.left;
	const LONG desktopY = screenPos.y - m_dxgiOutputRect.top;

	// The window itself must be completely contained by the
	// selected output.
	if (desktopX < 0 || desktopY < 0 || desktopX + width > desktopWidth || desktopY + height > desktopHeight)
	{
		LOG_WARN("[Screenshot] Window spans outside its DXGI output; "
				 "skipping capture: client=%dx%d screen=%ld,%ld "
				 "output=%ld,%ld-%ld,%ld",
				 width, height, screenPos.x, screenPos.y, m_dxgiOutputRect.left, m_dxgiOutputRect.top, m_dxgiOutputRect.right, m_dxgiOutputRect.bottom);
		return false;
	}

	// Get the duplication description. This gives us the rotation
	// of the output.
	DXGI_OUTDUPL_DESC duplicationDesc{};

	m_dxgiDuplication->GetDesc(&duplicationDesc);

	DXGI_MODE_ROTATION rotation = duplicationDesc.Rotation;

	if (rotation == DXGI_MODE_ROTATION_UNSPECIFIED)
		rotation = DXGI_MODE_ROTATION_IDENTITY;

	DXGI_OUTDUPL_FRAME_INFO frameInfo{};
	ComPtr<IDXGIResource> desktopResource;

	HRESULT hr = S_OK;

	// The first AcquireNextFrame after a fresh (or freshly reinitialized)
	// duplication session frequently comes back with LastPresentTime == 0,
	// meaning DXGI has not actually signaled any real desktop update for
	// this region yet. On some driver/GPU combinations that first handoff
	// is stale/black. Discard frames like that and re-acquire, rather than
	// risk copying out invalid pixel data. Real, actively-presenting
	// windows (games in particular) resolve this on the very next
	// iteration, so the retry budget can stay small.
	constexpr int kMaxEmptyFrameRetries = 5;
	int emptyFrameRetries = 0;

	for (;;)
	{
		desktopResource.Reset();

		hr = m_dxgiDuplication->AcquireNextFrame(100, &frameInfo, &desktopResource);

		if (hr == DXGI_ERROR_WAIT_TIMEOUT)
		{
			LOG_WARN("[Screenshot] AcquireNextFrame timed out");
			return false;
		}

		if (hr == DXGI_ERROR_ACCESS_LOST)
		{
			LOG_DEBUG("[Screenshot] DXGI access lost, reinitializing");

			ShutdownDXGI();

			if (!InitializeDXGI(hwnd))
				return false;

			// Get the new duplication description after reinitialization.
			duplicationDesc = {};
			m_dxgiDuplication->GetDesc(&duplicationDesc);

			rotation = duplicationDesc.Rotation;

			if (rotation == DXGI_MODE_ROTATION_UNSPECIFIED)
				rotation = DXGI_MODE_ROTATION_IDENTITY;

			emptyFrameRetries = 0; // fresh session, reset the retry budget
			continue;
		}

		if (FAILED(hr))
		{
			LOG_ERROR("[Screenshot] AcquireNextFrame failed: 0x%08X", static_cast<unsigned>(hr));
			return false;
		}

		if (frameInfo.LastPresentTime.QuadPart == 0 && emptyFrameRetries < kMaxEmptyFrameRetries)
		{
			m_dxgiDuplication->ReleaseFrame();
			++emptyFrameRetries;
			continue;
		}

		break;
	}

	// From this point onward, we own an acquired frame and MUST
	// release it before returning.
	const auto failFrame = [&]() -> bool
	{
		m_dxgiDuplication->ReleaseFrame();
		return false;
	};

	ComPtr<ID3D11Texture2D> desktopTexture;

	hr = desktopResource.As(&desktopTexture);

	if (FAILED(hr))
		return failFrame();

	// Get the ACTUAL dimensions of the acquired DXGI surface.
	D3D11_TEXTURE2D_DESC frameDesc{};

	desktopTexture->GetDesc(&frameDesc);

	const LONG frameWidth = static_cast<LONG>(frameDesc.Width);
	const LONG frameHeight = static_cast<LONG>(frameDesc.Height);

	/*
	 * Normally, for a rotated portrait output:
	 *
	 *   DesktopCoordinates: 1080 x 1920
	 *   Acquired surface:   1920 x 1080
	 *
	 * Microsoft documents that the duplication surface is in the
	 * unrotated orientation and the desktop image is rotated inside it.
	 *
	 * Some older/driver combinations may already provide the image
	 * using the desktop orientation. If the dimensions tell us that
	 * this is happening, treat it as identity.
	 */
	if (rotation == DXGI_MODE_ROTATION_ROTATE90 || rotation == DXGI_MODE_ROTATION_ROTATE270)
	{
		if (frameWidth == desktopWidth && frameHeight == desktopHeight)
		{
			LOG_DEBUG("[Screenshot] DXGI frame already matches desktop "
					  "orientation (%ldx%ld); ignoring rotation=%d",
					  frameWidth, frameHeight, static_cast<int>(rotation));

			rotation = DXGI_MODE_ROTATION_IDENTITY;
		}
		else if (frameWidth != desktopHeight || frameHeight != desktopWidth)
		{
			LOG_ERROR("[Screenshot] Unexpected rotated DXGI frame size: "
					  "desktop=%ldx%ld frame=%ldx%ld rotation=%d",
					  desktopWidth, desktopHeight, frameWidth, frameHeight, static_cast<int>(rotation));

			return failFrame();
		}
	}
	else
	{
		if (frameWidth != desktopWidth || frameHeight != desktopHeight)
		{
			LOG_ERROR("[Screenshot] Unexpected DXGI frame size: "
					  "desktop=%ldx%ld frame=%ldx%ld rotation=%d",
					  desktopWidth, desktopHeight, frameWidth, frameHeight, static_cast<int>(rotation));

			return failFrame();
		}
	}

	/*
	 * The rectangle we want is expressed in the normal/rotated
	 * desktop coordinate system.
	 *
	 * DXGI gives us an unrotated surface, so calculate the
	 * corresponding source rectangle using the inverse rotation.
	 *
	 * This is equivalent to:
	 *
	 *   RotateRect(desktopRect, desktopSize, ReverseRotation(rotation))
	 *
	 * used by WebRTC's DXGI capturer.
	 */
	RECT sourceRect{};

	switch (rotation)
	{
	case DXGI_MODE_ROTATION_IDENTITY:
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	{
		sourceRect.left = desktopX;
		sourceRect.top = desktopY;
		sourceRect.right = desktopX + width;
		sourceRect.bottom = desktopY + height;
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE90:
	{
		// Reverse 90 => 270
		sourceRect.left = desktopY;
		sourceRect.top = desktopWidth - (desktopX + width);
		sourceRect.right = sourceRect.left + height;
		sourceRect.bottom = sourceRect.top + width;
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE180:
	{
		sourceRect.left = desktopWidth - (desktopX + width);
		sourceRect.top = desktopHeight - (desktopY + height);
		sourceRect.right = sourceRect.left + width;
		sourceRect.bottom = sourceRect.top + height;
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE270:
	{
		sourceRect.left = desktopHeight - (desktopY + height);
		sourceRect.top = desktopX;
		sourceRect.right = sourceRect.left + height;
		sourceRect.bottom = sourceRect.top + width;
		break;
	}

	default:
	{
		LOG_ERROR("[Screenshot] Unsupported DXGI rotation: %d", static_cast<int>(rotation));
		return failFrame();
	}
	}

	const LONG sourceWidth = sourceRect.right - sourceRect.left;
	const LONG sourceHeight = sourceRect.bottom - sourceRect.top;

	if (sourceWidth <= 0 || sourceHeight <= 0)
	{
		LOG_ERROR("[Screenshot] Invalid DXGI source rectangle");
		return failFrame();
	}

	// Validate against the ACTUAL acquired surface dimensions.
	if (sourceRect.left < 0 || sourceRect.top < 0 || sourceRect.right > frameWidth || sourceRect.bottom > frameHeight)
	{
		LOG_ERROR("[Screenshot] DXGI source rectangle outside frame: "
				  "source=%ld,%ld-%ld,%ld frame=%ldx%ld rotation=%d",
				  sourceRect.left, sourceRect.top, sourceRect.right, sourceRect.bottom, frameWidth, frameHeight, static_cast<int>(rotation));

		return failFrame();
	}

	/*
	 * Staging texture contains only the required source rectangle.
	 *
	 * For a 90/270 degree rotation, sourceWidth/sourceHeight are
	 * swapped relative to the final screenshot dimensions.
	 */
	if (!CreateDXGIStagingTexture(static_cast<int>(sourceWidth), static_cast<int>(sourceHeight)))
		return failFrame();

	D3D11_BOX sourceBox{};

	sourceBox.left = static_cast<UINT>(sourceRect.left);
	sourceBox.top = static_cast<UINT>(sourceRect.top);
	sourceBox.front = 0;
	sourceBox.right = static_cast<UINT>(sourceRect.right);
	sourceBox.bottom = static_cast<UINT>(sourceRect.bottom);
	sourceBox.back = 1;

	// GPU-side crop.
	m_dxgiContext->CopySubresourceRegion(m_dxgiStagingTexture.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &sourceBox);

	// The GPU copy has been queued. We no longer need the acquired
	// desktop duplication frame.
	hr = m_dxgiDuplication->ReleaseFrame();

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] ReleaseFrame failed: 0x%08X", static_cast<unsigned>(hr));
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};

	hr = m_dxgiContext->Map(m_dxgiStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] Map failed: 0x%08X", static_cast<unsigned>(hr));
		return false;
	}

	const size_t rowSize = static_cast<size_t>(width) * 4;
	const size_t imageSize = rowSize * static_cast<size_t>(height);

	output.pixels.resize(imageSize);

	const auto *base = static_cast<const uint8_t *>(mapped.pData);
	uint8_t *const dstBase = output.pixels.data();
	const size_t srcPitch = mapped.RowPitch;

	/*
	 * Rotate the cropped source rectangle back into normal
	 * desktop orientation.
	 *
	 * The DXGI desktop duplication format is BGRA8, i.e. 4 bytes
	 * per pixel. The rotation is constant for the whole capture,
	 * so it is switched on once here instead of once per pixel;
	 * each branch below uses the largest contiguous memcpy the
	 * access pattern allows.
	 */
	switch (rotation)
	{
	case DXGI_MODE_ROTATION_IDENTITY:
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	{
		// Source and destination rows are both contiguous runs of
		// pixels, so entire rows (or, if there's no padding, the
		// whole image) can be copied in one shot per iteration.
		if (srcPitch == rowSize)
		{
			std::memcpy(dstBase, base, imageSize);
		}
		else
		{
			for (int y = 0; y < height; ++y)
			{
				const uint8_t *src = base + static_cast<size_t>(y) * srcPitch;
				uint8_t *dst = dstBase + static_cast<size_t>(y) * rowSize;
				std::memcpy(dst, src, rowSize);
			}
		}
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE90:
	{
		// Clockwise 90°: dest (x, y) <- source (y, sourceHeight-1-x),
		// i.e. inverted: dest x = sourceHeight-1-sy, dest y = sx.
		// Walk the SOURCE row-by-row (contiguous reads) and scatter
		// each pixel into its destination column; this keeps reads
		// sequential instead of striding through RowPitch per pixel.
		for (int sy = 0; sy < static_cast<int>(sourceHeight); ++sy)
		{
			const uint8_t *srcRow = base + static_cast<size_t>(sy) * srcPitch;
			const int x = static_cast<int>(sourceHeight) - 1 - sy; // dest x, in [0, width)
			uint8_t *dstCol = dstBase + static_cast<size_t>(x) * 4;

			for (int sx = 0; sx < static_cast<int>(sourceWidth); ++sx)
			{
				const int y = sx; // dest y, in [0, height)
				std::memcpy(dstCol + static_cast<size_t>(y) * rowSize, srcRow + static_cast<size_t>(sx) * 4, 4);
			}
		}
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE180:
	{
		// 180°: each destination row is the corresponding source row
		// reversed. Copy pixel-by-pixel within the row (contiguous
		// read, strided write by -4 bytes), row by row (contiguous
		// blocks either way).
		for (int y = 0; y < height; ++y)
		{
			const int sy = static_cast<int>(sourceHeight) - 1 - y;
			const uint8_t *srcRow = base + static_cast<size_t>(sy) * srcPitch;
			uint8_t *dstRow = dstBase + static_cast<size_t>(y) * rowSize;

			for (int x = 0; x < width; ++x)
			{
				const int sx = static_cast<int>(sourceWidth) - 1 - x;
				std::memcpy(dstRow + static_cast<size_t>(x) * 4, srcRow + static_cast<size_t>(sx) * 4, 4);
			}
		}
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE270:
	{
		// Clockwise 270° (= CCW 90°): dest (x, y) <- source (sourceWidth-1-y, x),
		// i.e. inverted: dest x = sy, dest y = sourceWidth-1-sx.
		// As with ROTATE90, walk the SOURCE row-by-row for sequential
		// reads and scatter into destination columns.
		for (int sy = 0; sy < static_cast<int>(sourceHeight); ++sy)
		{
			const uint8_t *srcRow = base + static_cast<size_t>(sy) * srcPitch;
			const int x = sy; // dest x, in [0, width)

			for (int sx = 0; sx < static_cast<int>(sourceWidth); ++sx)
			{
				const int y = static_cast<int>(sourceWidth) - 1 - sx; // dest y, in [0, height)
				uint8_t *dst = dstBase + static_cast<size_t>(y) * rowSize + static_cast<size_t>(x) * 4;
				std::memcpy(dst, srcRow + static_cast<size_t>(sx) * 4, 4);
			}
		}
		break;
	}

	default:
		// Should already have been rejected above.
		break;
	}

	m_dxgiContext->Unmap(m_dxgiStagingTexture.Get(), 0);

	output.width = width;
	output.height = height;
	output.filename = CreateFilenameForWindow(hwnd);

	LOG_DEBUG("[Screenshot] DXGI capture successful: "
			  "client=%dx%d screen=%ld,%ld "
			  "output=%ld,%ld-%ld,%ld "
			  "frame=%ldx%ld rotation=%d "
			  "source=%ld,%ld-%ld,%ld",
			  width, height, screenPos.x, screenPos.y, m_dxgiOutputRect.left, m_dxgiOutputRect.top, m_dxgiOutputRect.right, m_dxgiOutputRect.bottom, frameWidth, frameHeight, static_cast<int>(rotation), sourceRect.left, sourceRect.top, sourceRect.right, sourceRect.bottom);

	LOG_DEBUG("[Screenshot] pixels: size=%zu capacity=%zu", output.pixels.size(), output.pixels.capacity());

	return true;
}

bool Screenshot::DetectHDR(HWND hwnd, HDRInfo &outInfo)
{
	if (!hwnd)
		return false;

	const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

	if (!monitor)
		return false;

	ComPtr<IDXGIFactory1> factory;

	HRESULT hr = CreateDXGIFactory1(IID_PPV_ARGS(&factory));

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] CreateDXGIFactory1 failed (HDR detect): 0x%08X", static_cast<unsigned>(hr));
		return false;
	}

	HDRInfo info{};

	for (UINT adapterIndex = 0;; ++adapterIndex)
	{
		ComPtr<IDXGIAdapter1> adapter;

		hr = factory->EnumAdapters1(adapterIndex, &adapter);

		if (hr == DXGI_ERROR_NOT_FOUND)
			break;

		if (FAILED(hr))
			continue;

		DXGI_ADAPTER_DESC1 adapterDesc{};

		if (FAILED(adapter->GetDesc1(&adapterDesc)))
			continue;

		if (adapterDesc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
			continue;

		for (UINT outputIndex = 0;; ++outputIndex)
		{
			ComPtr<IDXGIOutput> output;

			hr = adapter->EnumOutputs(outputIndex, &output);

			if (hr == DXGI_ERROR_NOT_FOUND)
				break;

			if (FAILED(hr))
				continue;

			DXGI_OUTPUT_DESC outputDesc{};

			if (FAILED(output->GetDesc(&outputDesc)))
				continue;

			if (outputDesc.Monitor != monitor)
				continue;

			ComPtr<IDXGIOutput6> output6;

			hr = output.As(&output6);

			if (FAILED(hr))
			{
				LOG_ERROR("[Screenshot] IDXGIOutput6 unavailable: 0x%08X", static_cast<unsigned>(hr));
				return false;
			}

			DXGI_OUTPUT_DESC1 desc1{};

			hr = output6->GetDesc1(&desc1);

			if (FAILED(hr))
			{
				LOG_ERROR("[Screenshot] IDXGIOutput6::GetDesc1 failed: 0x%08X", static_cast<unsigned>(hr));
				return false;
			}

			info.colorSpace = desc1.ColorSpace;
			info.minLuminance = desc1.MinLuminance;
			info.maxLuminance = desc1.MaxLuminance;
			info.maxFullFrameLuminance = desc1.MaxFullFrameLuminance;

			info.active = desc1.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;

			LOG_DEBUG("[Screenshot] HDR detection: monitor=%p "
					  "colorSpace=%d bitsPerColor=%u "
					  "min=%.2f max=%.2f maxFullFrame=%.2f active=%d",
					  monitor, static_cast<int>(desc1.ColorSpace), desc1.BitsPerColor, desc1.MinLuminance, desc1.MaxLuminance, desc1.MaxFullFrameLuminance, info.active);

			outInfo = info;
			return true;
		}
	}

	LOG_WARN("[Screenshot] Could not find DXGI output for monitor (HDR detect)");

	return false;
}

/**
 * Captures the HDR framebuffer and encodes it as a native HDR JXR file.
 *
 * Uses IDXGIOutput5::DuplicateOutput1 to preserve the Windows HDR
 * compositor's FP16/scRGB data and encodes it as JPEG XR without
 * reducing the HDR data to SDR.
 */
bool Screenshot::HDRCapture(HWND hwnd, const HDRInfo &info)
{
	if (!hwnd)
		return false;

	if (!info.active)
		return false;

	/*
	 * The normal DXGI path uses IDXGIOutput1::DuplicateOutput(),
	 * which gives us BGRA8. That is not sufficient for native HDR.
	 *
	 * Tear down the SDR duplication and recreate it using
	 * IDXGIOutput5::DuplicateOutput1() requesting FP16 scRGB.
	 */
	ShutdownDXGI();

	if (!InitializeDXGI(hwnd, true))
	{
		LOG_ERROR("[Screenshot] Failed to initialize native HDR DXGI capture");
		return false;
	}

	bool success = false;

	// Always leave the DXGI duplication in a clean state. The next
	// normal SDR capture will recreate the normal BGRA8 duplication.
	const auto cleanup = [&]() { ShutdownDXGI(); };

	RECT clientRect{};

	if (!GetClientRect(hwnd, &clientRect))
	{
		LOG_ERROR("[Screenshot] GetClientRect failed for HDR capture");
		cleanup();
		return false;
	}

	const int width = clientRect.right - clientRect.left;
	const int height = clientRect.bottom - clientRect.top;

	if (width <= 0 || height <= 0)
	{
		LOG_ERROR("[Screenshot] Invalid HDR capture dimensions: %dx%d", width, height);
		cleanup();
		return false;
	}

	POINT screenPos{0, 0};

	if (!ClientToScreen(hwnd, &screenPos))
	{
		LOG_ERROR("[Screenshot] ClientToScreen failed for HDR capture");
		cleanup();
		return false;
	}

	/*
	 * Verify that the monitor hasn't changed between DetectHDR()
	 * and the actual capture.
	 */
	const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);

	if (!monitor || monitor != m_dxgiMonitor)
	{
		LOG_ERROR("[Screenshot] HDR monitor changed during capture");
		cleanup();
		return false;
	}

	const LONG desktopWidth = m_dxgiOutputRect.right - m_dxgiOutputRect.left;
	const LONG desktopHeight = m_dxgiOutputRect.bottom - m_dxgiOutputRect.top;
	const LONG desktopX = screenPos.x - m_dxgiOutputRect.left;
	const LONG desktopY = screenPos.y - m_dxgiOutputRect.top;

	if (desktopX < 0 || desktopY < 0 || desktopX + width > desktopWidth || desktopY + height > desktopHeight)
	{
		LOG_WARN("[Screenshot] HDR window spans outside its DXGI output, skipping capture: client=%dx%d screen=%ld,%ld output=%ld,%ld-%ld,%ld", width, height, screenPos.x, screenPos.y, m_dxgiOutputRect.left, m_dxgiOutputRect.top, m_dxgiOutputRect.right, m_dxgiOutputRect.bottom);
		cleanup();
		return false;
	}

	DXGI_OUTDUPL_DESC duplicationDesc{};

	if (!m_dxgiDuplication)
	{
		LOG_ERROR("[Screenshot] HDR duplication object is null");
		cleanup();
		return false;
	}

	m_dxgiDuplication->GetDesc(&duplicationDesc);

	DXGI_MODE_ROTATION rotation = duplicationDesc.Rotation;

	if (rotation == DXGI_MODE_ROTATION_UNSPECIFIED)
		rotation = DXGI_MODE_ROTATION_IDENTITY;

	DXGI_OUTDUPL_FRAME_INFO frameInfo{};
	ComPtr<IDXGIResource> desktopResource;

	HRESULT hr = S_OK;

	/*
	 * Same protection against the first stale/empty frame that you
	 * already use in DXGICapture().
	 */
	constexpr int kMaxEmptyFrameRetries = 5;
	int emptyFrameRetries = 0;

	for (;;)
	{
		desktopResource.Reset();

		hr = m_dxgiDuplication->AcquireNextFrame(100, &frameInfo, &desktopResource);

		if (hr == DXGI_ERROR_WAIT_TIMEOUT)
		{
			LOG_WARN("[Screenshot] HDR AcquireNextFrame timed out");
			cleanup();
			return false;
		}

		if (hr == DXGI_ERROR_ACCESS_LOST)
		{
			LOG_ERROR("[Screenshot] HDR DXGI access lost");
			cleanup();
			return false;
		}

		if (FAILED(hr))
		{
			LOG_ERROR("[Screenshot] HDR AcquireNextFrame failed: 0x%08X", static_cast<unsigned>(hr));
			cleanup();
			return false;
		}

		if (frameInfo.LastPresentTime.QuadPart == 0 && emptyFrameRetries < kMaxEmptyFrameRetries)
		{
			m_dxgiDuplication->ReleaseFrame();
			++emptyFrameRetries;
			continue;
		}

		break;
	}

	/*
	 * From here until ReleaseFrame(), every failure must release
	 * the acquired frame.
	 */
	const auto failFrame = [&]() -> bool
	{
		m_dxgiDuplication->ReleaseFrame();
		cleanup();
		return false;
	};

	ComPtr<ID3D11Texture2D> desktopTexture;

	hr = desktopResource.As(&desktopTexture);

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] HDR resource is not a D3D11 texture: 0x%08X", static_cast<unsigned>(hr));
		return failFrame();
	}

	D3D11_TEXTURE2D_DESC frameDesc{};
	desktopTexture->GetDesc(&frameDesc);

	const LONG frameWidth = static_cast<LONG>(frameDesc.Width);
	const LONG frameHeight = static_cast<LONG>(frameDesc.Height);

	/*
	 * Native HDR must arrive as FP16 scRGB.
	 *
	 * Windows Advanced Color uses FP16/scRGB for its canonical HDR
	 * composition surface. 1.0 corresponds to SDR reference white
	 * (80 nits), while values > 1.0 preserve HDR highlights.
	 */
	if (frameDesc.Format != DXGI_FORMAT_R16G16B16A16_FLOAT)
	{
		LOG_ERROR("[Screenshot] HDR duplication returned unexpected format: %d (%ldx%ld)", static_cast<int>(frameDesc.Format), frameWidth, frameHeight);

		return failFrame();
	}

	/*
	 * For rotated displays DXGI's acquired surface is normally in
	 * the unrotated orientation.
	 *
	 * Some drivers can provide a surface already matching the
	 * desktop orientation, so use the same defensive handling as
	 * the normal capture path.
	 */
	if (rotation == DXGI_MODE_ROTATION_ROTATE90 || rotation == DXGI_MODE_ROTATION_ROTATE270)
	{
		if (frameWidth == desktopWidth && frameHeight == desktopHeight)
		{
			LOG_DEBUG("[Screenshot] HDR frame already matches desktop orientation (%ldx%ld); ignoring rotation=%d", frameWidth, frameHeight, static_cast<int>(rotation));
			rotation = DXGI_MODE_ROTATION_IDENTITY;
		}
		else if (frameWidth != desktopHeight || frameHeight != desktopWidth)
		{
			LOG_ERROR("[Screenshot] Unexpected HDR rotated frame size: desktop=%ldx%ld frame=%ldx%ld rotation=%d", desktopWidth, desktopHeight, frameWidth, frameHeight, static_cast<int>(rotation));
			return failFrame();
		}
	}
	else
	{
		if (frameWidth != desktopWidth || frameHeight != desktopHeight)
		{
			LOG_ERROR("[Screenshot] Unexpected HDR frame size: desktop=%ldx%ld frame=%ldx%ld rotation=%d", desktopWidth, desktopHeight, frameWidth, frameHeight, static_cast<int>(rotation));
			return failFrame();
		}
	}

	/*
	 * Calculate the source rectangle using the inverse rotation.
	 * This is the same coordinate mapping already used by your
	 * SDR DXGI capture.
	 */
	RECT sourceRect{};

	switch (rotation)
	{
	case DXGI_MODE_ROTATION_IDENTITY:
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	{
		sourceRect.left = desktopX;
		sourceRect.top = desktopY;
		sourceRect.right = desktopX + width;
		sourceRect.bottom = desktopY + height;
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE90:
	{
		sourceRect.left = desktopY;
		sourceRect.top = desktopWidth - (desktopX + width);
		sourceRect.right = sourceRect.left + height;
		sourceRect.bottom = sourceRect.top + width;
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE180:
	{
		sourceRect.left = desktopWidth - (desktopX + width);
		sourceRect.top = desktopHeight - (desktopY + height);
		sourceRect.right = sourceRect.left + width;
		sourceRect.bottom = sourceRect.top + height;
		break;
	}

	case DXGI_MODE_ROTATION_ROTATE270:
	{
		sourceRect.left = desktopHeight - (desktopY + height);
		sourceRect.top = desktopX;
		sourceRect.right = sourceRect.left + height;
		sourceRect.bottom = sourceRect.top + width;
		break;
	}

	default:
	{
		LOG_ERROR("[Screenshot] Unsupported HDR DXGI rotation: %d", static_cast<int>(rotation));
		return failFrame();
	}
	}

	const LONG sourceWidth = sourceRect.right - sourceRect.left;
	const LONG sourceHeight = sourceRect.bottom - sourceRect.top;

	if (sourceWidth <= 0 || sourceHeight <= 0)
	{
		LOG_ERROR("[Screenshot] Invalid HDR source rectangle");
		return failFrame();
	}

	if (sourceRect.left < 0 || sourceRect.top < 0 || sourceRect.right > frameWidth || sourceRect.bottom > frameHeight)
	{
		LOG_ERROR("[Screenshot] HDR source rectangle outside frame: source=%ld,%ld-%ld,%ld frame=%ldx%ld", sourceRect.left, sourceRect.top, sourceRect.right, sourceRect.bottom, frameWidth, frameHeight);
		return failFrame();
	}

	/*
	 * Native FP16 staging texture.
	 *
	 * Do NOT use m_dxgiStagingTexture here because that texture is
	 * created as BGRA8 by the SDR path.
	 */
	D3D11_TEXTURE2D_DESC stagingDesc{};

	stagingDesc.Width = static_cast<UINT>(sourceWidth);
	stagingDesc.Height = static_cast<UINT>(sourceHeight);

	stagingDesc.MipLevels = 1;
	stagingDesc.ArraySize = 1;
	stagingDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	stagingDesc.SampleDesc.Count = 1;
	stagingDesc.Usage = D3D11_USAGE_STAGING;
	stagingDesc.BindFlags = 0;
	stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	stagingDesc.MiscFlags = 0;

	ComPtr<ID3D11Texture2D> hdrStagingTexture;

	hr = m_dxgiDevice->CreateTexture2D(&stagingDesc, nullptr, &hdrStagingTexture);

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] HDR staging texture creation failed: 0x%08X", static_cast<unsigned>(hr));
		return failFrame();
	}

	D3D11_BOX sourceBox{};

	sourceBox.left = static_cast<UINT>(sourceRect.left);
	sourceBox.top = static_cast<UINT>(sourceRect.top);
	sourceBox.front = 0;
	sourceBox.right = static_cast<UINT>(sourceRect.right);
	sourceBox.bottom = static_cast<UINT>(sourceRect.bottom);
	sourceBox.back = 1;

	m_dxgiContext->CopySubresourceRegion(hdrStagingTexture.Get(), 0, 0, 0, 0, desktopTexture.Get(), 0, &sourceBox);

	/*
	 * The GPU copy has been queued. Release the duplication frame
	 * before mapping the staging texture.
	 */
	hr = m_dxgiDuplication->ReleaseFrame();

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] HDR ReleaseFrame failed: 0x%08X", static_cast<unsigned>(hr));
		cleanup();
		return false;
	}

	D3D11_MAPPED_SUBRESOURCE mapped{};

	hr = m_dxgiContext->Map(hdrStagingTexture.Get(), 0, D3D11_MAP_READ, 0, &mapped);

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] HDR staging Map failed: 0x%08X", static_cast<unsigned>(hr));
		cleanup();
		return false;
	}

	constexpr size_t bytesPerPixel = 8; // RGBA16F
	const size_t rowSize = static_cast<size_t>(width) * bytesPerPixel;
	const size_t imageSize = rowSize * static_cast<size_t>(height);
	std::vector<BYTE> pixels(imageSize);
	const auto *srcBase = static_cast<const uint8_t *>(mapped.pData);

	/*
	 * WIC's 64bppRGBAHalf format has the same channel ordering and
	 * 16-bit half-float representation as DXGI_FORMAT_R16G16B16A16_FLOAT:
	 *
	 *   R16
	 *   G16
	 *   B16
	 *   A16
	 *
	 * Therefore there is no color conversion here.
	 *
	 * We only crop and rotate.
	 */
	switch (rotation)
	{
	case DXGI_MODE_ROTATION_IDENTITY:
	case DXGI_MODE_ROTATION_UNSPECIFIED:
	{
		if (mapped.RowPitch == rowSize)
			std::memcpy(pixels.data(), srcBase, imageSize);
		else
			for (int y = 0; y < height; ++y)
			{
				const uint8_t *src = srcBase + static_cast<size_t>(y) * mapped.RowPitch;
				uint8_t *dst = pixels.data() + static_cast<size_t>(y) * rowSize;
				std::memcpy(dst, src, rowSize);
			}

		break;
	}

	case DXGI_MODE_ROTATION_ROTATE90:
	{
		for (int sy = 0; sy < static_cast<int>(sourceHeight); ++sy)
		{
			const uint8_t *srcRow = srcBase + static_cast<size_t>(sy) * mapped.RowPitch;
			const int x = static_cast<int>(sourceHeight) - 1 - sy;
			uint8_t *dstCol = pixels.data() + static_cast<size_t>(x) * bytesPerPixel;

			for (int sx = 0; sx < static_cast<int>(sourceWidth); ++sx)
			{
				const int y = sx;
				std::memcpy(dstCol + static_cast<size_t>(y) * rowSize, srcRow + static_cast<size_t>(sx) * bytesPerPixel, bytesPerPixel);
			}
		}

		break;
	}

	case DXGI_MODE_ROTATION_ROTATE180:
	{
		for (int y = 0; y < height; ++y)
		{
			const int sy = static_cast<int>(sourceHeight) - 1 - y;
			const uint8_t *srcRow = srcBase + static_cast<size_t>(sy) * mapped.RowPitch;

			uint8_t *dstRow = pixels.data() + static_cast<size_t>(y) * rowSize;

			for (int x = 0; x < width; ++x)
			{
				const int sx = static_cast<int>(sourceWidth) - 1 - x;
				std::memcpy(dstRow + static_cast<size_t>(x) * bytesPerPixel, srcRow + static_cast<size_t>(sx) * bytesPerPixel, bytesPerPixel);
			}
		}

		break;
	}

	case DXGI_MODE_ROTATION_ROTATE270:
	{
		for (int sy = 0; sy < static_cast<int>(sourceHeight); ++sy)
		{
			const uint8_t *srcRow = srcBase + static_cast<size_t>(sy) * mapped.RowPitch;
			const int x = sy;

			for (int sx = 0; sx < static_cast<int>(sourceWidth); ++sx)
			{
				const int y = static_cast<int>(sourceWidth) - 1 - sx;
				uint8_t *dst = pixels.data() + static_cast<size_t>(y) * rowSize + static_cast<size_t>(x) * bytesPerPixel;
				std::memcpy(dst, srcRow + static_cast<size_t>(sx) * bytesPerPixel, bytesPerPixel);
			}
		}

		break;
	}

	default:
		break;
	}

	m_dxgiContext->Unmap(hdrStagingTexture.Get(), 0);

	// ---------------------------------------------------------------
	// HDR → SDR tonemap path  (same file handling as normal SDR)
	// ---------------------------------------------------------------
	if (m_hdrOutputMode != HDR_NATIVE)
	{
		ScreenshotBuffer sdr;

		const size_t hdrRowPitch = static_cast<size_t>(width) * 8; // tightly packed

		// if (!TonemapHDRToSDR(pixels.data(), width, height, hdrRowPitch, info, sdr, m_hdrOutputMode))
		if (!TonemapHDRToSDR2(pixels.data(), width, height, hdrRowPitch, info, sdr, m_hdrOutputMode))
		{
			LOG_ERROR("[Screenshot] HDR → SDR tonemap failed");
			cleanup();
			return false;
		}

		// Same naming as the normal path
		sdr.filename = CreateFilenameForWindow(hwnd);

		const std::wstring fullPath = std::wstring(m_path) + sdr.filename;

		if (!SaveBitmapToFile(sdr, fullPath.c_str()))
		{
			LOG_ERROR("[Screenshot] SaveBitmapToFile (tonemapped) failed: %ls", fullPath.c_str());
			cleanup();
			return false;
		}

		if (m_format == JPEG)
		{
			if (!EncodeFileAsJPEG(fullPath.c_str()))
			{
				LOG_ERROR("[Screenshot] Failed to queue JPEG encoding (tonemapped): %ls", fullPath.c_str());
				cleanup();
				return false;
			}
		}
		else if (m_format == PNG)
		{
			if (!EncodeFileAsPNG(fullPath.c_str()))
			{
				LOG_ERROR("[Screenshot] Failed to queue PNG encoding (tonemapped): %ls", fullPath.c_str());
				cleanup();
				return false;
			}
		}
		// BMP is already done

		LOG_INFO("[Screenshot] HDR tonemapped → SDR saved: %ls", fullPath.c_str());
		cleanup();
		return true;
	}

	/*
	 * Build the .jxr filename.
	 */
	const wchar_t *processName = GetCachedProcessName(hwnd);

	SYSTEMTIME st{};
	GetLocalTime(&st);

	wchar_t filename[512]{};

	swprintf_s(filename, L"%ls_HDR_%04d-%02d-%02d_%02d-%02d-%02d-%03d.jxr", processName, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

	const std::wstring fullPath = std::wstring(m_path) + filename;

	/*
	 * WIC encoding.
	 *
	 * JPEG XR supports 64bppRGBAHalf natively, so the FP16 scRGB
	 * samples can be written without reducing them to 8/10 bits.
	 */
	HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	const bool uninitializeCOM = SUCCEEDED(comHr);

	/*
	 * RPC_E_CHANGED_MODE means this thread is already initialized
	 * with a different apartment model. COM is still usable in
	 * that case, so don't treat it as fatal.
	 */
	if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
	{
		LOG_ERROR("[Screenshot] CoInitializeEx failed for WIC: 0x%08X", static_cast<unsigned>(comHr));

		cleanup();
		return false;
	}

	ComPtr<IWICImagingFactory> factory;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	ComPtr<IPropertyBag2> encoderOptions;

	hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] WIC factory creation failed: 0x%08X", static_cast<unsigned>(hr));

		if (uninitializeCOM)
			CoUninitialize();

		cleanup();
		return false;
	}

	hr = factory->CreateStream(&stream);

	if (SUCCEEDED(hr))
		hr = stream->InitializeFromFilename(fullPath.c_str(), GENERIC_WRITE);

	if (SUCCEEDED(hr))
		hr = factory->CreateEncoder(GUID_ContainerFormatWmp, nullptr, &encoder);

	if (SUCCEEDED(hr))
		hr = encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache);

	if (SUCCEEDED(hr))
		hr = encoder->CreateNewFrame(&frame, &encoderOptions);

	if (SUCCEEDED(hr))
		hr = frame->Initialize(encoderOptions.Get());

	if (SUCCEEDED(hr))
		hr = frame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height));

	if (SUCCEEDED(hr))
		hr = frame->SetResolution(96.0, 96.0);

	WICPixelFormatGUID pixelFormat = GUID_WICPixelFormat64bppRGBAHalf;

	if (SUCCEEDED(hr))
		hr = frame->SetPixelFormat(&pixelFormat);

	/*
	 * Do not allow WIC to silently convert the FP16 data to an
	 * integer format. That would defeat native HDR capture.
	 */
	if (SUCCEEDED(hr) && pixelFormat != GUID_WICPixelFormat64bppRGBAHalf)
	{
		LOG_ERROR("[Screenshot] JPEG XR encoder did not accept 64bppRGBAHalf");
		hr = WINCODEC_ERR_UNSUPPORTEDPIXELFORMAT;
	}

	if (SUCCEEDED(hr))
		hr = frame->WritePixels(static_cast<UINT>(height), static_cast<UINT>(rowSize), static_cast<UINT>(imageSize), pixels.data());

	if (SUCCEEDED(hr))
		hr = frame->Commit();

	if (SUCCEEDED(hr))
		hr = encoder->Commit();

	if (FAILED(hr))
	{
		LOG_ERROR("[Screenshot] JPEG XR HDR encoding failed: 0x%08X (%ls)", static_cast<unsigned>(hr), fullPath.c_str());

		// Do not leave a corrupt .jxr behind.
		DeleteFileW(fullPath.c_str());

		if (uninitializeCOM)
			CoUninitialize();

		cleanup();
		return false;
	}

	if (uninitializeCOM)
		CoUninitialize();

	LOG_INFO("[Screenshot] Native HDR JPEG XR saved: %ls (scRGB FP16, %dx%d, colorSpace=%d, maxLuminance=%.1f nits)", fullPath.c_str(), width, height, static_cast<int>(info.colorSpace), info.maxLuminance);

	cleanup();

	success = true;
	return success;
}

void Screenshot::AutoClean()
{
	LOG_DEBUG("[Screenshot] AutoClean: releasing all DXGI resources");

	m_dxgiStagingTexture.Reset();
	m_dxgiStagingWidth = 0;
	m_dxgiStagingHeight = 0;

	m_dxgiDuplication.Reset();
	m_dxgiContext.Reset();
	m_dxgiDevice.Reset();

	m_dxgiMonitor = nullptr;
}

void Screenshot::Update()
{
	// LOG_TRACE("Screenshot::Update");

	if (m_lastScreenshotTime == 0)
		return;

	if (GetTickCount64() - m_lastScreenshotTime >= AUTO_CLEAN_INTERVAL_MS)
	{
		AutoClean();
		m_lastScreenshotTime = 0;
	}
}

/**
 * Tonemap native HDR (R16G16B16A16_FLOAT scRGB) → SDR BGRA8.
 *
 * - Uses the display's max luminance from HDRInfo when available.
 * - Falls back to a reasonable default (1000 nits) if the value is missing/zero.
 * - Simple ACES-like curve + mild desaturation of highlights.
 * - Pure CPU, no extra GPU resources required.
 *
 * Returns true on success. On failure output is left unmodified.
 */
bool Screenshot::TonemapHDRToSDR2(const uint8_t *hdrPixels, int width, int height, size_t hdrRowPitch, const HDRInfo &info, ScreenshotBuffer &sdrOutput, HDROutputMode mode)
{
	if (!hdrPixels || width <= 0 || height <= 0)
		return false;

	const float peakNits = (info.maxLuminance > 1.0f) ? info.maxLuminance : 1000.0f;
	const size_t sdrRowSize = static_cast<size_t>(width) * 4;
	const size_t sdrSize = sdrRowSize * static_cast<size_t>(height);

	sdrOutput.pixels.resize(sdrSize);
	sdrOutput.width = width;
	sdrOutput.height = height;

	uint8_t *dst = sdrOutput.pixels.data();

	const float whitePoint = 1.5f;
	const float whitePoint2 = 2.2f; // higher threshold for bright scene with clouds
	const float exposureScale = 1.0f;

	// Fast + accurate half -> float
	auto halfToFloat = [](uint16_t h) -> float
	{
		union
		{
			uint32_t u;
			float f;
		} v;
		const uint32_t sign = (h & 0x8000u) << 16;
		const uint32_t exp = (h >> 10) & 0x1Fu;
		const uint32_t mant = h & 0x3FFu;

		if (exp == 0)
		{
			if (mant == 0)
				return sign ? -0.0f : 0.0f;
			const float f = std::ldexp(static_cast<float>(mant), -24);
			return sign ? -f : f;
		}
		if (exp == 31)
			return sign ? -1e5f : 1e5f;

		v.u = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
		return v.f;
	};

	// --- Pass 1: measure scene brightness (arithmetic mean, converted to nits) ---
	// Assumes buffer is scRGB linear (1.0 = 80 nits), standard for R16G16B16A16_FLOAT HDR capture.
	const float scRGBToNits = 80.0f;
	const int sampleStride = 4;

	double sum = 0.0;
	size_t sampleCount = 0;

	for (int y = 0; y < height; y += sampleStride)
	{
		const uint8_t *srcRow = hdrPixels + static_cast<size_t>(y) * hdrRowPitch;
		for (int x = 0; x < width; x += sampleStride)
		{
			const uint16_t *p = reinterpret_cast<const uint16_t *>(srcRow + x * 8);
			const float r = halfToFloat(p[0]);
			const float g = halfToFloat(p[1]);
			const float b = halfToFloat(p[2]);
			const float luma = (0.2126f * r + 0.7152f * g + 0.0722f * b) * scRGBToNits;
			sum += static_cast<double>(max(luma, 0.0f));
			++sampleCount;
		}
	}

	const float avgLumNits = (sampleCount > 0) ? static_cast<float>(sum / static_cast<double>(sampleCount)) : 0.0f;

	const float darkAnchorNits = 5.0f;	   // ~ your dark scene (4.7)
	const float brightAnchorNits = 140.0f; // ~ your bright scene (140)

	const float sceneEV = std::log2(max(avgLumNits, 0.01f));
	const float darkEV = std::log2(darkAnchorNits);
	const float brightEV = std::log2(brightAnchorNits);

	float t = std::clamp((sceneEV - darkEV) / (brightEV - darkEV), 0.0f, 1.0f);

	// Bias curve: keeps mid-tone scenes closer to MAX, only pulls toward
	// MIN  as the scene approaches the bright anchor. Tune curvePower:
	//   1.0 = linear (current behavior)
	//   2-4 = MAX holds longer, natural kicks in later
	const float curvePower = 3.0f;
	t = std::pow(t, curvePower);

	float paperWhiteMin; // value used to avoid clipping in bright scenes
	float paperWhiteMax; // value used to avoid black crush in dark scenes

	switch (mode)
	{
	case HDR_TONEMAP_BRIGHT:
		paperWhiteMin = 140.0f;
		paperWhiteMax = 300.0f;
		break;
	case HDR_TONEMAP_MID:
		paperWhiteMin = 110.0f;
		paperWhiteMax = 250.0f;
		break;
	case HDR_TONEMAP_DARK:
		paperWhiteMin = 80.0f;
		paperWhiteMax = 200.0f;
		break;
	default:
		paperWhiteMin = 110.0f;
		paperWhiteMax = 250.0f;
	};

	const float paperWhite = std::lerp(paperWhiteMax, paperWhiteMin, t); // dynamically adjust paper white based on the power curve

	LOG_DEBUG("[HDR] paperWhite=%.0f, avgLum=%.1f", paperWhite, avgLumNits);

	const float exposure = paperWhite / peakNits;
	const float finalExposure = exposure * exposureScale;
	const float wp = paperWhite == paperWhiteMin ? whitePoint2 : whitePoint;
	const float whitePointSq = wp * wp;

	auto tonemap = [whitePointSq](float x) -> float
	{
		x = x * (1.0f + x / whitePointSq) / (1.0f + x);
		return std::clamp(x, 0.0f, 1.0f);
	};

	auto toSRGB = [](float v) -> uint8_t
	{
		v = std::clamp(v, 0.0f, 1.0f);
		if (v <= 0.0031308f)
			v *= 12.92f;
		else
			v = 1.055f * std::pow(v, 1.0f / 2.4f) - 0.055f;
		return static_cast<uint8_t>(std::round(v * 255.0f));
	};

// --- Pass 2: full tonemap, parallel over rows ---
#pragma omp parallel for schedule(dynamic)
	for (int y = 0; y < height; ++y)
	{
		const uint8_t *srcRow = hdrPixels + static_cast<size_t>(y) * hdrRowPitch;
		uint8_t *dstRow = dst + static_cast<size_t>(y) * sdrRowSize;

		for (int x = 0; x < width; ++x)
		{
			const uint16_t *p = reinterpret_cast<const uint16_t *>(srcRow + x * 8);

			float r = halfToFloat(p[0]) * finalExposure;
			float g = halfToFloat(p[1]) * finalExposure;
			float b = halfToFloat(p[2]) * finalExposure;

			r = tonemap(r);
			g = tonemap(g);
			b = tonemap(b);

			dstRow[x * 4 + 0] = toSRGB(b);
			dstRow[x * 4 + 1] = toSRGB(g);
			dstRow[x * 4 + 2] = toSRGB(r);
			dstRow[x * 4 + 3] = 255;
		}
	}

	return true;
}