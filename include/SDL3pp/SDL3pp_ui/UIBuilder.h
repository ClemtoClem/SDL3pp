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

		// ── Pointer callbacks ────────────────────────────────────────────────
		Derived& OnMousePress  (std::function<void(SDL::MouseButton)> cb)      { system.OnPress(id, std::move(cb));      return _self(); }
		Derived& OnMouseRelease(std::function<void(SDL::MouseButton)> cb)      { system.OnRelease(id, std::move(cb));    return _self(); }
		Derived& OnMouseEnter  (std::function<void()> cb)                      { system.OnMouseEnter(id, std::move(cb)); return _self(); }
		Derived& OnMouseLeave  (std::function<void()> cb)                      { system.OnMouseLeave(id, std::move(cb)); return _self(); }
		Derived& OnClick       (std::function<void(SDL::MouseButton)> cb)      { system.OnClick(id, std::move(cb));      return _self(); }
		Derived& OnClick       (std::function<void()> cb)                      { system.OnClick(id, [cb](SDL::MouseButton) { cb(); }); return _self(); }
		Derived& OnDoubleClick (std::function<void(SDL::MouseButton)> cb)      { system.OnDoubleClick(id, std::move(cb));return _self(); }
		Derived& OnDoubleClick (std::function<void()> cb)                      { system.OnDoubleClick(id, [cb](SDL::MouseButton) { cb(); }); return _self(); }
		Derived& OnMultiClick  (std::function<void(SDL::MouseButton, int)> cb) { system.OnMultiClick(id, std::move(cb)); return _self(); }
		Derived& OnFocusGain   (std::function<void()> cb)                      { system.OnFocusGain(id, std::move(cb));  return _self(); }
		Derived& OnFocusLose  (std::function<void()> cb)                      { system.OnFocusLose(id, std::move(cb)); return _self(); }

		// ── Behavior & hierarchy ─────────────────────────────────────────────
		Derived& Visible(bool v = true)  { system.SetVisible(id, v); return _self(); }
		Derived& Show()                  { return Visible(true); }
		Derived& Hide()                  { return Visible(false); }
		Derived& Enable (bool e = true)  { system.SetEnabled(id, e); return _self(); }
		Derived& Disable()               { return Enable(false); }

		Derived& AsRoot()                       { system.SetRoot(id);             return _self(); }
		Derived& AttachTo(ECS::EntityId parent) { system.AppendChild(parent, id); return _self(); }

		template <typename... Ids>
		Derived& Children(Ids... children) {
			(system.AppendChild(id, ECS::EntityId(children)), ...);
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

		Derived& AutoScrollable(bool x, bool y) {
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
			this->system.template OnChange<T>(this->id, std::move(cb));
			return this->_self();
		}

		template <typename T>
		Derived& Value(T v) {
			this->system.template SetValue<T>(this->id, v);
			return this->_self();
		}

		template <typename T>
		Derived& GetValue() {
			return this->system.template GetValue<T>(this->id);
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
		ContainerBuilder& Child(ECS::EntityId child) {
			this->system.AppendChild(this->id, child);
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

	struct SliderBuilder : BuilderBase<SliderBuilder> {
		using BuilderBase<SliderBuilder>::BuilderBase;

		template <typename T>
		SliderBuilder& OnChange(std::function<void(T)> cb) {
			this->system.template OnChange<T>(this->id, std::move(cb));
			return *this;
		}
		template <typename T>
		SliderBuilder& Markers(std::vector<T> m) {
			this->system.template SetSliderMarkers<T>(this->id, std::move(m));
			return *this;
		}
	};

	struct SpinnerBuilder : BuilderBase<SpinnerBuilder> {
		using BuilderBase<SpinnerBuilder>::BuilderBase;

		SpinnerBuilder& Speed(float s) {
			if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->speed = s;
			return *this;
		}
		SpinnerBuilder& ArcSpan(float a) {
			if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->arcSpan = a;
			return *this;
		}
		SpinnerBuilder& Thickness(float t) {
			if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->thickness = t;
			return *this;
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

	struct InputBuilder
		: NumericMixin<InputBuilder,
		  EditableMixin<InputBuilder>>
	{
		using NumericMixin<InputBuilder, EditableMixin<InputBuilder>>::NumericMixin;

		InputBuilder& InputType(InputType t) {
			if (auto* iv = this->system.GetCtx().template Get<NumericValue>(this->id)) {
				// Store type info; actual validation happens on keystroke
			}
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
	};

	// ── State widgets (Toggle, RadioButton) ────────────────────────────────────────
	struct ToggleBuilder
		: StateableMixin<ToggleBuilder,
		  EditableMixin <ToggleBuilder>>
	{
		using StateableMixin<ToggleBuilder, EditableMixin<ToggleBuilder>>::StateableMixin;
	};

	struct RadioButtonBuilder
		: StateableMixin<RadioButtonBuilder,
		  EditableMixin <RadioButtonBuilder>>
	{
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

	struct BadgeBuilder
		: ImageMixin<BadgeBuilder,
		  EditableMixin<BadgeBuilder>>
	{
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
	struct ComboBoxBuilder
		: ScrollableMixin<ComboBoxBuilder,
		  EditableMixin <ComboBoxBuilder>>
	{
		using ScrollableMixin<ComboBoxBuilder, EditableMixin<ComboBoxBuilder>>::ScrollableMixin;

		ComboBoxBuilder& Items(std::vector<std::string> items) {
			this->system.SetComboBoxItems(this->id, std::move(items));
			return this->_self();
		}

		ComboBoxBuilder& Selected(int idx) {
			this->system.SetComboBoxSelection(this->id, idx);
			return this->_self();
		}
	};

	// ── Tree (scrollable hierarchical items) ────────────────────────────────────────
	struct TreeBuilder : ScrollableMixin<TreeBuilder> {
		using ScrollableMixin<TreeBuilder>::ScrollableMixin;

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
	};

	// ── Expander (collapsible container with header) ────────────────────────────────
	struct ExpanderBuilder
		: EditableMixin<ExpanderBuilder>
	{
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
		return ScrollBarBuilder{*this, MakeScrollBar(n, contentSize, viewSize, o)};
	}

	inline ProgressBuilder System::Progress(std::string_view n, float v, float mx) {
		return ProgressBuilder{*this, MakeProgress(n, v, mx)};
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
