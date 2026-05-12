#pragma once
#include "ConsoleSink.h"
#include "FileSink.h"
#include "LogUtils.h"

// Optional SDL3pp log bridge (enabled if SDL3pp is available)
#include "../SDL3pp_log.h"

namespace SDL {
namespace LOG {

// ─────────────────────────────────────────────────────────────────────────────
// LogStream — deferred RAII message builder flushed on destruction
// ─────────────────────────────────────────────────────────────────────────────

class LogStream {
public:
	LogStream(ILogger& logger, LogContext ecs_context)
		: m_logger(logger), m_ctx(std::move(ecs_context)) {}

	~LogStream() {
		if (m_logger.IsEnabled(m_ctx.level, m_ctx.category))
			m_logger.Dispatch(m_ctx, m_buf.str());
	}

	template<typename T>
	LogStream& operator<<(const T& v) { m_buf << v; return *this; }
	LogStream& operator<<(std::ostream& (*fn)(std::ostream&)) { m_buf << fn; return *this; }
	LogStream& operator<<(const std::exception& ex) { m_buf << "Exception: " << ex.what(); return *this; }

private:
	ILogger&           m_logger;
	LogContext         m_ctx;
	std::ostringstream m_buf;
};

// ─────────────────────────────────────────────────────────────────────────────
// Logger — singleton, multi-sink, per-category level filtering
// ─────────────────────────────────────────────────────────────────────────────

class Logger : public ILogger {
public:
	static Logger& Instance() {
		static Logger inst;
		return inst;
	}

	// ── Sink management ──────────────────────────────────────────────────────

	void AddSink(std::shared_ptr<ILogSink> sink) override {
		std::lock_guard lk(m_mx);
		m_sinks.push_back(std::move(sink));
	}

	void ClearSinks() {
		std::lock_guard lk(m_mx);
		m_sinks.clear();
	}

	// ── Level filtering ──────────────────────────────────────────────────────

	void SetMinLevel(LogLevel lv)                               noexcept { m_globalMin = lv; }
	void SetMaxLevel(LogLevel lv)                               noexcept { m_globalMax = lv; }
	void SetCategoryMinLevel(SDL::LOG::LogCategory cat, LogLevel lv)      noexcept {
		m_catMin[static_cast<size_t>(cat)] = lv;
	}

	[[nodiscard]] bool IsEnabled(LogLevel lv, SDL::LOG::LogCategory cat = SDL::LOG::LogCategory::App)
												 const noexcept override {
		if (lv == LogLevel::None) return true;
		LogLevel catMin = m_catMin[static_cast<size_t>(cat)];
		return lv >= catMin && lv >= m_globalMin && lv <= m_globalMax;
	}

	// ── Dispatch ─────────────────────────────────────────────────────────────

	void Dispatch(const LogContext& ecs_context, const std::string& msg) override {
		// Push to ring buffer (always)
		LogEntry entry{ ecs_context, msg, GetTimestamp() };
		m_ring.Push(entry);

		// Forward to SDL's log system (SDL3pp bridge)
		SDL::LogPriority prio = _ToSDLPriority(ecs_context.level);
		int sdlCat = static_cast<int>(SDL::LOG_CATEGORY_CUSTOM) +
					 static_cast<int>(ecs_context.category);
		SDL::LogMessage(static_cast<SDL::LogCategory>(sdlCat), prio, "%s", msg.c_str());
		std::lock_guard lk(m_mx);
		for (auto& s : m_sinks) s->Write(ecs_context, msg);
	}

	// ── Convenience ──────────────────────────────────────────────────────────

	LogStream Log(LogContext ecs_context) { return LogStream(*this, std::move(ecs_context)); }

	void Separator(SDL::LOG::LogCategory cat = SDL::LOG::LogCategory::App) {
		LogContext ecs_context{ SDL::LOG::LogLevel::None, cat, "", "", 0 };
		std::string line(LOG_SEPARATOR_SIZE, '-');
		Dispatch(ecs_context, line);
	}

	void Flush() {
		std::lock_guard lk(m_mx);
		for (auto& s : m_sinks) s->Flush();
	}

	// ── Ring buffer access ────────────────────────────────────────────────────

	[[nodiscard]] LogRingBuffer& GetRingBuffer() noexcept { return m_ring; }
	[[nodiscard]] const LogRingBuffer& GetRingBuffer() const noexcept { return m_ring; }

	// ── SDL log callback installation ────────────────────────────────────────

	/// Install our Logger as the SDL log output function.
	/// After this call, all SDL::Log* calls will appear in our sinks.
	static void BridgeSDLFunction() {
		SDL::SetLogOutputFunction([](void*, int cat, SDL::LogPriority prio, const char* msg) {
			LogContext ecs_context;
			ecs_context.level    = _FromSDLPriority(prio);
			ecs_context.category = _FromSDLCategory(cat);
			ecs_context.file     = "SDL";
			ecs_context.line     = 0;
			Logger::Instance().Dispatch(ecs_context, msg);
		}, nullptr);
	}
	
private:
	Logger() {
		// Default: console sink enabled
		m_sinks.push_back(std::make_shared<ConsoleSink>(true));
		m_catMin.fill(LogLevel::Trace); // enable all categories by default
	}
	Logger(const Logger&)            = delete;
	Logger& operator=(const Logger&) = delete;

	std::vector<std::shared_ptr<ILogSink>> m_sinks;
	std::mutex  m_mx;
	LogRingBuffer m_ring;
	SDL::LOG::LogLevel    m_globalMin = SDL::LOG::LogLevel::Trace;
	SDL::LOG::LogLevel    m_globalMax = SDL::LOG::LogLevel::Critical;
	std::array<SDL::LOG::LogLevel, static_cast<size_t>(SDL::LOG::LogCategory::Count)> m_catMin{};

	static SDL::LogPriority _ToSDLPriority(LogLevel lv) noexcept {
		switch (lv) {
			case LogLevel::Trace:
			case LogLevel::Verbose:  return SDL::LOG_PRIORITY_VERBOSE;
			case LogLevel::Debug:    return SDL::LOG_PRIORITY_DEBUG;
			case LogLevel::Info:     return SDL::LOG_PRIORITY_INFO;
			case LogLevel::Success:  return SDL::LOG_PRIORITY_INFO;
			case LogLevel::Warning:  return SDL::LOG_PRIORITY_WARN;
			case LogLevel::Error:    return SDL::LOG_PRIORITY_ERROR;
			case LogLevel::Critical: return SDL::LOG_PRIORITY_CRITICAL;
			default:                 return SDL::LOG_PRIORITY_INFO;
		}
	}
	static SDL::LOG::LogLevel _FromSDLPriority(SDL::LogPriority p) noexcept {
		switch (p) {
			case SDL::LOG_PRIORITY_VERBOSE:  return SDL::LOG::LogLevel::Verbose;
			case SDL::LOG_PRIORITY_DEBUG:    return SDL::LOG::LogLevel::Debug;
			case SDL::LOG_PRIORITY_INFO:     return SDL::LOG::LogLevel::Info;
			case SDL::LOG_PRIORITY_WARN:     return SDL::LOG::LogLevel::Warning;
			case SDL::LOG_PRIORITY_ERROR:    return SDL::LOG::LogLevel::Error;
			case SDL::LOG_PRIORITY_CRITICAL: return SDL::LOG::LogLevel::Critical;
			default:                         return SDL::LOG::LogLevel::Info;
		}
	}
	static SDL::LOG::LogCategory _FromSDLCategory(int cat) noexcept {
		if (cat == SDL::LOG_CATEGORY_APPLICATION) return SDL::LOG::LogCategory::App;
		if (cat == SDL::LOG_CATEGORY_ERROR)       return SDL::LOG::LogCategory::System;
		if (cat == SDL::LOG_CATEGORY_SYSTEM)      return SDL::LOG::LogCategory::System;
		if (cat == SDL::LOG_CATEGORY_AUDIO)       return SDL::LOG::LogCategory::Audio;
		if (cat == SDL::LOG_CATEGORY_VIDEO)       return SDL::LOG::LogCategory::Render;
		if (cat == SDL::LOG_CATEGORY_RENDER)      return SDL::LOG::LogCategory::Render;
		if (cat == SDL::LOG_CATEGORY_INPUT)       return SDL::LOG::LogCategory::Input;
		return SDL::LOG::LogCategory::App;
	}
};

} // namespace LOG
} // namespace SDL

// ─────────────────────────────────────────────────────────────────────────────
// Public macros
// ─────────────────────────────────────────────────────────────────────────────

#define LOG_INIT_FILE(dir) \
	SDL::LOG::Logger::Instance().AddSink(std::make_shared<SDL::LOG::FileSink>(dir))

#define LOG_SET_MIN(lv)   SDL::LOG::Logger::Instance().SetMinLevel(lv)
#define LOG_SET_MAX(lv)   SDL::LOG::Logger::Instance().SetMaxLevel(lv)
#define LOG_INSTALL_SDL   SDL::LOG::Logger::BridgeSDLFunction()
#define LOG_SEPARATOR     SDL::LOG::Logger::Instance().Separator()
#define LOG_FLUSH         SDL::LOG::Logger::Instance().Flush()

// Per-category access
#define LOG_CAT(lv, cat)  SDL::LOG::Logger::Instance().Log({lv, cat, __FILE__, __func__, __LINE__})

// Shortcuts
#define LOG_TRACE    LOG_CAT(SDL::LOG::LogLevel::Trace,   SDL::LOG::LogCategory::App)
#define LOG_VERBOSE  LOG_CAT(SDL::LOG::LogLevel::Verbose, SDL::LOG::LogCategory::App)
#define LOG_DEBUG    LOG_CAT(SDL::LOG::LogLevel::Debug,   SDL::LOG::LogCategory::App)
#define LOG_INFO     LOG_CAT(SDL::LOG::LogLevel::Info,    SDL::LOG::LogCategory::App)
#define LOG_SUCCESS  LOG_CAT(SDL::LOG::LogLevel::Success, SDL::LOG::LogCategory::App)
#define LOG_WARNING  LOG_CAT(SDL::LOG::LogLevel::Warning, SDL::LOG::LogCategory::App)
#define LOG_ERROR    LOG_CAT(SDL::LOG::LogLevel::Error,   SDL::LOG::LogCategory::App)
#define LOG_CRITICAL LOG_CAT(SDL::LOG::LogLevel::Critical,SDL::LOG::LogCategory::App)

#define LOG_GAME(lv)     LOG_CAT(lv, SDL::LOG::LogCategory::Game)
#define LOG_RENDER(lv)   LOG_CAT(lv, SDL::LOG::LogCategory::Render)
#define LOG_AUDIO(lv)    LOG_CAT(lv, SDL::LOG::LogCategory::Audio)
#define LOG_INPUT(lv)    LOG_CAT(lv, SDL::LOG::LogCategory::Input)
#define LOG_RESOURCE(lv) LOG_CAT(lv, SDL::LOG::LogCategory::Resource)
#define LOG_UI(lv)       LOG_CAT(lv, SDL::LOG::LogCategory::UI)

// Scope timer macro: logs duration on scope exit
#define LOG_SCOPE_TIMER(name) \
	ScopeTimer _scopeTimer_##__LINE__(name); \
	struct _ScopeTimerGuard_##__LINE__ { \
		const ScopeTimer& t; \
		_ScopeTimerGuard_##__LINE__(const ScopeTimer& st) : t(st) {} \
		~_ScopeTimerGuard_##__LINE__() { \
			LOG_CAT(t.Level(), SDL::LOG::LogCategory::System) \
				<< t.Name() << " took " << t.ElapsedMs() << " ms"; \
		} \
	} _scopeGuard_##__LINE__(_scopeTimer_##__LINE__)
