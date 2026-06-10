#pragma once

#include "UIComponents.h"
#include "../SDL3pp_engine/ECS.h"

namespace SDL::UI {

	// ==================================================================================
	// StyleSystem — resolves computed styles for the current frame.
	//
	// Merges StyleBundle overrides into per-widget style components so that the
	// render and layout passes read a single consistent value.
	// Currently a no-op stub: styles are read directly by RenderSystem.
	// ==================================================================================

	class StyleSystem {
	public:
		explicit StyleSystem(ECS::Context& ctx) : m_ctx(ctx) {}

		void Resolve(ECS::EntityId root);

	private:
		ECS::Context& m_ctx;
	};

	inline void StyleSystem::Resolve([[maybe_unused]] ECS::EntityId root) {
		// StyleBundle merging + CSS-cascade computation will live here.
	}

} // namespace SDL::UI
