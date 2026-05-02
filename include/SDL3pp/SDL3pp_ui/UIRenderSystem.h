#pragma once

#include "UIComponents.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_resources.h"
#include "../SDL3pp_ttf.h"

#include <functional>
#include <optional>
#include <typeindex>
#include <unordered_map>

namespace SDL::UI {

	class UILayoutSystem;
	class System;

	// ==================================================================================
	// WidgetRenderRegistry — extension point for widget-specific rendering
	// ==================================================================================
	//
	// Each widget type can register a render function keyed on a marker component
	// (e.g. SpinnerData, GraphData). At draw time, the registry is queried with
	// the entity's component set; if any registered marker is present, its render
	// function is called instead of the built-in switch fallback.
	//
	// This keeps _DrawWidget extensible: adding a new widget renderer means
	// registering a new entry, not editing a 2000-line switch.
	// ==================================================================================

	using RenderFn = std::function<void(ECS::EntityId, RendererRef, const Style&,
	                                     const FRect&, ECS::Context&)>;

	class WidgetRenderRegistry {
	public:
		template <typename T>
		void Register(RenderFn fn) {
			m_renderers[std::type_index(typeid(T))] = std::move(fn);
		}

		bool Dispatch(ECS::EntityId e, RendererRef r, const Style& s,
		              const FRect& rect, ECS::Context& ctx) const;

		template <typename T>
		void RegisterByPresence(RenderFn fn);  ///< Defined inline below — uses ctx.Has<T>() at dispatch.

	private:
		std::unordered_map<std::type_index, RenderFn> m_renderers;

		// Component-presence checkers used by Dispatch to test which renderer applies.
		std::vector<std::pair<std::type_index, std::function<bool(ECS::EntityId, ECS::Context&)>>> m_presenceCheckers;

		template <typename T>
		static bool _HasComponent(ECS::EntityId e, ECS::Context& ctx) {
			return ctx.Get<T>(e) != nullptr;
		}
	};

	template <typename T>
	void WidgetRenderRegistry::RegisterByPresence(RenderFn fn) {
		m_renderers[std::type_index(typeid(T))] = std::move(fn);
		m_presenceCheckers.emplace_back(std::type_index(typeid(T)),
		                                &WidgetRenderRegistry::_HasComponent<T>);
	}

	// ==================================================================================
	// UIRenderSystem — render + animate pipeline
	// ==================================================================================

	class UIRenderSystem {
	public:
		UIRenderSystem(ECS::Context& ctx, RendererRef renderer, ResourcePool& pool,
		               UILayoutSystem& layout, System& sys);
		~UIRenderSystem();

		void Process       (ECS::EntityId root, ECS::EntityId focused);
		void ProcessAnimate(float dt, ECS::EntityId root);

		[[nodiscard]] WidgetRenderRegistry&       GetRegistry()       noexcept { return m_registry; }
		[[nodiscard]] const WidgetRenderRegistry& GetRegistry() const noexcept { return m_registry; }

	private:
		ECS::Context&        m_ctx;
		RendererRef          m_renderer;
		ResourcePool&        m_pool;
		UILayoutSystem&      m_layout;
		System&              m_sys;
		WidgetRenderRegistry m_registry;

#if defined(SDL3PP_ENABLE_TTF)
		std::optional<SDL::RendererTextEngine> m_engine;
#endif

		void _DrawWidget    (ECS::EntityId e, const WidgetState& st, const Style& s);
		void _DrawBackground(ECS::EntityId e, const FRect& rect, const Style& s, const WidgetState& st);
		void _DrawText      (ECS::EntityId e, const std::string& text, const FRect& rect,
		                     TextHAlign hAlign = TextHAlign::Center, TextVAlign vAlign = TextVAlign::Center);

		void _RegisterBuiltinRenderers();

		void _DrawLabel(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawButton(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawSlider(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawProgress(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawKnob(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawToggle(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawRadio(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawInput(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawTextArea(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawListBox(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawImage(ECS::EntityId e, const FRect& rect, const Style& s);
		void _DrawCanvas(ECS::EntityId e, const FRect& rect, const Style& s);

		// ── Rendering helpers ────────────────────────────────────────────────────────
		void _FillRect(const FRect& r, SDL::Color c, float op = 1.f);
		void _FillRR(const FRect& r, SDL::Color c, float radius, float op = 1.f);
		void _StrokeRR(const FRect& r, SDL::Color c, const SDL::FBox& borders, float radius, float op = 1.f);
		void _DrawScrollBar(const FRect& rect, const ScrollBarData& sb, const Style& s);
		void _DrawHueBar(const FRect& rect, float op = 1.f);

		struct ScrollViewInfo {
			float innerW = 0.f, innerH = 0.f;
			float viewW = 0.f, viewH = 0.f;
			bool showX = false, showY = false;
		};
		ScrollViewInfo _ComputeScrollView(const FRect& r, const LayoutProps& lp, const Widget& w) const;
	};

	// ==================================================================================
	// Implementation: UIRenderSystem
	// ==================================================================================

	inline UIRenderSystem::UIRenderSystem(ECS::Context& ctx, RendererRef renderer, ResourcePool& pool,
	                                       UILayoutSystem& layout, System& sys)
		: m_ctx(ctx), m_renderer(renderer), m_pool(pool), m_layout(layout), m_sys(sys) {
		_RegisterBuiltinRenderers();
	}

	inline UIRenderSystem::~UIRenderSystem() {
#if defined(SDL3PP_ENABLE_TTF)
		m_ctx.Each<TextCache>([](ECS::EntityId, TextCache &tc) {
			tc.text = SDL::Text{};
		});
#endif
	}

	inline void UIRenderSystem::Process(ECS::EntityId root, ECS::EntityId focused) {
		if (!m_ctx.IsAlive(root)) return;
		auto& drawList = m_layout.GetDrawList();
		for (const auto& call : drawList) {
			if (!m_ctx.IsAlive(call.entity)) continue;
			auto *w = m_ctx.Get<Widget>(call.entity);
			auto *st = m_ctx.Get<WidgetState>(call.entity);
			auto *s = m_ctx.Get<Style>(call.entity);
			auto *cr = m_ctx.Get<ComputedRect>(call.entity);
			if (!w || !st || !s || !cr || !Has(w->behavior, BehaviorFlag::Visible)) continue;

			SDL_Rect clipRectSDL = {(int)call.clipRect.x, (int)call.clipRect.y,
			                        (int)call.clipRect.w, (int)call.clipRect.h};
			m_renderer.SetClipRect(clipRectSDL);
			_DrawWidget(call.entity, *st, *s);
		}
		m_renderer.SetClipRect(SDL_Rect{}); // Clear clip with empty rect
	}

	inline void UIRenderSystem::ProcessAnimate(float dt, ECS::EntityId root) {
		// Animation processing placeholder - animation fields would be added to WidgetState if needed
		(void)dt;
		(void)root;
	}


	inline void UIRenderSystem::_RegisterBuiltinRenderers() {
		m_registry.Register<std::nullptr_t>([](ECS::EntityId e, RendererRef r, const Style& s,
		                                        const FRect& rect, ECS::Context& ctx) {
			// Default no-op renderer
		});
	}

	inline void UIRenderSystem::_DrawWidget(ECS::EntityId e, const WidgetState& st, const Style& s) {
		auto *w = m_ctx.Get<Widget>(e);
		auto *cr = m_ctx.Get<ComputedRect>(e);
		if (!w || !cr) return;

		_DrawBackground(e, cr->screen, s, st);

		// Dispatch to widget-specific renderers
		switch (w->type) {
		case WidgetType::Label:
			_DrawLabel(e, cr->screen, s);
			break;
		case WidgetType::Button:
			_DrawButton(e, cr->screen, s);
			break;
		case WidgetType::Slider:
			_DrawSlider(e, cr->screen, s);
			break;
		case WidgetType::Progress:
			_DrawProgress(e, cr->screen, s);
			break;
		case WidgetType::Knob:
			_DrawKnob(e, cr->screen, s);
			break;
		case WidgetType::Toggle:
			_DrawToggle(e, cr->screen, s);
			break;
		case WidgetType::RadioButton:
			_DrawRadio(e, cr->screen, s);
			break;
		case WidgetType::Input:
			_DrawInput(e, cr->screen, s);
			break;
		case WidgetType::TextArea:
			_DrawTextArea(e, cr->screen, s);
			break;
		case WidgetType::ListBox:
			_DrawListBox(e, cr->screen, s);
			break;
		case WidgetType::Image:
			_DrawImage(e, cr->screen, s);
			break;
		case WidgetType::Canvas:
			_DrawCanvas(e, cr->screen, s);
			break;
		default:
			break;
		}
	}

	inline void UIRenderSystem::_DrawBackground(ECS::EntityId e, const FRect& rect, const Style& s, const WidgetState& st) {

		auto color = s.bgColor;
		if (st.hovered) {
			color.r = (uint8_t)SDL::Clamp((float)color.r * 1.1f, 0.f, 255.f);
			color.g = (uint8_t)SDL::Clamp((float)color.g * 1.1f, 0.f, 255.f);
			color.b = (uint8_t)SDL::Clamp((float)color.b * 1.1f, 0.f, 255.f);
		}

		m_renderer.SetDrawColor(color);
		if (s.radius.top_left > 0.f) {
			FCorners corners{s.radius.top_left, s.radius.top_right, s.radius.bottom_left, s.radius.bottom_right};
			m_renderer.RenderFillRoundedRect(rect, corners);
		} else {
			m_renderer.RenderFillRect(rect);
		}

		// Draw borders
		if (s.borders.left > 0.f || s.borders.right > 0.f || s.borders.top > 0.f || s.borders.bottom > 0.f) {
			m_renderer.SetDrawColor(s.bdColor);
			FRect top    {rect.x, rect.y, rect.w, s.borders.top};
			FRect bottom {rect.x, rect.y + rect.h - s.borders.bottom, rect.w, s.borders.bottom};
			FRect left   {rect.x, rect.y, s.borders.left, rect.h};
			FRect right  {rect.x + rect.w - s.borders.right, rect.y, s.borders.right, rect.h};
			if (s.borders.top > 0.f) m_renderer.RenderFillRect(top);
			if (s.borders.bottom > 0.f) m_renderer.RenderFillRect(bottom);
			if (s.borders.left > 0.f) m_renderer.RenderFillRect(left);
			if (s.borders.right > 0.f) m_renderer.RenderFillRect(right);
		}
	}

	inline void UIRenderSystem::_DrawLabel(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *te = m_ctx.Get<TextEdit>(e);
		if (!te || te->text.empty()) return;

		if (te && !te->text.empty()) {
			_DrawText(e, te->text, rect, TextHAlign::Left, TextVAlign::Center);
		}
	}

	inline void UIRenderSystem::_DrawButton(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *st = m_ctx.Get<WidgetState>(e);
		auto *te = m_ctx.Get<TextEdit>(e);

		// Draw pressed state with darker background
		SDL::Color bgColor = s.bgColor;
		if (st && st->pressed) {
			bgColor.r = (uint8_t)SDL::Max(0.f, (float)bgColor.r * 0.8f);
			bgColor.g = (uint8_t)SDL::Max(0.f, (float)bgColor.g * 0.8f);
			bgColor.b = (uint8_t)SDL::Max(0.f, (float)bgColor.b * 0.8f);
		} else if (st && st->hovered) {
			bgColor.r = (uint8_t)SDL::Min(255.f, (float)bgColor.r * 1.1f);
			bgColor.g = (uint8_t)SDL::Min(255.f, (float)bgColor.g * 1.1f);
			bgColor.b = (uint8_t)SDL::Min(255.f, (float)bgColor.b * 1.1f);
		}

		_FillRR(rect, bgColor, 4.f);

		// Draw text
		if (te && !te->text.empty()) {
			_DrawText(e, te->text, rect, TextHAlign::Center, TextVAlign::Center);
		}
	}

	inline void UIRenderSystem::_DrawSlider(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *sd = m_ctx.Get<SliderData>(e);
		auto *nv = m_ctx.Get<NumericValue>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		if (!sd || !nv || !lp) return;

		bool h = (sd->orientation == Orientation::Horizontal);
		float trackLen = h ? (rect.w - lp->padding.left - lp->padding.right) : (rect.h - lp->padding.top - lp->padding.bottom);
		float thumbSize = 16.f;
		float normVal = (float)((nv->val - nv->min) / SDL::Max(0.0001, nv->max - nv->min));
		normVal = SDL::Clamp(normVal, 0.f, 1.f);

		FRect track, thumb, fill;
		if (h) {
			// Horizontal slider
			float cx = rect.x + lp->padding.left;
			float cy = rect.y + rect.h * 0.5f;
			track = {cx, cy - 4.f, trackLen, 8.f};
			float thumbX = cx + normVal * (trackLen - thumbSize);
			thumb = {thumbX, cy - thumbSize * 0.5f, thumbSize, thumbSize};
			fill = {cx, cy - 4.f, normVal * trackLen, 8.f};
		} else {
			// Vertical slider
			float cx = rect.x + rect.w * 0.5f;
			float cy = rect.y + lp->padding.top;
			track = {cx - 4.f, cy, 8.f, trackLen};
			float thumbY = cy + (1.f - normVal) * (trackLen - thumbSize);
			thumb = {cx - thumbSize * 0.5f, thumbY, thumbSize, thumbSize};
			fill = {cx - 4.f, cy + (1.f - normVal) * trackLen, 8.f, normVal * trackLen};
		}

		// Draw track background
		_FillRR(track, {80, 80, 80, 200}, 4.f);

		// Draw fill (progress)
		_FillRR(fill, {100, 150, 255, 200}, 4.f);

		// Draw thumb with hover/press feedback
		SDL::Color thumbColor = s.bgColor;
		if (sd->drag) {
			thumbColor.r = (uint8_t)SDL::Min(255.f, (float)thumbColor.r * 1.2f);
			thumbColor.g = (uint8_t)SDL::Min(255.f, (float)thumbColor.g * 1.2f);
			thumbColor.b = (uint8_t)SDL::Min(255.f, (float)thumbColor.b * 1.2f);
		}
		_FillRR(thumb, thumbColor, 8.f);
	}

	inline void UIRenderSystem::_DrawProgress(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *nv = m_ctx.Get<NumericValue>(e);
		if (!nv) return;

		float ratio = SDL::Clamp((float)(nv->val - nv->min) / SDL::Max(0.0001, nv->max - nv->min), 0.f, 1.f);

		// Draw background track
		_FillRR(rect, {80, 80, 80, 200}, 4.f);

		// Draw filled portion
		FRect fill{rect.x, rect.y, rect.w * ratio, rect.h};
		_FillRR(fill, {100, 200, 100, 255}, 4.f);

		// Optional: draw border
		_StrokeRR(rect, {60, 60, 60, 255}, {1, 1, 1, 1}, 4.f);
	}

	inline void UIRenderSystem::_DrawKnob(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *kd = m_ctx.Get<KnobData>(e);
		auto *nv = m_ctx.Get<NumericValue>(e);
		if (!kd || !nv) return;

		float cx = rect.x + rect.w * 0.5f;
		float cy = rect.y + rect.h * 0.5f;
		float radius = SDL::Min(rect.w, rect.h) * 0.35f;
		float innerRadius = radius * 0.6f;

		// Draw outer circle (background)
		m_renderer.SetDrawColor(s.bgColor);
		m_renderer.RenderFillCircle({cx, cy}, radius);

		// Draw track arc (270 degrees)
		m_renderer.SetDrawColor({100, 100, 100, 200});
		for (int i = 0; i < 27; ++i) {
			float angle = -135.f + i * 10.f;
			float rad = angle * 3.14159265f / 180.f;
			float x = cx + radius * 0.8f * std::cos(rad);
			float y = cy + radius * 0.8f * std::sin(rad);
			m_renderer.RenderFillCircle({x, y}, 2.f);
		}

		// Draw indicator line based on current value
		float normVal = (nv->val - nv->min) / SDL::Max(0.0001f, nv->max - nv->min);
		float angle = -135.f + normVal * 270.f;
		float rad = angle * 3.14159265f / 180.f;
		float ix = cx + innerRadius * std::cos(rad);
		float iy = cy + innerRadius * std::sin(rad);
		float ox = cx + radius * 0.9f * std::cos(rad);
		float oy = cy + radius * 0.9f * std::sin(rad);

		m_renderer.SetDrawColor(s.textColor);
		m_renderer.RenderLine({ix, iy}, {ox, oy});

		// Draw center dot
		m_renderer.SetDrawColor({255, 255, 255, 255});
		m_renderer.RenderFillCircle({cx, cy}, innerRadius * 0.3f);
	}

	inline void UIRenderSystem::_DrawToggle(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *tog = m_ctx.Get<ToggleData>(e);
		auto *st = m_ctx.Get<WidgetState>(e);
		if (!tog) return;

		// Pill-shaped toggle switch
		float sw = rect.w * 0.5f;
		float sh = rect.h * 0.6f;
		float sx = rect.x + (rect.w - sw) * 0.5f;
		float sy = rect.y + (rect.h - sh) * 0.5f;

		// Animated transition
		float t = tog->animT;
		SDL::Color bgColor = tog->checked ?
			SDL::Color{80, 200, 100, 255} : SDL::Color{100, 100, 100, 255};

		_FillRR({sx, sy, sw, sh}, bgColor, sh * 0.5f);

		// Animated knob position
		float knobX = tog->checked ? (sx + sw - sh * 0.4f) : (sx + sh * 0.4f);
		m_renderer.SetDrawColor({255, 255, 255, 255});
		m_renderer.RenderFillCircle({knobX, sy + sh * 0.5f}, sh * 0.35f);

		// Draw label text if present
		auto *te = m_ctx.Get<TextEdit>(e);
		if (te && !te->text.empty()) {
			FRect labelRect = {rect.x + 44.f, rect.y, rect.w - 44.f, rect.h};
			_DrawText(e, te->text, labelRect, TextHAlign::Left, TextVAlign::Center);
		}
	}

	inline void UIRenderSystem::_DrawRadio(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *rd = m_ctx.Get<RadioData>(e);
		if (!rd) return;

		float radius = SDL::Min(rect.w, rect.h) * 0.25f;
		float cx = rect.x + radius + 4.f;
		float cy = rect.y + rect.h * 0.5f;

		// Draw outer circle
		m_renderer.SetDrawColor({100, 100, 100, 200});
		m_renderer.RenderCircle({cx, cy}, radius);

		// Draw filled dot if checked
		if (rd->checked) {
			m_renderer.SetDrawColor({100, 200, 100, 255});
			m_renderer.RenderFillCircle({cx, cy}, radius * 0.6f);
		}

		// Draw label text if present
		auto *te = m_ctx.Get<TextEdit>(e);
		if (te && !te->text.empty()) {
			FRect labelRect = {rect.x + 28.f, rect.y, rect.w - 28.f, rect.h};
			_DrawText(e, te->text, labelRect, TextHAlign::Left, TextVAlign::Center);
		}
	}

	inline void UIRenderSystem::_DrawInput(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *te = m_ctx.Get<TextEdit>(e);
		auto *st = m_ctx.Get<WidgetState>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		if (!te || !lp) return;

		// Draw input background
		SDL::Color bgColor = st && st->hovered ? s.bgHoveredColor : s.bgColor;
		_FillRR(rect, bgColor, 4.f);

		// Draw border
		SDL::Color borderColor = st && st->focused ? s.bdColor : SDL::Color{100, 100, 100, 100};
		_StrokeRR(rect, borderColor, SDL::FBox{1.f, 1.f, 1.f, 1.f}, 4.f);

		// Draw text (placeholder or actual)
		const std::string& displayText = (te->text.empty() && st && !st->focused) ? te->placeholder : te->text;
		if (!displayText.empty()) {
			FRect textRect = {rect.x + 6.f, rect.y, rect.w - 12.f - 2.f, rect.h};
			_DrawText(e, displayText, textRect, TextHAlign::Left, TextVAlign::Center);
		}

		// Draw cursor if focused
		if (st && st->focused) {
			auto *ts = m_ctx.Get<TextSelection>(e);
			if (ts && te) {
				float cursorX = rect.x + 6.f + 20.f; // Approx based on cursor position
				m_renderer.SetDrawColor(SDL::Color{255, 255, 255, 255});
				m_renderer.RenderLine(SDL::FPoint{cursorX, rect.y + 2.f}, SDL::FPoint{cursorX, rect.y + rect.h - 2.f});
			}
		}
	}

	inline void UIRenderSystem::_DrawTextArea(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *te = m_ctx.Get<TextEdit>(e);
		auto *ta = m_ctx.Get<TextAreaData>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		if (!te || !ta || !lp) return;

		Widget w; w.name = "textarea"; w.type = WidgetType::TextArea; w.behavior = BehaviorFlag::Enable | BehaviorFlag::Visible | BehaviorFlag::ScrollableX | BehaviorFlag::ScrollableY;
		ScrollViewInfo svi = _ComputeScrollView(rect, *lp, w);

		// Draw background
		_FillRR(rect, s.bgColor, 4.f);

		// Draw text content area
		FRect contentRect{rect.x + lp->padding.left, rect.y + lp->padding.top,
		                 svi.viewW, svi.viewH};
		SDL_Rect clipRect{(int)contentRect.x, (int)contentRect.y, (int)contentRect.w, (int)contentRect.h};
		m_renderer.SetClipRect(clipRect);

		// Split text into lines
		float lineH = 14.f; // Approx line height
		float yOff = lp->scrollY;
		int lineNum = 0;
		size_t lineStart = 0;

		for (size_t i = 0; i <= te->text.size(); ++i) {
			if (i == te->text.size() || te->text[i] == '\n') {
				std::string line = te->text.substr(lineStart, i - lineStart);
				float lineY = rect.y + lp->padding.top + lineNum * lineH - yOff;
				if (lineY >= rect.y && lineY < rect.y + rect.h) {
					FRect lineRect = {contentRect.x, lineY, contentRect.w, lineH};
					_DrawText(e, line, lineRect, TextHAlign::Left, TextVAlign::Top);
				}
				lineStart = i + 1;
				lineNum++;
			}
		}

		m_renderer.SetClipRect(std::nullopt);

		// Draw scrollbars
		if (svi.showY) {
			FRect sbRect{rect.x + rect.w - lp->scrollbarThickness, rect.y + lp->padding.top,
			           lp->scrollbarThickness, svi.viewH};
			ScrollBarData sb{};
			sb.orientation = Orientation::Vertical;
			sb.contentSize = lp->contentH;
			sb.viewSize = svi.viewH;
			sb.offset = lp->scrollY;
			_DrawScrollBar(sbRect, sb, s);
		}
	}

	inline void UIRenderSystem::_DrawListBox(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *lb = m_ctx.Get<ListBoxData>(e);
		auto *lp = m_ctx.Get<LayoutProps>(e);
		auto *st = m_ctx.Get<WidgetState>(e);
		auto *ilv = m_ctx.Get<ItemListView>(e);
		if (!lb || !lp || !ilv) return;

		Widget w; w.name = "listbox"; w.type = WidgetType::ListBox; w.behavior = BehaviorFlag::Enable | BehaviorFlag::Visible | BehaviorFlag::ScrollableY;
		ScrollViewInfo svi = _ComputeScrollView(rect, *lp, w);

		// Draw background
		_FillRR(rect, s.bgColor, 4.f);

		// Draw items
		FRect itemArea{rect.x + lp->padding.left, rect.y + lp->padding.top,
		              svi.viewW, svi.viewH};
		SDL_Rect itemClip{(int)itemArea.x, (int)itemArea.y, (int)itemArea.w, (int)itemArea.h};
		m_renderer.SetClipRect(itemClip);

		float yOff = lp->scrollY;
		for (int i = 0; i < (int)ilv->items.size(); ++i) {
			float itemY = rect.y + lp->padding.top + i * ilv->itemHeight - yOff;
			if (itemY + ilv->itemHeight < rect.y + lp->padding.top) continue;
			if (itemY > rect.y + lp->padding.top + svi.viewH) break;

			// Draw item background (selected or hovered)
			FRect itemRect{itemArea.x, itemY, itemArea.w, ilv->itemHeight};
			SDL::Color itemBg;
			if (i == ilv->selectedIndex) {
				itemBg = SDL::Color{80, 150, 255, 200};
			} else if (i == ilv->hoverIndex) {
				itemBg = SDL::Color{100, 100, 100, 100};
			} else {
				itemBg = SDL::Color{0, 0, 0, 0};
			}
			if (itemBg.a > 0) {
				_FillRR(itemRect, itemBg, 2.f);
			}

			// Draw item text
			if (i < ilv->items.size()) {
				_DrawText(e, ilv->items[i], itemRect, TextHAlign::Left, TextVAlign::Center);
			}
		}

		m_renderer.SetClipRect(std::nullopt);

		// Draw scrollbar
		if (svi.showY) {
			FRect sbRect{rect.x + rect.w - lp->scrollbarThickness, rect.y + lp->padding.top,
			           lp->scrollbarThickness, svi.viewH};
			ScrollBarData sb{};
			sb.orientation = Orientation::Vertical;
			sb.contentSize = lp->contentH;
			sb.viewSize = svi.viewH;
			sb.offset = lp->scrollY;
			_DrawScrollBar(sbRect, sb, s);
		}
	}

	inline void UIRenderSystem::_DrawImage(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *id = m_ctx.Get<ImageData>(e);
		if (!id || id->key.empty()) return;

		// Get or load texture from pool
		// TODO: EnsureTexture requires full System definition
		auto texRef = m_pool.Get<SDL::Texture>(id->key);
		if (!texRef) return;

		// Compute fit rect based on ImageFit mode
		FRect fitRect = rect;
		auto texSize = texRef->GetSize();
		if (texSize.x > 0 && texSize.y > 0) {
			float texAspect = texSize.x / texSize.y;
			float rectAspect = rect.w / rect.h;

			switch (id->fit) {
				case ImageFit::Contain: {
					// Fit entire image within rect, preserving aspect ratio
					if (texAspect > rectAspect) {
						// Image wider: fit to width
						fitRect.h = rect.w / texAspect;
						fitRect.y += (rect.h - fitRect.h) * 0.5f;
					} else {
						// Image taller: fit to height
						fitRect.w = rect.h * texAspect;
						fitRect.x += (rect.w - fitRect.w) * 0.5f;
					}
					break;
				}
				case ImageFit::Cover: {
					// Fill entire rect, may crop image
					if (texAspect < rectAspect) {
						// Image narrower: scale to height
						fitRect.w = rect.h * texAspect;
						fitRect.x += (rect.w - fitRect.w) * 0.5f;
					} else {
						// Image wider: scale to width
						fitRect.h = rect.w / texAspect;
						fitRect.y += (rect.h - fitRect.h) * 0.5f;
					}
					break;
				}
				case ImageFit::Fill:
					// Stretch to rect exactly (may distort)
					fitRect = rect;
					break;
				case ImageFit::Tile:
					// Tile: just draw at native size
					fitRect = {rect.x, rect.y, (float)texSize.x, (float)texSize.y};
					break;
				case ImageFit::None:
					// Draw at actual size at top-left
					fitRect = {rect.x, rect.y, (float)texSize.x, (float)texSize.y};
					break;
			}
		}

		// Render texture
		m_renderer.RenderTexture(*texRef, std::nullopt, SDL_FRect{fitRect.x, fitRect.y, fitRect.w, fitRect.h});
	}

	inline void UIRenderSystem::_DrawCanvas(ECS::EntityId e, const FRect& rect, const Style& s) {
		auto *cd = m_ctx.Get<CanvasData>(e);
		if (!cd || !cd->renderCb) return;
		cd->renderCb(m_renderer, rect);
	}

	// ── Helper implementations ───────────────────────────────────────────────────────

	inline void UIRenderSystem::_FillRect(const FRect& r, SDL::Color c, float op) {
		c.a = (uint8_t)(c.a * op);
		m_renderer.SetDrawColor(c);
		m_renderer.RenderFillRect(r);
	}

	inline void UIRenderSystem::_FillRR(const FRect& r, SDL::Color c, float radius, float op) {
		c.a = (uint8_t)(c.a * op);
		m_renderer.SetDrawColor(c);
		if (radius > 0.f) {
			FCorners corners{radius, radius, radius, radius};
			m_renderer.RenderFillRoundedRect(r, corners);
		} else {
			m_renderer.RenderFillRect(r);
		}
	}

	inline void UIRenderSystem::_StrokeRR(const FRect& r, SDL::Color c, const SDL::FBox& borders, float radius, float op) {
		if (borders.left <= 0.f && borders.right <= 0.f && borders.top <= 0.f && borders.bottom <= 0.f)
			return;
		c.a = (uint8_t)(c.a * op);
		m_renderer.SetDrawColor(c);

		// Draw borders as rectangles (simple approach)
		if (borders.top > 0.f)
			m_renderer.RenderFillRect(FRect{r.x, r.y, r.w, borders.top});
		if (borders.bottom > 0.f)
			m_renderer.RenderFillRect(FRect{r.x, r.y + r.h - borders.bottom, r.w, borders.bottom});
		if (borders.left > 0.f)
			m_renderer.RenderFillRect(FRect{r.x, r.y, borders.left, r.h});
		if (borders.right > 0.f)
			m_renderer.RenderFillRect(FRect{r.x + r.w - borders.right, r.y, borders.right, r.h});
	}

	inline UIRenderSystem::ScrollViewInfo UIRenderSystem::_ComputeScrollView(const FRect& r, const LayoutProps& lp, const Widget& w) const {
		ScrollViewInfo v;
		v.innerW = r.w - lp.padding.left - lp.padding.right;
		v.innerH = r.h - lp.padding.top - lp.padding.bottom;

		bool showX = false, showY = false;
		if (w.type == WidgetType::Container || w.type == WidgetType::ListBox ||
		    w.type == WidgetType::TextArea || w.type == WidgetType::Tree) {
			showX = Has(w.behavior, BehaviorFlag::ScrollableX) ||
			        (Has(w.behavior, BehaviorFlag::AutoScrollableX) && lp.contentW > v.innerW);
			showY = Has(w.behavior, BehaviorFlag::ScrollableY) ||
			        (Has(w.behavior, BehaviorFlag::AutoScrollableY) && lp.contentH > v.innerH);
		}

		v.showX = showX;
		v.showY = showY;
		v.viewW = v.showY ? SDL::Max(0.f, v.innerW - lp.scrollbarThickness) : v.innerW;
		v.viewH = v.showX ? SDL::Max(0.f, v.innerH - lp.scrollbarThickness) : v.innerH;
		return v;
	}

	inline void UIRenderSystem::_DrawHueBar(const FRect& rect, float op) {
		// Draw 6-color gradient bar for color picker
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

	inline void UIRenderSystem::_DrawScrollBar(const FRect& rect, const ScrollBarData& sb, const Style& s) {
		if (sb.contentSize <= sb.viewSize) return;

		float ratio = sb.viewSize / sb.contentSize;
		ratio = SDL::Clamp(ratio, 0.05f, 1.f);

		bool isVert = (sb.orientation == Orientation::Vertical);

		// Draw track
		m_renderer.SetDrawColor(s.bgColor);
		m_renderer.RenderFillRect(rect);

		// Draw thumb
		float thumbLen = isVert ? (rect.h * ratio) : (rect.w * ratio);
		float maxOffset = isVert ? (rect.h - thumbLen) : (rect.w - thumbLen);
		float offsetRatio = (sb.contentSize - sb.viewSize > 0.f) ?
		                    (sb.offset / (sb.contentSize - sb.viewSize)) : 0.f;
		float thumbPos = maxOffset * offsetRatio;

		FRect thumb;
		if (isVert) {
			thumb = {rect.x, rect.y + thumbPos, rect.w, thumbLen};
		} else {
			thumb = {rect.x + thumbPos, rect.y, thumbLen, rect.h};
		}

		m_renderer.SetDrawColor(s.textColor);
		FCorners scrollThumbCorners{3.f, 3.f, 3.f, 3.f};
		m_renderer.RenderFillRoundedRect(thumb, scrollThumbCorners);
	}

} // namespace SDL::UI
