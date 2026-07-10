#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <cstdio>
#include <cstring>
#ifdef _DEBUG
#include <chrono>
#endif

inline const char *BaseFileName(const char *path)
{
    const char *file = std::strrchr(path, '/');
    if (!file)
        file = std::strrchr(path, '\\');
    return file ? file + 1 : path;
}

#define LOG_IMPL(level, fmt, ...)     \
    do                                \
    {                                 \
        printf("[%s] " fmt "\n",      \
               level, ##__VA_ARGS__); \
    } while (0)

#define LOG_ERROR(fmt, ...) LOG_IMPL("E", fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_IMPL("W", fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_IMPL("I", fmt, ##__VA_ARGS__)

#ifdef _DEBUG
#define LOG_DEBUG(fmt, ...) LOG_IMPL("D", fmt, ##__VA_ARGS__)
#else
#define LOG_DEBUG(fmt, ...) \
    do                      \
    {                       \
    } while (0)
#endif

#define START_CHRONO(name) \
    auto name = std::chrono::high_resolution_clock::now()

#define END_CHRONO(name, displayName)                                     \
    do                                                                    \
    {                                                                     \
        auto __chrono_end = std::chrono::high_resolution_clock::now();    \
        auto __chrono_duration =                                          \
            std::chrono::duration<double, std::milli>(                    \
                __chrono_end - name);                                     \
        LOG_DEBUG("%s: %.3f ms", displayName, __chrono_duration.count()); \
    } while (0)

#endif // LOGGING_HPP