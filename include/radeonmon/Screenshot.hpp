#pragma once

#include "radeonmon/JpegEncoder.hpp"
#include "radeonmon/PngEncoder.hpp"

#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>
#include <wincodec.h>

using Microsoft::WRL::ComPtr;

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "windowscodecs.lib")

class Screenshot
{
  public:
	enum Format
	{
		BMP,
		JPEG,
		PNG
	};

	enum HDROutputMode : uint8_t
	{
		HDR_NATIVE, // write the full HDR data out (e.g. fp16 -> JPEG XR)

		HDR_TONEMAP_BRIGHT, // use high paper white thresholds to keep the image bright, tradeoff: some highlights might clip
		HDR_TONEMAP_MID,	// use in-between paper white
		HDR_TONEMAP_DARK,	// use low paper white to avoid highlight clipping, tradeoff: the overall image is dim
	};

	Format m_format = BMP;
	HDROutputMode m_hdrOutputMode = HDR_NATIVE; // HDR is the default behavior

	static constexpr DWORD MIN_INTERVAL_MS = 500; // Minimum 500 ms between shots (2 per second max)

	Screenshot();
	~Screenshot();
	bool GetScreenshot();
	bool BurstScreenshot(int n = 2);
	inline const wchar_t *GetPath() const { return m_path; }
	inline bool IsPathEmpty() const { return m_path[0] == '\0'; }
	bool SetPath(const wchar_t *newPath);
	void Update(); // invoke autoclean

  private:
	struct ScreenshotBuffer
	{
		std::vector<BYTE> pixels;
		int width = 0;
		int height = 0;
		std::wstring filename;
	};

	struct HDRInfo
	{
		bool active = false;
		DXGI_COLOR_SPACE_TYPE colorSpace = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709;
		float minLuminance = 0.0f;
		float maxLuminance = 0.0f;
		float maxFullFrameLuminance = 0.0f;
	};

	wchar_t m_path[MAX_PATH] = {};
	ULONGLONG m_lastScreenshotTime = 0;
	JpegEncoder m_jpegEncoder;
	PngEncoder m_pngEncoder;

	// fallback process retriever using win32u.dll
	using NtUserQueryWindow_t = ULONG_PTR(WINAPI *)(HWND, ULONG);
	NtUserQueryWindow_t m_NtUserQueryWindow = nullptr;

	// cache
	HWND m_lastHwnd = nullptr;
	wchar_t m_lastProcessName[MAX_PATH] = L"unknown";
	static constexpr size_t LASTPROCESSNAMECOUNT = _countof(m_lastProcessName);

	static constexpr ULONGLONG BURST_INTERVAL_MS = 1000;
	DWORD m_lastBurstTime;

	bool SaveBitmapToFile(const ScreenshotBuffer &buffer, const wchar_t *filePath);
	bool EncodeFileAsJPEG(const wchar_t *filePath); // encode and deletes the raw file;
	bool EncodeFileAsPNG(const wchar_t *filePath);	// encode and deletes the raw file;
	DWORD GetProcessIdFromWindow(HWND hwnd);		// New helper to find PID with fallback solution
	const wchar_t *GetCachedProcessName(HWND hwnd);
	void StripUnrealSuffix(wchar_t *name);
	std::wstring CreateFilenameForWindow(HWND hwnd, const wchar_t *tag = nullptr);

	bool InitializeDXGI(HWND hwnd, bool nativeHDR = false);
	void ShutdownDXGI();
	bool CreateDXGIStagingTexture(int width, int height);
	bool DXGICapture(ScreenshotBuffer &output, HWND hwnd);

	bool DetectHDR(HWND hwnd, HDRInfo &outInfo);
	bool HDRCapture(HWND hwnd, const HDRInfo &info);
	// bool TonemapHDRToSDR(const uint8_t *hdrPixels, int width, int height, size_t hdrRowPitch, const HDRInfo &info, ScreenshotBuffer &sdrOutput, HDROutputMode);
	bool TonemapHDRToSDR2(const uint8_t *hdrPixels, int width, int height, size_t hdrRowPitch, const HDRInfo &info, ScreenshotBuffer &sdrOutput, HDROutputMode);

	ComPtr<ID3D11Device> m_dxgiDevice;
	ComPtr<ID3D11DeviceContext> m_dxgiContext;
	ComPtr<IDXGIOutputDuplication> m_dxgiDuplication;
	ComPtr<ID3D11Texture2D> m_dxgiStagingTexture;

	HMONITOR m_dxgiMonitor = nullptr;

	RECT m_dxgiOutputRect{};

	int m_dxgiStagingWidth = 0;
	int m_dxgiStagingHeight = 0;

	// HDR detection cache - separate from m_dxgiMonitor since HDR status
	// can be queried/invalidated independently of duplication setup.
	HMONITOR m_hdrMonitor = nullptr;
	HDRInfo m_hdrInfo{};

	// Autoclean
	static constexpr ULONGLONG AUTO_CLEAN_INTERVAL_MS = 5 * 60 * 1000; // 5min
	void AutoClean();
};