#pragma once

#include "UIComponents.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_rect.h"

namespace SDL::UI {

	// ==================================================================================
	// CullingSystem — flags widgets that are fully off-screen so they are skipped.
	//
	// Reads only final geometry (ComputedRect: absolute / inner_clip) against the
	// viewport — no coupling to the layout / scrollbar model — which is exactly why
	// this concern is cleanly separable from LayoutSystem (whereas clip computation,
	// sharing the padding + scrollbar-reservation logic, is kept inside LayoutSystem).
	//
	// A widget is culled when its visible (clipped) rect has no area: this happens
	// for content scrolled out of a container (its inner_clip collapses against the
	// ancestor clip) or anything outside the viewport. IndexSystem skips culled
	// widgets, removing them from both the draw list and the hit-test list.
	//
	// Portaled popups are unaffected: their inner_clip is intersected with the root /
	// window (not the tree parent), so they survive an off-screen ancestor.
	// ==================================================================================

	class CullingSystem {
	public:
		explicit CullingSystem(ECS::Context& ctx) : m_ctx(ctx) {}

		void Update(ECS::EntityId root, FRect viewport);

	private:
		ECS::Context& m_ctx;
		void _Cull(ECS::EntityId e, FRect viewport);
	};

	inline void CullingSystem::Update(ECS::EntityId root, FRect viewport) {
		if (!m_ctx.IsAlive(root)) return;
		_Cull(root, viewport);
	}

	inline void CullingSystem::_Cull(ECS::EntityId e, FRect viewport) {
		if (!m_ctx.IsAlive(e)) return;
		auto* cr = m_ctx.Get<ComputedRect>(e);
		if (!cr) return;

		// Fully clipped away (e.g. scrolled out) or entirely outside the viewport.
		const FRect& c = cr->inner_clip;
		bool empty   = (c.w <= 0.f || c.h <= 0.f);
		bool offview = !cr->absolute.HasIntersection(viewport);
		cr->culled = empty || offview;

		if (auto* ch = m_ctx.Get<Children>(e)) {
			for (ECS::EntityId cid : ch->ids)
				_Cull(cid, viewport);
		}
	}

} // namespace SDL::UI
