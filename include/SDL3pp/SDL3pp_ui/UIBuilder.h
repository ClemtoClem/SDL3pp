#pragma once

#include "UIComponents.h"
#include "UISystem.h"
#include "UIValue.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SDL::UI {

	// ==================================================================================
	// BuilderBase — CRTP base used by every builder
	// ==================================================================================

	template <typename Derived>
	struct BuilderBase {
		System&        system;
		ECS::EntityId  id;

		BuilderBase(System& s, ECS::EntityId e) : system(s), id(e) {}

		operator ECS::EntityId() const noexcept { return id; }

		[[nodiscard]] ECS::EntityId Id() const noexcept { return id; }

		// ── Layout (size) ────────────────────────────────────────────────────
		Derived& W(float px)       { system.GetLayout(id).width  = Value::Px(px); return _self(); }
		Derived& W(const Value& v) { system.GetLayout(id).width  = v;             return _self(); }
		Derived& H(float px)       { system.GetLayout(id).height = Value::Px(px); return _self(); }
		Derived& H(const Value& v) { system.GetLayout(id).height = v;             return _self(); }
		Derived& GrowW(float g)    { return W(Value::Grow(g)); }
		Derived& GrowH(float g)    { return H(Value::Grow(g)); }
		Derived& Grow(float g)     { W(Value::Grow(g)); H(Value::Grow(g)); return _self(); }
		Derived& MinW(float px)    { system.GetLayout(id).minWidth  = Value::Px(px); return _self(); }
		Derived& MinH(float px)    { system.GetLayout(id).minHeight = Value::Px(px); return _self(); }
		Derived& MaxW(float px)    { system.GetLayout(id).maxWidth  = Value::Px(px); return _self(); }
		Derived& MaxH(float px)    { system.GetLayout(id).maxHeight = Value::Px(px); return _self(); }

		// ── Layout (position) ────────────────────────────────────────────────
		Derived& X(float px)       { system.GetLayout(id).absX = Value::Px(px); return _self(); }
		Derived& X(const Value& v) { system.GetLayout(id).absX = v;             return _self(); }
		Derived& Y(float px)       { system.GetLayout(id).absY = Value::Px(px); return _self(); }
		Derived& Y(const Value& v) { system.GetLayout(id).absY = v;             return _self(); }

		// ── Layout (spacing) ─────────────────────────────────────────────────
		Derived& Padding(float p) { system.GetLayout(id).padding = SDL::FBox(p); return _self(); }
		Derived& Padding(float h, float v) {
			auto& l = system.GetLayout(id);
			l.padding.left = l.padding.right = h;
			l.padding.top  = l.padding.bottom = v;
			return _self();
		}
		Derived& Padding(const SDL::FBox& pad) { system.GetLayout(id).padding = pad; return _self(); }
		Derived& PaddingH(float h) {
			auto& l = system.GetLayout(id);
			l.padding.left = l.padding.right = h;
			return _self();
		}
		Derived& PaddingV(float v) {
			auto& l = system.GetLayout(id);
			l.padding.top = l.padding.bottom = v;
			return _self();
		}

		Derived& Margin(float m) { system.GetLayout(id).margin = SDL::FBox(m);	return _self(); }
		Derived& Margin(float h, float v) {
			auto& l = system.GetLayout(id);
			l.margin.left = l.margin.right = h;
			l.margin.top  = l.margin.bottom = v;
			return _self();
		}
		Derived& Margin(const SDL::FBox& mar) { system.GetLayout(id).margin = mar; return _self(); }
		Derived& MarginV(float v) {
			auto& l = system.GetLayout(id);
			l.margin.top = l.margin.bottom = v;
			return _self();
		}
		Derived& MarginH(float h) {
			auto& l = system.GetLayout(id);
			l.margin.left = l.margin.right = h;
			return _self();
		}
		
		Derived& Gap(float g) { system.GetLayout(id).gap = g; return _self(); }
		Derived& Align(SDL::UI::Align ha, SDL::UI::Align va) {
			auto& l = system.GetLayout(id);
			l.alignSelfH = ha; l.alignSelfV = va;
			return _self();
		}
		Derived& AlignH(SDL::UI::Align a) { system.GetLayout(id).alignSelfH = a; return _self(); }
		Derived& AlignV(SDL::UI::Align a) { system.GetLayout(id).alignSelfV = a; return _self(); }
		Derived& Attach(AttachLayout a)   { system.GetLayout(id).attach = a;     return _self(); }

		// ── Style (background per state) ─────────────────────────────────────
		Derived& BgColor        (const SDL::Color& c) { system.GetStyle(id).bgColor         = c; return _self(); }
		Derived& BgHoveredColor (const SDL::Color& c) { system.GetStyle(id).bgHoveredColor  = c; return _self(); }
		Derived& BgPressedColor (const SDL::Color& c) { system.GetStyle(id).bgPressedColor  = c; return _self(); }
		Derived& BgCheckedColor (const SDL::Color& c) { system.GetStyle(id).bgCheckedColor  = c; return _self(); }
		Derived& BgFocusedColor (const SDL::Color& c) { system.GetStyle(id).bgFocusedColor  = c; return _self(); }
		Derived& BgDisabledColor(const SDL::Color& c) { system.GetStyle(id).bgDisabledColor = c; return _self(); }

		// ── Style (border per state) ─────────────────────────────────────────
		Derived& BdColor        (const SDL::Color& c) { system.GetStyle(id).bdColor         = c; return _self(); }
		Derived& BdHoveredColor (const SDL::Color& c) { system.GetStyle(id).bdHoveredColor  = c; return _self(); }
		Derived& BdPressedColor (const SDL::Color& c) { system.GetStyle(id).bdPressedColor  = c; return _self(); }
		Derived& BdCheckedColor (const SDL::Color& c) { system.GetStyle(id).bdCheckedColor  = c; return _self(); }
		Derived& BdFocusedColor (const SDL::Color& c) { system.GetStyle(id).bdFocusedColor  = c; return _self(); }
		Derived& BdDisabledColor(const SDL::Color& c) { system.GetStyle(id).bdDisabledColor = c; return _self(); }

		// ── Style (text per state) ───────────────────────────────────────────
		Derived& TextColor           (const SDL::Color& c) { system.GetStyle(id).textColor            = c; return _self(); }
		Derived& TextHoveredColor    (const SDL::Color& c) { system.GetStyle(id).textHoveredColor     = c; return _self(); }
		Derived& TextPressedColor    (const SDL::Color& c) { system.GetStyle(id).textPressedColor     = c; return _self(); }
		Derived& TextCheckedColor    (const SDL::Color& c) { system.GetStyle(id).textCheckedColor     = c; return _self(); }
		Derived& TextDisabledColor   (const SDL::Color& c) { system.GetStyle(id).textDisabledColor    = c; return _self(); }
		Derived& TextPlaceholderColor(const SDL::Color& c) { system.GetStyle(id).textPlaceholderColor = c; return _self(); }

		// ── Style (geometry) ─────────────────────────────────────────────────
		Derived& Borders(float w)               { system.GetStyle(id).borders = SDL::FBox(w); return _self(); }
		Derived& Borders(const SDL::FBox& b)    { system.GetStyle(id).borders = b;            return _self(); }
		Derived& Radius (float r)                { system.GetStyle(id).radius  = SDL::FCorners(r); return _self(); }
		Derived& Radius (const SDL::FCorners& r) { system.GetStyle(id).radius  = r;            return _self(); }
		Derived& Opacity(float a)                { system.GetStyle(id).opacity = SDL::Clamp(a, 0.f, 1.f); return _self(); }

		// ── Style (font) ─────────────────────────────────────────────────────
		Derived& FontSize(float sz)       { system.GetStyle(id).fontSize = sz; return _self(); }

		// ── Style (custom application) ────────────────────────────────────────
		template <typename F>
		Derived& WithStyle(F &&fn) { fn(system.GetStyle(id)); return _self(); }

		Derived& Style(const Style& s) { system.GetStyle(id) = s; return _self(); }

		// ── Pointer callbacks ────────────────────────────────────────────────
		Derived& OnMousePress  (std::function<void(SDL::MouseButton)> cb)		{ system.OnPress(id, std::move(cb));      return _self(); }
		Derived& OnMousePress  (std::function<void()> cb)						{ system.OnPress(id, [cb](SDL::MouseButton) { cb(); }); return _self(); }
		
		Derived& OnMouseRelease(std::function<void(SDL::MouseButton)> cb)		{ system.OnRelease(id, std::move(cb));    return _self(); }
		Derived& OnMouseRelease(std::function<void()> cb)						{ system.OnRelease(id, [cb](SDL::MouseButton) { cb(); }); return _self(); }
		
		Derived& OnMouseEnter  (std::function<void()> cb)						{ system.OnMouseEnter(id, std::move(cb)); return _self(); }
		Derived& OnMouseLeave  (std::function<void()> cb)						{ system.OnMouseLeave(id, std::move(cb)); return _self(); }
		
		Derived& OnClick       (std::function<void(SDL::MouseButton)> cb)		{ system.OnClick(id, std::move(cb));      return _self(); }
		Derived& OnClick       (std::function<void()> cb)						{ system.OnClick(id, [cb](SDL::MouseButton) { cb(); }); return _self(); }
		
		Derived& OnDoubleClick (std::function<void(SDL::MouseButton)> cb)		{ system.OnDoubleClick(id, std::move(cb));return _self(); }
		Derived& OnDoubleClick (std::function<void()> cb)						{ system.OnDoubleClick(id, [cb](SDL::MouseButton) { cb(); }); return _self(); }
		
		Derived& OnMultiClick  (std::function<void(SDL::MouseButton, int)> cb)	{ system.OnMultiClick(id, std::move(cb)); return _self(); }
		Derived& OnMultiClick  (std::function<void()>)							{ system.OnMultiClick(id, [cb](SDL::MouseButton, int) { cb(); }); return _self(); }
		Derived& OnFocusGain   (std::function<void()> cb)						{ system.OnFocusGain(id, std::move(cb));  return _self(); }
		Derived& OnFocusLose   (std::function<void()> cb)						{ system.OnFocusLose(id, std::move(cb));  return _self(); }

		// ── Behavior & hierarchy ─────────────────────────────────────────────
		Derived& SetVisible(bool v) { system.SetVisible(id, v); return _self(); }
		Derived& Show()				{ return SetVisible(true); }
		Derived& Hide()				{ return SetVisible(false); }

		Derived& SetEnable(bool e)  { system.SetEnable(id, e); return _self(); }
		Derived& Enable()			{ return SetEnable(true); }
		Derived& Disable()			{ return SetEnable(false); }

		// ── Gradients (present on all widgets) ──────────────────────
		Derived& BgGradient(SDL::Color color2,
				GradientAnchor start = GradientAnchor::Top,
				GradientAnchor end   = GradientAnchor::Bottom) {
			SDL::UI::BgGradient g;
			g.color2         = color2;
			g.color2Hovered  = {SDL::Clamp8(color2.r+20),SDL::Clamp8(color2.g+20),SDL::Clamp8(color2.b+20),color2.a};
			g.color2Pressed  = {SDL::Clamp8(color2.r*0.7f),SDL::Clamp8(color2.g*0.7f),SDL::Clamp8(color2.b*0.7f),color2.a};
			g.color2Disabled = {SDL::Clamp8(color2.r*0.5f),SDL::Clamp8(color2.g*0.5f),SDL::Clamp8(color2.b*0.5f),160};
			g.start = start; g.end = end;
			system.SetBgGradient(id, g); return _self();
		}

		Derived& AsRoot()                       { system.SetRoot(id);             return _self(); }
		Derived& AttachTo(ECS::EntityId parent) { system.AppendChild(parent, id); return _self(); }
		
		/** @brief Append a child entity to this container. */
		Derived &Child(ECS::EntityId e) {
			system.AppendChild(id, e);
			return _self();
		}

		/** @brief Append a child entity to this container. */
		template <typename C>
			requires(std::convertible_to<C, ECS::EntityId>)
		Derived &Child(C && c) {
			system.AppendChild(id, static_cast<ECS::EntityId>(c));
			return _self();
		}

		/**
		 * @brief Append multiple child entities to this container in one call.
		 * @param cs  Any number of entity IDs (or Builder instances) to append.
		 */
		template <typename... Cs>
			requires(std::convertible_to<Cs, ECS::EntityId> && ...)
		Derived &Children(Cs &&...cs) {
			(system.AppendChild(id, static_cast<ECS::EntityId>(std::forward<Cs>(cs))), ...);
			return _self();
		}

	protected:
		Derived& _self() noexcept { return static_cast<Derived&>(*this); }
	};

	// ==================================================================================
	// Builder — generic builder used by factories that don't need a typed DSL
	// ==================================================================================

	struct Builder : BuilderBase<Builder> {
		using BuilderBase<Builder>::BuilderBase;
	};

	// ==================================================================================
	// Mixins — opt-in feature blocks for typed builders
	// ==================================================================================

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct ScrollableMixin : Base {
		using Base::Base;
		
		Derived& SetAutoScrollable(bool both) {
			this->system.SetAutoScrollable(this->id, both, both);
			return this->_self();
		}
		Derived& SetAutoScrollable(bool x, bool y) {
			this->system.SetAutoScrollable(this->id, x, y);
			return this->_self();
		}
		Derived& ScrollbarThickness(float t) {
			this->system.SetScrollbarThickness(this->id, t);
			return this->_self();
		}
		Derived& OnScrollChange(std::function<void(SDL::FPoint, SDL::FPoint)> cb) {
			this->system.OnScrollChange(this->id, std::move(cb));
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct EditableMixin : Base {
		using Base::Base;

		Derived& Placeholder(std::string_view ph) {
			this->system.GetContent(this->id).placeholder = std::string(ph);
			return this->_self();
		}
		Derived& OnTextChange(std::function<void(const std::string&)> cb) {
			this->system.OnTextChange(this->id, std::move(cb));
			return this->_self();
		}
		Derived& ClickSound(std::string_view key) {
			this->system.GetStyle(this->id).clickSound = std::string(key);
			return this->_self();
		}
		Derived& HoverSound(std::string_view key) {
			this->system.GetStyle(this->id).hoverSound = std::string(key);
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct GridChildMixin : Base {
		using Base::Base;

		Derived& Cell(int col, int row, int colSpan = 1, int rowSpan = 1) {
			this->system.SetGridCell(this->id, col, row, colSpan, rowSpan);
			return this->_self();
		}
	};

	// ── Numeric widgets mixin: Slider, Knob, Progress, Input with value ────────────
	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct NumericMixin : Base {
		using Base::Base;

		template <typename T>
		Derived& OnChange(std::function<void(T)> cb) {
			this->system.OnChange<T>(this->id, std::move(cb));
			return this->_self();
		}

		template <typename T>
		Derived& Value(T v) {
			this->system.SetValue<T>(this->id, v);
			return this->_self();
		}

		template <typename T>
		Derived& MinValue(T v) {
			this->system.SetMinValue<T>(this->id, v);
			return this->_self();
		}

		template <typename T>
		Derived& MaxValue(T v) {
			this->system.SetMaxValue<T>(this->id, v);
			return this->_self();
		}
	};

	// ── State widgets mixin: Toggle, RadioButton ──────────────────────────────────
	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct StateableMixin : Base {
		using Base::Base;

		Derived& Checked(bool b = true) {
			this->system.SetChecked(this->id, b);
			return this->_self();
		}
		Derived& Unchecked() { return SetChecked(false); }

		Derived& OnToggle(std::function<void(bool)> cb) {
			this->system.OnToggle(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ── Image widgets mixin: Image, Badge, Icon ───────────────────────────────────
	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct ImageMixin : Base {
		using Base::Base;

		Derived& ImageKey(std::string_view key, ImageFit fit = ImageFit::Contain) {
			this->system.SetImageKey(this->id, key, fit);
			return this->_self();
		}
	};

	// ── Tooltip mixin for any widget ──────────────────────────────────────────────
	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct TooltipMixin : Base {
		using Base::Base;

		Derived& Tooltip(std::string_view text, float delay = 1.f) {
			this->system.SetTooltip(this->id, text, delay);
			return this->_self();
		}

		Derived& SetTooltipText(std::string_view text) {
			if (auto* tl = this->system.GetCtx().template Get<TooltipData>(this->id)) 
				tl->text = text;
			return this->_self();
		}

		Derived& SetTooltipDelay(float delay) {
			if (auto* tl = this->system.GetCtx().template Get<TooltipData>(this->id)) 
				tl->delay = delay;
			return this->_self();
		}

		Derived& SetTooltipVisible(bool v) {
			if (auto* tl = this->system.GetCtx().template Get<TooltipData>(this->id)) 
				tl->visible = v;
			return this->_self();
		}

		Derived& ShowTooltip() {
			return SetTooltipVisible(true);
		}

		Derived& HideTooltip() {
			return SetTooltipVisible(false);
		}

		Derived& RemoveTooltip() {
			this->system.RemoveTooltip(this->id);
			return this->_self();
		}
	};

	// ==================================================================================
	// Typed builders
	// ==================================================================================

	struct ContainerBuilder : ScrollableMixin<ContainerBuilder> {
		using ScrollableMixin<ContainerBuilder>::ScrollableMixin;

		ContainerBuilder& Layout(UI::Layout l) {
			this->system.GetLayout(this->id).layout = l;
			return this->_self();
		}

		// Non-grid versions (delegated to base)
		ContainerBuilder& Child(ECS::EntityId e) {
			this->system.AppendChild(this->id, e);
			return this->_self();
		}
		template <typename C>
		ContainerBuilder& Child(C && c) {
			this->system.AppendChild(this->id, static_cast<ECS::EntityId>(c));
			return this->_self();
		}

		// Grid versions
		ContainerBuilder& Child(ECS::EntityId e, int col, int row, int colSpan = 1, int rowSpan = 1) {
			this->system.AppendChild(this->id, e);
			this->system.SetGridCell(e, col, row, colSpan, rowSpan);
			return this->_self();
		}
		template <typename C>
		ContainerBuilder& Child(C && c, int col, int row, int colSpan = 1, int rowSpan = 1) {
			ECS::EntityId eid = static_cast<ECS::EntityId>(c);
			this->system.AppendChild(this->id, eid);
			this->system.SetGridCell(eid, col, row, colSpan, rowSpan);
			return this->_self();
		}
		ContainerBuilder& GridCols(int n) {
			this->system.SetGridCols(this->id, n);
			return this->_self();
		}
		ContainerBuilder& GridRows(int n) {
			this->system.SetGridRows(this->id, n);
			return this->_self();
		}
		ContainerBuilder& GridColSizing(GridSizing s) {
			this->system.SetGridColSizing(this->id, s);
			return this->_self();
		}
		ContainerBuilder& GridRowSizing(GridSizing s) {
			this->system.SetGridRowSizing(this->id, s);
			return this->_self();
		}
		ContainerBuilder& GridLineStyle(GridLines l) {
			this->system.SetGridLines(this->id, l);
			return this->_self();
		}
		ContainerBuilder& GridLineColor(const SDL::Color& c) {
			this->system.SetGridLineColor(this->id, c);
			return this->_self();
		}
		ContainerBuilder& GridLineThickness(float t) {
			this->system.SetGridLineThickness(this->id, t);
			return this->_self();
		}
		ContainerBuilder& GridSizingMode(GridSizing cols, GridSizing rows) {
			this->system.SetGridColSizing(this->id, cols);
			this->system.SetGridRowSizing(this->id, rows);
			return this->_self();
		}
	};

	struct ListBoxBuilder : ScrollableMixin<ListBoxBuilder> {
		using ScrollableMixin<ListBoxBuilder>::ScrollableMixin;

		ListBoxBuilder& Items(std::vector<std::string> items) {
			this->system.SetListBoxItems(this->id, std::move(items));
			return this->_self();
		}

		ListBoxBuilder& Selected(int idx) {
			this->system.SetListBoxSelection(this->id, idx);
			return this->_self();
		}

		ListBoxBuilder& Reorderable(bool v = true) {
			this->system.SetListBoxReorderable(this->id, v);
			return this->_self();
		}

		ListBoxBuilder& OnReorder(std::function<void(int, int)> cb) {
			this->system.SetListBoxOnReorder(this->id, std::move(cb));
			return this->_self();
		}
	};

	struct SpinnerBuilder : BuilderBase<SpinnerBuilder> {
		using BuilderBase<SpinnerBuilder>::BuilderBase;

		SpinnerBuilder& Speed(float s) {
			if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->speed = s;
			return this->_self();
		}
		SpinnerBuilder& ArcSpan(float a) {
			if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->arcSpan = a;
			return this->_self();
		}
		SpinnerBuilder& Thickness(float t) {
			if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->thickness = t;
			return this->_self();
		}
	};

	struct SplitterBuilder : BuilderBase<SplitterBuilder> {
		using BuilderBase<SplitterBuilder>::BuilderBase;

		SplitterBuilder& Ratio(float r) {
			if (auto* sd = this->system.GetCtx().template Get<SplitterData>(this->id)) sd->ratio = r;
			return *this;
		}
		SplitterBuilder& MinRatio(float r) {
			if (auto* sd = this->system.GetCtx().template Get<SplitterData>(this->id)) sd->minRatio = r;
			return *this;
		}
		SplitterBuilder& MaxRatio(float r) {
			if (auto* sd = this->system.GetCtx().template Get<SplitterData>(this->id)) sd->maxRatio = r;
			return *this;
		}
		SplitterBuilder& OnChange(std::function<void(float)> cb) {
			this->system.OnChange(this->id, std::move(cb));
			return *this;
		}
	};

	// TextAreaBuilder — chains Scrollable + Editable + GridChild on top of BuilderBase.
	struct TextAreaBuilder : 
		ScrollableMixin<TextAreaBuilder, 
		EditableMixin<TextAreaBuilder, 
		GridChildMixin<TextAreaBuilder>>> 
	{
		// On importe le constructeur du sommet de la chaîne
		using ScrollableMixin::ScrollableMixin; 

		// Méthodes spécifiques uniquement au TextArea
		TextAreaBuilder& ReadOnly(bool ro = true) {
			this->system.SetTextAreaReadOnly(this->id, ro);
			return this->_self();
		}

		TextAreaBuilder& TextAreaTabSize(int n) {
			this->system.SetTextAreaTabSize(this->id, n);
			return this->_self();
		}
	};

	// ── Label & Button (text-bearing simple widgets) ───────────────────────────────
	struct LabelBuilder : EditableMixin<LabelBuilder> {
		using EditableMixin<LabelBuilder>::EditableMixin;
	};

	struct ButtonBuilder : EditableMixin<ButtonBuilder> {
		using EditableMixin<ButtonBuilder>::EditableMixin;
	};

	// ── Numeric widgets (Slider, Knob, Progress, Input, ScrollBar) ─────────────────
	struct KnobBuilder : NumericMixin<KnobBuilder> {
		using NumericMixin<KnobBuilder>::NumericMixin;

		template <typename T>
		KnobBuilder& Markers(std::vector<T> m) {
			this->system.template SetKnobMarkers<T>(this->id, std::move(m));
			return this->_self();
		}
		KnobBuilder& FillColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).fillColor = c;
			return this->_self();
		}
		KnobBuilder& TrackColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).trackColor = c;
			return this->_self();
		}
		KnobBuilder& ThumbColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).thumbColor = c;
			return this->_self();
		}
	};

	struct ProgressBuilder : NumericMixin<ProgressBuilder> {
		using NumericMixin<ProgressBuilder>::NumericMixin;

		ProgressBuilder& Indeterminate(bool i = true) {
			if (auto* pd = this->system.GetCtx().template Get<ProgressData>(this->id)) {
				pd->isIndeterminate = i;
			}
			return this->_self();
		}
		ProgressBuilder& FillColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).fillColor = c;
			return this->_self();
		}
		ProgressBuilder& TrackColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).trackColor = c;
			return this->_self();
		}
	};

	struct SliderBuilder : BuilderBase<SliderBuilder> {
		using BuilderBase<SliderBuilder>::BuilderBase;

		template <typename T>
		SliderBuilder& OnChange(std::function<void(T)> cb) {
			this->system.template OnChange<T>(this->id, std::move(cb));
			return this->_self();
		}
		template <typename T>
		SliderBuilder& Markers(std::vector<T> m) {
			this->system.template SetSliderMarkers<T>(this->id, std::move(m));
			return this->_self();
		}
		SliderBuilder& FillColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).fillColor = c;
			return this->_self();
		}
		SliderBuilder& TrackColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).trackColor = c;
			return this->_self();
		}
		SliderBuilder& ThumbColor(const SDL::Color& c) {
			this->system.GetStyle(this->id).thumbColor = c;
			return this->_self();
		}
	};

	struct InputBuilder : NumericMixin<InputBuilder, EditableMixin<InputBuilder>> {
		using NumericMixin<InputBuilder, EditableMixin<InputBuilder>>::NumericMixin;

		InputBuilder& InputType([[maybe_unused]] InputType t) {
			// Store type info; actual validation happens on keystroke
			return this->_self();
		}
		InputBuilder& SetText(std::string_view text) {
			this->system.SetText(this->id, std::string(text));
			return this->_self();
		}
		InputBuilder& InputDecimals(int n) {
			if (auto* nv = this->system.GetCtx().template Get<NumericValue>(this->id))
				nv->decimals = n;
			return this->_self();
		}
	};

	struct ScrollBarBuilder : NumericMixin<ScrollBarBuilder> {
		using NumericMixin<ScrollBarBuilder>::NumericMixin;

		ScrollBarBuilder& TrackSize(float s) {
			if (auto* sb = this->system.GetCtx().template Get<ScrollBarData>(this->id)) {
				sb->trackSize = s;
			}
			return this->_self();
		}
		ScrollBarBuilder& OnScroll(std::function<void(float)> cb) {
			this->system.OnScroll(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ── State widgets (Toggle, RadioButton) ────────────────────────────────────────
	struct ToggleBuilder : StateableMixin<ToggleBuilder, EditableMixin<ToggleBuilder>> {
		using StateableMixin<ToggleBuilder, EditableMixin<ToggleBuilder>>::StateableMixin;
	};

	struct RadioButtonBuilder : StateableMixin<RadioButtonBuilder, EditableMixin<RadioButtonBuilder>> {
		using StateableMixin<RadioButtonBuilder, EditableMixin<RadioButtonBuilder>>::StateableMixin;

		RadioButtonBuilder& Group(std::string_view grp) {
			if (auto* rb = this->system.GetCtx().template Get<RadioButtonData>(this->id)) {
				rb->group = std::string(grp);
			}
			return this->_self();
		}
	};

	// ── Image widgets (Image, Badge) ──────────────────────────────────────────────
	struct ImageBuilder : ImageMixin<ImageBuilder> {
		using ImageMixin<ImageBuilder>::ImageMixin;

		ImageBuilder& Fit(ImageFit f) {
			if (auto* id = this->system.GetCtx().template Get<ImageData>(this->id)) {
				id->fit = f;
			}
			return this->_self();
		}
	};

	struct BadgeBuilder : ImageMixin<BadgeBuilder, EditableMixin<BadgeBuilder>> {
		using ImageMixin<BadgeBuilder, EditableMixin<BadgeBuilder>>::ImageMixin;

		BadgeBuilder& Variant(std::string_view v) {
			if (auto* bd = this->system.GetCtx().template Get<BadgeData>(this->id)) {
				bd->variant = std::string(v);
			}
			return this->_self();
		}
	};

	// ── Canvas & specialized ──────────────────────────────────────────────────────
	struct CanvasBuilder : BuilderBase<CanvasBuilder> {
		using BuilderBase<CanvasBuilder>::BuilderBase;

		CanvasBuilder& OnRender(std::function<void(SDL::RendererRef, const SDL::FRect&)> cb) {
			if (auto* c = this->system.GetCtx().template Get<CanvasData>(this->id)) {
				c->renderCb = std::move(cb);
			}
			return this->_self();
		}

		CanvasBuilder& OnEvent(std::function<void(SDL::Event&)> cb) {
			if (auto* c = this->system.GetCtx().template Get<CanvasData>(this->id)) {
				c->eventCb = std::move(cb);
			}
			return this->_self();
		}

		CanvasBuilder& OnUpdate(std::function<void(float)> cb) {
			if (auto* c = this->system.GetCtx().template Get<CanvasData>(this->id)) {
				c->updateCb = std::move(cb);
			}
			return this->_self();
		}
	};

	struct SeparatorBuilder : BuilderBase<SeparatorBuilder> {
		using BuilderBase<SeparatorBuilder>::BuilderBase;

		SeparatorBuilder& Thickness(float t) {
			this->system.GetStyle(this->id).borders.top = t;
			return this->_self();
		}

		SeparatorBuilder& Color(const SDL::Color& c) {
			this->system.GetStyle(this->id).bdColor = c;
			return this->_self();
		}
	};

	// ── ComboBox (editable + scrollable dropdown) ──────────────────────────────────
	struct ComboBoxBuilder : ScrollableMixin<ComboBoxBuilder, EditableMixin<ComboBoxBuilder>> {
		using ScrollableMixin<ComboBoxBuilder, EditableMixin<ComboBoxBuilder>>::ScrollableMixin;

		ComboBoxBuilder& Items(std::vector<std::string> items) {
			this->system.SetComboBoxItems(this->id, std::move(items));
			return this->_self();
		}

		ComboBoxBuilder& Selected(int idx) {
			this->system.SetComboBoxSelection(this->id, idx);
			return this->_self();
		}

		template <typename T>
		ComboBoxBuilder& OnChange(std::function<void(T)> cb) {
			this->system.template OnChange<T>(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ── Tree (scrollable hierarchical items) ────────────────────────────────────────
	struct TreeBuilder : ScrollableMixin<TreeBuilder> {
		using ScrollableMixin<TreeBuilder>::ScrollableMixin;

		TreeBuilder& TreeNode(std::string_view label, int level,
							  bool hasChildren, bool expanded,
							  std::string_view iconKey = "") {
			this->system.AddTreeNode(this->id,
				SDL::UI::TreeNodeData{std::string(label), std::string(iconKey), level, hasChildren, expanded});
			return this->_self();
		}

		TreeBuilder& OnSelect(std::function<void(int)> cb) {
			this->system.OnTreeSelect(this->id, [cb = std::move(cb)](int idx, bool) { cb(idx); });
			return this->_self();
		}

		TreeBuilder& OnToggleNode(std::function<void(int, bool)> cb) {
			if (auto* t = this->system.GetCtx().template Get<TreeData>(this->id)) {
				t->onToggleNode = std::move(cb);
			}
			return this->_self();
		}
	};

	// ── ColorPicker (specialized color selection widget) ──────────────────────────
	struct ColorPickerBuilder : BuilderBase<ColorPickerBuilder> {
		using BuilderBase<ColorPickerBuilder>::BuilderBase;

		ColorPickerBuilder& OnColorChange(std::function<void(SDL::Color)> cb) {
			this->system.OnColorChange(this->id, std::move(cb));
			return this->_self();
		}

		ColorPickerBuilder& AllowAlpha(bool a = true) {
			if (auto* cp = this->system.GetCtx().template Get<ColorPickerData>(this->id)) {
				cp->allowAlpha = a;
			}
			return this->_self();
		}

		ColorPickerBuilder& PickedColor(SDL::Color c) {
			this->system.SetPickedColor(this->id, c);
			return this->_self();
		}

		ColorPickerBuilder& PickerColorA(SDL::Color c) {
			if (auto* cp = this->system.GetCtx().template Get<ColorPickerData>(this->id))
				cp->colorA = c;
			return this->_self();
		}

		ColorPickerBuilder& PickerColorB(SDL::Color c) {
			if (auto* cp = this->system.GetCtx().template Get<ColorPickerData>(this->id))
				cp->colorB = c;
			return this->_self();
		}

		ColorPickerBuilder& PickerShowAlpha(bool v = true) {
			if (auto* cp = this->system.GetCtx().template Get<ColorPickerData>(this->id))
				cp->allowAlpha = v;
			return this->_self();
		}

		template <typename T>
		ColorPickerBuilder& OnChange(std::function<void(T)> cb) {
			this->system.template OnChange<T>(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ── Popup (draggable/resizable floating container) ───────────────────────────
	struct PopupBuilder : ScrollableMixin<PopupBuilder> {
		using ScrollableMixin<PopupBuilder>::ScrollableMixin;

		PopupBuilder& Draggable(bool d = true) {
			if (auto* p = this->system.GetCtx().template Get<PopupData>(this->id)) {
				p->draggable = d;
			}
			return this->_self();
		}

		PopupBuilder& Resizable(bool r = true) {
			if (auto* p = this->system.GetCtx().template Get<PopupData>(this->id)) {
				p->resizable = r;
			}
			return this->_self();
		}

		PopupBuilder& Modal(bool m = true) {
			if (auto* p = this->system.GetCtx().template Get<PopupData>(this->id)) {
				p->modal = m;
			}
			return this->_self();
		}

		PopupBuilder& OnClose(std::function<void()> cb) {
			if (auto* p = this->system.GetCtx().template Get<PopupData>(this->id)) {
				p->onClose = std::move(cb);
			}
			return this->_self();
		}

		PopupBuilder& PopupOpen(bool v = true) {
			this->system.SetPopupOpen(this->id, v);
			return this->_self();
		}
	};

	// ── TabView (tabbed container) ─────────────────────────────────────────────────
	struct TabViewBuilder : ScrollableMixin<TabViewBuilder> {
		using ScrollableMixin<TabViewBuilder>::ScrollableMixin;

		TabViewBuilder& TabLocation(TabLocation l) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id)) {
				tv->tabLocation = l;
			}
			return this->_self();
		}

		TabViewBuilder& OnTabChange(std::function<void(int)> cb) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id)) {
				tv->onTabChange = std::move(cb);
			}
			return this->_self();
		}

		TabViewBuilder& AddTab(std::string_view label, bool closable = false) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id))
				tv->tabs.push_back({std::string(label), closable});
			return this->_self();
		}
	};

	// ── Expander (collapsible container with header) ────────────────────────────────
	struct ExpanderBuilder : EditableMixin<ExpanderBuilder> {
		using EditableMixin<ExpanderBuilder>::EditableMixin;

		ExpanderBuilder& Expanded(bool e = true) {
			this->system.SetExpanderExpanded(this->id, e);
			return this->_self();
		}

		ExpanderBuilder& OnToggle(std::function<void(bool)> cb) {
			this->system.OnExpanderToggle(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ── MenuBar (horizontal menu with items) ───────────────────────────────────────
	struct MenuBarBuilder : ScrollableMixin<MenuBarBuilder> {
		using ScrollableMixin<MenuBarBuilder>::ScrollableMixin;

		MenuBarBuilder& Items(std::vector<std::string> items) {
			if (auto* mb = this->system.GetCtx().template Get<MenuBarData>(this->id)) {
				mb->items = std::move(items);
			}
			return this->_self();
		}

		MenuBarBuilder& OnItemSelect(std::function<void(int)> cb) {
			if (auto* mb = this->system.GetCtx().template Get<MenuBarData>(this->id)) {
				mb->onItemSelect = std::move(cb);
			}
			return this->_self();
		}
	};

	// ==================================================================================
	// Theme — predefined style presets
	// ==================================================================================

	struct Theme {
		static Style PrimaryButton(const SDL::Color& color) {
			Style s;
			s.bgColor = color;
			s.bgHoveredColor = color;
			s.bgPressedColor = color;
			s.textColor = SDL::Color{235, 235, 245, 255};
			s.borders = SDL::FBox(0.f);
			s.radius = SDL::FCorners(4.f);
			return s;
		}

		static Style GhostButton() {
			Style s;
			s.bgColor = SDL::Color{0, 0, 0, 0};
			s.bgHoveredColor = SDL::Color{50, 50, 60, 128};
			s.bgPressedColor = SDL::Color{70, 70, 80, 200};
			s.textColor = SDL::Color{235, 235, 245, 255};
			s.borders = SDL::FBox(0.f);
			s.radius = SDL::FCorners(4.f);
			return s;
		}

		static Style Transparent() {
			Style s;
			s.bgColor = SDL::Color{0, 0, 0, 0};
			s.bgHoveredColor = SDL::Color{0, 0, 0, 0};
			s.bgPressedColor = SDL::Color{0, 0, 0, 0};
			s.borders = SDL::FBox(0.f);
			return s;
		}
	};

	// ==================================================================================
	// Builder Factory Method Implementations
	// (Defined here after all builders are declared)
	// ==================================================================================

	inline ContainerBuilder System::Container(std::string_view n) {
		return ContainerBuilder{*this, MakeContainer(n)};
	}

	inline LabelBuilder System::Label(std::string_view n, std::string_view text) {
		return LabelBuilder{*this, MakeLabel(n, text)};
	}

	inline ButtonBuilder System::Button(std::string_view n, std::string_view text) {
		return ButtonBuilder{*this, MakeButton(n, text)};
	}

	inline ToggleBuilder System::Toggle(std::string_view n, std::string_view text) {
		return ToggleBuilder{*this, MakeToggle(n, text)};
	}

	inline RadioButtonBuilder System::Radio(std::string_view n, std::string_view group, std::string_view text) {
		return RadioButtonBuilder{*this, MakeRadioButton(n, group, text)};
	}

	inline ScrollBarBuilder System::ScrollBar(std::string_view n, float contentSize, float viewSize, Orientation o) {
		return ScrollBarBuilder{*this, MakeScrollBar(n, contentSize, viewSize, 10.f, o)};
	}

	inline ProgressBuilder System::Progress(std::string_view n, float v, float mx) {
		return ProgressBuilder{*this, MakeProgressBase(n, NumericValue{typeid(float), 0.0, static_cast<double>(mx), static_cast<double>(v), 1.0}, 4.f, Orientation::Horizontal)};
	}

	inline SeparatorBuilder System::Separator(std::string_view n) {
		return SeparatorBuilder{*this, MakeSeparator(n)};
	}

	inline InputBuilder System::Input(std::string_view n, std::string_view placeholder) {
		return InputBuilder{*this, MakeInput(n, placeholder)};
	}

	inline KnobBuilder System::Knob(std::string_view n, float mn, float mx, float v, KnobShape shape) {
		return KnobBuilder{*this, MakeKnob(n, mn, mx, v, shape)};
	}

	inline ImageBuilder System::ImageWidget(std::string_view n, std::string_view key, ImageFit fit) {
		return ImageBuilder{*this, MakeImage(n, key, fit)};
	}

	inline CanvasBuilder System::CanvasWidget(std::string_view n,
	                                    std::function<void(SDL::Event&)> eventCb,
	                                    std::function<void(float)> updateCb,
	                                    std::function<void(RendererRef, FRect)> renderCb) {
		return CanvasBuilder{*this, MakeCanvas(n, eventCb, updateCb, renderCb)};
	}

	inline TextAreaBuilder System::TextArea(std::string_view n, std::string_view text, std::string_view placeholder) {
		return TextAreaBuilder{*this, MakeTextArea(n, text, placeholder)};
	}

	inline ListBoxBuilder System::ListBoxWidget(std::string_view n, const std::vector<std::string>& items) {
		return ListBoxBuilder{*this, MakeListBox(n, items)};
	}

	inline Builder System::GradedGraph(std::string_view n) {
		return Builder{*this, MakeGraph(n)};
	}

	inline ComboBoxBuilder System::ComboBox(std::string_view n, const std::vector<std::string>& items, int sel) {
		return ComboBoxBuilder{*this, MakeComboBox(n, items, sel)};
	}

	inline TabViewBuilder System::TabView(std::string_view n) {
		return TabViewBuilder{*this, MakeTabView(n)};
	}

	inline ExpanderBuilder System::Expander(std::string_view n, std::string_view label, bool expanded) {
		return ExpanderBuilder{*this, MakeExpander(n, label, expanded)};
	}

	inline SplitterBuilder System::Splitter(std::string_view n, Orientation o, float ratio) {
		return SplitterBuilder{*this, MakeSplitter(n, o, ratio)};
	}

	inline SpinnerBuilder System::Spinner(std::string_view n, float speed) {
		return SpinnerBuilder{*this, MakeSpinner(n, speed)};
	}

	inline BadgeBuilder System::Badge(std::string_view n, std::string_view text) {
		return BadgeBuilder{*this, MakeBadge(n, text)};
	}

	inline ColorPickerBuilder System::ColorPicker(std::string_view n, ColorPickerPalette palette, float step) {
		return ColorPickerBuilder{*this, MakeColorPicker(n, palette, step)};
	}

	inline PopupBuilder System::Popup(std::string_view n, std::string_view title, bool closable, bool draggable, bool resizable) {
		return PopupBuilder{*this, MakePopup(n, title, closable, draggable, resizable)};
	}

	inline TreeBuilder System::Tree(std::string_view n) {
		return TreeBuilder{*this, MakeTree(n)};
	}

} // namespace SDL::UI
