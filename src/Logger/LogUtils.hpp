#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <deque>
#include <iomanip>
#include <memory>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr std::string_view LOG_DEFAULT_DIR  = "logs";
inline constexpr int              LOG_SEPARATOR_SIZE = 60;
inline constexpr size_t           LOG_RING_CAPACITY  = 256;

// ─────────────────────────────────────────────────────────────────────────────
// LogLevel
// ─────────────────────────────────────────────────────────────────────────────

enum class LogLevel : uint8_t {
    None = 0,
    Trace,
    Verbose,
    Debug,
    Info,
    Success,
    Warning,
    Error,
    Critical
};

// ─────────────────────────────────────────────────────────────────────────────
// LogCategory — mirrors SDL log categories for bridge compatibility
// ─────────────────────────────────────────────────────────────────────────────

enum class LogCategory : uint8_t {
    App = 0,
    Game,
    Render,
    Audio,
    Input,
    Resource,
    System,
    UI,
    Count
};

// ─────────────────────────────────────────────────────────────────────────────
// LogContext — carries source metadata for each message
// ─────────────────────────────────────────────────────────────────────────────

struct LogContext {
    LogLevel    level    = LogLevel::Info;
    LogCategory category = LogCategory::App;
    const char* file     = "";
    const char* function = "";
    int         line     = 0;

    static LogContext Make(LogLevel lv, LogCategory cat,
                           std::source_location loc = std::source_location::current()) noexcept {
        return { lv, cat, loc.file_name(), loc.function_name(), (int)loc.line() };
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// LogEntry — storable log record (used by ring buffer & JSON sink)
// ─────────────────────────────────────────────────────────────────────────────

struct LogEntry {
    LogContext  ctx;
    std::string message;
    std::string timestamp;
};

// ─────────────────────────────────────────────────────────────────────────────
// Formatting helpers
// ─────────────────────────────────────────────────────────────────────────────

inline std::string_view LogLevelToString(LogLevel level) noexcept {
    constexpr std::array<std::string_view, 9> kNames = {
        "NONE", "TRACE", "VERBOSE", "DEBUG",
        "INFO", "SUCCESS", "WARNING", "ERROR", "CRITICAL"
    };
    auto idx = static_cast<size_t>(level);
    return idx < kNames.size() ? kNames[idx] : "UNKNOWN";
}

inline std::string_view LogCategoryToString(LogCategory cat) noexcept {
    constexpr std::array<std::string_view, static_cast<size_t>(LogCategory::Count)> kNames = {
        "App","Game","Render","Audio","Input","Resource","System","UI"
    };
    auto idx = static_cast<size_t>(cat);
    return idx < kNames.size() ? kNames[idx] : "?";
}

inline std::string_view LogLevelToAnsi(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace:    return "\033[90m";       // dark grey
        case LogLevel::Verbose:  return "\033[36m";       // cyan
        case LogLevel::Debug:    return "\033[94m";       // bright blue
        case LogLevel::Info:     return "\033[34m";       // blue
        case LogLevel::Success:  return "\033[32m";       // green
        case LogLevel::Warning:  return "\033[33m";       // yellow
        case LogLevel::Error:    return "\033[31m";       // red
        case LogLevel::Critical: return "\033[41;97m";    // white on red
        default:                 return "\033[0m";
    }
}

inline std::string_view LogCategoryToAnsi(LogCategory cat) noexcept {
    constexpr std::array<std::string_view,
                         static_cast<size_t>(LogCategory::Count)> kColors = {
        "\033[37m",   // App     white
        "\033[92m",   // Game    bright green
        "\033[95m",   // Render  bright magenta
        "\033[96m",   // Audio   bright cyan
        "\033[93m",   // Input   bright yellow
        "\033[33m",   // Resource yellow
        "\033[97m",   // System  bright white
        "\033[34m",   // UI      blue
    };
    auto idx = static_cast<size_t>(cat);
    return idx < kColors.size() ? kColors[idx] : "\033[0m";
}

/// Thread-safe timestamp with millisecond precision.
inline std::string GetTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    auto tt  = system_clock::to_time_t(now);
    auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &tt);
#else
    localtime_r(&tt, &tmBuf);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tmBuf, "%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// LogRingBuffer — thread-safe circular buffer for in-game log overlay
// ─────────────────────────────────────────────────────────────────────────────

class LogRingBuffer {
public:
    explicit LogRingBuffer(size_t capacity = LOG_RING_CAPACITY)
        : m_capacity(capacity) {}

    void Push(const LogEntry& e) {
        std::lock_guard lk(m_mx);
        if (m_buf.size() >= m_capacity) m_buf.pop_front();
        m_buf.push_back(e);
    }

    /// Snapshot of the last `n` entries (oldest first).
    [[nodiscard]] std::vector<LogEntry> GetRecent(size_t n = 16) const {
        std::lock_guard lk(m_mx);
        size_t count = std::min(n, m_buf.size());
        std::vector<LogEntry> out;
        out.reserve(count);
        auto it = m_buf.end() - (std::ptrdiff_t)count;
        out.assign(it, m_buf.end());
        return out;
    }

    void Clear() { std::lock_guard lk(m_mx); m_buf.clear(); }
    [[nodiscard]] size_t Size() const { std::lock_guard lk(m_mx); return m_buf.size(); }

private:
    mutable std::mutex  m_mx;
    std::deque<LogEntry> m_buf;
    size_t               m_capacity;
};

// ─────────────────────────────────────────────────────────────────────────────
// ScopeTimer — RAII scope-duration profiler
// ─────────────────────────────────────────────────────────────────────────────

class ScopeTimer {
public:
    using Clock = std::chrono::high_resolution_clock;

    explicit ScopeTimer(std::string name, LogLevel lv = LogLevel::Debug)
        : m_name(std::move(name)), m_level(lv), m_start(Clock::now()) {}

    ~ScopeTimer() {
        using namespace std::chrono;
        m_elapsedUs = duration_cast<microseconds>(Clock::now() - m_start).count();
    }

    [[nodiscard]] const std::string& Name()        const noexcept { return m_name; }
    [[nodiscard]] LogLevel           Level()       const noexcept { return m_level; }
    [[nodiscard]] long long          ElapsedUs()   const noexcept { return m_elapsedUs; }
    [[nodiscard]] double             ElapsedMs()   const noexcept { return m_elapsedUs / 1000.0; }

private:
    std::string  m_name;
    LogLevel     m_level;
    Clock::time_point m_start;
    mutable long long m_elapsedUs = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ILogSink & ILogger interfaces
// ─────────────────────────────────────────────────────────────────────────────

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void Write(const LogContext& ctx, const std::string& msg) = 0;
    virtual void Flush() {}
};

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void AddSink(std::shared_ptr<ILogSink> sink)                      = 0;
    virtual bool IsEnabled(LogLevel lv, LogCategory cat = LogCategory::App)
                                                               const noexcept = 0;
    virtual void Dispatch(const LogContext& ctx, const std::string& msg)      = 0;
};

} // namespace core
