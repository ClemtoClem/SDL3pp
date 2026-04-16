#pragma once
#include "../Core/ScriptParser.hpp"
#include "../ECS/Components.hpp"
#include "../Logger/Logger.hpp"
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_scene.h>
#include <SDL3pp/SDL3pp_resources.h>
#include <format>
#include <string>

namespace game {

// ─────────────────────────────────────────────────────────────────────────────
// EntityBuilder
//
// Dual-component strategy: each entity gets BOTH game-logic components
// (for our Systems) AND scene-graph components (for SDL3pp rendering).
//
//  Game components  : Transform, Velocity, DirectionComp, SpeedComp,
//                     SpriteAnim, CollisionBoxes, HealthComp, EntityTag,
//                     + PlayerTag | MobAI | AutoWalk+Dialogue
//
//  Scene components : Transform2D, GlobalTransform2D, Visible, Tag,
//                     AnimatedSprite (frames = 4 dirs × nbCols),
//                     SceneGroup, SceneParent, SceneScript (sync script)
//
// The SceneScript sync (runs every frame before PropagateTransforms2D):
//   Transform{x,y} (tile-units)  → Transform2D.position (pixels)
//   SpriteAnim.scale * dts/ts     → Transform2D.scale
//   dir.dir + sa.currentCol      → AnimatedSprite.frame
//   Transform.y * 100            → AnimatedSprite.zOrder  (Y-depth sort)
// ─────────────────────────────────────────────────────────────────────────────

class EntityBuilder {
public:
	// `dispTileSizePtr` is a live pointer; the sync script reads it every frame
	// so scale-in/out is reflected immediately without rebuilding entities.
	EntityBuilder(SDL::ECS::World&        world,
				  SDL::ECS::SceneBuilder& scene,
				  SDL::RendererRef        renderer,
				  SDL::ResourcePool&      pool,
				  const core::ScriptSectionPtr& infoEntities,
				  const std::string&      assetsBasePath,
				  int*                    dispTileSizePtr)
		: m_world(world), m_scene(scene), m_renderer(renderer),
		  m_pool(pool), m_info(infoEntities),
		  m_base(assetsBasePath), m_dts(dispTileSizePtr) {}

	// ── High-level factory ────────────────────────────────────────────────────

	SDL::ECS::EntityId BuildFromSpawn(const core::EntitySpawnDef& sp,
									  SDL::ECS::EntityId parentGroup = SDL::ECS::NullEntity) {
		EntityType et = EntityType::Default;
		if (sp.typeName == "PLAYER")                                    et = EntityType::Player;
		else if (sp.typeName == "PNJ1" || sp.typeName == "PNJ2")       et = EntityType::Pnj;
		else if (sp.typeName == "BLOB"   || sp.typeName == "MINI_BLOB") et = EntityType::Blob;
		else if (sp.typeName == "GOBLIN" || sp.typeName == "MINI_GOBLIN") et = EntityType::Goblin;
		else if (sp.typeName == "SKELETON")                              et = EntityType::Skeleton;
		else if (sp.typeName == "DRAGON" || sp.typeName == "DRAGON_BOSS") et = EntityType::Dragon;
		return Build(sp.typeName, sp.x, sp.y, sp.dir, et, parentGroup);
	}

	SDL::ECS::EntityId Build(const std::string& typeName,
							  float x, float y, Direction dir,
							  EntityType etype,
							  SDL::ECS::EntityId parentGroup = SDL::ECS::NullEntity) {
		if (!m_info) return SDL::ECS::NullEntity;
		auto it = m_info->find(typeName);
		if (it == m_info->end()) {
			LOG_GAME(core::LogLevel::Warning) << "EntityBuilder: no config for '" << typeName << "'";
			return SDL::ECS::NullEntity;
		}
		const core::ScriptValue& cfg = it->second;

		// ── Config values ─────────────────────────────────────────────────────
		std::string img    = cfg.Get("texture").AsString("");
		int   tileSize     = cfg.Get("tileSize").AsInt(64);
		int   frameSteps   = cfg.Get("frameBetweenEachTile").AsInt(5);
		bool  firstIsStand = cfg.Get("firstTileIsStanding").AsBool(true);
		float scale         = cfg.Get("scale").AsFloat(1.5f);
		float speed        = cfg.Get("speed").AsFloat(0.05f);
		float pushForce    = cfg.Get("pushForce").AsFloat(8.f);
		constexpr int kNbCols = 3; // stand + 2 walk frames

		SDL::TextureRef tex = _LoadTex(img);

		// ── 1. Spawn via SceneBuilder ─────────────────────────────────────────
		//    Creates entity with Transform2D, GlobalTransform2D, Tag, Visible
		//    + AnimatedSprite component
		float initPx = x * (float)*m_dts;
		float initPy = y * (float)*m_dts;
		float initSc = scale * (float)*m_dts / (float)tileSize;

		auto nb = m_scene.AnimSprite2D(typeName, tex)
			.Position({initPx, initPy})
			.Scale(initSc)
			.Play(false)                          // manual frame selection
			.ZOrder((int)(y * 100))
			.Group(etype == EntityType::Player ? "player" :
				   etype == EntityType::Pnj    ? "npc" : "mob");

		SDL::ECS::EntityId eid = nb.id;

		// Populate all frames: 4 rows (N,S,W,E) × kNbCols columns
		if (auto* as = m_world.Get<SDL::ECS::AnimatedSprite>(eid)) {
			as->frames.clear();
			as->frames.reserve(4 * kNbCols);
			for (int row = 0; row < 4; ++row)
				for (int col = 0; col < kNbCols; ++col)
					as->frames.push_back({(float)(col * tileSize),
										  (float)(row * tileSize),
										  (float)tileSize, (float)tileSize});
			as->fps = 0.f; as->playing = false;
		}

		// Attach to scene hierarchy
		if (parentGroup != SDL::ECS::NullEntity)
			nb.AttachTo(parentGroup);

		// ── 2. Game-logic components ──────────────────────────────────────────
		m_world.Add<Transform>(eid, {x, y});
		m_world.Add<Velocity>(eid);
		m_world.Add<DirectionComp>(eid, {dir});
		m_world.Add<EntityTag>(eid, {etype, typeName});
		m_world.Add<SpeedComp>(eid, {speed, speed * 2.f, false});
		m_world.Add<CollisionResult>(eid);

		CollisionBoxes boxes{}; boxes.tileSize = tileSize;
		_ParseBoxes(cfg, "bodyBox", boxes.body);
		_ParseBoxes(cfg, "stepBox", boxes.step);
		m_world.Add<CollisionBoxes>(eid, boxes);

		SpriteAnim sa{};
		sa.tileSize     = tileSize;
		sa.nbCols       = kNbCols;
		sa.scale         = scale;
		sa.framesPerCol = frameSteps;
		sa.firstIsStand = firstIsStand;
		sa.currentCol   = firstIsStand ? 0 : 1;
		m_world.Add<SpriteAnim>(eid, sa);

		HealthComp hc{}; hc.hp = hc.maxHp = 100.f; hc.regenPerSec = 0.5f;
		m_world.Add<HealthComp>(eid, hc);

		// Type-specific
		if (etype == EntityType::Player) {
			m_world.Add<PlayerTag>(eid, {0});
		} else if (etype == EntityType::Pnj) {
			m_world.Add<AutoWalk>(eid);
			m_world.Add<Dialogue>(eid);
		} else {
			MobAI ai{};
			ai.spawnX = x; ai.spawnY = y; ai.pushForce = pushForce;
			ai.chaseRadius = (etype == EntityType::Dragon) ? 12.f : 8.f;
			ai.attackRange = (etype == EntityType::Dragon) ? 1.4f : 0.9f;
			m_world.Add<MobAI>(eid, ai);
		}

		// ── 3. Sync script ────────────────────────────────────────────────────
		// Runs inside scene.Update(dt), before PropagateTransforms2D.
		// Bridges the gap between game-logic state and scene-graph state.
		int* dtsPtr = m_dts;
		nb.OnUpdate([dtsPtr, tileSize](SDL::ECS::EntityId id, SDL::ECS::World& w, float) {
				auto* tr  = w.Get<Transform>(id);
				auto* dir = w.Get<DirectionComp>(id);
				auto* sa  = w.Get<SpriteAnim>(id);
				auto* t2d = w.Get<SDL::ECS::Transform2D>(id);
				auto* as  = w.Get<SDL::ECS::AnimatedSprite>(id);
				if (!tr || !t2d) return;

				int  dts = *dtsPtr;
				t2d->position = { tr->x * (float)dts, tr->y * (float)dts };

				if (sa) {
					float sc = sa->scale * (float)dts / (float)tileSize;
					t2d->scale = {sc, sc};
				}

				if (dir && sa && as && !as->frames.empty()) {
					int frame = (int)dir->dir * sa->nbCols + sa->currentCol;
					as->frame   = std::clamp(frame, 0, (int)as->frames.size()-1);
					as->playing = false;
					if (tr) as->zOrder = (int)(tr->y * 100);
				}
		});

		LOG_GAME(core::LogLevel::Debug)
			<< std::format("Built '{}' eid={} ({:.1f},{:.1f})", typeName, eid, x, y);
		return eid;
	}

private:
	SDL::ECS::World&        m_world;
	SDL::ECS::SceneBuilder& m_scene;
	SDL::RendererRef        m_renderer;
	SDL::ResourcePool&      m_pool;
	core::ScriptSectionPtr  m_info;
	std::string             m_base;
	int*                    m_dts; // live pointer to dispTileSize

	SDL::TextureRef _LoadTex(const std::string& img) {
		if (img.empty()) return nullptr;
		const std::string key = "tex:" + img;
		if (auto h = m_pool.Get<SDL::Texture>(key)) return *h.get();
		try {
			SDL::Texture t(m_renderer, m_base + "/textures/" + img);
			m_pool.Add<SDL::Texture>(key, std::move(t));
			if (auto h2 = m_pool.Get<SDL::Texture>(key)) return *h2.get();
		} catch (const std::exception& e) {
			LOG_RESOURCE(core::LogLevel::Warning) << "EntityBuilder texture: " << e.what();
		}
		return nullptr;
	}

	void _ParseBoxes(const core::ScriptValue& cfg, const std::string& key, SDL::FRect out[4]) {
		SDL::FRect fallback{8,8,48,48};
		auto v = cfg.Get(key);
		if (v.IsNull())     { for (int i=0;i<4;++i) out[i]=fallback; return; }
		if (v.IsRect())     { auto r=v.AsRect(); SDL::FRect b{r.x,r.y,r.w,r.h};
							  for (int i=0;i<4;++i) { out[i]=b; } return; }
		if (!v.IsSection()) { for (int i=0;i<4;++i) out[i]=fallback; return; }
		auto d = v.Get("default");
		SDL::FRect def = fallback;
		if (d.IsRect()) { auto r=d.AsRect(); def={r.x,r.y,r.w,r.h}; }
		for (int i=0;i<4;++i) out[i]=def;
		auto setDir = [&](const char* n, Direction dir) {
			auto dv = v.Get(n);
			if (dv.IsRect()) { auto r=dv.AsRect(); out[(int)dir]={r.x,r.y,r.w,r.h}; }
		};
		setDir("north",Direction::North); setDir("south",Direction::South);
		setDir("west",Direction::West);   setDir("east",Direction::East);
	}
};

} // namespace game
