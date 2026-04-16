// ─────────────────────────────────────────────────────────────────────────────
// Polyadventure — C++20 SDL3pp rewrite
// main.cpp — SDL3pp callback entry point (inspired by 12_scene_graph.cpp and
//             13_ui.cpp from the SDL3pp example suite)
//
// Pattern: struct Main owns all RAII resources as members.  SDL3pp dispatches
// the four lifecycle callbacks through SDL3PP_DEFINE_CALLBACKS(Main):
//
//   SDL_AppInit    → Main::Init   (static) — SDL subsystem init + new Main
//   SDL_AppIterate → Main::Iterate()       — per-frame update + render
//   SDL_AppEvent   → Main::Event()         — one call per SDL event
//   SDL_AppQuit    → Main::Quit   (static) — delete Main + SDL subsystem quit
// ─────────────────────────────────────────────────────────────────────────────

#include <SDL3pp/SDL3pp.h>
#define SDL3PP_MAIN_USE_CALLBACKS 1   // must precede ALL SDL3pp includes
#include <SDL3pp/SDL3pp_main.h>       // registers SDL_AppInit/Iterate/Event/Quit
#include <SDL3pp/SDL3pp_image.h>      // IMG_LoadTexture  (no explicit IMG_Init needed in SDL3)
#include <SDL3pp/SDL3pp_resources.h>  // ResourceManager / ResourcePool
#include <SDL3pp/SDL3pp_ui.h>         // UI::System + Builder DSL
#include <SDL3pp/SDL3pp_scene.h>      // SceneBuilder / SceneGraph
#include <SDL3pp/SDL3pp_timer.h>      // FrameTimer

#include "Logger/Logger.hpp"
#include "Core/ScriptParser.hpp"

// ── State headers — include in dependency order ───────────────────────────────
// Each header only *declares* its deferred _GoToXxx() / _StartGame() method.
// The definitions appear below, after all types are complete.
#include "States/IState.hpp"
#include "States/PrefaceState.hpp"
#include "States/MenuState.hpp"
#include "States/GameState.hpp"

#include <algorithm>
#include <format>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Deferred state-transition definitions
// (All state types are complete here, so std::make_unique<T>() is valid.)
// ─────────────────────────────────────────────────────────────────────────────

namespace game {

void PrefaceState::_GoToMenu() {
	if (m_ctx) m_ctx->switchState(std::make_unique<MenuState>());
}
void MenuState::_StartGame(bool loadSave) {
	if (!m_ctx) return;
	// If "Nouvelle Partie" is chosen, wipe any existing save first.
	if (!loadSave && !m_ctx->savePath.empty())
		core::SaveManager(m_ctx->savePath).Delete();
	m_ctx->switchState(std::make_unique<GameState>());
}
void GameState::_GoToMenu() {
	if (m_ctx) m_ctx->switchState(std::make_unique<MenuState>());
}

} // namespace game

// ─────────────────────────────────────────────────────────────────────────────
// Palette — shared colours used across the Main HUD overlay
// ─────────────────────────────────────────────────────────────────────────────

namespace pal {
	constexpr SDL::Color BG      = {  0,   0,   0, 255 };
	constexpr SDL::Color FPS_COL = { 90,  90,  90, 200 };
}

// ─────────────────────────────────────────────────────────────────────────────
// Main — SDL3pp application struct
// ─────────────────────────────────────────────────────────────────────────────

struct Main {

	// =========================================================================
	// Constants
	// =========================================================================

	static constexpr SDL::Point kDefaultSize   = {860, 640};
	static constexpr const char* kAppId        = "com.sdl3pp.polyadventure";
	static constexpr const char* kAppVersion   = "0.3.0";
	static constexpr const char* kDefaultTitle = "POLYADVENTURE";

	// =========================================================================
	// Statics
	// =========================================================================

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::SetAppMetadata(kDefaultTitle, kAppVersion, kAppId);
		SDL::Init(SDL::INIT_VIDEO | SDL::INIT_GAMEPAD);
		SDL::TTF::Init();
		SDL::MIX::Init();
		*out = new Main(args);
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult /*result*/) {
		delete m;           // ~Main() handles RAII cleanup of game resources
		SDL::MIX::Quit();
		SDL::TTF::Quit();
		SDL::Quit();
	}

	static SDL::Window _MakeWindow() {
		return SDL::CreateWindowAndRenderer(kDefaultTitle, kDefaultSize,
											 SDL_WINDOW_RESIZABLE, nullptr);
	}

	SDL::Window      window  { _MakeWindow()         };
	SDL::RendererRef renderer{ window.GetRenderer()  };

	// ── Asset pool shared across all states ───────────────────────────────────
	SDL::ResourceManager  rm;
	SDL::ResourcePool&    pool { *rm.CreatePool("shared") };

	// ── State machine ─────────────────────────────────────────────────────────
	std::unique_ptr<game::IState> m_current;
	std::unique_ptr<game::IState> m_next;
	bool                          m_wantsQuit = false;

	// ── AppContext (shared with every state) ──────────────────────────────────
	game::AppContext m_ctx;

	// ── Frame timing (target 60 fps, capped delta-time) ───────────────────────
	SDL::FrameTimer m_timer { 60.f };

	// =========================================================================
	// Constructor
	// =========================================================================

	Main(SDL::AppArgs args) {
		// ── Logger ─────────────────────────────────────────────────────────────
		LOG_INIT_FILE("logs");
		core::LogLevel level = core::LogLevel::Info;
		for (auto arg : args) {
			if (arg == "--debug") {
				level = core::LogLevel::Debug;
				break;
			} else if (arg == "--trace") {
				level = core::LogLevel::Trace;
				break;
			}
		}
		core::Logger::Instance().SetMinLevel(level);
#if LOGGER_HAS_SDL
		core::Logger::BridgeSDLFunction();
#endif
		LOG_SEPARATOR;
		LOG_INFO << std::format("=== {} {} ===", kDefaultTitle, kAppVersion);

		// ── Config ─────────────────────────────────────────────────────────────
		const std::string assetsBase = std::string(SDL::GetBasePath()) + "assets/";
		core::ScriptSectionPtr config;
		try {
			config = core::ParseConfFile(assetsBase + "configs/config.script");
			LOG_SUCCESS << "config.script loaded";
		} catch (const std::exception& e) {
			LOG_WARNING << "config.script: " << e.what() << " — using defaults";
			config = std::make_shared<core::ScriptSection>();
		}

		// ── Apply window settings from config ──────────────────────────────────
		{
			int w = config->count("screenWidth")
					? (*config)["screenWidth"].AsInt(kDefaultSize.x)   : kDefaultSize.x;
			int h = config->count("screenHeight")
					? (*config)["screenHeight"].AsInt(kDefaultSize.y) : kDefaultSize.y;
			std::string title = config->count("screenTitle")
					? (*config)["screenTitle"].AsString(kDefaultTitle) : kDefaultTitle;

			window.SetSize({w, h});
			window.SetPosition({SDL::WINDOWPOS_CENTERED, SDL::WINDOWPOS_CENTERED});
			window.SetTitle(title.c_str());
			//renderer.SetVSync(1);
			renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
			LOG_INFO << std::format("Window {}x{}  '{}'", w, h, title);
		}

		// ── Wire AppContext ─────────────────────────────────────────────────────
		m_ctx.renderer    = renderer;
		m_ctx.pool        = &pool;
		m_ctx.config      = config;
		m_ctx.assetsBasePath = assetsBase;
		m_ctx.savePath    = assetsBase + "saves/save.dat";

		m_ctx.quit = [this] {
			m_wantsQuit = true;
		};
		m_ctx.switchState = [this](std::unique_ptr<game::IState> s) {
			m_next = std::move(s);
		};

		// ── Boot into PrefaceState ──────────────────────────────────────────────
		m_next = std::make_unique<game::PrefaceState>();

		m_timer.Begin();

		LOG_SUCCESS << "Main complete — entering event loop";
		LOG_SEPARATOR;
	}

	// =========================================================================
	// Destructor
	// =========================================================================

	~Main() {
		if (m_current) m_current->Leave();
		m_current.reset();
		m_next.reset();
		rm.ReleaseAll();

		LOG_INFO << "Shutdown complete";
		LOG_SEPARATOR;
		LOG_FLUSH;
	}

	// =========================================================================
	// SDL_AppIterate — one call per frame
	// =========================================================================

	SDL::AppResult Iterate() {
		// ── Frame timing ────────────────────────────────────────────────────────
		m_timer.Begin();

		// ── Deferred quit ───────────────────────────────────────────────────────
		if (m_wantsQuit) return SDL::APP_SUCCESS;

		// ── Update resource loading (async or sync) ─────────────────────────────
		rm.UpdateAll();   // update resource loading (async or sync)

		// ── State transition ────────────────────────────────────────────────────
		if (m_next) {
			if (m_current) m_current->Leave();
			m_current = std::move(m_next);
			m_current->Enter(m_ctx);
		}
		if (!m_current) return SDL::APP_SUCCESS;

		// ── Delta-time (capped — prevents physics explosion after app suspend) ──
		const float dt = std::clamp(m_timer.GetDelta(), 0.001f, 0.05f);

		// ── Render ──────────────────────────────────────────────────────────────
		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();

		m_current->Update(dt);   // game logic
		m_current->Render(dt);   // calls m_ui->Iterate(dt) internally

		//_DrawOverlay();          // FPS counter, always on top

		renderer.Present();
		m_timer.End();           // sleep if frame ran faster than target FPS
		return SDL::APP_CONTINUE;
	}

	// =========================================================================
	// SDL_AppEvent — one call per queued SDL event
	// =========================================================================

	SDL::AppResult Event(const SDL::Event& ev) {
		// Global quit shortcut (SDL window close button)
		if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;

		// Deferred quit already requested
		if (m_wantsQuit) return SDL::APP_SUCCESS;

		// Delegate to the current state; false return ≡ state requests quit
		if (m_current && !m_current->HandleEvent(ev))
			return SDL::APP_SUCCESS;

		return SDL::APP_CONTINUE;
	}


	// ── Minimal FPS / time overlay (top-right corner, always visible) ─────────
	void _DrawOverlay() {
		const float fps = m_timer.GetFPS();
		if (fps <= 0.f) return;   // not yet measured (first second)

		renderer.SetDrawColor(pal::FPS_COL);

		const std::string fpsStr  = std::format("{:.0f} fps", fps);
		const std::string timeStr = std::format("{:.1f} s", m_timer.GetTime());

		const SDL::Point sz = renderer.GetOutputSize();
		const float xRight  = static_cast<float>(sz.x);

		renderer.RenderDebugText(
			{ xRight - static_cast<float>(fpsStr.size())  * 8.f - 6.f, 4.f  }, fpsStr);
		renderer.RenderDebugText(
			{ xRight - static_cast<float>(timeStr.size()) * 8.f - 6.f, 14.f }, timeStr);
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)