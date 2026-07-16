#pragma once

#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <cstdio>
#include <cstring>

inline const char *BaseFileName(const char *path)
{
    const char *file = std::strrchr(path, '/');

    if (!file)
        file = std::strrchr(path, '\\');

    return file ? file + 1 : path;
}

#ifdef _DEBUG

#include <chrono>

#define COLOR_RED "\033[91m"
#define COLOR_ORANGE "\033[33m"
#define COLOR_YELLOW "\033[93m"
#define COLOR_CYAN "\033[96m"
#define COLOR_DIM_GREY "\033[90m"
#define COLOR_RESET "\033[0m"

#define LOG_IMPL(level, color, fmt, ...)                                      \
    do                                                                        \
    {                                                                         \
        std::printf(                                                          \
            color "[%s] " COLOR_DIM_GREY "%s:%d " color fmt COLOR_RESET "\n", \
            level,                                                            \
            BaseFileName(__FILE__),                                           \
            __LINE__,                                                         \
            ##__VA_ARGS__);                                                   \
    } while (0)

#define LOG_ERROR(fmt, ...) LOG_IMPL("E", COLOR_RED, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_IMPL("W", COLOR_ORANGE, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_IMPL("I", COLOR_YELLOW, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) LOG_IMPL("T", COLOR_DIM_GREY, fmt, ##__VA_ARGS__)

#define LOGLN()            \
    do                     \
    {                      \
        std::printf("\n"); \
    } while (0)

#define START_CHRONO(name) \
    auto name = std::chrono::high_resolution_clock::now()

#define END_CHRONO(name, displayName)                                  \
    do                                                                 \
    {                                                                  \
        auto __chrono_end = std::chrono::high_resolution_clock::now(); \
        auto __elapsed = std::chrono::duration<double, std::milli>(    \
            __chrono_end - (name));                                    \
        LOG_DEBUG("%s: %.3f ms", displayName, __elapsed.count());      \
    } while (0)

#define END_CHRONO_MICRO(name, displayName)                            \
    do                                                                 \
    {                                                                  \
        auto __chrono_end = std::chrono::high_resolution_clock::now(); \
        auto __elapsed = std::chrono::duration<double, std::micro>(    \
            __chrono_end - (name));                                    \
        LOG_DEBUG("%s: %.1f us", displayName, __elapsed.count());      \
    } while (0)

#else

template <typename... Args>
constexpr void logDisabled(Args &&...)
{
}

#define LOG_IMPL(...) logDisabled(__VA_ARGS__)

#define LOG_ERROR(...) LOG_IMPL(__VA_ARGS__)
#define LOG_WARN(...) LOG_IMPL(__VA_ARGS__)
#define LOG_INFO(...) LOG_IMPL(__VA_ARGS__)
#define LOG_DEBUG(...) LOG_IMPL(__VA_ARGS__)
#define LOG_TRACE(...) LOG_IMPL(__VA_ARGS__)

#define LOGLN() \
    do          \
    {           \
    } while (0)

#define START_CHRONO(name)
#define END_CHRONO(name, displayName)
#define END_CHRONO_MICRO(name, displayName)

#endif

#endif // LOGGING_HPP