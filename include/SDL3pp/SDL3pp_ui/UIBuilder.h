#pragma once

#include "UIComponents.h"
#include "UIContext.h"
#include "UIValue.h"

#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace SDL::UI {

	// ==================================================================================
	// BuilderBase — CRTP base used by every builder.
	//
	// Holds ONLY universal API:
	//   - Tag (searchable label, opt-in)				   → Tag
	//   - Layout (size, position, alignment)              → LayoutProps
	//   - Behavior flags (Show / Hide / Enable / …)       → Widget
	//   - Hierarchy (Child / Children / AttachTo)         → Children, Parent
	//   - Pointer & focus callbacks                       → Callbacks
	//
	// Every visual aspect lives in a dedicated mixin (Background, Border, Text,
	// Spacing, Transform, Accent, Sound, …) so that a Separator builder doesn't
	// expose .TextColor() and an Image builder doesn't expose .BgGradient().
	// ==================================================================================

	template <typename Derived>
	struct BuilderBase {
		Context&       system;
		ECS::EntityId id;

		BuilderBase(Context& s, ECS::EntityId e) : system(s), id(e) {}

		operator ECS::EntityId() const noexcept { return id; }
		[[nodiscard]] ECS::EntityId Id() const noexcept { return id; }

		// ── Tag ─────────────────────────────────────────────────────────────
		Derived& SetTag(std::string_view tag) {
			_Ensure<Tag>().value = std::string(tag);
			return _self();
		}
		[[nodiscard]] std::string GetTag() const {
			if (auto* t = system.GetCtx().template Get<Tag>(id)) return t->value;
			return {};
		}

		// ── Layout — size ───────────────────────────────────────────────────
		Derived& W(float px)       { system.GetCtx().template Get<LayoutProps>(id)->width  = Value::Px(px); return _self(); }
		Derived& W(const Value& v) { system.GetCtx().template Get<LayoutProps>(id)->width  = v;             return _self(); }
		Derived& H(float px)       { system.GetCtx().template Get<LayoutProps>(id)->height = Value::Px(px); return _self(); }
		Derived& H(const Value& v) { system.GetCtx().template Get<LayoutProps>(id)->height = v;             return _self(); }
		Derived& GrowW(float g)    { return W(Value::Grow(g)); }
		Derived& GrowH(float g)    { return H(Value::Grow(g)); }
		Derived& Grow(float g)     { W(Value::Grow(g)); H(Value::Grow(g)); return _self(); }
		Derived& MinW(float px)    { system.GetCtx().template Get<LayoutProps>(id)->minWidth  = Value::Px(px); return _self(); }
		Derived& MinH(float px)    { system.GetCtx().template Get<LayoutProps>(id)->minHeight = Value::Px(px); return _self(); }
		Derived& MaxW(float px)    { system.GetCtx().template Get<LayoutProps>(id)->maxWidth  = Value::Px(px); return _self(); }
		Derived& MaxH(float px)    { system.GetCtx().template Get<LayoutProps>(id)->maxHeight = Value::Px(px); return _self(); }

		// ── Layout — position ───────────────────────────────────────────────
		Derived& X(float px)       { system.GetCtx().template Get<LayoutProps>(id)->absX = Value::Px(px); return _self(); }
		Derived& X(const Value& v) { system.GetCtx().template Get<LayoutProps>(id)->absX = v;             return _self(); }
		Derived& Y(float px)       { system.GetCtx().template Get<LayoutProps>(id)->absY = Value::Px(px); return _self(); }
		Derived& Y(const Value& v) { system.GetCtx().template Get<LayoutProps>(id)->absY = v;             return _self(); }

		// ── Layout — alignment / attach ─────────────────────────────────────
		Derived& Align(SDL::UI::Align ha, SDL::UI::Align va) {
			auto& l = *system.GetCtx().template Get<LayoutProps>(id);
			l.alignSelfH = ha; l.alignSelfV = va;
			return _self();
		}
		Derived& AlignH(SDL::UI::Align a)       { system.GetCtx().template Get<LayoutProps>(id)->alignSelfH = a; return _self(); }
		Derived& AlignV(SDL::UI::Align a)       { system.GetCtx().template Get<LayoutProps>(id)->alignSelfV = a; return _self(); }
		Derived& Attach(AttachLayout a)         { system.GetCtx().template Get<LayoutProps>(id)->attach     = a; return _self(); }
		Derived& BoxSizing(SDL::UI::BoxSizing s){ system.GetCtx().template Get<LayoutProps>(id)->boxSizing = s; return _self(); }

		// ── Behavior — visibility / enable ──────────────────────────────────
		Derived& SetVisible(bool v) { system.SetVisible(id, v); return _self(); }
		Derived& Show()             { return SetVisible(true); }
		Derived& Hide()             { return SetVisible(false); }
		Derived& SetEnable(bool e)  { system.SetEnable(id, e); return _self(); }
		Derived& Enable()           { return SetEnable(true); }
		Derived& Disable()          { return SetEnable(false); }

		// ── Hierarchy ───────────────────────────────────────────────────────
		Derived& AsRoot()                       { system.SetRoot(id);             return _self(); }
		Derived& AttachTo(ECS::EntityId parent) { system.AppendChild(parent, id); return _self(); }

		Derived& Child(ECS::EntityId e) { system.AppendChild(id, e); return _self(); }

		template <typename C> requires(std::convertible_to<C, ECS::EntityId>)
		Derived& Child(C&& c) {
			system.AppendChild(id, static_cast<ECS::EntityId>(c));
			return _self();
		}

		template <typename... Cs> requires(std::convertible_to<Cs, ECS::EntityId> && ...)
		Derived& Children(Cs&&... cs) {
			(system.AppendChild(id, static_cast<ECS::EntityId>(std::forward<Cs>(cs))), ...);
			return _self();
		}

		// ── Z-order / layering (IndexSystem) ────────────────────────────────
		/// Place this widget (and its subtree) on an explicit plane. Higher layers
		/// draw on top of and receive events before lower ones.
		Derived& Layer(UILayer l) {
			auto& lp = _Ensure<LayerProps>();
			lp.layer = l; lp.inherit = false;
			return _self();
		}
		/// Fine ordering within (or near) the resolved layer. Keep small relative to
		/// the 1000-unit plane spacing of UILayer.
		Derived& ZOffset(int z) { _Ensure<LayerProps>().zOffset = z; return _self(); }

		// ── Portal attachment (popups / dropdowns / tooltips) ───────────────
		/// Anchor this widget for clipping: Parent (default flow), Root or Window to
		/// escape ancestor clipping (so a popup is not cut off by its container).
		Derived& Portal(PopupAttach a) {
			auto& lp = _Ensure<LayerProps>();
			lp.attach = a;
			if (a != PopupAttach::Target) lp.attachTarget = ECS::NullEntity;
			return _self();
		}
		/// Portal onto an explicit target entity's clip rect.
		Derived& Portal(ECS::EntityId target) {
			auto& lp = _Ensure<LayerProps>();
			lp.attach = PopupAttach::Target; lp.attachTarget = target;
			return _self();
		}

		// ── Animation / transitions (AnimationSystem) ───────────────────────
		/// Opt this widget into smooth, CSS-like transitions on the given channels.
		Derived& Animate(AnimChannel ch = AnimChannel::All) {
			_Ensure<AnimationStyle>().channels = ch;
			_Ensure<AnimationState>();
			return _self();
		}
		/// Configure transition timing (and opt in if not already animated).
		Derived& Transition(float seconds, Easing e = Easing::EaseOut,
		                     AnimChannel ch = AnimChannel::Color | AnimChannel::Toggle | AnimChannel::Opacity) {
			auto& as = _Ensure<AnimationStyle>();
			as.duration = seconds; as.easing = e; as.channels = ch;
			_Ensure<AnimationState>();
			return _self();
		}
	protected:
		Derived& _self() noexcept { return static_cast<Derived&>(*this); }

		/// Lazy-attach helper: returns a reference to component @p T,
		/// adding it with default-constructed values if absent.
		template <typename T>
		T& _Ensure() {
			auto& ctx = system.GetCtx();
			if (auto* c = ctx.template Get<T>(id)) return *c;
			return ctx.template Add<T>(id);
		}
	};

	// ==================================================================================
	// Builder — generic builder used by factories that don't need a typed DSL
	// ==================================================================================

	struct Builder : BuilderBase<Builder> {
		using BuilderBase<Builder>::BuilderBase;
	};

	// ==================================================================================
	// Behavior mixins — operate on Widget::behavior bitmask only
	// ==================================================================================

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct HoverableMixin : Base {
		using Base::Base;
		Derived& SetHoverable(bool h = true) { this->system.SetHoverable(this->id, h); return this->_self(); }
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct FocusableMixin : Base {
		using Base::Base;
		Derived& SetFocusable(bool f = true) { this->system.SetFocusable(this->id, f); return this->_self(); }
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct SelectableMixin : Base {
		using Base::Base;
		Derived& SetSelectable(bool s = true) { this->system.SetSelectable(this->id, s); return this->_self(); }
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct DraggableMixin : Base {
		using Base::Base;
		Derived& SetDraggable(bool d = true) {
			auto& w = *this->system.GetCtx().template Get<Widget>(this->id);
			if (d) w.behavior |= WidgetBehaviorFlag::Draggable;
			else   w.behavior &= ~WidgetBehaviorFlag::Draggable;
			return this->_self();
		}
		Derived& OnDrag(std::function<void(SDL::FPoint)> cb) {
			this->system.OnDrag(this->id, std::move(cb));
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct TouchMixin : Base {
		using Base::Base;
		Derived& OnTouchFingerEvent(std::function<void(const SDL::TouchFingerEvent&)> cb) {
			this->system.OnTouchFingerEvent(this->id, std::move(cb));
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct ClickableMixin : Base {
		using Base::Base;
		Derived& OnClick(std::function<void(SDL::MouseButton)> cb) {
			this->system.OnClick(this->id, std::move(cb));
			return this->_self();
		}
		Derived& OnClick(std::function<void()> cb) {
			this->system.OnClick(this->id, [cb = std::move(cb)](SDL::MouseButton) { cb(); });
			return this->_self();
		}
		Derived& OnPress(std::function<void(SDL::MouseButton)> cb) {
			this->system.OnPress(this->id, std::move(cb));
			return this->_self();
		}
		Derived& OnRelease(std::function<void(SDL::MouseButton)> cb) {
			this->system.OnRelease(this->id, std::move(cb));
			return this->_self();
		}
		Derived& OnDoubleClick(std::function<void(SDL::MouseButton)> cb) {
			this->system.OnDoubleClick(this->id, std::move(cb));
			return this->_self();
		}
		Derived& OnMouseEnter(std::function<void()> cb) {
			this->system.OnMouseEnter(this->id, std::move(cb));
			return this->_self();
		}
		Derived& OnMouseLeave(std::function<void()> cb) {
			this->system.OnMouseLeave(this->id, std::move(cb));
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct ScrollableMixin : Base {
		using Base::Base;
		Derived& SetAutoScrollable(bool both)      { this->system.SetAutoScrollable(this->id, both, both); return this->_self(); }
		Derived& SetAutoScrollable(bool x, bool y) { this->system.SetAutoScrollable(this->id, x, y);       return this->_self(); }
		Derived& ScrollbarThickness(float t) {
			this->template _Ensure<ScrollbarStyle>().thickness = t;
			return this->_self();
		}
		Derived& OnScrollChange(std::function<void(SDL::FPoint, SDL::FPoint)> cb) {
			this->system.OnScrollChange(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ==================================================================================
	// Visual mixins — each touches a SINGLE specialized style component
	// ==================================================================================

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct BackgroundMixin : Base {
		using Base::Base;

		Derived& BgColor        (const SDL::Color& c) { this->template _Ensure<BackgroundStyle>().color         = c; return this->_self(); }
		Derived& BgHoveredColor (const SDL::Color& c) { this->template _Ensure<BackgroundStyle>().hoveredColor  = c; return this->_self(); }
		Derived& BgPressedColor (const SDL::Color& c) { this->template _Ensure<BackgroundStyle>().selectedColor = c; return this->_self(); }
		Derived& BgCheckedColor (const SDL::Color& c) { this->template _Ensure<BackgroundStyle>().checkedColor  = c; return this->_self(); }
		Derived& BgFocusedColor (const SDL::Color& c) { this->template _Ensure<BackgroundStyle>().focusedColor  = c; return this->_self(); }
		Derived& BgDisabledColor(const SDL::Color& c) { this->template _Ensure<BackgroundStyle>().disabledColor = c; return this->_self(); }

		Derived& BgGradient(SDL::Color color2,
		                    GradientAnchor start = GradientAnchor::Top,
		                    GradientAnchor end   = GradientAnchor::Bottom) {
			SDL::UI::BgGradient g;
			g.color2         = color2;
			g.color2Hovered  = {SDL::Clamp8(color2.r + 20),   SDL::Clamp8(color2.g + 20),   SDL::Clamp8(color2.b + 20),   color2.a};
			g.color2Selected = {SDL::Clamp8(color2.r * 0.7f), SDL::Clamp8(color2.g * 0.7f), SDL::Clamp8(color2.b * 0.7f), color2.a};
			g.color2Disabled = {SDL::Clamp8(color2.r * 0.5f), SDL::Clamp8(color2.g * 0.5f), SDL::Clamp8(color2.b * 0.5f), 160};
			g.start = start; g.end = end;
			this->template _Ensure<BackgroundStyle>().gradient = g;
			return this->_self();
		}

		Derived& RemoveBgGradient() {
			if (auto* bg = this->system.GetCtx().template Get<BackgroundStyle>(this->id))
				bg->gradient.reset();
			return this->_self();
		}

		template <typename F>
		Derived& WithBgGradient(F&& fn) {
			auto& bg = this->template _Ensure<BackgroundStyle>();
			if (!bg.gradient) bg.gradient.emplace();
			fn(*bg.gradient);
			return this->_self();
		}

		template <typename F>
		Derived& WithBackground(F&& fn) {
			fn(this->template _Ensure<BackgroundStyle>());
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct BorderMixin : Base {
		using Base::Base;

		Derived& BdColor        (const SDL::Color& c) { this->template _Ensure<BorderStyle>().color         = c; return this->_self(); }
		Derived& BdHoveredColor (const SDL::Color& c) { this->template _Ensure<BorderStyle>().hoveredColor  = c; return this->_self(); }
		Derived& BdPressedColor (const SDL::Color& c) { this->template _Ensure<BorderStyle>().selectedColor = c; return this->_self(); }
		Derived& BdCheckedColor (const SDL::Color& c) { this->template _Ensure<BorderStyle>().checkedColor  = c; return this->_self(); }
		Derived& BdFocusedColor (const SDL::Color& c) { this->template _Ensure<BorderStyle>().focusedColor  = c; return this->_self(); }
		Derived& BdDisabledColor(const SDL::Color& c) { this->template _Ensure<BorderStyle>().disabledColor = c; return this->_self(); }

		Derived& Borders(float w)                { this->template _Ensure<BorderStyle>().dimensions = SDL::FBox(w);      return this->_self(); }
		Derived& Borders(const SDL::FBox& b)     { this->template _Ensure<BorderStyle>().dimensions = b;                 return this->_self(); }
		Derived& Radius (float r)                { this->template _Ensure<BorderStyle>().radius     = SDL::FCorners(r);  return this->_self(); }
		Derived& Radius (const SDL::FCorners& r) { this->template _Ensure<BorderStyle>().radius     = r;                 return this->_self(); }

		template <typename F>
		Derived& WithBorder(F&& fn) {
			fn(this->template _Ensure<BorderStyle>());
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct TextStyleMixin : Base {
		using Base::Base;

		Derived& TextColor           (const SDL::Color& c) { this->template _Ensure<TextStyle>().color            = c; return this->_self(); }
		Derived& TextHoveredColor    (const SDL::Color& c) { this->template _Ensure<TextStyle>().hoveredColor     = c; return this->_self(); }
		Derived& TextPressedColor    (const SDL::Color& c) { this->template _Ensure<TextStyle>().selectedColor    = c; return this->_self(); }
		Derived& TextCheckedColor    (const SDL::Color& c) { this->template _Ensure<TextStyle>().checkedColor     = c; return this->_self(); }
		Derived& TextDisabledColor   (const SDL::Color& c) { this->template _Ensure<TextStyle>().disabledColor    = c; return this->_self(); }
		Derived& TextPlaceholderColor(const SDL::Color& c) { this->template _Ensure<TextStyle>().placeholderColor = c; return this->_self(); }

		Derived& FontKey (std::string_view key) { this->template _Ensure<TextStyle>().fontKey  = std::string(key); return this->_self(); }
		Derived& FontSize(float sz)             { this->template _Ensure<TextStyle>().fontSize = sz;               return this->_self(); }
		Derived& UseFont (FontType ft)          { this->template _Ensure<TextStyle>().usedFont = ft;               return this->_self(); }

		Derived& TextAlign (TextHAlign h, TextVAlign v) {
			auto& ts = this->template _Ensure<TextStyle>();
			ts.alignH = h; ts.alignV = v;
			return this->_self();
		}
		Derived& TextAlignH(TextHAlign h) { this->template _Ensure<TextStyle>().alignH = h; return this->_self(); }
		Derived& TextAlignV(TextVAlign v) { this->template _Ensure<TextStyle>().alignV = v; return this->_self(); }

		template <typename F>
		Derived& WithText(F&& fn) {
			fn(this->template _Ensure<TextStyle>());
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct SpacingMixin : Base {
		using Base::Base;

		Derived& Padding(float p) { this->template _Ensure<SpacingStyle>().padding = SDL::FBox(p); return this->_self(); }
		Derived& Padding(float h, float v) {
			auto& s = this->template _Ensure<SpacingStyle>();
			s.padding.left = s.padding.right = h;
			s.padding.top  = s.padding.bottom = v;
			return this->_self();
		}
		Derived& Padding(const SDL::FBox& pad) { this->template _Ensure<SpacingStyle>().padding = pad; return this->_self(); }
		Derived& PaddingH(float h) {
			auto& s = this->template _Ensure<SpacingStyle>();
			s.padding.left = s.padding.right = h;
			return this->_self();
		}
		Derived& PaddingV(float v) {
			auto& s = this->template _Ensure<SpacingStyle>();
			s.padding.top = s.padding.bottom = v;
			return this->_self();
		}

		Derived& Margin(float m) { this->template _Ensure<SpacingStyle>().margin = SDL::FBox(m); return this->_self(); }
		Derived& Margin(float h, float v) {
			auto& s = this->template _Ensure<SpacingStyle>();
			s.margin.left = s.margin.right = h;
			s.margin.top  = s.margin.bottom = v;
			return this->_self();
		}
		Derived& Margin(const SDL::FBox& mar) { this->template _Ensure<SpacingStyle>().margin = mar; return this->_self(); }
		Derived& MarginH(float h) {
			auto& s = this->template _Ensure<SpacingStyle>();
			s.margin.left = s.margin.right = h;
			return this->_self();
		}
		Derived& MarginV(float v) {
			auto& s = this->template _Ensure<SpacingStyle>();
			s.margin.top = s.margin.bottom = v;
			return this->_self();
		}

		Derived& Gap(float g) { this->template _Ensure<SpacingStyle>().gap = g; return this->_self(); }

		template <typename F>
		Derived& WithSpacing(F&& fn) {
			fn(this->template _Ensure<SpacingStyle>());
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct TransformMixin : Base {
		using Base::Base;
		Derived& Opacity(float a) {
			this->template _Ensure<TransformStyle>().opacity = SDL::Clamp(a, 0.f, 1.f);
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct AccentMixin : Base {
		using Base::Base;
		Derived& TrackColor    (const SDL::Color& c) { this->template _Ensure<AccentStyle>().trackColor     = c; return this->_self(); }
		Derived& FillColor     (const SDL::Color& c) { this->template _Ensure<AccentStyle>().fillColor      = c; return this->_self(); }
		Derived& ThumbColor    (const SDL::Color& c) { this->template _Ensure<AccentStyle>().thumbColor     = c; return this->_self(); }
		Derived& SeparatorColor(const SDL::Color& c) { this->template _Ensure<AccentStyle>().separatorColor = c; return this->_self(); }
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct SoundMixin : Base {
		using Base::Base;
		Derived& ClickSound (std::string_view k) { this->template _Ensure<SoundStyle>().clickKey  = std::string(k); return this->_self(); }
		Derived& HoverSound (std::string_view k) { this->template _Ensure<SoundStyle>().hoverKey  = std::string(k); return this->_self(); }
		Derived& ScrollSound(std::string_view k) { this->template _Ensure<SoundStyle>().scrollKey = std::string(k); return this->_self(); }
		Derived& ShowSound  (std::string_view k) { this->template _Ensure<SoundStyle>().showKey   = std::string(k); return this->_self(); }
		Derived& HideSound  (std::string_view k) { this->template _Ensure<SoundStyle>().hideKey   = std::string(k); return this->_self(); }
	};

	/// @brief Tooltip mixin — attaches a tooltip container+label sub-entity.
	///        The returned LabelBuilder lets you style the tooltip text independently.
	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct TooltipMixin : Base {
		using Base::Base;

		Derived& Tooltip(std::string_view text, float delay = 1.f) {
			this->system.EnsureTooltip(this->id, text, delay);
			return this->_self();
		}
		Derived& SetTooltipText   (std::string_view t)  { this->system.SetTooltipText(this->id, t);    return this->_self(); }
		Derived& SetTooltipDelay  (float delay)          { this->system.SetTooltipDelay(this->id, delay); return this->_self(); }
		Derived& SetTooltipVisible(bool v)               { this->system.SetTooltipVisible(this->id, v); return this->_self(); }
		Derived& ShowTooltip()                           { return SetTooltipVisible(true); }
		Derived& HideTooltip()                           { return SetTooltipVisible(false); }
		Derived& RemoveTooltip()                         { this->system.RemoveTooltip(this->id); return this->_self(); }

		Derived& TooltipBgColor  (const SDL::Color& c) { this->template _Ensure<TooltipStyle>().bgColor   = c; return this->_self(); }
		Derived& TooltipBdColor  (const SDL::Color& c) { this->template _Ensure<TooltipStyle>().bdColor   = c; return this->_self(); }
		Derived& TooltipTextColor(const SDL::Color& c) { this->template _Ensure<TooltipStyle>().textColor = c; return this->_self(); }

		/// @brief Access the tooltip's inner label builder to apply rich styles.
		///        Creates the tooltip sub-entity if it doesn't exist yet.
		template <typename F>
		Derived& WithTooltipLabel(F&& fn) {
			this->system.EnsureTooltip(this->id, "", 1.f);
			if (auto* td = this->system.GetCtx().template Get<TooltipData>(this->id)) {
				if (td->labelEntity != ECS::NullEntity) {
					LabelBuilder lb{this->system, td->labelEntity};
					fn(lb);
				}
			}
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct IconMixin : Base {
		using Base::Base;
		Derived& SetIcon(std::string_view key) {
			this->template _Ensure<IconData>().key = std::string(key);
			return this->_self();
		}
		Derived& IconPad(float p) {
			this->template _Ensure<IconData>().pad = p;
			return this->_self();
		}
	};

	// ==================================================================================
	// Content / data mixins
	// ==================================================================================

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct EditableMixin : Base {
		using Base::Base;

		Derived& SetText(std::string_view text) {
			if (auto* te = this->system.GetCtx().template Get<TextEdit>(this->id))
				te->text = std::string(text);
			return this->_self();
		}
		Derived& Placeholder(std::string_view ph) {
			if (auto* te = this->system.GetCtx().template Get<TextEdit>(this->id))
				te->placeholder = std::string(ph);
			return this->_self();
		}
		Derived& OnTextChange(std::function<void(const std::string&)> cb) {
			this->system.OnTextChange(this->id, std::move(cb));
			return this->_self();
		}
		Derived& ReadOnly(bool ro = true) {
			if (auto* te = this->system.GetCtx().template Get<TextEdit>(this->id))
				te->readOnly = ro;
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

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct NumericMixin : Base {
		using Base::Base;
		template <typename T> Derived& OnChange (std::function<void(T)> cb) { this->system.template OnChange<T>(this->id, std::move(cb));   return this->_self(); }
		template <typename T> Derived& Value    (T v)                       { this->system.template SetValue<T>(this->id, v);               return this->_self(); }
		template <typename T> Derived& MinValue (T v)                       { this->system.template SetMinValue<T>(this->id, v);            return this->_self(); }
		template <typename T> Derived& MaxValue (T v)                       { this->system.template SetMaxValue<T>(this->id, v);            return this->_self(); }
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct StateableMixin : Base {
		using Base::Base;
		Derived& Checked(bool b = true) { this->system.SetChecked(this->id, b); return this->_self(); }
		Derived& Unchecked()            { return Checked(false); }
		Derived& OnToggle(std::function<void(bool)> cb) {
			this->system.OnToggle(this->id, std::move(cb));
			return this->_self();
		}
	};

	template <typename Derived, typename Base = BuilderBase<Derived>>
	struct ImageMixin : Base {
		using Base::Base;
		Derived& ImageKey(std::string_view key, ImageFit fit = ImageFit::Contain) {
			this->system.SetImageKey(this->id, key, fit);
			return this->_self();
		}
	};

	// ==================================================================================
	// Typed builders — composed à la carte from the mixins above.
	// ==================================================================================

	// ── Separator ────────────────────────────────────────────────────────────────
	struct SeparatorBuilder:
		  SpacingMixin<SeparatorBuilder,
		  TransformMixin<SeparatorBuilder,
		  TooltipMixin<SeparatorBuilder>>>
	{
		using SpacingMixin::SpacingMixin;

		SeparatorBuilder& SeparatorThickness(float t) {
			this->template _Ensure<SeparatorData>().thickness = t;
			return this->_self();
		}
		SeparatorBuilder& SeparatorColor(const SDL::Color& c) {
			this->template _Ensure<SeparatorData>().color = c;
			return this->_self();
		}
		SeparatorBuilder& SeparatorOrientation(Orientation o) {
			this->template _Ensure<SeparatorData>().orientation = o;
			return this->_self();
		}
	};

	// ── Graph ─────────────────────────────────────────────────────────────────────
	struct GraphBuilder
		: AccentMixin<GraphBuilder,
		  SpacingMixin<GraphBuilder,
		  TransformMixin<GraphBuilder,
		  TooltipMixin<GraphBuilder>>>>
	{
		using AccentMixin::AccentMixin;
	};

	// ── Image ─────────────────────────────────────────────────────────────────────
	struct ImageBuilder
		: ImageMixin<ImageBuilder,
		  SpacingMixin<ImageBuilder,
		  TransformMixin<ImageBuilder,
		  TooltipMixin<ImageBuilder>>>>
	{
		using ImageMixin::ImageMixin;

		ImageBuilder& FitImage(ImageFit f) {
			this->template _Ensure<ImageData>().fit = f;
			return this->_self();
		}
	};

	// ── Label ─────────────────────────────────────────────────────────────────────
	/// @brief Label supports global text style + per-span rich styles.
	///        The label text may contain '\n' and '\t'; layout measures multi-line size.
	struct LabelBuilder
		: BackgroundMixin<LabelBuilder,
		  BorderMixin<LabelBuilder,
		  TextStyleMixin<LabelBuilder,
		  SpacingMixin<LabelBuilder,
		  TransformMixin<LabelBuilder,
		  EditableMixin<LabelBuilder,
		  TooltipMixin<LabelBuilder>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		// ── Rich-text span API ───────────────────────────────────────────────
		LabelBuilder& AddSpan(int start, int end, TextSpanStyle style) {
			this->template _Ensure<TextSpans>().Add(start, end, style);
			return this->_self();
		}
		LabelBuilder& ClearSpans() {
			if (auto* ts = this->system.GetCtx().template Get<TextSpans>(this->id))
				ts->Clear();
			return this->_self();
		}

		/// @brief Shortcut: colour a character range.
		LabelBuilder& SpanColor(int start, int end, SDL::Color c) {
			TextSpanStyle s; s.color = c;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: bold a character range.
		LabelBuilder& SpanBold(int start, int end) {
			TextSpanStyle s; s.bold = true;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: italic a character range.
		LabelBuilder& SpanItalic(int start, int end) {
			TextSpanStyle s; s.italic = true;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: underline a character range.
		LabelBuilder& SpanUnderline(int start, int end) {
			TextSpanStyle s; s.underline = true;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: strikethrough a character range.
		LabelBuilder& SpanStrike(int start, int end) {
			TextSpanStyle s; s.strikethrough = true;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: highlight a character range.
		LabelBuilder& SpanHighlight(int start, int end, SDL::Color hlColor = {255, 255, 100, 80}) {
			TextSpanStyle s; s.highlight = true; s.highlightColor = hlColor;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: reverse (inverted) text in a character range.
		LabelBuilder& SpanReversed(int start, int end) {
			TextSpanStyle s; s.reversed = true;
			return AddSpan(start, end, s);
		}
		/// @brief Shortcut: override font key/size for a character range.
		LabelBuilder& SpanFont(int start, int end, std::string_view fontKey, float fontSize = 0.f) {
			TextSpanStyle s; s.fontKey = std::string(fontKey); s.fontSize = fontSize;
			return AddSpan(start, end, s);
		}
	};

	// ── Button ────────────────────────────────────────────────────────────────────
	struct ButtonBuilder
		: BackgroundMixin<ButtonBuilder,
		  BorderMixin<ButtonBuilder,
		  TextStyleMixin<ButtonBuilder,
		  SpacingMixin<ButtonBuilder,
		  TransformMixin<ButtonBuilder,
		  IconMixin<ButtonBuilder,
		  SoundMixin<ButtonBuilder,
		  StateableMixin<ButtonBuilder,
		  EditableMixin<ButtonBuilder,
		  ClickableMixin<ButtonBuilder,
		  TooltipMixin<ButtonBuilder>>>>>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;
	};

	// ── Toggle / Radio ────────────────────────────────────────────────────────────
	struct ToggleBuilder
		: BackgroundMixin<ToggleBuilder,
		  BorderMixin<ToggleBuilder,
		  TextStyleMixin<ToggleBuilder,
		  SpacingMixin<ToggleBuilder,
		  TransformMixin<ToggleBuilder,
		  StateableMixin<ToggleBuilder,
		  EditableMixin<ToggleBuilder,
		  TooltipMixin<ToggleBuilder>>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;
	};

	struct RadioBuilder
		: BackgroundMixin<RadioBuilder,
		  BorderMixin<RadioBuilder,
		  TextStyleMixin<RadioBuilder,
		  SpacingMixin<RadioBuilder,
		  TransformMixin<RadioBuilder,
		  StateableMixin<RadioBuilder,
		  EditableMixin<RadioBuilder,
		  TooltipMixin<RadioBuilder>>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		RadioBuilder& Group(std::string_view grp) {
			if (auto* rb = this->system.GetCtx().template Get<RadioState>(this->id))
				rb->group = std::string(grp);
			return this->_self();
		}
	};

	// ── Container ─────────────────────────────────────────────────────────────────
	struct ContainerBuilder
		: BackgroundMixin<ContainerBuilder,
		  BorderMixin<ContainerBuilder,
		  SpacingMixin<ContainerBuilder,
		  TransformMixin<ContainerBuilder,
		  ScrollableMixin<ContainerBuilder,
		  TooltipMixin<ContainerBuilder>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ContainerBuilder& Layout(UI::Layout l) {
			this->system.GetCtx().template Get<LayoutProps>(this->id)->layout = l;
			return this->_self();
		}

		ContainerBuilder& ChildrenAlign(SDL::UI::Align h, SDL::UI::Align v) {
			auto& lp = *this->system.GetCtx().template Get<LayoutProps>(this->id);
			lp.alignChildrenH = h; lp.alignChildrenV = v;
			return this->_self();
		}

		ContainerBuilder& Child(ECS::EntityId e) {
			this->system.AppendChild(this->id, e);
			return this->_self();
		}
		template <typename C,
		          typename = std::enable_if_t<std::is_convertible_v<C, ECS::EntityId>>>
		ContainerBuilder& Child(C&& c) {
			this->system.AppendChild(this->id, static_cast<ECS::EntityId>(c));
			return this->_self();
		}
		ContainerBuilder& Child(ECS::EntityId e, int col, int row, int colSpan = 1, int rowSpan = 1) {
			this->system.AppendChild(this->id, e);
			this->system.SetGridCell(e, col, row, colSpan, rowSpan);
			return this->_self();
		}
		template <typename C>
		ContainerBuilder& Child(C&& c, int col, int row, int colSpan = 1, int rowSpan = 1) {
			ECS::EntityId eid = static_cast<ECS::EntityId>(c);
			this->system.AppendChild(this->id, eid);
			this->system.SetGridCell(eid, col, row, colSpan, rowSpan);
			return this->_self();
		}

		ContainerBuilder& GridCols        (int n)              { this->system.SetGridCols(this->id, n);          return this->_self(); }
		ContainerBuilder& GridRows        (int n)              { this->system.SetGridRows(this->id, n);          return this->_self(); }
		ContainerBuilder& GridColSizing   (GridSizing s)       { this->system.SetGridColSizing(this->id, s);     return this->_self(); }
		ContainerBuilder& GridRowSizing   (GridSizing s)       { this->system.SetGridRowSizing(this->id, s);     return this->_self(); }
		ContainerBuilder& GridLineStyle   (GridLines l)        { this->system.SetGridLines(this->id, l);         return this->_self(); }
		ContainerBuilder& GridLineColor   (const SDL::Color& c){ this->system.SetGridLineColor(this->id, c);     return this->_self(); }
		ContainerBuilder& GridLineThickness(float t)           { this->system.SetGridLineThickness(this->id, t); return this->_self(); }
	};

	// ── Slider / Knob / Progress / ScrollBar ──────────────────────────────────────
	struct SliderBuilder
		: BackgroundMixin<SliderBuilder,
		  BorderMixin<SliderBuilder,
		  SpacingMixin<SliderBuilder,
		  TransformMixin<SliderBuilder,
		  AccentMixin<SliderBuilder,
		  NumericMixin<SliderBuilder,
		  TooltipMixin<SliderBuilder>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		template <typename T>
		SliderBuilder& Markers(std::vector<T> m) {
			this->system.template SetSliderMarkers<T>(this->id, std::move(m));
			return this->_self();
		}
	};

	struct KnobBuilder
		: BackgroundMixin<KnobBuilder,
		  BorderMixin<KnobBuilder,
		  TransformMixin<KnobBuilder,
		  AccentMixin<KnobBuilder,
		  NumericMixin<KnobBuilder,
		  TooltipMixin<KnobBuilder>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		template <typename T>
		KnobBuilder& Markers(std::vector<T> m) {
			this->system.template SetKnobMarkers<T>(this->id, std::move(m));
			return this->_self();
		}
	};

	struct ProgressBuilder
		: BackgroundMixin<ProgressBuilder,
		  BorderMixin<ProgressBuilder,
		  TransformMixin<ProgressBuilder,
		  AccentMixin<ProgressBuilder,
		  NumericMixin<ProgressBuilder,
		  TooltipMixin<ProgressBuilder>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ProgressBuilder& Indeterminate(bool i = true) {
			if (auto* pd = this->system.GetCtx().template Get<ProgressData>(this->id))
				pd->isIndeterminate = i;
			return this->_self();
		}
	};

	struct ScrollBarBuilder
		: BackgroundMixin<ScrollBarBuilder,
		  AccentMixin<ScrollBarBuilder,
		  NumericMixin<ScrollBarBuilder,
		  TooltipMixin<ScrollBarBuilder>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ScrollBarBuilder& TrackSize(float s) {
			if (auto* sb = this->system.GetCtx().template Get<ScrollBarConfig>(this->id))
				sb->trackSize = s;
			return this->_self();
		}
		ScrollBarBuilder& OnScroll(std::function<void(float)> cb) {
			this->system.OnScroll(this->id, std::move(cb));
			return this->_self();
		}
	};

	// ── Input ─────────────────────────────────────────────────────────────────────
	struct InputBuilder
		: BackgroundMixin<InputBuilder,
		  BorderMixin<InputBuilder,
		  TextStyleMixin<InputBuilder,
		  SpacingMixin<InputBuilder,
		  TransformMixin<InputBuilder,
		  EditableMixin<InputBuilder,
		  NumericMixin<InputBuilder,
		  TooltipMixin<InputBuilder>>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		InputBuilder& InputFilter(SDL::UI::InputFilterType filter) {
			if (auto* in = this->system.GetCtx().template Get<InputData>(this->id))
				in->filter = filter;
			return this->_self();
		}
		InputBuilder& InputCustomFilter(std::function<bool(char)> filterFn) {
			if (auto* in = this->system.GetCtx().template Get<InputData>(this->id))
				in->customFilter = std::move(filterFn);
			return this->_self();
		}
		InputBuilder& InputFormat(std::string_view format = "{}") {
			if (auto* in = this->system.GetCtx().template Get<InputData>(this->id))
				in->format = std::string(format);
			return this->_self();
		}
		InputBuilder& PassworldMode(bool v = true) {
			if (auto* in = this->system.GetCtx().template Get<InputData>(this->id))
				in->passwordMode = v;
			return this->_self();
		}
		InputBuilder& OnIncrement(std::function<void()> cb) {
			if (auto* in = this->system.GetCtx().template Get<InputData>(this->id))
				in->onIncrement = std::move(cb);
			return this->_self();
		}
		InputBuilder& OnDecrement(std::function<void()> cb) {
			if (auto* in = this->system.GetCtx().template Get<InputData>(this->id))
				in->onDecrement = std::move(cb);
			return this->_self();
		}

	};

	// ── TextArea ──────────────────────────────────────────────────────────────────
	struct TextAreaBuilder
		: BackgroundMixin<TextAreaBuilder,
		  BorderMixin<TextAreaBuilder,
		  TextStyleMixin<TextAreaBuilder,
		  SpacingMixin<TextAreaBuilder,
		  TransformMixin<TextAreaBuilder,
		  ScrollableMixin<TextAreaBuilder,
		  EditableMixin<TextAreaBuilder,
		  GridChildMixin<TextAreaBuilder,
		  TooltipMixin<TextAreaBuilder>>>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		TextAreaBuilder& AddSpan(int start, int end, TextSpanStyle style) {
			this->system.AddTextAreaSpan(this->id, start, end, style);
			return this->_self();
		}
		TextAreaBuilder& ClearSpans() {
			this->system.ClearTextAreaSpans(this->id);
			return this->_self();
		}
		TextAreaBuilder& TabSize(int n) {
			this->system.SetTextAreaTabSize(this->id, n);
			return this->_self();
		}
	};

	// ── ListBox ───────────────────────────────────────────────────────────────────
	/// @brief ListBox composed of a scroll view + per-item button entities.
	struct ListBoxBuilder
		: BackgroundMixin<ListBoxBuilder,
		  BorderMixin<ListBoxBuilder,
		  TextStyleMixin<ListBoxBuilder,
		  SpacingMixin<ListBoxBuilder,
		  TransformMixin<ListBoxBuilder,
		  ClickableMixin<ListBoxBuilder,
		  ScrollableMixin<ListBoxBuilder,
		  TooltipMixin<ListBoxBuilder>>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ListBoxBuilder& Items   (std::vector<std::string> items) { this->system.SetListBoxItems(this->id, std::move(items)); return this->_self(); }
		ListBoxBuilder& Selected(int idx)                        { this->system.SetListBoxSelection(this->id, idx);          return this->_self(); }
		ListBoxBuilder& Reorderable(bool v = true)               { this->system.SetListBoxReorderable(this->id, v);          return this->_self(); }
		ListBoxBuilder& OnReorder(std::function<void(int, int)> cb) {
			this->system.SetListBoxOnReorder(this->id, std::move(cb));
			return this->_self();
		}

		/// @brief Apply a style callback to every item button (called per entity).
		template <typename F>
		ListBoxBuilder& WithItemStyle(F&& fn) {
			this->system.ApplyListBoxItemStyle(this->id, std::forward<F>(fn));
			return this->_self();
		}
	};

	// ── ComboBox ──────────────────────────────────────────────────────────────────
	/// @brief ComboBox composed of a toggle button + overlay with item buttons.
	struct ComboBoxBuilder
		: BackgroundMixin<ComboBoxBuilder,
		  BorderMixin<ComboBoxBuilder,
		  TextStyleMixin<ComboBoxBuilder,
		  SpacingMixin<ComboBoxBuilder,
		  TransformMixin<ComboBoxBuilder,
		  TooltipMixin<ComboBoxBuilder>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ComboBoxBuilder& Items   (std::vector<std::string> items) { this->system.SetComboBoxItems(this->id, std::move(items)); return this->_self(); }
		ComboBoxBuilder& Selected(int idx)                        { this->system.SetComboBoxSelection(this->id, idx);          return this->_self(); }

		template <typename T>
		ComboBoxBuilder& OnChange(std::function<void(T)> cb) {
			this->system.template OnChange<T>(this->id, std::move(cb));
			return this->_self();
		}

		/// @brief Style the toggle button.
		template <typename F>
		ComboBoxBuilder& WithToggleButton(F&& fn) {
			if (auto* cb = this->system.GetCtx().template Get<ComboBoxData>(this->id)) {
				if (cb->toggleButton != ECS::NullEntity) {
					ButtonBuilder bb{this->system, cb->toggleButton};
					fn(bb);
				}
			}
			return this->_self();
		}

		/// @brief Style the dropdown overlay container.
		template <typename F>
		ComboBoxBuilder& WithOverlay(F&& fn) {
			if (auto* cb = this->system.GetCtx().template Get<ComboBoxData>(this->id)) {
				if (cb->overlay != ECS::NullEntity) {
					ContainerBuilder ob{this->system, cb->overlay};
					fn(ob);
				}
			}
			return this->_self();
		}
	};

	// ── Tree ──────────────────────────────────────────────────────────────────────
	/// @brief Tree composed of a scroll container with row button entities per node.
	struct TreeBuilder
		: BackgroundMixin<TreeBuilder,
		  BorderMixin<TreeBuilder,
		  TextStyleMixin<TreeBuilder,
		  SpacingMixin<TreeBuilder,
		  TransformMixin<TreeBuilder,
		  ScrollableMixin<TreeBuilder,
		  TooltipMixin<TreeBuilder>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

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
			if (auto* t = this->system.GetCtx().template Get<TreeData>(this->id))
				t->onToggleNode = std::move(cb);
			return this->_self();
		}
	};

	// ── Canvas ────────────────────────────────────────────────────────────────────
	struct CanvasBuilder
		: SpacingMixin<CanvasBuilder,
		  TransformMixin<CanvasBuilder,
		  TooltipMixin<CanvasBuilder>>>
	{
		using SpacingMixin::SpacingMixin;

		CanvasBuilder& OnRender(std::function<void(SDL::RendererRef, const SDL::FRect&)> cb) {
			if (auto* c = this->system.GetCtx().template Get<CanvasData>(this->id))
				c->renderCb = std::move(cb);
			return this->_self();
		}
		CanvasBuilder& OnEvent(std::function<void(SDL::Event&)> cb) {
			if (auto* c = this->system.GetCtx().template Get<CanvasData>(this->id))
				c->eventCb = std::move(cb);
			return this->_self();
		}
		CanvasBuilder& OnUpdate(std::function<void(float)> cb) {
			if (auto* c = this->system.GetCtx().template Get<CanvasData>(this->id))
				c->updateCb = std::move(cb);
			return this->_self();
		}
	};

	// ── Spinner / Splitter / Badge ────────────────────────────────────────────────
	struct SpinnerBuilder
		: TransformMixin<SpinnerBuilder,
		  AccentMixin<SpinnerBuilder,
		  TooltipMixin<SpinnerBuilder>>>
	{
		using TransformMixin::TransformMixin;

		SpinnerBuilder& Speed    (float s) { if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->speed     = s; return this->_self(); }
		SpinnerBuilder& ArcSpan  (float a) { if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->arcSpan   = a; return this->_self(); }
		SpinnerBuilder& Thickness(float t) { if (auto* sd = this->system.GetCtx().template Get<SpinnerData>(this->id)) sd->thickness = t; return this->_self(); }
	};

	struct SplitterBuilder
		: BackgroundMixin<SplitterBuilder,
		  TransformMixin<SplitterBuilder,
		  TooltipMixin<SplitterBuilder>>>
	{
		using BackgroundMixin::BackgroundMixin;

		SplitterBuilder& Ratio   (float r) { if (auto* ss = this->system.GetCtx().template Get<SplitterState>(this->id))  ss->ratio    = r; return this->_self(); }
		SplitterBuilder& MinRatio(float r) { if (auto* sc = this->system.GetCtx().template Get<SplitterConfig>(this->id)) sc->minRatio = r; return this->_self(); }
		SplitterBuilder& MaxRatio(float r) { if (auto* sc = this->system.GetCtx().template Get<SplitterConfig>(this->id)) sc->maxRatio = r; return this->_self(); }
		SplitterBuilder& OnChange(std::function<void(float)> cb) {
			this->system.OnChange(this->id, std::move(cb));
			return this->_self();
		}
	};

	struct BadgeBuilder
		: BackgroundMixin<BadgeBuilder,
		  TextStyleMixin<BadgeBuilder,
		  SpacingMixin<BadgeBuilder,
		  TransformMixin<BadgeBuilder,
		  ImageMixin<BadgeBuilder,
		  EditableMixin<BadgeBuilder,
		  TooltipMixin<BadgeBuilder>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		BadgeBuilder& Variant(std::string_view v) {
			if (auto* bd = this->system.GetCtx().template Get<BadgeData>(this->id))
				bd->variant = std::string(v);
			return this->_self();
		}
	};

	// ── ColorPicker ───────────────────────────────────────────────────────────────
	struct ColorPickerBuilder
		: BackgroundMixin<ColorPickerBuilder,
		  BorderMixin<ColorPickerBuilder,
		  SpacingMixin<ColorPickerBuilder,
		  TransformMixin<ColorPickerBuilder,
		  TooltipMixin<ColorPickerBuilder>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ColorPickerBuilder& OnColorChange(std::function<void(SDL::Color)> cb) {
			this->system.OnColorChange(this->id, std::move(cb));
			return this->_self();
		}
		ColorPickerBuilder& AllowAlpha(bool a = true) {
			if (auto* cp = this->system.GetCtx().template Get<ColorPickerConfig>(this->id))
				cp->allowAlpha = a;
			return this->_self();
		}
		ColorPickerBuilder& PickedColor(SDL::Color c)  { this->system.SetPickedColor(this->id, c); return this->_self(); }
		ColorPickerBuilder& PickerColorA(SDL::Color c) { if (auto* cp = this->system.GetCtx().template Get<ColorPickerConfig>(this->id)) cp->colorA = c; return this->_self(); }
		ColorPickerBuilder& PickerColorB(SDL::Color c) { if (auto* cp = this->system.GetCtx().template Get<ColorPickerConfig>(this->id)) cp->colorB = c; return this->_self(); }
	};

	// ── Popup ─────────────────────────────────────────────────────────────────────
	struct PopupBuilder
		: BackgroundMixin<PopupBuilder,
		  BorderMixin<PopupBuilder,
		  SpacingMixin<PopupBuilder,
		  TransformMixin<PopupBuilder,
		  ScrollableMixin<PopupBuilder,
		  DraggableMixin<PopupBuilder,
		  TooltipMixin<PopupBuilder>>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		PopupBuilder& Resizable(bool r = true) { if (auto* p = this->system.GetCtx().template Get<PopupConfig>(this->id)) p->resizable = r; return this->_self(); }
		PopupBuilder& Modal    (bool m = true) { if (auto* p = this->system.GetCtx().template Get<PopupConfig>(this->id)) p->modal     = m; return this->_self(); }
		PopupBuilder& OnClose  (std::function<void()> cb) {
			if (auto* p = this->system.GetCtx().template Get<PopupConfig>(this->id)) p->onClose = std::move(cb);
			return this->_self();
		}
		PopupBuilder& PopupOpen(bool v = true) { this->system.SetPopupOpen(this->id, v); return this->_self(); }
	};

	// ── TabView ───────────────────────────────────────────────────────────────────
	/// @brief TabView composed of a tab-bar row of buttons + content area.
	struct TabViewBuilder
		: BackgroundMixin<TabViewBuilder,
		  BorderMixin<TabViewBuilder,
		  SpacingMixin<TabViewBuilder,
		  TransformMixin<TabViewBuilder,
		  TooltipMixin<TabViewBuilder>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		TabViewBuilder& TabLocation(SDL::UI::TabLocation l) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id))
				tv->tabLocation = l;
			return this->_self();
		}
		TabViewBuilder& OnTabChange(std::function<void(int)> cb) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id))
				tv->onTabChange = std::move(cb);
			return this->_self();
		}
		/// @brief Add a tab and return a ContainerBuilder for its content area.
		///        Call this after the TabView is created; sub-entities must exist.
		TabViewBuilder& AddTab(std::string_view label, bool closable = false) {
			this->system.AddTabViewTab(this->id, label, closable);
			return this->_self();
		}
		/// @brief Style the tab-bar container.
		template <typename F>
		TabViewBuilder& WithTabBar(F&& fn) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id)) {
				if (tv->tabBar != ECS::NullEntity) {
					ContainerBuilder cb{this->system, tv->tabBar};
					fn(cb);
				}
			}
			return this->_self();
		}
		/// @brief Style the content area container.
		template <typename F>
		TabViewBuilder& WithContentArea(F&& fn) {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id)) {
				if (tv->contentArea != ECS::NullEntity) {
					ContainerBuilder cb{this->system, tv->contentArea};
					fn(cb);
				}
			}
			return this->_self();
		}
		/// @brief Access the content container of tab at @p index.
		[[nodiscard]] ECS::EntityId GetTabContent(int index) const {
			if (auto* tv = this->system.GetCtx().template Get<TabViewData>(this->id)) {
				if (index >= 0 && index < (int)tv->tabs.size())
					return tv->tabs[index].tabContent;
			}
			return ECS::NullEntity;
		}
	};

	// ── Expander ──────────────────────────────────────────────────────────────────
	/// @brief Expander composed of a header button + collapsible content container.
	struct ExpanderBuilder
		: BackgroundMixin<ExpanderBuilder,
		  BorderMixin<ExpanderBuilder,
		  SpacingMixin<ExpanderBuilder,
		  TransformMixin<ExpanderBuilder,
		  TooltipMixin<ExpanderBuilder>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		ExpanderBuilder& Expanded(bool e = true) { this->system.SetExpanderExpanded(this->id, e); return this->_self(); }
		ExpanderBuilder& OnToggle(std::function<void(bool)> cb) {
			this->system.OnExpanderToggle(this->id, std::move(cb));
			return this->_self();
		}

		/// @brief Style the header button (shows label + expand arrow).
		template <typename F>
		ExpanderBuilder& WithHeader(F&& fn) {
			if (auto* ed = this->system.GetCtx().template Get<ExpanderData>(this->id)) {
				if (ed->headerButton != ECS::NullEntity) {
					ButtonBuilder bb{this->system, ed->headerButton};
					fn(bb);
				}
			}
			return this->_self();
		}

		/// @brief Style or populate the collapsible content container.
		template <typename F>
		ExpanderBuilder& WithContent(F&& fn) {
			if (auto* ed = this->system.GetCtx().template Get<ExpanderData>(this->id)) {
				if (ed->contentEntity != ECS::NullEntity) {
					ContainerBuilder cb{this->system, ed->contentEntity};
					fn(cb);
				}
			}
			return this->_self();
		}

		/// @brief Returns the content container entity for direct child attachment.
		[[nodiscard]] ECS::EntityId GetContentEntity() const {
			if (auto* ed = this->system.GetCtx().template Get<ExpanderData>(this->id))
				return ed->contentEntity;
			return ECS::NullEntity;
		}
	};

	// ── MenuBar ───────────────────────────────────────────────────────────────────
	/// @brief MenuBar composed of menu-title buttons + per-menu overlay with item buttons.
	struct MenuBarBuilder
		: BackgroundMixin<MenuBarBuilder,
		  BorderMixin<MenuBarBuilder,
		  TextStyleMixin<MenuBarBuilder,
		  SpacingMixin<MenuBarBuilder,
		  TransformMixin<MenuBarBuilder,
		  TooltipMixin<MenuBarBuilder>>>>>>
	{
		using BackgroundMixin::BackgroundMixin;

		MenuBarBuilder& AddMenu(MenuBarMenu menu) {
			this->system.AddMenuBarMenu(this->id, std::move(menu));
			return this->_self();
		}
		MenuBarBuilder& OnItemSelect(std::function<void(int)> cb) {
			if (auto* mb = this->system.GetCtx().template Get<MenuBarData>(this->id))
				mb->onItemSelect = std::move(cb);
			return this->_self();
		}
	};

	// ── ItemBuilder (list rows / menu items) ─────────────────────────────────────
	struct ItemBuilder
		: TextStyleMixin<ItemBuilder,
		  SpacingMixin<ItemBuilder,
		  IconMixin<ItemBuilder,
		  SelectableMixin<ItemBuilder,
		  TooltipMixin<ItemBuilder>>>>>
	{
		using TextStyleMixin::TextStyleMixin;
	};

	// ==================================================================================
	// Theme — predefined style presets as composable lambdas
	// ==================================================================================

	namespace Theme {

		inline auto PrimaryButton(SDL::Color color) {
			SDL::Color bdColor = color.Brighten(40);
			return [color, bdColor](auto& builder) mutable {
				builder
					.BgColor         (color)
					.BgHoveredColor  (color.Brighten(25))
					.BgPressedColor  (color.Darken(35))
					.BgDisabledColor (color.Darken(50).SetA(160))
					.BdColor         (bdColor)
					.BdHoveredColor  (bdColor)
					.BdPressedColor  (bdColor)
					.BdDisabledColor (bdColor.Darken(50))
					.TextColor       ({235, 235, 245, 255})
					.TextHoveredColor({255, 255, 255, 255})
					.TextPressedColor({255, 255, 255, 255})
					.Borders         (1.f)
					.Radius          (6.f);
			};
		}

		inline auto GhostButton() {
			SDL::Color bdColor = {70, 75, 100, 200};
			return [bdColor](auto& builder) mutable {
				builder
					.BgColor        ({0, 0, 0, 0})
					.BgHoveredColor ({50, 55, 80, 80})
					.BgPressedColor ({20, 22, 36, 100})
					.BdColor        (bdColor)
					.BdHoveredColor (bdColor)
					.BdPressedColor (bdColor)
					.BdDisabledColor(bdColor.Darken(50).SetA(160))
					.TextColor      ({235, 235, 245, 255})
					.Borders        (1.f)
					.Radius         (6.f);
			};
		}

		inline auto Transparent() {
			return [](auto& builder) {
				builder
					.BgColor       ({0, 0, 0, 0})
					.BgHoveredColor({0, 0, 0, 0})
					.BgPressedColor({0, 0, 0, 0})
					.Radius        (0.f)
					.Borders       (0.f);
			};
		}

		inline auto Card() {
			return [](auto& builder) {
				builder
					.BgColor({26, 28, 40, 255})
					.BdColor({50, 54, 78, 255})
					.Borders(1.f)
					.Radius (8.f);
			};
		}

		inline auto CardLight() {
			SDL::Color bdColor = {210, 213, 225, 255};
			return [bdColor](auto& builder) mutable {
				builder
					.BgColor({248, 249, 252, 255})
					.BgHoveredColor({235, 238, 248, 255})
					.BgPressedColor({215, 220, 238, 255})
					.BdColor(bdColor)
					.BdHoveredColor(bdColor)
					.BdPressedColor(bdColor)
					.TextColor({30, 32, 42, 255})
					.TextHoveredColor({10, 12, 22, 255})
					.TextDisabledColor({160, 162, 175, 200})
					.TextPlaceholderColor({160, 162, 175, 180})
					.Borders(1.f)
					.Radius (8.f);
			};
		}

	} // namespace Theme

	// ==================================================================================
	// Builder Factory Method Implementations (deferred — Context must be complete)
	// ==================================================================================

	inline ContainerBuilder Context::Container() {
		return ContainerBuilder{*this, MakeContainer()};
	}

	inline LabelBuilder Context::Label(std::string_view text) {
		return LabelBuilder{*this, MakeLabel(text)};
	}

	inline ButtonBuilder Context::Button(std::string_view text) {
		return ButtonBuilder{*this, MakeButton(text)};
	}

	inline ToggleBuilder Context::Toggle(std::string_view text) {
		return ToggleBuilder{*this, MakeToggle(text)};
	}

	inline RadioBuilder Context::Radio(std::string_view group, std::string_view text) {
		return RadioBuilder{*this, MakeRadio(group, text)};
	}

	inline ScrollBarBuilder Context::ScrollBar(float contentSize, float viewSize, Orientation o) {
		return ScrollBarBuilder{*this, MakeScrollBar(contentSize, viewSize, 10.f, o)};
	}

	inline SeparatorBuilder Context::Separator() {
		return SeparatorBuilder{*this, MakeSeparator()};
	}

	inline InputBuilder Context::Input(std::string_view placeholder) {
		return InputBuilder{*this, MakeInput(placeholder)};
	}

	inline ImageBuilder Context::Image(std::string_view key, ImageFit fit) {
		return ImageBuilder{*this, MakeImage(key, fit)};
	}

	inline CanvasBuilder Context::Canvas(std::function<void(SDL::Event&)> eventCb,
	                                          std::function<void(float)> updateCb,
	                                          std::function<void(RendererRef, FRect)> renderCb) {
		return CanvasBuilder{*this, MakeCanvas(std::move(eventCb), std::move(updateCb), std::move(renderCb))};
	}

	inline TextAreaBuilder Context::TextArea(std::string_view text, std::string_view placeholder) {
		return TextAreaBuilder{*this, MakeTextArea(text, placeholder)};
	}

	inline ListBoxBuilder Context::ListBoxWidget(const std::vector<std::string>& items) {
		return ListBoxBuilder{*this, MakeListBox(items)};
	}

	inline GraphBuilder Context::GradedGraph() {
		return GraphBuilder{*this, MakeGraph()};
	}

	inline ComboBoxBuilder Context::ComboBox(const std::vector<std::string>& items, int sel) {
		return ComboBoxBuilder{*this, MakeComboBox(items, sel)};
	}

	inline TabViewBuilder Context::TabView() {
		return TabViewBuilder{*this, MakeTabView()};
	}

	inline ExpanderBuilder Context::Expander(std::string_view label, bool expanded) {
		return ExpanderBuilder{*this, MakeExpander(label, expanded)};
	}

	inline SplitterBuilder Context::Splitter(Orientation o, float ratio) {
		return SplitterBuilder{*this, MakeSplitter(o, ratio)};
	}

	inline SpinnerBuilder Context::Spinner(float speed) {
		return SpinnerBuilder{*this, MakeSpinner(speed)};
	}

	inline BadgeBuilder Context::Badge(std::string_view text) {
		return BadgeBuilder{*this, MakeBadge(text)};
	}

	inline ColorPickerBuilder Context::ColorPicker(ColorPickerPalette palette, float step) {
		return ColorPickerBuilder{*this, MakeColorPicker(palette, step)};
	}

	inline PopupBuilder Context::Popup(std::string_view title,
	                                  bool closable, bool draggable, bool resizable) {
		return PopupBuilder{*this, MakePopup(title, closable, draggable, resizable)};
	}

	inline TreeBuilder Context::Tree() {
		return TreeBuilder{*this, MakeTree()};
	}

	inline MenuBarBuilder Context::MenuBar() {
		return MenuBarBuilder{*this, MakeMenuBar()};
	}

} // namespace SDL::UI
