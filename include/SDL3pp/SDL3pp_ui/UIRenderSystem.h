#pragma once

#include "UIComponents.h"
#include "UIIndexSystem.h"
#include "../SDL3pp_engine/ECS.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_resources.h"
#include "../SDL3pp_ttf.h"

#include <functional>
#include <optional>
#include <typeindex>
#include <unordered_map>

namespace SDL::UI {

	class Context;

	// ==================================================================================
	// DrawCtx — per-entity resolved style context for rendering
	// ==================================================================================
	// Aggregates all style components so draw functions don't need multiple ctx lookups.

	struct DrawCtx {
		WidgetStateFlag       states    = WidgetStateFlag::None;
		const BackgroundStyle* bg       = nullptr;
		const BorderStyle*     bd       = nullptr;
		const TextStyle*       ts       = nullptr;
		const AccentStyle*     ac       = nullptr;
		const SpacingStyle*    sp       = nullptr;
		const ScrollbarStyle*  sb       = nullptr;

		// ── Eased transition factors [0,1] (from AnimationState; else hard 0/1) ──
		float hoverT   = 0.f;
		float pressT   = 0.f;
		float focusT   = 0.f;
		float checkedT = 0.f;
		float opacity  = 1.f;   ///< Global multiplier on every colour's alpha.

		// Composite the per-state colours in priority order (low→high): a factor of
		// 0 leaves the colour untouched and 1 fully selects it, so when factors are
		// hard 0/1 this reproduces the original discrete state precedence exactly,
		// while intermediate values cross-fade (CSS-like :hover/:active transitions).
		[[nodiscard]] static SDL::Color _Mix(SDL::Color a, SDL::Color b, float t) noexcept {
			auto m = [t](Uint8 x, Uint8 y) {
				float v = x + (int(y) - int(x)) * t + 0.5f;   // t may overshoot (EaseOutBack)
				return (Uint8)(v < 0.f ? 0.f : (v > 255.f ? 255.f : v));
			};
			return {m(a.r, b.r), m(a.g, b.g), m(a.b, b.b), m(a.a, b.a)};
		}
		[[nodiscard]] SDL::Color _Op(SDL::Color c) const noexcept {
			c.a = (Uint8)(c.a * opacity + 0.5f);
			return c;
		}

		// ── Convenience accessors with defaults ─────────────────────────────────
		[[nodiscard]] SDL::Color BgColor() const noexcept {
			if (!bg) return _Op({22, 22, 30, 255});
			SDL::Color c = bg->color;
			c = _Mix(c, bg->checkedColor,  checkedT);
			c = _Mix(c, bg->hoveredColor,  hoverT);
			c = _Mix(c, bg->focusedColor,  focusT);
			c = _Mix(c, bg->selectedColor, pressT);
			return _Op(c);
		}
		[[nodiscard]] SDL::Color BdColor() const noexcept {
			if (!bd) return _Op({55, 58, 78, 255});
			SDL::Color c = bd->color;
			c = _Mix(c, bd->hoveredColor, hoverT);
			c = _Mix(c, bd->focusedColor, focusT);
			return _Op(c);
		}
		[[nodiscard]] SDL::Color TextColor() const noexcept {
			if (!ts) return _Op({215, 215, 220, 255});
			return _Op(_Mix(ts->color, ts->hoveredColor, hoverT));
		}
		[[nodiscard]] SDL::FCorners Radius() const noexcept {
			return bd ? bd->radius : SDL::FCorners{5.f, 5.f, 5.f, 5.f};
		}
		[[nodiscard]] SDL::FBox Borders() const noexcept {
			return bd ? bd->dimensions : SDL::FBox{1.f, 1.f, 1.f, 1.f};
		}
		[[nodiscard]] SDL::FBox Padding() const noexcept {
			return sp ? sp->padding : SDL::FBox{8.f, 6.f, 8.f, 6.f};
		}
		[[nodiscard]] float ScrollbarThickness() const noexcept {
			return sb ? sb->thickness : 8.f;
		}
		[[nodiscard]] SDL::Color TrackColor() const noexcept {
			return ac ? ac->trackColor : SDL::Color{42, 44, 58, 255};
		}
		[[nodiscard]] SDL::Color FillColor() const noexcept {
			return ac ? ac->fillColor : SDL::Color{70, 130, 210, 255};
		}
		[[nodiscard]] SDL::Color ThumbColor() const noexcept {
			return ac ? ac->thumbColor : SDL::Color{100, 160, 230, 255};
		}
	};

	// ==================================================================================
	// WidgetRenderRegistry — extension point for widget-specific rendering
	// ==================================================================================

	using RenderFn = std::function<void(ECS::EntityId, RendererRef, const DrawCtx&,
	                                     const FRect&, ECS::Context&)>;

	class WidgetRenderRegistry {
	public:
		template <typename T>
		void Register(RenderFn fn) {
			m_renderers[std::type_index(typeid(T))] = std::move(fn);
		}

		bool Dispatch(ECS::EntityId e, RendererRef r, const DrawCtx& dctx,
		              const FRect& rect, ECS::Context& ctx) const;

		template <typename T>
		void RegisterByPresence(RenderFn fn);

	private:
		std::unordered_map<std::type_index, RenderFn> m_renderers;
		std::vector<std::pair<std::type_index, std::function<bool(ECS::EntityId, ECS::Context&)>>> m_presenceCheckers;

		template <typename T>
		static bool _HasComponent(ECS::EntityId e, ECS::Context& ctx) {
			return ctx.Get<T>(e) != nullptr;
		}
	};

	inline bool WidgetRenderRegistry::Dispatch(ECS::EntityId e, RendererRef r, const DrawCtx& dctx,
	                                            const FRect& rect, ECS::Context& ctx) const {
		for (const auto& [ti, checker] : m_presenceCheckers) {
			if (checker(e, ctx)) {
				auto it = m_renderers.find(ti);
				if (it != m_renderers.end()) {
					it->second(e, r, dctx, rect, ctx);
					return true;
				}
			}
		}
		return false;
	}

	template <typename T>
	void WidgetRenderRegistry::RegisterByPresence(RenderFn fn) {
		m_renderers[std::type_index(typeid(T))] = std::move(fn);
		m_presenceCheckers.emplace_back(std::type_index(typeid(T)),
		                                &WidgetRenderRegistry::_HasComponent<T>);
	}

	// ==================================================================================
	// RenderSystem — render + animate pipeline
	// ==================================================================================

	class RenderSystem {
	public:
		RenderSystem(ECS::Context& ctx, RendererRef renderer, ResourcePool& pool,
		               IndexSystem& index, Context& sys);
		~RenderSystem();

		void Process       (ECS::EntityId root, ECS::EntityId focused);
		void ProcessAnimate(float dt, ECS::EntityId root);

		[[nodiscard]] WidgetRenderRegistry&       GetRegistry()       noexcept { return m_registry; }
		[[nodiscard]] const WidgetRenderRegistry& GetRegistry() const noexcept { return m_registry; }

	private:
		ECS::Context&        m_ctx;
		RendererRef          m_renderer;
		ResourcePool&        m_pool;
		IndexSystem&         m_index;
		Context&              m_sys;
		WidgetRenderRegistry m_registry;

#if defined(SDL3PP_ENABLE_TTF)
		std::optional<SDL::RendererTextEngine> m_engine;
#endif

		DrawCtx _MakeCtx(ECS::EntityId e) const noexcept;

		void _DrawWidget    (ECS::EntityId e, const DrawCtx& dctx, const FRect& rect);
		void _DrawBackground(const FRect& rect, const DrawCtx& dctx);
		void _DrawText      (ECS::EntityId e, const std::string& text, const FRect& rect,
		                     TextHAlign hAlign = TextHAlign::Center, TextVAlign vAlign = TextVAlign::Center);

		void _RegisterBuiltinRenderers();

		void _DrawLabel    (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawButton   (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawSlider   (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawProgress (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawKnob     (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawToggle   (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawRadio    (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawInput    (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawTextArea (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawListBox  (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawImage    (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);
		void _DrawCanvas   (ECS::EntityId e, const FRect& rect, const DrawCtx& dctx);

		// ── Rendering helpers ────────────────────────────────────────────────────────
		void _FillRect (const FRect& r, SDL::Color c, float op = 1.f);
		void _FillRR   (const FRect& r, SDL::Color c, float radius, float op = 1.f);
		void _StrokeRR (const FRect& r, SDL::Color c, const SDL::FBox& borders, float radius, float op = 1.f);
		void _DrawScrollBar(const FRect& rect, Orientation orient, float contentSize, float viewSize, float offset, const DrawCtx& dctx);
		void _DrawHueBar(const FRect& rect, float op = 1.f);

		struct ScrollViewInfo {
			float innerW = 0.f, innerH = 0.f;
			float viewW = 0.f, viewH = 0.f;
			bool showX = false, showY = false;
		};
		ScrollViewInfo _ComputeScrollView(const FRect& r, const LayoutProps& lp,
		                                  const Widget& w, const DrawCtx& dctx) const;
	};

	// ==================================================================================
	// Implementation: RenderSystem
	// ==================================================================================

	inline RenderSystem::RenderSystem(ECS::Context& ctx, RendererRef renderer, ResourcePool& pool,
	                                       IndexSystem& index, Context& sys)
		: m_ctx(ctx), m_renderer(renderer), m_pool(pool), m_index(index), m_sys(sys) {
		_RegisterBuiltinRenderers();
	}

	inline RenderSystem::~RenderSystem() {
#if defined(SDL3PP_ENABLE_TTF)
		m_ctx.Each<TextCache>([](ECS::EntityId, TextCache &tc) {
			tc.text = SDL::Text{};
		});
#endif
	}

	inline DrawCtx RenderSystem::_MakeCtx(ECS::EntityId e) const noexcept {
		DrawCtx dctx;
		WidgetStateFlag st = WidgetStateFlag::None;
		if (auto* w = m_ctx.Get<Widget>(e)) { dctx.states = w->states; st = w->states; }
		dctx.bg = m_ctx.Get<BackgroundStyle>(e);
		dctx.bd = m_ctx.Get<BorderStyle>(e);
		dctx.ts = m_ctx.Get<TextStyle>(e);
		dctx.ac = m_ctx.Get<AccentStyle>(e);
		dctx.sp = m_ctx.Get<SpacingStyle>(e);
		dctx.sb = m_ctx.Get<ScrollbarStyle>(e);

		// Transition factors: eased progress if the widget is animated, else a hard
		// 0/1 snapshot of the current state (identical to the pre-animation look).
		if (auto* an = m_ctx.Get<AnimationState>(e)) {
			Easing ease = Easing::EaseOut;
			if (auto* as = m_ctx.Get<AnimationStyle>(e)) ease = as->easing;
			dctx.hoverT   = EaseEval(ease, an->hoverT);
			dctx.pressT   = EaseEval(ease, an->pressT);
			dctx.focusT   = EaseEval(ease, an->focusT);
			dctx.checkedT = EaseEval(ease, an->checkedT);
			dctx.opacity  = an->opacity;
		} else {
			dctx.hoverT   = Has(st, WidgetStateFlag::Hovered) ? 1.f : 0.f;
			dctx.pressT   = Has(st, WidgetStateFlag::Pressed) ? 1.f : 0.f;
			dctx.focusT   = Has(st, WidgetStateFlag::Focused) ? 1.f : 0.f;
			dctx.checkedT = Has(st, WidgetStateFlag::Checked) ? 1.f : 0.f;
		}
		if (auto* tf = m_ctx.Get<TransformStyle>(e)) dctx.opacity *= tf->opacity;
		return dctx;
	}

	inline void RenderSystem::Process(ECS::EntityId root, [[maybe_unused]] ECS::EntityId focused) {
		if (!m_ctx.IsAlive(root)) return;
		auto& drawList = m_index.GetDrawList();
		for (const auto& call : drawList) {
			if (!m_ctx.IsAlive(call.entity)) continue;
			auto *w  = m_ctx.Get<Widget>(call.entity);
			auto *cr = m_ctx.Get<ComputedRect>(call.entity);
			if (!w || !cr || !Has(w->behavior, WidgetBehaviorFlag::Visible)) continue;

			DrawCtx dctx = _MakeCtx(call.entity);

			SDL_Rect clipRectSDL = {(int)call.clipRect.x, (int)call.clipRect.y,
			                        (int)call.clipRect.w, (int)call.clipRect.h};
			m_renderer.SetClipRect(clipRectSDL);
			_DrawWidget(call.entity, dctx, cr->absolute);
		}
		m_renderer.SetClipRect(SDL_Rect{}); // Clear clip
	}

	inline void RenderSystem::ProcessAnimate(float dt, ECS::EntityId root) {
		(void)dt; (void)root;
	}

	inline void RenderSystem::_RegisterBuiltinRenderers() {
		// Default no-op — users can register custom renderers via GetRegistry()
	}

	inline void RenderSystem::_DrawWidget(ECS::EntityId e, const DrawCtx& dctx, const FRect& rect) {
		auto *w = m_ctx.Get<Widget>(e);
		if (!w) return;

		_DrawBackground(rect, dctx);

		switch (w->type) {
		case WidgetType::Label:    _DrawLabel    (e, rect, dctx); break;
		case WidgetType::Button:   _DrawButton   (e, rect, dctx); break;
		case WidgetType::Slider:   _DrawSlider   (e, rect, dctx); break;
		case WidgetType::Progress: _DrawProgress (e, rect, dctx); break;
		case WidgetType::Knob:     _DrawKnob     (e, rect, dctx); break;
		case WidgetType::Toggle:   _DrawToggle   (e, rect, dctx); break;
		case WidgetType::Radio:    _DrawRadio    (e, rect, dctx); break;
		case WidgetType::Input:    _DrawInput    (e, rect, dctx); break;
		case WidgetType::TextArea: _DrawTextArea (e, rect, dctx); break;
		case WidgetType::ListBox:  _DrawListBox  (e, rect, dctx); break;
		case WidgetType::Image:    _DrawImage    (e, rect, dctx); break;
		case WidgetType::Canvas:   _DrawCanvas   (e, rect, dctx); break;
		default: break;
		}

		// Custom registry dispatch
		m_registry.Dispatch(e, m_renderer, dctx, rect, m_ctx);
	}

	inline void RenderSystem::_DrawBackground(const FRect& rect, const DrawCtx& dctx) {
		SDL::Color col = dctx.BgColor();
		if (col.a == 0) return;

		// Gradient background
		if (dctx.bg && dctx.bg->gradient.has_value()) {
			const auto& grad = *dctx.bg->gradient;

			SDL::Color col2 = grad.color2;
			if      (Has(dctx.states, WidgetStateFlag::Hovered))  col2 = grad.color2Hovered;
			else if (Has(dctx.states, WidgetStateFlag::Pressed))  col2 = grad.color2Selected;
			else if (Has(dctx.states, WidgetStateFlag::Checked))  col2 = grad.color2Checked;
			else if (Has(dctx.states, WidgetStateFlag::Focused))  col2 = grad.color2Focused;

			bool horiz = (grad.start == GradientAnchor::Left || grad.start == GradientAnchor::Right);
			bool flip  = (grad.start == GradientAnchor::Bottom || grad.start == GradientAnchor::Right);
			SDL::Color c0 = flip ? col2 : col;
			SDL::Color c1 = flip ? col  : col2;

			auto toFColor = [](SDL::Color c) -> SDL_FColor {
				return {c.r / 255.f, c.g / 255.f, c.b / 255.f, c.a / 255.f};
			};

			SDL::Vertex tl{}, tr{}, bl{}, br{};
			tl.position = {rect.x,          rect.y};
			tr.position = {rect.x + rect.w, rect.y};
			bl.position = {rect.x,          rect.y + rect.h};
			br.position = {rect.x + rect.w, rect.y + rect.h};

			if (horiz) {
				tl.color = tr.color = toFColor(c0);
				bl.color = br.color = toFColor(c1);
				// Horizontal: left = c0, right = c1
				tl.color = bl.color = toFColor(c0);
				tr.color = br.color = toFColor(c1);
			} else {
				// Vertical: top = c0, bottom = c1
				tl.color = tr.color = toFColor(c0);
				bl.color = br.color = toFColor(c1);
			}

			std::array<SDL::Vertex, 4> verts = {tl, tr, bl, br};
			std::array<int, 6> idx = {0, 1, 2, 1, 3, 2};
			m_renderer.RenderGeometry(nullptr, verts, idx);
		} else {
			SDL::FCorners r = dctx.Radius();
			if (r.top_left > 0.f || r.top_right > 0.f || r.bottom_left > 0.f || r.bottom_right > 0.f) {
				m_renderer.SetDrawColor(col);
				m_renderer.RenderFillRoundedRect(rect, r);
			} else {
				m_renderer.SetDrawColor(col);
				m_renderer.RenderFillRect(rect);
			}
		}

		// Draw borders
		SDL::FBox borders = dctx.Borders();
		if (borders.left > 0.f || borders.right > 0.f || borders.top > 0.f || borders.bottom > 0.f) {
			SDL::Color bc = dctx.BdColor();
			m_renderer.SetDrawColor(bc);
			if (borders.top > 0.f)
				m_renderer.RenderFillRect(FRect{rect.x, rect.y, rect.w, borders.top});
			if (borders.bottom > 0.f)
				m_renderer.RenderFillRect(FRect{rect.x, rect.y + rect.h - borders.bottom, rect.w, borders.bottom});
			if (borders.left > 0.f)
				m_renderer.RenderFillRect(FRect{rect.x, rect.y, borders.left, rect.h});
			if (borders.right > 0.f)
				m_renderer.RenderFillRect(FRect{rect.x + rect.w - borders.right, rect.y, borders.right, rect.h});
		}
	}

	inline void RenderSystem::_DrawLabel(ECS::EntityId e, const FRect& rect, [[maybe_unused]] const DrawCtx& dctx) {
		auto *te = m_ctx.Get<TextEdit>(e);
		if (!te || te->text.empty()) return;

		TextHAlign ha = TextHAlign::Left;
		TextVAlign va = TextVAlign::Center;
		if (dctx.ts) { ha = dctx.ts->alignH; va = dctx.ts->alignV; }

		_DrawText(e, te->text, rect, ha, va);
	}

	inline void RenderSystem::_DrawButton(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *te = m_ctx.Get<TextEdit>(e);

		SDL::Color bgColor = dctx.BgColor();
		_FillRR(rect, bgColor, dctx.Radius().top_left);

		if (te && !te->text.empty()) {
			TextHAlign ha = TextHAlign::Center;
			TextVAlign va = TextVAlign::Center;
			if (dctx.ts) { ha = dctx.ts->alignH; va = dctx.ts->alignV; }
			_DrawText(e, te->text, rect, ha, va);
		}
	}

	inline void RenderSystem::_DrawSlider(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *sc = m_ctx.Get<SliderConfig>(e);
		auto *si = m_ctx.Get<SliderInteraction>(e);
		auto *nv = m_ctx.Get<NumericValue<float>>(e);
		if (!sc || !nv) return;

		SDL::FBox pad = dctx.Padding();
		bool h = (sc->orientation == Orientation::Horizontal);
		float trackLen = h ? (rect.w - pad.left - pad.right) : (rect.h - pad.top - pad.bottom);
		float thumbSize = 16.f;
		float normVal = nv->GetNorm<float>();
		normVal = SDL::Clamp(normVal, 0.f, 1.f);

		FRect track, thumb, fill;
		if (h) {
			float cx = rect.x + pad.left;
			float cy = rect.y + rect.h * 0.5f;
			track = {cx, cy - 4.f, trackLen, 8.f};
			float thumbX = cx + normVal * (trackLen - thumbSize);
			thumb = {thumbX, cy - thumbSize * 0.5f, thumbSize, thumbSize};
			fill = {cx, cy - 4.f, normVal * trackLen, 8.f};
		} else {
			float cx = rect.x + rect.w * 0.5f;
			float cy = rect.y + pad.top;
			track = {cx - 4.f, cy, 8.f, trackLen};
			float thumbY = cy + (1.f - normVal) * (trackLen - thumbSize);
			thumb = {cx - thumbSize * 0.5f, thumbY, thumbSize, thumbSize};
			fill = {cx - 4.f, cy + (1.f - normVal) * trackLen, 8.f, normVal * trackLen};
		}

		_FillRR(track, dctx.TrackColor(), 4.f);
		_FillRR(fill,  dctx.FillColor(),  4.f);

		SDL::Color thumbCol = (si && si->drag) ? dctx.FillColor() : dctx.ThumbColor();
		_FillRR(thumb, thumbCol, 8.f);
	}

	inline void RenderSystem::_DrawProgress(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *nv = m_ctx.Get<NumericValue<float>>(e);
		if (!nv) return;

		float ratio = SDL::Clamp(nv->GetNorm<float>(), 0.f, 1.f);

		_FillRR(rect, dctx.TrackColor(), 4.f);

		FRect fill{rect.x, rect.y, rect.w * ratio, rect.h};
		_FillRR(fill, dctx.FillColor(), 4.f);

		_StrokeRR(rect, dctx.BdColor(), {1, 1, 1, 1}, 4.f);
	}

	inline void RenderSystem::_DrawKnob(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *nv = m_ctx.Get<NumericValue<float>>(e);
		if (!nv) return;
		(void)m_ctx.Get<KnobConfig>(e); // ensure component present

		float cx = rect.x + rect.w * 0.5f;
		float cy = rect.y + rect.h * 0.5f;
		float radius = SDL::Min(rect.w, rect.h) * 0.35f;
		float innerRadius = radius * 0.6f;

		m_renderer.SetDrawColor(dctx.BgColor());
		m_renderer.RenderFillCircle({cx, cy}, radius);

		m_renderer.SetDrawColor(dctx.TrackColor());
		for (int i = 0; i < 27; ++i) {
			float angle = -135.f + i * 10.f;
			float rad = angle * 3.14159265f / 180.f;
			float x = cx + radius * 0.8f * std::cos(rad);
			float y = cy + radius * 0.8f * std::sin(rad);
			m_renderer.RenderFillCircle({x, y}, 2.f);
		}

		float normVal = nv->GetNorm<float>();
		float angle = -135.f + normVal * 270.f;
		float rad = angle * 3.14159265f / 180.f;
		float ix = cx + innerRadius * std::cos(rad);
		float iy = cy + innerRadius * std::sin(rad);
		float ox = cx + radius * 0.9f * std::cos(rad);
		float oy = cy + radius * 0.9f * std::sin(rad);

		m_renderer.SetDrawColor(dctx.TextColor());
		m_renderer.RenderLine({ix, iy}, {ox, oy});

		m_renderer.SetDrawColor({255, 255, 255, 255});
		m_renderer.RenderFillCircle({cx, cy}, innerRadius * 0.3f);
	}

	inline void RenderSystem::_DrawToggle(ECS::EntityId e, const FRect& rect, [[maybe_unused]] const DrawCtx& dctx) {
		auto *tog = m_ctx.Get<ToggleState>(e);
		if (!tog) return;

		constexpr float sw = 40.f;
		constexpr float sh = 20.f;
		float sx = rect.x + 4.f;
		float sy = rect.y + (rect.h - sh) * 0.5f;

		// Eased on/off progress: animated when ToggleAnim is driven, else snap.
		float t = tog->checked ? 1.f : 0.f;
		if (auto* ta = m_ctx.Get<ToggleAnim>(e)) {
			Easing ease = Easing::EaseOut;
			if (auto* as = m_ctx.Get<AnimationStyle>(e)) ease = as->easing;
			t = EaseEval(ease, ta->animT);
		}

		SDL::Color offC{100, 100, 100, 255}, onC{80, 200, 100, 255};
		_FillRR({sx, sy, sw, sh}, DrawCtx::_Mix(offC, onC, t), sh * 0.5f);

		float knobMin = sx + sh * 0.4f;
		float knobMax = sx + sw - sh * 0.4f;
		float knobX   = knobMin + (knobMax - knobMin) * t;
		m_renderer.SetDrawColor({255, 255, 255, 255});
		m_renderer.RenderFillCircle({knobX, sy + sh * 0.5f}, sh * 0.35f);

		auto *te = m_ctx.Get<TextEdit>(e);
		if (te && !te->text.empty()) {
			FRect labelRect = {rect.x + sw + 8.f, rect.y, rect.w - sw - 8.f, rect.h};
			_DrawText(e, te->text, labelRect, TextHAlign::Left, TextVAlign::Center);
		}
	}

	inline void RenderSystem::_DrawRadio(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *rd = m_ctx.Get<RadioState>(e);
		if (!rd) return;

		float radius = SDL::Min(rect.w, rect.h) * 0.25f;
		float cx = rect.x + radius + 4.f;
		float cy = rect.y + rect.h * 0.5f;

		m_renderer.SetDrawColor(dctx.BdColor());
		m_renderer.RenderCircle({cx, cy}, radius);

		if (rd->checked) {
			m_renderer.SetDrawColor(dctx.FillColor());
			m_renderer.RenderFillCircle({cx, cy}, radius * 0.6f);
		}

		auto *te = m_ctx.Get<TextEdit>(e);
		if (te && !te->text.empty()) {
			FRect labelRect = {rect.x + 28.f, rect.y, rect.w - 28.f, rect.h};
			_DrawText(e, te->text, labelRect, TextHAlign::Left, TextVAlign::Center);
		}
	}

	inline void RenderSystem::_DrawInput(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *te = m_ctx.Get<TextEdit>(e);
		if (!te) return;

		SDL::Color bgColor = dctx.BgColor();
		_FillRR(rect, bgColor, 4.f);

		SDL::Color borderColor = Has(dctx.states, WidgetStateFlag::Focused)
			? dctx.BdColor() : SDL::Color{100, 100, 100, 100};
		_StrokeRR(rect, borderColor, {1.f, 1.f, 1.f, 1.f}, 4.f);

		SDL::Color textCol = te->text.empty() && !Has(dctx.states, WidgetStateFlag::Focused)
			? (dctx.ts ? dctx.ts->placeholderColor : SDL::Color{90, 92, 105, 200})
			: dctx.TextColor();

		const std::string& displayText = (te->text.empty() && !Has(dctx.states, WidgetStateFlag::Focused))
			? te->placeholder : te->text;

		if (!displayText.empty()) {
			FRect textRect = {rect.x + 6.f, rect.y, rect.w - 12.f, rect.h};
			_DrawText(e, displayText, textRect, TextHAlign::Left, TextVAlign::Center);
			(void)textCol;
		}

		if (Has(dctx.states, WidgetStateFlag::Focused)) {
			float cursorX = rect.x + 6.f + 8.f * (float)te->cursor;
			m_renderer.SetDrawColor({255, 255, 255, 200});
			m_renderer.RenderLine(SDL::FPoint{cursorX, rect.y + 3.f},
			                       SDL::FPoint{cursorX, rect.y + rect.h - 3.f});
		}
	}

	inline void RenderSystem::_DrawTextArea(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *te = m_ctx.Get<TextEdit>(e);
		auto *ta = m_ctx.Get<TextAreaData>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		if (!te || !ta || !lp) return;

		_FillRR(rect, dctx.BgColor(), 4.f);

		SDL::FBox pad = dctx.Padding();
		float viewW = rect.w - pad.left - pad.right;
		float viewH = rect.h - pad.top  - pad.bottom;
		float sbt = dctx.ScrollbarThickness();

		FRect contentRect{rect.x + pad.left, rect.y + pad.top, viewW, viewH};
		SDL_Rect clipRect{(int)contentRect.x, (int)contentRect.y,
		                  (int)contentRect.w, (int)contentRect.h};
		m_renderer.SetClipRect(clipRect);

		float lineH = 14.f;
		float yOff = lp->scrollY;
		int lineNum = 0;
		size_t lineStart = 0;

		for (size_t i = 0; i <= te->text.size(); ++i) {
			if (i == te->text.size() || te->text[i] == '\n') {
				std::string line = te->text.substr(lineStart, i - lineStart);
				float lineY = rect.y + pad.top + lineNum * lineH - yOff;
				if (lineY >= rect.y && lineY < rect.y + rect.h) {
					FRect lineRect = {contentRect.x, lineY, contentRect.w, lineH};
					_DrawText(e, line, lineRect, TextHAlign::Left, TextVAlign::Top);
				}
				lineStart = i + 1;
				lineNum++;
			}
		}

		m_renderer.SetClipRect(std::nullopt);

		bool showY = lp->contentH > viewH;
		if (showY) {
			FRect sbRect{rect.x + rect.w - sbt, rect.y + pad.top, sbt, viewH};
			_DrawScrollBar(sbRect, Orientation::Vertical, lp->contentH, viewH, lp->scrollY, dctx);
		}
	}

	inline void RenderSystem::_DrawListBox(ECS::EntityId e, const FRect& rect, const DrawCtx& dctx) {
		auto *lb = m_ctx.Get<ListBoxData>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *ilv = m_ctx.Get<ItemListView>(e);
		if (!lb || !lp || !ilv) return;

		_FillRR(rect, dctx.BgColor(), 4.f);

		SDL::FBox pad = dctx.Padding();
		float sbt = dctx.ScrollbarThickness();
		float viewW = rect.w - pad.left - pad.right;
		float viewH = rect.h - pad.top  - pad.bottom;

		FRect itemArea{rect.x + pad.left, rect.y + pad.top, viewW, viewH};
		SDL_Rect itemClip{(int)itemArea.x, (int)itemArea.y,
		                  (int)itemArea.w, (int)itemArea.h};
		m_renderer.SetClipRect(itemClip);

		float yOff = lp->scrollY;
		for (int i = 0; i < (int)ilv->items.size(); ++i) {
			float itemY = rect.y + pad.top + i * ilv->itemHeight - yOff;
			if (itemY + ilv->itemHeight < rect.y + pad.top) continue;
			if (itemY > rect.y + pad.top + viewH) break;

			FRect itemRect{itemArea.x, itemY, itemArea.w, ilv->itemHeight};
			SDL::Color itemBg;
			if (i == ilv->selectedIndex) {
				itemBg = dctx.FillColor();
			} else if (i == ilv->hoverIndex) {
				itemBg = dctx.bg ? dctx.bg->hoveredColor : SDL::Color{100, 100, 100, 100};
			} else {
				itemBg = {0, 0, 0, 0};
			}
			if (itemBg.a > 0) {
				_FillRR(itemRect, itemBg, 2.f);
			}

			_DrawText(e, ilv->items[i], itemRect, TextHAlign::Left, TextVAlign::Center);
		}

		m_renderer.SetClipRect(std::nullopt);

		bool showY = lp->contentH > viewH;
		if (showY) {
			FRect sbRect{rect.x + rect.w - sbt, rect.y + pad.top, sbt, viewH};
			_DrawScrollBar(sbRect, Orientation::Vertical, lp->contentH, viewH, lp->scrollY, dctx);
		}
	}

	inline void RenderSystem::_DrawImage(ECS::EntityId e, const FRect& rect, [[maybe_unused]] const DrawCtx& dctx) {
		auto *id = m_ctx.Get<ImageData>(e);
		if (!id || id->key.empty()) return;

		auto texRef = m_pool.Get<SDL::Texture>(id->key);
		if (!texRef) return;

		FRect fitRect = rect;
		auto texSize = texRef->GetSize();
		if (texSize.x > 0 && texSize.y > 0) {
			float texAspect = (float)texSize.x / (float)texSize.y;
			float rectAspect = rect.w / rect.h;

			switch (id->fit) {
				case ImageFit::Contain: {
					if (texAspect > rectAspect) {
						fitRect.h = rect.w / texAspect;
						fitRect.y += (rect.h - fitRect.h) * 0.5f;
					} else {
						fitRect.w = rect.h * texAspect;
						fitRect.x += (rect.w - fitRect.w) * 0.5f;
					}
					break;
				}
				case ImageFit::Cover: {
					if (texAspect < rectAspect) {
						fitRect.w = rect.h * texAspect;
						fitRect.x += (rect.w - fitRect.w) * 0.5f;
					} else {
						fitRect.h = rect.w / texAspect;
						fitRect.y += (rect.h - fitRect.h) * 0.5f;
					}
					break;
				}
				case ImageFit::Fill:  fitRect = rect; break;
				case ImageFit::Tile:
				case ImageFit::None:
					fitRect = {rect.x, rect.y, (float)texSize.x, (float)texSize.y};
					break;
			}
		}

		m_renderer.RenderTexture(*texRef, std::nullopt,
		                         SDL_FRect{fitRect.x, fitRect.y, fitRect.w, fitRect.h});
	}

	inline void RenderSystem::_DrawCanvas(ECS::EntityId e, const FRect& rect, [[maybe_unused]] const DrawCtx& dctx) {
		auto *cd = m_ctx.Get<CanvasData>(e);
		if (!cd || !cd->renderCb) return;
		cd->renderCb(m_renderer, rect);
	}

	// ── Helper implementations ───────────────────────────────────────────────────────

	inline void RenderSystem::_FillRect(const FRect& r, SDL::Color c, float op) {
		c.a = (uint8_t)(c.a * op);
		m_renderer.SetDrawColor(c);
		m_renderer.RenderFillRect(r);
	}

	inline void RenderSystem::_FillRR(const FRect& r, SDL::Color c, float radius, float op) {
		c.a = (uint8_t)(c.a * op);
		m_renderer.SetDrawColor(c);
		if (radius > 0.f) {
			FCorners corners{radius, radius, radius, radius};
			m_renderer.RenderFillRoundedRect(r, corners);
		} else {
			m_renderer.RenderFillRect(r);
		}
	}

	inline void RenderSystem::_StrokeRR(const FRect& r, SDL::Color c, const SDL::FBox& borders,
	                                       [[maybe_unused]] float radius, float op) {
		if (borders.left <= 0.f && borders.right <= 0.f && borders.top <= 0.f && borders.bottom <= 0.f)
			return;
		c.a = (uint8_t)(c.a * op);
		m_renderer.SetDrawColor(c);
		if (borders.top > 0.f)
			m_renderer.RenderFillRect(FRect{r.x, r.y, r.w, borders.top});
		if (borders.bottom > 0.f)
			m_renderer.RenderFillRect(FRect{r.x, r.y + r.h - borders.bottom, r.w, borders.bottom});
		if (borders.left > 0.f)
			m_renderer.RenderFillRect(FRect{r.x, r.y, borders.left, r.h});
		if (borders.right > 0.f)
			m_renderer.RenderFillRect(FRect{r.x + r.w - borders.right, r.y, borders.right, r.h});
	}

	inline RenderSystem::ScrollViewInfo RenderSystem::_ComputeScrollView(const FRect& r,
	                                                                          const LayoutProps& lp,
	                                                                          const Widget& w,
	                                                                          const DrawCtx& dctx) const {
		ScrollViewInfo v;
		SDL::FBox pad = dctx.Padding();
		v.innerW = r.w - pad.left - pad.right;
		v.innerH = r.h - pad.top  - pad.bottom;

		float sbt = dctx.ScrollbarThickness();

		bool showX = false, showY = false;
		if (w.type == WidgetType::Container || w.type == WidgetType::ListBox ||
		    w.type == WidgetType::TextArea || w.type == WidgetType::Tree) {
			showX = Has(w.behavior, WidgetBehaviorFlag::ScrollableX) ||
			        (Has(w.behavior, WidgetBehaviorFlag::AutoScrollableX) && lp.contentW > v.innerW);
			showY = Has(w.behavior, WidgetBehaviorFlag::ScrollableY) ||
			        (Has(w.behavior, WidgetBehaviorFlag::AutoScrollableY) && lp.contentH > v.innerH);
		}

		v.showX = showX;
		v.showY = showY;
		v.viewW = showY ? SDL::Max(0.f, v.innerW - sbt) : v.innerW;
		v.viewH = showX ? SDL::Max(0.f, v.innerH - sbt) : v.innerH;
		return v;
	}

	inline void RenderSystem::_DrawHueBar(const FRect& rect, [[maybe_unused]] float op) {
		const SDL::Color hues[7] = {
			{255, 0, 0, 255}, {255, 255, 0, 255}, {0, 255, 0, 255},
			{0, 255, 255, 255}, {0, 0, 255, 255}, {255, 0, 255, 255},
			{255, 0, 0, 255}
		};
		float sw = rect.w / 6.f;
		for (int i = 0; i < 6; ++i) {
			FRect seg{rect.x + i * sw, rect.y, sw, rect.h};
			m_renderer.SetDrawColor(hues[i]);
			m_renderer.RenderFillRect(seg);
		}
	}

	inline void RenderSystem::_DrawScrollBar(const FRect& rect, Orientation orient,
	                                           float contentSize, float viewSize,
	                                           float offset, const DrawCtx& dctx) {
		if (contentSize <= viewSize) return;

		float ratio = SDL::Clamp(viewSize / contentSize, 0.05f, 1.f);
		bool isVert = (orient == Orientation::Vertical);

		SDL::Color trackCol = dctx.sb ? dctx.sb->trackColor : SDL::Color{42, 44, 58, 200};
		SDL::Color thumbCol = dctx.sb ? dctx.sb->thumbColor : SDL::Color{100, 160, 230, 220};

		m_renderer.SetDrawColor(trackCol);
		m_renderer.RenderFillRect(rect);

		float thumbLen = isVert ? (rect.h * ratio) : (rect.w * ratio);
		float maxOffset = isVert ? (rect.h - thumbLen) : (rect.w - thumbLen);
		float offsetRatio = (contentSize - viewSize > 0.f)
			? (offset / (contentSize - viewSize)) : 0.f;
		float thumbPos = maxOffset * offsetRatio;

		FRect thumb;
		if (isVert) {
			thumb = {rect.x, rect.y + thumbPos, rect.w, thumbLen};
		} else {
			thumb = {rect.x + thumbPos, rect.y, thumbLen, rect.h};
		}

		m_renderer.SetDrawColor(thumbCol);
		FCorners scrollThumbCorners{3.f, 3.f, 3.f, 3.f};
		m_renderer.RenderFillRoundedRect(thumb, scrollThumbCorners);
	}

	// NOTE: RenderSystem::_DrawText is defined in UISystem.h (after Context is fully declared)
	// so it can use Context::ResolveFont / Context::EnsureText for proper SDL_ttf rendering.

} // namespace SDL::UI
