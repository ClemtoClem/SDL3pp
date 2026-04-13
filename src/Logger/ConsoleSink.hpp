#pragma once
#include "LogUtils.hpp"
#include <iostream>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// ConsoleSink — thread-safe colored terminal output
// ─────────────────────────────────────────────────────────────────────────────

class ConsoleSink : public ILogSink {
public:
    /// If `colorEnabled` is false, ANSI codes are omitted (useful for piped output).
    explicit ConsoleSink(bool colorEnabled = true) : m_color(colorEnabled) {}

    void Write(const LogContext& ctx, const std::string& message) override {
        std::ostringstream ss;
        const char* reset = "\033[0m";

        if (m_color) {
            // Timestamp in dark grey
            ss << "\033[90m" << GetTimestamp() << reset << " ";

            // Level badge with color
            ss << LogLevelToAnsi(ctx.level)
               << '[' << std::left << std::setw(7) << LogLevelToString(ctx.level) << ']'
               << reset << ' ';

            // Category tag
            ss << LogCategoryToAnsi(ctx.category)
               << '<' << std::left << std::setw(8) << LogCategoryToString(ctx.category) << '>'
               << reset << ' ';

            // Message
            ss << message;

            // Source location (dimmed)
            if (ctx.line > 0)
                ss << " \033[90m(" << ctx.file << ':' << ctx.line << ")\033[0m";
        } else {
            ss << GetTimestamp() << ' '
               << '[' << std::left << std::setw(7) << LogLevelToString(ctx.level) << "] "
               << '<' << std::left << std::setw(8) << LogCategoryToString(ctx.category) << "> "
               << message;
            if (ctx.line > 0)
                ss << " (" << ctx.file << ':' << ctx.line << ')';
        }

        static std::mutex s_mx;
        std::lock_guard lk(s_mx);
        // Critical → stderr, rest → stdout
        if (ctx.level >= LogLevel::Error)
            std::cerr << ss.str() << '\n';
        else
            std::cout << ss.str() << '\n';
    }

    void Flush() override {
        std::cout.flush();
        std::cerr.flush();
    }

private:
    bool m_color;
};

} // namespace core
