#pragma once

#include "UIIndexSystem.h"
#include "../SDL3pp_engine/ECS.h"

#include <vector>

namespace SDL::UI {

	// ==================================================================================
	// BatchingSystem — groups draw calls for optimal GPU state changes.
	//
	// Consumes the flattened, z-ordered draw list from IndexSystem and groups
	// consecutive calls that share the same render state (texture, blend mode, shader)
	// into batches so that RenderSystem can issue fewer SDL_RenderGeometry calls.
	// Currently a pass-through: returns the ordered draw list unchanged.
	// ==================================================================================

	class BatchingSystem {
	public:
		BatchingSystem(ECS::Context& ctx, IndexSystem& index)
			: m_ctx(ctx), m_index(index) {}

		[[nodiscard]] const std::vector<DrawCall>& Generate(ECS::EntityId root);

	private:
		ECS::Context& m_ctx;
		IndexSystem&  m_index;
	};

	inline const std::vector<DrawCall>&
	BatchingSystem::Generate([[maybe_unused]] ECS::EntityId root) {
		// In the future, group by render state (texture / blend mode) here.
		return m_index.GetDrawList();
	}

} // namespace SDL::UI
