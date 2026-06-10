#ifndef SDL3PP_ENGINE_QUADTREE_H_
#define SDL3PP_ENGINE_QUADTREE_H_

/**
 * @file Quadtree.h
 * @brief 2-D loose quadtree for broad-phase collision and spatial queries.
 *
 * A quadtree recursively partitions a rectangular region into four quadrants,
 * pushing items down until a node holds few enough of them. Broad-phase
 * collision then only compares items that share a node instead of every pair.
 *
 * Typical use (rebuild each frame for dynamic scenes):
 * ```cpp
 * SDL::Physics::Quadtree tree({0, 0, worldW, worldH});
 * for (auto& [id, rect] : actors) tree.Insert({id, rect});
 * tree.Query(playerRect, [&](const SDL::Physics::QuadtreeItem& it) {
 *     // narrow-phase against `it`
 * });
 * ```
 */

#include <memory>
#include <vector>

#include "../SDL3pp_rect.h"
#include "ECS.h"

namespace SDL {
namespace Physics {

/// An entity paired with its world-space 2-D bounds.
struct QuadtreeItem {
	ECS::EntityId id;
	FRect         bounds;
};

/**
 * Region quadtree storing `QuadtreeItem`s.
 *
 * Items that straddle a quadrant boundary stay in the parent node, so a query
 * walks the parent plus any overlapping children.
 */
class Quadtree {
public:
	/**
	 * @param bounds     the region covered by the whole tree.
	 * @param maxObjects split a node once it exceeds this many items.
	 * @param maxLevels  maximum subdivision depth.
	 */
	explicit Quadtree(const FRect& bounds, int maxObjects = 8, int maxLevels = 6)
		: m_bounds(bounds), m_maxObjects(maxObjects), m_maxLevels(maxLevels) {}

	/// Remove all items and collapse the tree.
	void Clear() {
		m_items.clear();
		for (auto& n : m_nodes) n.reset();
	}

	/// Rebuild the tree from a fresh set of items.
	void Build(const std::vector<QuadtreeItem>& items) {
		Clear();
		for (const auto& it : items) Insert(it);
	}

	/// Insert an item, descending into the quadrant that fully contains it.
	void Insert(const QuadtreeItem& item) {
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
	void Retrieve(std::vector<QuadtreeItem>& out, const FRect& area) const {
		if (!Intersects(m_bounds, area)) return;
		if (m_nodes[0]) {
			for (const auto& n : m_nodes)
				if (Intersects(n->m_bounds, area)) n->Retrieve(out, area);
		}
		for (const auto& it : m_items)
			if (Intersects(it.bounds, area)) out.push_back(it);
	}

	/// Invoke `fn(const QuadtreeItem&)` for every item overlapping `area`.
	template<class Fn>
	void Query(const FRect& area, Fn&& fn) const {
		if (!Intersects(m_bounds, area)) return;
		if (m_nodes[0])
			for (const auto& n : m_nodes)
				if (Intersects(n->m_bounds, area)) n->Query(area, fn);
		for (const auto& it : m_items)
			if (Intersects(it.bounds, area)) fn(it);
	}

	/// Total number of items stored in this node and all descendants.
	[[nodiscard]] size_t Size() const {
		size_t n = m_items.size();
		if (m_nodes[0]) for (const auto& c : m_nodes) n += c->Size();
		return n;
	}

	[[nodiscard]] const FRect& Bounds() const noexcept { return m_bounds; }

	/// Fast AABB overlap test for two 2-D rects.
	[[nodiscard]] static bool Intersects(const FRect& a, const FRect& b) noexcept {
		return a.x < b.x + b.w && a.x + a.w > b.x &&
					 a.y < b.y + b.h && a.y + a.h > b.y;
	}

private:
	FRect                     m_bounds;
	int                       m_maxObjects;
	int                       m_maxLevels;
	int                       m_level = 0;
	std::vector<QuadtreeItem> m_items;
	std::unique_ptr<Quadtree> m_nodes[4];

	Quadtree(const FRect& bounds, int maxObjects, int maxLevels, int level)
		: m_bounds(bounds), m_maxObjects(maxObjects), m_maxLevels(maxLevels), m_level(level) {}

	void Split() {
		const float w = m_bounds.w * 0.5f;
		const float h = m_bounds.h * 0.5f;
		const float x = m_bounds.x;
		const float y = m_bounds.y;
		auto make = [&](const FRect& r) {
			return std::unique_ptr<Quadtree>(new Quadtree(r, m_maxObjects, m_maxLevels, m_level + 1));
		};
		m_nodes[0] = make({x + w, y,     w, h}); // top-right
		m_nodes[1] = make({x,     y,     w, h}); // top-left
		m_nodes[2] = make({x,     y + h, w, h}); // bottom-left
		m_nodes[3] = make({x + w, y + h, w, h}); // bottom-right
	}

	/// Index of the child fully containing `rect`, or -1 if it straddles.
	[[nodiscard]] int GetIndex(const FRect& rect) const {
		const float vMid = m_bounds.x + m_bounds.w * 0.5f;
		const float hMid = m_bounds.y + m_bounds.h * 0.5f;
		const bool top    = rect.y + rect.h < hMid;
		const bool bottom = rect.y > hMid;
		const bool left   = rect.x + rect.w < vMid;
		const bool right  = rect.x > vMid;
		if (left) {
			if (top)    return 1;
			if (bottom) return 2;
		} else if (right) {
			if (top)    return 0;
			if (bottom) return 3;
		}
		return -1;
	}
};

} // namespace Physics
} // namespace SDL

#endif // SDL3PP_ENGINE_QUADTREE_H_
