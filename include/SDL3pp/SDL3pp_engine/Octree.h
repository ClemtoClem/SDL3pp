#ifndef SDL3PP_ENGINE_OCTREE_H_
#define SDL3PP_ENGINE_OCTREE_H_

/**
 * @file Octree.h
 * @brief 3-D octree for broad-phase collision and spatial queries.
 *
 * The 3-D analogue of `Quadtree`: it partitions an `FAABB` into eight octants.
 * Items that straddle an octant boundary stay in the parent node. Rebuild it
 * each frame for dynamic scenes, or once for static geometry.
 *
 * ```cpp
 * SDL::Physics::Octree tree(worldBounds);
 * for (auto& [id, box] : objects) tree.Insert({id, box});
 * tree.Query(searchBox, [&](const SDL::Physics::OctreeItem& it) { ... });
 * ```
 */

#include <memory>
#include <vector>

#include "ECS.h"
#include "Math3D.h"

namespace SDL {
namespace Physics {

/// An entity paired with its world-space 3-D bounds.
struct OctreeItem {
	ECS::EntityId id;
	FAABB         bounds;
};

/// Region octree storing `OctreeItem`s.
class Octree {
public:
	/**
	 * @param bounds     the region covered by the whole tree.
	 * @param maxObjects split a node once it exceeds this many items.
	 * @param maxLevels  maximum subdivision depth.
	 */
	explicit Octree(const FAABB& bounds, int maxObjects = 8, int maxLevels = 6)
		: m_bounds(bounds), m_maxObjects(maxObjects), m_maxLevels(maxLevels) {}

	/// Remove all items and collapse the tree.
	void Clear() {
		m_items.clear();
		for (auto& n : m_nodes) n.reset();
	}

	/// Rebuild the tree from a fresh set of items.
	void Build(const std::vector<OctreeItem>& items) {
		Clear();
		for (const auto& it : items) Insert(it);
	}

	/// Insert an item, descending into the octant that fully contains it.
	void Insert(const OctreeItem& item) {
		if (!m_bounds.Intersects(item.bounds)) return;

		if (m_nodes[0]) {
			int index = GetIndex(item.bounds);
			if (index != -1) { m_nodes[index]->Insert(item); return; }
		}

		m_items.push_back(item);

		if (static_cast<int>(m_items.size()) > m_maxObjects && m_level < m_maxLevels) {
			if (!m_nodes[0]) Split();
			size_t i = 0;
			while (i < m_items.size()) {
				int index = GetIndex(m_items[i].bounds);
				if (index != -1) {
					m_nodes[index]->Insert(m_items[i]);
					m_items.erase(m_items.begin() + static_cast<long>(i));
				} else {
					++i;
				}
			}
		}
	}

	/// Append every item whose node overlaps `area` to `out`.
	void Retrieve(std::vector<OctreeItem>& out, const FAABB& area) const {
		if (!m_bounds.Intersects(area)) return;
		if (m_nodes[0])
			for (const auto& n : m_nodes)
				if (n->m_bounds.Intersects(area)) n->Retrieve(out, area);
		for (const auto& it : m_items)
			if (it.bounds.Intersects(area)) out.push_back(it);
	}

	/// Invoke `fn(const OctreeItem&)` for every item overlapping `area`.
	template<class Fn>
	void Query(const FAABB& area, Fn&& fn) const {
		if (!m_bounds.Intersects(area)) return;
		if (m_nodes[0])
			for (const auto& n : m_nodes)
				if (n->m_bounds.Intersects(area)) n->Query(area, fn);
		for (const auto& it : m_items)
			if (it.bounds.Intersects(area)) fn(it);
	}

	/**
	 * Invoke `fn(const OctreeItem&)` for every item whose box the ray may hit.
	 *
	 * Broad-phase only: descends into octants the ray enters. Run a precise
	 * ray test on each reported item in the callback.
	 */
	template<class Fn>
	void QueryRay(const FRay& ray, Fn&& fn) const {
		float tMin, tMax;
		if (!ray.Intersects(m_bounds, tMin, tMax)) return;
		for (const auto& it : m_items) {
			float a, b;
			if (ray.Intersects(it.bounds, a, b)) fn(it);
		}
		if (m_nodes[0]) for (const auto& n : m_nodes) n->QueryRay(ray, fn);
	}

	/// Total number of items stored in this node and all descendants.
	[[nodiscard]] size_t Size() const {
		size_t n = m_items.size();
		if (m_nodes[0]) for (const auto& c : m_nodes) n += c->Size();
		return n;
	}

	[[nodiscard]] const FAABB& Bounds() const noexcept { return m_bounds; }

private:
	FAABB                   m_bounds;
	int                     m_maxObjects;
	int                     m_maxLevels;
	int                     m_level = 0;
	std::vector<OctreeItem> m_items;
	std::unique_ptr<Octree> m_nodes[8];

	Octree(const FAABB& bounds, int maxObjects, int maxLevels, int level)
		: m_bounds(bounds), m_maxObjects(maxObjects), m_maxLevels(maxLevels), m_level(level) {}

	void Split() {
		const FVector3 c   = m_bounds.Center();
		const FVector3 mn  = m_bounds.Min;
		const FVector3 mx  = m_bounds.Max;
		auto make = [&](const FVector3& a, const FVector3& b) {
			return std::unique_ptr<Octree>(new Octree(FAABB(a, b), m_maxObjects, m_maxLevels, m_level + 1));
		};
		m_nodes[0] = make({mn.x, mn.y, mn.z}, {c.x, c.y, c.z});
		m_nodes[1] = make({c.x, mn.y, mn.z}, {mx.x, c.y, c.z});
		m_nodes[2] = make({mn.x, c.y, mn.z}, {c.x, mx.y, c.z});
		m_nodes[3] = make({c.x, c.y, mn.z}, {mx.x, mx.y, c.z});
		m_nodes[4] = make({mn.x, mn.y, c.z}, {c.x, c.y, mx.z});
		m_nodes[5] = make({c.x, mn.y, c.z}, {mx.x, c.y, mx.z});
		m_nodes[6] = make({mn.x, c.y, c.z}, {c.x, mx.y, mx.z});
		m_nodes[7] = make({c.x, c.y, c.z}, {mx.x, mx.y, mx.z});
	}

	/// Index of the child fully containing `box`, or -1 if it straddles.
	[[nodiscard]] int GetIndex(const FAABB& box) const {
		for (int i = 0; i < 8; ++i)
			if (m_nodes[i]->m_bounds.Contains(box)) return i;
		return -1;
	}
};

} // namespace Physics
} // namespace SDL

#endif // SDL3PP_ENGINE_OCTREE_H_
