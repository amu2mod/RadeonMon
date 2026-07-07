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

#endif // LOGGING_HPP