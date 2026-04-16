#pragma once
#include "LogUtils.hpp"
#include <filesystem>
#include <fstream>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// FileSink — plain-text file output with session-timestamped filename
// ─────────────────────────────────────────────────────────────────────────────

class FileSink : public ILogSink {
public:
	static constexpr size_t kDefaultMaxBytes = 10 * 1024 * 1024; // 10 MB

	/// `directory` is created if it does not exist.
	/// The log file is named `session_YYYY-MM-DD_HH-MM-SS.log`.
	explicit FileSink(const std::string& directory = std::string(LOG_DEFAULT_DIR),
					  size_t maxBytes = kDefaultMaxBytes)
		: m_maxBytes(maxBytes), m_written(0)
	{
		std::filesystem::create_directories(directory);

		using namespace std::chrono;
		auto tt = system_clock::to_time_t(system_clock::now());
		std::tm tmBuf{};
#if defined(_WIN32)
		localtime_s(&tmBuf, &tt);
#else
		localtime_r(&tt, &tmBuf);
#endif
		std::ostringstream ss;
		ss << directory << "/session_"
		   << std::put_time(&tmBuf, "%Y-%m-%d_%H-%M-%S") << ".log";
		m_path = ss.str();
	}

	~FileSink() override {
		if (m_stream.is_open()) m_stream.close();
	}

	void Write(const LogContext& ctx, const std::string& message) override {
		if (!_EnsureOpen()) return;

		std::ostringstream line;
		if (ctx.level != LogLevel::None) {
			line << GetTimestamp()
				 << " [" << LogLevelToString(ctx.level) << "] "
				 << '<' << LogCategoryToString(ctx.category) << "> ";
			if (ctx.line > 0)
				line << ctx.file << ':' << ctx.line << " | ";
		}
		line << message << '\n';

		std::string s = line.str();
		{
			std::lock_guard lk(m_mx);
			m_stream << s;
			m_written += s.size();
			// Rotate when file exceeds size limit
			if (m_written >= m_maxBytes) _Rotate();
		}
	}

	void Flush() override {
		std::lock_guard lk(m_mx);
		if (m_stream.is_open()) m_stream.flush();
	}

	[[nodiscard]] const std::string& GetPath() const noexcept { return m_path; }

private:
	bool _EnsureOpen() {
		if (!m_stream.is_open())
			m_stream.open(m_path, std::ios::app);
		return m_stream.is_open();
	}

	void _Rotate() {
		// Close current file and start a new one with incremented suffix
		m_stream.close();
		m_written = 0;
		m_rotations++;
		// Insert rotation index before extension
		auto dot = m_path.rfind('.');
		if (dot != std::string::npos)
			m_path.insert(dot, '.' + std::to_string(m_rotations));
		else
			m_path += '.' + std::to_string(m_rotations);
		m_stream.open(m_path, std::ios::app);
	}

	std::string   m_path;
	std::ofstream m_stream;
	std::mutex    m_mx;
	size_t        m_maxBytes;
	size_t        m_written;
	int           m_rotations = 0;
};

} // namespace core
