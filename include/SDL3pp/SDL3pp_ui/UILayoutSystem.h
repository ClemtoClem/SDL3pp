#pragma once

#include "UIComponents.h"
#include "UIValue.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_rect.h"

#include <vector>
#include <functional>
#include <algorithm>

namespace SDL::UI {

	class System;  // Forward declaration

	// ==================================================================================
	// UILayoutSystem — Measure + Place pipeline
	// ==================================================================================
	//
	// Owns:
	//   - the dirty flag that controls when a recalculation runs
	//   - the flattened draw-list (built in the same pass as Place) consumed by
	//     UIRenderSystem to avoid recursive tree traversal at render time
	//
	// API:
	//   - Process(root, viewport): runs Measure + Place + draw-list build if dirty.
	//   - MarkDirty():             requests a recalculation on the next Process call.
	//   - GetDrawList():           returns the latest flattened draw list.
	//
	// ==================================================================================

	class UILayoutSystem {
	public:
		struct DrawCall {
			ECS::EntityId entity;
			FRect         clipRect;
			int           zIndex;
		};

		UILayoutSystem(ECS::Context& ctx, System& sys);

		void Process(ECS::EntityId root, FRect viewport);

		void                 MarkDirty() noexcept           { m_dirty = true; }
		void                 MarkDirty(ECS::EntityId e);  ///< Mark a specific subtree (and its ancestors) dirty.
		[[nodiscard]] bool   IsDirty()  const noexcept     { return m_dirty; }

		[[nodiscard]] const std::vector<DrawCall>& GetDrawList() const noexcept { return m_drawList; }

	private:
		ECS::Context&         m_ctx;
		System&               m_sys;
		bool                  m_dirty   = true;
		FRect                 m_viewport = {};
		std::vector<DrawCall> m_drawList;

		FPoint _Measure      (ECS::EntityId e, const LayoutContext& ctx);
		void   _Place        (ECS::EntityId e, FRect rect, const LayoutContext& ctx);
		FPoint _IntrinsicSize(ECS::EntityId e);
		void   _BuildDrawList(ECS::EntityId root);
		void   _ClearDirtyLayout(ECS::EntityId root);
		void   _UpdateClips(ECS::EntityId e, FRect parentClip);

		LayoutContext _MakeRootCtx(FRect viewport) const noexcept;
		LayoutContext _MakeChildCtx(ECS::EntityId parent, const LayoutContext& parentCtx) const noexcept;

		float _TextWidth(const std::string& text, ECS::EntityId e);
		float _TextHeight(ECS::EntityId e);
		void  _ContainerScrollbars(const Widget& w, const LayoutProps& lp, float cW, float cH, bool& outShowX, bool& outShowY) const noexcept;
	};

	// ==================================================================================
	// Implementation: UILayoutSystem
	// ==================================================================================

	inline UILayoutSystem::UILayoutSystem(ECS::Context& ctx, System& sys) : m_ctx(ctx), m_sys(sys) {}

	inline void UILayoutSystem::Process(ECS::EntityId root, FRect viewport) {
		if (!m_ctx.IsAlive(root)) return;
		m_viewport = viewport;
		if (m_dirty) {
			LayoutContext rootCtx = _MakeRootCtx(viewport);
			_Measure(root, rootCtx);
			_Place(root, viewport, rootCtx);
			_UpdateClips(root, viewport);
			_BuildDrawList(root);
			_ClearDirtyLayout(root);
			m_dirty = false;
		}
	}

	inline void UILayoutSystem::MarkDirty(ECS::EntityId e) {
		if (!m_ctx.IsAlive(e)) return;
		m_dirty = true;
		auto *parent = m_ctx.Get<Parent>(e);
		if (parent && m_ctx.IsAlive(parent->id)) {
			MarkDirty(parent->id);
		}
	}


	inline void UILayoutSystem::_ContainerScrollbars(const Widget& w, const LayoutProps& lp,
	                                                   float cW, float cH, bool& outShowX, bool& outShowY) const noexcept {
		outShowX = outShowY = false;
		if (w.type != WidgetType::Container && w.type != WidgetType::ListBox &&
		    w.type != WidgetType::TextArea && w.type != WidgetType::Tree)
			return;

		bool always_x = Has(w.behavior, BehaviorFlag::ScrollableX);
		bool always_y = Has(w.behavior, BehaviorFlag::ScrollableY);
		bool auto_x   = Has(w.behavior, BehaviorFlag::AutoScrollableX);
		bool auto_y   = Has(w.behavior, BehaviorFlag::AutoScrollableY);

		outShowX = always_x || (auto_x && lp.contentW > cW);
		outShowY = always_y || (auto_y && lp.contentH > cH);
	}

	inline LayoutContext UILayoutSystem::_MakeRootCtx(FRect viewport) const noexcept {
		LayoutContext ctx;
		ctx.windowSize = {viewport.w, viewport.h};
		ctx.rootSize = {viewport.w, viewport.h};
		ctx.rootFontSize = 14.f; // TODO: configurable
		ctx.parentSize = {viewport.w, viewport.h};
		ctx.parentPadding = {0.f, 0.f, 0.f, 0.f};
		ctx.parentFontSize = ctx.rootFontSize;
		return ctx;
	}

	inline LayoutContext UILayoutSystem::_MakeChildCtx(ECS::EntityId parent, const LayoutContext& parentCtx) const noexcept {
		LayoutContext ctx = parentCtx;
		if (!m_ctx.IsAlive(parent)) return ctx;
		auto *lp = m_ctx.Get<LayoutProps>(parent);
		if (!lp) return ctx;
		ctx.parentSize = FPoint{lp->contentW, lp->contentH};
		ctx.parentPadding = lp->padding;
		ctx.parentFontSize = parentCtx.rootFontSize; // TODO: from parent style
		return ctx;
	}

	inline FPoint UILayoutSystem::_IntrinsicSize(ECS::EntityId e) {
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
		case WidgetType::RadioButton:
			return {80.f, 24.f};
		case WidgetType::Slider:
			return {80.f, 24.f};
		case WidgetType::ScrollBar:
			return {10.f, 80.f};
		case WidgetType::Input: {
			const float arrowsW = m_ctx.Get<NumericValue>(e) ? 20.f : 0.f;
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
			auto *pd = m_ctx.Get<PopupData>(e);
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

	inline FPoint UILayoutSystem::_Measure(ECS::EntityId e, const LayoutContext& ctx) {
		if (!m_ctx.IsAlive(e)) return {};
		auto *w  = m_ctx.Get<Widget>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !lp || !cr) return {};
		if (!Has(w->behavior, BehaviorFlag::Visible)) {
			cr->measured = {};
			return {};
		}

		bool wa = lp->width.IsAuto() || lp->width.IsGrow();
		bool ha = lp->height.IsAuto() || lp->height.IsGrow();
		float fw = wa ? 0.f : lp->width.Resolve(ctx);
		float fh = ha ? 0.f : lp->height.Resolve(ctx);

		float cW = SDL::Max(0.f, (wa ? ctx.parentSize.x : fw) - lp->padding.left - lp->padding.right);
		float cH = SDL::Max(0.f, (ha ? ctx.parentSize.y : fh) - lp->padding.top  - lp->padding.bottom);

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
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
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
			_ContainerScrollbars(*w, *lp, cW, cH, showX, showY);
			if (showY) cW = SDL::Max(0.f, cW - lp->scrollbarThickness);
			if (showX) cH = SDL::Max(0.f, cH - lp->scrollbarThickness);
		}

		FPoint intr = _IntrinsicSize(e);

		LayoutContext cc;
		cc.windowSize = ctx.windowSize;
		cc.rootSize = ctx.rootSize;
		cc.rootFontSize = ctx.rootFontSize;
		cc.parentSize = {cW, cH};
		cc.parentPadding = lp->padding;
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
				if (!cw2 || !cl2 || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
				if (cl2->attach == AttachLayout::Absolute || cl2->attach == AttachLayout::Fixed) {
					_Measure(cid, cc);
					continue;
				}
				flow.push_back(cid);
				float g = growOf(cl2);
				tGrow += g;
				if (isCol) tFixed += cl2->margin.top  + cl2->margin.bottom;
				else       tFixed += cl2->margin.left + cl2->margin.right;
				if (g == 0.f) {
					FPoint csz = _Measure(cid, cc);
					if (isCol) {
						chW = SDL::Max(chW, csz.x + cl2->margin.left + cl2->margin.right);
						tFixed += csz.y;
					} else {
						chH = SDL::Max(chH, csz.y + cl2->margin.top + cl2->margin.bottom);
						tFixed += csz.x;
					}
				}
			}

			int fvis    = (int)flow.size();
			float avail   = isCol ? cH : cW;
			float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, fvis - 1));
			float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

			for (int i = 0; i < fvis; ++i) {
				ECS::EntityId cid = flow[i];
				auto *cl2  = m_ctx.Get<LayoutProps>(cid);
				auto *ccr2 = m_ctx.Get<ComputedRect>(cid);
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

				float mw = ccr2->measured.x + cl2->margin.left + cl2->margin.right;
				float mh = ccr2->measured.y + cl2->margin.top  + cl2->margin.bottom;
				if (isCol) {
					chW  = SDL::Max(chW, mw);
					chH += mh + (i > 0 ? lp->gap : 0.f);
				} else {
					chH  = SDL::Max(chH, mh);
					chW += mw + (i > 0 ? lp->gap : 0.f);
				}
			}
			vis = fvis;

		} else if (ch && lp->layout == Layout::Stack) {
			int lineVis = 0;
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue;
				auto *cw  = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				if (!cw || !cl || !Has(cw->behavior, BehaviorFlag::Visible)) continue;

				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
					_Measure(cid, cc);
					continue;
				}

				FPoint csz = _Measure(cid, cc);
				float mw = csz.x + cl->margin.left + cl->margin.right;
				float mh = csz.y + cl->margin.top  + cl->margin.bottom;

				float flowW = mw;
				if (lineVis > 0 && cW > 0.f && curLineW + lp->gap + flowW > cW) {
					chW = SDL::Max(chW, curLineW);
					chH += curLineH + lp->gap;
					curLineW = flowW;
					curLineH = mh;
					lineVis = 1;
				} else {
					curLineW += flowW + (lineVis > 0 ? lp->gap : 0.f);
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
			auto *gp     = m_ctx.Get<LayoutGridProps>(e);
			int numCols  = gp ? SDL::Max(1, gp->columns) : 2;
			float gap    = lp->gap;

			int numRows  = gp ? gp->rows : 0;
			if (numRows <= 0) {
				int autoIdx = 0;
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					if (!cw2 || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
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
					if (!cw2 || !cl || !ccr || !Has(cw2->behavior, BehaviorFlag::Visible)) { if (!gc) ++autoIdx; continue; }
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					int c  = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : ((autoIdx % numCols));
					int r  = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : ((autoIdx / numCols));
					int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					if (!gc) ++autoIdx;

					if (cSiz == GridSizing::Content) {
						float childW = ccr->measured.x + cl->margin.left + cl->margin.right;
						float perCol = SDL::Max(0.f, (childW - gap * (float)(cs - 1)) / (float)cs);
						for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
							colW[ci] = SDL::Max(colW[ci], perCol);
					}
					if (rSiz == GridSizing::Content || baseCellH == 0.f) {
						float childH = ccr->measured.y + cl->margin.top + cl->margin.bottom;
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
				if (!cw || !cl || !Has(cw->behavior, BehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
					_Measure(cid, cc);
					continue;
				}
				FPoint csz = _Measure(cid, cc);
				chW = SDL::Max(chW, csz.x + cl->margin.left + cl->margin.right);
				chH = SDL::Max(chH, csz.y + cl->margin.top + cl->margin.bottom);
			}
		}

		if (w->type != WidgetType::ListBox && w->type != WidgetType::TextArea) {
			lp->contentW = chW;
			lp->contentH = chH;
		}

		float bW = wa ? SDL::Max(intr.x, chW) + lp->padding.left + lp->padding.right : fw;
		float bH = ha ? SDL::Max(intr.y, chH) + lp->padding.top  + lp->padding.bottom : fh;

		if (ha) {
			if (w->type == WidgetType::Expander) {
				if (auto *exd = m_ctx.Get<ExpanderData>(e))
					bH = exd->headerH + chH + lp->padding.bottom;
			} else if (w->type == WidgetType::TabView) {
				if (auto *tvd = m_ctx.Get<TabViewData>(e))
					bH = tvd->tabHeight + chH + lp->padding.bottom;
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

	inline void UILayoutSystem::_Place(ECS::EntityId e, FRect rect, const LayoutContext& ctx) {
		if (!m_ctx.IsAlive(e)) return;

		auto *w  = m_ctx.Get<Widget>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !lp || !cr) return;

		cr->screen = rect;
		auto *ch = m_ctx.Get<Children>(e);
		if (!ch || ch->ids.empty()) return;

		const FRect &self = cr->screen;
		float cw  = self.w - lp->padding.left - lp->padding.right;
		float topInset = lp->padding.top;

		if (w->type == WidgetType::Expander) {
			if (auto *exd = m_ctx.Get<ExpanderData>(e)) topInset = exd->headerH;
		} else if (w->type == WidgetType::TabView) {
			if (auto *tvd = m_ctx.Get<TabViewData>(e); tvd && !tvd->tabsBottom) topInset = tvd->tabHeight;
		} else if (w->type == WidgetType::Popup) {
			if (auto *pd = m_ctx.Get<PopupData>(e)) topInset = pd->headerH;
		}
		float ch2 = SDL::Max(0.f, self.h - topInset - lp->padding.bottom);

		if (w->type == WidgetType::Container || w->type == WidgetType::ListBox ||
		    w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
			bool showX = false, showY = false;
			_ContainerScrollbars(*w, *lp, cw, ch2, showX, showY);
			if (showY) cw  = SDL::Max(0.f, cw  - lp->scrollbarThickness);
			if (showX) ch2 = SDL::Max(0.f, ch2 - lp->scrollbarThickness);
		}

		if (w->type == WidgetType::Splitter) {
			auto *spl = m_ctx.Get<SplitterData>(e);
			if (spl && ch->ids.size() >= 2) {
				bool horiz = (spl->orientation == Orientation::Horizontal);
				float ox = self.x + lp->padding.left;
				float oy = self.y + lp->padding.top;
				float first  = horiz ? cw * spl->ratio : ch2 * spl->ratio;
				float second = horiz ? cw - first - spl->handleSize : ch2 - first - spl->handleSize;
				second = SDL::Max(0.f, second);
				if (m_ctx.IsAlive(ch->ids[0])) {
					auto *cc0 = m_ctx.Get<ComputedRect>(ch->ids[0]);
					if (cc0) {
						_Place(ch->ids[0], horiz ? FRect{ox, oy, first, ch2} : FRect{ox, oy, cw, first}, ctx);
					}
				}
				if (m_ctx.IsAlive(ch->ids[1])) {
					auto *cc1 = m_ctx.Get<ComputedRect>(ch->ids[1]);
					if (cc1) {
						_Place(ch->ids[1], horiz ? FRect{ox + first + spl->handleSize, oy, second, ch2} :
						                           FRect{ox, oy + first + spl->handleSize, cw, second}, ctx);
					}
				}
			}
			return;
		}

		float cx = self.x + lp->padding.left - lp->scrollX;
		float cy = self.y + topInset - lp->scrollY;

		// Place Absolute/Fixed children first
		for (ECS::EntityId cid : ch->ids) {
			if (!m_ctx.IsAlive(cid)) continue;
			auto *cw2 = m_ctx.Get<Widget>(cid);
			auto *cl  = m_ctx.Get<LayoutProps>(cid);
			auto *cc  = m_ctx.Get<ComputedRect>(cid);
			if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;

			if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
				float ox = (cl->attach == AttachLayout::Fixed) ? 0.f : self.x;
				float oy = (cl->attach == AttachLayout::Fixed) ? 0.f : self.y;
				LayoutContext absCtx = _MakeRootCtx(m_viewport);
				absCtx.parentSize = {self.w, self.h};
				absCtx.parentPadding = lp->padding;
				cc->screen = {ox + cl->absX.Resolve(absCtx), oy + cl->absY.Resolve(absCtx), cc->measured.x, cc->measured.y};
				_Place(cid, cc->screen, ctx);
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
				if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

				flowChildren.push_back(cid);
				float g = growOfP(cl);
				tGrow += g;
				if (isCol) {
					tFixed += cl->margin.top + cl->margin.bottom;
					if (g == 0.f) tFixed += cc->measured.y;
				} else {
					tFixed += cl->margin.left + cl->margin.right;
					if (g == 0.f) tFixed += cc->measured.x;
				}
			}

			int vis = (int)flowChildren.size();
			float avail = (isCol) ? ch2 : cw;
			float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, vis - 1));
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
				float childW = cc->measured.x;
				float childH = cc->measured.y;
				float g = growOfP(cl);
				if (g > 0.f) {
					if (isCol) childH = gUnit * g;
					else       childW = gUnit * g;
				}
				if (isCol) {
					if (cl->alignSelfH == Align::Stretch)
						childW = SDL::Max(0.f, cw - cl->margin.left - cl->margin.right);
				} else {
					if (cl->alignSelfV == Align::Stretch)
						childH = SDL::Max(0.f, ch2 - cl->margin.top - cl->margin.bottom);
				}
				computed[i] = {0.f, 0.f, childW, childH};
			}

			float currentX = cx;
			float currentY = cy;
			float rightX = cx + cw;
			float bottomY = cy + ch2;

			if (lastGrowIdx == -1) {
				for (int i = 0; i < vis; ++i) {
					auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
					if (isCol) {
						computed[i].y = currentY + cl->margin.top;
						currentY += computed[i].h + cl->margin.top + cl->margin.bottom + lp->gap;
					} else {
						computed[i].x = currentX + cl->margin.left;
						currentX += computed[i].w + cl->margin.left + cl->margin.right + lp->gap;
					}
				}
			} else {
				for (int i = vis - 1; i > lastGrowIdx; --i) {
					auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
					if (isCol) {
						bottomY -= cl->margin.bottom;
						bottomY -= computed[i].h;
						computed[i].y = bottomY;
						bottomY -= (cl->margin.top + lp->gap);
					} else {
						rightX -= cl->margin.right;
						rightX -= computed[i].w;
						computed[i].x = rightX;
						rightX -= (cl->margin.left + lp->gap);
					}
				}

				for (int i = 0; i <= lastGrowIdx; ++i) {
					auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
					if (isCol) {
						computed[i].y = currentY + cl->margin.top;
						if (i == lastGrowIdx)
							computed[i].h = SDL::Max(0.f, bottomY - computed[i].y - cl->margin.bottom);
						currentY += computed[i].h + cl->margin.top + cl->margin.bottom + lp->gap;
					} else {
						computed[i].x = currentX + cl->margin.left;
						if (i == lastGrowIdx)
							computed[i].w = SDL::Max(0.f, rightX - computed[i].x - cl->margin.right);
						currentX += computed[i].w + cl->margin.left + cl->margin.right + lp->gap;
					}
				}
			}

			// Apply secondary axis alignment
			for (int i = 0; i < vis; ++i) {
				auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
				auto *cc = m_ctx.Get<ComputedRect>(flowChildren[i]);

				if (isCol) {
					float px = cx + cl->margin.left;
					switch (cl->alignSelfH) {
						case Align::Center:  px = cx + (cw - computed[i].w) * 0.5f; break;
						case Align::End:     px = cx + cw - computed[i].w - cl->margin.right; break;
						default: break;
					}
					computed[i].x = px;
				} else {
					float py = cy + cl->margin.top;
					switch (cl->alignSelfV) {
						case Align::Center:  py = cy + (ch2 - computed[i].h) * 0.5f; break;
						case Align::End:     py = cy + ch2 - computed[i].h - cl->margin.bottom; break;
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

					if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) ||
					    cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
						j++; continue;
					}

					float itemGrow = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
					float itemFixedW = cl->margin.left + cl->margin.right + (itemGrow == 0.f ? cc->measured.x : 0.f);
					float itemH = cc->measured.y + cl->margin.top + cl->margin.bottom;

					if (rowItems > 0 && cw > 0.f && rowFixedW + lp->gap + itemFixedW > cw) {
						break;
					}

					rowFixedW += itemFixedW + (rowItems > 0 ? lp->gap : 0.f);
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

					if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) ||
					    cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					float childW = cc->measured.x, childH = cc->measured.y;
					float cg = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
					if (cg > 0.f) childW = gUnit * cg;

					if (!firstInRow) cx += lp->gap;
					firstInRow = false;

					float px = cx + cl->margin.left;
					float py = cy + cl->margin.top;

					switch (cl->alignSelfV) {
						case Align::Stretch: childH = SDL::Max(0.f, rowMaxH - cl->margin.top - cl->margin.bottom); [[fallthrough]];
						case Align::Start:   break;
						case Align::Center:  py = cy + (rowMaxH - childH) * 0.5f; break;
						case Align::End:     py = cy + rowMaxH - childH - cl->margin.bottom; break;
					}

					_Place(cid, {px, py, childW, childH}, ctx);
					cx += childW + cl->margin.left + cl->margin.right;
				}

				cx = startX;
				cy += rowMaxH + lp->gap;
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
					if (!cw2 || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
					auto *gc = m_ctx.Get<GridCell>(cid);
					int r = gc ? gc->row : (autoIdx / numCols);
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					numRows = SDL::Max(numRows, r + rs);
					if (!gc) ++autoIdx;
				}
				numRows = SDL::Max(1, numRows);
			}

			float gap = lp->gap;
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
				if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

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
					childW = SDL::Max(0.f, cellW - cl->margin.left - cl->margin.right);
				if (cl->alignSelfV == Align::Stretch)
					childH = SDL::Max(0.f, cellH - cl->margin.top - cl->margin.bottom);

				float px = cx + colX[c] + cl->margin.left;
				float py = cy + rowY[r] + cl->margin.top;

				switch (cl->alignSelfH) {
					case Align::Center: px = cx + colX[c] + (cellW - childW) * 0.5f; break;
					case Align::End: px = cx + colX[c] + cellW - childW - cl->margin.right; break;
					default: break;
				}
				switch (cl->alignSelfV) {
					case Align::Center: py = cy + rowY[r] + (cellH - childH) * 0.5f; break;
					case Align::End: py = cy + rowY[r] + cellH - childH - cl->margin.bottom; break;
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
				if (!cw2 || !cc || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
				if (cl->attach == AttachLayout::Fixed) continue;

				float childRelativeRight = (cc->screen.x + cc->screen.w + cl->margin.right) - (self.x + lp->padding.left) + lp->scrollX;
				float childRelativeBottom = (cc->screen.y + cc->screen.h + cl->margin.bottom) - (self.y + lp->padding.top) + lp->scrollY;

				maxContentX = SDL::Max(maxContentX, childRelativeRight);
				maxContentY = SDL::Max(maxContentY, childRelativeBottom);
			}

			lp->contentW = maxContentX;
			lp->contentH = maxContentY;
		}
	}

	inline void UILayoutSystem::_UpdateClips(ECS::EntityId e, FRect parentClip) {
		if (!m_ctx.IsAlive(e)) return;

		auto *w = m_ctx.Get<Widget>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !lp || !cr) return;

		cr->clip = cr->screen.GetIntersection(parentClip);

		FRect childClip = cr->clip;

		if (w->type == WidgetType::Container || w->type == WidgetType::ListBox ||
		    w->type == WidgetType::TextArea || w->type == WidgetType::Tree) {
			float innerW = cr->screen.w - lp->padding.left - lp->padding.right;
			float innerH = cr->screen.h - lp->padding.top - lp->padding.bottom;
			bool showX = false, showY = false;
			_ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);

			childClip = cr->screen;
			childClip.x += lp->padding.left;
			childClip.y += lp->padding.top;
			childClip.w = SDL::Round(innerW - (showY ? lp->scrollbarThickness : 0.f));
			childClip.h = SDL::Round(innerH - (showX ? lp->scrollbarThickness : 0.f));
			childClip = childClip.GetIntersection(parentClip);
		} else if (w->type == WidgetType::Expander) {
			if (auto *exd = m_ctx.Get<ExpanderData>(e)) {
				childClip = cr->screen;
				childClip.y += exd->headerH;
				childClip.h = SDL::Max(0.f, childClip.h - exd->headerH);
				childClip = childClip.GetIntersection(parentClip);
			}
		} else if (w->type == WidgetType::TabView) {
			if (auto *tvd = m_ctx.Get<TabViewData>(e)) {
				childClip = cr->screen;
				if (!tvd->tabsBottom) {
					childClip.y += tvd->tabHeight;
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

	inline void UILayoutSystem::_BuildDrawList(ECS::EntityId root) {
		m_drawList.clear();
		std::function<void(ECS::EntityId, int)> traverse = [&](ECS::EntityId e, int z) {
			if (!m_ctx.IsAlive(e)) return;
			auto *w = m_ctx.Get<Widget>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !cr || !Has(w->behavior, BehaviorFlag::Visible)) return;

			m_drawList.push_back({e, cr->clip, z});

			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				for (size_t i = 0; i < ch->ids.size(); ++i) {
					traverse(ch->ids[i], z + 1);
				}
			}
		};
		traverse(root, 0);
	}

	inline void UILayoutSystem::_ClearDirtyLayout(ECS::EntityId root) {
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
