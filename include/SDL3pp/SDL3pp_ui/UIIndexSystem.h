#pragma once

#include "UIComponents.h"
#include "../SDL3pp_engine/ECS.h"

#include <vector>
#include <algorithm>

namespace SDL::UI {

	// ==================================================================================
	// IndexSystem — widget indexing: resolves the draw + event order of every widget.
	//
	// Walks the widget tree once per layout change and produces a single flattened,
	// z-ordered list (`DrawCall`) that defines, for the whole UI:
	//   • on which *plane* (UILayer) each widget is drawn, and
	//   • the order in which widgets receive pointer events.
	//
	// Ordering rules (low → high == back → front):
	//   1. resolved layer  — a widget on UILayer::Content inherits its parent's
	//                        resolved layer, so an entire popup subtree lifts onto the
	//                        popup plane without tagging each descendant; an explicit
	//                        .Layer(...) starts a new resolved layer for its subtree.
	//   2. zOffset         — fine nudging within (or near) a plane; kept small relative
	//                        to the 1000-unit plane spacing of UILayer.
	//   3. document order  — tree pre-order sequence (stable within a layer).
	//
	// RenderSystem consumes the list front→back; the EventSystem hit-test scans it
	// back→front and returns the first widget under the pointer — so layering governs
	// both rendering and event handling from one source of truth.
	// ==================================================================================

	class IndexSystem {
	public:
		explicit IndexSystem(ECS::Context& ctx) : m_ctx(ctx) {}

		/// @brief Rebuild the ordered draw/event list from @p root.
		void Build(ECS::EntityId root);

		[[nodiscard]] const std::vector<DrawCall>& GetDrawList() const noexcept { return m_drawList; }

	private:
		ECS::Context&         m_ctx;
		std::vector<DrawCall> m_drawList;
		Uint32                m_seq = 0;

		void _Traverse(ECS::EntityId e, int parentBaseLayer);
	};

	// ── Implementation ──────────────────────────────────────────────────────────────

	inline void IndexSystem::Build(ECS::EntityId root) {
		m_drawList.clear();
		m_seq = 0;
		if (!m_ctx.IsAlive(root)) return;

		_Traverse(root, (int)UILayer::Content);

		std::stable_sort(m_drawList.begin(), m_drawList.end(),
			[](const DrawCall& a, const DrawCall& b) {
				if (a.layer != b.layer) return a.layer < b.layer;
				return a.seq < b.seq;
			});
	}

	inline void IndexSystem::_Traverse(ECS::EntityId e, int parentBaseLayer) {
		if (!m_ctx.IsAlive(e)) return;

		auto* w  = m_ctx.Get<Widget>(e);
		auto* cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !cr || !Has(w->behavior, WidgetBehaviorFlag::Visible)) return;

		// Resolve this widget's base plane: an explicit layer (inherit==false, or any
		// non-Content value) opens a new plane for the subtree; otherwise inherit.
		int  base = parentBaseLayer;
		int  zoff = 0;
		if (auto* lp = m_ctx.Get<LayerProps>(e)) {
			if (!lp->inherit || lp->layer != UILayer::Content)
				base = (int)lp->layer;
			zoff = lp->zOffset;
		}

		// Skip the culled node itself, but still descend: a portaled descendant
		// (clipped to the root) may be on-screen even when this ancestor is not.
		if (!cr->culled)
			m_drawList.push_back(DrawCall{e, cr->inner_clip, base + zoff, m_seq++});

		// Children inherit this widget's base plane (not its zOffset).
		if (auto* ch = m_ctx.Get<Children>(e)) {
			for (ECS::EntityId c : ch->ids)
				_Traverse(c, base);
		}
	}

} // namespace SDL::UI
