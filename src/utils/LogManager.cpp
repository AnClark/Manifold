#include "LogManager.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sstream>

// ============================================================
// Internal utilities
// ============================================================

namespace
{

// Format a system_clock time_point as "HH:MM:SS.mmm".
std::string formatTimestamp(const std::chrono::system_clock::time_point& tp)
{
    using namespace std::chrono;

    auto t   = system_clock::to_time_t(tp);
    auto ms  = duration_cast<milliseconds>(tp.time_since_epoch()) % 1000;

    // localtime_s / localtime_r are available on Windows and POSIX respectively.
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                  static_cast<int>(ms.count()));
    return buf;
}

// Convert a string to lower-case for case-insensitive matching.
std::string toLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

} // anonymous namespace

// ============================================================
// Construction
// ============================================================

LogManager::LogManager(size_t capacity)
    : m_capacity(nextPow2(capacity))
    , m_mask(m_capacity - 1)
{
    m_buffer.resize(m_capacity);
}

// ============================================================
// Global default instance  (Meyer's Singleton)
//
// Constructed on the first call using initialCapacity; the argument is
// ignored on subsequent calls.  Local-static initialization is thread-safe
// since C++11 – no additional locking required.
// ============================================================

LogManager& LogManager::instance(size_t initialCapacity)
{
    static LogManager s_instance(initialCapacity);
    return s_instance;
}

// ============================================================
// Logging
// ============================================================

void LogManager::log(LogLevel level, const std::string& message, const std::string& tag)
{
    // Check the level atomically before taking the lock to avoid unnecessary contention.
    if (static_cast<uint8_t>(level) < m_minLevel.load(std::memory_order_relaxed))
        return;

    LogEntry entry;
    entry.level     = level;
    entry.tag       = tag;
    entry.message   = message;
    entry.timestamp = std::chrono::system_clock::now();

    NewLogCallback callbackCopy;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        // Write to the current slot using bitwise addressing.
        m_buffer[m_head & m_mask] = entry;
        m_head++;

        // Increment count while the buffer is not yet full; once full the oldest
        // entry is silently overwritten and count stays at capacity.
        if (m_count < m_capacity)
            m_count++;

        // Bump the write sequence number while still holding the lock so that
        // a UI thread reading through the mutex always sees consistent data.
        m_writeSeq.fetch_add(1, std::memory_order_relaxed);

        callbackCopy = m_callback;  // Copy for invocation outside the lock.
    }

    // Invoke the callback outside the lock to prevent deadlocks (the UI thread
    // may hold its own mutex when processing this notification).
    if (callbackCopy)
        callbackCopy(entry);
}

void LogManager::logf(LogLevel level, const std::string& tag, const char* fmt, ...) noexcept
{
    if (static_cast<uint8_t>(level) < m_minLevel.load(std::memory_order_relaxed))
        return;

    char buf[2048];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    log(level, buf, tag);
}

void LogManager::trace(const std::string& message, const std::string& tag)
{ log(LogLevel::Trace, message, tag); }

void LogManager::debug(const std::string& message, const std::string& tag)
{ log(LogLevel::Debug, message, tag); }

void LogManager::info(const std::string& message, const std::string& tag)
{ log(LogLevel::Info, message, tag); }

void LogManager::warn(const std::string& message, const std::string& tag)
{ log(LogLevel::Warn, message, tag); }

void LogManager::error(const std::string& message, const std::string& tag)
{ log(LogLevel::Error, message, tag); }

void LogManager::fatal(const std::string& message, const std::string& tag)
{ log(LogLevel::Fatal, message, tag); }

// ============================================================
// Control
// ============================================================

void LogManager::setMinLevel(LogLevel level)
{
    m_minLevel.store(static_cast<uint8_t>(level), std::memory_order_relaxed);
}

LogLevel LogManager::getMinLevel() const
{
    return static_cast<LogLevel>(m_minLevel.load(std::memory_order_relaxed));
}

void LogManager::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_head  = 0;
    m_count = 0;
    // No need to zero m_buffer – m_count == 0 is sufficient to mark it empty.
}

// ============================================================
// Queries
// ============================================================

size_t LogManager::size() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count;
}

bool LogManager::empty() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_count == 0;
}

uint64_t LogManager::writeSeq() const noexcept
{
    return m_writeSeq.load(std::memory_order_relaxed);
}

size_t LogManager::capacity() const
{
    return m_capacity;  // Read-only; no locking required.
}

std::vector<LogEntry> LogManager::getAll() const
{
    std::vector<LogEntry> out;
    std::lock_guard<std::mutex> lock(m_mutex);
    copyAllLocked(out);
    return out;
}

std::vector<LogEntry> LogManager::getRecent(size_t n) const
{
    std::lock_guard<std::mutex> lock(m_mutex);

    if (n >= m_count)
    {
        // Requested count exceeds available entries – return everything.
        std::vector<LogEntry> out;
        copyAllLocked(out);
        return out;
    }

    // The n most recent entries occupy the n slots immediately before m_head.
    std::vector<LogEntry> out;
    out.reserve(n);
    size_t start = (m_head - n) & m_mask;
    for (size_t i = 0; i < n; ++i)
        out.push_back(m_buffer[(start + i) & m_mask]);
    return out;
}

std::vector<LogEntry> LogManager::filterByLevel(LogLevel minLevel, LogLevel maxLevel) const
{
    Filter f;
    f.minLevel = minLevel;
    f.maxLevel = maxLevel;
    return filter(f);
}

std::vector<LogEntry> LogManager::filterByKeyword(const std::string& keyword,
                                                   bool caseSensitive) const
{
    Filter f;
    f.keyword       = keyword;
    f.caseSensitive = caseSensitive;
    return filter(f);
}

std::vector<LogEntry> LogManager::filterByTag(const std::string& tag) const
{
    Filter f;
    f.tag = tag;
    return filter(f);
}

std::vector<LogEntry> LogManager::filter(const Filter& f) const
{
    std::vector<LogEntry> all;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        copyAllLocked(all);
    }

    std::vector<LogEntry> result;
    result.reserve(all.size());
    for (const auto& entry : all)
    {
        if (matchesFilter(entry, f))
            result.push_back(entry);
    }
    return result;
}

// ============================================================
// Statistics
// ============================================================

std::array<size_t, 6> LogManager::levelCounts() const
{
    std::array<size_t, 6> counts{};

    std::lock_guard<std::mutex> lock(m_mutex);

    size_t start = (m_head - m_count) & m_mask;
    for (size_t i = 0; i < m_count; ++i)
    {
        const auto& entry = m_buffer[(start + i) & m_mask];
        auto idx = static_cast<size_t>(entry.level);
        if (idx < counts.size())
            counts[idx]++;
    }
    return counts;
}

// ============================================================
// Export
// ============================================================

std::string LogManager::exportToString() const
{
    return exportToString(Filter{});
}

std::string LogManager::exportToString(const Filter& f) const
{
    auto entries = filter(f);

    std::ostringstream oss;
    for (const auto& entry : entries)
    {
        oss << '[' << formatTimestamp(entry.timestamp) << ']'
            << ' ' << '[' << levelToString(entry.level) << ']';

        if (!entry.tag.empty())
            oss << ' ' << '[' << entry.tag << ']';

        oss << ' ' << entry.message << '\n';
    }
    return oss.str();
}

// ============================================================
// Callback
// ============================================================

void LogManager::setNewLogCallback(NewLogCallback callback)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = std::move(callback);
}

void LogManager::clearNewLogCallback()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_callback = nullptr;
}

// ============================================================
// Static utilities
// ============================================================

const char* LogManager::levelToString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace: return "TRACE";
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info:  return "INFO ";
        case LogLevel::Warn:  return "WARN ";
        case LogLevel::Error: return "ERROR";
        case LogLevel::Fatal: return "FATAL";
        default:              return "?????";
    }
}

const char* LogManager::levelToShortString(LogLevel level)
{
    switch (level)
    {
        case LogLevel::Trace: return "TRC";
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
        case LogLevel::Fatal: return "FTL";
        default:              return "???";
    }
}

// ============================================================
// Private methods
// ============================================================

size_t LogManager::nextPow2(size_t n, size_t minVal)
{
    if (n < minVal) n = minVal;
    // Already a power of two – return as-is.
    if ((n & (n - 1)) == 0) return n;

    // Shift up to the next power of two.
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void LogManager::copyAllLocked(std::vector<LogEntry>& out) const
{
    // Caller must hold m_mutex.
    out.reserve(m_count);

    // Slot index of the oldest entry (bitwise wrap).
    size_t start = (m_head - m_count) & m_mask;

    for (size_t i = 0; i < m_count; ++i)
        out.push_back(m_buffer[(start + i) & m_mask]);
}

bool LogManager::matchesFilter(const LogEntry& entry, const Filter& f)
{
    // Minimum level bound.
    if (f.minLevel.has_value() && entry.level < f.minLevel.value())
        return false;

    // Maximum level bound.
    if (f.maxLevel.has_value() && entry.level > f.maxLevel.value())
        return false;

    // Exact tag match.
    if (!f.tag.empty() && entry.tag != f.tag)
        return false;

    // Keyword search in message body.
    if (!f.keyword.empty())
    {
        if (f.caseSensitive)
        {
            if (entry.message.find(f.keyword) == std::string::npos)
                return false;
        }
        else
        {
            if (toLower(entry.message).find(toLower(f.keyword)) == std::string::npos)
                return false;
        }
    }

    return true;
}
