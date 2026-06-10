#ifndef SDL3PP_ENGINE_RAYCAST_SYSTEM_H_
#define SDL3PP_ENGINE_RAYCAST_SYSTEM_H_

/**
 * @file Systems/RaycastSystem.h
 * @brief Ray casting against the ECS world — distances, nearest-hit queries and
 *        line-of-sight checks. Ideal for shooters and AI perception.
 *
 * `RaycastWorld` snapshots all `Collider3D` / `MeshCollider` entities into an
 * octree once, then answers many ray queries cheaply (broad-phase octree +
 * precise narrow-phase: ray vs AABB, sphere, or triangle mesh). Build it once
 * after transforms are propagated, then fire as many rays as you like the same
 * frame.
 *
 * ```cpp
 * SDL::ECS::RaycastWorld world(ctx);
 * FRay aim{muzzlePos, aimDir};
 * if (auto hit = world.Raycast(aim, 100.f, weaponMask, shooterId)) {
 *     ApplyDamage(hit.entity, hit.point, hit.normal);
 * }
 * bool canSee = world.LineOfSight(enemyEye, playerHead);
 * float wallDist = world.DistanceToObstacle(pos, forward, 50.f);
 * ```
 */

#include <limits>
#include <unordered_map>
#include <vector>

#include "../Collisions.h"
#include "../Components.h"
#include "../ECS.h"
#include "../Octree.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/// Outcome of a world raycast.
struct RaycastResult {
	bool     hit      = false;
	EntityId entity   = NullEntity;
	float    distance = 0.f;
	FVector3 point;
	FVector3 normal;

	explicit operator bool() const noexcept { return hit; }
};

/**
 * Spatial snapshot for fast repeated ray queries.
 *
 * Rebuild it whenever colliders move (typically once per frame, after
 * `PropagateTransforms3D`).
 */
class RaycastWorld {
public:
	/// Build from every `Collider3D` and `MeshCollider` in the world.
	explicit RaycastWorld(Context& ctx) : m_tree(FAABB({-1, -1, -1}, {1, 1, 1})) { Rebuild(ctx); }

	/// Recompute the octree and per-entity records.
	void Rebuild(Context& ctx) {
		m_records.clear();
		FAABB worldBounds;
		std::vector<Physics::OctreeItem> items;

		ctx.Each<Collider3D, GlobalTransform3D>([&](EntityId e, Collider3D& c, GlobalTransform3D& gt) {
			const FVector3 pos = gt.Position();
			Record r;
			r.box   = c.WorldAABB(pos);
			r.layer = LayerOf(ctx, e);
			if (c.sphereRadius > 0.f) { r.kind = Kind::Sphere; r.center = pos; r.radius = c.sphereRadius; }
			else                        r.kind = Kind::Box;
			m_records[e] = r;
			items.push_back({e, r.box});
			worldBounds.Expand(r.box);
		});

		ctx.Each<MeshCollider, GlobalTransform3D>([&](EntityId e, MeshCollider& mc, GlobalTransform3D& gt) {
			if (!mc.mesh) return;
			Record r;
			r.kind  = Kind::Mesh;
			r.model = gt.world;
			r.mesh  = mc.mesh;
			r.box   = mc.mesh->bounds.Transformed(gt.world);
			r.layer = LayerOf(ctx, e);
			m_records[e] = r;
			items.push_back({e, r.box});
			worldBounds.Expand(r.box);
		});

		if (!worldBounds.IsValid()) worldBounds = FAABB({-1, -1, -1}, {1, 1, 1});
		else { const FVector3 m{1, 1, 1}; worldBounds = {worldBounds.Min - m, worldBounds.Max + m}; }
		m_tree = Physics::Octree(worldBounds);
		m_tree.Build(items);
	}

	/**
	 * Cast a ray and return the nearest hit.
	 *
	 * @param ray       origin + (normalised) direction.
	 * @param maxDist   maximum travel distance.
	 * @param layerMask only consider entities whose layer bit is set here.
	 * @param ignore    an entity to skip (e.g. the shooter).
	 */
	[[nodiscard]] RaycastResult Raycast(const FRay& ray,
																			float maxDist = std::numeric_limits<float>::infinity(),
																			Uint32 layerMask = 0xFFFFFFFFu,
																			EntityId ignore = NullEntity) const {
		RaycastResult best;
		float bestDist = maxDist;
		m_tree.QueryRay(ray, [&](const Physics::OctreeItem& item) {
			if (item.id == ignore) return;
			auto it = m_records.find(item.id);
			if (it == m_records.end()) return;
			const Record& r = it->second;
			if ((r.layer & layerMask) == 0) return;

			Physics::RayHit h = NarrowPhase(ray, r, bestDist);
			if (h.hit && h.distance < bestDist) {
				bestDist      = h.distance;
				best.hit      = true;
				best.entity   = item.id;
				best.distance = h.distance;
				best.point    = h.point;
				best.normal   = h.normal;
			}
		});
		return best;
	}

	/// True when nothing blocks the segment `from`→`to`.
	[[nodiscard]] bool LineOfSight(const FVector3& from, const FVector3& to,
																 Uint32 layerMask = 0xFFFFFFFFu,
																 EntityId ignore = NullEntity) const {
		const FVector3 d = to - from;
		const float dist = d.Length();
		if (dist < 1e-6f) return true;
		FRay ray{from, d};
		RaycastResult r = Raycast(ray, dist - 1e-3f, layerMask, ignore);
		return !r.hit;
	}

	/// Distance to the nearest obstacle along `dir`, or `maxDist` if none.
	[[nodiscard]] float DistanceToObstacle(const FVector3& origin, const FVector3& dir,
																				 float maxDist, Uint32 layerMask = 0xFFFFFFFFu,
																				 EntityId ignore = NullEntity) const {
		RaycastResult r = Raycast(FRay{origin, dir}, maxDist, layerMask, ignore);
		return r.hit ? r.distance : maxDist;
	}

private:
	enum class Kind { Box, Sphere, Mesh };
	struct Record {
		Kind                  kind = Kind::Box;
		FAABB                 box;
		FVector3              center;
		float                 radius = 0.f;
		FMatrix4              model = FMatrix4::Identity();
		std::shared_ptr<Mesh> mesh;
		Uint32                layer = 1u;
	};

	[[nodiscard]] static Uint32 LayerOf(Context& ctx, EntityId e) {
		const CollisionLayer* l = ctx.Get<CollisionLayer>(e);
		return l ? l->layer : 1u;
	}

	[[nodiscard]] static Physics::RayHit NarrowPhase(const FRay& ray, const Record& r, float maxDist) {
		switch (r.kind) {
		case Kind::Sphere:
			return Physics::Collisions::Raycast(ray, Physics::Sphere{r.center, r.radius}, maxDist);
		case Kind::Mesh:
			return RaycastMesh(ray, r, maxDist);
		case Kind::Box:
		default:
			return Physics::Collisions::Raycast(ray, r.box, maxDist);
		}
	}

	/// Precise triangle test: cast the ray in the mesh's local space.
	[[nodiscard]] static Physics::RayHit RaycastMesh(const FRay& ray, const Record& r, float maxDist) {
		const FMatrix4 inv = r.model.Inverse();
		const FRay local{inv.TransformPoint(ray.origin), inv.TransformDir(ray.direction)};
		Physics::RayHit best;
		float bestT = std::numeric_limits<float>::infinity();
		r.mesh->ForEachTriangle([&](const FVector3& a, const FVector3& b, const FVector3& c) {
			float t, u, v;
			if (local.Intersects(a, b, c, t, u, v) && t < bestT) {
				bestT = t;
				FVector3 lp = local.At(t);
				FVector3 ln = (b - a).Cross(c - a);
				best.hit    = true;
				best.point  = r.model.TransformPoint(lp);
				best.normal = r.model.TransformDir(ln).Normalize();
				if (best.normal.Dot(ray.direction) > 0.f) best.normal = -best.normal;
			}
		});
		if (best.hit) {
			best.distance = (best.point - ray.origin).Length();
			if (best.distance > maxDist) best.hit = false;
		}
		return best;
	}

	std::unordered_map<EntityId, Record> m_records;
	Physics::Octree                      m_tree;
};

/// One-shot helpers that build a temporary `RaycastWorld` (convenience).
struct RaycastSystem {
	[[nodiscard]] static RaycastResult Raycast(Context& ctx, const FRay& ray,
																						 float maxDist = std::numeric_limits<float>::infinity(),
																						 Uint32 layerMask = 0xFFFFFFFFu,
																						 EntityId ignore = NullEntity) {
		return RaycastWorld(ctx).Raycast(ray, maxDist, layerMask, ignore);
	}

	[[nodiscard]] static bool LineOfSight(Context& ctx, const FVector3& from, const FVector3& to,
																				Uint32 layerMask = 0xFFFFFFFFu, EntityId ignore = NullEntity) {
		return RaycastWorld(ctx).LineOfSight(from, to, layerMask, ignore);
	}
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_RAYCAST_SYSTEM_H_
