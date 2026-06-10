#ifndef SDL3PP_ENGINE_COLLISIONS_H_
#define SDL3PP_ENGINE_COLLISIONS_H_

/**
 * @file Collisions.h
 * @brief Header-only collision primitives, overlap/resolution tests and
 *        raycasts for 2-D and 3-D, built on the `SDL3pp` math types.
 *
 * Everything here is free-function geometry with no ECS dependency, so it can
 * be reused by the collision and raycast *systems* (see `Systems/`) or called
 * directly. Shapes:
 *   - 2-D: `FRect` (axis-aligned), `Circle`, segments
 *   - 3-D: `FAABB`, `Sphere`, `FPlane`, triangles, `FRay`
 *
 * `Overlap()` answers yes/no, `Resolve()` returns a minimum translation vector
 * (MTV) to separate two shapes, and `Raycast()` returns a `RayHit` with the
 * distance, contact point and surface normal — handy for shooters and AI
 * line-of-sight checks.
 */

#include <limits>

#include "Math3D.h"

namespace SDL {
namespace Physics {

/**
 * @defgroup CategoryEngineCollisions Engine — Collisions
 *
 * Geometric overlap, resolution and raycast helpers.
 *
 * @{
 */

/// A 2-D circle.
struct Circle {
	FVector2 center;
	float    radius = 0.f;
};

/// A 3-D sphere.
struct Sphere {
	FVector3 center;
	float    radius = 0.f;
};

/// Result of a raycast: whether it hit and where.
struct RayHit {
	bool     hit      = false;
	float    distance = 0.f;        ///< distance along the ray to the contact
	FVector3 point;                 ///< world-space contact point
	FVector3 normal;                ///< surface normal at the contact

	explicit operator bool() const noexcept { return hit; }
};

namespace Collisions {

inline constexpr float kInfinity = std::numeric_limits<float>::infinity();

// ─────────────────────────────────────────────────────────────────────────────
// 2-D overlap & containment
// ─────────────────────────────────────────────────────────────────────────────

/// Axis-aligned rect overlap.
[[nodiscard]] inline bool Overlap(const FRect& a, const FRect& b) noexcept {
	return a.x < b.x + b.w && a.x + a.w > b.x &&
				 a.y < b.y + b.h && a.y + a.h > b.y;
}

/// Circle overlap.
[[nodiscard]] inline bool Overlap(const Circle& a, const Circle& b) noexcept {
	const float r = a.radius + b.radius;
	return a.center.DistanceSq(b.center) <= r * r;
}

/// Circle vs axis-aligned rect.
[[nodiscard]] inline bool Overlap(const Circle& c, const FRect& r) noexcept {
	const float cx = SDL::Clamp(c.center.x, r.x, r.x + r.w);
	const float cy = SDL::Clamp(c.center.y, r.y, r.y + r.h);
	const float dx = c.center.x - cx;
	const float dy = c.center.y - cy;
	return dx * dx + dy * dy <= c.radius * c.radius;
}

/// Point inside rect (inclusive).
[[nodiscard]] inline bool Contains(const FRect& r, const FVector2& p) noexcept {
	return p.x >= r.x && p.x <= r.x + r.w && p.y >= r.y && p.y <= r.y + r.h;
}

/**
 * Minimum translation vector to push rect `a` out of rect `b`.
 *
 * @param mtv [out] the smallest displacement applied to `a` that separates them.
 * @returns true if the rects currently overlap.
 */
[[nodiscard]] inline bool Resolve(const FRect& a, const FRect& b, FVector2& mtv) noexcept {
	const float dx1 = b.x + b.w - a.x;       // a pushed right
	const float dx2 = a.x + a.w - b.x;       // a pushed left
	const float dy1 = b.y + b.h - a.y;       // a pushed down
	const float dy2 = a.y + a.h - b.y;       // a pushed up
	if (dx1 <= 0 || dx2 <= 0 || dy1 <= 0 || dy2 <= 0) { mtv = {}; return false; }
	const float ox = dx1 < dx2 ? dx1 : -dx2;
	const float oy = dy1 < dy2 ? dy1 : -dy2;
	if (SDL::Abs(ox) < SDL::Abs(oy)) mtv = {ox, 0.f};
	else                             mtv = {0.f, oy};
	return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3-D overlap & containment
// ─────────────────────────────────────────────────────────────────────────────

/// Box overlap.
[[nodiscard]] inline bool Overlap(const FAABB& a, const FAABB& b) noexcept { return a.Intersects(b); }

/// Sphere overlap.
[[nodiscard]] inline bool Overlap(const Sphere& a, const Sphere& b) noexcept {
	const float r = a.radius + b.radius;
	return a.center.DistanceSq(b.center) <= r * r;
}

/// Closest point on (or in) an AABB to `p`.
[[nodiscard]] inline FVector3 ClosestPointOnAABB(const FAABB& box, const FVector3& p) noexcept {
	return {SDL::Clamp(p.x, box.Min.x, box.Max.x),
					SDL::Clamp(p.y, box.Min.y, box.Max.y),
					SDL::Clamp(p.z, box.Min.z, box.Max.z)};
}

/// Sphere vs AABB.
[[nodiscard]] inline bool Overlap(const Sphere& s, const FAABB& box) noexcept {
	const FVector3 q = ClosestPointOnAABB(box, s.center);
	return q.DistanceSq(s.center) <= s.radius * s.radius;
}

/// Point inside AABB (inclusive).
[[nodiscard]] inline bool Contains(const FAABB& box, const FVector3& p) noexcept { return box.Contains(p); }

/**
 * Minimum translation vector to push box `a` out of box `b`.
 *
 * @param mtv [out] the smallest displacement applied to `a` that separates them.
 * @returns true if the boxes currently overlap.
 */
[[nodiscard]] inline bool Resolve(const FAABB& a, const FAABB& b, FVector3& mtv) noexcept {
	if (!a.Intersects(b)) { mtv = {}; return false; }
	const float dx1 = b.Max.x - a.Min.x, dx2 = a.Max.x - b.Min.x;
	const float dy1 = b.Max.y - a.Min.y, dy2 = a.Max.y - b.Min.y;
	const float dz1 = b.Max.z - a.Min.z, dz2 = a.Max.z - b.Min.z;
	const float ox = dx1 < dx2 ? dx1 : -dx2;
	const float oy = dy1 < dy2 ? dy1 : -dy2;
	const float oz = dz1 < dz2 ? dz1 : -dz2;
	const float ax = SDL::Abs(ox), ay = SDL::Abs(oy), az = SDL::Abs(oz);
	if (ax <= ay && ax <= az)      mtv = {ox, 0.f, 0.f};
	else if (ay <= ax && ay <= az) mtv = {0.f, oy, 0.f};
	else                           mtv = {0.f, 0.f, oz};
	return true;
}

/// Minimum translation vector to separate sphere `a` from sphere `b`.
[[nodiscard]] inline bool Resolve(const Sphere& a, const Sphere& b, FVector3& mtv) noexcept {
	const FVector3 d = a.center - b.center;
	const float distSq = d.LengthSq();
	const float r = a.radius + b.radius;
	if (distSq >= r * r) { mtv = {}; return false; }
	const float dist = SDL::Sqrt(distSq);
	const FVector3 n = dist > 1e-6f ? d / dist : FVector3{0.f, 1.f, 0.f};
	mtv = n * (r - dist);
	return true;
}

/// Closest point to `p` on segment a→b.
[[nodiscard]] inline FVector3 ClosestPointOnSegment(const FVector3& a, const FVector3& b,
																										const FVector3& p) noexcept {
	const FVector3 ab = b - a;
	const float denom = ab.LengthSq();
	if (denom < 1e-12f) return a;
	const float t = SDL::Clamp((p - a).Dot(ab) / denom, 0.f, 1.f);
	return a + ab * t;
}

// ─────────────────────────────────────────────────────────────────────────────
// 3-D raycasts (return contact distance, point and normal)
// ─────────────────────────────────────────────────────────────────────────────

/// Ray vs AABB. Reports the entry face normal.
[[nodiscard]] inline RayHit Raycast(const FRay& ray, const FAABB& box,
																		float maxDist = kInfinity) noexcept {
	float tMin = 0.f, tMax = maxDist;
	int   axis = 0;
	float sign = -1.f;
	for (int i = 0; i < 3; ++i) {
		const float orig = (&ray.origin.x)[i];
		const float dir  = (&ray.direction.x)[i];
		const float bmin = (&box.Min.x)[i];
		const float bmax = (&box.Max.x)[i];
		if (SDL::Abs(dir) < 1e-8f) {
			if (orig < bmin || orig > bmax) return {};
		} else {
			float t1 = (bmin - orig) / dir;
			float t2 = (bmax - orig) / dir;
			float s  = -1.f;
			if (t1 > t2) { std::swap(t1, t2); s = 1.f; }
			if (t1 > tMin) { tMin = t1; axis = i; sign = s; }
			tMax = SDL::Min(tMax, t2);
			if (tMin > tMax) return {};
		}
	}
	if (tMin > maxDist) return {};
	RayHit h;
	h.hit = true;
	h.distance = tMin;
	h.point = ray.At(tMin);
	h.normal = {};
	(&h.normal.x)[axis] = sign;
	return h;
}

/// Ray vs sphere. Reports the outward surface normal at the entry point.
[[nodiscard]] inline RayHit Raycast(const FRay& ray, const Sphere& s,
																		float maxDist = kInfinity) noexcept {
	const FVector3 oc = ray.origin - s.center;
	const float b = oc.Dot(ray.direction);
	const float c = oc.LengthSq() - s.radius * s.radius;
	const float disc = b * b - c;
	if (disc < 0.f) return {};
	const float sq = SDL::Sqrt(disc);
	float t = -b - sq;
	if (t < 0.f) t = -b + sq;     // origin inside the sphere
	if (t < 0.f || t > maxDist) return {};
	RayHit h;
	h.hit = true;
	h.distance = t;
	h.point = ray.At(t);
	h.normal = (h.point - s.center).Normalize();
	return h;
}

/// Ray vs plane. Reports the plane normal oriented toward the ray.
[[nodiscard]] inline RayHit Raycast(const FRay& ray, const FPlane& plane,
																		float maxDist = kInfinity) noexcept {
	float t;
	if (!ray.Intersects(plane, t) || t > maxDist) return {};
	RayHit h;
	h.hit = true;
	h.distance = t;
	h.point = ray.At(t);
	h.normal = plane.normal.Dot(ray.direction) > 0.f ? -plane.normal : plane.normal;
	return h;
}

/// Ray vs triangle (Möller–Trumbore). Set `cullBackface` to ignore back faces.
[[nodiscard]] inline RayHit RaycastTriangle(const FRay& ray,
																						const FVector3& v0, const FVector3& v1,
																						const FVector3& v2,
																						float maxDist = kInfinity,
																						bool cullBackface = false) noexcept {
	float t, u, v;
	if (!ray.Intersects(v0, v1, v2, t, u, v) || t > maxDist) return {};
	FVector3 n = (v1 - v0).Cross(v2 - v0).Normalize();
	if (n.Dot(ray.direction) > 0.f) {
		if (cullBackface) return {};
		n = -n;
	}
	RayHit h;
	h.hit = true;
	h.distance = t;
	h.point = ray.At(t);
	h.normal = n;
	return h;
}

} // namespace Collisions

/** @} */ // CategoryEngineCollisions

} // namespace Physics
} // namespace SDL

#endif // SDL3PP_ENGINE_COLLISIONS_H_
