#pragma once

#include "UIComponents.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_stdinc.h"

#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace SDL::UI {

	class System;

	// ==================================================================================
	// UIFactory — single point of widget construction
	// ==================================================================================
	//
	// Creates ECS entities and attaches the components a given widget kind needs.
	// Adding a new widget type only requires:
	//   1. Adding a new entry to the WidgetType enum (UIComponents.h)
	//   2. Adding a Make* method here that calls _Spawn(...) + the
	//      widget-specific component initialization.
	// No other file needs to change.
	//
	// ==================================================================================

	class UIFactory {
	public:
		UIFactory(System& sys, ECS::Context& ctx) : m_sys(sys), m_ctx(ctx) {}

		// ── Hierarchy management ─────────────────────────────────────────────
		void AppendChild(ECS::EntityId parent, ECS::EntityId child) {
			if (parent == ECS::NullEntity || child == ECS::NullEntity) return;
			if (!m_ctx.IsAlive(parent) || !m_ctx.IsAlive(child)) return;
			auto* ch = m_ctx.Get<Children>(parent);
			auto* p  = m_ctx.Get<Parent>(child);
			if (!ch || !p) return;
			// Detach from previous parent if any.
			if (p->id != ECS::NullEntity && p->id != parent) {
				if (auto* prev = m_ctx.Get<Children>(p->id)) prev->Remove(child);
			}
			// Avoid duplicate insertion.
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
		// Widget creation — base widgets
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeContainer(std::string_view n) {
			return _Spawn(n, WidgetType::Container, _AutoScrollable());
		}

		ECS::EntityId MakeLabel(std::string_view n, std::string_view text) {
			ECS::EntityId e = _Spawn(n, WidgetType::Label);
			m_ctx.Get<TextEdit>(e)->text = std::string(text);
			auto& s = *m_ctx.Get<Style>(e);
			s.padding.top = s.padding.bottom = 2.f;
			return e;
		}

		ECS::EntityId MakeButton(std::string_view n, std::string_view text) {
			ECS::EntityId e = _Spawn(n, WidgetType::Button, _Interactive());
			m_ctx.Get<TextEdit>(e)->text = std::string(text);
			return e;
		}

		ECS::EntityId MakeToggle(std::string_view n, std::string_view text) {
			ECS::EntityId e = _Spawn(n, WidgetType::Toggle, _Interactive());
			m_ctx.Get<TextEdit>(e)->text = std::string(text);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(28.f);
			m_ctx.Add<ToggleData>(e);
			return e;
		}

		ECS::EntityId MakeRadioButton(std::string_view n, std::string_view group, std::string_view text) {
			ECS::EntityId e = _Spawn(n, WidgetType::RadioButton, _Interactive());
			m_ctx.Get<TextEdit>(e)->text = std::string(text);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(24.f);
			m_ctx.Add<RadioData>(e, RadioData{std::string(group), false});
			return e;
		}

		ECS::EntityId MakeSeparator(std::string_view n) {
			ECS::EntityId e = _Spawn(n, WidgetType::Separator);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			lp.height = Value::Px(1.f);
			auto& s = *m_ctx.Get<Style>(e);
			s.margin.top = s.margin.bottom = 6.f;
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — value-bearing widgets (Slider / Knob / Progress)
		// ──────────────────────────────────────────────────────────────────────

		template <typename T=float>
		ECS::EntityId MakeSlider(std::string_view n, NumericValue<T> v = {}, float thickness = 12.f, Orientation o = Orientation::Horizontal) {
			ECS::EntityId e = _Spawn(n, WidgetType::Slider, _Interactive());
			m_ctx.Add<NumericValue<T>>(e, std::move(v));
			SliderData sd;
			sd.orientation = o;
			m_ctx.Add<SliderData>(e, sd);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Horizontal) lp.height = Value::Px(thickness);
			else                              lp.width  = Value::Px(thickness);
			return e;
		}

		template <typename T=float>
		ECS::EntityId MakeProgress(std::string_view n, NumericValue<T> v = {}, float thickness = 12.f, Orientation o = Orientation::Horizontal) {
			ECS::EntityId e = _Spawn(n, WidgetType::Progress);
			m_ctx.Add<NumericValue<T>>(e, std::move(v));
			ProgressData pd;
			pd.orientation = o;
			m_ctx.Add<ProgressData>(e, pd);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Horizontal) lp.height = Value::Px(thickness);
			else                              lp.width  = Value::Px(thickness);
			return e;
		}

		template <typename T=float>
		ECS::EntityId MakeKnob(std::string_view n, NumericValue<T> v, float size = 80.f, KnobShape shape = KnobShape::Potentiometer) {
			ECS::EntityId e = _Spawn(n, WidgetType::Knob, _Interactive());
			m_ctx.Add<NumericValue<T>>(e, std::move(v));
			KnobData kd;
			kd.shape = shape;
			m_ctx.Add<KnobData>(e, kd);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			lp.width = lp.height = Value::Px(size);
			return e;
		}

		ECS::EntityId MakeScrollBar(std::string_view n, float contentSize, float viewSize, float thickness = 12.f, Orientation o = Orientation::Vertical) {
			ECS::EntityId e = _Spawn(n, WidgetType::ScrollBar, _Interactive());
			ScrollBarData sd;
			sd.contentSize = contentSize;
			sd.viewSize    = viewSize;
			sd.orientation = o;
			m_ctx.Add<ScrollBarData>(e, sd);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Vertical) lp.width  = Value::Px(thickness);
			else                            lp.height = Value::Px(thickness);
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — text input
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeInput(std::string_view n, InputFilterType filter = InputFilterType::Text, std::string_view placeholder = "") {
			ECS::EntityId e = _Spawn(n, WidgetType::Input, _Interactive());
			InputData in{};
			in.filter = filter;
			m_ctx.Add<InputData>(e, in);
			auto& te = *m_ctx.Get<TextEdit>(e);
			te.placeholder = std::string(placeholder);
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(30.f);
			return e;
		}

		template <typename T=float>
		ECS::EntityId MakeInputValue(std::string_view n, NumericValue<T> v = {}, std::string_view placeholder = "") {
			ECS::EntityId e = MakeInput(n, std::is_floating_point<T> ? InputFilterType::Float : InputFilterType::Integer, placeholder);
			auto& nv = m_ctx.Add<NumericValue<T>>(e, std::move(v));
			m_ctx.Get<TextEdit>(e)->text = FormatNumeric(nv);
			return e;
		}

		ECS::EntityId MakeTextArea(std::string_view n, std::string_view text = "", std::string_view placeholder = "") {
			ECS::EntityId e = _Spawn(n, WidgetType::TextArea, _Interactive() | _AutoScrollable());
			auto& te = *m_ctx.Get<TextEdit>(e);
			te.text        = std::string(text);
			te.placeholder = std::string(placeholder);
			m_ctx.Add<TextSelection>(e);
			m_ctx.Add<TextSpans>(e);
			m_ctx.Add<TextAreaData>(e);
			m_ctx.Get<Style>(e)->padding = {6.f, 6.f, 6.f, 6.f};
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — visuals (Image / Canvas)
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeImage(std::string_view n, std::string_view key, ImageFit fit = ImageFit::None) {
			ECS::EntityId e = _Spawn(n, WidgetType::Image);
			m_ctx.Add<ImageData>(e, ImageData{std::string(key), fit});
			return e;
		}

		ECS::EntityId MakeCanvas(std::string_view n,
		                         std::function<void(SDL::Event&)>        eventCb,
		                         std::function<void(float)>              updateCb,
		                         std::function<void(RendererRef, FRect)> renderCb) {
			ECS::EntityId e = _Spawn(n, WidgetType::Canvas, _Interactive());
			m_ctx.Add<CanvasData>(e, CanvasData{std::move(eventCb), std::move(updateCb), std::move(renderCb)});
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — list-like (ListBox / ComboBox / Tree)
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeListBox(std::string_view n, std::vector<std::string> items) {
			ECS::EntityId e = _Spawn(n, WidgetType::ListBox, _Interactive() | _AutoScrollable());
			auto& lv = m_ctx.Add<ItemListView>(e);
			lv.items = std::move(items);
			m_ctx.Add<ListBoxData>(e);
			m_ctx.Get<Style>(e)->padding = {2.f, 2.f, 2.f, 2.f};
			return e;
		}

		ECS::EntityId MakeComboBox(std::string_view n, std::vector<std::string> items, int selectedIndex = 0) {
			ECS::EntityId e = _Spawn(n, WidgetType::ComboBox, _Interactive() | _AutoScrollable());
			auto& lv = m_ctx.Add<ItemListView>(e);
			lv.items         = std::move(items);
			lv.selectedIndex = lv.items.empty() ? -1 : SDL::Clamp(selectedIndex, 0, (int)lv.items.size() - 1);
			m_ctx.Add<ComboBoxData>(e);
			return e;
		}

		ECS::EntityId MakeTree(std::string_view n) {
			ECS::EntityId e = _Spawn(n, WidgetType::Tree, _Interactive() | _AutoScrollable());
			m_ctx.Add<TreeData>(e);
			m_ctx.Get<Style>(e)->padding = {2.f, 2.f, 2.f, 2.f};
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — composite (TabView / Expander / Splitter / Popup)
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeTabView(std::string_view n) {
			ECS::EntityId e = _Spawn(n, WidgetType::TabView, _Interactive());
			m_ctx.Add<TabViewData>(e);
			return e;
		}

		ECS::EntityId MakeExpander(std::string_view n, std::string_view label, bool expanded) {
			ECS::EntityId e = _Spawn(n, WidgetType::Expander, _Interactive());
			auto& d = m_ctx.Add<ExpanderData>(e);
			d.label    = std::string(label);
			d.expanded = expanded;
			d.animT    = expanded ? 1.f : 0.f;
			return e;
		}

		ECS::EntityId MakeSplitter(std::string_view n, Orientation o, float ratio) {
			ECS::EntityId e = _Spawn(n, WidgetType::Splitter, WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			auto& d = m_ctx.Add<SplitterData>(e);
			d.orientation = o;
			d.ratio       = SDL::Clamp(ratio, d.minRatio, d.maxRatio);
			return e;
		}

		ECS::EntityId MakePopup(std::string_view n, std::string_view title,
		                        bool closable, bool draggable, bool resizable) {
			ECS::EntityId e = _Spawn(n, WidgetType::Popup,
			                          WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable
			                        | WidgetBehaviorFlag::Resizable | WidgetBehaviorFlag::Draggable);
			auto& d = m_ctx.Add<PopupData>(e);
			d.title     = std::string(title);
			d.closable  = closable;
			d.draggable = draggable;
			d.resizable = resizable;
			if (auto* lp = m_ctx.Get<LayoutProps>(e)) {
				lp->attach = AttachLayout::Fixed;
			}
			if (auto* s = m_ctx.Get<Style>(e)) {
				s->padding = {4.f, 4.f, 4.f, d.headerH + 4.f};
			}
			return e;
		}

		// ──────────────────────────────────────────────────────────────────────
		// Widget creation — misc (Graph / Spinner / Badge / ColorPicker / MenuBar)
		// ──────────────────────────────────────────────────────────────────────

		ECS::EntityId MakeGraph(std::string_view n) {
			ECS::EntityId e = _Spawn(n, WidgetType::Graph, WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			m_ctx.Add<GraphData>(e);
			m_ctx.Get<Style>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}

		ECS::EntityId MakeSpinner(std::string_view n, float speed) {
			ECS::EntityId e = _Spawn(n, WidgetType::Spinner);
			auto& d = m_ctx.Add<SpinnerData>(e);
			d.speed = speed;
			return e;
		}

		ECS::EntityId MakeBadge(std::string_view n, std::string_view text) {
			ECS::EntityId e = _Spawn(n, WidgetType::Badge);
			auto& d = m_ctx.Add<BadgeData>(e);
			d.text = std::string(text);
			m_ctx.Get<LayoutProps>(e)->width = Value::Auto();
			m_ctx.Get<LayoutProps>(e)->height = Value::Auto();
			return e;
		}

		ECS::EntityId MakeColorPicker(std::string_view n, ColorPickerPalette palette, float step) {
			ECS::EntityId e = _Spawn(n, WidgetType::ColorPicker, _Interactive());
			auto& d = m_ctx.Add<ColorPickerData>(e);
			d.palette       = palette;
			d.precisionStep = step;
			return e;
		}

		ECS::EntityId MakeMenuBar(std::string_view n) {
			ECS::EntityId e = _Spawn(n, WidgetType::MenuBar, WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable);
			m_ctx.Add<MenuBarData>(e);
			auto& lp = *m_ctx.Get<LayoutProps>(e);
			lp.height = Value::Px(26.f);
			auto& s = *m_ctx.Get<Style>(e);
			s.padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}

	private:
		System&       m_sys;
		ECS::Context& m_ctx;

		/// Common interactive behavior bundle for clickable / focusable widgets.
		static constexpr WidgetBehaviorFlag _Interactive() noexcept {
			return WidgetBehaviorFlag::Hoverable | WidgetBehaviorFlag::Selectable | WidgetBehaviorFlag::Focusable;
		}

		/// Common interactive behavior bundle for Auto scrollable
		static constexpr WidgetBehaviorFlag _AutoScrollable() noexcept {
			return WidgetBehaviorFlag::AutoScrollableX | WidgetBehaviorFlag::AutoScrollableY;
		}

		/// Create an entity and attach the universal component bundle.
		/// Widget-specific components are added by each Make* method afterward.
		ECS::EntityId _Spawn(std::string_view name, WidgetType type,
		                     WidgetBehaviorFlag extraBehavior = WidgetBehaviorFlag::None) {
			ECS::EntityId e = m_ctx.CreateEntity();

			WidgetBehaviorFlag beh = WidgetBehaviorFlag::Enable | WidgetBehaviorFlag::Visible | extraBehavior;
			m_ctx.Add<Widget>(e, Widget{std::string(name), type, beh, WidgetStateFlag::None, DirtyFlag::All});

			auto& style = m_ctx.Add<Style>(e);
			style.usedFont = FontType::Default;

			m_ctx.Add<LayoutProps>(e);
			m_ctx.Add<Callbacks>(e);
			m_ctx.Add<ComputedRect>(e);
			m_ctx.Add<Children>(e);
			m_ctx.Add<Parent>(e);

			if (type == WidgetType::Input || type == WidgetType::TextArea)
				m_ctx.Add<TextEdit>(e);

			// Scrollable hosts get a drag-state component for inline scrollbars.
			if (type == WidgetType::Container || type == WidgetType::TextArea
			    || type == WidgetType::ListBox || type == WidgetType::Tree)
				m_ctx.Add<ContainerScrollState>(e);

			return e;
		}
	};

} // namespace SDL::UI
