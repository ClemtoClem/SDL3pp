#ifndef SDL3PP_ENGINE_COLLISION_SYSTEM_H_
#define SDL3PP_ENGINE_COLLISION_SYSTEM_H_

/**
 * @file Systems/CollisionSystem.h
 * @brief Broad-phase + narrow-phase collision detection over the ECS.
 *
 * Each frame the system reads colliders and world transforms, inserts them into
 * a spatial tree (quadtree in 2-D, octree in 3-D) for cheap broad-phase, then
 * runs precise overlap tests on the candidate pairs. Pairs are reported with a
 * minimum-translation vector (MTV) so callers can resolve penetration.
 *
 * Layer filtering uses the optional `CollisionLayer` component; a missing
 * component means "collides with everything".
 *
 * ```cpp
 * for (auto& c : SDL::ECS::CollisionSystem2D::Detect(ctx)) {
 *     if (c.trigger) HandleTrigger(c.a, c.b);
 *     else           Separate(ctx, c);   // push apart using c.mtv
 * }
 * ```
 */

#include <vector>

#include "../Collisions.h"
#include "../Components.h"
#include "../ECS.h"
#include "../Octree.h"
#include "../Quadtree.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/// An overlapping pair of 2-D colliders.
struct CollisionPair2D {
	EntityId a = NullEntity;
	EntityId b = NullEntity;
	FVector2 mtv;          ///< push `a` by this to separate it from `b`
	bool     trigger = false;
};

/// An overlapping pair of 3-D colliders.
struct CollisionPair3D {
	EntityId a = NullEntity;
	EntityId b = NullEntity;
	FVector3 mtv;          ///< push `a` by this to separate it from `b`
	bool     trigger = false;
};

namespace detail {

[[nodiscard]] inline bool LayersCollide(Context& ctx, EntityId a, EntityId b) {
	const CollisionLayer* la = ctx.Get<CollisionLayer>(a);
	const CollisionLayer* lb = ctx.Get<CollisionLayer>(b);
	if (!la || !lb) return true;
	return la->CollidesWith(*lb);
}

} // namespace detail

/// 2-D collision detection using a quadtree broad-phase.
struct CollisionSystem2D {
	/// Detect all overlapping `Collider2D` pairs within `worldBounds`.
	static std::vector<CollisionPair2D> Detect(Context& ctx, const FRect& worldBounds) {
		struct Entry { EntityId id; FRect rect; const Collider2D* col; };
		std::vector<Entry> entries;
		Physics::Quadtree tree(worldBounds);

		ctx.Each<Collider2D, GlobalTransform2D>([&](EntityId e, Collider2D& c, GlobalTransform2D& gt) {
			const FRect r = c.WorldRect(gt.position);
			entries.push_back({e, r, &c});
			tree.Insert({e, r});
		});

		std::vector<CollisionPair2D> pairs;
		std::vector<Physics::QuadtreeItem> candidates;
		for (const Entry& it : entries) {
			candidates.clear();
			tree.Retrieve(candidates, it.rect);
			for (const auto& cand : candidates) {
				if (cand.id <= it.id) continue;                 // dedup + skip self
				if (!detail::LayersCollide(ctx, it.id, cand.id)) continue;
				const Collider2D* other = ctx.Get<Collider2D>(cand.id);
				if (!other) continue;
				FVector2 mtv;
				if (Physics::Collisions::Resolve(it.rect, cand.bounds, mtv))
					pairs.push_back({it.id, cand.id, mtv, it.col->isTrigger || other->isTrigger});
			}
		}
		return pairs;
	}

	/// Detect using bounds auto-computed from the colliders.
	static std::vector<CollisionPair2D> Detect(Context& ctx) {
		return Detect(ctx, AutoBounds(ctx));
	}

	/// Push `pair.a` (and `pair.b`, when both dynamic) apart along the MTV.
	static void Separate(Context& ctx, const CollisionPair2D& pair) {
		if (pair.trigger) return;
		Collider2D* ca = ctx.Get<Collider2D>(pair.a);
		Collider2D* cb = ctx.Get<Collider2D>(pair.b);
		Transform2D* ta = ctx.Get<Transform2D>(pair.a);
		Transform2D* tb = ctx.Get<Transform2D>(pair.b);
		const bool aMov = ta && (!ca || !ca->isStatic);
		const bool bMov = tb && (!cb || !cb->isStatic);
		if (aMov && bMov) {
			ta->position.x += pair.mtv.x * 0.5f; ta->position.y += pair.mtv.y * 0.5f;
			tb->position.x -= pair.mtv.x * 0.5f; tb->position.y -= pair.mtv.y * 0.5f;
		} else if (aMov) {
			ta->position.x += pair.mtv.x; ta->position.y += pair.mtv.y;
		} else if (bMov) {
			tb->position.x -= pair.mtv.x; tb->position.y -= pair.mtv.y;
		}
	}

	/// World rect enclosing all colliders (with margin), for the broad phase.
	static FRect AutoBounds(Context& ctx) {
		bool any = false;
		float minX = 0, minY = 0, maxX = 0, maxY = 0;
		ctx.Each<Collider2D, GlobalTransform2D>([&](EntityId, Collider2D& c, GlobalTransform2D& gt) {
			const FRect r = c.WorldRect(gt.position);
			if (!any) { minX = r.x; minY = r.y; maxX = r.x + r.w; maxY = r.y + r.h; any = true; }
			else {
				minX = SDL::Min(minX, r.x);          minY = SDL::Min(minY, r.y);
				maxX = SDL::Max(maxX, r.x + r.w);    maxY = SDL::Max(maxY, r.y + r.h);
			}
		});
		if (!any) return {0, 0, 1, 1};
		const float m = 1.f;
		return {minX - m, minY - m, (maxX - minX) + 2 * m, (maxY - minY) + 2 * m};
	}
};

/// 3-D collision detection using an octree broad-phase.
struct CollisionSystem3D {
	/// Detect all overlapping `Collider3D` pairs within `worldBounds`.
	static std::vector<CollisionPair3D> Detect(Context& ctx, const FAABB& worldBounds) {
		struct Entry { EntityId id; FAABB box; const Collider3D* col; FVector3 pos; };
		std::vector<Entry> entries;
		Physics::Octree tree(worldBounds);

		ctx.Each<Collider3D, GlobalTransform3D>([&](EntityId e, Collider3D& c, GlobalTransform3D& gt) {
			const FVector3 pos = gt.Position();
			const FAABB box = c.WorldAABB(pos);
			entries.push_back({e, box, &c, pos});
			tree.Insert({e, box});
		});

		std::vector<CollisionPair3D> pairs;
		std::vector<Physics::OctreeItem> candidates;
		for (const Entry& it : entries) {
			candidates.clear();
			tree.Retrieve(candidates, it.box);
			for (const auto& cand : candidates) {
				if (cand.id <= it.id) continue;
				if (!detail::LayersCollide(ctx, it.id, cand.id)) continue;
				const Collider3D* other = ctx.Get<Collider3D>(cand.id);
				if (!other) continue;
				const GlobalTransform3D* og = ctx.Get<GlobalTransform3D>(cand.id);
				const FVector3 otherPos = og ? og->Position() : FVector3{};

				FVector3 mtv;
				bool hit = false;
				if (it.col->sphereRadius > 0.f && other->sphereRadius > 0.f) {
					Physics::Sphere sa{it.pos, it.col->sphereRadius};
					Physics::Sphere sb{otherPos, other->sphereRadius};
					hit = Physics::Collisions::Resolve(sa, sb, mtv);
				} else {
					hit = Physics::Collisions::Resolve(it.box, cand.bounds, mtv);
				}
				if (hit)
					pairs.push_back({it.id, cand.id, mtv, it.col->isTrigger || other->isTrigger});
			}
		}
		return pairs;
	}

	/// Detect using bounds auto-computed from the colliders.
	static std::vector<CollisionPair3D> Detect(Context& ctx) {
		return Detect(ctx, AutoBounds(ctx));
	}

	/// Push `pair.a` (and `pair.b`, when both dynamic) apart along the MTV.
	static void Separate(Context& ctx, const CollisionPair3D& pair) {
		if (pair.trigger) return;
		Collider3D* ca = ctx.Get<Collider3D>(pair.a);
		Collider3D* cb = ctx.Get<Collider3D>(pair.b);
		Transform3D* ta = ctx.Get<Transform3D>(pair.a);
		Transform3D* tb = ctx.Get<Transform3D>(pair.b);
		const bool aMov = ta && (!ca || !ca->isStatic);
		const bool bMov = tb && (!cb || !cb->isStatic);
		if (aMov && bMov) { ta->position += pair.mtv * 0.5f; tb->position -= pair.mtv * 0.5f; }
		else if (aMov)    { ta->position += pair.mtv; }
		else if (bMov)    { tb->position -= pair.mtv; }
	}

	/// World box enclosing all colliders (with margin), for the broad phase.
	static FAABB AutoBounds(Context& ctx) {
		FAABB bounds;
		ctx.Each<Collider3D, GlobalTransform3D>([&](EntityId, Collider3D& c, GlobalTransform3D& gt) {
			bounds.Expand(c.WorldAABB(gt.Position()));
		});
		if (!bounds.IsValid()) return FAABB({-1, -1, -1}, {1, 1, 1});
		const FVector3 m{1.f, 1.f, 1.f};
		return {bounds.Min - m, bounds.Max + m};
	}
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_COLLISION_SYSTEM_H_
