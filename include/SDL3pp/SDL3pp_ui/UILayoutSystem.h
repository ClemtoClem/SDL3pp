#pragma once

#include "UIComponents.h"
#include "UIValue.h"
#include "../SDL3pp_engine/ECS.h"
#include "../SDL3pp_rect.h"

#include <vector>
#include <functional>
#include <algorithm>

namespace SDL::UI {

	class Context;  // Forward declaration

	// ==================================================================================
	// LayoutSystem — Measure + Place pipeline
	// ==================================================================================
	//
	// Owns:
	//   - the dirty flag that controls when a recalculation runs
	//   - geometry (ComputedRect: measured / relative / absolute / inner+outer clip)
	//
	// The flattened, z-ordered draw/event list is produced separately by IndexSystem
	// (which reads the ComputedRect clips written here); RenderSystem and the event
	// hit-test consume that list.
	//
	// API:
	//   - Process(root, viewport): runs Measure + Place + clip propagation if dirty.
	//   - MarkDirty():             requests a recalculation on the next Process call.
	//
	// ==================================================================================

	class LayoutSystem {
	public:
		LayoutSystem(ECS::Context& ctx, Context& sys);

		void Process(ECS::EntityId root, FRect viewport);

		void                 MarkDirty() noexcept           { m_dirty = true; }
		void                 MarkDirty(ECS::EntityId e);  ///< Mark a specific subtree (and its ancestors) dirty.
		[[nodiscard]] bool   IsDirty()  const noexcept     { return m_dirty; }

	private:
		ECS::Context&         m_ctx;
		Context&               m_sys;
		bool                  m_dirty   = true;
		FRect                 m_viewport = {};
		FRect                 m_rootClip = {};  ///< Clip used by widgets portaled to the root.

		// ── Spacing / scrollbar helpers ───────────────────────────────────────────
		[[nodiscard]] const SpacingStyle& _Sp(ECS::EntityId e) const noexcept {
			static const SpacingStyle k_def;
			auto* s = m_ctx.Get<SpacingStyle>(e);
			return s ? *s : k_def;
		}
		[[nodiscard]] float _SbThick(ECS::EntityId e) const noexcept {
			auto* s = m_ctx.Get<ScrollbarStyle>(e);
			return s ? s->thickness : 8.f;
		}

		FPoint _Measure      (ECS::EntityId e, const LayoutContext& ctx);
		void   _Place        (ECS::EntityId e, FRect rect, const LayoutContext& ctx);
		FPoint _IntrinsicSize(ECS::EntityId e);
		void   _ClearDirtyLayout(ECS::EntityId root);
		void   _UpdateClips(ECS::EntityId e, FRect parentClip);

		LayoutContext _MakeRootCtx(FRect viewport) const noexcept;
		LayoutContext _MakeChildCtx(ECS::EntityId parent, const LayoutContext& parentCtx) const noexcept;

		float _TextWidth(const std::string& text, ECS::EntityId e);
		float _TextHeight(ECS::EntityId e);
		void  _ContainerScrollbars(const Widget& w, const LayoutProps& lp, ECS::EntityId e,
		                           float cW, float cH, bool& outShowX, bool& outShowY) const noexcept;
	};

	// ==================================================================================
	// Implementation: LayoutSystem
	// ==================================================================================

	inline LayoutSystem::LayoutSystem(ECS::Context& ctx, Context& sys) : m_ctx(ctx), m_sys(sys) {}

	inline void LayoutSystem::Process(ECS::EntityId root, FRect viewport) {
		if (!m_ctx.IsAlive(root)) return;
		// If viewport changed (e.g. window resize), mark layout dirty to recalculate all geometry
		if (m_viewport != viewport) {
			m_dirty = true;
		}
		m_viewport = viewport;
		if (m_dirty) {
			LayoutContext rootCtx = _MakeRootCtx(viewport);
			_Measure(root, rootCtx);
			_Place(root, viewport, rootCtx);
			if (auto* rcr = m_ctx.Get<ComputedRect>(root))
				m_rootClip = rcr->absolute.GetIntersection(viewport);
			else
				m_rootClip = viewport;
			_UpdateClips(root, viewport);
			_ClearDirtyLayout(root);
			m_dirty = false;
		}
	}

	inline void LayoutSystem::MarkDirty(ECS::EntityId e) {
		if (!m_ctx.IsAlive(e)) return;
		m_dirty = true;
		auto *parent = m_ctx.Get<Parent>(e);
		if (parent && m_ctx.IsAlive(parent->id)) {
			MarkDirty(parent->id);
		}
	}


	inline void LayoutSystem::_ContainerScrollbars(const Widget& w, const LayoutProps& lp, ECS::EntityId e,
	                                                  float cW, float cH, bool& outShowX, bool& outShowY) const noexcept {
		outShowX = outShowY = false;
		if (w.type != WidgetType::Container && w.type != WidgetType::ListBox &&
		    w.type != WidgetType::TextArea && w.type != WidgetType::Tree)
			return;

		bool always_x = Has(w.behavior, WidgetBehaviorFlag::ScrollableX);
		bool always_y = Has(w.behavior, WidgetBehaviorFlag::ScrollableY);
		bool auto_x   = Has(w.behavior, WidgetBehaviorFlag::AutoScrollableX);
		bool auto_y   = Has(w.behavior, WidgetBehaviorFlag::AutoScrollableY);

		// First pass: basic check without cross-axis compensation
		outShowX = always_x || (auto_x && lp.contentW > cW);
		outShowY = always_y || (auto_y && lp.contentH > cH);

		float sbt = _SbThick(e);
		// Second pass: if one scrollbar is visible, it reduces space for the other axis
		if (outShowY && !outShowX && auto_x)
			outShowX = (lp.contentW > cW - sbt);
		if (outShowX && !outShowY && auto_y)
			outShowY = (lp.contentH > cH - sbt);
	}

	inline LayoutContext LayoutSystem::_MakeRootCtx(FRect viewport) const noexcept {
		LayoutContext ctx;
		ctx.windowSize = {viewport.w, viewport.h};
		ctx.rootSize = {viewport.w, viewport.h};
		ctx.rootFontSize = 14.f; // TODO: configurable
		ctx.parentSize = {viewport.w, viewport.h};
		ctx.parentPadding = {0.f, 0.f, 0.f, 0.f};
		ctx.parentFontSize = ctx.rootFontSize;
		return ctx;
	}

	inline LayoutContext LayoutSystem::_MakeChildCtx(ECS::EntityId parent, const LayoutContext& parentCtx) const noexcept {
		LayoutContext ctx = parentCtx;
		if (!m_ctx.IsAlive(parent)) return ctx;
		auto *lp = m_ctx.Get<LayoutProps>(parent);
		if (!lp) return ctx;
		ctx.parentSize = FPoint{lp->contentW, lp->contentH};
		ctx.parentPadding = _Sp(parent).padding;
		ctx.parentFontSize = parentCtx.rootFontSize; // TODO: from parent style
		return ctx;
	}

	inline FPoint LayoutSystem::_IntrinsicSize(ECS::EntityId e) {
		auto *w = m_ctx.Get<Widget>(e);
		if (!w) return {};
		float ch = _TextHeight(e);
		switch (w->type) {
		case WidgetType::Label:
		case WidgetType::Button: {
			auto *te = m_ctx.Get<TextEdit>(e);
			if (!te || te->text.empty())
				return {60.f, ch + 4.f};
			return {_TextWidth(te->text, e), ch + 4.f};
		}
		case WidgetType::Toggle:
			return {80.f, 28.f};
		case WidgetType::Radio:
			return {80.f, 24.f};
		case WidgetType::Slider:
			return {80.f, 24.f};
		case WidgetType::ScrollBar:
			return {10.f, 80.f};
		case WidgetType::Input: {
			const float arrowsW = m_ctx.Get<NumericValue<float>>(e) ? 20.f : 0.f;
			return {80.f + arrowsW, SDL::Max(30.f, ch + 8.f)};
		}
		case WidgetType::TextArea:
			return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
		case WidgetType::ListBox:
			return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
		case WidgetType::Graph:
			return {200.f, 120.f};
		case WidgetType::Progress:
			return {80.f, 18.f};
		case WidgetType::Separator:
			return {0.f, 1.f};
		case WidgetType::Knob:
			return {56.f, 56.f};
		case WidgetType::ComboBox:
			return {120.f, SDL::Max(28.f, ch + 8.f)};
		case WidgetType::Image:
			return {160.f, 120.f};
		case WidgetType::Canvas:
			return {160.f, 120.f};
		case WidgetType::TabView: {
			auto *tvd = m_ctx.Get<TabViewData>(e);
			return {200.f, tvd ? tvd->tabHeight : 32.f};
		}
		case WidgetType::Expander: {
			auto *exd = m_ctx.Get<ExpanderData>(e);
			return {120.f, exd ? exd->headerH : 28.f};
		}
		case WidgetType::Splitter:
			return {200.f, 200.f};
		case WidgetType::Spinner:
			return {32.f, 32.f};
		case WidgetType::Badge:
			return {SDL::Max(20.f, _TextWidth("0", e) + 10.f), SDL::Max(18.f, ch + 4.f)};
		case WidgetType::ColorPicker:
			return {220.f, 240.f};
		case WidgetType::Popup: {
			auto *pd = m_ctx.Get<PopupConfig>(e);
			return {320.f, 240.f + (pd ? pd->headerH : 28.f)};
		}
		case WidgetType::Tree:
			return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
		case WidgetType::MenuBar:
			return {400.f, ch + 8.f};
		default:
			return {};
		}
	}

	inline FPoint LayoutSystem::_Measure(ECS::EntityId e, const LayoutContext& ctx) {
		if (!m_ctx.IsAlive(e)) return {};
		auto *w  = m_ctx.Get<Widget>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !lp || !cr) return {};
		if (!Has(w->behavior, WidgetBehaviorFlag::Visible)) {
			cr->measured = {};
			return {};
		}

		const auto& sp = _Sp(e);

		bool wa = lp->width.IsAuto() || lp->width.IsGrow();
		bool ha = lp->height.IsAuto() || lp->height.IsGrow();
		float fw = wa ? 0.f : lp->width.Resolve(ctx);
		float fh = ha ? 0.f : lp->height.Resolve(ctx);

		float cW = SDL::Max(0.f, (wa ? ctx.parentSize.x : fw) - sp.padding.left - sp.padding.right);
		float cH = SDL::Max(0.f, (ha ? ctx.parentSize.y : fh) - sp.padding.top  - sp.padding.bottom);

		// Pre-compute content size for container-like widgets
		if (w->type == WidgetType::ListBox) {
			if (auto *ilv = m_ctx.Get<ItemListView>(e)) {
				lp->contentH = (float)ilv->items.size() * ilv->itemHeight;
				float maxW = 0.f;
				for (const auto &item : ilv->items)
					maxW = SDL::Max(maxW, _TextWidth(item, e));
				lp->contentW = maxW;
			}
		} else if (w->type == WidgetType::TextArea) {
			if (m_ctx.Has<TextAreaData>(e)) {
				auto *te = m_ctx.Get<TextEdit>(e);
				if (te) {
					float lineH = _TextHeight(e) + 2.f;
					int lines = std::count(te->text.begin(), te->text.end(), '\n') + 1;
					lp->contentH = (float)lines * lineH;
					float maxW = 0.f;
					size_t lineStart = 0;
					for (size_t i = 0; i <= te->text.size(); ++i) {
						if (i == te->text.size() || te->text[i] == '\n') {
							float lw = _TextWidth(te->text.substr(lineStart, i - lineStart), e);
							maxW = SDL::Max(maxW, lw);
							lineStart = i + 1;
						}
					}
					lp->contentW = maxW;
				}
			}
		}

		// Reserve space for scrollbars
		if (w->type == WidgetType::Container || w->type == WidgetType::ListBox ||
		    w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
			bool showX = false, showY = false;
			_ContainerScrollbars(*w, *lp, e, cW, cH, showX, showY);
			float sbt = _SbThick(e);
			if (showY) cW = SDL::Max(0.f, cW - sbt);
			if (showX) cH = SDL::Max(0.f, cH - sbt);
		}

		FPoint intr = _IntrinsicSize(e);

		LayoutContext cc;
		cc.windowSize = ctx.windowSize;
		cc.rootSize = ctx.rootSize;
		cc.rootFontSize = ctx.rootFontSize;
		cc.parentSize = {cW, cH};
		cc.parentPadding = sp.padding;
		cc.parentFontSize = ctx.rootFontSize;

		float chW = 0.f, chH = 0.f;
		float curLineW = 0.f, curLineH = 0.f;
		int vis = 0;
		auto *ch = m_ctx.Get<Children>(e);

		if (ch && (lp->layout == Layout::InColumn || lp->layout == Layout::InLine)) {
			const bool isCol = (lp->layout == Layout::InColumn);
			auto growOf = [&](const LayoutProps* cl) -> float {
				return 0.01f * (isCol ? (cl->height.IsGrow() ? cl->height.val : 0.f)
							 : (cl->width.IsGrow() ? cl->width.val  : 0.f));
			};
			std::vector<ECS::EntityId> flow;
			float tFixed = 0.f, tGrow = 0.f;

			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cl2 = m_ctx.Get<LayoutProps>(cid);
				if (!cw2 || !cl2 || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
				if (cl2->attach == AttachLayout::Absolute || cl2->attach == AttachLayout::Fixed) {
					_Measure(cid, cc);
					continue;
				}
				const auto& cspm = _Sp(cid);
				flow.push_back(cid);
				float g = growOf(cl2);
				tGrow += g;
				if (isCol) tFixed += cspm.margin.top  + cspm.margin.bottom;
				else       tFixed += cspm.margin.left + cspm.margin.right;
				if (g == 0.f) {
					FPoint csz = _Measure(cid, cc);
					if (isCol) {
						chW = SDL::Max(chW, csz.x + cspm.margin.left + cspm.margin.right);
						tFixed += csz.y;
					} else {
						chH = SDL::Max(chH, csz.y + cspm.margin.top + cspm.margin.bottom);
						tFixed += csz.x;
					}
				}
			}

			int fvis    = (int)flow.size();
			float avail   = isCol ? cH : cW;
			float gBudget = SDL::Max(0.f, avail - tFixed - sp.gap * SDL::Max(0, fvis - 1));
			float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

			for (int i = 0; i < fvis; ++i) {
				ECS::EntityId cid = flow[i];
				auto *cl2  = m_ctx.Get<LayoutProps>(cid);
				auto *ccr2 = m_ctx.Get<ComputedRect>(cid);
				const auto& cspm = _Sp(cid);
				float g = growOf(cl2);

				if (g > 0.f) {
					float growSz = SDL::Max(0.f, gUnit * g);
					LayoutContext growCtx = cc;
					if (isCol) growCtx.parentSize.y = growSz;
					else       growCtx.parentSize.x = growSz;
					_Measure(cid, growCtx);
					if (isCol) ccr2->measured.y = growSz;
					else       ccr2->measured.x = growSz;
				}

				float mw = ccr2->measured.x + cspm.margin.left + cspm.margin.right;
				float mh = ccr2->measured.y + cspm.margin.top  + cspm.margin.bottom;
				if (isCol) {
					chW  = SDL::Max(chW, mw);
					chH += mh + (i > 0 ? sp.gap : 0.f);
				} else {
					chH  = SDL::Max(chH, mh);
					chW += mw + (i > 0 ? sp.gap : 0.f);
				}
			}
			vis = fvis;

		} else if (ch && lp->layout == Layout::Stack) {
			int lineVis = 0;
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw  = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				if (!cw || !cl || !Has(cw->behavior, WidgetBehaviorFlag::Visible)) continue;

				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
					_Measure(cid, cc);
					continue;
				}

				const auto& cspm = _Sp(cid);
				FPoint csz = _Measure(cid, cc);
				float mw = csz.x + cspm.margin.left + cspm.margin.right;
				float mh = csz.y + cspm.margin.top  + cspm.margin.bottom;

				float flowW = mw;
				if (lineVis > 0 && cW > 0.f && curLineW + sp.gap + flowW > cW) {
					chW = SDL::Max(chW, curLineW);
					chH += curLineH + sp.gap;
					curLineW = flowW;
					curLineH = mh;
					lineVis = 1;
				} else {
					curLineW += flowW + (lineVis > 0 ? sp.gap : 0.f);
					curLineH = SDL::Max(curLineH, mh);
					lineVis++;
				}
				++vis;
			}
			if (lineVis > 0) {
				chW = SDL::Max(chW, curLineW);
				chH += curLineH;
			}

		} else if (ch && lp->layout == Layout::InGrid) {
			// Measure all children first
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				if (!cw2 || !cl || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
				_Measure(cid, cc);
			}

			auto *gp     = m_ctx.Get<LayoutGridProps>(e);
			int numCols  = gp ? SDL::Max(1, gp->columns) : 2;
			float gap    = sp.gap;

			int numRows  = gp ? gp->rows : 0;
			if (numRows <= 0) {
				int autoIdx = 0;
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					if (!cw2 || !cl || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
					auto *gc  = m_ctx.Get<GridCell>(cid);
					int r = gc ? gc->row : (autoIdx / numCols);
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					numRows = SDL::Max(numRows, r + rs);
					if (!gc) ++autoIdx;
				}
				numRows = SDL::Max(1, numRows);
			}

			GridSizing cSiz = gp ? gp->colSizing : GridSizing::Fixed;
			GridSizing rSiz = gp ? gp->rowSizing : GridSizing::Fixed;
			float baseCellW = SDL::Max(1.f, (cW - gap * (float)(numCols - 1)) / (float)numCols);
			float baseCellH = (!ha && numRows > 0) ? SDL::Max(1.f, (cH - gap * (float)(numRows - 1)) / (float)numRows) : 0.f;

			std::vector<float> colW(numCols, cSiz == GridSizing::Fixed ? baseCellW : 0.f);
			std::vector<float> rowH(numRows, (rSiz == GridSizing::Fixed && baseCellH > 0.f) ? baseCellH : 0.f);

			if (cSiz == GridSizing::Content || rSiz == GridSizing::Content || baseCellH == 0.f) {
				int autoIdx = 0;
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *gc  = m_ctx.Get<GridCell>(cid);
					auto *ccr = m_ctx.Get<ComputedRect>(cid);
					const auto& cspm = _Sp(cid);
					if (!cw2 || !cl || !ccr || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) { if (!gc) ++autoIdx; continue; }
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					int c  = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : ((autoIdx % numCols));
					int r  = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : ((autoIdx / numCols));
					int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					if (!gc) ++autoIdx;

					if (cSiz == GridSizing::Content) {
						float childW = ccr->measured.x + cspm.margin.left + cspm.margin.right;
						float perCol = SDL::Max(0.f, (childW - gap * (float)(cs - 1)) / (float)cs);
						for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
							colW[ci] = SDL::Max(colW[ci], perCol);
					}
					if (rSiz == GridSizing::Content || baseCellH == 0.f) {
						float childH = ccr->measured.y + cspm.margin.top + cspm.margin.bottom;
						float perRow = SDL::Max(0.f, (childH - gap * (float)(rs - 1)) / (float)rs);
						for (int ri = r; ri < SDL::Min(r + rs, numRows); ++ri)
							rowH[ri] = SDL::Max(rowH[ri], perRow);
					}
				}
			}

			if (gp) {
				gp->colWidths  = colW;
				gp->rowHeights = rowH;
			}

			chW = 0.f;
			for (int ci = 0; ci < numCols; ++ci)
				chW += colW[ci] + (ci > 0 ? gap : 0.f);
			chH = 0.f;
			for (int ri = 0; ri < numRows; ++ri)
				chH += rowH[ri] + (ri > 0 ? gap : 0.f);
		} else if (ch) {
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw  = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				if (!cw || !cl || !Has(cw->behavior, WidgetBehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
					_Measure(cid, cc);
					continue;
				}
				const auto& cspm = _Sp(cid);
				FPoint csz = _Measure(cid, cc);
				chW = SDL::Max(chW, csz.x + cspm.margin.left + cspm.margin.right);
				chH = SDL::Max(chH, csz.y + cspm.margin.top + cspm.margin.bottom);
			}
		}

		if (w->type != WidgetType::ListBox && w->type != WidgetType::TextArea) {
			lp->contentW = chW;
			lp->contentH = chH;
		}

		float bW = wa ? SDL::Max(intr.x, chW) + sp.padding.left + sp.padding.right : fw;
		float bH = ha ? SDL::Max(intr.y, chH) + sp.padding.top  + sp.padding.bottom : fh;

		if (ha) {
			if (w->type == WidgetType::Expander) {
				if (auto *exd = m_ctx.Get<ExpanderData>(e))
					bH = exd->headerH + chH + sp.padding.bottom;
			} else if (w->type == WidgetType::TabView) {
				if (auto *tvd = m_ctx.Get<TabViewData>(e))
					bH = tvd->tabHeight + chH + sp.padding.bottom;
			}
		}

		float rMinW = lp->minWidth.Resolve(ctx);
		float rMinH = lp->minHeight.Resolve(ctx);
		float rMaxW = lp->maxWidth.Resolve(ctx);
		float rMaxH = lp->maxHeight.Resolve(ctx);
		if (rMaxW >= 0.f) bW = SDL::Min(bW, rMaxW);
		if (rMaxH >= 0.f) bH = SDL::Min(bH, rMaxH);
		if (rMinW >= 0.f) bW = SDL::Max(bW, rMinW);
		if (rMinH >= 0.f) bH = SDL::Max(bH, rMinH);

		cr->measured = {bW, bH};
		return cr->measured;
	}

	inline void LayoutSystem::_Place(ECS::EntityId e, FRect rect, const LayoutContext& ctx) {
		if (!m_ctx.IsAlive(e)) return;

		auto *w  = m_ctx.Get<Widget>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !lp || !cr) return;

		cr->absolute = rect;
		auto *ch = m_ctx.Get<Children>(e);
		if (!ch || ch->ids.empty()) return;

		const auto& sp = _Sp(e);
		const FRect &self = cr->absolute;
		float cw  = self.w - sp.padding.left - sp.padding.right;
		float topInset = sp.padding.top;

		if (w->type == WidgetType::Expander) {
			if (auto *exd = m_ctx.Get<ExpanderData>(e)) topInset = exd->headerH;
		} else if (w->type == WidgetType::TabView) {
			if (auto *tvd = m_ctx.Get<TabViewData>(e); tvd && tvd->tabLocation != TabLocation::Bottom) topInset = tvd->tabHeight;
		} else if (w->type == WidgetType::Popup) {
			if (auto *pd = m_ctx.Get<PopupConfig>(e)) topInset = pd->headerH;
		}
		float ch2 = SDL::Max(0.f, self.h - topInset - sp.padding.bottom);

		if (w->type == WidgetType::Container || w->type == WidgetType::ListBox ||
		    w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
			bool showX = false, showY = false;
			_ContainerScrollbars(*w, *lp, e, cw, ch2, showX, showY);
			float sbt = _SbThick(e);
			if (showY) cw  = SDL::Max(0.f, cw  - sbt);
			if (showX) ch2 = SDL::Max(0.f, ch2 - sbt);
		}

		if (w->type == WidgetType::Splitter) {
			auto *sc  = m_ctx.Get<SplitterConfig>(e);
			auto *ss  = m_ctx.Get<SplitterState>(e);
			if (sc && ss && ch->ids.size() >= 2) {
				bool horiz = (sc->orientation == Orientation::Horizontal);
				float ox = self.x + sp.padding.left;
				float oy = self.y + sp.padding.top;
				float first  = horiz ? cw * ss->ratio : ch2 * ss->ratio;
				float second = horiz ? cw - first - sc->handleSize : ch2 - first - sc->handleSize;
				second = SDL::Max(0.f, second);
				if (m_ctx.IsAlive(ch->ids[0])) {
					_Place(ch->ids[0], horiz ? FRect{ox, oy, first, ch2} : FRect{ox, oy, cw, first}, ctx);
				}
				if (m_ctx.IsAlive(ch->ids[1])) {
					_Place(ch->ids[1], horiz ? FRect{ox + first + sc->handleSize, oy, second, ch2} :
					                           FRect{ox, oy + first + sc->handleSize, cw, second}, ctx);
				}
			}
			return;
		}

		float cx = self.x + sp.padding.left - lp->scrollX;
		float cy = self.y + topInset - lp->scrollY;

		// Place Absolute/Fixed children first
		for (ECS::EntityId cid : ch->ids) {
			if (!m_ctx.IsAlive(cid)) continue;
			auto *cw2 = m_ctx.Get<Widget>(cid);
			auto *cl  = m_ctx.Get<LayoutProps>(cid);
			auto *cc  = m_ctx.Get<ComputedRect>(cid);
			if (!cw2 || !cl || !cc || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;

			if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
				float ox = (cl->attach == AttachLayout::Fixed) ? 0.f : self.x;
				float oy = (cl->attach == AttachLayout::Fixed) ? 0.f : self.y;
				LayoutContext absCtx = _MakeRootCtx(m_viewport);
				absCtx.parentSize = {self.w, self.h};
				absCtx.parentPadding = sp.padding;
				cc->absolute = {ox + cl->absX.Resolve(absCtx), oy + cl->absY.Resolve(absCtx), cc->measured.x, cc->measured.y};
				_Place(cid, cc->absolute, ctx);
			}
		}

		// Layout engine for flow children
		if (lp->layout == Layout::InColumn || lp->layout == Layout::InLine) {
			const bool isCol = (lp->layout == Layout::InColumn);
			auto growOfP = [&](const LayoutProps* cl) -> float {
				return 0.01f * (isCol ? (cl->height.IsGrow() ? cl->height.val : 0.f)
							  : (cl->width.IsGrow() ? cl->width.val : 0.f));
			};
			std::vector<ECS::EntityId> flowChildren;
			float tFixed = 0.f, tGrow = 0.f;

			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				auto *cc  = m_ctx.Get<ComputedRect>(cid);
				if (!cw2 || !cl || !cc || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

				const auto& cspm = _Sp(cid);
				flowChildren.push_back(cid);
				float g = growOfP(cl);
				tGrow += g;
				if (isCol) {
					tFixed += cspm.margin.top + cspm.margin.bottom;
					if (g == 0.f) tFixed += cc->measured.y;
				} else {
					tFixed += cspm.margin.left + cspm.margin.right;
					if (g == 0.f) tFixed += cc->measured.x;
				}
			}

			int vis = (int)flowChildren.size();
			float avail = (isCol) ? ch2 : cw;
			float gBudget = SDL::Max(0.f, avail - tFixed - sp.gap * SDL::Max(0, vis - 1));
			float gUnit = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

			int lastGrowIdx = -1;
			for (int i = 0; i < vis; ++i) {
				auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
				if (growOfP(cl) > 0.f) lastGrowIdx = i;
			}

			// Pre-compute sizes with grow
			std::vector<FRect> computed(vis);
			for (int i = 0; i < vis; ++i) {
				auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
				auto *cc = m_ctx.Get<ComputedRect>(flowChildren[i]);
				const auto& cspm = _Sp(flowChildren[i]);
				float childW = cc->measured.x;
				float childH = cc->measured.y;
				float g = growOfP(cl);
				if (g > 0.f) {
					if (isCol) childH = gUnit * g;
					else       childW = gUnit * g;
				}
				if (isCol) {
					if (cl->alignSelfH == Align::Stretch)
						childW = SDL::Max(0.f, cw - cspm.margin.left - cspm.margin.right);
				} else {
					if (cl->alignSelfV == Align::Stretch)
						childH = SDL::Max(0.f, ch2 - cspm.margin.top - cspm.margin.bottom);
				}
				computed[i] = {0.f, 0.f, childW, childH};
			}

			float currentX = cx;
			float currentY = cy;
			float rightX = cx + cw;
			float bottomY = cy + ch2;

			if (lastGrowIdx == -1) {
				for (int i = 0; i < vis; ++i) {
					const auto& cspm = _Sp(flowChildren[i]);
					if (isCol) {
						computed[i].y = currentY + cspm.margin.top;
						currentY += computed[i].h + cspm.margin.top + cspm.margin.bottom + sp.gap;
					} else {
						computed[i].x = currentX + cspm.margin.left;
						currentX += computed[i].w + cspm.margin.left + cspm.margin.right + sp.gap;
					}
				}
			} else {
				for (int i = vis - 1; i > lastGrowIdx; --i) {
					const auto& cspm = _Sp(flowChildren[i]);
					if (isCol) {
						bottomY -= cspm.margin.bottom;
						bottomY -= computed[i].h;
						computed[i].y = bottomY;
						bottomY -= (cspm.margin.top + sp.gap);
					} else {
						rightX -= cspm.margin.right;
						rightX -= computed[i].w;
						computed[i].x = rightX;
						rightX -= (cspm.margin.left + sp.gap);
					}
				}

				for (int i = 0; i <= lastGrowIdx; ++i) {
					const auto& cspm = _Sp(flowChildren[i]);
					if (isCol) {
						computed[i].y = currentY + cspm.margin.top;
						if (i == lastGrowIdx)
							computed[i].h = SDL::Max(0.f, bottomY - computed[i].y - cspm.margin.bottom);
						currentY += computed[i].h + cspm.margin.top + cspm.margin.bottom + sp.gap;
					} else {
						computed[i].x = currentX + cspm.margin.left;
						if (i == lastGrowIdx)
							computed[i].w = SDL::Max(0.f, rightX - computed[i].x - cspm.margin.right);
						currentX += computed[i].w + cspm.margin.left + cspm.margin.right + sp.gap;
					}
				}
			}

			// Apply secondary axis alignment
			for (int i = 0; i < vis; ++i) {
				auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
				const auto& cspm = _Sp(flowChildren[i]);

				if (isCol) {
					float px = cx + cspm.margin.left;
					switch (cl->alignSelfH) {
						case Align::Center:  px = cx + (cw - computed[i].w) * 0.5f; break;
						case Align::End:     px = cx + cw - computed[i].w - cspm.margin.right; break;
						default: break;
					}
					computed[i].x = px;
				} else {
					float py = cy + cspm.margin.top;
					switch (cl->alignSelfV) {
						case Align::Center:  py = cy + (ch2 - computed[i].h) * 0.5f; break;
						case Align::End:     py = cy + ch2 - computed[i].h - cspm.margin.bottom; break;
						default: break;
					}
					computed[i].y = py;
				}

				_Place(flowChildren[i], computed[i], ctx);
			}
		} else if (lp->layout == Layout::Stack) {
			float startX = cx;
			size_t i = 0;
			while (i < ch->ids.size()) {
				size_t j = i;
				float rowFixedW = 0.f, rowGrow = 0.f, rowMaxH = 0.f;
				int rowItems = 0;

				while (j < ch->ids.size()) {
					ECS::EntityId cid = ch->ids[j];
					if (!m_ctx.IsAlive(cid)) { j++; continue; }
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);

					if (!cw2 || !cl || !cc || !Has(cw2->behavior, WidgetBehaviorFlag::Visible) ||
					    cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
						j++; continue;
					}

					const auto& cspm = _Sp(cid);
					float itemGrow = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
					float itemFixedW = cspm.margin.left + cspm.margin.right + (itemGrow == 0.f ? cc->measured.x : 0.f);
					float itemH = cc->measured.y + cspm.margin.top + cspm.margin.bottom;

					if (rowItems > 0 && cw > 0.f && rowFixedW + sp.gap + itemFixedW > cw) {
						break;
					}

					rowFixedW += itemFixedW + (rowItems > 0 ? sp.gap : 0.f);
					rowGrow += itemGrow;
					rowMaxH = SDL::Max(rowMaxH, itemH);
					rowItems++;
					j++;
				}

				if (rowItems == 0) { i++; continue; }

				float gBudget = SDL::Max(0.f, cw - rowFixedW);
				float gUnit = (rowGrow > 0.f) ? gBudget / rowGrow : 0.f;

				bool firstInRow = true;
				for (size_t k = i; k < j; ++k) {
					ECS::EntityId cid = ch->ids[k];
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);

					if (!cw2 || !cl || !cc || !Has(cw2->behavior, WidgetBehaviorFlag::Visible) ||
					    cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					const auto& cspm = _Sp(cid);
					float childW = cc->measured.x, childH = cc->measured.y;
					float cg = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
					if (cg > 0.f) childW = gUnit * cg;

					if (!firstInRow) cx += sp.gap;
					firstInRow = false;

					float px = cx + cspm.margin.left;
					float py = cy + cspm.margin.top;

					switch (cl->alignSelfV) {
						case Align::Stretch: childH = SDL::Max(0.f, rowMaxH - cspm.margin.top - cspm.margin.bottom); [[fallthrough]];
						case Align::Start:   break;
						case Align::Center:  py = cy + (rowMaxH - childH) * 0.5f; break;
						case Align::End:     py = cy + rowMaxH - childH - cspm.margin.bottom; break;
					}

					_Place(cid, {px, py, childW, childH}, ctx);
					cx += childW + cspm.margin.left + cspm.margin.right;
				}

				cx = startX;
				cy += rowMaxH + sp.gap;
				i = j;
			}
		} else if (lp->layout == Layout::InGrid) {
			auto *gp = m_ctx.Get<LayoutGridProps>(e);
			int numCols = gp ? SDL::Max(1, gp->columns) : 2;

			int numRows = gp ? gp->rows : 0;
			if (numRows <= 0) {
				int autoIdx = 0;
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					if (!cw2 || !cl || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
					auto *gc = m_ctx.Get<GridCell>(cid);
					int r = gc ? gc->row : (autoIdx / numCols);
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					numRows = SDL::Max(numRows, r + rs);
					if (!gc) ++autoIdx;
				}
				numRows = SDL::Max(1, numRows);
			}

			float gap = sp.gap;
			const std::vector<float> *colWidths = (gp && (int)gp->colWidths.size() == numCols) ? &gp->colWidths : nullptr;
			const std::vector<float> *rowHeights = (gp && (int)gp->rowHeights.size() == numRows) ? &gp->rowHeights : nullptr;

			float uniformColW = SDL::Max(1.f, (cw - gap * (float)(numCols - 1)) / (float)numCols);
			float uniformRowH = SDL::Max(1.f, (ch2 - gap * (float)(numRows - 1)) / (float)numRows);

			std::vector<float> colX(numCols), rowY(numRows);
			float accX = 0.f;
			for (int ci = 0; ci < numCols; ++ci) {
				colX[ci] = accX;
				accX += (colWidths ? (*colWidths)[ci] : uniformColW) + gap;
			}
			float accY = 0.f;
			for (int ri = 0; ri < numRows; ++ri) {
				rowY[ri] = accY;
				accY += (rowHeights ? (*rowHeights)[ri] : uniformRowH) + gap;
			}

			int autoIdx = 0;
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				auto *cc  = m_ctx.Get<ComputedRect>(cid);
				if (!cw2 || !cl || !cc || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

				const auto& cspm = _Sp(cid);
				auto *gc = m_ctx.Get<GridCell>(cid);
				int c = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : (autoIdx % numCols);
				int r = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : (autoIdx / numCols);
				int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
				int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
				if (!gc) ++autoIdx;

				float cellW = 0.f;
				for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
					cellW += (colWidths ? (*colWidths)[ci] : uniformColW) + (ci > c ? gap : 0.f);
				float cellH = 0.f;
				for (int ri = r; ri < SDL::Min(r + rs, numRows); ++ri)
					cellH += (rowHeights ? (*rowHeights)[ri] : uniformRowH) + (ri > r ? gap : 0.f);

				float childW = cc->measured.x, childH = cc->measured.y;
				if (cl->alignSelfH == Align::Stretch)
					childW = SDL::Max(0.f, cellW - cspm.margin.left - cspm.margin.right);
				if (cl->alignSelfV == Align::Stretch)
					childH = SDL::Max(0.f, cellH - cspm.margin.top - cspm.margin.bottom);

				float px = cx + colX[c] + cspm.margin.left;
				float py = cy + rowY[r] + cspm.margin.top;

				switch (cl->alignSelfH) {
					case Align::Center: px = cx + colX[c] + (cellW - childW) * 0.5f; break;
					case Align::End: px = cx + colX[c] + cellW - childW - cspm.margin.right; break;
					default: break;
				}
				switch (cl->alignSelfV) {
					case Align::Center: py = cy + rowY[r] + (cellH - childH) * 0.5f; break;
					case Align::End: py = cy + rowY[r] + cellH - childH - cspm.margin.bottom; break;
					default: break;
				}

				_Place(cid, {px, py, childW, childH}, ctx);
			}
		}

		// Compute content bounds for Containers
		if (w->type == WidgetType::Container) {
			float maxContentX = 0.f;
			float maxContentY = 0.f;

			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cc  = m_ctx.Get<ComputedRect>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				if (!cw2 || !cc || !cl || !Has(cw2->behavior, WidgetBehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Fixed) continue;

				const auto& cspm = _Sp(cid);
				float childRelativeRight  = (cc->absolute.x + cc->absolute.w + cspm.margin.right)
				                          - (self.x + sp.padding.left) + lp->scrollX;
				float childRelativeBottom = (cc->absolute.y + cc->absolute.h + cspm.margin.bottom)
				                          - (self.y + sp.padding.top) + lp->scrollY;

				maxContentX = SDL::Max(maxContentX, childRelativeRight);
				maxContentY = SDL::Max(maxContentY, childRelativeBottom);
			}

			lp->contentW = maxContentX;
			lp->contentH = maxContentY;
		}
	}

	inline void LayoutSystem::_UpdateClips(ECS::EntityId e, FRect parentClip) {
		if (!m_ctx.IsAlive(e)) return;

		auto *w  = m_ctx.Get<Widget>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *bs = m_ctx.Get<BorderStyle>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !lp || !cr) return;

		// Portal attachment: a floating widget (popup / dropdown / tooltip) clips to
		// its anchor instead of its tree parent, so it is not cut off by an ancestor.
		FRect effParentClip = parentClip;
		if (auto* lay = m_ctx.Get<LayerProps>(e)) {
			switch (lay->attach) {
			case PopupAttach::Root:   effParentClip = m_rootClip; break;
			case PopupAttach::Window: effParentClip = m_viewport; break;
			case PopupAttach::Target:
				if (auto* tcr = m_ctx.Get<ComputedRect>(lay->attachTarget))
					effParentClip = tcr->inner_clip;
				break;
			case PopupAttach::Parent: break;
			}
		}

		cr->inner_clip = cr->absolute.GetIntersection(effParentClip);
		if (bs) cr->outer_clip = cr->inner_clip.Extend(bs->dimensions);
		else    cr->outer_clip = cr->inner_clip;

		FRect childClip = cr->inner_clip;
		const auto& sp = _Sp(e);

		if (w->type == WidgetType::Container || w->type == WidgetType::ListBox ||
		    w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
			float innerW = cr->absolute.w - sp.padding.left - sp.padding.right;
			float innerH = cr->absolute.h - sp.padding.top  - sp.padding.bottom;
			bool showX = false, showY = false;
			_ContainerScrollbars(*w, *lp, e, innerW, innerH, showX, showY);

			float sbt = _SbThick(e);
			childClip = cr->absolute;
			childClip.x += sp.padding.left;
			childClip.y += sp.padding.top;
			childClip.w = SDL::Round(innerW - (showY ? sbt : 0.f));
			childClip.h = SDL::Round(innerH - (showX ? sbt : 0.f));
			childClip = childClip.GetIntersection(parentClip);
		} else if (w->type == WidgetType::Expander) {
			if (auto *exd = m_ctx.Get<ExpanderData>(e)) {
				childClip = cr->absolute;
				float bdH = bs ? bs->dimensions.GetH() : 0.f;
				childClip.y += exd->headerH + bdH;
				childClip.h = SDL::Max(0.f, childClip.h - exd->headerH);
				childClip = childClip.GetIntersection(parentClip);
			}
		} else if (w->type == WidgetType::TabView) {
			if (auto *tvd = m_ctx.Get<TabViewData>(e)) {
				childClip = cr->absolute;
				float bdH = bs ? bs->dimensions.GetH() : 0.f;
				if (tvd->tabLocation != TabLocation::Bottom) {
					childClip.y += tvd->tabHeight + bdH;
					childClip.h = SDL::Max(0.f, childClip.h - tvd->tabHeight);
				} else {
					childClip.h = SDL::Max(0.f, childClip.h - tvd->tabHeight);
				}
				childClip = childClip.GetIntersection(parentClip);
			}
		}

		auto *ch = m_ctx.Get<Children>(e);
		if (ch) {
			for (ECS::EntityId c : ch->ids) {
				_UpdateClips(c, childClip);
			}
		}
	}

	inline void LayoutSystem::_ClearDirtyLayout(ECS::EntityId root) {
		std::function<void(ECS::EntityId)> traverse = [&](ECS::EntityId e) {
			if (!m_ctx.IsAlive(e)) return;
			auto *w = m_ctx.Get<Widget>(e);
			if (w) {
				w->dirty = DirtyFlag::None;
			}
			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				for (ECS::EntityId c : ch->ids) {
					traverse(c);
				}
			}
		};
		traverse(root);
	}

} // namespace SDL::UI
