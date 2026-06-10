#ifndef SDL3PP_ENGINE_RENDER_SYSTEM_2D_H_
#define SDL3PP_ENGINE_RENDER_SYSTEM_2D_H_

/**
 * @file Systems/RenderSystem2D.h
 * @brief Draws 2-D sprites and animated sprites via `SDL::Renderer`.
 *
 * `RenderScene` collects every visible `Sprite` / `AnimatedSprite` that also
 * has a `GlobalTransform2D`, sorts them by `zOrder`, applies the active
 * `SceneCamera` (offset + zoom) and draws them in one back-to-front pass.
 *
 * Run after `PropagateTransforms2D`. Debug helpers draw transform crosshairs
 * and sprite bounds.
 */

#include <algorithm>
#include <vector>

#include "../../SDL3pp_render.h"
#include "../Components.h"
#include "../ECS.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/**
 * Render all visible sprites and animated sprites, camera-transformed and
 * z-sorted, to `renderer`.
 */
inline void RenderScene(Context& ctx, RendererRef renderer) {
	SceneCamera camera;
	if (auto* s = ctx.ConstStorage<SceneCamera>())
		if (!s->View().empty()) camera = s->View()[0];

	if (camera.viewportW == 0 || camera.viewportH == 0) {
		Point sz = renderer.GetOutputSize();
		camera.viewportW = sz.x;
		camera.viewportH = sz.y;
	}
	const FPoint viewCentre = {camera.viewportW * 0.5f, camera.viewportH * 0.5f};

	struct SpriteItem { Sprite*         s; GlobalTransform2D* x; float z; };
	struct AnimItem   { AnimatedSprite* s; GlobalTransform2D* x; float z; };

	std::vector<SpriteItem> sprites;
	std::vector<AnimItem>   anims;
	sprites.reserve(ctx.Storage<Sprite>().Size());
	anims.reserve(ctx.Storage<AnimatedSprite>().Size());

	ctx.Each<Sprite, GlobalTransform2D>([&](EntityId e, Sprite& spr, GlobalTransform2D& gt) {
		const Visible* vis = ctx.Get<Visible>(e);
		if (vis && !vis->value) return;
		if (!spr.texture) return;
		sprites.push_back({&spr, &gt, static_cast<float>(spr.zOrder)});
	});
	ctx.Each<AnimatedSprite, GlobalTransform2D>([&](EntityId e, AnimatedSprite& as, GlobalTransform2D& gt) {
		const Visible* vis = ctx.Get<Visible>(e);
		if (vis && !vis->value) return;
		if (!as.texture) return;
		anims.push_back({&as, &gt, static_cast<float>(as.zOrder)});
	});

	std::stable_sort(sprites.begin(), sprites.end(),
		[](const SpriteItem& a, const SpriteItem& b) { return a.z < b.z; });
	std::stable_sort(anims.begin(), anims.end(),
		[](const AnimItem& a, const AnimItem& b) { return a.z < b.z; });

	auto si = sprites.begin();
	auto ai = anims.begin();
	while (si != sprites.end() || ai != anims.end()) {
		const bool drawSprite = ai == anims.end() ||
														(si != sprites.end() && si->z <= ai->z);
		if (drawSprite) {
			Sprite&            spr = *si->s;
			GlobalTransform2D& gt  = *si->x;
			++si;

			const float wx = viewCentre.x + (gt.position.x - camera.offset.x) * camera.zoom;
			const float wy = viewCentre.y + (gt.position.y - camera.offset.y) * camera.zoom;
			const float sx = gt.scale.x * camera.zoom;
			const float sy = gt.scale.y * camera.zoom;

			Point texSz = spr.texture.GetSize();
			const float frameW = (spr.srcRect.w > 0.f) ? spr.srcRect.w : static_cast<float>(texSz.x);
			const float frameH = (spr.srcRect.h > 0.f) ? spr.srcRect.h : static_cast<float>(texSz.y);
			FRect dst = {wx - frameW * sx * spr.pivot.x,
									 wy - frameH * sy * spr.pivot.y,
									 frameW * sx, frameH * sy};

			OptionalRef<const FRectRaw> src = std::nullopt;
			if (spr.srcRect.w > 0.f) src = spr.srcRect;
			FPoint pivot = {dst.w * spr.pivot.x, dst.h * spr.pivot.y};
			const Uint8 a = static_cast<Uint8>(spr.tint.a * spr.alpha);
			spr.texture.SetColorMod(spr.tint.r, spr.tint.g, spr.tint.b);
			spr.texture.SetAlphaMod(a);
			renderer.RenderTextureRotated(spr.texture, src, dst, gt.rotation, pivot);
		} else {
			AnimatedSprite&    as = *ai->s;
			GlobalTransform2D& gt = *ai->x;
			++ai;

			const float wx = viewCentre.x + (gt.position.x - camera.offset.x) * camera.zoom;
			const float wy = viewCentre.y + (gt.position.y - camera.offset.y) * camera.zoom;
			const float sx = gt.scale.x * camera.zoom;
			const float sy = gt.scale.y * camera.zoom;

			FRect frame = as.CurrentFrame();
			const float frameW = frame.w > 0.f ? frame.w : 32.f;
			const float frameH = frame.h > 0.f ? frame.h : 32.f;
			FRect dst = {wx - frameW * sx * as.pivot.x,
									 wy - frameH * sy * as.pivot.y,
									 frameW * sx, frameH * sy};

			OptionalRef<const FRectRaw> src = std::nullopt;
			if (frame.w > 0.f) src = frame;
			FPoint pivot = {dst.w * as.pivot.x, dst.h * as.pivot.y};
			const Uint8 a = static_cast<Uint8>(as.tint.a * as.alpha);
			as.texture.SetColorMod(as.tint.r, as.tint.g, as.tint.b);
			as.texture.SetAlphaMod(a);
			renderer.RenderTextureRotated(as.texture, src, dst, gt.rotation, pivot);
		}
	}
}

/// Thin system wrapper around `RenderScene`.
struct RenderSystem2D {
	static void Run(Context& ctx, RendererRef renderer) { RenderScene(ctx, renderer); }
};

/// Draw a magenta crosshair at every entity's world position.
inline void DebugDrawTransforms2D(Context& ctx, RendererRef renderer, float size = 8.f) {
	renderer.SetDrawColor({255, 0, 255, 200});
	ctx.Each<GlobalTransform2D>([&](EntityId, GlobalTransform2D& gt) {
		renderer.RenderLine({gt.position.x - size, gt.position.y}, {gt.position.x + size, gt.position.y});
		renderer.RenderLine({gt.position.x, gt.position.y - size}, {gt.position.x, gt.position.y + size});
	});
}

/// Draw bounding rects around all sprites.
inline void DebugDrawSpriteBounds(Context& ctx, RendererRef renderer,
																	SDL::Color color = {0, 255, 255, 120}) {
	renderer.SetDrawColor(color);
	ctx.Each<Sprite, GlobalTransform2D>([&](EntityId, Sprite& sp, GlobalTransform2D& gt) {
		Point texSz = sp.texture.GetSize();
		const float w = sp.srcRect.w > 0 ? sp.srcRect.w : static_cast<float>(texSz.x);
		const float h = sp.srcRect.h > 0 ? sp.srcRect.h : static_cast<float>(texSz.y);
		renderer.RenderRect(gt.DestRect(w, h));
	});
}

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_RENDER_SYSTEM_2D_H_
