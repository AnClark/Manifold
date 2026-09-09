#pragma once

#include "base/ProcessingRun.hpp"

#include <chrono>
#include <string>

/**
 * @file TimeStamp.hpp
 * @brief Utility helpers for formatting processing run time points and elapsed durations.
 *
 * This header provides small string-formatting helpers used to produce readable
 * timestamps and wall-clock duration summaries from the repository's
 * `RunTimePoint` type alias.
 */

namespace TimestampUtils
{
/**
 * @brief Format a `RunTimePoint` as a human-readable timestamp.
 *
 * Converts a `RunTimePoint` from the system clock into a string with the
 * format `YYYY-MM-DD  HH:MM:SS`.
 *
 * @param tp The time point to translate into a local wall-clock timestamp.
 * @return A string containing the formatted date and time.
 */
static std::string fmtRunTimestamp(const RunTimePoint& tp)
{
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

/**
 * @brief Format the elapsed wall-clock duration between two time points.
 *
 * Computes the number of whole seconds between `start` and `end` and presents
 * the result in a compact human-readable form. Values below one minute are
 * shown as `Xs`, and longer durations are shown as `Xm Ys`.
 *
 * @param start The starting time point.
 * @param end The ending time point.
 * @return A duration string such as `12s` or `1m 05s`.
 */
static std::string fmtElapsed(const RunTimePoint& start, const RunTimePoint& end)
{
    using namespace std::chrono;
    auto secs = duration_cast<seconds>(end - start).count();
    if (secs < 60)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(secs));
        return buf;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%lldm %llds",
                  static_cast<long long>(secs / 60),
                  static_cast<long long>(secs % 60));
    return buf;
}
}
