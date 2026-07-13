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

#include <ifdef.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include "../../third_party/AMD/ADLX-1.5/SDK/Include/ADLXDefines.h"

#include "radeonmon/logging.hpp"

extern UINT g_dpi;

enum class PropertyType
{
    Text,
    Separator
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
    bool warning = false;
    bool repaintLabel = true;

    inline void SetLabel(const wchar_t *txt)
    {
        label = txt ? txt : L"";
    }

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

    ~GdiBackBuffer()
    {
        Destroy();
    }
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
    void Log(size_t index) const
    {
        LOG_DEBUG("[%zu] %.2f C, %.2f%%, %.2f MHz / %.2f MHz",
                  index,
                  dTemperature,
                  dUsage,
                  dEffectiveFreq,
                  dCurrentFreq);
    }
#endif
};

struct RyzenMetrics
{
    std::vector<RyzenCoreMetrics> cores;
    char name[256] = "";
    double dTemperature = 0.0;
    double dPower = 0.0;

#ifdef _DEBUG
    void Log() const
    {
        LOG_DEBUG("");
        LOG_DEBUG("----------------------------");
        LOG_DEBUG("-- Ryzen Metrics");
        LOG_DEBUG("Ryzen Metrics: Temp=%.2f C, Power=%.2f W, Cores=%zu",
                  dTemperature,
                  dPower,
                  cores.size());

        for (size_t i = 0; i < cores.size(); ++i)
        {
            cores[i].Log(i);
        }

        LOG_DEBUG("----------------------------");
        LOG_DEBUG("");
    }
#endif

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
        writeJsonString(name);

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
};

struct WindowBorder
{
    RECT top{};
    RECT bottom{};
    RECT left{};
    RECT right{};
};

struct NetworkInterface
{
    NET_LUID luid{};
    std::wstring adapterName;
    std::wstring address;

    bool operator==(const NetworkInterface &other) const
    {
        return luid.Value == other.luid.Value && address == other.address;
    }

    std::wstring display() const
    {
        return adapterName + L": " + address;
    }
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

    int windowWidth;
    int windowHeight;
};

namespace RadeonMon::Hardware
{
    struct MetricDouble
    {
        bool isSupported = false;
        double value = 0.0;
        int min = 0;
        int max = 0;

        int RoundedValue() const { return static_cast<int>(std::lround(value)); }
    };

    struct MetricInt
    {
        bool isSupported = false;
        int value = 0;
        int min = 0;
        int max = 0;
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

        int GetFPS() const
        {
            return current;
        }

        int Delta() const
        {
            return current - previous;
        }
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
        GPU_CAP_FAN_DUTY = 1 << 17
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

        MetricInt fanSpeed;
        MetricInt fanDuty;

        MetricInt vram;
        MetricInt sharedMemory;

        MetricInt npuFrequency;
        MetricInt npuActivityLevel;

        // FPSMetrics fps;
        int fps;

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
                uint64_t v = neg ? -value : static_cast<uint64_t>(value);

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

        // Memory
        adlx_uint totalVRAMMB = 0;
        std::wstring vramType;

        // BIOS
        std::wstring biosPartNumber;
        std::wstring biosVersion;
        std::wstring biosDate;

        // Status
        adlx_bool hasDesktops = false;

        // For tooltip
        std::wstring tooltipText;

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

        void SetVendorId(const char *value)
        {
            vendorId = CharToWide(value);
        }

        void SetDeviceId(const char *value)
        {
            deviceId = CharToWide(value);
        }

        void SetRevisionId(const char *value)
        {
            revisionId = CharToWide(value);
        }

        void SetSubSystemId(const char *value)
        {
            subSystemId = CharToWide(value);
        }

        void SetSubSystemVendorId(const char *value)
        {
            subSystemVendorId = CharToWide(value);
        }

        void SetName(const char *value)
        {
            name = CharToWide(value);
            strName = value;
        }

        void SetDriverPath(const char *value)
        {
            driverPath = CharToWide(value);
        }

        void SetPnpString(const char *value)
        {
            pnpString = CharToWide(value);
        }

        void SetVramType(const char *value)
        {
            vramType = CharToWide(value);
        }

        void SetBiosPartNumber(const char *value)
        {
            biosPartNumber = CharToWide(value);
        }

        void SetBiosVersion(const char *value)
        {
            biosVersion = CharToWide(value);
        }

        void SetBiosDate(const char *value)
        {
            biosDate = CharToWide(value);
        }

        void BuildToolTip()
        {
            tooltipText = GetTooltip();
        }

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

        void Log() const
        {
            LOG_INFO("");
            LOG_INFO("=== GPU Information ===");
            LOG_INFO("Name:%ls", name.c_str());
            LOG_INFO("Vendor ID:%ls", vendorId.c_str());
            LOG_INFO("Device ID:%ls", deviceId.c_str());
            LOG_INFO("Revision ID:%ls", revisionId.c_str());
            LOG_INFO("Subsystem ID:%ls", subSystemId.c_str());
            LOG_INFO("Subsystem Vendor ID:%ls", subSystemVendorId.c_str());
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
            LOG_INFO("=======================");
            LOG_INFO("");
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

        std::wstring GetTooltip() const
        {
            std::wstring tooltip;

            tooltip += L"=== GPU Information ===\r\n";
            tooltip += L"Name: " + name + L"\r\n";
            tooltip += L"Vendor ID: " + vendorId + L"\r\n";
            tooltip += L"Device ID: " + deviceId + L"\r\n";
            tooltip += L"Revision ID: " + revisionId + L"\r\n";
            tooltip += L"Subsystem ID: " + subSystemId + L"\r\n";
            tooltip += L"Subsystem Vendor ID: " + subSystemVendorId + L"\r\n";
            tooltip += L"Unique ID: " + std::to_wstring(uniqueId) + L"\r\n";
            tooltip += L"ASIC Family: " + std::wstring(AsicFamilyToString(asicFamilyType)) + L"\r\n";
            tooltip += L"GPU Type: " + std::wstring(GPUTypeToString(gpuType)) + L"\r\n";
            tooltip += L"External: " + std::wstring(isExternal ? L"Yes" : L"No") + L"\r\n";
            tooltip += L"Driver Path: " + driverPath + L"\r\n";
            tooltip += L"PNP String: " + pnpString + L"\r\n";
            tooltip += L"Total VRAM: " + std::to_wstring(totalVRAMMB) + L" MB\r\n";
            tooltip += L"VRAM Type: " + vramType + L"\r\n";
            tooltip += L"BIOS Part Number: " + biosPartNumber + L"\r\n";
            tooltip += L"BIOS Version: " + biosVersion + L"\r\n";
            tooltip += L"BIOS Date: " + biosDate + L"\r\n";
            tooltip += L"Has Desktops: " + std::wstring(hasDesktops ? L"Yes" : L"No");

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
        void Add(const DisplayInfo &display)
        {
            m_displays.push_back(display);
        }

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

        void SetCurrent(int index)
        {
            m_current = index;
        }

        const std::optional<DisplayInfo> Current() const
        {
            if (m_displays.empty())
                return std::nullopt;

            return m_displays[m_current];
        }

        size_t Size() const
        {
            return m_displays.size();
        }

        bool Empty() const
        {
            return m_displays.empty();
        }

        void LogAll()
        {
            LOG_DEBUG("");
            LOG_DEBUG("Displays");
            LOG_DEBUG("---------------");

            for (auto &d : m_displays)
                d.Log();

            LOG_DEBUG("---------------");
            LOG_DEBUG("");
        }

        const RadeonMon::Hardware::DisplayInfo &Get(int index) const { return m_displays.at(index); }

    private:
        std::vector<DisplayInfo> m_displays;
        size_t m_current = 0;
    };
}
