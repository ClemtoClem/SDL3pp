#ifndef SDL3PP_ENGINE_TRANSFORM_SYSTEM_H_
#define SDL3PP_ENGINE_TRANSFORM_SYSTEM_H_

/**
 * @file Systems/TransformSystem.h
 * @brief Propagates local transforms down the scene hierarchy into world
 *        transforms, for both 2-D and 3-D nodes.
 *
 * Call once per frame, after gameplay/scripts mutate local transforms and
 * before rendering or spatial queries that need world positions.
 *
 * - `PropagateTransforms2D` walks `Transform2D` → `GlobalTransform2D`.
 * - `PropagateTransforms3D` walks `Transform3D` → `GlobalTransform3D`.
 *
 * Roots are entities without a `SceneParent` (or with `SceneParent::id ==
 * NullEntity`). Children are reached through `SceneChildren`.
 */

#include "../Components.h"
#include "../ECS.h"

namespace SDL {
namespace ECS {

/**
 * @defgroup CategoryEngineSystems Engine — Systems
 *
 * One header per system: transform propagation, rendering, animation,
 * scripting, collision, raycasting, signals and processes.
 *
 * @{
 */

/**
 * Compute `GlobalTransform2D` for every node by combining parent → child
 * `Transform2D`s through the hierarchy.
 */
inline void PropagateTransforms2D(Context& ctx) {
	auto dfs = [&](auto& self, EntityId e, const GlobalTransform2D& parent) -> void {
		if (!ctx.IsAlive(e)) return;
		const Transform2D* local = ctx.Get<Transform2D>(e);
		if (!local) return;
		const GlobalTransform2D global = parent.Combine(*local);
		ctx.Add<GlobalTransform2D>(e, global);
		if (const SceneChildren* ch = ctx.Get<SceneChildren>(e))
			for (EntityId child : ch->ids) self(self, child, global);
	};

	const GlobalTransform2D identity{};
	ctx.Each<Transform2D>([&](EntityId e, Transform2D&) {
		const SceneParent* par = ctx.Get<SceneParent>(e);
		if (!par || par->id == NullEntity) dfs(dfs, e, identity);
	});
}

/**
 * Compute `GlobalTransform3D` (world matrix) for every node by multiplying
 * parent → child local matrices through the hierarchy.
 */
inline void PropagateTransforms3D(Context& ctx) {
	auto dfs = [&](auto& self, EntityId e, const GlobalTransform3D& parent) -> void {
		if (!ctx.IsAlive(e)) return;
		const Transform3D* local = ctx.Get<Transform3D>(e);
		if (!local) return;
		const GlobalTransform3D global = parent.Combine(*local);
		ctx.Add<GlobalTransform3D>(e, global);
		if (const SceneChildren* ch = ctx.Get<SceneChildren>(e))
			for (EntityId child : ch->ids) self(self, child, global);
	};

	const GlobalTransform3D identity{};
	ctx.Each<Transform3D>([&](EntityId e, Transform3D&) {
		const SceneParent* par = ctx.Get<SceneParent>(e);
		if (!par || par->id == NullEntity) dfs(dfs, e, identity);
	});
}

/// Convenience wrapper running both 2-D and 3-D propagation.
struct TransformSystem {
	static void Run(Context& ctx) {
		PropagateTransforms2D(ctx);
		PropagateTransforms3D(ctx);
	}
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_TRANSFORM_SYSTEM_H_
