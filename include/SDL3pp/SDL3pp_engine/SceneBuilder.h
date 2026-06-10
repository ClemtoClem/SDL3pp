#ifndef SDL3PP_ENGINE_SCENE_BUILDER_H_
#define SDL3PP_ENGINE_SCENE_BUILDER_H_

/**
 * @file SceneBuilder.h
 * @brief Fluent scene-graph factory for 2-D and 3-D nodes.
 *
 * `SceneBuilder` owns a `SceneGraph` plus a `SignalBus` and a `ProcessManager`,
 * and exposes strongly-typed builders that create entities with the right
 * components attached. Every builder converts implicitly to `EntityId`.
 *
 * ```cpp
 * SDL::ECS::SceneBuilder scene(ctx, renderer);
 *
 * auto root  = scene.Node2D("Root");
 * auto hero  = scene.Sprite2D("Hero", heroTex).Position({400,300}).ZOrder(5).AttachTo(root);
 * scene.Camera2D("Cam").Follow(hero).Zoom(1.5f);
 *
 * auto world = scene.Node3D("World");
 * scene.Mesh3D("Crate", crateMesh).Position(0,0,-5).AttachTo(world);
 * scene.Camera3D("Eye").Position(0,2,6).LookAt({0,0,0});
 * scene.Light3D("Sun").Directional().RotationEuler(-0.7f, 0.4f, 0.f);
 *
 * // Per frame:
 * scene.Update(dt);   // scripts → animations → transforms → processes
 * scene.Render();     // 2-D pass (call RenderSystem3D for the 3-D pass)
 * ```
 */

#include <functional>
#include <string>
#include <vector>

#include "../SDL3pp_render.h"
#include "Components.h"
#include "ECS.h"
#include "Systems/AnimationSystem.h"
#include "Systems/ProcessSystem.h"
#include "Systems/RenderSystem2D.h"
#include "Systems/ScriptSystem.h"
#include "Systems/SignalSystem.h"
#include "Systems/TransformSystem.h"

namespace SDL {
namespace ECS {

/**
 * @defgroup CategoryEngineSceneBuilder Engine — Scene Builder
 *
 * Fluent factory for building scene-graph entities.
 *
 * @{
 */

class SceneBuilder; // forward

// ═════════════════════════════════════════════════════════════════════════════
// 2-D node builders
// ═════════════════════════════════════════════════════════════════════════════

/// CRTP base giving every 2-D builder the common node operations.
template<typename Derived>
struct SceneNodeBuilder {
	Context&      ecs_context;
	SceneBuilder& scene;
	EntityId      id;

	SceneNodeBuilder(Context& ctx, SceneBuilder& sc, EntityId e)
		: ecs_context(ctx), scene(sc), id(e) {}

	operator EntityId() const noexcept { return id; }
	[[nodiscard]] EntityId Id() const noexcept { return id; }

	Derived& Position(FPoint p)         { _Transform2D().position = p; return _self(); }
	Derived& Position(float x, float y) { _Transform2D().position = {x, y}; return _self(); }
	Derived& Rotation(float deg)        { _Transform2D().rotation = deg; return _self(); }
	Derived& Scale(FPoint s)            { _Transform2D().scale = s; return _self(); }
	Derived& Scale(float s)             { _Transform2D().scale = {s, s}; return _self(); }

	Derived& Hide()             { _SetVisible(false); return _self(); }
	Derived& Show()             { _SetVisible(true);  return _self(); }
	Derived& SetVisible(bool v) { _SetVisible(v);     return _self(); }

	Derived& Name(const std::string& n) {
		if (auto* t = ecs_context.Get<Tag>(id)) t->name = n;
		else ecs_context.Add<Tag>(id, {n});
		return _self();
	}

	Derived& Group(const std::string& g) {
		if (!ecs_context.Has<SceneGroup>(id)) ecs_context.Add<SceneGroup>(id);
		ecs_context.Get<SceneGroup>(id)->Add(g);
		return _self();
	}

	Derived& OnUpdate(SceneScript::UpdateFn fn) { _Script().onUpdate = std::move(fn); return _self(); }
	Derived& OnReady (SceneScript::ReadyFn  fn) { _Script().onReady  = std::move(fn); return _self(); }
	Derived& OnInput (SceneScript::InputFn  fn) { _Script().onInput  = std::move(fn); return _self(); }

	Derived& TweenTo(float* target, float to, float duration,
									 SceneTween::Ease ease = SceneTween::Ease::Out,
									 std::function<void()> onDone = nullptr) {
		const float from = target ? *target : 0.f;
		ecs_context.Add<SceneTween>(id, {target, from, to, duration, 0.f, false, ease, std::move(onDone)});
		return _self();
	}

	Derived& AttachTo(EntityId parent); // defined after SceneBuilder
	Derived& AddChild(EntityId child);  // defined after SceneBuilder

protected:
	Transform2D& _Transform2D() {
		if (!ecs_context.Has<Transform2D>(id)) ecs_context.Add<Transform2D>(id);
		return *ecs_context.Get<Transform2D>(id);
	}
	SceneScript& _Script() {
		if (!ecs_context.Has<SceneScript>(id)) ecs_context.Add<SceneScript>(id);
		return *ecs_context.Get<SceneScript>(id);
	}
	void _SetVisible(bool v) {
		if (auto* vis = ecs_context.Get<Visible>(id)) vis->value = v;
		else ecs_context.Add<Visible>(id, {v});
	}
	Derived& _self() noexcept { return static_cast<Derived&>(*this); }
};

/// Plain 2-D transform node.
struct Node2DBuilder : SceneNodeBuilder<Node2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;
};

/// Textured 2-D sprite node.
struct Sprite2DBuilder : SceneNodeBuilder<Sprite2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;
	Sprite2DBuilder& ZOrder(int z)  { if (auto* s = ecs_context.Get<Sprite>(id)) s->zOrder = z; return *this; }
	Sprite2DBuilder& Tint(Color c)  { if (auto* s = ecs_context.Get<Sprite>(id)) s->tint = c;   return *this; }
	Sprite2DBuilder& Alpha(float a) { if (auto* s = ecs_context.Get<Sprite>(id)) s->alpha = a;  return *this; }
	Sprite2DBuilder& SrcRect(FRect r){ if (auto* s = ecs_context.Get<Sprite>(id)) s->srcRect = r;return *this; }
	Sprite2DBuilder& Pivot(FPoint p){ if (auto* s = ecs_context.Get<Sprite>(id)) s->pivot = p;  return *this; }
};

/// Animated 2-D sprite node.
struct AnimSprite2DBuilder : SceneNodeBuilder<AnimSprite2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;
	AnimSprite2DBuilder& AddFrame(FRect r) { if (auto* a = ecs_context.Get<AnimatedSprite>(id)) a->frames.push_back(r); return *this; }
	AnimSprite2DBuilder& AddFrames(std::initializer_list<FRect> rects) {
		if (auto* a = ecs_context.Get<AnimatedSprite>(id)) for (const FRect& r : rects) a->frames.push_back(r);
		return *this;
	}
	AnimSprite2DBuilder& Spritesheet(int frameW, int frameH, int count, int row = 0) {
		if (auto* a = ecs_context.Get<AnimatedSprite>(id)) {
			a->frames.clear();
			for (int i = 0; i < count; ++i)
				a->frames.push_back({static_cast<float>(i * frameW), static_cast<float>(row * frameH),
														 static_cast<float>(frameW), static_cast<float>(frameH)});
		}
		return *this;
	}
	AnimSprite2DBuilder& FPS(float fps) { if (auto* a = ecs_context.Get<AnimatedSprite>(id)) a->fps = fps;  return *this; }
	AnimSprite2DBuilder& Loop(bool l = true) { if (auto* a = ecs_context.Get<AnimatedSprite>(id)) a->loop = l; return *this; }
	AnimSprite2DBuilder& Play(bool p = true) { if (auto* a = ecs_context.Get<AnimatedSprite>(id)) a->playing = p; return *this; }
	AnimSprite2DBuilder& ZOrder(int z)  { if (auto* a = ecs_context.Get<AnimatedSprite>(id)) a->zOrder = z; return *this; }
	AnimSprite2DBuilder& Tint(Color c)  { if (auto* a = ecs_context.Get<AnimatedSprite>(id)) a->tint = c;   return *this; }
};

/// 2-D camera node.
struct Camera2DBuilder : SceneNodeBuilder<Camera2DBuilder> {
	using SceneNodeBuilder::SceneNodeBuilder;
	Camera2DBuilder& Zoom(float z)    { if (auto* c = ecs_context.Get<SceneCamera>(id)) c->zoom = z;   return *this; }
	Camera2DBuilder& Offset(FPoint o) { if (auto* c = ecs_context.Get<SceneCamera>(id)) c->offset = o; return *this; }
	Camera2DBuilder& Follow(EntityId target) {
		OnUpdate([target](EntityId self, Context& ctx, float) {
			if (!ctx.IsAlive(target)) return;
			const auto* gt = ctx.Get<GlobalTransform2D>(target);
			if (!gt) return;
			if (auto* cam = ctx.Get<SceneCamera>(self)) cam->offset = gt->position;
		});
		return *this;
	}
};

// ═════════════════════════════════════════════════════════════════════════════
// 3-D node builders
// ═════════════════════════════════════════════════════════════════════════════

/// CRTP base giving every 3-D builder the common node operations.
template<typename Derived>
struct SceneNode3DBuilder {
	Context&      ecs_context;
	SceneBuilder& scene;
	EntityId      id;

	SceneNode3DBuilder(Context& ctx, SceneBuilder& sc, EntityId e)
		: ecs_context(ctx), scene(sc), id(e) {}

	operator EntityId() const noexcept { return id; }
	[[nodiscard]] EntityId Id() const noexcept { return id; }

	Derived& Position(const FVector3& p)      { _Transform().position = p; return _self(); }
	Derived& Position(float x, float y, float z) { _Transform().position = {x, y, z}; return _self(); }
	Derived& Rotation(const FQuaternion& q)   { _Transform().rotation = q; return _self(); }
	Derived& RotationEuler(float pitch, float yaw, float roll) {
		_Transform().rotation = FQuaternion::FromEuler(pitch, yaw, roll); return _self();
	}
	Derived& Scale(const FVector3& s)         { _Transform().scale = s; return _self(); }
	Derived& Scale(float s)                   { _Transform().scale = {s, s, s}; return _self(); }

	/// Orient the node to look toward `target` (world space, +Y up).
	Derived& LookAt(const FVector3& target, const FVector3& up = {0.f, 1.f, 0.f}) {
		Transform3D& t = _Transform();
		const FVector3 dir = (target - t.position).Normalize();
		const FMatrix4 view = FMatrix4::LookAt(t.position, target, up);
		(void)view;
		t.rotation = FQuaternion::FromTo({0.f, 0.f, -1.f}, dir);
		return _self();
	}

	Derived& Hide()             { _SetVisible(false); return _self(); }
	Derived& Show()             { _SetVisible(true);  return _self(); }
	Derived& SetVisible(bool v) { _SetVisible(v);     return _self(); }

	Derived& Name(const std::string& n) {
		if (auto* t = ecs_context.Get<Tag>(id)) t->name = n;
		else ecs_context.Add<Tag>(id, {n});
		return _self();
	}
	Derived& Group(const std::string& g) {
		if (!ecs_context.Has<SceneGroup>(id)) ecs_context.Add<SceneGroup>(id);
		ecs_context.Get<SceneGroup>(id)->Add(g);
		return _self();
	}

	Derived& OnUpdate(SceneScript::UpdateFn fn) { _Script().onUpdate = std::move(fn); return _self(); }
	Derived& OnReady (SceneScript::ReadyFn  fn) { _Script().onReady  = std::move(fn); return _self(); }
	Derived& OnInput (SceneScript::InputFn  fn) { _Script().onInput  = std::move(fn); return _self(); }

	/// Attach a keyframe animation clip that drives this node's transform.
	Derived& Animate(std::shared_ptr<AnimationClip> clip, float speed = 1.f, bool playing = true) {
		ecs_context.Add<AnimationPlayer>(id, {std::move(clip), 0.f, speed, playing});
		return _self();
	}

	Derived& AttachTo(EntityId parent); // defined after SceneBuilder
	Derived& AddChild(EntityId child);  // defined after SceneBuilder

protected:
	Transform3D& _Transform() {
		if (!ecs_context.Has<Transform3D>(id)) ecs_context.Add<Transform3D>(id);
		return *ecs_context.Get<Transform3D>(id);
	}
	SceneScript& _Script() {
		if (!ecs_context.Has<SceneScript>(id)) ecs_context.Add<SceneScript>(id);
		return *ecs_context.Get<SceneScript>(id);
	}
	void _SetVisible(bool v) {
		if (auto* vis = ecs_context.Get<Visible>(id)) vis->value = v;
		else ecs_context.Add<Visible>(id, {v});
	}
	Derived& _self() noexcept { return static_cast<Derived&>(*this); }
};

/// Plain 3-D transform node.
struct Node3DBuilder : SceneNode3DBuilder<Node3DBuilder> {
	using SceneNode3DBuilder::SceneNode3DBuilder;
};

/// 3-D mesh-renderer node.
struct Mesh3DBuilder : SceneNode3DBuilder<Mesh3DBuilder> {
	using SceneNode3DBuilder::SceneNode3DBuilder;
	Mesh3DBuilder& Mesh(std::shared_ptr<GpuMesh> m)  { _R().mesh = std::move(m); return *this; }
	Mesh3DBuilder& SetMaterial(const Material& mat)  { _R().material = mat; return *this; }
	Mesh3DBuilder& BaseColor(const FVector4& c)      { _R().material.baseColor = c; return *this; }
	Mesh3DBuilder& CastShadow(bool s = true)         { _R().castShadow = s; return *this; }
	Mesh3DBuilder& Layer(int l)                      { _R().layer = l; return *this; }
	/// Add a triangle-precise collider sharing CPU geometry.
	Mesh3DBuilder& WithMeshCollider(std::shared_ptr<SDL::Mesh> cpu, bool isStatic = true) {
		ecs_context.Add<MeshCollider>(id, {std::move(cpu), isStatic});
		return *this;
	}
private:
	MeshRenderer& _R() {
		if (!ecs_context.Has<MeshRenderer>(id)) ecs_context.Add<MeshRenderer>(id);
		return *ecs_context.Get<MeshRenderer>(id);
	}
};

/// 3-D camera node.
struct Camera3DBuilder : SceneNode3DBuilder<Camera3DBuilder> {
	using SceneNode3DBuilder::SceneNode3DBuilder;
	Camera3DBuilder& Fov(float radians)  { _C().fovY = radians; return *this; }
	Camera3DBuilder& FovDegrees(float d) { _C().fovY = SDL::DegToRad(d); return *this; }
	Camera3DBuilder& Clip(float n, float f) { _C().nearZ = n; _C().farZ = f; return *this; }
	Camera3DBuilder& Perspective()       { _C().projection = Camera3D::Projection::Perspective; return *this; }
	Camera3DBuilder& Orthographic(float height) {
		_C().projection = Camera3D::Projection::Orthographic; _C().orthoHeight = height; return *this;
	}
	Camera3DBuilder& Primary(bool p = true) { _C().primary = p; return *this; }
private:
	Camera3D& _C() {
		if (!ecs_context.Has<Camera3D>(id)) ecs_context.Add<Camera3D>(id);
		return *ecs_context.Get<Camera3D>(id);
	}
};

/// 3-D light node.
struct Light3DBuilder : SceneNode3DBuilder<Light3DBuilder> {
	using SceneNode3DBuilder::SceneNode3DBuilder;
	Light3DBuilder& Directional() { _L().type = Light::Type::Directional; return *this; }
	Light3DBuilder& Point()       { _L().type = Light::Type::Point; return *this; }
	Light3DBuilder& Spot()        { _L().type = Light::Type::Spot; return *this; }
	Light3DBuilder& LightColor(const FVector3& c) { _L().color = c; return *this; }
	Light3DBuilder& Intensity(float i) { _L().intensity = i; return *this; }
	Light3DBuilder& Range(float r)     { _L().range = r; return *this; }
	Light3DBuilder& Cone(float inner, float outer) { _L().innerCone = inner; _L().outerCone = outer; return *this; }
private:
	Light& _L() {
		if (!ecs_context.Has<Light>(id)) ecs_context.Add<Light>(id);
		return *ecs_context.Get<Light>(id);
	}
};

// ═════════════════════════════════════════════════════════════════════════════
// SceneGraph — hierarchy manager
// ═════════════════════════════════════════════════════════════════════════════

/// Owns node creation/destruction and parent-child links over an ECS context.
class SceneGraph {
public:
	explicit SceneGraph(Context& ctx, RendererRef renderer)
		: m_ctx(ctx), m_renderer(renderer) {}

	/// Create a 2-D node (Transform2D + GlobalTransform2D + Visible [+ Tag]).
	EntityRef CreateNode(const std::string& name = {}, EntityId parentId = NullEntity) {
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
		AnimationSystem::Run(m_ctx, dt);
		PropagateTransforms2D(m_ctx);
		PropagateTransforms3D(m_ctx);
	}

	void DispatchInput(const SDL::Event& ev) { ScriptSystem::DispatchInput(m_ctx, ev); }
	void Render() { RenderScene(m_ctx, m_renderer); }

	/// Set the active 2-D camera (stored on a dedicated entity).
	void SetCamera(const SceneCamera& cam) {
		if (m_cameraEntity == NullEntity || !m_ctx.IsAlive(m_cameraEntity))
			m_cameraEntity = m_ctx.CreateEntity();
		m_ctx.Add<SceneCamera>(m_cameraEntity, cam);
	}

	/// Get the active 2-D camera (default-constructed if none set).
	[[nodiscard]] SceneCamera GetCamera() const {
		if (m_cameraEntity != NullEntity)
			if (const SceneCamera* c = m_ctx.Get<SceneCamera>(m_cameraEntity))
				return *c;
		return {};
	}

	[[nodiscard]] Context&    GetCtx()      const noexcept { return m_ctx; }
	[[nodiscard]] RendererRef GetRenderer() const noexcept { return m_renderer; }

	[[nodiscard]] EntityId FindByName(const std::string& name) const {
		EntityId result = NullEntity;
		const_cast<Context&>(m_ctx).Each<Tag>([&](EntityId e, const Tag& tag) {
			if (tag.name == name) result = e;
		});
		return result;
	}

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

// ═════════════════════════════════════════════════════════════════════════════
// SceneBuilder — fluent factory
// ═════════════════════════════════════════════════════════════════════════════

/// High-level fluent scene construction API (2-D and 3-D).
class SceneBuilder {
public:
	SceneBuilder(Context& ctx, RendererRef renderer)
		: m_graph(ctx, renderer), m_ctx(ctx), m_renderer(renderer) {}

	// ── 2-D factories ─────────────────────────────────────────────────────────
	Node2DBuilder Node2D(const std::string& name = {}) {
		return Node2DBuilder{m_ctx, *this, _Spawn2D(name)};
	}
	Sprite2DBuilder Sprite2D(const std::string& name, TextureRef tex = {}) {
		EntityId id = _Spawn2D(name);
		m_ctx.Add<Sprite>(id, {tex});
		return Sprite2DBuilder{m_ctx, *this, id};
	}
	AnimSprite2DBuilder AnimSprite2D(const std::string& name, TextureRef tex = {}) {
		EntityId id = _Spawn2D(name);
		AnimatedSprite as; as.texture = tex;
		m_ctx.Add<AnimatedSprite>(id, std::move(as));
		return AnimSprite2DBuilder{m_ctx, *this, id};
	}
	Camera2DBuilder Camera2D(const std::string& name = "Camera2D") {
		EntityId id = _Spawn2D(name);
		m_ctx.Add<SceneCamera>(id);
		return Camera2DBuilder{m_ctx, *this, id};
	}

	// ── 3-D factories ─────────────────────────────────────────────────────────
	Node3DBuilder Node3D(const std::string& name = {}) {
		return Node3DBuilder{m_ctx, *this, _Spawn3D(name)};
	}
	Mesh3DBuilder Mesh3D(const std::string& name, std::shared_ptr<GpuMesh> mesh = {}) {
		EntityId id = _Spawn3D(name);
		MeshRenderer mr; mr.mesh = std::move(mesh);
		m_ctx.Add<MeshRenderer>(id, std::move(mr));
		return Mesh3DBuilder{m_ctx, *this, id};
	}
	Camera3DBuilder Camera3D(const std::string& name = "Camera3D") {
		EntityId id = _Spawn3D(name);
		m_ctx.Add<SDL::ECS::Camera3D>(id); // qualify: the method name shadows the type here
		return Camera3DBuilder{m_ctx, *this, id};
	}
	Light3DBuilder Light3D(const std::string& name = "Light") {
		EntityId id = _Spawn3D(name);
		m_ctx.Add<Light>(id);
		return Light3DBuilder{m_ctx, *this, id};
	}

	// ── Root management ─────────────────────────────────────────────────────────
	void SetRoot(EntityId e) { m_root = e; }
	[[nodiscard]] EntityId GetRoot() const noexcept { return m_root; }

	// ── Signals & processes ─────────────────────────────────────────────────────
	void Connect(const std::string& node, const std::string& signal, SignalBus::Handler h) {
		m_bus.Connect(node, signal, std::move(h));
	}
	void Emit(const std::string& node, const std::string& signal) { m_bus.Emit(node, signal); }
	void Emit(EntityId e, const std::string& signal) {
		if (const Tag* t = m_ctx.Get<Tag>(e)) m_bus.Emit(t->name, signal);
	}
	[[nodiscard]] SignalBus&      GetBus()       noexcept { return m_bus; }
	[[nodiscard]] ProcessManager& GetProcesses() noexcept { return m_processes; }

	/// Launch a process now.
	std::weak_ptr<Process> Run(std::shared_ptr<Process> p) { return m_processes.Attach(std::move(p)); }

	// ── Per-frame ───────────────────────────────────────────────────────────────
	/// Scripts → animations → transforms (2-D + 3-D) → processes.
	void Update(float dt = 0.f) {
		ScriptSystem::Run(m_ctx, dt);
		AnimationSystem::Run(m_ctx, dt);
		PropagateTransforms2D(m_ctx);
		PropagateTransforms3D(m_ctx);
		m_processes.Update(dt);
	}
	void DispatchInput(const SDL::Event& ev) { ScriptSystem::DispatchInput(m_ctx, ev); }
	/// Render the 2-D pass (sprites). Use `RenderSystem3D` for the 3-D pass.
	void Render() { RenderScene(m_ctx, m_renderer); }

	// ── Hierarchy helpers (called by builders) ──────────────────────────────────
	void _AttachTo(EntityId child, EntityId parent) { m_graph.SetParent(child, parent); }
	void _AddChild(EntityId parent, EntityId child) { m_graph.SetParent(child, parent); }

	// ── Utilities ───────────────────────────────────────────────────────────────
	[[nodiscard]] EntityId FindByName(const std::string& n) const { return m_graph.FindByName(n); }
	[[nodiscard]] std::vector<EntityId> FindGroup(const std::string& g) const { return m_graph.FindGroup(g); }
	void DestroyNode(EntityId e) { m_graph.DestroyNode(e); }

	[[nodiscard]] Context&    GetCtx()      noexcept { return m_ctx; }
	[[nodiscard]] RendererRef GetRenderer() noexcept { return m_renderer; }
	[[nodiscard]] SceneGraph& GetGraph()    noexcept { return m_graph; }

private:
	SceneGraph     m_graph;
	Context&       m_ctx;
	RendererRef    m_renderer;
	EntityId       m_root = NullEntity;
	SignalBus      m_bus;
	ProcessManager m_processes;

	EntityId _Spawn2D(const std::string& name) {
		EntityId id = m_ctx.CreateEntity();
		m_ctx.Add<Transform2D>(id);
		m_ctx.Add<GlobalTransform2D>(id);
		m_ctx.Add<Visible>(id, {true});
		if (!name.empty()) m_ctx.Add<Tag>(id, {name});
		return id;
	}
	EntityId _Spawn3D(const std::string& name) {
		EntityId id = m_ctx.CreateEntity();
		m_ctx.Add<Transform3D>(id);
		m_ctx.Add<GlobalTransform3D>(id);
		m_ctx.Add<Visible>(id, {true});
		if (!name.empty()) m_ctx.Add<Tag>(id, {name});
		return id;
	}
};

// ═════════════════════════════════════════════════════════════════════════════
// Deferred builder methods (need SceneBuilder to be complete)
// ═════════════════════════════════════════════════════════════════════════════

template<typename Derived>
Derived& SceneNodeBuilder<Derived>::AttachTo(EntityId parent) { scene._AttachTo(id, parent); return _self(); }
template<typename Derived>
Derived& SceneNodeBuilder<Derived>::AddChild(EntityId child)  { scene._AddChild(id, child);  return _self(); }

template<typename Derived>
Derived& SceneNode3DBuilder<Derived>::AttachTo(EntityId parent) { scene._AttachTo(id, parent); return _self(); }
template<typename Derived>
Derived& SceneNode3DBuilder<Derived>::AddChild(EntityId child)  { scene._AddChild(id, child);  return _self(); }

/** @} */ // CategoryEngineSceneBuilder

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_SCENE_BUILDER_H_
