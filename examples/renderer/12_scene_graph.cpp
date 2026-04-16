/*
 * renderer/12_scene_graph.cpp
 *
 * Scene graph demo using SDL3pp_ecs.h + SDL3pp_scene.h.
 *
 * Demonstrates:
 *  - Building a 2-D scene hierarchy (parent → child entities)
 *  - ECS::Transform2D propagation (GlobalECS::Transform2D computed from parent chain)
 *  - Animated rotation at each level of the hierarchy
 *  - ECS::Sprite z-ordering and alpha
 *  - ECS::SceneCamera with zoom and pan
 *  - RAII ECS::EntityRef lifetime management
 *
 * Scene layout:
 *
 *   [root_crate]  ── centre of screen, slow rotation
 *       ├── [orbit_0]  ── orbits the root, medium distance
 *       │       └── [moon_0]  ── orbits orbit_0, small crate
 *       ├── [orbit_1]  ── same as orbit_0, offset 90°
 *       ├── [orbit_2]  ── offset 180°
 *       └── [orbit_3]  ── offset 270°
 *
 * Controls:
 *   ESC           ──  exit
 *   arrows        ──  pan camera
 *   +/-           ──  zoom in / out
 *   D             ──  toggle debug crosshairs
 *   SPACE         ──  pause animation
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_image.h>
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_scene.h>

#include <cmath>
#include <format>
#include <numbers>

using namespace std::literals;

// ─────────────────────────────────────────────────────────────────────────────
// Application constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr SDL::Point kWindowSz       = {800, 600};
static constexpr float      kOrbitRadius    = 160.f;  ///< Parent ↔ child distance
static constexpr float      kMoonRadius     = 55.f;   ///< Child ↔ moon distance
static constexpr float      kRootRotSpeed   = 20.f;   ///< deg/s root crate
static constexpr float      kOrbitRotSpeed  = 60.f;   ///< deg/s orbit crates
static constexpr float      kMoonRotSpeed   = 120.f;  ///< deg/s moon crates
static constexpr float      kCamPanSpeed    = 200.f;  ///< px/s camera pan
static constexpr float      kCamZoomSpeed   = 1.5f;   ///< zoom factor/s

// ─────────────────────────────────────────────────────────────────────────────
// Custom ECS components
// ─────────────────────────────────────────────────────────────────────────────

/// Spins the entity's local rotation automatically.
struct Spinner { float degreesPerSecond = 0.f; };

/// Orbits the entity around the parent origin at a given angular velocity.
struct Orbiter {
	float radius          = 0.f;
	float angularSpeed    = 0.f; ///< deg/s
	float currentAngle    = 0.f; ///< current angle in degrees
};

// ─────────────────────────────────────────────────────────────────────────────
// Main application
// ─────────────────────────────────────────────────────────────────────────────

struct Main {
	// ── Window / renderer ─────────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs /*args*/) {
		SDL::SetAppMetadata("Scene Graph Demo", "1.0",
												"com.example.scene-graph");
		SDL::Init(SDL::INIT_VIDEO);
		*out = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
	}

	static SDL::Window MakeWindow() {
		return SDL::CreateWindowAndRenderer(
			"examples/renderer/scene-graph - Scene Graph + ECS demo", kWindowSz, 0, nullptr);
	}

	SDL::Window      window  {MakeWindow()};
	SDL::RendererRef renderer{window.GetRenderer()};

	// ── ECS ECS::World + scene graph ────────────────────────────────────────────────

	SDL::ECS::World world;
	SDL::ECS::SceneGraph scene{world, renderer};

	// ── Crate texture (shared by all sprites) ─────────────────────────────────

	SDL::Texture crateTexture{LoadCrateTexture()};

	SDL::Texture LoadCrateTexture() {
		// Try SDL3_image first (supports JPEG); fall back to BMP stub.
		SDL::Texture tex;
		try {
			tex = SDL::LoadTexture(renderer, std::format("{}../../../assets/textures/crate.jpg", SDL::GetBasePath()));
		} catch (...) {
			// Fallback: create a plain brown 64×64 texture to stay runnable.
			SDL::Surface surf = SDL::CreateSurface({64, 64}, SDL::PIXELFORMAT_RGB24);
			surf.FillRect({}, SDL::MapSurfaceRGB(surf, 139, 90, 43));
			tex = SDL::CreateTextureFromSurface(renderer, surf);
			SDL::Log("crate.jpg not found - using placeholder texture");
		}
		return tex;
	}

	// ── Persistent entity IDs (root + orbits) ─────────────────────────────────

	SDL::ECS::EntityId rootId     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId orbitId[4] = {};
	SDL::ECS::EntityId moonId[4]  = {};

	// ── State ──────────────────────────────────────────────────────────────────

	SDL::FrameTimer m_frameTimer{60.f};

	bool   m_paused       = false;
	bool   m_debugDraw    = false;

	// Camera pan/zoom
	float  m_camX         = 0.f;
	float  m_camY         = 0.f;
	float  m_camZoom      = 1.f;

	// Input state
	bool   m_keyUp = false, m_keyLeft = false, m_keyDown = false, m_keyRight = false;
	bool   m_keyPlus = false, m_keyMinus = false;

	// ── Constructor: build scene ───────────────────────────────────────────────

	Main() {
		const float cx = kWindowSz.x * 0.5f;
		const float cy = kWindowSz.y * 0.5f;

		// ── Root crate (centre of screen) ─────────────────────────────────────
 {
			auto ref = scene.CreateNode("root_crate");
			rootId = ref.Release();  // Keep the entity alive beyond this scope.

			world.Add<SDL::ECS::Transform2D>(rootId, {{cx, cy}, 90.f, {1.f, 1.f}});
			world.Add<SDL::ECS::Sprite>(rootId, {
				.texture = crateTexture,
				.tint    = {255, 220, 180, 255},
				.zOrder  = 0
			});
			world.Add<Spinner>(rootId, {kRootRotSpeed});
		}

		// ── Four orbiting crates + their moon ─────────────────────────────────

		const float startAngles[4] = {0.f, 90.f, 180.f, 270.f};
		const SDL::Color orbitTints[4] = {
			{180, 220, 255, 255},  // blue
			{220, 255, 180, 255},  // green
			{255, 180, 220, 255},  // pink
			{255, 255, 180, 255},  // yellow
		};

		for (int i = 0; i < 4; ++i) {
			// Orbit crate
			{
				auto ref = scene.CreateNode(std::format("orbit_{}", i), rootId);
				orbitId[i] = ref.Release();

				// Initial position along the orbit circle.
				const float rad = SDL::DegToRad(startAngles[i]);
				world.Add<SDL::ECS::Transform2D>(orbitId[i], { {SDL::Cos(rad) * kOrbitRadius,
					 SDL::Sin(rad) * kOrbitRadius},
					0.f, {0.6f, 0.6f}
				});
				world.Add<SDL::ECS::Sprite>(orbitId[i], {
					.texture = crateTexture,
					.tint    = orbitTints[i],
					.zOrder  = 1
				});
				world.Add<Spinner>(orbitId[i], {kOrbitRotSpeed * (i % 2 == 0 ? 1.f : -1.f)});
				world.Add<Orbiter>(orbitId[i], {
					.radius       = kOrbitRadius,
					.angularSpeed = kOrbitRotSpeed,
					.currentAngle = startAngles[i]
				});
			}

			// Moon (child of orbit crate)
			{
				auto ref = scene.CreateNode(std::format("moon_{}", i), orbitId[i]);
				moonId[i] = ref.Release();

				world.Add<SDL::ECS::Transform2D>(moonId[i], { {kMoonRadius, 0.f}, 0.f, {0.3f, 0.3f}
				});
				world.Add<SDL::ECS::Sprite>(moonId[i], {
					.texture = crateTexture,
					.tint    = {200, 200, 200, 200},
					.zOrder  = 2
				});
				world.Add<Spinner>(moonId[i], {kMoonRotSpeed});
				world.Add<Orbiter>(moonId[i], {
					.radius       = kMoonRadius,
					.angularSpeed = kMoonRotSpeed * 1.5f,
					.currentAngle = 0.f
				});
			}
		}

		// ── Camera ─────────────────────────────────────────────────────────────

		scene.SetCamera({
			.offset    = {0.f, 0.f},
			.zoom      = m_camZoom,
			.viewportW = kWindowSz.x,
			.viewportH = kWindowSz.y
		});

		m_frameTimer.Begin();
	}

	// ── Destructor: release persistent entity IDs ──────────────────────────────

	~Main() {
		// The ECS::World destructor frees everything; we just need to not double-free.
		// No explicit cleanup needed here – ECS::World's storage owns everything.
	}

	// ── Event handling ────────────────────────────────────────────────────────

	SDL::AppResult Event(const SDL::Event& ev) {
		if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;

		if (ev.type == SDL::EVENT_KEY_DOWN || ev.type == SDL::EVENT_KEY_UP) {
			const bool down = (ev.type == SDL::EVENT_KEY_DOWN);
			switch (ev.key.key) {
				case SDL::KEYCODE_ESCAPE: return SDL::APP_SUCCESS;
				case SDL::KEYCODE_SPACE:  if (down) m_paused = !m_paused; break;
				case SDL::KEYCODE_F1:
					if (down) {
						m_debugDraw = !m_debugDraw;
						if (m_debugDraw) {
							SDL::SetLogPriorities(SDL::LOG_PRIORITY_DEBUG);
							SDL::LogDebug(SDL::LOG_CATEGORY_APPLICATION, "Enable debug mode");
						} else {
							SDL::SetLogPriorities(SDL::LOG_PRIORITY_WARN);
						}
					}
					break;
				case SDL::KEYCODE_UP:     m_keyUp     = down; break;
				case SDL::KEYCODE_LEFT:   m_keyLeft   = down; break;
				case SDL::KEYCODE_DOWN:   m_keyDown   = down; break;
				case SDL::KEYCODE_RIGHT:  m_keyRight  = down; break;
				case SDL::KEYCODE_EQUALS: m_keyPlus   = down; break;
				case SDL::KEYCODE_MINUS:  m_keyMinus  = down; break;
				default: break;
			}
		}
		return SDL::APP_CONTINUE;
	}

	// ── Per-frame ─────────────────────────────────────────────────────────────

	SDL::AppResult Iterate() {
		// ── Time ──────────────────────────────────────────────────────────────
		m_frameTimer.Begin();
		const float dt = m_paused ? 0.f : m_frameTimer.GetDelta();

		// ── Camera pan / zoom ─────────────────────────────────────────────────
		{
			const float panSpeed = kCamPanSpeed / m_camZoom;
			if (m_keyUp)    m_camY -= panSpeed * dt;
			if (m_keyDown)  m_camY += panSpeed * dt;
			if (m_keyLeft)  m_camX -= panSpeed * dt;
			if (m_keyRight) m_camX += panSpeed * dt;
			if (m_keyPlus)  m_camZoom *= (1.f + kCamZoomSpeed * dt);
			if (m_keyMinus) m_camZoom /= (1.f + kCamZoomSpeed * dt);
			m_camZoom = std::clamp(m_camZoom, 0.1f, 10.f);

			scene.SetCamera({
				.offset    = {m_camX, m_camY},
				.zoom      = m_camZoom,
				.viewportW = kWindowSz.x,
				.viewportH = kWindowSz.y
			});
		}

		// ── Animate: Spinners ─────────────────────────────────────────────────
		world.Each<SDL::ECS::Transform2D, Spinner>([dt](SDL::ECS::EntityId, SDL::ECS::Transform2D& t, Spinner& s) {
			t.rotation += s.degreesPerSecond * dt;
		});

		// ── Animate: Orbiters (update local position to orbit parent) ─────────
		world.Each<SDL::ECS::Transform2D, Orbiter>([dt](SDL::ECS::EntityId, SDL::ECS::Transform2D& t, Orbiter& o) {
			o.currentAngle += o.angularSpeed * dt;
			const float rad = SDL::DegToRad(o.currentAngle);
			t.position = {SDL::Cos(rad) * o.radius, SDL::Sin(rad) * o.radius};
		});

		// ── Scene update + render ─────────────────────────────────────────────
		scene.Update(dt);

		renderer.SetDrawColor({15, 15, 25, 255});
		renderer.RenderClear();

		scene.Render();

		if (m_debugDraw) SDL::ECS::DebugDrawTransforms2D(world, renderer);

		// ── HUD ───────────────────────────────────────────────────────────────
		DrawHud();

		renderer.Present();
		m_frameTimer.End();
		return SDL::APP_CONTINUE;
	}

	void DrawHud() {
		renderer.SetDrawColor({200, 200, 200, 255});

		renderer.RenderDebugTextFormat({8.f, 8.f},
			"Entities : {}", world.EntityCount());
		renderer.RenderDebugTextFormat({8.f, 20.f},
			"Zoom     : {:.2f}x", m_camZoom);
		renderer.RenderDebugTextFormat({8.f, 32.f},
			"Camera   : ({:.0f}, {:.0f})", m_camX, m_camY);
		renderer.RenderDebugTextFormat({8.f, 44.f},
			"Time     : {:.1f}s{}", m_frameTimer.GetTime(), m_paused ? " [PAUSED]" : "");
		renderer.RenderDebugTextFormat({8.f, 56.f},
			"FPS      : {:.1f}", m_frameTimer.GetFPS());

		renderer.SetDrawColor({100, 100, 100, 255});
		renderer.RenderDebugText({8.f, static_cast<float>(kWindowSz.y - 16.f)},
			"WASD = pan  +/- = zoom  SPACE = pause  F1 = debug  ESC = quit");
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
