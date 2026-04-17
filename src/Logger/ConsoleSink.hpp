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

	void Write(const LogContext& ecs_context, const std::string& message) override {
		std::ostringstream ss;
		const char* reset = "\033[0m";

		if (m_color) {
			// Timestamp in dark grey
			ss << "\033[90m" << GetTimestamp() << reset << " ";

			// Level badge with color
			ss << LogLevelToAnsi(ecs_context.level)
			   << '[' << std::left << std::setw(7) << LogLevelToString(ecs_context.level) << ']'
			   << reset << ' ';

			// Category tag
			ss << LogCategoryToAnsi(ecs_context.category)
			   << '<' << std::left << std::setw(8) << LogCategoryToString(ecs_context.category) << '>'
			   << reset << ' ';

			// Message
			ss << message;

			// Source location (dimmed)
			if (ecs_context.line > 0)
				ss << " \033[90m(" << ecs_context.file << ':' << ecs_context.line << ")\033[0m";
		} else {
			ss << GetTimestamp() << ' '
			   << '[' << std::left << std::setw(7) << LogLevelToString(ecs_context.level) << "] "
			   << '<' << std::left << std::setw(8) << LogCategoryToString(ecs_context.category) << "> "
			   << message;
			if (ecs_context.line > 0)
				ss << " (" << ecs_context.file << ':' << ecs_context.line << ')';
		}

		static std::mutex s_mx;
		std::lock_guard lk(s_mx);
		// Critical → stderr, rest → stdout
		if (ecs_context.level >= LogLevel::Error)
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
