#pragma once

#define WIN32_LEAN_AND_MEAN

#include <winsock2.h>
#include <ws2tcpip.h>
#include <Windows.h>

#include <cstdint>
#include <string>
#include <cmath>
#include <optional>
#include <vector>
#include <regex>
#include <cstring>

#include <ifdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include "radeonmon/logging.hpp"
#include "radeonmon/constants.hpp"

#include "../../third_party/AMD/ADLX-1.5/SDK/Include/ADLXDefines.h"
#include "../../third_party/AMD/ADLX-1.5/SDK/Include/ADLXStructures.h"

extern UINT g_dpi;

enum class PropertyType
{
	Text,
	Separator
};

enum TextLevel
{
	Neutral = 0,
	Warning = 1,
	Alert = 2,
	Count = 3
};

struct PropertyItem
{
	std::wstring label;
	int value;
	int value2;
	WCHAR textValue[32];
	RECT labelRc;
	RECT valueRc;
	PropertyType type = PropertyType::Text;
	bool dirty = true;
	TextLevel textLevel = TextLevel::Neutral;
	bool repaintLabel = true;
	int textX;
	int textY;
	UINT textLength = 0;
	RECT textLabelRc;
	RECT textRc;

	inline void SetLabel(const wchar_t *txt) { label = txt ? txt : L""; }

	void SetValue(const char *src)
	{
		if (!src)
		{
			textValue[0] = L'\0';
			dirty = true;
			return;
		}

		MultiByteToWideChar(CP_UTF8, 0, src, -1, textValue, _countof(textValue));

		dirty = true;
	}

	void SetValue(const wchar_t *src)
	{
		if (src)
		{
			wcsncpy_s(textValue, _countof(textValue), src, _TRUNCATE);
		}
		else
		{
			textValue[0] = L'\0';
		}

		dirty = true;
	}

	void SetValue(HWND hwnd, HFONT font, const wchar_t *src)
	{
		if (src)
		{
			wcsncpy_s(textValue, _countof(textValue), src, _TRUNCATE);
		}
		else
		{
			textValue[0] = L'\0';
		}

		HDC hdc = GetDC(hwnd);
		HFONT oldFont = (HFONT)SelectObject(hdc, font);
		UpdateTextLayout(hdc);
		SelectObject(hdc, oldFont);
		ReleaseDC(hwnd, hdc);

		dirty = true;
	}

	void UpdateTextLayout(HDC hdc)
	{
		SIZE size{};

		if (hdc && textValue[0] != L'\0')
		{
			GetTextExtentPoint32W(hdc, textValue, static_cast<int>(wcslen(textValue)), &size);
		}

		// Pixel width of the text.
		textLength = static_cast<UINT>(wcslen(textValue));

		// Horizontally centered inside valueRc.
		textX = valueRc.left + ((valueRc.right - valueRc.left) - size.cx) / 2;

		// Vertically centered inside valueRc.
		textY = valueRc.top + ((valueRc.bottom - valueRc.top) - size.cy) / 2;
	}

	void DrawTextValue(HDC hdc, COLORREF color, HFONT font)
	{
		HFONT oldFont = (HFONT)SelectObject(hdc, font);
		COLORREF oldColor = SetTextColor(hdc, color);
		int oldBkMode = SetBkMode(hdc, TRANSPARENT);

		DrawTextW(hdc, textValue, -1, &valueRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

		SetBkMode(hdc, oldBkMode);
		SetTextColor(hdc, oldColor);
		SelectObject(hdc, oldFont);
	}

	void ClearLabelRC(HDC hdc, COLORREF color)
	{
		HBRUSH brush = CreateSolidBrush(color);
		FillRect(hdc, &valueRc, brush);
		DeleteObject(brush);
	}

	void ClearValueRC(HDC hdc, COLORREF color)
	{
		HBRUSH brush = CreateSolidBrush(color);
		FillRect(hdc, &valueRc, brush);
		DeleteObject(brush);
	}

	void ClearRC(HDC hdc, COLORREF color)
	{
		HBRUSH brush = CreateSolidBrush(color);

		RECT rc = valueRc;
		UnionRect(&rc, &rc, &labelRc); // merges into 1 rect including the gap

		FillRect(hdc, &rc, brush);

		DeleteObject(brush);
	}

	RECT GetUnionRC() const
	{
		RECT result;
		UnionRect(&result, &labelRc, &valueRc);
		return result;
	}
};

struct GdiBackBuffer
{
	HDC memDC = nullptr;
	HBITMAP bitmap = nullptr;
	HBITMAP oldBitmap = nullptr;
	HBRUSH bgBrush = nullptr;

	int width = 0;
	int height = 0;

	void Log() { LOG_DEBUG("Back buffer= %dx%d", width, height); }

	void Create(HDC referenceDC, int w, int h, COLORREF bgColor)
	{
		Destroy(); // safe re-init

		width = w;
		height = h;

		LOG_DEBUG("Creating back buffer: %dx%d", w, h);

		memDC = CreateCompatibleDC(referenceDC);

		bitmap = CreateCompatibleBitmap(referenceDC, w, h);
		oldBitmap = (HBITMAP)SelectObject(memDC, bitmap);

		bgBrush = CreateSolidBrush(bgColor);

		RECT rc{0, 0, w, h};
		FillRect(memDC, &rc, bgBrush);
	}

	void Destroy()
	{
		if (!memDC)
			return;

		if (memDC)
		{
			if (oldBitmap)
				SelectObject(memDC, oldBitmap);

			if (bitmap)
			{
				DeleteObject(bitmap);
				bitmap = nullptr;
			}

			DeleteDC(memDC);
			memDC = nullptr;
		}

		if (bgBrush)
		{
			DeleteObject(bgBrush);
			bgBrush = nullptr;
		}

		oldBitmap = nullptr;
		width = height = 0;
	}

	~GdiBackBuffer() { Destroy(); }
};

enum AdlxStates
{
	Error = -1,
	NotSupported = -2
};

enum RyzenCpuState
{
	AdminRequired = -3,
	SdkRequired = -4,
};

enum class RyzenMetricState
{
	NotSupported = -1,
	Error = 0,
};

struct RyzenCoreMetrics
{
	double dTemperature = 0.0;
	double dUsage = 0.0;
	double dEffectiveFreq = 0.0;
	double dCurrentFreq = 0.0;

#ifdef _DEBUG
	void Log(size_t index) const { LOG_DEBUG("[%zu] %.2f C, %.2f%%, %.2f MHz / %.2f MHz", index, dTemperature, dUsage, dEffectiveFreq, dCurrentFreq); }
#endif
};

struct RyzenMetrics
{
	std::vector<RyzenCoreMetrics> cores;
	char name[256] = "";
	char shortName[32] = "";
	double dTemperature = 0.0;
	double dPower = 0.0;
	double usage = 0.0;

	int GetCoreCount() { return static_cast<int>(cores.size()); }

	void SetShortName(const char *value, int coreCount)
	{
		if (!value)
		{
			shortName[0] = '\0';
			return;
		}

		std::string result(value);

		// Remove AMD prefix
		result = std::regex_replace(result, std::regex(R"(^\s*AMD\s+)", std::regex::icase), "");

		// Remove "N-Core Processor" if present
		result = std::regex_replace(result, std::regex(R"(\s+\d+-Core\s+Processor$)", std::regex::icase), "");

		// Remove Radeon / graphics suffix
		result = std::regex_replace(result, std::regex(R"(\s+w\/.*$)", std::regex::icase), "");

		// Append core count
		if (coreCount > 0)
			result += " (" + std::to_string(coreCount) + "C)";

		// Normalize whitespace
		result = std::regex_replace(result, std::regex(R"(\s+)"), " ");

		// Trim
		if (!result.empty() && result.front() == ' ')
			result.erase(0, 1);

		if (!result.empty() && result.back() == ' ')
			result.pop_back();

		// Copy safely
		strncpy_s(shortName, sizeof(shortName), result.c_str(), _TRUNCATE);
	}

	int BuildJson(char *buffer, int bufferSize) const
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
			if (p >= end)
				return;

			char tmp[32];
			char *t = tmp + sizeof(tmp) - 1;
			*t = '\0';

			bool negative = value < 0;
			if (negative)
				value = -value;

			long long integer = (long long)value;
			double frac = value - integer;

			int decimal = (int)(frac * 10.0 + 0.5);

			if (decimal == 10)
			{
				decimal = 0;
				++integer;
			}

			*--t = '0' + static_cast<char>(decimal);
			*--t = '.';

			do
			{
				*--t = '0' + (integer % 10);
				integer /= 10;
			} while (integer > 0);

			if (negative)
				*--t = '-';

			write(t);
		};

		write("{\"name\":");
		writeJsonString(shortName);

		write(",\"temperature\":");
		writeDouble(dTemperature);

		write(",\"power\":");
		writeDouble(dPower);

		write(",\"cores\":[");

		for (size_t i = 0; i < cores.size(); ++i)
		{
			if (i > 0)
				write(",");

			write("{\"t\":");
			writeDouble(cores[i].dTemperature);

			write(",\"u\":");
			writeDouble(cores[i].dUsage);

			write(",\"e\":");
			writeDouble(cores[i].dEffectiveFreq);

			write(",\"c\":");
			writeDouble(cores[i].dCurrentFreq);

			write("}");
		}

		write("]}");

		*p = '\0';

		return static_cast<int>(p - buffer);
	}

#ifdef _DEBUG
	void Log() const
	{
		LOGLN();
		LOG_DEBUG("----------------------------");
		LOG_DEBUG("-- Ryzen Metrics");
		LOG_DEBUG("Ryzen Metrics: Temp=%.2f C, Power=%.2f W, Cores=%zu", dTemperature, dPower, cores.size());

		for (size_t i = 0; i < cores.size(); ++i)
			cores[i].Log(i);

		LOG_DEBUG("----------------------------");
		LOGLN();
	}
#endif
};

struct WindowBorder
{
	RECT top{};
	RECT bottom{};
	RECT left{};
	RECT right{};

	// Extras
	RECT gamepadIcon{};
	RECT gamepadStatus{};
	RECT screeshotIcon{};
};

struct NetworkInterface
{
	NET_LUID luid{};
	std::wstring adapterName;
	std::wstring address;

	bool operator==(const NetworkInterface &other) const { return luid.Value == other.luid.Value && address == other.address; }

	std::wstring display() const { return adapterName + L": " + address; }
};

/**
  ---------------------------------
  |           TTTLE               |
  ---------------------------------
  | LABEL              VALUE      |
  | ----------------------------- |
  | LABEL              VALUE      |
  | ----------------------------- |
  | LABEL              VALUE      |
  | ----------------------------- |
  |                               |
  |                               |
  |        server status          |
  |         notification          |
  |             card              |
  | ----------------------------- |


 <-- border paddingSide  labelWidth gap valueWidth paddingSide border -->

 ∧
 |

titlePadding
titleHeight
titlePadding

paddingTop
*lineHeight
*lineGap
*separatorHeight
*lineGap

spacer

lineHeight2 (server status)
lineGap
lineHeight2 (notification)
lineGap
cardHeight
paddingBottom

 |
 ∨


 */
struct LayoutMetrics
{
	// width related
	int border;
	int paddingSide;
	int labelWidth;
	int valueWidth;
	int gap;

	// shared
	int paddingTop;

	// title
	int titlePadding;
	int titleHeight;
	int titleWidth;

	// body font (g_font)
	int lineHeight;
	int separatorHeight;
	int lineGap;
	int spacer;

	// notification font (g_notificationFont)
	int lineHeight2;

	// card font (g_cardFont)
	int cardHeight;
	int paddingBottom;

	// card
	int cardPaddingTopBottom = CARD_PADDING;

	int windowWidth;
	int windowHeight;

	// font
	int charWidth;

	// FPS tag
	int tagGap;
	int tagTopPadding;
	int tagSidePadding;
	int tagCharWidth;
	int tagCharHeight;
	int tagTextWidth;
	int tagWidth;
	int tagOffsetX;

	void Log() const
	{
		LOG_DEBUG(
			"LayoutMetrics: border=%d paddingSide=%d labelWidth=%d valueWidth=%d gap=%d paddingTop=%d titlePadding=%d titleHeight=%d titleWidth=%d lineHeight=%d separatorHeight=%d lineGap=%d spacer=%d lineHeight2=%d cardHeight=%d paddingBottom=%d cardPaddingTopBottom=%d windowWidth=%d windowHeight=%d charWidth=%d",
			border, paddingSide, labelWidth, valueWidth, gap, paddingTop, titlePadding, titleHeight, titleWidth, lineHeight, separatorHeight, lineGap, spacer, lineHeight2, cardHeight, paddingBottom, cardPaddingTopBottom, windowWidth, windowHeight, charWidth);
	}
};

namespace RadeonMon::Hardware
{
	struct MetricDouble
	{
		bool isSupported = false;
		double value = 0.0;
		int min = 0;
		int max = 0;

		double minValue = 0.0;
		double maxValue = 0.0;

		bool hasChanged = true;

		int RoundedValue() const { return static_cast<int>(std::lround(value)); }
	};

	struct MetricInt
	{
		bool isSupported = false;
		int value = 0;
		int min = 0;
		int max = 0;

		int minValue = 0;
		int maxValue = 0;

		bool hasChanged = true;
	};

	struct FPSMetrics
	{
		int current = 0;
		int previous = 0;

		void SetFPS(int fps)
		{
			previous = current;
			current = fps;
		}

		int GetFPS() const { return current; }

		int Delta() const { return current - previous; }
	};

	enum GPU_CAPS : uint32_t
	{
		// V0
		GPU_CAP_USAGE = 1 << 0,
		GPU_CAP_CLOCK = 1 << 1,
		GPU_CAP_VRAM_CLOCK = 1 << 2,
		GPU_CAP_TEMP = 1 << 3,
		GPU_CAP_HOTSPOT = 1 << 4,
		GPU_CAP_POWER = 1 << 5,
		GPU_CAP_BOARD_POWER = 1 << 6,
		GPU_CAP_FAN_SPEED = 1 << 7,
		GPU_CAP_VRAM_USAGE = 1 << 8,
		GPU_CAP_VOLTAGE = 1 << 9,
		GPU_CAP_INTAKE_TEMP = 1 << 10,

		// V1
		GPU_CAP_MEM_TEMP = 1 << 11,
		GPU_CAP_NPU_FREQ = 1 << 12,
		GPU_CAP_NPU_ACTIVITY = 1 << 13,

		// V2
		GPU_CAP_SHARED_MEMORY = 1 << 16,

		// V3
		GPU_CAP_FAN_DUTY = 1 << 17,

		// Tuning
		GPU_CAP_MANUAL_POWER_TUNING = 1 << 18
	};

	struct GpuMetricsSnapshot
	{
		bool valid = false;

		MetricDouble usage;
		MetricInt clockSpeed;
		MetricInt vramClockSpeed;

		MetricDouble temperature;
		MetricDouble hotspot;
		MetricDouble memoryTemperature;
		MetricDouble intakeTemperature;

		MetricDouble power;
		MetricDouble totalBoardPower;
		MetricInt voltage;
		MetricInt powerLimit; // Tuning setting
		MetricInt powerLimitWatts;

		MetricInt fanSpeed;
		MetricInt fanDuty;

		MetricInt vram;
		MetricInt sharedMemory;

		MetricInt npuFrequency;
		MetricInt npuActivityLevel;

		// FPSMetrics fps;
		int fps = -1;

		int64_t timestampMs = 0;

		// Fast zero-allocation, snprintf-free JSON builder
		int BuildJson(char *buffer, int bufferSize, const char *name) const
		{
			if (buffer == nullptr || bufferSize <= 1)
			{
				LOG_ERROR("Invalid JSON buffer");
				return -1;
			}

			char *p = buffer;
			const char *const end = buffer + bufferSize - 1;

			auto write = [&](const char *str) -> bool
			{
				while (*str)
				{
					if (p >= end)
						return false;
					*p++ = *str++;
				}
				return true;
			};

			auto writeChar = [&](char c) -> bool
			{
				if (p >= end)
					return false;
				*p++ = c;
				return true;
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

			auto writeInt = [&](int64_t value) -> bool
			{
				if (value == 0)
					return writeChar('0');

				char tmp[24];
				char *t = tmp + 23;
				*t = '\0';

				const bool neg = value < 0;
				uint64_t v = neg ? uint64_t(-(value + 1)) + 1 : uint64_t(value);

				do
				{
					*--t = '0' + (v % 10);
					v /= 10;
				} while (v > 0);

				if (neg)
					*--t = '-';
				return write(t);
			};

			// Simple double to string (no snprintf)
			auto writeDouble = [&](double value) -> bool
			{
				if (value < 0)
				{
					if (!writeChar('-'))
						return false;
					value = -value;
				}

				int64_t integral = static_cast<int64_t>(value);
				if (!writeInt(integral))
					return false;

				if (!writeChar('.'))
					return false;

				double fractional = value - integral;
				constexpr int precision = 1; // TODO: might need 2 for ryzen metrics

				for (int i = 0; i < precision; ++i)
				{
					fractional *= 10;
					int digit = static_cast<int>(fractional);
					if (!writeChar(static_cast<char>('0' + digit)))
						return false;
					fractional -= digit;
				}

				return true;
			};

			// Unified metric writer
			auto writeMetric = [&](const char *key, const auto &metric) -> bool
			{
				if (!write(",\""))
					return false;
				if (!write(key))
					return false;
				if (!write("\":{"))
					return false;

				if (!write("\"supported\":"))
					return false;
				if (!write(metric.isSupported ? "true" : "false"))
					return false;

				if (!write(",\"value\":"))
					return false;
				if (metric.isSupported)
				{
					if constexpr (std::is_same_v<std::decay_t<decltype(metric)>, MetricDouble>)
					{
						if (!writeDouble(metric.value))
							return false;
					}
					else
					{
						if (!writeInt(metric.value))
							return false;
					}
				}
				else
				{
					if (!write("null"))
						return false;
				}

				if (!write(",\"min\":"))
					return false;
				if (!writeInt(metric.min))
					return false;

				if (!write(",\"max\":"))
					return false;
				if (!writeInt(metric.max))
					return false;

				return writeChar('}');
			};

			// Build JSON
			if (!write("{\"valid\":"))
				goto overflow;
			if (!write(valid ? "true" : "false"))
				goto overflow;

			if (!write(",\"name\":"))
				goto overflow;
			writeJsonString(name);

			if (!writeMetric("usage", usage))
				goto overflow;
			if (!writeMetric("clock_speed", clockSpeed))
				goto overflow;
			if (!writeMetric("vram_clock_speed", vramClockSpeed))
				goto overflow;

			if (!writeMetric("temperature", temperature))
				goto overflow;
			if (!writeMetric("hotspot", hotspot))
				goto overflow;
			if (!writeMetric("memory_temperature", memoryTemperature))
				goto overflow;
			if (!writeMetric("intake_temperature", intakeTemperature))
				goto overflow;

			if (!writeMetric("power", power))
				goto overflow;
			if (!writeMetric("total_board_power", totalBoardPower))
				goto overflow;
			if (!writeMetric("voltage", voltage))
				goto overflow;

			if (!writeMetric("fan_speed", fanSpeed))
				goto overflow;
			if (!writeMetric("fan_duty", fanDuty))
				goto overflow;

			if (!writeMetric("vram", vram))
				goto overflow;
			if (!writeMetric("shared_memory", sharedMemory))
				goto overflow;

			if (!writeMetric("npu_frequency", npuFrequency))
				goto overflow;
			if (!writeMetric("npu_activity_level", npuActivityLevel))
				goto overflow;

			if (!write(",\"timestamp_ms\":"))
				goto overflow;
			if (!writeInt(timestampMs))
				goto overflow;

			if (!writeChar('}'))
				goto overflow;

			*p = '\0';
			return static_cast<int>(p - buffer);

		overflow:
			*p = '\0';
			LOG_ERROR("GPU metrics JSON buffer overflow (size=%d)", bufferSize);
			return -1;
		}
	};

	struct GPUInfo
	{
		// Identification
		std::wstring vendorId;
		std::wstring deviceId;
		std::wstring revisionId;
		std::wstring subSystemId;
		std::wstring subSystemVendorId;
		adlx_int uniqueId = 0;

		// General information
		adlx::ADLX_ASIC_FAMILY_TYPE asicFamilyType = adlx::ASIC_UNDEFINED;
		adlx::ADLX_GPU_TYPE gpuType = adlx::GPUTYPE_UNDEFINED;
		adlx_bool isExternal = false;

		// Descriptive information
		std::wstring name;
		std::string strName;
		std::wstring driverPath;
		std::wstring pnpString;

		// Precomputed for json
		char shortName[32] = "";

		// Memory
		adlx_uint totalVRAMMB = 0;
		std::wstring vramType;

		// BIOS
		std::wstring biosPartNumber;
		std::wstring biosVersion;
		std::wstring biosDate;

		// Status
		adlx_bool hasDesktops = false;

		// ADLX GPU2 - AMD Software / Driver information
		std::string amdSoftwareEdition;
		std::string amdSoftwareVersion;
		std::string driverVersion;
		std::string amdWindowsDriverVersion;

		adlx_uint amdSoftwareReleaseYear = 0;
		adlx_uint amdSoftwareReleaseMonth = 0;
		adlx_uint amdSoftwareReleaseDay = 0;

		// Windows LUID
		ADLX_LUID luid = {};

		// ADLX GPU2 - Applications running on this GPU
		struct GPUApplicationInfo
		{
			adlx_ulong processId = 0;

			std::wstring name;
			std::wstring fullPath;

			ADLX_APP_GPU_DEPENDENCY dependency = ADLX_APP_GPU_DEPENDENCY::APP_GPU_UNKNOWN;
		};

		std::vector<GPUApplicationInfo> applications;

		adlx_bool applicationListSupported = false;

		// ADLX GPU3 - Architecture / VRAM information
		std::wstring microArchitecture;
		adlx_uint highestVRAMBandwidth = 0;
		adlx_uint invisibleVRAM = 0;
		adlx_uint visibleVRAM = 0;
		adlx_uint vramVendorRevId = 0;
		adlx_uint vramBandwidth = 0;
		adlx_uint vramBitRate = 0;

		// For tooltip
		std::wstring tooltipText;

		// Helpers
		static std::wstring CharToWide(const char *value)
		{
			if (!value || !*value)
				return L"Unknown";

			int length = MultiByteToWideChar(CP_UTF8, 0, value, -1, nullptr, 0);

			if (length <= 0)
				return L"Unknown";

			std::wstring result(length - 1, L'\0');

			MultiByteToWideChar(CP_UTF8, 0, value, -1, result.data(), length);

			return result;
		}

		void SetVendorId(const char *value) { vendorId = CharToWide(value); }

		void SetDeviceId(const char *value) { deviceId = CharToWide(value); }

		void SetRevisionId(const char *value) { revisionId = CharToWide(value); }

		void SetSubSystemId(const char *value) { subSystemId = CharToWide(value); }

		void SetSubSystemVendorId(const char *value) { subSystemVendorId = CharToWide(value); }

		void SetShortName(const char *value)
		{
			if (!value)
			{
				shortName[0] = '\0';
				return;
			}

			std::string result(value);

			// Remove common vendor prefixes
			result = std::regex_replace(result, std::regex(R"(^\s*(AMD|NVIDIA|Intel)\s+)", std::regex::icase), "");

			// Remove trademark / registration markers
			result = std::regex_replace(result, std::regex(R"(\s*\((TM|R)\))", std::regex::icase), "");

			// Remove generic suffixes
			result = std::regex_replace(result, std::regex(R"(\s+(Graphics|GPU|Adapter)\s*$)", std::regex::icase), "");

			// Normalize whitespace
			result = std::regex_replace(result, std::regex(R"(\s+)"), " ");

			// Trim
			if (!result.empty() && result.front() == ' ')
				result.erase(0, 1);

			if (!result.empty() && result.back() == ' ')
				result.pop_back();

			// Store in fixed-size buffer
			strncpy_s(shortName, sizeof(shortName), result.c_str(), _TRUNCATE);

			shortName[sizeof(shortName) - 1] = '\0';
		}

		void SetName(const char *value)
		{
			name = CharToWide(value);
			strName = value ? value : "";
			SetShortName(value);
		}

		void SetDriverPath(const char *value) { driverPath = CharToWide(value); }
		void SetPnpString(const char *value) { pnpString = CharToWide(value); }
		void SetVramType(const char *value) { vramType = CharToWide(value); }
		void SetBiosPartNumber(const char *value) { biosPartNumber = CharToWide(value); }
		void SetBiosVersion(const char *value) { biosVersion = CharToWide(value); }
		void SetBiosDate(const char *value) { biosDate = CharToWide(value); }

		// ADLX GPU2 setters
		void SetAMDSoftwareEdition(const char *value) { amdSoftwareEdition = value ? value : ""; }
		void SetAMDSoftwareVersion(const char *value) { amdSoftwareVersion = value ? value : ""; }
		void SetDriverVersion(const char *value) { driverVersion = value ? value : ""; }
		void SetAMDWindowsDriverVersion(const char *value) { amdWindowsDriverVersion = value ? value : ""; }

		void SetAMDSoftwareReleaseDate(adlx_uint year, adlx_uint month, adlx_uint day)
		{
			amdSoftwareReleaseYear = year;
			amdSoftwareReleaseMonth = month;
			amdSoftwareReleaseDay = day;
		}

		void SetLuid(adlx_uint lowPart, adlx_uint highPart)
		{
			luid.lowPart = lowPart;
			luid.highPart = highPart;
		}

		void AddGpuApplication(adlx_ulong processId, const wchar_t *process_name, const wchar_t *fullPath, ADLX_APP_GPU_DEPENDENCY dependency)
		{
			GPUApplicationInfo app;

			app.processId = processId;

			if (process_name)
				app.name = process_name;

			if (fullPath)
				app.fullPath = fullPath;

			app.dependency = dependency;

			applications.push_back(std::move(app));
		}

		// ADLX GPU3 setters
		void SetMicroArchitecture(const char *value) { microArchitecture = CharToWide(value); }
		void SetHighestVRAMBandwidth(adlx_uint value) { highestVRAMBandwidth = value; }
		void SetInvisibleVRAM(adlx_uint value) { invisibleVRAM = value; }
		void SetVisibleVRAM(adlx_uint value) { visibleVRAM = value; }
		void SetVRAMVendorRevId(adlx_uint value) { vramVendorRevId = value; }
		void SetVRAMBandwidth(adlx_uint value) { vramBandwidth = value; }
		void SetVRAMBitRate(adlx_uint value) { vramBitRate = value; }

		void BuildToolTip() { tooltipText = GetTooltip(); }

		static const wchar_t *AsicFamilyToString(adlx::ADLX_ASIC_FAMILY_TYPE family)
		{
			switch (family)
			{
			case adlx::ASIC_UNDEFINED:
				return L"Undefined";

			case adlx::ASIC_RADEON:
				return L"Radeon";

			case adlx::ASIC_FIREPRO:
				return L"FirePro";

			case adlx::ASIC_FIREMV:
				return L"FireMV";

			case adlx::ASIC_FIRESTREAM:
				return L"FireStream";

			case adlx::ASIC_FUSION:
				return L"Fusion";

			case adlx::ASIC_EMBEDDED:
				return L"Embedded";

			default:
				return L"Unknown";
			}
		}

		static const wchar_t *GPUTypeToString(adlx::ADLX_GPU_TYPE type)
		{
			switch (type)
			{
			case adlx::GPUTYPE_UNDEFINED:
				return L"Undefined";

			case adlx::GPUTYPE_INTEGRATED:
				return L"Integrated";

			case adlx::GPUTYPE_DISCRETE:
				return L"Discrete";

			default:
				return L"Unknown";
			}
		}

		static const wchar_t *GpuDependencyToString(ADLX_APP_GPU_DEPENDENCY dependency)
		{
			switch (dependency)
			{
			case ADLX_APP_GPU_DEPENDENCY::APP_GPU_BOUND:
				return L"Bound";

			case ADLX_APP_GPU_DEPENDENCY::APP_GPU_NOT_BOUND:
				return L"Not Bound";

			case ADLX_APP_GPU_DEPENDENCY::APP_GPU_UNKNOWN:
			default:
				return L"Unknown";
			}
		}

		static const wchar_t *VendorIdToString(const std::wstring &vendorId)
		{
			if (_wcsicmp(vendorId.c_str(), L"1002") == 0)
				return L"AMD";

			if (_wcsicmp(vendorId.c_str(), L"10DE") == 0)
				return L"NVIDIA";

			if (_wcsicmp(vendorId.c_str(), L"8086") == 0)
				return L"Intel";

			return L"Unknown";
		}

		void Log() const
		{
			LOGLN();
			LOG_INFO("=== GPU Information ===");
			LOG_INFO("Name:%ls", name.c_str());
			LOG_INFO("ShortName:%s", shortName);
			LOG_INFO("Vendor ID:%ls (%ls)", vendorId.c_str(), std::wstring(VendorIdToString(vendorId)).c_str());
			LOG_INFO("Device ID:%ls", deviceId.c_str());
			LOG_INFO("Revision ID:%ls", revisionId.c_str());
			LOG_INFO("Subsystem ID:%ls", subSystemId.c_str());
			LOG_INFO("Subsystem Vendor ID:%ls (%ls)", subSystemVendorId.c_str(), std::wstring(SubSystemVendorToString(subSystemVendorId)).c_str());
			LOG_INFO("Unique ID: %d", uniqueId);
			LOG_INFO("ASIC Family:%ls", AsicFamilyToString(asicFamilyType));
			LOG_INFO("GPU Type:%ls", GPUTypeToString(gpuType));
			LOG_INFO("External:%ls", isExternal ? L"Yes" : L"No");
			LOG_INFO("Driver Path:%ls", driverPath.c_str());
			LOG_INFO("PNP String:%ls", pnpString.c_str());
			LOG_INFO("Total VRAM: %u MB", totalVRAMMB);
			LOG_INFO("VRAM Type:%ls", vramType.c_str());
			LOG_INFO("BIOS Part Number:%ls", biosPartNumber.c_str());
			LOG_INFO("BIOS Version:%ls", biosVersion.c_str());
			LOG_INFO("BIOS Date:%ls", biosDate.c_str());
			LOG_INFO("Has Desktops:%ls", hasDesktops ? L"Yes" : L"No");

			// GPU2
			LOG_INFO("AMD Software Edition:%s", amdSoftwareEdition.c_str());
			LOG_INFO("AMD Software Version:%s", amdSoftwareVersion.c_str());
			LOG_INFO("Driver Version:%s", driverVersion.c_str());
			LOG_INFO("AMD Windows Driver Version:%s", amdWindowsDriverVersion.c_str());
			LOG_INFO("AMD Software Release Date: %u-%02u-%02u", amdSoftwareReleaseYear, amdSoftwareReleaseMonth, amdSoftwareReleaseDay);
			LOG_INFO("LUID: LowPart:%lu HighPart:%lu", luid.lowPart, luid.highPart);
			LOG_INFO("Application List Supported:%ls", applicationListSupported ? L"Yes" : L"No");
			LOG_INFO("Applications:%zu", applications.size());
			for (const auto &app : applications)
				LOG_INFO("  PID:%lu Name:%ls Path:%ls Dependency:%ls", app.processId, app.name.c_str(), app.fullPath.c_str(), GpuDependencyToString(app.dependency));

			// GPU3
			LOG_INFO("Micro Architecture:%ls", microArchitecture.c_str());
			LOG_INFO("Highest VRAM Bandwidth:%u MB/s", highestVRAMBandwidth);
			LOG_INFO("Invisible VRAM:%u MB", invisibleVRAM);
			LOG_INFO("Visible VRAM:%u MB", visibleVRAM);
			LOG_INFO("VRAM Vendor ID: 0x%X (%ls)", vramVendorRevId, VRAMVendorToString(vramVendorRevId));
			LOG_INFO("VRAM Bandwidth:%u MB/s", vramBandwidth);
			LOG_INFO("VRAM Bit Rate:%u Mbps", vramBitRate);
			LOG_INFO("=======================");
			LOGLN();
		}

		int GetDriverPathTooltipWidth(HWND hwnd) const
		{
			std::wstring text = L"Driver Path: " + driverPath;
			HDC hdc = GetDC(hwnd);
			SIZE size = {};
			GetTextExtentPoint32W(hdc, text.c_str(), static_cast<int>(text.length()), &size);
			ReleaseDC(hwnd, hdc);
			return size.cx;
		}

		static const wchar_t *VRAMVendorToString(adlx_uint vendorId)
		{
			switch (vendorId)
			{
			case 0x1:
				return L"Samsung";

			case 0x2:
				return L"Infineon";

			case 0x3:
				return L"Elpida";

			case 0x4:
				return L"Etron";

			case 0x5:
				return L"Nanya";

			case 0x6:
				return L"Hynix (SK hynix)";

			case 0x7:
				return L"Mosel";

			case 0x8:
				return L"Winbond";

			case 0x9:
				return L"ESMT";

			case 0xF:
				return L"Micron";

			default:
				return L"Unknown";
			}
		}

		static const wchar_t *SubSystemVendorToString(const std::wstring &vendorId)
		{
			unsigned int id = 0;

			try
			{
				id = std::stoul(vendorId, nullptr, 16);
			}
			catch (...)
			{
				return L"Unknown";
			}

			switch (id)
			{
			case 0x1002:
				return L"AMD";

			case 0x1043:
				return L"ASUSTeK";

			case 0x196D:
				return L"Club 3D";

			case 0x1092:
				return L"Diamond Multimedia";

			case 0x18BC:
				return L"GeCube";

			case 0x1458:
				return L"Gigabyte";

			case 0x17AF:
				return L"HIS";

			case 0x16F3:
				return L"Jetway";

			case 0x1462:
				return L"MSI";

			case 0x1DA2:
				return L"Sapphire";

			case 0x148C:
				return L"PowerColor";

			case 0x1545:
				return L"VisionTek";

			case 0x1682:
			case 0x1EAE:
				return L"XFX";

			case 0x1025:
				return L"Acer";

			case 0x106B:
				return L"Apple";

			case 0x1028:
				return L"Dell";

			case 0x107B:
				return L"Gateway";

			case 0x103C:
				return L"HP";

			case 0x17AA:
				return L"Lenovo";

			case 0x104D:
				return L"Sony";

			case 0x1179:
				return L"Toshiba";

			default:
				return L"Unknown";
			}
		}

		std::wstring GetTooltip() const
		{
			std::wstring tooltip;

			// GPU INFO
			tooltip += L"=== GPU Info ===\r\n";
			tooltip += L"Name: " + name + L"\r\n";
			tooltip += L"Vendor ID: " + vendorId + L" (" + std::wstring(VendorIdToString(vendorId)) + L")\r\n";
			tooltip += L"Device ID: " + deviceId + L"\r\n";
			tooltip += L"Revision ID: " + revisionId + L"\r\n";
			tooltip += L"Subsystem ID: " + subSystemId + L"\r\n";
			tooltip += L"Subsystem Vendor ID: " + subSystemVendorId + L" (" + std::wstring(SubSystemVendorToString(subSystemVendorId)) + L")\r\n";
			tooltip += L"Unique ID: " + std::to_wstring(uniqueId) + L"\r\n";
			tooltip += L"ASIC Family: " + std::wstring(AsicFamilyToString(asicFamilyType)) + L"\r\n";
			tooltip += L"GPU Type: " + std::wstring(GPUTypeToString(gpuType)) + L"\r\n";
			tooltip += L"External: " + std::wstring(isExternal ? L"Yes" : L"No") + L"\r\n";
			tooltip += L"Micro Architecture: " + microArchitecture + L"\r\n";
			tooltip += L"Driver Path: " + driverPath + L"\r\n";
			tooltip += L"PNP String: " + pnpString + L"\r\n";
			tooltip += L"Has Desktops: " + std::wstring(hasDesktops ? L"Yes" : L"No") + L"\r\n";

			// BIOS INFO
			tooltip += L"\r\n=== BIOS Info ===\r\n";
			tooltip += L"Part Number: " + biosPartNumber + L"\r\n";
			tooltip += L"Version: " + biosVersion + L"\r\n";
			tooltip += L"Date: " + biosDate + L"\r\n";

			// VRAM INFO
			tooltip += L"\r\n=== VRAM Info ===\r\n";
			tooltip += L"Total VRAM: " + std::to_wstring(totalVRAMMB) + L" MB\r\n";
			tooltip += L"VRAM Type: " + vramType + L"\r\n";
			tooltip += L"Visible VRAM: " + std::to_wstring(visibleVRAM) + L" MB\r\n";
			tooltip += L"Invisible VRAM: " + std::to_wstring(invisibleVRAM) + L" MB\r\n";
			tooltip += L"Highest VRAM Bandwidth: " + std::to_wstring(highestVRAMBandwidth) + L" MB/s\r\n";
			tooltip += L"VRAM Bandwidth: " + std::to_wstring(vramBandwidth) + L" MB/s\r\n";
			tooltip += L"VRAM Bit Rate: " + std::to_wstring(vramBitRate) + L" Mbps\r\n";

			wchar_t buffer[64] = {};
			swprintf_s(buffer, L"VRAM Vendor ID: 0x%02X (%ls)\r\n", vramVendorRevId, VRAMVendorToString(vramVendorRevId));
			tooltip += buffer;

			// SOFTWARE INFO
			tooltip += L"\r\n=== Software Info ===\r\n";
			tooltip += L"AMD Software Edition: " + CharToWide(amdSoftwareEdition.c_str()) + L"\r\n";
			tooltip += L"AMD Software Version: " + CharToWide(amdSoftwareVersion.c_str()) + L"\r\n";
			tooltip += L"Driver Version: " + CharToWide(driverVersion.c_str()) + L"\r\n";
			tooltip += L"AMD Windows Driver Version: " + CharToWide(amdWindowsDriverVersion.c_str()) + L"\r\n";

			if (amdSoftwareReleaseYear != 0)
				tooltip += L"AMD Software Release Date: " + std::to_wstring(amdSoftwareReleaseYear) + L"-" + std::to_wstring(amdSoftwareReleaseMonth) + L"-" + std::to_wstring(amdSoftwareReleaseDay) + L"\r\n";

			tooltip += L"LUID: " + std::to_wstring(luid.highPart) + L":" + std::to_wstring(luid.lowPart) + L"\r\n";
			tooltip += L"Application List Supported: " + std::wstring(applicationListSupported ? L"Yes" : L"No") + L"\r\n";
			tooltip += L"Applications: " + std::to_wstring(applications.size()) + L"\r\n";

			for (const auto &app : applications)
			{
				tooltip += L"  PID: " + std::to_wstring(app.processId) + L"\r\n";
				tooltip += L"  Name: " + app.name + L"\r\n";
				tooltip += L"  Path: " + app.fullPath + L"\r\n";
				tooltip += L"  GPU Dependency: " + std::wstring(GpuDependencyToString(app.dependency)) + L"\r\n";
			}

			return tooltip;
		}
	};

	struct DisplayInfo
	{
		int index = -1;
		wchar_t name[256] = {};
		uint16_t width = 0;
		uint16_t height = 0;
		uint16_t frequency = 0;
		bool isPortrait;

		void Log() { LOG_INFO("\t[%d] : %ls, %dx%d @%dHz, portrait=%s", index, name, width, height, frequency, isPortrait ? "yes" : "no"); }
	};

	class DisplayManager
	{
	public:
		void Add(const DisplayInfo &display) { m_displays.push_back(display); }

		void Discover()
		{
			for (DWORD i = 0;; ++i)
			{
				DISPLAY_DEVICE dd = {};
				dd.cb = sizeof(dd);

				if (!EnumDisplayDevices(nullptr, i, &dd, 0))
					break;

				// Skip inactive displays if desired
				if (!(dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
					continue;

				DisplayInfo di;

				DEVMODE dm = {};
				dm.dmSize = sizeof(dm);

				if (EnumDisplaySettings(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm))
				{
					di.index = i;
					wcscpy_s(di.name, dd.DeviceName);
					di.width = static_cast<uint16_t>(dm.dmPelsWidth);
					di.height = static_cast<uint16_t>(dm.dmPelsHeight);
					di.frequency = static_cast<uint16_t>(dm.dmDisplayFrequency);
					di.isPortrait = dm.dmPelsHeight >= dm.dmPelsWidth;

					Add(di);
				}

				dd.cb = sizeof(dd); // Required before next call
			}

			LogAll();
		}

		void Clear()
		{
			m_displays.clear();
			m_current = 0;
		}

		/**
		 * Move the cursor to the next element of the list then returns the element.
		 *
		 * Usage:
		 * auto display = manager.Next();
		 * if (display.has_value()) { ... }
		 */
		const std::optional<DisplayInfo> Next()
		{
			if (m_displays.empty())
				return std::nullopt;

			m_current = (m_current + 1) % m_displays.size();
			DisplayInfo result = m_displays[m_current];

			return result;
		}

		void SetCurrent(int index) { m_current = index; }

		const std::optional<DisplayInfo> Current() const
		{
			if (m_displays.empty())
				return std::nullopt;

			return m_displays[m_current];
		}

		size_t Size() const { return m_displays.size(); }

		bool Empty() const { return m_displays.empty(); }

		void LogAll()
		{
			LOGLN();
			LOG_DEBUG("Displays");
			LOG_DEBUG("---------------");

			for (auto &d : m_displays)
				d.Log();

			LOG_DEBUG("---------------");
			LOGLN();
		}

		const RadeonMon::Hardware::DisplayInfo &Get(int index) const { return m_displays.at(index); }

	private:
		std::vector<DisplayInfo> m_displays;
		size_t m_current = 0;
	};
} // namespace RadeonMon::Hardware

struct AppTitle
{
	RECT rc{};
	int x = 0;
	int y = 0;
	const wchar_t *name = nullptr;
	uint8_t textLength = 0;

	AppTitle(const wchar_t *n, uint8_t len) : name(n), textLength(len) {}

	void SetTitle(const wchar_t *ptr, uint8_t len)
	{
		name = ptr;
		textLength = len;
	}

	// Use already precomputed metrics to center the RC
	void UpdateRC(const RECT &borderTop, const LayoutMetrics &m)
	{
		// center the title inside the top border
		x = (m.windowWidth - m.titleWidth) / 2;
		y = (borderTop.bottom - m.titleHeight) / 2;
		rc = {x, y, x + m.titleWidth, y + m.titleHeight};
	}

	// Recompute the width then updates the layout metrics
	void UpdateRC(HDC hdc, LayoutMetrics &m, const RECT &borderTop, HFONT font)
	{
		SIZE sz{};
		HFONT oldFont = (HFONT)SelectObject(hdc, font);

		if (!oldFont)
			LOG_ERROR("SelectObject failed on g_titleFont");

		if (!GetTextExtentPoint32W(hdc, name, textLength, &sz))
			LOG_ERROR("GetTextExtentPoint32W failed");

		m.titleWidth = sz.cx;
		UpdateRC(borderTop, m);

		SelectObject(hdc, oldFont);
	}
};

struct VersionCheckResult
{
	std::wstring latestVersion;
	bool updateAvailable;
	bool showDialogs;
};

struct adlx_version
{
	int major = 0;
	int minor = 0;
	bool valid = false;

	constexpr bool operator<(const adlx_version &other) const { return major < other.major || (major == other.major && minor < other.minor); }
	constexpr bool operator>=(const adlx_version &other) const { return !(*this < other); }
};

enum class UpscalingType : uint8_t
{
	FSR1,
	FSR2,
	FSR3,
	FSR4,
	Count
};

enum class GraphicsAPI : uint8_t
{
	DX9,
	DX11,
	DX12,
	Vulkan,
	Count
};

inline constexpr const wchar_t *UpscalingTypeTxt[] = {L"FSR1", L"FSR2", L"FSR3", L"FSR4"};
inline constexpr const wchar_t *GraphicsAPITxt[] = {L"DX9", L"DX11", L"DX12", L"VK"};

static_assert(_countof(UpscalingTypeTxt) == static_cast<size_t>(UpscalingType::Count));
static_assert(_countof(GraphicsAPITxt) == static_cast<size_t>(GraphicsAPI::Count));
