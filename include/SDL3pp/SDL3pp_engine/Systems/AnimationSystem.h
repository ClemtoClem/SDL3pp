#ifndef SDL3PP_ENGINE_ANIMATION_SYSTEM_H_
#define SDL3PP_ENGINE_ANIMATION_SYSTEM_H_

/**
 * @file Systems/AnimationSystem.h
 * @brief Advances all time-based animation components each frame.
 *
 * Handles three independent kinds of animation:
 *   - `AnimatedSprite`  — 2-D sprite-sheet frame playback
 *   - `SceneTween`      — single-value easing tweens
 *   - `AnimationPlayer` — 3-D keyframe clips sampled into `Transform3D`
 *
 * Finished, non-looping tweens are removed automatically.
 */

#include <vector>

#include "../Components.h"
#include "../ECS.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/// Advances sprite, tween and keyframe animations.
struct AnimationSystem {
	static void Run(Context& ctx, float dt) {
		// Sprite-sheet playback.
		ctx.Each<AnimatedSprite>([dt](EntityId, AnimatedSprite& as) { as.Update(dt); });

		// Value tweens — collect finished ones to remove after iteration.
		std::vector<EntityId> finished;
		ctx.Each<SceneTween>([&](EntityId e, SceneTween& tw) {
			tw.Update(dt);
			if (tw.done) finished.push_back(e);
		});
		for (EntityId e : finished) ctx.Remove<SceneTween>(e);

		// Keyframe clips → Transform3D.
		ctx.Each<AnimationPlayer>([&](EntityId e, AnimationPlayer& ap) {
			if (!ap.playing || !ap.clip || ap.clip->keys.empty()) return;
			ap.time += dt * ap.speed;
			if (!ap.clip->loop && ap.time >= ap.clip->duration) {
				ap.time = ap.clip->duration;
				ap.playing = false;
			}
			const Keyframe k = ap.clip->Sample(ap.time);
			Transform3D* t = ctx.Get<Transform3D>(e);
			if (!t) t = &ctx.Add<Transform3D>(e);
			t->position = k.position;
			t->rotation = k.rotation;
			t->scale    = k.scale;
		});
	}
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_ANIMATION_SYSTEM_H_
