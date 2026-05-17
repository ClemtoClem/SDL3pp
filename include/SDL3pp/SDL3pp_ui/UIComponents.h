#pragma once

#include "UIEnums.h"
#include "UIValue.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_events.h"
#include "../SDL3pp_mouse.h"
#include "../SDL3pp_render.h"
#include "../SDL3pp_ttf.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <typeindex>
#include <limits>
#include <vector>

namespace SDL::UI {

	// ==================================================================================
	// Shared utilities
	// ==================================================================================

	// ── NumericValue ──────────────────────────────────────────────────────────────
	template <typename T>
	struct NumericValue {
		T value = T(0);
		T min   = std::numeric_limits<T>::lowest();
		T max   = std::numeric_limits<T>::max();
		T step  = T(0);
		std::string format = "{}";

		constexpr NumericValue() = default;

		constexpr NumericValue(T value,
		                       T minValue = std::numeric_limits<T>::lowest(),
		                       T maxValue = std::numeric_limits<T>::max(),
		                       T step = T(0),
		                       std::string_view fmt = "{}")
			: value(value), min(minValue), max(maxValue), step(step), format(fmt) {}

		void Set(T v) { value = SDL::Clamp(v, min, max); }

		template <typename U>
		U GetNorm() const {
			return (max > min) ? U(value - min) / U(max - min) : U(0);
		}
	};

	template <typename T>
	std::string FormatNumeric(const NumericValue<T>& v) {
		if constexpr (std::is_integral_v<T>) {
			return std::to_string(v.value);
		} else if constexpr (std::is_floating_point_v<T>) {
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(3) << v.value;
			return oss.str();
		}
		return {};
	}

	// ── Color conversions (RGB ↔ HSV) ────────────────────────────────────────────
	inline void RgbToHsv(SDL::Color c, float& h, float& s, float& v) noexcept {
		float rf = c.r / 255.f, gf = c.g / 255.f, bf = c.b / 255.f;
		float cmax = std::max(rf, std::max(gf, bf));
		float cmin = std::min(rf, std::min(gf, bf));
		float delta = cmax - cmin;
		v = cmax;
		s = (cmax > 0.f) ? delta / cmax : 0.f;
		if (delta < 1e-6f) { h = 0.f; return; }
		if      (cmax == rf) h = (gf - bf) / delta / 6.f;
		else if (cmax == gf) h = (2.f + (bf - rf) / delta) / 6.f;
		else                 h = (4.f + (rf - gf) / delta) / 6.f;
		if (h < 0.f) h += 1.f;
	}

	inline SDL::Color HsvToColor(float h, float s, float v, Uint8 a = 255) noexcept {
		if (s < 1e-6f) {
			Uint8 g = static_cast<Uint8>(v * 255.f);
			return {g, g, g, a};
		}
		float hh = h * 6.f;
		int   i  = static_cast<int>(hh) % 6;
		float f  = hh - static_cast<float>(i);
		float p  = v * (1.f - s);
		float q  = v * (1.f - f * s);
		float t  = v * (1.f - (1.f - f) * s);
		float r = 0, g = 0, b = 0;
		switch (i) {
			case 0:  r = v; g = t; b = p; break;
			case 1:  r = q; g = v; b = p; break;
			case 2:  r = p; g = v; b = t; break;
			case 3:  r = p; g = q; b = v; break;
			case 4:  r = t; g = p; b = v; break;
			default: r = v; g = p; b = q; break;
		}
		return {static_cast<Uint8>(r * 255.f),
		        static_cast<Uint8>(g * 255.f),
		        static_cast<Uint8>(b * 255.f), a};
	}

	// ==================================================================================
	// Tier 0 — Inline value-types (not ECS components on their own)
	// ==================================================================================

	/// @brief Two-stop gradient inlined inside @ref BackgroundStyle.
	struct BgGradient {
		SDL::Color color2         = { 40,  40,  60, 255};
		SDL::Color color2Hovered  = { 60,  62,  90, 255};
		SDL::Color color2Selected = { 20,  20,  35, 255};
		SDL::Color color2Checked  = { 80, 160, 240, 255};
		SDL::Color color2Focused  = { 20,  20,  35, 255};
		SDL::Color color2Disabled = { 28,  28,  38, 160};
		GradientAnchor start = GradientAnchor::Top;
		GradientAnchor end   = GradientAnchor::Bottom;
	};

	// ==================================================================================
	// Tier 1 — Universal components (every widget gets these via UIFactory::_Spawn)
	// ==================================================================================

	/// @brief Optional searchable tag — enables path-based lookup ("/tag1/tag2/@id").
	struct Tag {
		std::string value;
	};

	/// @brief Identity, behavior bitmask and live state of a widget.
	struct Widget {
		WidgetType          type     = WidgetType::Container;
		WidgetBehaviorFlag  behavior = WidgetBehaviorFlag::Enable
		                             | WidgetBehaviorFlag::Visible
		                             | WidgetBehaviorFlag::DispatchEvent;
		WidgetStateFlag     states   = WidgetStateFlag::None;
		DirtyFlag           dirty    = DirtyFlag::All;

		[[nodiscard]] bool Is(WidgetStateFlag f) const noexcept {
			return Has(states, f);
		}
	};

	/// @brief Layout-only properties (size, position, flow, alignment, scroll state).
	///        Spacing-related fields (margin/padding/gap) live in @ref SpacingStyle.
	struct LayoutProps {
		// ── Position & size ─────────────────────────────────────────────────
		Value absX      = Value::Px(0);
		Value absY      = Value::Px(0);
		Value width     = Value::Auto();
		Value height    = Value::Auto();
		Value minWidth  = Value::Px(-1.f);
		Value minHeight = Value::Px(-1.f);
		Value maxWidth  = Value::Px(-1.f);
		Value maxHeight = Value::Px(-1.f);

		// ── Flow & alignment ────────────────────────────────────────────────
		Layout       layout         = Layout::InColumn;
		Align        alignChildrenH = Align::Stretch;
		Align        alignChildrenV = Align::Stretch;
		Align        alignSelfH     = Align::Stretch;
		Align        alignSelfV     = Align::Stretch;
		AttachLayout attach         = AttachLayout::Relative;
		BoxSizing    boxSizing      = BoxSizing::BorderBox;

		// ── Scroll state (host for any widget that scrolls its content) ─────
		float scrollX  = 0.f, scrollY  = 0.f;
		float contentW = 0.f, contentH = 0.f;
	};

	/// @brief Resolved geometry produced by the layout pass.
	struct ComputedRect {
		FPoint measured   = {}; ///< Intrinsic size from Measure pass
		FRect  inner_rel  = {}; ///< Inner relative rect without borders
		FRect  outer_rel  = {}; ///< Outer relative rect with borders
		FRect  absolute   = {}; ///< Final screen rect after Place pass
		FRect  inner_clip = {}; ///< Inner clip rect
		FRect  outer_clip = {}; ///< Outer clip rect
	};

	struct Parent {
		ECS::EntityId id = ECS::NullEntity;
	};

	struct Children {
		std::vector<ECS::EntityId> ids;
		void Add(ECS::EntityId e)    { ids.push_back(e); }
		void Remove(ECS::EntityId e) { std::erase(ids, e); }
	};

	/// @brief Centralized callback bag for pointer / focus / value-change events.
	struct Callbacks {
		// ── Pointer ─────────────────────────────────────────────────────────
		std::function<void(SDL::MouseButton)>      onPress;
		std::function<void(SDL::MouseButton)>      onRelease;
		std::function<void(SDL::MouseButton)>      onClick;
		std::function<void(SDL::MouseButton)>      onDoubleClick;
		std::function<void(SDL::MouseButton, int)> onMultiClick;
		std::function<void()>                      onMouseEnter;
		std::function<void()>                      onMouseLeave;
		std::function<void(SDL::FPoint)>           onDrag;

		// ── Focus ───────────────────────────────────────────────────────────
		std::function<void()> onFocusGain;
		std::function<void()> onFocusLose;

		// ── Value-change ────────────────────────────────────────────────────
		std::function<void(float)>                    onChange;
		std::function<void(SDL::Color)>               onColorChange;
		std::function<void(const std::string&)>       onTextChange;
		std::function<void(bool)>                     onToggle;
		std::function<void(float)>                    onScroll;
		std::function<void(SDL::FPoint, SDL::FPoint)> onScrollChange;

		// ── Touch ───────────────────────────────────────────────────────────
		std::function<void(const SDL::TouchFingerEvent&)> onTouchFinger;

		// ── Item-list ───────────────────────────────────────────────────────
		std::function<void(int, bool)>  onTreeSelect;
		std::function<void(int, int)>   onItemReorder;
	};

	// ==================================================================================
	// Tier 2 — Specialized style components
	// ==================================================================================

	/// @brief Background fill of a widget.
	struct BackgroundStyle {
		SDL::Color color         = { 22,  22,  30, 255};
		SDL::Color hoveredColor  = { 40,  42,  58, 255};
		SDL::Color selectedColor = { 14,  14,  20, 255};
		SDL::Color checkedColor  = { 55, 115, 195, 255};
		SDL::Color focusedColor  = { 14,  14,  20, 255};
		SDL::Color disabledColor = { 22,  22,  28, 160};

		std::optional<BgGradient> gradient = std::nullopt;
	};

	/// @brief Border drawn around the content box.
	struct BorderStyle {
		SDL::Color color         = { 55,  58,  78, 255};
		SDL::Color hoveredColor  = { 90,  95, 130, 255};
		SDL::Color selectedColor = { 90,  95, 130, 255};
		SDL::Color checkedColor  = { 90,  95, 130, 255};
		SDL::Color focusedColor  = { 70, 130, 210, 255};
		SDL::Color disabledColor = { 90,  95, 130, 255};

		SDL::FBox     dimensions = { 1.f, 1.f, 1.f, 1.f };
		SDL::FCorners radius     = { 5.f, 5.f, 5.f, 5.f };
	};

	/// @brief Text colour, font and alignment.
	struct TextStyle {
		SDL::Color color            = {215, 215, 220, 255};
		SDL::Color hoveredColor     = {255, 255, 255, 255};
		SDL::Color selectedColor    = {255, 255, 255, 255};
		SDL::Color checkedColor     = {255, 255, 255, 255};
		SDL::Color disabledColor    = {110, 110, 120, 200};
		SDL::Color placeholderColor = { 90,  92, 105, 200};

		std::string fontKey;
		float       fontSize = 0.f;
		FontType    usedFont = FontType::Inherited;
		TextHAlign  alignH   = TextHAlign::Center;
		TextVAlign  alignV   = TextVAlign::Center;
	};

	/// @brief Box-model spacing — margin (outside), padding (inside), gap (between children).
	struct SpacingStyle {
		SDL::FBox margin  = {0.f, 0.f, 0.f, 0.f};
		SDL::FBox padding = {8.f, 6.f, 8.f, 6.f};
		float     gap     = 4.f;
	};

	/// @brief Per-entity rendering transform (opacity, future rotation/scale).
	struct TransformStyle {
		float opacity = 1.f;
	};

	/// @brief Accent colours used by track/fill/thumb-style widgets.
	struct AccentStyle {
		SDL::Color trackColor     = { 42,  44,  58, 255};
		SDL::Color fillColor      = { 70, 130, 210, 255};
		SDL::Color thumbColor     = {100, 160, 230, 255};
		SDL::Color separatorColor = { 55,  58,  78, 255};
	};

	/// @brief Audio cues triggered by widget interactions.
	struct SoundStyle {
		std::string clickKey;
		std::string hoverKey;
		std::string scrollKey;
		std::string showKey;
		std::string hideKey;
	};

	/// @brief Inline scrollbar visuals.
	struct ScrollbarStyle {
		float      thickness  = 8.f;
		SDL::Color trackColor = { 42,  44,  58, 200};
		SDL::Color thumbColor = {100, 160, 230, 220};
	};

	/// @brief Tooltip colours — paired with @ref TooltipData.
	struct TooltipStyle {
		SDL::Color bgColor   = { 30,  32,  44, 245};
		SDL::Color bdColor   = { 75,  80, 108, 255};
		SDL::Color textColor = {215, 218, 228, 255};
	};

	// ==================================================================================
	// Tier 3 — Reusable add-ons (opt-in feature components)
	// ==================================================================================

	/// @brief Decorative icon attached to a widget.
	struct IconData {
		std::string key;
		float       pad = 4.f;

		float opacityNormal   = 1.f;
		float opacityHovered  = 1.f;
		float opacityPressed  = 0.85f;
		float opacityDisabled = 0.35f;

		SDL::Color tintNormalColor   = {255, 255, 255, 255};
		SDL::Color tintHoveredColor  = {255, 255, 255, 255};
		SDL::Color tintPressedColor  = {220, 220, 220, 255};
		SDL::Color tintDisabledColor = {180, 180, 180, 255};
	};

	/// @brief Hover-tooltip behaviour.
	///        The factory creates a sub-entity (container + label) stored in `entity`.
	///        Use TooltipMixin builder methods to style the label.
	struct TooltipData {
		std::string   text;
		float         delay         = 1.f;
		bool          visible       = true;
		ECS::EntityId entity        = ECS::NullEntity; ///< Tooltip container entity
		ECS::EntityId labelEntity   = ECS::NullEntity; ///< Label entity inside container
	};

	/// @brief 9-slice tileset skin.
	struct TilesetData {
		int   tileW        = 16;
		int   tileH        = 16;
		int   tilesPerRow  = 3;
		int   firstTileIdx = 0;
		float borderW      = 0.f;
		float borderH      = 0.f;

		[[nodiscard]] FRect TileRect(int rel) const noexcept {
			int abs = firstTileIdx + rel;
			int row = abs / tilesPerRow;
			int col = abs % tilesPerRow;
			return {static_cast<float>(col * tileW),
			        static_cast<float>(row * tileH),
			        static_cast<float>(tileW),
			        static_cast<float>(tileH)};
		}
		[[nodiscard]] float BorderW() const noexcept { return borderW > 0.f ? borderW : static_cast<float>(tileW); }
		[[nodiscard]] float BorderH() const noexcept { return borderH > 0.f ? borderH : static_cast<float>(tileH); }
	};

	/// @brief Cached TTF text object, created lazily by the renderer.
	struct TextCache {
		SDL::Text text;
	};

	// ── TextEdit + TextSelection (shared by Input, TextArea and Label) ───────────
	struct TextEdit {
		std::string text;
		std::string placeholder;
		int   cursor     = 0;
		float blinkTimer = 0.f;
		int   tabSize    = 4;
		bool  readOnly   = false;

		[[nodiscard]] int LineCount() const noexcept {
			int n = 1;
			for (char c : text) if (c == '\n') ++n;
			return n;
		}
		[[nodiscard]] int LineStart(int line) const noexcept {
			int cur = 0;
			for (int i = 0; i < (int)text.size(); ++i) {
				if (cur == line) return i;
				if (text[i] == '\n') ++cur;
			}
			return (int)text.size();
		}
		[[nodiscard]] int LineEnd(int line) const noexcept {
			int i = LineStart(line);
			while (i < (int)text.size() && text[i] != '\n') ++i;
			return i;
		}
		[[nodiscard]] int LineOf(int pos) const noexcept {
			pos = std::clamp(pos, 0, (int)text.size());
			int line = 0;
			for (int i = 0; i < pos; ++i) if (text[i] == '\n') ++line;
			return line;
		}
		[[nodiscard]] int ColOf(int pos) const noexcept {
			return pos - LineStart(LineOf(pos));
		}
	};

	struct TextSelection {
		int anchor = 0;
		int focus  = 0;

		[[nodiscard]] int  Min()          const noexcept { return std::min(anchor, focus); }
		[[nodiscard]] int  Max()          const noexcept { return std::max(anchor, focus); }
		[[nodiscard]] bool HasSelection() const noexcept { return anchor != focus; }
		void Clear() noexcept { anchor = focus = 0; }
		void Set(int a, int f) noexcept { anchor = a; focus = f; }
		void Set(int a, int f, int /*cursor*/) noexcept { anchor = a; focus = f; }

		[[nodiscard]] std::string GetSelected(const std::string& src) const {
			if (!HasSelection()) return {};
			int a = std::clamp(Min(), 0, (int)src.size());
			int b = std::clamp(Max(), 0, (int)src.size());
			return (a < b) ? src.substr(a, b - a) : std::string{};
		}
		int DeleteFrom(std::string& dst, int& cursor) {
			if (!HasSelection()) return 0;
			int a = std::clamp(Min(), 0, (int)dst.size());
			int b = std::clamp(Max(), 0, (int)dst.size());
			if (a < b) dst.erase(a, b - a);
			cursor = a;
			Clear();
			return b - a;
		}
	};

	/// @brief Per-span rich text style — used by Label, TextArea, Input.
	///        Fields left at default (color={0,0,0,0}, fontSize=0, fontKey="") inherit
	///        from the widget's TextStyle.
	struct TextSpanStyle {
		bool       bold          = false;
		bool       italic        = false;
		bool       underline     = false;
		bool       strikethrough = false;
		bool       highlight     = false;
		bool       reversed      = false;      ///< Invert text/background colours
		SDL::Color color          = {0, 0, 0, 0};
		SDL::Color highlightColor = {255, 255, 100, 80};
		std::string fontKey;                   ///< Override font key (empty = inherit)
		float       fontSize = 0.f;            ///< Override font size (0 = inherit)
	};

	struct TextSpans {
		struct Span {
			int           start = 0;
			int           end   = 0;
			TextSpanStyle style;
		};
		std::vector<Span> spans;

		void Add(int start, int end, TextSpanStyle style) {
			if (start >= end) return;
			spans.push_back({start, end, style});
			std::sort(spans.begin(), spans.end(),
			          [](const Span& a, const Span& b) { return a.start < b.start; });
		}
		void Clear() noexcept { spans.clear(); }

		[[nodiscard]] const TextSpanStyle* StyleAt(int pos) const noexcept {
			const TextSpanStyle* found = nullptr;
			for (const auto& sp : spans)
				if (pos >= sp.start && pos < sp.end) found = &sp.style;
			return found;
		}

		void Shift(int at, int delta) {
			for (auto& sp : spans) {
				if (sp.start >= at) sp.start = std::max(at, sp.start + delta);
				if (sp.end   >  at) sp.end   = std::max(at, sp.end   + delta);
			}
			std::erase_if(spans, [](const Span& s) { return s.start >= s.end; });
		}
	};

	/// @brief State common to all selectable item-list widgets.
	struct ItemListView {
		std::vector<std::string> items;
		int   selectedIndex = -1;
		int   hoverIndex    = -1;
		float itemHeight    = 22.f;
		int   maxVisible    = 6;
		float scrollY       = 0.f;
	};

	// ==================================================================================
	// Grid layout (Container with Layout::InGrid)
	// ==================================================================================

	struct LayoutGridProps {
		int        columns       = 2;
		int        rows          = 0;
		GridSizing colSizing     = GridSizing::Fixed;
		GridSizing rowSizing     = GridSizing::Fixed;
		GridLines  lines         = GridLines::None;
		SDL::Color lineColor     = {55, 60, 88, 160};
		float      lineThickness = 1.f;

		std::vector<float> colWidths;
		std::vector<float> rowHeights;
	};

	struct GridCell {
		int col     = 0;
		int row     = 0;
		int colSpan = 1;
		int rowSpan = 1;
	};

	// ==================================================================================
	// Tier 4 — Widget-specific data
	// ==================================================================================

	// ── SliderData ────────────────────────────────────────────────────────────────
	struct SliderData {
		Orientation        orientation  = Orientation::Horizontal;
		std::vector<float> markers;

		bool   drag         = false;
		float  dragStartPos = 0.f;
		double dragStartVal = 0.0;
	};

	// ── KnobData ──────────────────────────────────────────────────────────────────
	enum class KnobShape : Uint8 {
		Arc,
		Potentiometer
	};

	struct KnobData {
		KnobShape shape = KnobShape::Arc;

		bool   drag           = false;
		float  dragStartY     = 0.f;
		double dragStartVal   = 0.0;
		float  dragStartAngle = 0.f;
	};

	// ── ProgressData ──────────────────────────────────────────────────────────────
	struct ProgressData {
		Orientation orientation     = Orientation::Horizontal;
		bool        isIndeterminate = false;
	};

	// ── ToggleData ────────────────────────────────────────────────────────────────
	struct ToggleData {
		bool  checked = false;
		float animT   = 0.f;
	};

	// ── RadioData ─────────────────────────────────────────────────────────────────
	struct RadioData {
		std::string group;
		bool        checked = false;
	};

	// ── SeparatorData ─────────────────────────────────────────────────────────────
	struct SeparatorData {
		Orientation orientation = Orientation::Horizontal;
	};

	// ── ScrollBarData ─────────────────────────────────────────────────────────────
	struct ScrollBarData {
		Orientation orientation = Orientation::Vertical;
		float       contentSize = 0.f;
		float       viewSize    = 0.f;
		float       offset      = 0.f;
		float       trackSize   = 8.f;

		bool  drag         = false;
		float dragStartPos = 0.f;
		float dragStartOff = 0.f;
	};

	// ── ContainerScrollState ──────────────────────────────────────────────────────
	struct ContainerScrollState {
		FRect thumbX = {};
		FRect thumbY = {};

		bool  dragX        = false;
		float dragStartX   = 0.f;
		float dragStartOff = 0.f;

		bool  dragY          = false;
		float dragStartY     = 0.f;
		float dragStartOffY  = 0.f;
	};

	// ── InputData ─────────────────────────────────────────────────────────────────
	struct InputData {
		InputFilterType filter = InputFilterType::Text;
		std::string     format = "{}";

		ECS::EntityId incrementButton = ECS::NullEntity;
		ECS::EntityId decrementButton = ECS::NullEntity;

		bool   pressUp      = false;
		bool   pressDown    = false;
		bool   passwordMode = false;

		std::function<bool(char)> customFilter; ///< For InputFilterType::Custom, return true to allow character
		std::function<void()> onIncrement;        ///< Called when increment button is pressed
		std::function<void()> onDecrement;        ///< Called when decrement button is pressed
	};

	// ── ImageData ─────────────────────────────────────────────────────────────────
	struct ImageData {
		std::string key;
		ImageFit    fit = ImageFit::Contain;
	};

	// ── CanvasData ────────────────────────────────────────────────────────────────
	struct CanvasData {
		std::function<void(SDL::Event&)>           eventCb;
		std::function<void(float)>                 updateCb;
		std::function<void(RendererRef, FRect)>    renderCb;
	};

	// ── ListBoxData ───────────────────────────────────────────────────────────────
	/// @brief Dynamic list of selectable items rendered as child button entities.
	struct ListBoxData {
		bool reorderable = false;

		bool  dragActive = false;
		int   dragSrcIdx = -1;
		int   dragDstIdx = -1;
		float dragY      = 0.f;
		float dragStartY = 0.f;
		bool  dragMoved  = false;

		ECS::EntityId              scrollView   = ECS::NullEntity; ///< Scroll container child
		std::vector<ECS::EntityId> itemButtons;                     ///< Per-item button entities
	};

	// ── TextAreaData ──────────────────────────────────────────────────────────────
	struct TextAreaData {
		float scrollY = 0.f;
	};

	// ── GraphData ─────────────────────────────────────────────────────────────────
	struct GraphData {
		std::vector<float> data;
		float minVal     = 0.f;
		float maxVal     = 1.f;
		float xMin       = 0.f;
		float xMax       = 1.f;
		int   xDivisions = 8;
		int   yDivisions = 5;
		SDL::Color lineColor = { 70, 130, 210, 255};
		SDL::Color fillColor = { 70, 130, 210,  55};
		SDL::Color gridColor = { 55,  60,  88, 200};
		SDL::Color axisColor = {175, 180, 200, 220};
		std::string xLabel;
		std::string yLabel;
		std::string title;
		bool showFill = true;
		bool barMode  = false;
		bool logFreq  = false;
	};

	// ── ComboBoxData ──────────────────────────────────────────────────────────────
	/// @brief ComboBox composed of a toggle button + scrollable overlay container.
	///        Sub-entities are created by UIFactory::MakeComboBox.
	struct ComboBoxData {
		bool  open         = false;
		float scrollOffset = 0.f;
		FRect dropRect     = {};

		ECS::EntityId toggleButton = ECS::NullEntity; ///< Button showing current selection
		ECS::EntityId overlay      = ECS::NullEntity; ///< Popup overlay container (Fixed)
	};

	// ── TabViewData ───────────────────────────────────────────────────────────────
	/// @brief TabView composed of a tab-bar row (button per tab) + content area.
	///        Sub-entities are created by UIFactory::MakeTabView.
	struct TabViewData {
		struct Tab {
			std::string   label;
			bool          closable    = false;
			ECS::EntityId tabButton   = ECS::NullEntity; ///< Button in the tab bar
			ECS::EntityId tabContent  = ECS::NullEntity; ///< Content container for this tab
		};
		std::vector<Tab> tabs;
		int   activeTab  = 0;
		float tabHeight  = 32.f;
		TabLocation tabLocation = TabLocation::Top;
		std::function<void(int)> onTabChange;

		ECS::EntityId tabBar      = ECS::NullEntity; ///< Row container holding tab buttons
		ECS::EntityId contentArea = ECS::NullEntity; ///< Container showing active tab content
	};

	// ── ExpanderData ──────────────────────────────────────────────────────────────
	/// @brief Expander composed of a header button + collapsible content container.
	///        Sub-entities are created by UIFactory::MakeExpander.
	struct ExpanderData {
		bool  expanded = true;
		float animT    = 1.f;
		float headerH  = 28.f;

		ECS::EntityId headerButton  = ECS::NullEntity; ///< Toggle button (shows label + arrow)
		ECS::EntityId contentEntity = ECS::NullEntity; ///< Collapsible content container
	};

	// ── SplitterData ──────────────────────────────────────────────────────────────
	struct SplitterData {
		Orientation orientation = Orientation::Horizontal;
		float ratio      = 0.5f;
		float minRatio   = 0.05f;
		float maxRatio   = 0.95f;
		float handleSize = 6.f;

		bool  dragging  = false;
		float dragStart = 0.f;
		float dragRatio = 0.f;
	};

	// ── SpinnerData ───────────────────────────────────────────────────────────────
	struct SpinnerData {
		float angle     = 0.f;
		float speed     = 6.28f;
		float arcSpan   = 0.65f;
		float thickness = 3.f;
	};

	// ── BadgeData ─────────────────────────────────────────────────────────────────
	struct BadgeData {
		std::string text;
		std::string variant;
		SDL::Color  bgColor   = {220,  50,  40, 255};
		SDL::Color  textColor = {255, 255, 255, 255};
	};

	// ── ColorPickerData ───────────────────────────────────────────────────────────
	enum class ColorPickerPalette : Uint8 {
		Grayscale,
		RGB8,
		RGBFloat,
		GradientAB
	};

	struct ColorPickerData {
		ColorPickerPalette palette       = ColorPickerPalette::RGB8;
		SDL::Color         currentColor  = {255, 100,  50, 255};
		SDL::Color         colorA        = {  0,   0,   0, 255};
		SDL::Color         colorB        = {255, 255, 255, 255};
		float              precisionStep = 1.f / 255.f;
		bool               allowAlpha    = false;

		float hue   = 0.f;
		float sat   = 1.f;
		float val   = 1.f;
		float gradT = 0.f;

		bool dragging    = false;
		bool draggingHue = false;
	};

	// ── PopupData ─────────────────────────────────────────────────────────────────
	struct PopupData {
		std::string title;
		bool  closable  = true;
		bool  draggable = true;
		bool  resizable = false;
		bool  modal     = false;
		bool  open      = false;
		float headerH   = 28.f;

		bool        dragging   = false;
		SDL::FPoint dragOffset = {};
		SDL::FPoint pressPos   = {};

		bool        resizing        = false;
		SDL::FPoint resizeStart     = {};
		SDL::FPoint resizeStartSize = {};

		struct HeaderBtn {
			std::string           iconKey;
			std::function<void()> onClick;
		};
		std::vector<HeaderBtn> headerButtons;

		std::function<void()> onClose;
	};

	// ── Tree ──────────────────────────────────────────────────────────────────────
	struct TreeNodeData {
		std::string label;
		std::string iconKey;
		int  level       = 0;
		bool hasChildren = false;
		bool expanded    = false;
	};

	/// @brief Tree composed of a scroll container with per-node row entities.
	///        Node rows are re-built by the system when nodes are added/cleared.
	struct TreeData {
		std::vector<TreeNodeData> nodes;
		int   selectedIndex = -1;
		float itemHeight    = 22.f;
		float indentSize    = 16.f;
		float iconSize      = 14.f;
		std::function<void(int, bool)> onToggleNode;

		ECS::EntityId              scrollView = ECS::NullEntity; ///< Scroll container child
		std::vector<ECS::EntityId> nodeRows;                     ///< Per-node button entities
	};

	// ── MenuBar ───────────────────────────────────────────────────────────────────
	struct MenuBarItem {
		std::string label;
		std::string shortcutText;
		std::string iconKey;
		std::function<void()> action;
		std::vector<MenuBarItem> sub;
		bool separator = false;
		bool enabled   = true;
		bool checkable = false;
		bool checked   = false;

		static MenuBarItem Sep() { MenuBarItem i; i.separator = true; return i; }
	};

	struct MenuBarMenu {
		std::string label;
		std::vector<MenuBarItem> items;
		bool enabled = true;
	};

	/// @brief MenuBar composed of a menu-title button row + popup overlay per menu.
	///        Sub-entities are created/updated by UIFactory methods.
	struct MenuBarData {
		std::vector<MenuBarMenu> menus;
		int openMenu = -1;
		int hovMenu  = -1;
		int hovItem  = -1;
		std::function<void(int)> onItemSelect;

		ECS::EntityId              menuRow         = ECS::NullEntity; ///< Row of menu-title buttons
		std::vector<ECS::EntityId> menuButtons;                        ///< Per-menu title buttons
		ECS::EntityId              overlay         = ECS::NullEntity; ///< Dropdown overlay (Fixed)
		std::vector<ECS::EntityId> overlayItems;                       ///< Per-item buttons in overlay
	};

} // namespace SDL::UI
