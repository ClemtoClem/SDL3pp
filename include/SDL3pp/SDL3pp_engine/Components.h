#ifndef SDL3PP_ENGINE_COMPONENTS_H_
#define SDL3PP_ENGINE_COMPONENTS_H_

/**
 * @file Components.h
 * @brief All built-in engine components (2D and 3D) in one place.
 *
 * Components are plain data attached to ECS entities. Systems (one per file,
 * see `Systems/`) operate on them. This header gathers every component the
 * engine ships so a game only needs to include it once.
 *
 * | Group        | Components |
 * |--------------|-----------|
 * | Hierarchy    | `Tag`, `Visible`, `SceneParent`, `SceneChildren`, `SceneGroup` |
 * | Transform 2D | `Transform2D`, `GlobalTransform2D` |
 * | Transform 3D | `Transform3D`, `GlobalTransform3D` |
 * | Render 2D    | `Sprite`, `AnimatedSprite`, `SceneCamera` |
 * | Render 3D    | `MeshRenderer`, `Material`, `Camera3D`, `Light` |
 * | Physics      | `Collider2D`, `Collider3D`, `RigidBody2D`, `RigidBody3D`, `CollisionLayer` |
 * | Behaviour    | `SceneScript`, `SceneTween`, `AnimationPlayer` |
 *
 * All components live in `SDL::ECS` for backward compatibility with the
 * original scene module.
 */

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../SDL3pp_events.h"
#include "../SDL3pp_pixels.h"
#include "../SDL3pp_rect.h"
#include "../SDL3pp_render.h"
#include "ECS.h"
#include "Math3D.h"
#include "Mesh.h"

namespace SDL {

namespace ECS {

/**
 * @defgroup CategoryEngineComponents Engine — Components
 *
 * Built-in 2D and 3D components for entities.
 *
 * @{
 */

// ═════════════════════════════════════════════════════════════════════════════
// Hierarchy & identity
// ═════════════════════════════════════════════════════════════════════════════

/// Human-readable node name (used by `FindByName`).
struct Tag { std::string name; };

/// Visibility flag honoured by the render systems.
struct Visible { bool value = true; };

/// Parent link in the scene graph (`NullEntity` for roots).
struct SceneParent { EntityId id = NullEntity; };

/// Ordered list of child entities.
struct SceneChildren {
	std::vector<EntityId> ids;
	void Add   (EntityId c) { if (std::ranges::find(ids, c) == ids.end()) ids.push_back(c); }
	void Remove(EntityId c) { std::erase(ids, c); }
};

/// Multi-valued group membership (e.g. "enemies", "pickups").
struct SceneGroup {
	std::vector<std::string> groups;
	void Add   (const std::string& g) { groups.push_back(g); }
	bool Has   (const std::string& g) const { return std::ranges::find(groups, g) != groups.end(); }
	void Remove(const std::string& g) { std::erase(groups, g); }
};

// ═════════════════════════════════════════════════════════════════════════════
// 2D transforms
// ═════════════════════════════════════════════════════════════════════════════

/// Local 2-D transform (scene-space pixels). Rotation in degrees, clockwise.
struct Transform2D {
	FPoint position = {0.f, 0.f};
	float  rotation = 0.f;
	FPoint scale    = {1.f, 1.f};

	constexpr Transform2D() = default;
	constexpr Transform2D(FPoint pos, float rot = 0.f, FPoint scl = {1.f, 1.f})
		: position(pos), rotation(rot), scale(scl) {}
};

/// World-space 2-D transform produced by the transform system. Read-only.
struct GlobalTransform2D {
	FPoint position = {0.f, 0.f};
	float  rotation = 0.f;
	FPoint scale    = {1.f, 1.f};

	/// Centred destination rect for an image of size `w`×`h`.
	[[nodiscard]] FRect DestRect(float w, float h) const noexcept {
		return {position.x - w * scale.x * 0.5f,
						position.y - h * scale.y * 0.5f,
						w * scale.x, h * scale.y};
	}

	/// Combine this parent world transform with a child's local transform.
	[[nodiscard]] GlobalTransform2D Combine(const Transform2D& child) const noexcept {
		const float parentRad = SDL::DegToRad(rotation);
		const float cosP = SDL::Cos(parentRad);
		const float sinP = SDL::Sin(parentRad);
		const float lx = child.position.x * scale.x;
		const float ly = child.position.y * scale.y;
		return {
			.position = {position.x + cosP * lx - sinP * ly,
									 position.y + sinP * lx + cosP * ly},
			.rotation = rotation + child.rotation,
			.scale    = {scale.x * child.scale.x, scale.y * child.scale.y}};
	}
};

// ═════════════════════════════════════════════════════════════════════════════
// 3D transforms
// ═════════════════════════════════════════════════════════════════════════════

/// Local 3-D transform: position, quaternion rotation, non-uniform scale.
struct Transform3D {
	FVector3    position{0.f, 0.f, 0.f};
	FQuaternion rotation = FQuaternion::Identity();
	FVector3    scale{1.f, 1.f, 1.f};

	constexpr Transform3D() = default;
	Transform3D(const FVector3& p,
							const FQuaternion& r = FQuaternion::Identity(),
							const FVector3& s = {1.f, 1.f, 1.f})
		: position(p), rotation(r), scale(s) {}

	/// Local model matrix (T * R * S).
	[[nodiscard]] FMatrix4 LocalMatrix() const noexcept {
		return ComposeTRS(position, rotation, scale);
	}

	/// Forward direction (-Z in local space, rotated into the local frame).
	[[nodiscard]] FVector3 Forward() const noexcept { return rotation.Rotate({0.f, 0.f, -1.f}); }
	/// Right direction (+X).
	[[nodiscard]] FVector3 Right()   const noexcept { return rotation.Rotate({1.f, 0.f, 0.f}); }
	/// Up direction (+Y).
	[[nodiscard]] FVector3 Up()      const noexcept { return rotation.Rotate({0.f, 1.f, 0.f}); }
};

/// World-space 3-D transform produced by the transform system. Read-only.
struct GlobalTransform3D {
	FMatrix4 world = FMatrix4::Identity();

	/// World translation.
	[[nodiscard]] FVector3 Position() const noexcept { return {world.m[12], world.m[13], world.m[14]}; }

	/// Combine this parent world matrix with a child's local matrix.
	[[nodiscard]] GlobalTransform3D Combine(const Transform3D& child) const noexcept {
		return {world * child.LocalMatrix()};
	}
};

// ═════════════════════════════════════════════════════════════════════════════
// 2D rendering
// ═════════════════════════════════════════════════════════════════════════════

/// Static textured sprite drawn by the 2D render system.
struct Sprite {
	TextureRef texture;
	FRect      srcRect = {};
	FPoint     pivot   = {0.5f, 0.5f};
	Color      tint    = {255, 255, 255, 255};
	int        zOrder  = 0;
	float      alpha   = 1.f;
};

/// Sprite-sheet animation playing a sequence of source rects from one texture.
struct AnimatedSprite {
	TextureRef         texture;
	std::vector<FRect> frames;
	float              fps     = 12.f;
	bool               loop    = true;
	bool               playing = true;
	int                frame   = 0;
	float              elapsed = 0.f;
	FPoint             pivot   = {0.5f, 0.5f};
	Color              tint    = {255, 255, 255, 255};
	int                zOrder  = 0;
	float              alpha   = 1.f;

	/// Advance animation by `dt` seconds.
	void Update(float dt) noexcept {
		if (!playing || frames.empty() || fps <= 0.f) return;
		elapsed += dt;
		const float period = 1.f / fps;
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
		return frames[static_cast<size_t>(std::clamp(frame, 0, static_cast<int>(frames.size()) - 1))];
	}
};

/// 2-D camera: offset and zoom applied to the whole scene during rendering.
struct SceneCamera {
	FPoint offset    = {0.f, 0.f};
	float  zoom      = 1.f;
	int    viewportW = 0;
	int    viewportH = 0;
};

// ═════════════════════════════════════════════════════════════════════════════
// 3D rendering
// ═════════════════════════════════════════════════════════════════════════════

/// Surface appearance for a 3D mesh (Phong-style, shader-agnostic).
struct Material {
	FVector4   baseColor{1.f, 1.f, 1.f, 1.f};
	FVector4   emissive{0.f, 0.f, 0.f, 1.f};
	float      metallic  = 0.f;
	float      roughness = 0.5f;
	float      shininess = 32.f;
	TextureRef albedo;          ///< optional 2D texture handle (renderer-defined use)
	int        userId    = -1;  ///< free slot for app material/pipeline indexing
};

/// Renders a `GpuMesh` at the entity's `GlobalTransform3D`.
struct MeshRenderer {
	std::shared_ptr<GpuMesh> mesh;
	Material                 material;
	bool                     castShadow = true;
	bool                     visible    = true;
	int                      layer      = 0;  ///< render-layer / sort key
};

/// 3-D camera. The view matrix is derived from the entity's GlobalTransform3D.
struct Camera3D {
	enum class Projection { Perspective, Orthographic };

	Projection projection  = Projection::Perspective;
	float      fovY        = SDL::DegToRad(60.f); ///< vertical FOV in radians
	float      aspect      = 0.f;                  ///< 0 → derive from viewport
	float      nearZ       = 0.1f;
	float      farZ        = 1000.f;
	float      orthoHeight = 10.f;                 ///< view height for ortho
	bool       primary     = true;                 ///< the camera used for rendering

	/// Projection matrix (Vulkan clip space). Pass the target aspect ratio.
	[[nodiscard]] FMatrix4 ProjectionMatrix(float viewportAspect) const noexcept {
		const float a = aspect > 0.f ? aspect : viewportAspect;
		if (projection == Projection::Orthographic) {
			const float h = orthoHeight * 0.5f;
			const float w = h * a;
			return FMatrix4::Ortho(-w, w, -h, h, nearZ, farZ);
		}
		return FMatrix4::Perspective(fovY, a, nearZ, farZ);
	}

	/// View matrix for a camera positioned by `world` (inverse of the world matrix).
	[[nodiscard]] static FMatrix4 ViewMatrix(const GlobalTransform3D& world) noexcept {
		return world.world.Inverse();
	}
};

/// Light source. `Directional` uses the entity's forward axis as direction.
struct Light {
	enum class Type { Directional, Point, Spot };

	Type     type      = Type::Directional;
	FVector3 color{1.f, 1.f, 1.f};
	float    intensity = 1.f;
	float    range     = 20.f;                 ///< point/spot attenuation radius
	float    innerCone = SDL::DegToRad(20.f);  ///< spot inner cone (radians)
	float    outerCone = SDL::DegToRad(30.f);  ///< spot outer cone (radians)
	bool     castShadow = false;
};

// ═════════════════════════════════════════════════════════════════════════════
// Physics & collision
// ═════════════════════════════════════════════════════════════════════════════

/// Layer/mask used to filter collisions and raycasts.
struct CollisionLayer {
	Uint32 layer = 1u;        ///< the bit this object occupies
	Uint32 mask  = 0xFFFFFFFFu; ///< the layers it collides with

	[[nodiscard]] bool CollidesWith(const CollisionLayer& o) const noexcept {
		return (mask & o.layer) != 0 && (o.mask & layer) != 0;
	}
};

/// 2-D collider: an axis-aligned rect relative to the entity's Transform2D.
struct Collider2D {
	FRect bounds;            ///< local-space rect (x,y is offset from position)
	bool  isStatic = false;
	bool  isTrigger = false; ///< trigger: report overlaps but no response

	/// World-space rect given a world position.
	[[nodiscard]] FRect WorldRect(FPoint worldPos) const noexcept {
		return {worldPos.x + bounds.x, worldPos.y + bounds.y, bounds.w, bounds.h};
	}
};

/// 3-D collider: a local AABB (and optional sphere) relative to Transform3D.
struct Collider3D {
	FAABB bounds;            ///< local-space box
	float sphereRadius = 0.f;///< >0 enables sphere narrow-phase at the centre
	bool  isStatic  = false;
	bool  isTrigger = false;

	/// World-space AABB translated to `worldPos` (no rotation — broad phase).
	[[nodiscard]] FAABB WorldAABB(const FVector3& worldPos) const noexcept {
		return bounds.Translated(worldPos);
	}
};

/// Triangle-precise collider holding CPU mesh geometry (for exact raycasts).
struct MeshCollider {
	std::shared_ptr<Mesh> mesh;     ///< CPU geometry used for narrow-phase tests
	bool                  isStatic = true;

	/// Local-space bounds (falls back to an empty box when no mesh is set).
	[[nodiscard]] FAABB Bounds() const noexcept { return mesh ? mesh->bounds : FAABB{}; }
};

/// Simple 2-D rigid body for kinematic/euler integration.
struct RigidBody2D {
	FVector2 velocity{0.f, 0.f};
	FVector2 acceleration{0.f, 0.f};
	float    mass        = 1.f;
	float    drag        = 0.f;
	float    gravityScale = 0.f; ///< multiplies the world gravity
	bool     kinematic    = false;
};

/// Simple 3-D rigid body for kinematic/euler integration.
struct RigidBody3D {
	FVector3 velocity{0.f, 0.f, 0.f};
	FVector3 acceleration{0.f, 0.f, 0.f};
	float    mass         = 1.f;
	float    drag         = 0.f;
	float    gravityScale = 1.f;
	bool     kinematic    = false;
};

// ═════════════════════════════════════════════════════════════════════════════
// Behaviour: scripts, tweens, animation
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Per-node script callbacks (analogous to attaching a behaviour script).
 * Run each frame by the script system. All callbacks are optional.
 */
struct SceneScript {
	using UpdateFn = std::function<void(EntityId, Context&, float)>;
	using ReadyFn  = std::function<void(EntityId, Context&)>;
	using InputFn  = std::function<void(EntityId, Context&, const SDL::Event&)>;

	UpdateFn onUpdate;
	ReadyFn  onReady;
	InputFn  onInput;
	bool     readyCalled = false;
};

/// Interpolates a single `float*` from `from` to `to` over `duration` seconds.
struct SceneTween {
	float* target   = nullptr;
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
		const float t = std::clamp(elapsed / duration, 0.f, 1.f);
		*target = from + (to - from) * _Ease(t);
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
		case Ease::InOut: return t < 0.5f ? 2 * t * t : 1.f - 2 * (1 - t) * (1 - t);
		default:          return t;
		}
	}
};

/// One transform keyframe at a point in time.
struct Keyframe {
	float       time = 0.f;
	FVector3    position{0.f, 0.f, 0.f};
	FQuaternion rotation = FQuaternion::Identity();
	FVector3    scale{1.f, 1.f, 1.f};
};

/// A named, reusable animation clip (shared resource).
struct AnimationClip {
	std::string           name;
	std::vector<Keyframe> keys;   ///< sorted by `time`
	float                 duration = 0.f;
	bool                  loop     = true;

	/// Sample the clip at `t` seconds into local-transform values.
	[[nodiscard]] Keyframe Sample(float t) const noexcept {
		if (keys.empty()) return {};
		if (keys.size() == 1) return keys.front();
		if (loop && duration > 0.f) t = SDL::Fmod(t, duration);
		if (t <= keys.front().time) return keys.front();
		if (t >= keys.back().time)  return keys.back();
		for (size_t i = 1; i < keys.size(); ++i) {
			if (t <= keys[i].time) {
				const Keyframe& a = keys[i - 1];
				const Keyframe& b = keys[i];
				const float span = b.time - a.time;
				const float f = span > 1e-6f ? (t - a.time) / span : 0.f;
				Keyframe out;
				out.time     = t;
				out.position = a.position.Lerp(b.position, f);
				out.rotation = a.rotation.Slerp(b.rotation, f);
				out.scale    = a.scale.Lerp(b.scale, f);
				return out;
			}
		}
		return keys.back();
	}
};

/// Plays an `AnimationClip`, writing sampled values into the entity's Transform3D.
struct AnimationPlayer {
	std::shared_ptr<AnimationClip> clip;
	float time    = 0.f;
	float speed   = 1.f;
	bool  playing = true;
};

/** @} */ // CategoryEngineComponents

} // namespace ECS

} // namespace SDL

#endif // SDL3PP_ENGINE_COMPONENTS_H_
