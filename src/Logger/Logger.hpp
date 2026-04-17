#pragma once
#include "ConsoleSink.hpp"
#include "FileSink.hpp"
#include "LogUtils.hpp"

// Optional SDL3pp log bridge (enabled if SDL3pp is available)
#if __has_include(<SDL3/SDL_log.h>)
#   include <SDL3/SDL_log.h>
#   define LOGGER_HAS_SDL 1
#else
#   define LOGGER_HAS_SDL 0
#endif

namespace core {

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
	void SetCategoryMinLevel(LogCategory cat, LogLevel lv)      noexcept {
		m_catMin[static_cast<size_t>(cat)] = lv;
	}

	[[nodiscard]] bool IsEnabled(LogLevel lv, LogCategory cat = LogCategory::App)
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

#if LOGGER_HAS_SDL
		// Forward to SDL's log system (SDL3pp bridge)
		SDL_LogPriority prio = _ToSDLPriority(ecs_context.level);
		int sdlCat = static_cast<int>(SDL_LOG_CATEGORY_CUSTOM) +
					 static_cast<int>(ecs_context.category);
		SDL_LogMessage(sdlCat, prio, "%s", msg.c_str());
#endif

		std::lock_guard lk(m_mx);
		for (auto& s : m_sinks) s->Write(ecs_context, msg);
	}

	// ── Convenience ──────────────────────────────────────────────────────────

	LogStream Log(LogContext ecs_context) { return LogStream(*this, std::move(ecs_context)); }

	void Separator(LogCategory cat = LogCategory::App) {
		LogContext ecs_context{ LogLevel::None, cat, "", "", 0 };
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

#if LOGGER_HAS_SDL
	/// Install our Logger as the SDL log output function.
	/// After this call, all SDL_Log* calls will appear in our sinks.
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
#endif

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
	LogLevel    m_globalMin = LogLevel::Trace;
	LogLevel    m_globalMax = LogLevel::Critical;
	std::array<LogLevel, static_cast<size_t>(LogCategory::Count)> m_catMin{};

#if LOGGER_HAS_SDL
	static SDL_LogPriority _ToSDLPriority(LogLevel lv) noexcept {
		switch (lv) {
			case LogLevel::Trace:
			case LogLevel::Verbose:  return SDL_LOG_PRIORITY_VERBOSE;
			case LogLevel::Debug:    return SDL_LOG_PRIORITY_DEBUG;
			case LogLevel::Info:     return SDL_LOG_PRIORITY_INFO;
			case LogLevel::Success:  return SDL_LOG_PRIORITY_INFO;
			case LogLevel::Warning:  return SDL_LOG_PRIORITY_WARN;
			case LogLevel::Error:    return SDL_LOG_PRIORITY_ERROR;
			case LogLevel::Critical: return SDL_LOG_PRIORITY_CRITICAL;
			default:                 return SDL_LOG_PRIORITY_INFO;
		}
	}
	static LogLevel _FromSDLPriority(SDL_LogPriority p) noexcept {
		switch (p) {
			case SDL_LOG_PRIORITY_VERBOSE:  return LogLevel::Verbose;
			case SDL_LOG_PRIORITY_DEBUG:    return LogLevel::Debug;
			case SDL_LOG_PRIORITY_INFO:     return LogLevel::Info;
			case SDL_LOG_PRIORITY_WARN:     return LogLevel::Warning;
			case SDL_LOG_PRIORITY_ERROR:    return LogLevel::Error;
			case SDL_LOG_PRIORITY_CRITICAL: return LogLevel::Critical;
			default:                        return LogLevel::Info;
		}
	}
	static LogCategory _FromSDLCategory(int cat) noexcept {
		if (cat == SDL_LOG_CATEGORY_APPLICATION) return LogCategory::App;
		if (cat == SDL_LOG_CATEGORY_ERROR)       return LogCategory::System;
		if (cat == SDL_LOG_CATEGORY_SYSTEM)      return LogCategory::System;
		if (cat == SDL_LOG_CATEGORY_AUDIO)       return LogCategory::Audio;
		if (cat == SDL_LOG_CATEGORY_VIDEO)       return LogCategory::Render;
		if (cat == SDL_LOG_CATEGORY_RENDER)      return LogCategory::Render;
		if (cat == SDL_LOG_CATEGORY_INPUT)       return LogCategory::Input;
		return LogCategory::App;
	}
#endif
};

} // namespace core

// ─────────────────────────────────────────────────────────────────────────────
// Public macros
// ─────────────────────────────────────────────────────────────────────────────

#define LOG_INIT_FILE(dir) \
	core::Logger::Instance().AddSink(std::make_shared<core::FileSink>(dir))

#define LOG_SET_MIN(lv)   core::Logger::Instance().SetMinLevel(lv)
#define LOG_SET_MAX(lv)   core::Logger::Instance().SetMaxLevel(lv)
#define LOG_INSTALL_SDL   core::Logger::BridgeSDLFunction()
#define LOG_SEPARATOR     core::Logger::Instance().Separator()
#define LOG_FLUSH         core::Logger::Instance().Flush()

// Per-category access
#define LOG_CAT(lv, cat)  core::Logger::Instance().Log({lv, cat, __FILE__, __func__, __LINE__})

// Shortcuts
#define LOG_TRACE    LOG_CAT(core::LogLevel::Trace,   core::LogCategory::App)
#define LOG_VERBOSE  LOG_CAT(core::LogLevel::Verbose, core::LogCategory::App)
#define LOG_DEBUG    LOG_CAT(core::LogLevel::Debug,   core::LogCategory::App)
#define LOG_INFO     LOG_CAT(core::LogLevel::Info,    core::LogCategory::App)
#define LOG_SUCCESS  LOG_CAT(core::LogLevel::Success, core::LogCategory::App)
#define LOG_WARNING  LOG_CAT(core::LogLevel::Warning, core::LogCategory::App)
#define LOG_ERROR    LOG_CAT(core::LogLevel::Error,   core::LogCategory::App)
#define LOG_CRITICAL LOG_CAT(core::LogLevel::Critical,core::LogCategory::App)

#define LOG_GAME(lv)     LOG_CAT(lv, core::LogCategory::Game)
#define LOG_RENDER(lv)   LOG_CAT(lv, core::LogCategory::Render)
#define LOG_AUDIO(lv)    LOG_CAT(lv, core::LogCategory::Audio)
#define LOG_INPUT(lv)    LOG_CAT(lv, core::LogCategory::Input)
#define LOG_RESOURCE(lv) LOG_CAT(lv, core::LogCategory::Resource)
#define LOG_UI(lv)       LOG_CAT(lv, core::LogCategory::UI)

// Scope timer macro: logs duration on scope exit
#define LOG_SCOPE_TIMER(name) \
	core::ScopeTimer _scopeTimer_##__LINE__(name); \
	struct _ScopeTimerGuard_##__LINE__ { \
		const core::ScopeTimer& t; \
		_ScopeTimerGuard_##__LINE__(const core::ScopeTimer& st) : t(st) {} \
		~_ScopeTimerGuard_##__LINE__() { \
			LOG_CAT(t.Level(), core::LogCategory::System) \
				<< t.Name() << " took " << t.ElapsedMs() << " ms"; \
		} \
	} _scopeGuard_##__LINE__(_scopeTimer_##__LINE__)
