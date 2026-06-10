#ifndef SDL3PP_ENGINE_RENDER_SYSTEM_3D_H_
#define SDL3PP_ENGINE_RENDER_SYSTEM_3D_H_

/**
 * @file Systems/RenderSystem3D.h
 * @brief GPU mesh rendering system built on `SDL3pp_gpu.h`.
 *
 * Records draw calls for every visible `MeshRenderer` into a render pass. The
 * application owns the swapchain, render pass, depth buffer and graphics
 * pipeline (shaders are inherently app-specific); this system fills in the
 * per-object work: frustum culling, world/MVP matrices, uniform upload and the
 * indexed draw.
 *
 * Pipeline contract for the built-in path:
 *   - the vertex input layout matches `SDL::GetVertexAttributes()`
 *   - the vertex shader reads a uniform block at slot 0 laid out as
 *     `MeshUniforms` (mvp, model, normalMatrix)
 *
 * For anything fancier (per-material pipelines, texture binding, custom
 * uniforms) pass a `DrawHook` and take over the per-object recording.
 *
 * ```cpp
 * auto cmd  = device.AcquireCommandBuffer();
 * auto tex  = cmd.AcquireSwapchainTexture(window);
 * auto pass = cmd.BeginRenderPass(colorTargets, &depthTarget);
 * pass.BindGraphicsPipeline(pipeline);
 *
 * FMatrix4 vp;
 * SDL::ECS::RenderSystem3D::ComputeViewProj(ctx, aspect, vp);
 * SDL::ECS::RenderSystem3D::Render(ctx, cmd, pass, vp);
 *
 * pass.End();
 * cmd.Submit();
 * ```
 */

#include <functional>
#include <vector>

#include "../../SDL3pp_gpu.h"
#include "../../SDL3pp_strings.h"
#include "../Components.h"
#include "../ECS.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/// Per-object uniform block pushed to vertex uniform slot 0 by the built-in path.
struct MeshUniforms {
	FMatrix4 mvp          = FMatrix4::Identity(); ///< projection * view * model
	FMatrix4 model        = FMatrix4::Identity(); ///< world matrix (for world-space lighting)
	FMatrix4 normalMatrix = FMatrix4::Identity(); ///< inverse-transpose of model
};

/// GPU-friendly light record produced by `CollectLights` (std140-ish, mat-free).
struct GpuLight {
	FVector4 position;   ///< xyz = world position (point/spot), w = type (0 dir,1 point,2 spot)
	FVector4 direction;  ///< xyz = world direction, w = range
	FVector4 color;      ///< rgb = colour * intensity, a = spot outer-cone cosine
	FVector4 params;     ///< x = spot inner-cone cosine, yzw reserved
};

/// GPU mesh rendering system.
struct RenderSystem3D {
	/// `void(cmd, pass, entity, MeshRenderer, model, mvp)` — record one object yourself.
	using DrawHook = std::function<void(GPUCommandBuffer&, GPURenderPass&, EntityId,
																			const MeshRenderer&, const FMatrix4&, const FMatrix4&)>;

	/**
	 * Find the primary `Camera3D` and build its view-projection matrix.
	 *
	 * @param ctx       the world.
	 * @param aspect    viewport width / height (used when the camera's is 0).
	 * @param outVP[out]projection * view on success.
	 * @param eyeOut    optional out: the camera world position.
	 * @returns true if a usable camera was found.
	 */
	static bool ComputeViewProj(Context& ctx, float aspect, FMatrix4& outVP,
															FVector3* eyeOut = nullptr) {
		bool found = false;
		ctx.Each<Camera3D, GlobalTransform3D>([&](EntityId, Camera3D& cam, GlobalTransform3D& gt) {
			if (found && !cam.primary) return;
			const FMatrix4 view = Camera3D::ViewMatrix(gt);
			const FMatrix4 proj = cam.ProjectionMatrix(aspect);
			outVP = proj * view;
			if (eyeOut) *eyeOut = gt.Position();
			found = true;
		});
		return found;
	}

	/**
	 * Gather up to `maxLights` lights into `out` for fragment-shader upload.
	 *
	 * @returns the number of lights written.
	 */
	static Uint32 CollectLights(Context& ctx, GpuLight* out, Uint32 maxLights) {
		Uint32 n = 0;
		ctx.Each<Light, GlobalTransform3D>([&](EntityId, Light& l, GlobalTransform3D& gt) {
			if (n >= maxLights) return;
			const FVector3 pos = gt.Position();
			const FVector3 dir = (gt.world.TransformDir({0.f, 0.f, -1.f})).Normalize();
			GpuLight g;
			g.position  = FVector4(pos, static_cast<float>(static_cast<int>(l.type)));
			g.direction = FVector4(dir, l.range);
			g.color     = FVector4(l.color * l.intensity, SDL::Cos(l.outerCone));
			g.params    = FVector4(SDL::Cos(l.innerCone), 0.f, 0.f, 0.f);
			out[n++] = g;
		});
		return n;
	}

	/**
	 * Record draws for every visible `MeshRenderer`.
	 *
	 * A compatible graphics pipeline must already be bound on `pass`.
	 *
	 * @param ctx        the world.
	 * @param cmd        the command buffer that owns `pass` (for uniform pushes).
	 * @param pass       the active render pass.
	 * @param viewProj   projection * view.
	 * @param cull       skip meshes whose bounds fall outside the view frustum.
	 * @param hook       optional per-object recorder; when set it replaces the
	 *                   built-in uniform push (you must push your own uniforms).
	 */
	static void Render(Context& ctx, GPUCommandBuffer& cmd, GPURenderPass& pass,
										 const FMatrix4& viewProj, bool cull = true,
										 const DrawHook& hook = {}) {
		const FFrustum frustum = FFrustum::FromViewProj(viewProj);

		ctx.Each<MeshRenderer, GlobalTransform3D>([&](EntityId e, MeshRenderer& mr, GlobalTransform3D& gt) {
			if (!mr.visible || !mr.mesh || !mr.mesh->IsValid()) return;
			if (const Visible* v = ctx.Get<Visible>(e); v && !v->value) return;

			const FMatrix4& model = gt.world;
			if (cull) {
				const FAABB worldBounds = mr.mesh->Bounds().Transformed(model);
				if (!frustum.Intersects(worldBounds)) return;
			}
			const FMatrix4 mvp = viewProj * model;

			if (hook) {
				hook(cmd, pass, e, mr, model, mvp);
			} else {
				MeshUniforms u;
				u.mvp          = mvp;
				u.model        = model;
				u.normalMatrix = model.Inverse().Transpose();
				cmd.PushVertexUniformData(0, SourceBytes(&u, sizeof(u)));
			}
			mr.mesh->BindAndDraw(pass);
		});
	}
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_RENDER_SYSTEM_3D_H_
