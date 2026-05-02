#pragma once
#include "IState.hpp"
#include "../ECS/Systems.hpp"
#include "../Core/MapLoader.hpp"
#include "../Core/Camera.hpp"
#include "../Core/SaveManager.hpp"
#include "../ECS/EntityBuilder.hpp"
#include "../Logger/Logger.hpp"
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_scene.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <format>
#include <vector>

namespace game {

// ─────────────────────────────────────────────────────────────────────────────
// GameState — main gameplay
//
// Dual-world architecture:
//
//   m_gameWorld  ← ECS::Context shared by game systems AND scene graph
//     Every entity holds:
//       • Game components  : Transform, Velocity, SpriteAnim, CollisionBoxes…
//       • Scene components : Transform2D (pixels), AnimatedSprite, SceneScript
//     The SceneScript sync callback bridges game→scene every frame.
//
//   m_scene  ← SceneBuilder (wraps m_gameWorld)
//     Root → entitiesGroup → entity nodes
//     SignalBus for mob-died / teleport / player-spawned
//
//   m_ecs  ← separate ECS::Context  (UI only)
//   m_ui       ← SDL::UI::System
//     HUD row (health bar, polypoints, map name)
//     Canvas widget  ← game world rendered here
//     Message panel  (Fixed overlay, hidden by default)
//     Pause panel    (Fixed overlay, hidden by default)
// ─────────────────────────────────────────────────────────────────────────────

class GameState : public IState {
public:
	GameState() = default;

	// ── Public save/load helpers (called from pause UI or MenuState) ──────────
	bool SaveGame() {
		if (!m_ctx || m_ctx->savePath.empty()) return false;
		core::SaveManager sm(m_ctx->savePath);
		core::SaveData sd;
		m_gameWorld.Each<PlayerTag, HealthComp>(
			[&](SDL::ECS::EntityId, PlayerTag& p, HealthComp& h) {
				sd.polypoints = p.polypoints;
				sd.hp         = h.hp;
				sd.maxHp      = h.maxHp;
			});
		m_gameWorld.Each<PlayerTag, Transform>(
			[&](SDL::ECS::EntityId, PlayerTag&, Transform& t) {
				sd.playerX = t.x;
				sd.playerY = t.y;
			});
		m_gameWorld.Each<PlayerTag, DirectionComp>(
			[&](SDL::ECS::EntityId, PlayerTag&, DirectionComp& d) {
				sd.playerDir = d.dir;
			});
		sd.mapName = m_map.name;
		return sm.Save(sd);
	}

	// ═══════════════════════════════════════════════════════════════════════
	void Enter(AppContext& ctx) override {
		m_ctx = &ctx;
		LOG_GAME(core::LogLevel::Info) << "GameState::Enter";

		// ── Read config ────────────────────────────────────────────────────
		auto& cfg = *ctx.config;
		m_assetsBase      = ctx.assetsBasePath;
		m_dispTileSize    = cfg.count("dispTileSize")    ? cfg["dispTileSize"].AsInt(64) : 64;
		m_tilesetName     = cfg.count("tilesetImgName")  ? cfg["tilesetImgName"].AsString("tileset1.png") : "tileset1.png";
		m_tilesetTileSize = cfg.count("tileSize")        ? cfg["tileSize"].AsInt(16) : 16;
		m_firstMapName    = cfg.count("firstMapName")    ? cfg["firstMapName"].AsString("world") : "world";
		m_infoEntities    = cfg.count("infoEntities")    ? cfg["infoEntities"].AsSection()  : nullptr;

		// ── Load save if one exists ────────────────────────────────────────
		if (!ctx.savePath.empty()) {
			core::SaveManager sm(ctx.savePath);
			if (sm.Load(m_pendingSave)) {
				m_hasPendingSave = true;
				m_firstMapName   = m_pendingSave.mapName;
				LOG_GAME(core::LogLevel::Info)
					<< "Using save: map=" << m_firstMapName;
			}
		}

		// ── Scene graph (uses m_gameWorld) ─────────────────────────────────
		m_scene = std::make_unique<SDL::ECS::SceneBuilder>(m_gameWorld, ctx.renderer);
		m_sceneRoot      = m_scene->Node2D("Root").id;
		m_entitiesGroup  = m_scene->Node2D("Entities").AttachTo(m_sceneRoot).id;
		m_scene->SetRoot(m_sceneRoot);

		// Dedicated SceneCamera entity (free-standing, not parented)
		m_sceneCamEnt = m_gameWorld.CreateEntity();
		m_gameWorld.Add<SDL::ECS::SceneCamera>(m_sceneCamEnt, {});

		// ── Tileset texture ────────────────────────────────────────────────
		_LoadTileset(ctx);

		// ── UI (separate world) ────────────────────────────────────────────
		m_ecs = std::make_unique<SDL::ECS::Context>();
		m_ui      = std::make_unique<SDL::UI::System>(
			*m_ecs, ctx.renderer, SDL::MixerRef{nullptr}, *ctx.pool);
		_BuildUI();

		// ── Signal bus ─────────────────────────────────────────────────────
		_ConnectSignals();

		// ── First map ──────────────────────────────────────────────────────
		LoadMap(m_firstMapName);

		LOG_GAME(core::LogLevel::Success) << "GameState ready";
	}

	void Leave() override {
		m_ui.reset(); m_ecs.reset();
		m_scene.reset();
		m_gameWorld = {};
		LOG_GAME(core::LogLevel::Info) << "GameState::Leave";
	}

	bool HandleEvent(const SDL::Event& ev) override {
		if (ev.type == SDL::EVENT_QUIT) { m_ctx->quit(); return false; }

		// Forward to UI and scene input handlers
		if (m_ui)    m_ui->ProcessEvent(reinterpret_cast<const SDL::Event&>(ev));
		if (m_scene) m_scene->DispatchInput(reinterpret_cast<const SDL::Event&>(ev));

		if (ev.type == SDL::EVENT_KEY_DOWN) {
			switch (ev.key.key) {
				case SDL::KEYCODE_F1:
					m_debugMode = !m_debugMode;
					break;
				case SDL::KEYCODE_ESCAPE:
					m_paused = !m_paused;
					m_ui->SetVisible(m_pausePanel, m_paused);
					break;
				case SDL::KEYCODE_RETURN:
				case SDL::KEYCODE_KP_ENTER:
					m_showMessage ? _AdvanceMessage() : _CheckFacingInteraction();
					break;
				case SDL::KEYCODE_KP_PLUS:
				case SDL::KEYCODE_O:
					m_dispTileSize = std::min(128, m_dispTileSize + 4);
					m_camera.SetDispTileSize(m_dispTileSize); break;
				case SDL::KEYCODE_KP_MINUS:
				case SDL::KEYCODE_P:
					m_dispTileSize = std::max(16, m_dispTileSize - 4);
					m_camera.SetDispTileSize(m_dispTileSize); break;
				default: break;
			}
		}
		return true;
	}

	void Update(float dt) override {
		if (m_paused || m_showMessage) return;

		// 1. Game systems (movement, collision, AI, health, animation)
		Systems::RunAll(m_gameWorld, m_map, m_camera, dt);

		// 2. Scene graph: sync scripts (Transform→Transform2D) + propagate
		m_scene->Update(dt);

		// 3. Push camera state to SceneCamera (viewport needed for RenderScene)
		_SyncSceneCamera(m_camera.viewW, m_camera.viewH);

		// 4. HUD
		_UpdateHUD();

		// 5. Teleportation
		_CheckTeleport();

		// 6. Remove dead mobs (emit signal, hide sprite)
		std::vector<SDL::ECS::EntityId> toHide;
		m_gameWorld.Each<HealthComp, EntityTag>(
			[&](SDL::ECS::EntityId eid, HealthComp& h, EntityTag& tag) {
			if (h.enabled && h.hp <= 0.f && tag.type != EntityType::Player) {
				h.enabled = false;
				if (auto* ai = m_gameWorld.Get<MobAI>(eid)) ai->state = AIState::Dead;
				toHide.push_back(eid);
				m_scene->Emit(eid, "died");
			}
		});
		for (auto eid : toHide)
			if (auto* vis = m_gameWorld.Get<SDL::ECS::Visible>(eid)) vis->value = false;
	}

	void Render(float dt) override {
		if (m_ui) m_ui->Iterate(dt); // triggers Canvas renderCb → _RenderGame
	}

	// ── Public map loader (used by teleportation too) ─────────────────────────
	void LoadMap(const std::string& name) {
		m_map = core::LoadMap(m_assetsBase + "/maps", name);
		if (m_map.width == 0) {
			LOG_GAME(core::LogLevel::Error) << "Failed to load map: " << name;
			return;
		}
		m_camera.SetDispTileSize(m_dispTileSize);
		_SpawnEntities();
		m_ui->SetText(m_lblMapName, name);
	}

private:
	AppContext* m_ctx = nullptr;

	// ── Config ────────────────────────────────────────────────────────────────
	std::string          m_assetsBase;
	int                  m_dispTileSize    = 64;
	int                  m_tilesetTileSize = 16;
	std::string          m_tilesetName;
	std::string          m_firstMapName;
	core::ScriptSectionPtr m_infoEntities;

	// ── Game world (game logic + scene graph, same ECS world) ─────────────────
	SDL::ECS::Context     m_gameWorld;
	std::unique_ptr<SDL::ECS::SceneBuilder> m_scene;
	core::MapData       m_map;
	core::Camera        m_camera;
	SDL::ECS::EntityId  m_sceneRoot     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_entitiesGroup = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_sceneCamEnt   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_playerEntity  = SDL::ECS::NullEntity;

	// ── Tileset ───────────────────────────────────────────────────────────────
	SDL::TextureRef m_tilesetTex;
	int             m_tilesetCols = 1;

	// ── UI (separate ECS world) ───────────────────────────────────────────────
	std::unique_ptr<SDL::ECS::Context>  m_ecs;
	std::unique_ptr<SDL::UI::System>  m_ui;
	SDL::ECS::EntityId  m_pausePanel = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_msgPanel   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_lblHealth  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_lblPoints  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_lblMapName = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_healthBar  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId  m_lblMsg     = SDL::ECS::NullEntity;

	// ── State ─────────────────────────────────────────────────────────────────
	bool m_paused      = false;
	bool m_debugMode   = false;
	bool m_showMessage = false;
	std::vector<std::string> m_msgLines;
	int                      m_msgIdx = 0;

	// ── Save-game ─────────────────────────────────────────────────────────────
	bool            m_hasPendingSave = false;
	core::SaveData  m_pendingSave;

	// ═════════════════════════════════════════════════════════════════════════
	// UI construction
	// ═════════════════════════════════════════════════════════════════════════

	void _BuildUI() {
		using namespace SDL::UI;

		constexpr SDL::Color kBgHud   = {10,  12,  20,  230};
		constexpr SDL::Color kAcc     = {60, 140, 220, 255};
		constexpr SDL::Color kAccH    = {80, 160, 240, 255};
		constexpr SDL::Color kTxt     = {235,235,245, 255};
		constexpr SDL::Color kGreen   = {80, 220, 120, 255};
		constexpr SDL::Color kYellow  = {240,200,  60, 255};
		constexpr SDL::Color kDark    = {35,  40,  58, 255};

		m_ui->LoadFont("DejaVuSans", m_ctx->assetsBasePath + "fonts/DejaVuSans.ttf");
		m_ui->SetDefaultFont("DejaVuSans", 16.f);

		// ── Health group ──────────────────────────────────────────────────────
		m_lblHealth = m_ui->Label("hpLbl", "HP: 100/100")
			.TextColor(kGreen).FontSize(13.f);

		m_healthBar = m_ui->Progress("hpBar", 100.f, 100.f)
			.W(160.f).H(12.f)
			.FillColor(kGreen)
			.TrackColor({30,35,50,255})
			.BgColor({30,35,50,255});

		auto hpGrp = m_ui->Column("hpGrp", 3.f, 4.f)
			.BgColor({0,0,0,0})
			.AutoScrollable(false, false)
			.Children(m_lblHealth, m_healthBar);

		// ── Points + map name ─────────────────────────────────────────────────
		m_lblPoints  = m_ui->Label("pts",  "0 pts").TextColor(kYellow).FontSize(14.f).Grow(100.f);
		m_lblMapName = m_ui->Label("map",  "---").TextColor({160,165,200,200}).FontSize(12.f);

		// ── HUD row ───────────────────────────────────────────────────────────
		auto hud = m_ui->Row("hud", 14.f, 8.f)
			.H(46.f)
			.BgColor(kBgHud)
			.AutoScrollable(false, false)
			.Children(hpGrp, m_lblPoints, m_lblMapName);

		// ── Game canvas (grows to fill remaining space) ───────────────────────
		auto canvas = m_ui->CanvasWidget("game", nullptr, nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect){ _RenderGame(r, rect); })
			.Grow(100.f);

		// ── Root ──────────────────────────────────────────────────────────────
		auto root = m_ui->Column("root", 0.f, 0.f)
			.W(Value::Ww(100.f)).H(Value::Wh(100.f))
			.BgColor({0,0,0,255})
			.AutoScrollable(false, false)
			.Children(hud, canvas)
			.AsRoot();

		// ── Message panel (Fixed overlay, initially hidden) ───────────────────
		m_lblMsg = m_ui->Label("msgTxt", "").TextColor(kTxt).FontSize(14.f);

		m_msgPanel = m_ui->Column("msgPanel", 0.f, 0.f)
			.BgColor({10,12,22,220})
			.BdColor(kAcc)
			.X(Value::Rw(20.f)).Y(Value::Rh(75.f))
			.W(Value::Rw(60.f)).H(80.f)
			.Attach(AttachLayout::Fixed)
			.Visible(false).AutoScrollable(false, true)
			.Padding({12.f, 8.f, 12.f, 8.f})
			.Borders(SDL::FBox(2.f)).Radius(SDL::FCorners(8.f));

		m_ui->AppendChild(m_msgPanel, m_lblMsg);
		root.Child(m_msgPanel);

		// ── Pause panel (Fixed overlay, initially hidden) ─────────────────────
		auto mkPBtn = [&](const char* id, const char* lbl,
						  SDL::Color bg, SDL::Color bgh,
						  std::function<void()> cb) {
			return m_ui->Button(id, lbl)
				.BgColor(bg).BgHoveredColor(bgh)
				.Radius(SDL::FCorners(5.f))
				.TextColor(kTxt).FontSize(14.f)
				.H(38.f).MarginV(4.f)
				.OnClick(std::move(cb));
		};

		auto pauseTitle = m_ui->Label("pauseTitle", "⏸  PAUSE")
			.TextColor({200,200,230,255}).FontSize(18.f)
			.AlignH(Align::Center)
			.MarginV(8.f);

		auto bResume = mkPBtn("bResume", "  Reprendre",  kAcc,   kAccH,
							   [this]{ _SetPaused(false); });
		auto bSave   = mkPBtn("bSave",   "  Sauvegarder", kDark, {50,55,75,255},
							   [this]{ SaveGame(); });
		auto bMenu   = mkPBtn("bMenu",   "  Menu principal",
							   {130,35,35,255}, {165,50,45,255},
							   [this]{ _GoToMenu(); });

		m_pausePanel = m_ui->Column("pausePanel", 4.f, 0.f)
			.W(280.f)
			.BgColor({5,7,15,215})
			.BdColor(kAcc)
			.Visible(false)
			.AutoScrollable(false, false)
			.Attach(AttachLayout::Fixed)
			.X(Value::Rw(50.f) - 140.f)
			.Y(Value::Rh(30.f))
			.Padding({16.f, 20.f, 16.f, 20.f})
			.Borders(SDL::FBox(2.f))
			.Radius(SDL::FCorners(10.f));

		auto pauseInner = m_ui->Column("pauseInner", 4.f, 0.f)
			.BgColor({0,0,0,0})
			.AutoScrollable(false, false)
			.Children(pauseTitle, bResume, bSave, m_ui->Separator(), bMenu);
		m_ui->AppendChild(m_pausePanel, pauseInner);
		m_ui->AppendChild(root, m_pausePanel);

		m_ui->SetRoot(root);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Game canvas rendering (called by Canvas renderCb each frame)
	// ═════════════════════════════════════════════════════════════════════════

	void _RenderGame(SDL::RendererRef r, SDL::FRect rect) {
		// Clip all rendering to the canvas rect
		r.SetViewport(FRectToRect(rect));

		m_camera.SetViewport(0.f, 0.f, rect.w, rect.h);
		_SyncSceneCamera(rect.w, rect.h); // ensure correct viewport for RenderScene

		// 1. Background tile
		_DrawBgTile(r);

		// 2. Background layers
		int bgEnd = m_map.numLayers - m_map.frontLayers;
		_DrawTileLayers(r, 0, bgEnd);

		// 3. Scene entities (Y-sorted via zOrder set by sync script)
		SDL::ECS::RenderScene(m_gameWorld, r);

		// 4. Foreground layers (draw over entities)
		_DrawTileLayers(r, bgEnd, m_map.numLayers);

		// 5. Per-entity health bars
		_DrawEntityHUD(r);

		// 6. Debug overlay
		if (m_debugMode) _DrawDebug(r);

		r.SetViewport(std::nullopt); // restore full viewport for UI
	}

	// ── Tile rendering ────────────────────────────────────────────────────────

	void _DrawBgTile(SDL::RendererRef r) {
		if (!m_tilesetTex || m_map.backgroundTile == 0) {
			r.SetDrawColor({20,22,30,255});
			r.RenderFillRect(SDL::FRect{0.f, 0.f, m_camera.viewW, m_camera.viewH});
			return;
		}
		int id = m_map.backgroundTile;
		if (m_map.zeroIsNotTile && id > 0) --id;
		SDL::FRect src = _SrcRect(id);
		float ts = (float)m_dispTileSize;
		int sx, sy, ex, ey;
		m_camera.VisibleTileRange(m_map.width, m_map.height, sx, sy, ex, ey);
		for (int ty = sy; ty < ey; ++ty)
			for (int tx = sx; tx < ex; ++tx) {
				SDL::FRect dst = {m_camera.ScreenX((float)tx),
								  m_camera.ScreenY((float)ty), ts, ts};
				r.RenderTexture(m_tilesetTex, src, dst);
			}
	}

	void _DrawTileLayers(SDL::RendererRef r, int from, int to) {
		if (!m_tilesetTex) return;
		float ts = (float)m_dispTileSize;
		int sx, sy, ex, ey;
		m_camera.VisibleTileRange(m_map.width, m_map.height, sx, sy, ex, ey);
		for (int layer = from; layer < to; ++layer) {
			for (int ty = sy; ty < ey; ++ty) {
				for (int tx = sx; tx < ex; ++tx) {
					int id = m_map.GetTile(layer, tx, ty);
					if (id == 0) continue;
					int adj = (m_map.zeroIsNotTile && id > 0) ? id - 1 : id;
					SDL::FRect src = _SrcRect(adj);
					SDL::FRect dst = {m_camera.ScreenX((float)tx),
									  m_camera.ScreenY((float)ty), ts, ts};
					r.RenderTexture(m_tilesetTex, src, dst);
				}
			}
		}
	}

	SDL::FRect _SrcRect(int id) const noexcept {
		float ts  = (float)m_tilesetTileSize;
		int col   = id % m_tilesetCols;
		int row   = id / m_tilesetCols;
		return {col * ts, row * ts, ts, ts};
	}

	// ── Per-entity health bars ────────────────────────────────────────────────
	void _DrawEntityHUD(SDL::RendererRef r) {
		m_gameWorld.Each<Transform, HealthComp, EntityTag>(
			[&](SDL::ECS::EntityId eid, Transform& t, HealthComp& h, EntityTag&) {
			if (!h.enabled || h.maxHp <= 0.f) return;
			if (auto* vis = m_gameWorld.Get<SDL::ECS::Visible>(eid))
				if (!vis->value) return;
			float barW = (float)m_dispTileSize * 0.8f;
			float bx   = m_camera.ScreenX(t.x) - barW * 0.5f;
			float by   = m_camera.ScreenY(t.y) - (float)m_dispTileSize * 0.55f;
			float pct  = SDL::Clamp(h.hp / h.maxHp, 0.f, 1.f);
			SDL::Color fg = pct > 0.5f ? SDL::Color{60,200,80,220}
						  : pct > 0.25f ? SDL::Color{220,180,40,220}
										: SDL::Color{220,50,50,220};
			SDL::FRect bg_r  = {bx, by, barW, 5.f};
			SDL::FRect fg_r  = {bx, by, barW * pct, 5.f};
			r.SetDrawColor({40,10,10,180}); r.RenderFillRect(bg_r);
			r.SetDrawColor(fg);             r.RenderFillRect(fg_r);
		});
	}

	// ── Debug overlay ─────────────────────────────────────────────────────────
	void _DrawDebug(SDL::RendererRef r) {
		m_gameWorld.Each<PlayerTag, Transform, DirectionComp, CollisionResult>(
			[&](SDL::ECS::EntityId, PlayerTag& p, Transform& t,
				DirectionComp& dir, CollisionResult& cr) {
			r.SetScale({1.5f, 1.5f});
			r.SetDrawColor({255,255,80,255});
			const char* dirStr = dir.dir==Direction::North?"N":dir.dir==Direction::South?"S":
								 dir.dir==Direction::West ?"W":"E";
			auto l1 = std::format("Pos:({:.1f},{:.1f}) Dir:{}", t.x, t.y, dirStr);
			auto l2 = std::format("Facing:{} Pts:{} Ents:{}", (int)cr.facing,
								   p.polypoints, m_gameWorld.EntityCount());
			auto l3 = std::format("Map:{} DTS:{}", m_map.name, m_dispTileSize);
			r.RenderDebugText({4/1.5f,  4/1.5f}, l1);
			r.RenderDebugText({4/1.5f, 16/1.5f}, l2);
			r.RenderDebugText({4/1.5f, 28/1.5f}, l3);
			r.SetScale({1.f, 1.f});
		});

		// Collision step-box and body-box outlines
		m_gameWorld.Each<Transform, CollisionBoxes, DirectionComp>(
			[&](SDL::ECS::EntityId, Transform& t, CollisionBoxes& cb, DirectionComp& d) {
			SDL::FBox box = Systems::BoxToWorld(cb.step[(int)d.dir], t.x, t.y, cb.tileSize);
			r.SetDrawColor({255,60,60,160});
			r.RenderRect(SDL::FRect{m_camera.ScreenX(box.left), m_camera.ScreenY(box.top),
						   (box.right-box.left)*(float)m_dispTileSize, (box.bottom-box.top)*(float)m_dispTileSize});
			
			box = Systems::BoxToWorld(cb.body[(int)d.dir], t.x, t.y, cb.tileSize);
			r.SetDrawColor({60,255,60,160});
			r.RenderRect(SDL::FRect{m_camera.ScreenX(box.left), m_camera.ScreenY(box.top),
						   (box.right-box.left)*(float)m_dispTileSize, (box.bottom-box.top)*(float)m_dispTileSize});
		});

		// Scene transform debug (crosses at each entity)
		SDL::ECS::DebugDrawTransforms2D(m_gameWorld, r, 5.f);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Scene camera sync
	// ═════════════════════════════════════════════════════════════════════════

	void _SyncSceneCamera(float vpW, float vpH) {
		auto* cam = m_gameWorld.Get<SDL::ECS::SceneCamera>(m_sceneCamEnt);
		if (!cam) return;
		cam->offset    = { m_camera.worldX * (float)m_dispTileSize,
						   m_camera.worldY * (float)m_dispTileSize };
		cam->zoom      = 1.f;
		cam->viewportW = (int)vpW;
		cam->viewportH = (int)vpH;
	}

	// ═════════════════════════════════════════════════════════════════════════
	// HUD update
	// ═════════════════════════════════════════════════════════════════════════

	void _UpdateHUD() {
		m_gameWorld.Each<PlayerTag, HealthComp>(
			[&](SDL::ECS::EntityId, PlayerTag& p, HealthComp& h) {
			m_ui->SetText(m_lblHealth,
				std::format("HP: {:.0f}/{:.0f}", h.hp, h.maxHp));

			// Update progress bar: val + max via uiWorld's SliderData
			if (auto* nv = m_ecs->Get<SDL::UI::NumericValue>(m_healthBar)) {
				nv->max = h.maxHp;
				nv->val = SDL::Clamp(h.hp, 0.f, h.maxHp);
			}
			m_ui->SetText(m_lblPoints, std::format("★ {} pts", p.polypoints));
		});
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Entity spawning
	// ═════════════════════════════════════════════════════════════════════════

	void _SpawnEntities() {
		// Remove all non-player entities (scene + game components)
		std::vector<SDL::ECS::EntityId> toRemove;
		m_gameWorld.Each<EntityTag>([&](SDL::ECS::EntityId eid, EntityTag& tag) {
			if (tag.type != EntityType::Player) toRemove.push_back(eid);
		});
		for (auto e : toRemove) m_scene->DestroyNode(e);
		m_playerEntity = SDL::ECS::NullEntity;

		EntityBuilder builder(m_gameWorld, *m_scene, m_ctx->renderer,
							   *m_ctx->pool, m_infoEntities,
							   m_assetsBase, &m_dispTileSize);

		for (auto& sp : m_map.spawns) {
			auto eid = builder.BuildFromSpawn(sp, m_entitiesGroup);
			if (eid == SDL::ECS::NullEntity) continue;
			if (sp.typeName == "PLAYER") {
				m_playerEntity = eid;
				m_camera.worldX = sp.x;
				m_camera.worldY = sp.y;
				m_scene->Emit("PLAYER", "spawned");
			}
		}
		LOG_GAME(core::LogLevel::Info)
			<< std::format("Spawned {} entities", m_map.spawns.size());

		// ── Apply pending save data (overrides spawn positions) ───────────────
		if (m_hasPendingSave && m_playerEntity != SDL::ECS::NullEntity) {
			const auto& sd = m_pendingSave;
			if (auto* t = m_gameWorld.Get<Transform>(m_playerEntity)) {
				t->x = sd.playerX; t->y = sd.playerY;
			}
			if (auto* d = m_gameWorld.Get<DirectionComp>(m_playerEntity))
				d->dir = sd.playerDir;
			if (auto* h = m_gameWorld.Get<HealthComp>(m_playerEntity)) {
				h->hp = sd.hp; h->maxHp = sd.maxHp;
			}
			if (auto* p = m_gameWorld.Get<PlayerTag>(m_playerEntity))
				p->polypoints = sd.polypoints;
			m_camera.worldX = sd.playerX;
			m_camera.worldY = sd.playerY;
			m_hasPendingSave = false;
			LOG_GAME(core::LogLevel::Success) << "Save data applied";
		}
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Teleportation
	// ═════════════════════════════════════════════════════════════════════════

	void _CheckTeleport() {
		if (m_playerEntity == SDL::ECS::NullEntity) return;
		auto* res = m_gameWorld.Get<CollisionResult>(m_playerEntity);
		if (!res || res->facing != core::CollisionId::Transition) return;

		auto* tr  = m_gameWorld.Get<Transform>(m_playerEntity);
		auto* dir = m_gameWorld.Get<DirectionComp>(m_playerEntity);
		if (!tr || !dir) return;

		float fx = tr->x, fy = tr->y;
		switch (dir->dir) {
			case Direction::North: fy -= 1.f; break;
			case Direction::South: fy += 1.f; break;
			case Direction::West:  fx -= 1.f; break;
			case Direction::East:  fx += 1.f; break;
		}
		auto key = std::format("{}_{}", (int)std::round(fx), (int)std::round(fy));
		auto it  = m_map.teleports.find(key);
		if (it == m_map.teleports.end()) return;

		const auto& tele = it->second;
		if (tele.changeMap) {
			LoadMap(tele.mapName);
		}
		// Reposition player (may be a new entity after LoadMap)
		if (m_playerEntity != SDL::ECS::NullEntity)
			if (auto* t2 = m_gameWorld.Get<Transform>(m_playerEntity)) {
				t2->x = tele.x; t2->y = tele.y;
			}
		m_scene->Emit("WORLD", "teleport");
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Message / dialogue
	// ═════════════════════════════════════════════════════════════════════════

	void _CheckFacingInteraction() {
		if (m_playerEntity == SDL::ECS::NullEntity) return;
		auto* tr  = m_gameWorld.Get<Transform>(m_playerEntity);
		auto* dir = m_gameWorld.Get<DirectionComp>(m_playerEntity);
		auto* res = m_gameWorld.Get<CollisionResult>(m_playerEntity);
		if (!tr || !dir || !res) return;
		if (res->facing != core::CollisionId::Message &&
			res->facing != core::CollisionId::Access) return;

		float fx = tr->x, fy = tr->y;
		switch (dir->dir) {
			case Direction::North: fy -= 1.f; break;
			case Direction::South: fy += 1.f; break;
			case Direction::West:  fx -= 1.f; break;
			case Direction::East:  fx += 1.f; break;
		}
		auto key = std::format("{}_{}", (int)std::round(fx), (int)std::round(fy));
		auto it  = m_map.messages.find(key);
		if (it == m_map.messages.end()) return;

		m_msgLines = it->second;
		m_msgIdx   = 0;
		_ShowMsg(m_msgLines.empty() ? "" : m_msgLines[0]);
	}

	void _ShowMsg(const std::string& txt) {
		m_showMessage = true;
		m_ui->SetText(m_lblMsg, txt);
		m_ui->SetVisible(m_msgPanel, true);
	}

	void _AdvanceMessage() {
		++m_msgIdx;
		if (m_msgIdx >= (int)m_msgLines.size()) {
			m_showMessage = false;
			m_ui->SetVisible(m_msgPanel, false);
		} else {
			_ShowMsg(m_msgLines[(size_t)m_msgIdx]);
		}
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Signal connections
	// ═════════════════════════════════════════════════════════════════════════

	void _ConnectSignals() {
		// Tag names MUST match the uppercase entity type names produced by MapLoader
		m_scene->Connect("PLAYER", "spawned",
			[]{ LOG_GAME(core::LogLevel::Info) << "Signal: PLAYER spawned"; });
		m_scene->Connect("WORLD", "teleport",
			[]{ LOG_GAME(core::LogLevel::Info) << "Signal: Teleport"; });

		// Mob-died → award polypoints
		auto award = [this](const std::string& tag, int pts) {
			m_scene->Connect(tag, "died", [this, pts]{
				if (m_playerEntity != SDL::ECS::NullEntity)
					if (auto* p = m_gameWorld.Get<PlayerTag>(m_playerEntity))
						p->polypoints += pts;
			});
		};
		award("BLOB",      10);  award("MINI_BLOB",   5);
		award("GOBLIN",    20);  award("MINI_GOBLIN", 10);
		award("SKELETON",  30);
		award("DRAGON",   100);  award("DRAGON_BOSS", 500);
	}

	// ═════════════════════════════════════════════════════════════════════════
	// Helpers
	// ═════════════════════════════════════════════════════════════════════════

	void _SetPaused(bool p) {
		m_paused = p;
		m_ui->SetVisible(m_pausePanel, m_paused);
	}

	void _LoadTileset(AppContext& ctx) {
		const std::string key  = "tileset_main";
		const std::string path = m_assetsBase + "/textures/" + m_tilesetName;
		if (auto h = ctx.pool->Get<SDL::Texture>(key)) {
			m_tilesetTex = *h.get();
		} else {
			try {
				SDL::Texture t(ctx.renderer, path);
				ctx.pool->Add<SDL::Texture>(key, std::move(t));
				if (auto h2 = ctx.pool->Get<SDL::Texture>(key)) m_tilesetTex = *h2.get();
			} catch (const std::exception& e) {
				LOG_RESOURCE(core::LogLevel::Error) << "Tileset: " << e.what();
			}
		}
		if (m_tilesetTex) {
			m_tilesetTex.SetBlendMode(SDL::BLENDMODE_BLEND);
			m_tilesetTex.SetScaleMode(SDL::SCALEMODE_NEAREST);
			SDL::Point sz = m_tilesetTex.GetSize();
			m_tilesetCols = std::max(1, sz.x / m_tilesetTileSize);
		}
	}

	// Defined in main.cpp (requires MenuState to be complete)
	void _GoToMenu();
};

} // namespace game
