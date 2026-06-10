#pragma once

#include "UIComponents.h"
#include "../SDL3pp_engine/ECS.h"

namespace SDL::UI {

	// ==================================================================================
	// ClipSystem — scissor (clip) rectangle propagation.
	//
	// DELIBERATE NO-OP (efficiency / cohesion decision, 2026-05-21): inner+outer clip
	// computation (`LayoutSystem::_UpdateClips`) is kept inside LayoutSystem because it
	// shares that system's padding + scrollbar-reservation model (`_ContainerScrollbars`,
	// `_Sp`, `_SbThick`) and the portal-attach (LayerProps) handling. Extracting it
	// would force either duplicating that logic or a back-reference to LayoutSystem —
	// a net negative. The clip results live in ComputedRect::{inner,outer}_clip and are
	// consumed by IndexSystem (draw list), CullingSystem, RenderSystem and the hit-test.
	//
	// In contrast, CullingSystem reads only final geometry, so that concern WAS cleanly
	// separated out. This stub remains as a pipeline slot for a future GPU clip-stack.
	// ==================================================================================

	class ClipSystem {
	public:
		explicit ClipSystem(ECS::Context& ctx) : m_ctx(ctx) {}

		void Update(ECS::EntityId root) { (void)root; }

	private:
		ECS::Context& m_ctx;
	};

} // namespace SDL::UI
