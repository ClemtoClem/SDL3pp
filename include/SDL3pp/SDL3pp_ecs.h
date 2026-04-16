#ifndef SDL3PP_ECS_H_
#define SDL3PP_ECS_H_

/**
 * @file SDL3pp_ecs.h
 * @brief Entity Component System (ECS) core for SDL3pp.
 *
 * ## Design
 *
 * This ECS follows the **sparse-set** architecture described in the thesis
 * "A Graph-Based Approach To Concurrent ECS Design" (Choparinov, 2024).
 * Each component type owns a dense array + an entity→index map, giving:
 *
 * - O(1) add / get / remove per component
 * - O(n) iteration with perfect cache locality (dense array)
 * - Archetype-aware multi-component queries
 *
 * ## Usage
 *
 * ```cpp
 * SDL::World World;
 *
 * // Define plain-data components
 * struct Position { float x, y; };
 * struct Velocity { float vx, vy; };
 *
 * // Create entities
 * auto e1 = World.CreateEntity();
 * World.Add<Position>(e1, {100.f, 200.f});
 * World.Add<Velocity>(e1, {1.f, -1.f});
 *
 * // Query – iterate all entities with both Position AND Velocity
 * World.Each<Position, Velocity>([](SDL::EntityId e,
 *                                    Position& pos,
 *                                    Velocity& vel) {
 *     pos.x += vel.vx;
 *     pos.y += vel.vy;
 * });
 *
 * // Register and run systems
 * World.AddSystem([](SDL::World& w) {
 *     w.Each<Position, Velocity>([](SDL::EntityId, Position& p, Velocity& v) {
 *         p.x += v.vx; p.y += v.vy;
 *     });
 * });
 * World.RunSystems();
 *
 * // Destroy an entity (removes ALL its components automatically)
 * World.DestroyEntity(e1);
 * ```
 *
 * ## C++20 RAII
 *
 * `EntityRef` is a move-only RAII handle that auto-destroys the entity when
 * it goes out of scope:
 *
 * ```cpp
 * {
 *     auto ref = World.Spawn();  // returns EntityRef
 *     ref.Add<Position>({0, 0});
 * }  // entity destroyed here
 * ```
 */

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace SDL {

namespace ECS {

// ─────────────────────────────────────────────────────────────────────────────
// Entity identity
// ─────────────────────────────────────────────────────────────────────────────

/// Opaque entity identifier.  0 is the null/invalid entity.
using EntityId = uint64_t;

/// The null entity.  No entity ever receives this ID.
inline constexpr EntityId NullEntity = 0;

// ─────────────────────────────────────────────────────────────────────────────
// Component concept
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Constraint on types that may be used as ECS components.
 * References and raw pointers are explicitly disallowed.
 */
template<class T>
concept Component = !std::is_reference_v<T> &&
											 !std::is_pointer_v<T>  &&
											 !std::is_void_v<T>;

// ─────────────────────────────────────────────────────────────────────────────
// IComponentStorage  –  type-erased base
// ─────────────────────────────────────────────────────────────────────────────

class IComponentStorage {
public:
	virtual ~IComponentStorage()             = default;
	virtual bool Has(EntityId e)    const    = 0;
	virtual void Remove(EntityId e)          = 0;
	virtual void Clear()                     = 0;
	virtual size_t Size()           const    = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// ComponentStorage<T>  –  dense array + sparse index map (sparse-set style)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Stores all instances of component type T in a contiguous array.
 *
 * Invariant: m_data[i] belongs to entity m_entityOf[i].
 *
 * Removal uses the "swap-with-last" trick for O(1) amortised cost.
 */
template<Component T>
class ComponentStorage final : public IComponentStorage {
public:
	// ── Mutation ──────────────────────────────────────────────────────────────

	/**
	 * Add component T to entity `e`, replacing any existing value.
	 * @returns Reference to the stored component.
	 */
	T& Add(EntityId e, T value = {}) {
		auto [it, inserted] = m_indices.emplace(e, m_data.size());
		if (inserted) {
			m_data.push_back(std::move(value));
			m_entityOf.push_back(e);
		} else {
			m_data[it->second] = std::move(value);
		}
		return m_data[it->second];
	}

	/**
	 * Remove component T from entity `e`.
	 * No-op if the entity does not have this component.
	 */
	void Remove(EntityId e) override {
		auto it = m_indices.find(e);
		if (it == m_indices.end()) return;

		const size_t idx  = it->second;
		const size_t last = m_data.size() - 1;

		// Swap with the last element to avoid a gap.
		if (idx != last) {
			m_data[idx]     = std::move(m_data[last]);
			m_entityOf[idx] = m_entityOf[last];
			m_indices[m_entityOf[idx]] = idx;
		}
		m_data.pop_back();
		m_entityOf.pop_back();
		m_indices.erase(it);
	}

	void Clear() override {
		m_data.clear();
		m_entityOf.clear();
		m_indices.clear();
	}

	// ── Query ──────────────────────────────────────────────────────────────────

	/// Returns pointer to the component or nullptr if absent.
	[[nodiscard]] T* Get(EntityId e) noexcept {
		auto it = m_indices.find(e);
		return it != m_indices.end() ? &m_data[it->second] : nullptr;
	}

	/// Const overload.
	[[nodiscard]] const T* Get(EntityId e) const noexcept {
		auto it = m_indices.find(e);
		return it != m_indices.end() ? &m_data[it->second] : nullptr;
	}

	[[nodiscard]] bool Has(EntityId e) const override {
		return m_indices.count(e) > 0;
	}

	[[nodiscard]] size_t Size() const override { return m_data.size(); }

	// ── Bulk access ────────────────────────────────────────────────────────────

	/// Dense view of all component values (cache-friendly iteration).
	[[nodiscard]] std::span<T>       View()       noexcept { return m_data; }
	[[nodiscard]] std::span<const T> View() const noexcept { return m_data; }

	/// Parallel entity list: `Entities()[i]` owns `View()[i]`.
	[[nodiscard]] const std::vector<EntityId>& Entities() const noexcept {
		return m_entityOf;
	}

private:
	std::vector<T>                          m_data;
	std::vector<EntityId>                   m_entityOf;
	std::unordered_map<EntityId, size_t>    m_indices;
};

// ─────────────────────────────────────────────────────────────────────────────
// Forward declaration of World (needed by EntityRef)
// ─────────────────────────────────────────────────────────────────────────────

class World;

// ─────────────────────────────────────────────────────────────────────────────
// EntityRef  –  RAII handle to a live entity
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Move-only, RAII handle returned by `World::Spawn()`.
 *
 * The entity is destroyed automatically when the handle goes out of scope
 * (unless `Release()` is called first to transfer ownership back to the World).
 *
 * Conveniently wraps the common component operations so callers do not need
 * to pass the entity ID explicitly:
 *
 * ```cpp
 * auto crate = World.Spawn();
 * crate.Add<Position>({320.f, 240.f})
 *      .Add<Velocity>({0.f, 0.f})
 *      .Add<ECS::Sprite>({texture});
 * ```
 */
class EntityRef {
public:
	EntityRef() = default;
	EntityRef(World& World, EntityId id) : m_World(&World), m_id(id) {}

	~EntityRef() { Destroy(); }

	EntityRef(const EntityRef&)            = delete;
	EntityRef& operator=(const EntityRef&) = delete;

	EntityRef(EntityRef&& other) noexcept
		: m_World(other.m_World), m_id(other.m_id) {
		other.m_World = nullptr;
		other.m_id    = NullEntity;
	}

	EntityRef& operator=(EntityRef&& other) noexcept {
		if (this != &other) {
			Destroy();
			m_World       = other.m_World;
			m_id          = other.m_id;
			other.m_World = nullptr;
			other.m_id    = NullEntity;
		}
		return *this;
	}

	// ── Entity identity ────────────────────────────────────────────────────────

	[[nodiscard]] EntityId Id()      const noexcept { return m_id; }
	[[nodiscard]] bool     IsValid() const noexcept { return m_World && m_id != NullEntity; }
	explicit operator bool()         const noexcept { return IsValid(); }

	// ── Component helpers (defined after World) ────────────────────────────────

	template<Component T> EntityRef& Add(T value = {});
	template<Component T> T*         Get();
	template<Component T> bool        Has() const;
	template<Component T> EntityRef& Remove();

	void Destroy();

	/// Give up ownership – entity survives past this handle's lifetime.
	EntityId Release() noexcept {
		EntityId id = m_id;
		m_World = nullptr;
		m_id    = NullEntity;
		return id;
	}

private:
	World*   m_World = nullptr;
	EntityId m_id    = NullEntity;
};

// ─────────────────────────────────────────────────────────────────────────────
// World
// ─────────────────────────────────────────────────────────────────────────────

/**
 * The ECS universe.  Owns all entities, component storages, and systems.
 *
 * Thread safety: **none**.  All mutations must occur from the same thread.
 * (Use SDL3pp_resources.h for cross-thread asset loading.)
 */
class World {
public:
	World()           = default;
	~World()          = default;

	World(const World&)            = delete;
	World& operator=(const World&) = delete;
	World(World&&)                 = default;
	World& operator=(World&&)      = default;

	// ─────────────────────────────────────────────────────────────────────────
	// Entity lifecycle
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Create a new entity and return its raw ID.
	 * Prefer `Spawn()` for automatic RAII management.
	 */
	[[nodiscard]] EntityId CreateEntity() {
		EntityId id = ++m_nextId;
		m_alive.insert(id);
		return id;
	}

	/**
	 * Create a new entity wrapped in an RAII `EntityRef`.
	 * The entity is destroyed when the handle goes out of scope.
	 */
	[[nodiscard]] EntityRef Spawn() {
		return EntityRef{*this, CreateEntity()};
	}

	/**
	 * Destroy an entity and remove **all** its components.
	 * All `EntityRef` handles pointing to this entity become invalid.
	 */
	void DestroyEntity(EntityId e) {
		if (!m_alive.count(e)) return;
		for (auto& [type, storage] : m_storages) {
			if (storage->Has(e)) storage->Remove(e);
		}
		m_alive.erase(e);
	}

	[[nodiscard]] bool    IsAlive(EntityId e) const noexcept { return m_alive.count(e) > 0; }
	[[nodiscard]] size_t  EntityCount()       const noexcept { return m_alive.size(); }

	// ─────────────────────────────────────────────────────────────────────────
	// Component operations
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Add (or replace) component T on entity `e`.
	 * @returns Reference to the stored component.
	 */
	template<Component T>
	T& Add(EntityId e, T value = {}) {
		assert(IsAlive(e) && "Add<T>: entity is not alive");
		return GetStorage<T>().Add(e, std::move(value));
	}

	/**
	 * Get a mutable pointer to component T, or nullptr if absent.
	 */
	template<Component T>
	[[nodiscard]] T* Get(EntityId e) noexcept {
		auto* s = TryGetStorage<T>();
		return s ? s->Get(e) : nullptr;
	}

	/// Const overload.
	template<Component T>
	[[nodiscard]] const T* Get(EntityId e) const noexcept {
		auto it = m_storages.find(typeid(T));
		if (it == m_storages.end()) return nullptr;
		return static_cast<const ComponentStorage<T>*>(it->second.get())->Get(e);
	}

	/**
	 * Returns true if entity `e` has component T.
	 */
	template<Component T>
	[[nodiscard]] bool Has(EntityId e) const noexcept {
		auto it = m_storages.find(typeid(T));
		return it != m_storages.end() && it->second->Has(e);
	}

	/**
	 * Remove component T from entity `e` (no-op if absent).
	 */
	template<Component T>
	void Remove(EntityId e) {
		if (auto* s = TryGetStorage<T>()) s->Remove(e);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Queries
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Iterate every entity that has component T.
	 *
	 * ```cpp
	 * World.Each<Position>([](EntityId e, Position& p) { ... });
	 * ```
	 */
	template<Component T, class Fn>
	void Each(Fn&& fn) {
		auto* s = TryGetStorage<T>();
		if (!s) return;
		auto entities = s->Entities(); // snapshot (fn may modify World)
		for (EntityId e : entities) {
			if (!IsAlive(e)) continue;
			fn(e, *s->Get(e));
		}
	}

	/**
	 * Iterate every entity that has ALL listed component types.
	 *
	 * ```cpp
	 * World.Each<Position, Velocity>([](EntityId e, Position& p, Velocity& v) { ... });
	 * ```
	 *
	 * Iteration order follows the dense array of the first component type.
	 */
	template<Component First, Component Second, Component... Rest, class Fn>
	void Each(Fn&& fn) {
		auto* s = TryGetStorage<First>();
		if (!s) return;
		auto entities = s->Entities();
		for (EntityId e : entities) {
			if (!IsAlive(e)) continue;
			if (!Has<Second>(e)) continue;
			if (!(Has<Rest>(e) && ...)) continue;
			fn(e, *s->Get(e), *Get<Second>(e), *Get<Rest>(e)...);
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Raw storage access
	// ─────────────────────────────────────────────────────────────────────────

	/**
	 * Direct access to the dense storage for component T.
	 * Prefer `Each<T>()` for normal iteration.
	 */
	template<Component T>
	[[nodiscard]] ComponentStorage<T>& Storage() {
		return GetStorage<T>();
	}

	template<Component T>
	[[nodiscard]] const ComponentStorage<T>* ConstStorage() const {
		auto it = m_storages.find(typeid(T));
		if (it == m_storages.end()) return nullptr;
		return static_cast<const ComponentStorage<T>*>(it->second.get());
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Systems
	// ─────────────────────────────────────────────────────────────────────────

	using SystemFn = std::function<void(World&)>;

	/// Register a system to be called by `RunSystems()`.
	void AddSystem(SystemFn fn) {
		m_systems.push_back(std::move(fn));
	}

	/// Call all registered systems in registration order.
	void RunSystems() {
		for (auto& sys : m_systems) sys(*this);
	}

	/// Remove all registered systems.
	void ClearSystems() { m_systems.clear(); }

	// ─────────────────────────────────────────────────────────────────────────
private:
	// ─────────────────────────────────────────────────────────────────────────

	EntityId                                                          m_nextId = 0;
	std::unordered_set<EntityId>                                      m_alive;
	std::unordered_map<std::type_index,
										 std::unique_ptr<IComponentStorage>>            m_storages;
	std::vector<SystemFn>                                             m_systems;

	template<Component T>
	ComponentStorage<T>& GetStorage() {
		auto [it, inserted] = m_storages.emplace(typeid(T), nullptr);
		if (inserted) it->second = std::make_unique<ComponentStorage<T>>();
		return static_cast<ComponentStorage<T>&>(*it->second);
	}

	template<Component T>
	ComponentStorage<T>* TryGetStorage() noexcept {
		auto it = m_storages.find(typeid(T));
		return it != m_storages.end()
			? static_cast<ComponentStorage<T>*>(it->second.get())
			: nullptr;
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// EntityRef  –  inline method definitions (need World to be complete)
// ─────────────────────────────────────────────────────────────────────────────

template<Component T>
EntityRef& EntityRef::Add(T value) {
	assert(IsValid());
	m_World->Add<T>(m_id, std::move(value));
	return *this;
}

template<Component T>
T* EntityRef::Get() {
	assert(IsValid());
	return m_World->Get<T>(m_id);
}

template<Component T>
bool EntityRef::Has() const {
	return IsValid() && m_World->Has<T>(m_id);
}

template<Component T>
EntityRef& EntityRef::Remove() {
	assert(IsValid());
	m_World->Remove<T>(m_id);
	return *this;
}

inline void EntityRef::Destroy() {
	if (m_World && m_id != NullEntity) {
		m_World->DestroyEntity(m_id);
		m_World = nullptr;
		m_id    = NullEntity;
	}
}

} // namespace ECS

} // namespace SDL

#endif // SDL3PP_ECS_H_
