#pragma once

#include "UIComponents.h"
#include "../SDL3pp_engine/ECS.h"

#include <vector>
#include <algorithm>

namespace SDL::UI {

	// ==================================================================================
	// FocusSystem — keyboard focus-ring navigation between focusable widgets.
	//
	// Owns the navigation algorithm extracted from EventSystem: it gathers the ordered
	// set of focusable widgets reachable from the root (document order, skipping
	// hidden / disabled subtrees) and computes the next / previous target for Tab and
	// Shift+Tab traversal.
	//
	// It deliberately does NOT own the focused-entity state: applying focus drives
	// gain/lose callbacks and the text-input lifecycle, which are dispatch concerns
	// that stay in EventSystem. EventSystem queries FocusSystem for the target and
	// then commits it via its own _SetFocus, so this system has no upstream deps and
	// no side effects (every method is const / pure).
	// ==================================================================================

	class FocusSystem {
	public:
		explicit FocusSystem(ECS::Context& ctx) : m_ctx(ctx) {}

		/// @brief Per-frame hook (focus-ring maintenance / validation). No-op for now.
		void Update(ECS::EntityId root) { (void)root; }

		/// @brief Append every focusable widget reachable from @p e in document order.
		void CollectFocusable(ECS::EntityId e, std::vector<ECS::EntityId>& out) const;

		/// @brief Focus-ring target after @p current when traversing forward/backward.
		///        Wraps around; returns NullEntity if nothing is focusable.
		[[nodiscard]] ECS::EntityId Next(ECS::EntityId root, ECS::EntityId current,
		                                 bool forward) const;

	private:
		ECS::Context& m_ctx;
	};

	// ── Implementation ──────────────────────────────────────────────────────────────

	inline void FocusSystem::CollectFocusable(ECS::EntityId e, std::vector<ECS::EntityId>& out) const {
		if (!m_ctx.IsAlive(e)) return;
		auto* w = m_ctx.Get<Widget>(e);
		if (!w || !Has(w->behavior, WidgetBehaviorFlag::Visible) || !Has(w->behavior, WidgetBehaviorFlag::Enable))
			return;
		if (Has(w->behavior, WidgetBehaviorFlag::Focusable))
			out.push_back(e);
		if (auto* ch = m_ctx.Get<Children>(e)) {
			for (ECS::EntityId cid : ch->ids)
				CollectFocusable(cid, out);
		}
	}

	inline ECS::EntityId FocusSystem::Next(ECS::EntityId root, ECS::EntityId current,
	                                       bool forward) const {
		if (!m_ctx.IsAlive(root)) return ECS::NullEntity;

		std::vector<ECS::EntityId> ring;
		CollectFocusable(root, ring);
		if (ring.empty()) return ECS::NullEntity;

		if (!forward) std::reverse(ring.begin(), ring.end());

		auto it = std::find(ring.begin(), ring.end(), current);
		if (it == ring.end() || std::next(it) == ring.end())
			return ring.front();
		return *std::next(it);
	}

} // namespace SDL::UI
