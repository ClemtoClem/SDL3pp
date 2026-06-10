#pragma once

#include "UIComponents.h"
#include "../SDL3pp_engine/ECS.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_stdinc.h"

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace SDL::UI {

	class Context;

	// ==================================================================================
	// Factory — single point of widget construction
	//
	// _Spawn() attaches the universal bundle:
	//   Widget (tag+type+behavior+states+dirty), LayoutProps, Callbacks, ComputedRect, Children, Parent.
	//
	// Each Make* method opts in to the relevant style components via _Attach* helpers
	// and creates composite sub-entities where needed:
	//
	//   ComboBox   → toggle button + overlay container (with item buttons)
	//   Expander   → header button + content container
	//   TabView    → tab-bar row container + content area container
	//   ListBox    → scroll view container + item buttons (rebuilt on SetItems)
	//   Tree       → scroll view container + node row entities (rebuilt on AddNode/Clear)
	//   MenuBar    → menu-title button row + overlay container (rebuilt on AddMenu)
	//   Tooltip    → container + label (created via EnsureTooltip)
	// ==================================================================================

	class Factory {
	public:
		Factory(Context& sys, ECS::Context& ctx) : m_sys(sys), m_ctx(ctx) {}

		// ── Hierarchy management ─────────────────────────────────────────────
		void AppendChild(ECS::EntityId parent, ECS::EntityId child) {
			if (parent == ECS::NullEntity || child == ECS::NullEntity) return;
			if (!m_ctx.IsAlive(parent) || !m_ctx.IsAlive(child)) return;
			auto* ch = m_ctx.Get<Children>(parent);
			auto* p  = m_ctx.Get<Parent>(child);
			if (!ch || !p) return;
			if (p->id != ECS::NullEntity && p->id != parent) {
				if (auto* prev = m_ctx.Get<Children>(p->id)) prev->Remove(child);
			}
			for (auto id : ch->ids) if (id == child) return;
			ch->Add(child);
			p->id = parent;
		}

		void RemoveChild(ECS::EntityId parent, ECS::EntityId child) {
			if (parent == ECS::NullEntity || child == ECS::NullEntity) return;
			if (auto* ch = m_ctx.Get<Children>(parent)) ch->Remove(child);
			if (m_ctx.IsAlive(child))
				if (auto* p = m_ctx.Get<Parent>(child)) p->id = ECS::NullEntity;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Tooltip — creates a hidden container + label as a Fixed sub-entity
		// ──────────────────────────────────────────────────────────────────────

		/// @brief Create (or update) the tooltip sub-entity for @p owner.
		///        The container is Fixed-layout, hidden by default, and carries
		///        a Label child for the tooltip text.
		void EnsureTooltip(ECS::EntityId owner, std::string_view text, float delay) {
			auto* td = m_ctx.Get<TooltipData>(owner);
			if (!td) {
				td = &m_ctx.Add<TooltipData>(owner);
				if (!m_ctx.Has<TooltipStyle>(owner)) m_ctx.Add<TooltipStyle>(owner);
			}
			td->text  = std::string(text);
			td->delay = delay;
			if (td->entity == ECS::NullEntity) {
				ECS::EntityId cont = _Spawn(WidgetType::Container);
				_AttachVisual(cont);
				auto& lp = *m_ctx.Get<LayoutProps>(cont);
				lp.attach = AttachLayout::Fixed;
				// Tooltip plane: above everything except modals, clipped to the root.
				m_ctx.Add<LayerProps>(cont, LayerProps{
					.layer = UILayer::Tooltip, .inherit = false, .attach = PopupAttach::Root});
				auto& w   = *m_ctx.Get<Widget>(cont);
				w.behavior &= ~WidgetBehaviorFlag::Visible; // hidden until triggered
				w.behavior &= ~WidgetBehaviorFlag::DispatchEvent;
				auto& sp = *m_ctx.Get<SpacingStyle>(cont);
				sp.padding = {8.f, 4.f, 8.f, 4.f};

				ECS::EntityId lbl = MakeLabel(text);
				AppendChild(cont, lbl);

				td->entity      = cont;
				td->labelEntity = lbl;
			} else {
				if (!text.empty() && td->labelEntity != ECS::NullEntity) {
					if (auto* te = m_ctx.Get<TextEdit>(td->labelEntity))
						te->text = std::string(text);
				}
			}
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — base widgets
		// ──────────────────────────────────────────────────────────────────────

		void SetTag(ECS::EntityId e, std::string_view tag) {
			if (auto* t = m_ctx.Get<Tag>(e)) t->value = std::string(tag);
			else m_ctx.Add<Tag>(e, Tag{std::string(tag)});
		}

		ECS::EntityId MakeContainer() {
			ECS::EntityId e = _Spawn(WidgetType::Container, _AutoScrollable());
			_AttachVisual(e);
			m_ctx.Add<ScrollThumbInteraction>(e);
			return e;
		}

		ECS::EntityId MakeLabel(std::string_view text) {
			ECS::EntityId e = _Spawn(WidgetType::Label);
			_AttachText(e);
			_AttachSpacing(e);
			_AttachTransform(e);
			auto& te = m_ctx.Add<TextEdit>(e);
			te.text = std::string(text);
			m_ctx.Add<TextSpans>(e); // span support enabled on all labels
			auto& sp = *m_ctx.Get<SpacingStyle>(e);
			sp.padding.top = sp.padding.bottom = 2.f;
			sp.padding.left = sp.padding.right = 4.f;
			return e;
		}

		ECS::EntityId MakeButton(std::string_view text) {
			ECS::EntityId e = _Spawn(WidgetType::Button, _Interactive());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			_AttachAnimation(e, AnimChannel::Color);
			m_ctx.Add<TextEdit>(e).text = std::string(text);
			return e;
		}

		ECS::EntityId MakeToggle(std::string_view text) {
			ECS::EntityId e = _Spawn(WidgetType::Toggle, _Interactive());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Add<TextEdit>(e).text = std::string(text);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(28.f);
			m_ctx.Add<ToggleState>(e);
			m_ctx.Add<ToggleAnim>(e);
			_AttachAnimation(e, AnimChannel::Color | AnimChannel::Toggle);
			return e;
		}

		ECS::EntityId MakeRadio(std::string_view group, std::string_view text) {
			ECS::EntityId e = _Spawn(WidgetType::Radio, _Interactive());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Add<TextEdit>(e).text = std::string(text);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(24.f);
			m_ctx.Add<RadioState>(e, RadioState{std::string(group), false});
			return e;
		}

		ECS::EntityId MakeSeparator() {
			ECS::EntityId e = _Spawn(WidgetType::Separator);
			_AttachBorder(e);
			_AttachSpacing(e);
			_AttachTransform(e);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(1.f);
			auto& sp = *m_ctx.Get<SpacingStyle>(e);
			sp.margin.top = sp.margin.bottom = 6.f;
			sp.padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Value-bearing widgets (Slider / Knob / Progress / ScrollBar)
		// ──────────────────────────────────────────────────────────────────────

		template <typename T = float>
		ECS::EntityId MakeSlider(NumericValue<T> v = {},
		                         float thickness = 12.f, Orientation o = Orientation::Horizontal) {
			ECS::EntityId e = _Spawn(WidgetType::Slider, _Interactive());
			_AttachVisual(e);
			_AttachAccent(e);
			_AttachSound(e);
			m_ctx.Add<NumericValue<T>>(e, std::move(v));
			SliderConfig sc; sc.orientation = o;
			m_ctx.Add<SliderConfig>(e, sc);
			m_ctx.Add<SliderInteraction>(e);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Horizontal) lp.height = Value::Px(thickness);
			else                              lp.width  = Value::Px(thickness);
			return e;
		}

		template <typename T = float>
		ECS::EntityId MakeProgress(NumericValue<T> v = {},
		                           float thickness = 12.f, Orientation o = Orientation::Horizontal) {
			ECS::EntityId e = _Spawn(WidgetType::Progress);
			_AttachVisual(e);
			_AttachAccent(e);
			m_ctx.Add<NumericValue<T>>(e, std::move(v));
			ProgressData pd; pd.orientation = o;
			m_ctx.Add<ProgressData>(e, pd);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Horizontal) lp.height = Value::Px(thickness);
			else                              lp.width  = Value::Px(thickness);
			return e;
		}

		template <typename T = float>
		ECS::EntityId MakeKnob(NumericValue<T> v = {},
		                       float size = 80.f, KnobShape shape = KnobShape::Potentiometer) {
			ECS::EntityId e = _Spawn(WidgetType::Knob, _Interactive());
			_AttachVisual(e);
			_AttachAccent(e);
			_AttachSound(e);
			m_ctx.Add<NumericValue<T>>(e, std::move(v));
			KnobConfig kc; kc.shape = shape;
			m_ctx.Add<KnobConfig>(e, kc);
			m_ctx.Add<KnobInteraction>(e);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			lp.width = lp.height = Value::Px(size);
			return e;
		}

		ECS::EntityId MakeScrollBar(float contentSize, float viewSize,
		                            float thickness = 12.f, Orientation o = Orientation::Vertical) {
			ECS::EntityId e = _Spawn(WidgetType::ScrollBar, _Interactive());
			_AttachBackground(e);
			_AttachAccent(e);
			_AttachTransform(e);
			ScrollBarConfig sc; sc.orientation = o;
			m_ctx.Add<ScrollBarConfig>(e, sc);
			ScrollBarState ss; ss.contentSize = contentSize; ss.viewSize = viewSize;
			m_ctx.Add<ScrollBarState>(e, ss);
			m_ctx.Add<ScrollBarInteraction>(e);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Vertical) lp.width  = Value::Px(thickness);
			else                            lp.height = Value::Px(thickness);
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Text input
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeInput(InputFilterType filter = InputFilterType::Text,
		                        std::string_view placeholder = "") {
			ECS::EntityId e = _Spawn(WidgetType::Input, _Interactive());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			InputData in{}; in.filter = filter;
			m_ctx.Add<InputData>(e, in);
			auto& te = m_ctx.Add<TextEdit>(e);
			te.placeholder = std::string(placeholder);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(30.f);
			return e;
		}

		ECS::EntityId MakeInputFiltered(InputFilterType filter,
		                                std::string_view placeholder = "") {
			return MakeInput(filter, placeholder);
		}

		template <typename T = float>
		ECS::EntityId MakeInputValue(NumericValue<T> v = {},
		                             std::string_view placeholder = "") {
			ECS::EntityId e = MakeInput(std::is_floating_point_v<T>
			                                ? InputFilterType::Float
			                                : InputFilterType::Integer,
			                            placeholder);
			auto& nv = m_ctx.Add<NumericValue<T>>(e, std::move(v));
			m_ctx.Get<TextEdit>(e)->text = FormatNumeric(nv);
			return e;
		}

		ECS::EntityId MakeTextArea(std::string_view text        = "",
		                           std::string_view placeholder = "") {
			ECS::EntityId e = _Spawn(WidgetType::TextArea, _Interactive() | _AutoScrollable());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			auto& te = m_ctx.Add<TextEdit>(e);
			te.text        = std::string(text);
			te.placeholder = std::string(placeholder);
			m_ctx.Add<TextSelection>(e);
			m_ctx.Add<TextSpans>(e);
			m_ctx.Add<TextAreaData>(e);
			m_ctx.Add<ScrollThumbInteraction>(e);
			m_ctx.Get<SpacingStyle>(e)->padding = {6.f, 6.f, 6.f, 6.f};
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Visuals (Image / Canvas)
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeImage(std::string_view key,
		                        ImageFit fit = ImageFit::None) {
			ECS::EntityId e = _Spawn(WidgetType::Image);
			_AttachSpacing(e);
			_AttachTransform(e);
			m_ctx.Add<ImageData>(e, ImageData{std::string(key), fit});
			return e;
		}

		ECS::EntityId MakeCanvas(std::function<void(SDL::Event&)>        eventCb,
		                         std::function<void(float)>              updateCb,
		                         std::function<void(RendererRef, FRect)> renderCb) {
			ECS::EntityId e = _Spawn(WidgetType::Canvas, _Interactive());
			_AttachSpacing(e);
			_AttachTransform(e);
			m_ctx.Add<CanvasData>(e, CanvasData{std::move(eventCb), std::move(updateCb), std::move(renderCb)});
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// ListBox — scroll view + item button entities
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeListBox(std::vector<std::string> items) {
			ECS::EntityId e = _Spawn(WidgetType::ListBox, _Interactive() | _AutoScrollable());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Add<ScrollThumbInteraction>(e);
			m_ctx.Get<SpacingStyle>(e)->padding = {2.f, 2.f, 2.f, 2.f};

			auto& lbd = m_ctx.Add<ListBoxData>(e);

			// Inner scroll container
			ECS::EntityId sv = _SpawnContainer(_AutoScrollable());
			m_ctx.Get<LayoutProps>(sv)->layout = Layout::InColumn;
			m_ctx.Get<SpacingStyle>(sv)->gap   = 0.f;
			m_ctx.Get<SpacingStyle>(sv)->padding = {0.f, 0.f, 0.f, 0.f};
			AppendChild(e, sv);
			lbd.scrollView = sv;

			// Item buttons
			_RebuildListBoxItems(e, std::move(items));
			return e;
		}

		/// @brief Rebuild item button entities when the item list changes.
		void RebuildListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
			auto* lbd = m_ctx.Get<ListBoxData>(e);
			if (!lbd) return;
			// Remove previous item buttons
			for (auto eid : lbd->itemButtons) {
				if (m_ctx.IsAlive(eid)) {
					RemoveChild(lbd->scrollView, eid);
					m_ctx.DestroyEntity(eid);
				}
			}
			lbd->itemButtons.clear();
			_RebuildListBoxItems(e, std::move(items));
		}

		// ──────────────────────────────────────────────────────────────────────
		// ComboBox — toggle button + overlay container with item buttons
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeComboBox(std::vector<std::string> items,
		                           int selectedIndex = 0) {
			ECS::EntityId e = _Spawn(WidgetType::ComboBox, _Interactive());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Get<LayoutProps>(e)->layout = Layout::InLine;
			m_ctx.Get<SpacingStyle>(e)->padding = {0.f, 0.f, 0.f, 0.f};

			auto& cbd = m_ctx.Add<ComboBoxData>(e);

			// Toggle button showing the current selection
			std::string initLabel = (!items.empty() && selectedIndex >= 0 && selectedIndex < (int)items.size())
			                        ? items[selectedIndex] : "";
			ECS::EntityId btn = MakeButton(initLabel);
			m_ctx.Get<LayoutProps>(btn)->width  = Value::Grow(100.f);
			m_ctx.Get<SpacingStyle>(btn)->padding = {8.f, 4.f, 8.f, 4.f};
			AppendChild(e, btn);
			cbd.toggleButton = btn;

			// Overlay container (Fixed, hidden by default)
			ECS::EntityId ov = _Spawn(WidgetType::Container, _AutoScrollable());
			_AttachVisual(ov);
			m_ctx.Add<ScrollThumbInteraction>(ov);
			auto& ovLp = *m_ctx.Get<LayoutProps>(ov);
			ovLp.attach = AttachLayout::Fixed;
			ovLp.layout = Layout::InColumn;
			ovLp.width  = Value::Px(200.f);
			auto& ovW = *m_ctx.Get<Widget>(ov);
			ovW.behavior &= ~WidgetBehaviorFlag::Visible;
			m_ctx.Get<SpacingStyle>(ov)->padding = {2.f, 2.f, 2.f, 2.f};
			m_ctx.Get<SpacingStyle>(ov)->gap     = 0.f;
			cbd.overlay = ov;

			// Populate item buttons inside overlay
			_RebuildComboBoxItems(e, std::move(items), selectedIndex);
			return e;
		}

		/// @brief Rebuild item buttons in the overlay when items change.
		void RebuildComboBoxItems(ECS::EntityId e,
		                          std::vector<std::string> items,
		                          int selectedIndex) {
			auto* cbd = m_ctx.Get<ComboBoxData>(e);
			if (!cbd || cbd->overlay == ECS::NullEntity) return;
			auto* ch = m_ctx.Get<Children>(cbd->overlay);
			if (ch) {
				for (auto eid : ch->ids)
					if (m_ctx.IsAlive(eid)) m_ctx.DestroyEntity(eid);
				ch->ids.clear();
			}
			_RebuildComboBoxItems(e, std::move(items), selectedIndex);
		}

		// ──────────────────────────────────────────────────────────────────────
		// Tree — scroll view + node row entities (button per node)
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeTree() {
			ECS::EntityId e = _Spawn(WidgetType::Tree, _Interactive() | _AutoScrollable());
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Add<ScrollThumbInteraction>(e);
			m_ctx.Get<SpacingStyle>(e)->padding = {2.f, 2.f, 2.f, 2.f};

			auto& td = m_ctx.Add<TreeData>(e);

			ECS::EntityId sv = _SpawnContainer(_AutoScrollable());
			m_ctx.Get<LayoutProps>(sv)->layout   = Layout::InColumn;
			m_ctx.Get<SpacingStyle>(sv)->gap     = 0.f;
			m_ctx.Get<SpacingStyle>(sv)->padding = {0.f, 0.f, 0.f, 0.f};
			AppendChild(e, sv);
			td.scrollView = sv;
			return e;
		}

		/// @brief Add a node row entity to the tree. Called by Context::AddTreeNode.
		void AddTreeNodeRow(ECS::EntityId e, const TreeNodeData& node, int index) {
			auto* td = m_ctx.Get<TreeData>(e);
			if (!td || td->scrollView == ECS::NullEntity) return;

			std::string rowTag = "@tree_row_" + std::to_string(index);
			ECS::EntityId row = MakeButton(node.label);
			SetTag(row, rowTag);
			auto& lp = *m_ctx.Get<LayoutProps>(row);
			lp.width   = Value::Grow(100.f);
			lp.height  = Value::Px(td->itemHeight);
			// Store indent as padding-left
			auto& sp = *m_ctx.Get<SpacingStyle>(row);
			sp.padding.left = td->indentSize * (float)node.level + 4.f;
			sp.padding.top  = sp.padding.bottom = 2.f;
			if (auto* ts = m_ctx.Get<TextStyle>(row)) ts->alignH = TextHAlign::Left;

			AppendChild(td->scrollView, row);
			td->nodeRows.push_back(row);
		}

		void ClearTreeRows(ECS::EntityId e) {
			auto* td = m_ctx.Get<TreeData>(e);
			if (!td) return;
			for (auto eid : td->nodeRows) {
				if (m_ctx.IsAlive(eid)) {
					RemoveChild(td->scrollView, eid);
					m_ctx.DestroyEntity(eid);
				}
			}
			td->nodeRows.clear();
		}

		// ──────────────────────────────────────────────────────────────────────
		// TabView — tab-bar row + content area + per-tab button/content pairs
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeTabView() {
			// Outer container
			ECS::EntityId e = _Spawn(WidgetType::TabView, _Interactive());
			_AttachVisual(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Get<LayoutProps>(e)->layout = Layout::InColumn;
			m_ctx.Get<SpacingStyle>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			m_ctx.Get<SpacingStyle>(e)->gap     = 0.f;

			auto& tvd = m_ctx.Add<TabViewData>(e);

			// Tab-bar row
			ECS::EntityId tabBar = _SpawnContainer(WidgetBehaviorFlag::None);
			m_ctx.Get<LayoutProps>(tabBar)->layout = Layout::InLine;
			m_ctx.Get<LayoutProps>(tabBar)->height = Value::Px(tvd.tabHeight);
			m_ctx.Get<SpacingStyle>(tabBar)->gap   = 0.f;
			m_ctx.Get<SpacingStyle>(tabBar)->padding = {0.f, 0.f, 0.f, 0.f};
			AppendChild(e, tabBar);
			tvd.tabBar = tabBar;

			// Content area
			ECS::EntityId content = _SpawnContainer(WidgetBehaviorFlag::None);
			m_ctx.Get<LayoutProps>(content)->layout  = Layout::Stack;
			m_ctx.Get<LayoutProps>(content)->height  = Value::Grow(100.f);
			m_ctx.Get<SpacingStyle>(content)->padding = {0.f, 0.f, 0.f, 0.f};
			AppendChild(e, content);
			tvd.contentArea = content;
			return e;
		}

		/// @brief Add a tab: creates a tab button and a content container.
		void AddTabViewTab(ECS::EntityId e, std::string_view label, bool closable) {
			auto* tvd = m_ctx.Get<TabViewData>(e);
			if (!tvd) return;

			int idx = (int)tvd->tabs.size();
			TabViewData::Tab tab;
			tab.label    = std::string(label);
			tab.closable = closable;

			// Tab button
			ECS::EntityId btn = MakeButton(label);
			auto& btnLp = *m_ctx.Get<LayoutProps>(btn);
			btnLp.height = Value::Px(tvd->tabHeight);
			btnLp.width  = Value::Auto();
			m_ctx.Get<SpacingStyle>(btn)->padding = {12.f, 4.f, 12.f, 4.f};
			AppendChild(tvd->tabBar, btn);
			tab.tabButton = btn;

			// Content container (hidden unless active tab)
			ECS::EntityId cnt = _SpawnContainer(_AutoScrollable());
			_AttachVisual(cnt);
			m_ctx.Add<ScrollThumbInteraction>(cnt);
			m_ctx.Get<LayoutProps>(cnt)->layout = Layout::InColumn;
			m_ctx.Get<LayoutProps>(cnt)->height = Value::Grow(100.f);
			if (idx != tvd->activeTab) {
				m_ctx.Get<Widget>(cnt)->behavior &= ~WidgetBehaviorFlag::Visible;
			}
			AppendChild(tvd->contentArea, cnt);
			tab.tabContent = cnt;

			tvd->tabs.push_back(std::move(tab));
		}

		// ──────────────────────────────────────────────────────────────────────
		// Expander — header button + content container
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeExpander(std::string_view label, bool expanded) {
			ECS::EntityId e = _Spawn(WidgetType::Expander, _Interactive());
			_AttachVisual(e);
			_AttachTransform(e);
			_AttachSound(e);
			m_ctx.Get<LayoutProps>(e)->layout  = Layout::InColumn;
			m_ctx.Get<SpacingStyle>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			m_ctx.Get<SpacingStyle>(e)->gap     = 0.f;

			auto& ed = m_ctx.Add<ExpanderData>(e);
			ed.expanded = expanded;
			ed.animT    = expanded ? 1.f : 0.f;

			// Header button
			ECS::EntityId hdr = MakeButton(label);
			m_ctx.Get<LayoutProps>(hdr)->width  = Value::Grow(100.f);
			m_ctx.Get<LayoutProps>(hdr)->height = Value::Px(ed.headerH);
			if (auto* ts = m_ctx.Get<TextStyle>(hdr)) ts->alignH = TextHAlign::Left;
			m_ctx.Get<SpacingStyle>(hdr)->padding = {8.f, 4.f, 8.f, 4.f};
			AppendChild(e, hdr);
			ed.headerButton = hdr;

			// Content container
			ECS::EntityId cnt = _SpawnContainer(_AutoScrollable());
			_AttachVisual(cnt);
			m_ctx.Add<ScrollThumbInteraction>(cnt);
			m_ctx.Get<LayoutProps>(cnt)->layout = Layout::InColumn;
			m_ctx.Get<LayoutProps>(cnt)->height = Value::Grow(100.f);
			if (!expanded) {
				m_ctx.Get<Widget>(cnt)->behavior &= ~WidgetBehaviorFlag::Visible;
			}
			AppendChild(e, cnt);
			ed.contentEntity = cnt;
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// MenuBar — menu-title button row + overlay with item buttons
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeMenuBar() {
			ECS::EntityId e = _Spawn(WidgetType::MenuBar,
			                         WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			lp.height = Value::Px(26.f);
			lp.layout = Layout::InLine;
			m_ctx.Get<SpacingStyle>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			m_ctx.Get<SpacingStyle>(e)->gap     = 0.f;

			auto& mbd = m_ctx.Add<MenuBarData>(e);

			// Menu-title button row
			ECS::EntityId row = _SpawnContainer(WidgetBehaviorFlag::None);
			m_ctx.Get<LayoutProps>(row)->layout = Layout::InLine;
			m_ctx.Get<LayoutProps>(row)->height = Value::Grow(100.f);
			m_ctx.Get<SpacingStyle>(row)->gap     = 0.f;
			m_ctx.Get<SpacingStyle>(row)->padding = {0.f, 0.f, 0.f, 0.f};
			AppendChild(e, row);
			mbd.menuRow = row;

			// Shared overlay container (Fixed, hidden by default)
			ECS::EntityId ov = _Spawn(WidgetType::Container,
			                           WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			_AttachVisual(ov);
			m_ctx.Add<ScrollThumbInteraction>(ov);
			auto& ovLp = *m_ctx.Get<LayoutProps>(ov);
			ovLp.attach = AttachLayout::Fixed;
			ovLp.layout = Layout::InColumn;
			ovLp.width  = Value::Px(180.f);
			m_ctx.Get<Widget>(ov)->behavior &= ~WidgetBehaviorFlag::Visible;
			m_ctx.Get<SpacingStyle>(ov)->padding = {2.f, 2.f, 2.f, 2.f};
			m_ctx.Get<SpacingStyle>(ov)->gap     = 0.f;
			mbd.overlay = ov;
			return e;
		}

		/// @brief Add a menu to the MenuBar (creates a title button + populates overlay).
		void AddMenuBarMenu(ECS::EntityId e, MenuBarMenu menu) {
			auto* mbd = m_ctx.Get<MenuBarData>(e);
			if (!mbd || mbd->menuRow == ECS::NullEntity) return;

			mbd->menus.push_back(menu);

			// Title button
			ECS::EntityId btn = MakeButton(menu.label);
			m_ctx.Get<LayoutProps>(btn)->height = Value::Grow(100.f);
			m_ctx.Get<LayoutProps>(btn)->width  = Value::Auto();
			m_ctx.Get<SpacingStyle>(btn)->padding = {10.f, 0.f, 10.f, 0.f};
			if (!menu.enabled)
				m_ctx.Get<Widget>(btn)->behavior &= ~WidgetBehaviorFlag::Enable;
			AppendChild(mbd->menuRow, btn);
			mbd->menuButtons.push_back(btn);

			// Item buttons in the overlay (replaced on each open)
			// Items are stored in mbd->menus and overlay is rebuilt when the menu opens.
		}

		/// @brief Rebuild overlay item buttons for an open menu.
		void RebuildMenuOverlay(ECS::EntityId e, int menuIndex) {
			auto* mbd = m_ctx.Get<MenuBarData>(e);
			if (!mbd || mbd->overlay == ECS::NullEntity) return;
			if (menuIndex < 0 || menuIndex >= (int)mbd->menus.size()) return;

			// Destroy previous item buttons
			for (auto eid : mbd->overlayItems)
				if (m_ctx.IsAlive(eid)) m_ctx.DestroyEntity(eid);
			mbd->overlayItems.clear();
			if (auto* ch = m_ctx.Get<Children>(mbd->overlay)) ch->ids.clear();

			const auto& menu = mbd->menus[menuIndex];
			int itemIdx = 0;
			for (const auto& item : menu.items) {
				ECS::EntityId ib;
				if (item.separator) {
					ib = MakeSeparator();
				} else {
					ib = MakeButton(item.label);
					m_ctx.Get<LayoutProps>(ib)->width  = Value::Grow(100.f);
					m_ctx.Get<LayoutProps>(ib)->height = Value::Px(24.f);
					m_ctx.Get<SpacingStyle>(ib)->padding = {10.f, 2.f, 10.f, 2.f};
					if (auto* ts = m_ctx.Get<TextStyle>(ib)) ts->alignH = TextHAlign::Left;
					if (!item.enabled)
						m_ctx.Get<Widget>(ib)->behavior &= ~WidgetBehaviorFlag::Enable;
				}
				AppendChild(mbd->overlay, ib);
				mbd->overlayItems.push_back(ib);
				++itemIdx;
			}
		}

		// ──────────────────────────────────────────────────────────────────────
		// Misc
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeGraph() {
			ECS::EntityId e = _Spawn(WidgetType::Graph,
			                          WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			_AttachVisual(e);
			_AttachAccent(e);
			m_ctx.Add<GraphData>(e);
			m_ctx.Get<SpacingStyle>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}

		ECS::EntityId MakeSpinner(float speed) {
			ECS::EntityId e = _Spawn(WidgetType::Spinner);
			_AttachAccent(e);
			_AttachTransform(e);
			_AttachSpacing(e);
			auto& d = m_ctx.Add<SpinnerData>(e);
			d.speed = speed;
			return e;
		}

		ECS::EntityId MakeBadge(std::string_view text) {
			ECS::EntityId e = _Spawn(WidgetType::Badge);
			_AttachVisualText(e);
			_AttachTransform(e);
			auto& d = m_ctx.Add<BadgeData>(e);
			d.text = std::string(text);
			m_ctx.Get<LayoutProps>(e)->width  = Value::Auto();
			m_ctx.Get<LayoutProps>(e)->height = Value::Auto();
			return e;
		}

		ECS::EntityId MakeColorPicker(ColorPickerPalette palette, float step) {
			ECS::EntityId e = _Spawn(WidgetType::ColorPicker, _Interactive());
			_AttachVisual(e);
			_AttachAccent(e);
			_AttachSound(e);
			auto& d = m_ctx.Add<ColorPickerConfig>(e);
			d.palette       = palette;
			d.precisionStep = step;
			m_ctx.Add<ColorPickerState>(e);
			m_ctx.Add<ColorPickerInteraction>(e);
			return e;
		}

		ECS::EntityId MakePopup(std::string_view title,
		                        bool closable, bool draggable, bool resizable) {
			ECS::EntityId e = _Spawn(WidgetType::Popup,
			                          WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable
			                        | WidgetBehaviorFlag::Resizable | WidgetBehaviorFlag::Draggable);
			_AttachVisualText(e);
			_AttachTransform(e);
			_AttachSound(e);
			auto& d = m_ctx.Add<PopupConfig>(e);
			d.title     = std::string(title);
			d.closable  = closable;
			d.draggable = draggable;
			d.resizable = resizable;
			m_ctx.Add<PopupState>(e);
			m_ctx.Add<PopupInteraction>(e);
			if (auto* lp = m_ctx.Get<LayoutProps>(e))
				lp->attach = AttachLayout::Fixed;
			// Lift the whole popup subtree onto the Popup plane and clip it to the
			// root, so it draws above content and is never cut off by an ancestor.
			m_ctx.Add<LayerProps>(e, LayerProps{
				.layer = UILayer::Popup, .inherit = false, .attach = PopupAttach::Root});
			if (auto* sp = m_ctx.Get<SpacingStyle>(e))
				sp->padding = {4.f, 4.f, 4.f, d.headerH + 4.f};
			return e;
		}

		ECS::EntityId MakeSplitter(Orientation o, float ratio) {
			ECS::EntityId e = _Spawn(WidgetType::Splitter,
			                          WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			_AttachBackground(e);
			_AttachTransform(e);
			auto& sc = m_ctx.Add<SplitterConfig>(e);
			sc.orientation = o;
			auto& ss = m_ctx.Add<SplitterState>(e);
			ss.ratio = SDL::Clamp(ratio, sc.minRatio, sc.maxRatio);
			m_ctx.Add<SplitterInteraction>(e);
			return e;
		}

	private:
		Context&       m_sys;
		ECS::Context& m_ctx;

		static constexpr WidgetBehaviorFlag _Interactive() noexcept {
			return WidgetBehaviorFlag::Hoverable
			     | WidgetBehaviorFlag::Selectable
			     | WidgetBehaviorFlag::Focusable;
		}
		static constexpr WidgetBehaviorFlag _AutoScrollable() noexcept {
			return WidgetBehaviorFlag::AutoScrollableX | WidgetBehaviorFlag::AutoScrollableY;
		}

		// ── Style attachers ──────────────────────────────────────────────────
		void _AttachBackground(ECS::EntityId e) { if (!m_ctx.Has<BackgroundStyle>(e)) m_ctx.Add<BackgroundStyle>(e); }
		void _AttachBorder    (ECS::EntityId e) { if (!m_ctx.Has<BorderStyle>(e))     m_ctx.Add<BorderStyle>(e); }
		void _AttachText      (ECS::EntityId e) {
			if (!m_ctx.Has<TextStyle>(e)) {
				auto& ts = m_ctx.Add<TextStyle>(e);
				ts.usedFont = FontType::Default;
			}
		}
		void _AttachSpacing  (ECS::EntityId e) { if (!m_ctx.Has<SpacingStyle>(e))   m_ctx.Add<SpacingStyle>(e); }
		void _AttachTransform(ECS::EntityId e) { if (!m_ctx.Has<TransformStyle>(e)) m_ctx.Add<TransformStyle>(e); }
		void _AttachAccent   (ECS::EntityId e) { if (!m_ctx.Has<AccentStyle>(e))    m_ctx.Add<AccentStyle>(e); }
		void _AttachSound    (ECS::EntityId e) { if (!m_ctx.Has<SoundStyle>(e))     m_ctx.Add<SoundStyle>(e); }

		void _AttachVisual    (ECS::EntityId e) { _AttachBackground(e); _AttachBorder(e); _AttachSpacing(e); _AttachTransform(e); }
		void _AttachVisualText(ECS::EntityId e) { _AttachVisual(e); _AttachText(e); }

		/// @brief Opt a widget into smooth transitions with the given channels.
		void _AttachAnimation(ECS::EntityId e, AnimChannel channels) {
			if (!m_ctx.Has<AnimationStyle>(e)) {
				auto& as = m_ctx.Add<AnimationStyle>(e);
				as.channels = channels;
			}
			if (!m_ctx.Has<AnimationState>(e)) m_ctx.Add<AnimationState>(e);
		}

		// ── Universal spawn ──────────────────────────────────────────────────
		ECS::EntityId _Spawn(WidgetType type, WidgetBehaviorFlag extraBehavior = WidgetBehaviorFlag::None) {
			ECS::EntityId e = m_ctx.CreateEntity();
			WidgetBehaviorFlag beh = WidgetBehaviorFlag::Enable | WidgetBehaviorFlag::Visible
			                      | WidgetBehaviorFlag::DispatchEvent | extraBehavior;
			m_ctx.Add<Widget>(e, Widget{type, beh, WidgetStateFlag::None, DirtyFlag::All});
			m_ctx.Add<LayoutProps>(e);
			m_ctx.Add<PointerCbs>(e);
			m_ctx.Add<FocusCbs>(e);
			m_ctx.Add<ValueCbs>(e);
			m_ctx.Add<ItemCbs>(e);
			m_ctx.Add<ComputedRect>(e);
			m_ctx.Add<Children>(e);
			m_ctx.Add<Parent>(e);
			return e;
		}

		/// @brief Spawn a simple container (background + scroll, no text).
		ECS::EntityId _SpawnContainer(WidgetBehaviorFlag extra) {
			ECS::EntityId e = _Spawn(WidgetType::Container, extra);
			_AttachBackground(e);
			_AttachBorder(e);
			_AttachSpacing(e);
			_AttachTransform(e);
			return e;
		}

		// ── Private composite helpers ────────────────────────────────────────
		void _RebuildListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
			auto* lbd = m_ctx.Get<ListBoxData>(e);
			if (!lbd || lbd->scrollView == ECS::NullEntity) return;
			auto* ilv = m_ctx.Get<ItemListView>(e);
			if (!ilv) ilv = &m_ctx.Add<ItemListView>(e);
			ilv->items = items;

			for (int i = 0; i < (int)items.size(); ++i) {
				ECS::EntityId ib = MakeButton(items[i]);
				m_ctx.Get<LayoutProps>(ib)->width  = Value::Grow(100.f);
				m_ctx.Get<LayoutProps>(ib)->height = Value::Px(ilv->itemHeight);
				m_ctx.Get<SpacingStyle>(ib)->padding = {8.f, 2.f, 8.f, 2.f};
				if (auto* ts = m_ctx.Get<TextStyle>(ib)) ts->alignH = TextHAlign::Left;
				AppendChild(lbd->scrollView, ib);
				lbd->itemButtons.push_back(ib);
			}
		}

		void _RebuildComboBoxItems(ECS::EntityId e,
		                           std::vector<std::string> items,
		                           int selectedIndex) {
			auto* cbd = m_ctx.Get<ComboBoxData>(e);
			if (!cbd || cbd->overlay == ECS::NullEntity) return;
			auto* ilv = m_ctx.Get<ItemListView>(e);
			if (!ilv) ilv = &m_ctx.Add<ItemListView>(e);
			ilv->items = items;
			ilv->selectedIndex = items.empty() ? -1 : SDL::Clamp(selectedIndex, 0, (int)items.size() - 1);

			for (int i = 0; i < (int)items.size(); ++i) {
				ECS::EntityId ib = MakeButton(items[i]);
				m_ctx.Get<LayoutProps>(ib)->width  = Value::Grow(100.f);
				m_ctx.Get<LayoutProps>(ib)->height = Value::Px(24.f);
				m_ctx.Get<SpacingStyle>(ib)->padding = {8.f, 2.f, 8.f, 2.f};
				if (auto* ts = m_ctx.Get<TextStyle>(ib)) ts->alignH = TextHAlign::Left;
				AppendChild(cbd->overlay, ib);
			}
			// Update toggle button label
			if (cbd->toggleButton != ECS::NullEntity && !items.empty() &&
			    selectedIndex >= 0 && selectedIndex < (int)items.size()) {
				if (auto* te = m_ctx.Get<TextEdit>(cbd->toggleButton))
					te->text = items[selectedIndex];
			}
		}
	};

} // namespace SDL::UI
