#pragma once

#include "UIComponents.h"
#include "../SDL3pp_engine/ECS.h"

namespace SDL::UI {

	// ==================================================================================
	// MeasureSystem — bottom-up intrinsic-size computation.
	//
	// DELIBERATE NO-OP (efficiency decision, 2026-05-21): intrinsic sizing is fused
	// into LayoutSystem's single recursive measure→place descent (`_Measure` /
	// `_IntrinsicSize`), where it also reserves scrollbar space and resolves content
	// sizes using the live layout context. Splitting it into a standalone pass would
	// require a second full tree traversal (or duplicated context propagation) for no
	// functional gain, so measure is kept inside LayoutSystem. This stub remains as a
	// pipeline slot in case a future constraint-based two-pass layout needs it.
	// ==================================================================================

	class MeasureSystem {
	public:
		explicit MeasureSystem(ECS::Context& ctx) : m_ctx(ctx) {}

		void Update(ECS::EntityId root) { (void)root; }

	private:
		ECS::Context& m_ctx;
	};

} // namespace SDL::UI
