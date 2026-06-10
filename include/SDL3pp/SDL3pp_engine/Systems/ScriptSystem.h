#ifndef SDL3PP_ENGINE_SCRIPT_SYSTEM_H_
#define SDL3PP_ENGINE_SCRIPT_SYSTEM_H_

/**
 * @file Systems/ScriptSystem.h
 * @brief Runs per-entity `SceneScript` callbacks (ready / update / input).
 *
 * `onReady` fires exactly once on the first frame an entity is processed,
 * `onUpdate` fires every frame, and `onInput` fires for every SDL event passed
 * to `DispatchInput`.
 */

#include "../Components.h"
#include "../ECS.h"
#include "../../SDL3pp_events.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/// Drives `SceneScript` callbacks. See @ref AnimationSystem for tweens/anims.
struct ScriptSystem {
	/// Fire `onReady` (once) then `onUpdate` for every scripted entity.
	static void Run(Context& ctx, float dt) {
		ctx.Each<SceneScript>([&](EntityId id, SceneScript& s) {
			if (s.onReady && !s.readyCalled) {
				s.readyCalled = true;
				s.onReady(id, ctx);
			}
			if (s.onUpdate) s.onUpdate(id, ctx, dt);
		});
	}

	/// Forward an SDL event to every `SceneScript::onInput`.
	static void DispatchInput(Context& ctx, const SDL::Event& ev) {
		ctx.Each<SceneScript>([&](EntityId id, SceneScript& s) {
			if (s.onInput) s.onInput(id, ctx, ev);
		});
	}
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_SCRIPT_SYSTEM_H_
