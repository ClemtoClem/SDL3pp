#ifndef SDL3PP_SCENE_H_
#define SDL3PP_SCENE_H_

/**
 * @file SDL3pp_scene.h
 * @brief Scene graph built on top of SDL3pp_ecs.h with builder DSL.
 *
 * ## Architecture
 *
 * Each scene node is an ECS entity carrying typed components.
 *
 * | SDL3pp component   |
 * |--------------------|
 * | `Transform2D`        |
 * | `GlobalTransform2D`  |
 * | `SceneParent`      |
 * | `SceneChildren`    |
 * | `Sprite`           |
 * | `AnimatedSprite`   |
 * | `SceneCamera`      |
 * | `SceneScript`      |
 * | `SceneTween`       |
 * | `Tag`              |
 * | `Visible`          |
 * | `SceneGroup`       |
 * | `SceneSignal`      |
 *
 * ## Builder DSL
 *
 * ```cpp
 * SDL::SceneBuilder scene(m_ctx, renderer);
 *
 * auto root = scene.Node2D("Context");
 *
 * auto player = scene.Sprite2D("Player", playerTex)
 *     .Position({400, 300})
 *     .Scale({2, 2})
 *     .ZOrder(10)
 *     .Group("actors")
 *     .OnReady([](SDL::EntityId id, SDL::Context& w) {
 *         SDL::Log("Player ready!");
 *     })
 *     .OnUpdate([](SDL::EntityId id, SDL::Context& w, float dt) {
 *         auto& t = *ctx.Get<SDL::Transform2D>(id);
 *         t.position.x += 100.f * dt;
 *     })
 *     .AttachTo(root);
 *
 * auto cam = scene.Camera2D("MainCamera")
 *     .Follow(player)
 *     .Zoom(1.5f)
 *     .AttachTo(root);
 *
 * scene.SetRoot(root);
 *
 * // Each frame:
 * scene.Update(dt);   // PropagateTransforms2D + RunScripts
 * scene.Render();     // RenderScene
 * ```
 *
 * ## Signal system (lightweight signals)
 *
 * ```cpp
 * // Emit a named signal
 * scene.Emit("Player", "died");
 *
 * // Subscribe anywhere
 * scene.Connect("Player", "died", []{ SDL::Log("player died"); });
 * ```
 */

#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include "SDL3pp_ecs.h"
#include "SDL3pp_render.h"
#include "SDL3pp_stdinc.h"

namespace SDL {

namespace ECS {

// =============================================================================
// Built-in scene components (unchanged from v1)
// =============================================================================

/**
 * Local 2-D transform.  Values are in scene-space pixels.
 */
struct Transform2D {
	FPoint position = {0.f, 0.f};
	float  rotation = 0.f;   ///< Degrees, clockwise
	FPoint scale    = {1.f, 1.f};

	constexpr Transform2D() = default;
	constexpr Transform2D(FPoint pos, float rot = 0.f, FPoint scl = {1.f, 1.f})
		: position(pos), rotation(rot), scale(scl) {}
};

/**
 * Context-space transform computed by `PropagateTransforms2D()`.
 * Do not modify directly.
 */
struct GlobalTransform2D {
	FPoint position = {0.f, 0.f};
	float  rotation = 0.f;
	FPoint scale    = {1.f, 1.f};

	[[nodiscard]] FRect DestRect(float w, float h) const noexcept {
		return {
			position.x - w * scale.x * 0.5f,
			position.y - h * scale.y * 0.5f,
			w * scale.x,
			h * scale.y
		};
	}

	[[nodiscard]] GlobalTransform2D Combine(const Transform2D& child) const noexcept {
		const float parentRad = SDL::DegToRad(rotation);
		const float cosP = SDL::Cos(parentRad);
		const float sinP = SDL::Sin(parentRad);
		const float lx = child.position.x * scale.x;
		const float ly = child.position.y * scale.y;
		return {
			.position = {
				position.x + cosP * lx - sinP * ly,
				position.y + sinP * lx + cosP * ly
			},
			.rotation = rotation + child.rotation,
			.scale    = {scale.x * child.scale.x, scale.y * child.scale.y}
		};
	}
};

struct SceneParent   { EntityId id = NullEntity; };
struct SceneChildren {
	std::vector<EntityId> ids;
	void Add   (EntityId c) { if (std::ranges::find(ids, c) == ids.end()) ids.push_back(c); }
	void Remove(EntityId c) { std::erase(ids, c); }
};

/**
 * 2-D sprite rendered via `SDL::Renderer::RenderTextureRotated`.
 */
struct Sprite {
	TextureRef texture;
	FRect      srcRect    = {};
	FPoint     pivot      = {0.5f, 0.5f};
	Color      tint       = {255, 255, 255, 255};
	int        zOrder     = 0;
	float      alpha      = 1.f;
};

struct Tag     { std::string name; };
struct Visible { bool value = true; };

/**
 * Camera: offset and zoom applied to the whole scene during rendering.
 */
struct SceneCamera {
	FPoint offset    = {0.f, 0.f};
	float  zoom      = 1.f;
	int    viewportW = 0;
	int    viewportH = 0;
};

// =============================================================================
// New components
// =============================================================================

/**
 * @brief Animated sprite — plays a sequence of source rects from one texture
 *        at a given frame rate.
 */
struct AnimatedSprite {
	TextureRef             texture;
	std::vector<FRect>     frames;       ///< Source rects for each frame
	float                  fps        = 12.f;
	bool                   loop       = true;
	bool                   playing    = true;
	int                    frame      = 0;
	float                  elapsed    = 0.f;
	FPoint                 pivot      = {0.5f, 0.5f};
	Color                  tint       = {255, 255, 255, 255};
	int                    zOrder     = 0;
	float                  alpha      = 1.f;

	/// Advance animation by `dt` seconds.
	void Update(float dt) noexcept {
		if (!playing || frames.empty() || fps <= 0.f) return;
		elapsed += dt;
		float period = 1.f / fps;
		while (elapsed >= period) {
			elapsed -= period;
			++frame;
			if (frame >= static_cast<int>(frames.size())) {
				frame = loop ? 0 : static_cast<int>(frames.size()) - 1;
				if (!loop) playing = false;
			}
		}
	}

	[[nodiscard]] FRect CurrentFrame() const noexcept {
		if (frames.empty()) return {};
		return frames[static_cast<size_t>(std::clamp(frame, 0, static_cast<int>(frames.size())-1))];
	}
};

/**
 * @brief Per-node script callbacks.  Analogous to attaching a GDScript.
 *
 * Callbacks are called by `ScriptSystem::Run()` each frame.
 * All three are optional (default = nullptr).
 */
struct SceneScript {
	using UpdateFn = std::function<void(EntityId, Context&, float)>;  ///< (id, Context, dt)
	using ReadyFn  = std::function<void(EntityId, Context&)>;         ///< called once on first frame
	using InputFn  = std::function<void(EntityId, Context&, const SDL::Event&)>;

	UpdateFn onUpdate;
	ReadyFn  onReady;
	InputFn  onInput;
	bool     readyCalled = false;  ///< Ensures onReady fires exactly once.
};

/**
 * @brief Simple value tween.
 *
 * Interpolates a single `float*` from `from` to `to` over `duration` seconds.
 * Once complete, `onDone` is called and the component can be removed.
 */
struct SceneTween {
	float* target   = nullptr; ///< Pointer to the value being animated.
	float  from     = 0.f;
	float  to       = 1.f;
	float  duration = 0.5f;
	float  elapsed  = 0.f;
	bool   done     = false;

	enum class Ease { Linear, In, Out, InOut };
	Ease ease = Ease::Out;

	std::function<void()> onDone;

	void Update(float dt) noexcept {
		if (done || !target) return;
		elapsed += dt;
		float t = std::clamp(elapsed / duration, 0.f, 1.f);
		float e = _Ease(t);
		*target = from + (to - from) * e;
		if (t >= 1.f) {
			done = true;
			if (onDone) onDone();
		}
	}

private:
	[[nodiscard]] float _Ease(float t) const noexcept {
		switch (ease) {
		case Ease::In:    return t * t;
		case Ease::Out:   return 1.f - (1.f - t) * (1.f - t);
		case Ease::InOut: return t < 0.5f ? 2*t*t : 1.f - 2*(1-t)*(1-t);
		default:          return t;
		}
	}
};

/**
 * @brief Group membership (multi-valued).
 * The same entity can belong to several groups via repeated values.
 */
struct SceneGroup {
	std::vector<std::string> groups;

	void Add   (const std::string& g) { groups.push_back(g); }
	bool Has   (const std::string& g) const { return std::ranges::find(groups, g) != groups.end(); }
	void Remove(const std::string& g) { std::erase(groups, g); }
};

// =============================================================================
// Transform2D propagation
// =============================================================================

/**
 * Propagate local Transform2Ds through the hierarchy, computing GlobalTransform2Ds.
 * Call once per frame before rendering.
 */
inline void PropagateTransforms2D(Context& ecs_context) {
	auto dfs = [&](auto& self, EntityId e, const GlobalTransform2D& parentGlobal) -> void {
		if (!ecs_context.IsAlive(e)) return;
		const Transform2D* local = ecs_context.Get<Transform2D>(e);
		if (!local) return;
		const GlobalTransform2D global = parentGlobal.Combine(*local);
		ecs_context.Add<GlobalTransform2D>(e, global);
		if (const SceneChildren* ch = ecs_context.Get<SceneChildren>(e))
			for (EntityId child : ch->ids) self(self, child, global);
	};

	const GlobalTransform2D identity{};
	ecs_context.Each<Transform2D>([&](EntityId e, Transform2D&) {
		const SceneParent* par = ecs_context.Get<SceneParent>(e);
		if (!par || par->id == NullEntity) dfs(dfs, e, identity);
	});
}

// =============================================================================
// Script system
// =============================================================================

/**
 * @brief Run all `SceneScript` callbacks.
 *
 * Call `ScriptSystem::Run(m_ctx, dt)` once per frame (before rendering).
 * For input handling, use the overload that takes an `SDL::Event`.
 */
struct ScriptSystem {
	static void Run(Context& ecs_context, float dt) {
		ecs_context.Each<SceneScript>([&](EntityId id, SceneScript& s) {
			if (s.onReady && !s.readyCalled) {
				s.readyCalled = true;
				s.onReady(id, ecs_context);
			}
			if (s.onUpdate) s.onUpdate(id, ecs_context, dt);
		});

		// Advance tweens and remove finished ones.
		ecs_context.Each<SceneTween>([&](EntityId, SceneTween& tw) {
			tw.Update(dt);
		});

		// Advance animated sprites.
		ecs_context.Each<AnimatedSprite>([dt](EntityId, AnimatedSprite& as) {
			as.Update(dt);
		});
	}

	static void DispatchInput(Context& ecs_context, const SDL::Event& ev) {
		ecs_context.Each<SceneScript>([&](EntityId id, SceneScript& s) {
			if (s.onInput) s.onInput(id, ecs_context, ev);
		});
	}
};

// =============================================================================
// Signal bus — lightweight signal/slot
// =============================================================================

/**
 * @brief Decoupled signal bus for scene events.
 *
 * Signals are identified by a pair `(tag_name, signal_name)`.
 * Multiple listeners can connect to the same signal.
 *
 * ```cpp
 * SDL::SignalBus bus;
 * bus.Connect("Player", "hit", []{ SDL::Log("player hit"); });
 * bus.Emit  ("Player", "hit");
 * ```
 */
class SignalBus {
public:
	using Handler = std::function<void()>;

	/// Connect a handler to `(node, signal)`.
	void Connect(const std::string& node, const std::string& signal, Handler h) {
		m_slots[node + "::" + signal].push_back(std::move(h));
	}

	/// Emit `(node, signal)` — calls all connected handlers in order.
	void Emit(const std::string& node, const std::string& signal) const {
		auto it = m_slots.find(node + "::" + signal);
		if (it != m_slots.end())
			for (const auto& handler : it->second) handler();
	}

	/// Disconnect all handlers for `(node, signal)`.
	void Disconnect(const std::string& node, const std::string& signal) {
		m_slots.erase(node + "::" + signal);
	}

	void Clear() { m_slots.clear(); }

private:
	std::unordered_map<std::string, std::vector<Handler>> m_slots;
};

// =============================================================================
// Scene rendering
// =============================================================================

/**
 * Render all entities with `Sprite + GlobalTransform2D`.
 * Applies camera offset and zoom.  Sprites sorted by zOrder.
 */
inline void RenderScene(Context& ecs_context, RendererRef renderer) {
	SceneCamera camera;
	if (auto* s = ecs_context.ConstStorage<SceneCamera>())
		if (!s->View().empty()) camera = s->View()[0];

	if (camera.viewportW == 0 || camera.viewportH == 0) {
		Point sz = renderer.GetOutputSize();
		camera.viewportW = sz.x;
		camera.viewportH = sz.y;
	}
	const FPoint viewCentre = {camera.viewportW * 0.5f, camera.viewportH * 0.5f};

	struct SpriteItem  { Sprite*          s; GlobalTransform2D* x; float z; };
	struct AnimItem    { AnimatedSprite*  s; GlobalTransform2D* x; float z; };

	std::vector<SpriteItem> sprites;
	std::vector<AnimItem>   anims;
	sprites.reserve(ecs_context.Storage<Sprite>().Size());
	anims  .reserve(ecs_context.Storage<AnimatedSprite>().Size());

	ecs_context.Each<Sprite, GlobalTransform2D>([&](EntityId e, Sprite& spr, GlobalTransform2D& gt) {
		const Visible* vis = ecs_context.Get<Visible>(e);
		if (vis && !vis->value) return;
		if (!spr.texture) return;
		sprites.push_back({&spr, &gt, static_cast<float>(spr.zOrder)});
	});

	ecs_context.Each<AnimatedSprite, GlobalTransform2D>([&](EntityId e, AnimatedSprite& as, GlobalTransform2D& gt) {
		const Visible* vis = ecs_context.Get<Visible>(e);
		if (vis && !vis->value) return;
		if (!as.texture) return;
		anims.push_back({&as, &gt, static_cast<float>(as.zOrder)});
	});

	std::stable_sort(sprites.begin(), sprites.end(),
		[](const SpriteItem& a, const SpriteItem& b) { return a.z < b.z; });
	std::stable_sort(anims.begin(),   anims.end(),
		[](const AnimItem&   a, const AnimItem&   b) { return a.z < b.z; });

	// Merge-sort both lists by z-order into one draw sequence.
	auto si = sprites.begin();
	auto ai = anims.begin();
	while (si != sprites.end() || ai != anims.end()) {
		bool drawSprite = ai == anims.end() ||
						  (si != sprites.end() && si->z <= ai->z);
		if (drawSprite) {
			Sprite&         spr = *si->s;
			GlobalTransform2D& gt = *si->x;
			++si;

			// Apply camera.
			float wx = viewCentre.x + (gt.position.x - camera.offset.x) * camera.zoom;
			float wy = viewCentre.y + (gt.position.y - camera.offset.y) * camera.zoom;
			float sx = gt.scale.x * camera.zoom;
			float sy = gt.scale.y * camera.zoom;

			Point texSz = spr.texture.GetSize();
			float frameW = (spr.srcRect.w > 0.f) ? spr.srcRect.w : static_cast<float>(texSz.x);
			float frameH = (spr.srcRect.h > 0.f) ? spr.srcRect.h : static_cast<float>(texSz.y);
			FRect dst = {wx - frameW * sx * spr.pivot.x,
						 wy - frameH * sy * spr.pivot.y,
						 frameW * sx, frameH * sy};

			OptionalRef<const FRectRaw> src = std::nullopt;
			if (spr.srcRect.w > 0.f) src = spr.srcRect;
			FPoint pivot = {dst.w * spr.pivot.x, dst.h * spr.pivot.y};
			Uint8 a = static_cast<Uint8>(spr.tint.a * spr.alpha);
			spr.texture.SetColorMod(spr.tint.r, spr.tint.g, spr.tint.b);
			spr.texture.SetAlphaMod(a);
			renderer.RenderTextureRotated(spr.texture, src, dst, gt.rotation, pivot);

		} else {
			AnimatedSprite& as  = *ai->s;
			GlobalTransform2D& gt = *ai->x;
			++ai;

			float wx = viewCentre.x + (gt.position.x - camera.offset.x) * camera.zoom;
			float wy = viewCentre.y + (gt.position.y - camera.offset.y) * camera.zoom;
			float sx = gt.scale.x * camera.zoom;
			float sy = gt.scale.y * camera.zoom;

			FRect frame = as.CurrentFrame();
			float frameW = frame.w > 0.f ? frame.w : 32.f;
			float frameH = frame.h > 0.f ? frame.h : 32.f;
			FRect dst = {wx - frameW * sx * as.pivot.x,
						 wy - frameH * sy * as.pivot.y,
						 frameW * sx, frameH * sy};

			OptionalRef<const FRectRaw> src = std::nullopt;
			if (frame.w > 0.f) src = frame;
			FPoint pivot = {dst.w * as.pivot.x, dst.h * as.pivot.y};
			Uint8 a = static_cast<Uint8>(as.tint.a * as.alpha);
			as.texture.SetColorMod(as.tint.r, as.tint.g, as.tint.b);
			as.texture.SetAlphaMod(a);
			renderer.RenderTextureRotated(as.texture, src, dst, gt.rotation, pivot);
		}
	}
}

// =============================================================================
// Forward declaration of SceneBuilder (needed by builders)
// =============================================================================

class SceneBuilder;

// =============================================================================
// SceneNodeBuilder<Derived> — CRTP base builder
// =============================================================================

/**
 * @brief CRTP mixin providing common node operations to all typed builders.
 *
 * All methods return `Derived&` so derived-class calls chain cleanly.
 */
template<typename Derived>
struct SceneNodeBuilder {
	Context&      ecs_context;
	SceneBuilder& scene;
	EntityId      id;

	SceneNodeBuilder(Context& ctx, SceneBuilder& sc, EntityId e)
		: ecs_context(ctx), scene(sc), id(e) {}

	operator EntityId() const noexcept { return id; }
	[[nodiscard]] EntityId Id() const noexcept { return id; }

	// ── Transform2D ─────────────────────────────────────────────────────────────

	Derived& Position(FPoint p) { _Transform2D().position = p; return _self(); }

	Derived& Position(float x, float y) { _Transform2D().position = {x, y}; return _self(); }

	Derived& Rotation(float deg) { _Transform2D().rotation = deg; return _self(); }

	Derived& Scale(FPoint s) { _Transform2D().scale = s; return _self(); }

	Derived& Scale(float s) { _Transform2D().scale = {s, s}; return _self(); }

	// ── Visibility ────────────────────────────────────────────────────────────

	Derived& Hide() {
		if (auto* v = ecs_context.Get<Visible>(id)) v->value = false;
		else ecs_context.Add<Visible>(id, {false});
		return _self();
	}

	Derived& Show() {
		if (auto* v = ecs_context.Get<Visible>(id)) v->value = true;
		else ecs_context.Add<Visible>(id, {true});
		return _self();
	}

	Derived& SetVisible(bool vis) {
		if (auto* v = ecs_context.Get<Visible>(id)) v->value = vis;
		else ecs_context.Add<Visible>(id, {vis});
		return _self();
	}

	// ── Tag / name ────────────────────────────────────────────────────────────

	Derived& Name(const std::string& n) {
		if (auto* t = ecs_context.Get<Tag>(id)) t->name = n;
		else ecs_context.Add<Tag>(id, {n});
		return _self();
	}

	// ── Groups ────────────────────────────────────────────────────────────────

	/// Add this node to a named group.
	Derived& Group(const std::string& g) {
		if (!ecs_context.Has<SceneGroup>(id)) ecs_context.Add<SceneGroup>(id);
		ecs_context.Get<SceneGroup>(id)->Add(g);
		return _self();
	}

	// ── Scripts (per-node callbacks) ──────────────────────────────────────────

	/// Called every frame: `fn(id, m_ctx, dt)`.
	Derived& OnUpdate(SceneScript::UpdateFn fn) {
		_Script().onUpdate = std::move(fn);
		return _self();
	}

	/// Called once on the first frame: `fn(id, m_ctx)`.
	Derived& OnReady(SceneScript::ReadyFn fn) {
		_Script().onReady = std::move(fn);
		return _self();
	}

	/// Called for each SDL event: `fn(id, m_ctx, event)`.
	Derived& OnInput(SceneScript::InputFn fn) {
		_Script().onInput = std::move(fn);
		return _self();
	}

	// ── Tweens ────────────────────────────────────────────────────────────────

	/// Animate a float field to `to` over `duration` seconds.
	Derived& TweenTo(float* target, float to, float duration,
					 SceneTween::Ease ease = SceneTween::Ease::Out,
					 std::function<void()> onDone = nullptr) {
		float from = target ? *target : 0.f;
		ecs_context.Add<SceneTween>(id, {target, from, to, duration, 0.f, false, ease,
								   std::move(onDone)});
		return _self();
	}

	// ── Hierarchy ─────────────────────────────────────────────────────────────

	/// Re-parent this node under `parent` (inline, returns self).
	Derived& AttachTo(EntityId parent);  // defined after SceneBuilder

	/// Append a child entity to this node.
	Derived& AddChild(EntityId child);   // defined after SceneBuilder

protected:
	Transform2D& _Transform2D() {
		if (!ecs_context.Has<Transform2D>(id)) ecs_context.Add<Transform2D>(id);
		return *ecs_context.Get<Transform2D>(id);
	}

	SceneScript& _Script() {
		if (!ecs_context.Has<SceneScript>(id)) ecs_context.Add<SceneScript>(id);
		return *ecs_context.Get<SceneScript>(id);
	}

	Derived& _self() noexcept { return static_cast<Derived&>(*this); }
};

// =============================================================================
// Typed node builders
// =============================================================================

/**
 * @brief Builder for a plain 2-D transform node.
 */
struct Node2DBuilder : SceneNodeBuilder<Node2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;
};

/**
 * @brief Builder for a textured 2-D sprite.
 */
struct Sprite2DBuilder : SceneNodeBuilder<Sprite2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;

	Sprite2DBuilder& ZOrder(int z) {
		if (auto* sp = ecs_context.Get<Sprite>(id)) sp->zOrder = z;
		return *this;
	}

	Sprite2DBuilder& Tint(Color c) {
		if (auto* sp = ecs_context.Get<Sprite>(id)) sp->tint = c;
		return *this;
	}

	Sprite2DBuilder& Alpha(float a) {
		if (auto* sp = ecs_context.Get<Sprite>(id)) sp->alpha = a;
		return *this;
	}

	Sprite2DBuilder& SrcRect(FRect r) {
		if (auto* sp = ecs_context.Get<Sprite>(id)) sp->srcRect = r;
		return *this;
	}

	Sprite2DBuilder& Pivot(FPoint p) {
		if (auto* sp = ecs_context.Get<Sprite>(id)) sp->pivot = p;
		return *this;
	}
};

/**
 * @brief Builder for an animated sprite.
 */
struct AnimSprite2DBuilder : SceneNodeBuilder<AnimSprite2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;

	AnimSprite2DBuilder& AddFrame(FRect srcRect) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) as->frames.push_back(srcRect);
		return *this;
	}

	AnimSprite2DBuilder& AddFrames(std::initializer_list<FRect> rects) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id))
			for (const FRect& r : rects) as->frames.push_back(r);
		return *this;
	}

	/// Build frames from a horizontal sprite-sheet strip.
	AnimSprite2DBuilder& Spritesheet(int frameW, int frameH, int count, int row = 0) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) {
			as->frames.clear();
			for (int i = 0; i < count; ++i)
				as->frames.push_back({static_cast<float>(i * frameW),
									  static_cast<float>(row * frameH),
									  static_cast<float>(frameW),
									  static_cast<float>(frameH)});
		}
		return *this;
	}

	AnimSprite2DBuilder& FPS(float fps) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) as->fps = fps;
		return *this;
	}

	AnimSprite2DBuilder& Loop(bool l = true) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) as->loop = l;
		return *this;
	}

	AnimSprite2DBuilder& Play(bool p = true) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) as->playing = p;
		return *this;
	}

	AnimSprite2DBuilder& ZOrder(int z) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) as->zOrder = z;
		return *this;
	}

	AnimSprite2DBuilder& Tint(Color c) {
		if (auto* as = ecs_context.Get<AnimatedSprite>(id)) as->tint = c;
		return *this;
	}
};

/**
 * @brief Builder for a scene camera.
 */
struct Camera2DBuilder : SceneNodeBuilder<Camera2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;

	Camera2DBuilder& Zoom(float z) {
		if (auto* c = ecs_context.Get<SceneCamera>(id)) c->zoom = z;
		return *this;
	}

	Camera2DBuilder& Offset(FPoint o) {
		if (auto* c = ecs_context.Get<SceneCamera>(id)) c->offset = o;
		return *this;
	}

	/// Make the camera follow `target`'s GlobalTransform2D each frame.
	Camera2DBuilder& Follow(EntityId target) {
		OnUpdate([target](EntityId id, Context& ctx, float) {
			if (!ctx.IsAlive(target)) return;
			const auto* gt = ctx.Get<GlobalTransform2D>(target);
			if (!gt) return;
			if (auto* cam = ctx.Get<SceneCamera>(id))
				cam->offset = gt->position;
		});
		return *this;
	}
};

// =============================================================================
// SceneGraph — manager
// =============================================================================

class SceneGraph {
public:
	explicit SceneGraph(Context& ctx, RendererRef renderer)
		: m_ctx(ctx), m_renderer(renderer) {}

	EntityRef CreateNode(const std::string& name   = {},
						 EntityId parentId = NullEntity) {
		EntityRef ref = m_ctx.Spawn();
		const EntityId id = ref.Id();
		m_ctx.Add<Transform2D>(id);
		m_ctx.Add<GlobalTransform2D>(id);
		m_ctx.Add<Visible>(id, {true});
		if (!name.empty()) m_ctx.Add<Tag>(id, {name});

		if (parentId != NullEntity && m_ctx.IsAlive(parentId)) {
			m_ctx.Add<SceneParent>(id, {parentId});
			_GetOrAddChildren(parentId).Add(id);
		}
		return ref;
	}

	void DestroyNode(EntityId e) {
		if (!m_ctx.IsAlive(e)) return;
		if (const SceneParent* par = m_ctx.Get<SceneParent>(e))
			if (par->id != NullEntity)
				if (auto* ch = m_ctx.Get<SceneChildren>(par->id)) ch->Remove(e);
		if (const SceneChildren* ch = m_ctx.Get<SceneChildren>(e)) {
			auto childIds = ch->ids;
			for (EntityId c : childIds) DestroyNode(c);
		}
		m_ctx.DestroyEntity(e);
	}

	void SetParent(EntityId e, EntityId newParent) {
		if (const SceneParent* old = m_ctx.Get<SceneParent>(e))
			if (old->id != NullEntity)
				if (auto* ch = m_ctx.Get<SceneChildren>(old->id)) ch->Remove(e);

		if (newParent != NullEntity && m_ctx.IsAlive(newParent)) {
			m_ctx.Add<SceneParent>(e, {newParent});
			_GetOrAddChildren(newParent).Add(e);
		} else {
			m_ctx.Remove<SceneParent>(e);
		}
	}

	void Update(float dt = 0.f) {
		ScriptSystem::Run(m_ctx, dt);
		PropagateTransforms2D(m_ctx);
	}

	void DispatchInput(const SDL::Event& ev) {
		ScriptSystem::DispatchInput(m_ctx, ev);
	}

	void Render() {
		RenderScene(m_ctx, m_renderer);
	}

	void SetCamera(const SceneCamera& cam) {
		if (m_cameraEntity == NullEntity || !m_ctx.IsAlive(m_cameraEntity))
			m_cameraEntity = m_ctx.CreateEntity();
		m_ctx.Add<SceneCamera>(m_cameraEntity, cam);
	}

	[[nodiscard]] SceneCamera GetCamera() const {
		if (m_cameraEntity != NullEntity)
			if (const SceneCamera* c = m_ctx.Get<SceneCamera>(m_cameraEntity))
				return *c;
		return {};
	}

	[[nodiscard]] Context&       GetWorld()    const noexcept { return m_ctx; }
	[[nodiscard]] RendererRef  GetRenderer() const noexcept { return m_renderer; }

	[[nodiscard]] EntityId FindByName(const std::string& name) const {
		EntityId result = NullEntity;
		const_cast<Context&>(m_ctx).Each<Tag>([&](EntityId e, const Tag& tag) {
			if (tag.name == name) result = e;
		});
		return result;
	}

	/// Find all entities belonging to `group`.
	[[nodiscard]] std::vector<EntityId> FindGroup(const std::string& group) const {
		std::vector<EntityId> out;
		const_cast<Context&>(m_ctx).Each<SceneGroup>([&](EntityId e, const SceneGroup& g) {
			if (g.Has(group)) out.push_back(e);
		});
		return out;
	}

private:
	Context&    m_ctx;
	RendererRef m_renderer;
	EntityId    m_cameraEntity = NullEntity;

	SceneChildren& _GetOrAddChildren(EntityId e) {
		if (!m_ctx.Has<SceneChildren>(e)) m_ctx.Add<SceneChildren>(e);
		return *m_ctx.Get<SceneChildren>(e);
	}
};

// =============================================================================
// SceneBuilder — fluent factory (wraps SceneGraph)
// =============================================================================

/**
 * @brief High-level scene construction API node system.
 *
 * `SceneBuilder` owns a `SceneGraph` internally and exposes typed builder
 * factories that return strongly-typed builders (`Node2DBuilder`,
 * `Sprite2DBuilder`, etc.).  Each builder converts implicitly to `EntityId`.
 *
 * ```cpp
 * SDL::SceneBuilder scene(m_ctx, renderer);
 *
 * auto root  = scene.Node2D("Root");
 * auto hero  = scene.Sprite2D("Hero", heroTex)
 *                   .Position({400, 300})
 *                   .ZOrder(5)
 *                   .AttachTo(root);
 * auto cam   = scene.Camera2D("Cam")
 *                   .Follow(hero)
 *                   .Zoom(1.5f)
 *                   .AttachTo(root);
 *
 * scene.SetRoot(root);
 * // Per frame:
 * scene.Update(dt);
 * scene.Render();
 * ```
 */
class SceneBuilder {
public:
	SceneBuilder(Context& ctx, RendererRef renderer)
		: m_graph(ctx, renderer), m_ctx(ctx), m_renderer(renderer) {}

	// ── Node factories ────────────────────────────────────────────────────────

	/// Plain transform node.
	Node2DBuilder Node2D(const std::string& name = {}) {
		EntityId id = _Spawn(name);
		return Node2DBuilder{m_ctx, *this, id};
	}

	/// Static sprite node.
	Sprite2DBuilder Sprite2D(const std::string& name, TextureRef tex = {}) {
		EntityId id = _Spawn(name);
		m_ctx.Add<Sprite>(id, {tex});
		return Sprite2DBuilder{m_ctx, *this, id};
	}

	/// Animated sprite .
	AnimSprite2DBuilder AnimSprite2D(const std::string& name, TextureRef tex = {}) {
		EntityId id = _Spawn(name);
		AnimatedSprite as;
		as.texture = tex;
		m_ctx.Add<AnimatedSprite>(id, std::move(as));
		return AnimSprite2DBuilder{m_ctx, *this, id};
	}

	/// Camera node.
	Camera2DBuilder Camera2D(const std::string& name = "Camera2D") {
		EntityId id = _Spawn(name);
		m_ctx.Add<SceneCamera>(id);
		return Camera2DBuilder{m_ctx, *this, id};
	}

	// ── Root management ───────────────────────────────────────────────────────

	void SetRoot(EntityId e) { m_root = e; }
	[[nodiscard]] EntityId GetRoot() const noexcept { return m_root; }

	// ── Signal bus ────────────────────────────────────────────────────────────

	void Connect(const std::string& node, const std::string& signal,
				 SignalBus::Handler h) { m_bus.Connect(node, signal, std::move(h)); }

	void Emit(const std::string& node, const std::string& signal) { m_bus.Emit(node, signal); }

	void Emit(EntityId e, const std::string& signal) {
		const Tag* t = m_ctx.Get<Tag>(e);
		if (t) m_bus.Emit(t->name, signal);
	}

	[[nodiscard]] SignalBus& GetBus() noexcept { return m_bus; }

	// ── Per-frame ─────────────────────────────────────────────────────────────

	/// Advance scripts, tweens, animations, then propagate transforms.
	void Update(float dt = 0.f) {
		ScriptSystem::Run(m_ctx, dt);
		PropagateTransforms2D(m_ctx);
	}

	/// Forward an SDL event to all `SceneScript::onInput` handlers.
	void DispatchInput(const SDL::Event& ev) {
		ScriptSystem::DispatchInput(m_ctx, ev);
	}

	/// Render all visible sprites (static + animated).
	void Render() {
		RenderScene(m_ctx, m_renderer);
	}

	// ── Hierarchy helpers (called by builder AttachTo / AddChild) ─────────────

	void _AttachTo(EntityId child, EntityId parent) {
		m_graph.SetParent(child, parent);
	}

	void _AddChild(EntityId parent, EntityId child) {
		m_graph.SetParent(child, parent);
	}

	// ── Utilities ─────────────────────────────────────────────────────────────

	[[nodiscard]] EntityId FindByName(const std::string& name) const { return m_graph.FindByName(name); }

	[[nodiscard]] std::vector<EntityId> FindGroup(const std::string& g) const { return m_graph.FindGroup(g); }

	void DestroyNode(EntityId e) { m_graph.DestroyNode(e); }

	[[nodiscard]] Context&      GetWorld()    noexcept { return m_ctx; }
	[[nodiscard]] RendererRef GetRenderer() noexcept { return m_renderer; }
	[[nodiscard]] SceneGraph& GetGraph()    noexcept { return m_graph; }

private:
	SceneGraph  m_graph;
	Context&      m_ctx;
	RendererRef m_renderer;
	EntityId    m_root = NullEntity;
	SignalBus   m_bus;

	EntityId _Spawn(const std::string& name) {
		EntityId id = m_ctx.CreateEntity();
		m_ctx.Add<Transform2D>(id);
		m_ctx.Add<GlobalTransform2D>(id);
		m_ctx.Add<Visible>(id, {true});
		if (!name.empty()) m_ctx.Add<Tag>(id, {name});
		return id;
	}
};

// =============================================================================
// Deferred method definitions (need SceneBuilder to be complete)
// =============================================================================

template<typename Derived>
Derived& SceneNodeBuilder<Derived>::AttachTo(EntityId parent) {
	scene._AttachTo(id, parent);
	return _self();
}

template<typename Derived>
Derived& SceneNodeBuilder<Derived>::AddChild(EntityId child) {
	scene._AddChild(id, child);
	return _self();
}

// =============================================================================
// Debug helpers
// =============================================================================

/**
 * Draw a crosshair at each entity's m_ctx position (transform debugging).
 */
inline void DebugDrawTransforms2D(Context& ecs_context, RendererRef renderer, float size = 8.f) {
	renderer.SetDrawColor({255, 0, 255, 200});
	ecs_context.Each<GlobalTransform2D>([&](EntityId, GlobalTransform2D& gt) {
		renderer.RenderLine({gt.position.x - size, gt.position.y}, {gt.position.x + size, gt.position.y});
		renderer.RenderLine({gt.position.x, gt.position.y - size}, {gt.position.x, gt.position.y + size});
	});
}

/**
 * Draw FAABB bounding boxes around all sprites (collision debugging).
 */
inline void DebugDrawSpriteBounds(Context& ecs_context, RendererRef renderer,
								   SDL::Color color = {0, 255, 255, 120}) {
	renderer.SetDrawColor(color);
	ecs_context.Each<Sprite, GlobalTransform2D>([&](EntityId, Sprite& sp, GlobalTransform2D& gt) {
		SDL::Point texSz = sp.texture.GetSize();
		float w = sp.srcRect.w > 0 ? sp.srcRect.w : static_cast<float>(texSz.x);
		float h = sp.srcRect.h > 0 ? sp.srcRect.h : static_cast<float>(texSz.y);
		FRect dst = gt.DestRect(w, h);
		renderer.RenderRect(dst);
	});
}

} // namespace ECS

} // namespace SDL

#endif // SDL3PP_SCENE_H_