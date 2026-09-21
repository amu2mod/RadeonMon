#pragma once

#ifndef LOGGING_HPP
#define LOGGING_HPP

#include <time.h>

#include <cstdio>
#include <cstring>
#include <chrono>

inline const char *BaseFileName(const char *path)
{
	const char *file = std::strrchr(path, '/');

	if (!file)
		file = std::strrchr(path, '\\');

	return file ? file + 1 : path;
}

#ifdef _DEBUG

#define COLOR_RED "\033[91m"
#define COLOR_ORANGE "\033[33m"
#define COLOR_YELLOW "\033[93m"
#define COLOR_CYAN "\033[96m"
#define COLOR_DIM_GREY "\033[90m"
#define COLOR_RESET "\033[0m"

// timestamp cache
struct LogClock
{
	std::time_t second = (std::numeric_limits<std::time_t>::min)();
	int milliseconds = -1;
	char timestamp[13]{};
};

// use millisecond cache
inline const char *GetLogTimestamp(std::chrono::system_clock::time_point now)
{
	thread_local LogClock clock;
	const auto duration = now.time_since_epoch();
	const auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);
	const auto milliseconds = static_cast<int>(total_ms.count() % 1000);
	const auto second = static_cast<std::time_t>(std::chrono::duration_cast<std::chrono::seconds>(duration).count());

	if (second != clock.second)
	{
		std::tm tm{};
		localtime_s(&tm, &second);

		clock.timestamp[0] = char('0' + tm.tm_hour / 10);
		clock.timestamp[1] = char('0' + tm.tm_hour % 10);
		clock.timestamp[2] = ':';
		clock.timestamp[3] = char('0' + tm.tm_min / 10);
		clock.timestamp[4] = char('0' + tm.tm_min % 10);
		clock.timestamp[5] = ':';
		clock.timestamp[6] = char('0' + tm.tm_sec / 10);
		clock.timestamp[7] = char('0' + tm.tm_sec % 10);
		clock.timestamp[8] = '.';

		clock.second = second;
	}

	if (milliseconds != clock.milliseconds)
	{
		clock.timestamp[9] = char('0' + milliseconds / 100);
		clock.timestamp[10] = char('0' + (milliseconds / 10) % 10);
		clock.timestamp[11] = char('0' + milliseconds % 10);
		clock.timestamp[12] = '\0';

		clock.milliseconds = milliseconds;
	}

	return clock.timestamp;
}

#define LOG_IMPL(level, color, fmt, ...)                                                                                                                                                                                                                                                                                       \
	do                                                                                                                                                                                                                                                                                                                         \
	{                                                                                                                                                                                                                                                                                                                          \
		const auto log_now = std::chrono::system_clock::now();                                                                                                                                                                                                                                                                 \
		const char *timestamp = GetLogTimestamp(log_now);                                                                                                                                                                                                                                                                      \
                                                                                                                                                                                                                                                                                                                               \
		const char *file_name = BaseFileName(__FILE__);                                                                                                                                                                                                                                                                        \
		const int line = __LINE__;                                                                                                                                                                                                                                                                                             \
		const int prefix_length = 4 + 12 + 1 + static_cast<int>(std::strlen(file_name)) + static_cast<int>(std::snprintf(nullptr, 0, ":%d", line));                                                                                                                                                                            \
                                                                                                                                                                                                                                                                                                                               \
		/* Standard terminal tab stops are normally every 8 columns. */                                                                                                                                                                                                                                                        \
		constexpr int TAB_WIDTH = 8;                                                                                                                                                                                                                                                                                           \
		constexpr int MAX_TABS = 3;                                                                                                                                                                                                                                                                                            \
                                                                                                                                                                                                                                                                                                                               \
		int column = prefix_length;                                                                                                                                                                                                                                                                                            \
		int tabs = 0;                                                                                                                                                                                                                                                                                                          \
                                                                                                                                                                                                                                                                                                                               \
		while (tabs < MAX_TABS)                                                                                                                                                                                                                                                                                                \
		{                                                                                                                                                                                                                                                                                                                      \
			const int next_tab = ((column / TAB_WIDTH) + 1) * TAB_WIDTH;                                                                                                                                                                                                                                                       \
			column = next_tab;                                                                                                                                                                                                                                                                                                 \
			++tabs;                                                                                                                                                                                                                                                                                                            \
                                                                                                                                                                                                                                                                                                                               \
			/* Stop once we've reached the desired message area. */                                                                                                                                                                                                                                                            \
			if (column >= 48)                                                                                                                                                                                                                                                                                                  \
				break;                                                                                                                                                                                                                                                                                                         \
		}                                                                                                                                                                                                                                                                                                                      \
                                                                                                                                                                                                                                                                                                                               \
		const char *tab_string = tabs == 3 ? "\t\t\t" : tabs == 2 ? "\t\t" : "\t";                                                                                                                                                                                                                                             \
                                                                                                                                                                                                                                                                                                                               \
		std::printf(color "[%s] " COLOR_DIM_GREY "%s %s:%d%s" color fmt COLOR_RESET "\n", level, timestamp, file_name, line, tab_string, ##__VA_ARGS__);                                                                                                                                                                       \
	} while (0)

#define LOG_ERROR(fmt, ...) LOG_IMPL("E", COLOR_RED, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) LOG_IMPL("W", COLOR_ORANGE, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) LOG_IMPL("I", COLOR_YELLOW, fmt, ##__VA_ARGS__)
#define LOG_DEBUG(fmt, ...) LOG_IMPL("D", COLOR_CYAN, fmt, ##__VA_ARGS__)
#define LOG_TRACE(fmt, ...) LOG_IMPL("T", COLOR_DIM_GREY, fmt, ##__VA_ARGS__)

#ifdef LOGWM
#define LOG_WM(fmt, ...) LOG_IMPL("T", COLOR_DIM_GREY, fmt, ##__VA_ARGS__)
#else
#define LOG_WM(fmt, ...)                                                                                                                                                                                                                                                                                                       \
	do                                                                                                                                                                                                                                                                                                                         \
	{                                                                                                                                                                                                                                                                                                                          \
	} while (0)
#endif

#define LOGLN()                                                                                                                                                                                                                                                                                                                \
	do                                                                                                                                                                                                                                                                                                                         \
	{                                                                                                                                                                                                                                                                                                                          \
		std::printf("\n");                                                                                                                                                                                                                                                                                                     \
	} while (0)

#define START_CHRONO(name) auto name = std::chrono::high_resolution_clock::now()

#define END_CHRONO(name, displayName)                                                                                                                                                                                                                                                                                          \
	do                                                                                                                                                                                                                                                                                                                         \
	{                                                                                                                                                                                                                                                                                                                          \
		auto __chrono_end = std::chrono::high_resolution_clock::now();                                                                                                                                                                                                                                                         \
		auto __elapsed = std::chrono::duration<double, std::milli>(__chrono_end - (name));                                                                                                                                                                                                                                     \
		LOG_DEBUG("%s: %.3f ms", displayName, __elapsed.count());                                                                                                                                                                                                                                                              \
	} while (0)

#define END_CHRONO_MICRO(name, displayName)                                                                                                                                                                                                                                                                                    \
	do                                                                                                                                                                                                                                                                                                                         \
	{                                                                                                                                                                                                                                                                                                                          \
		auto __chrono_end = std::chrono::high_resolution_clock::now();                                                                                                                                                                                                                                                         \
		auto __elapsed = std::chrono::duration<double, std::micro>(__chrono_end - (name));                                                                                                                                                                                                                                     \
		LOG_DEBUG("%s: %.1f us", displayName, __elapsed.count());                                                                                                                                                                                                                                                              \
	} while (0)

#else

template <typename... Args> constexpr void logDisabled(Args &&...) {}

#define LOG_IMPL(...) logDisabled(__VA_ARGS__)

#define LOG_ERROR(...) LOG_IMPL(__VA_ARGS__)
#define LOG_WARN(...) LOG_IMPL(__VA_ARGS__)
#define LOG_INFO(...) LOG_IMPL(__VA_ARGS__)
#define LOG_DEBUG(...) LOG_IMPL(__VA_ARGS__)
#define LOG_TRACE(...) LOG_IMPL(__VA_ARGS__)
#define LOG_WM(...) LOG_IMPL(__VA_ARGS__)

#define LOGLN()                                                                                                                                                                                                                                                                                                                \
	do                                                                                                                                                                                                                                                                                                                         \
	{                                                                                                                                                                                                                                                                                                                          \
	} while (0)

#define START_CHRONO(name)
#define END_CHRONO(name, displayName)
#define END_CHRONO_MICRO(name, displayName)

#endif

#endif // LOGGING_HPP