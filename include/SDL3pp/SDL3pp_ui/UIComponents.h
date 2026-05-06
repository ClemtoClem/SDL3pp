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
#include <string>
#include <type_traits>
#include <typeindex>
#include <limits>
#include <vector>

namespace SDL::UI {

	// ==================================================================================
	// Shared utilities
	// ==================================================================================

	// ── NumericValue ────────────────────────────────────────────────

	template<typename T>
	struct NumericValue {
		T value = T(0);
		T min = std::numeric_limits<T>::lowest();
		T max = std::numeric_limits<T>::max();
		T step = T(0);
		std::string format = "{}";  ///< Format string for std::format

		constexpr NumericValue() = default;

		constexpr NumericValue(
			T value,
			T minValue = std::numeric_limits<T>::lowest(),
			T maxValue = std::numeric_limits<T>::max(),
			T step = T(0),
			std::string_view fmt = "{}"
		)
			: value(value), min(minValue), max(maxValue), step(step), format(fmt) {}

		void Set(T v) {
			value = SDL::Clamp(v, min, max);
		}

		template <typename U>
		U GetNorm() const {
			return (max > min) ? U(value - min) / U(max - min) : U(0);
		}
	};

	/// @brief Format a NumericValue's value as text (integer or fixed-point).
	template<typename T>
	std::string FormatNumeric(const NumericValue<T>& v) {
		if constexpr (std::is_integral_v<T>) {
			return std::to_string(v.value);
		} else if constexpr (std::is_floating_point_v<T>) {
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(v.decimals) << v.value;
			return oss.str();
		}
	}
	
	// ── Color convertion ────────────────────────────────────────────────
	
	/// @brief Convert RGB color to HSV floats in [0..1].
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

	/// @brief Convert HSV floats [0..1] to SDL::Color.
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
	// Tier 1 — Core components (every widget)
	// ==================================================================================

	struct Widget {
		std::string			name		= "";
		WidgetType			type		= WidgetType::Container;
		WidgetBehaviorFlag	behavior	= WidgetBehaviorFlag::Enable | WidgetBehaviorFlag::Visible | WidgetBehaviorFlag::DispatchEvent;
		WidgetStateFlag		states		= WidgetStateFlag::None;
		DirtyFlag			dirty		= DirtyFlag::All;
	};

	struct Style {
		// ── Background per state ─────────────────────────────────────────────
		SDL::Color bgColor          = { 22,  22,  30, 255};
		SDL::Color bgHoveredColor   = { 40,  42,  58, 255};
		SDL::Color bgPressedColor   = { 14,  14,  20, 255};
		SDL::Color bgCheckedColor   = { 55, 115, 195, 255};
		SDL::Color bgFocusedColor   = { 14,  14,  20, 255};
		SDL::Color bgDisabledColor  = { 22,  22,  28, 160};

		// ── Border per state ─────────────────────────────────────────────────
		SDL::Color bdColor          = { 55,  58,  78, 255};
		SDL::Color bdHoveredColor   = { 90,  95, 130, 255};
		SDL::Color bdPressedColor   = { 90,  95, 130, 255};
		SDL::Color bdCheckedColor   = { 90,  95, 130, 255};
		SDL::Color bdFocusedColor   = { 70, 130, 210, 255};
		SDL::Color bdDisabledColor  = { 90,  95, 130, 255};

		// ── Text per state ───────────────────────────────────────────────────
		SDL::Color textColor             = {215, 215, 220, 255};
		SDL::Color textHoveredColor      = {255, 255, 255, 255};
		SDL::Color textPressedColor      = {255, 255, 255, 255};
		SDL::Color textCheckedColor      = {255, 255, 255, 255};
		SDL::Color textDisabledColor     = {110, 110, 120, 200};
		SDL::Color textPlaceholderColor  = { 90,  92, 105, 200};

		// ── Accent colors (used by track/fill/thumb-style widgets) ───────────
		SDL::Color trackColor       = { 42,  44,  58, 255};
		SDL::Color fillColor        = { 70, 130, 210, 255};
		SDL::Color thumbColor       = {100, 160, 230, 255};
		SDL::Color separatorColor   = { 55,  58,  78, 255};

		// ── Tooltip ──────────────────────────────────────────────────────────
		SDL::Color tooltipBgColor   = { 30,  32,  44, 245};
		SDL::Color tooltipBdColor   = { 75,  80, 108, 255};
		SDL::Color tooltipTextColor = {215, 218, 228, 255};

		// ── Geometry ─────────────────────────────────────────────────────────
		SDL::FBox     borders = {1.f, 1.f, 1.f, 1.f};
		SDL::FCorners radius  = {5.f, 5.f, 5.f, 5.f};

		// ── Spacing ──────────────────────────────────────────────────────────
		SDL::FBox margin  = {0.f, 0.f, 0.f, 0.f};
		SDL::FBox padding = {8.f, 6.f, 8.f, 6.f};
		float     gap     = 4.f;

		// ── Font ─────────────────────────────────────────────────────────────
		std::string fontKey;
		float       fontSize = 0.f;
		FontType    usedFont = FontType::Inherited;

		// ── Misc ─────────────────────────────────────────────────────────────
		float opacity = 1.f;
		float scrollbarThickness = 8.f;

		std::string clickSound;
		std::string hoverSound;
		std::string scrollSound;
		std::string showSound;
		std::string hideSound;
	};

	struct LayoutProps {
		// ── Position & size ──────────────────────────────────────────────────
		Value absX      = Value::Px(0);
		Value absY      = Value::Px(0);
		Value width     = Value::Auto();
		Value height    = Value::Auto();
		Value minWidth  = Value::Px(-1.f);
		Value minHeight = Value::Px(-1.f);
		Value maxWidth  = Value::Px(-1.f);
		Value maxHeight = Value::Px(-1.f);

		// ── Flow & alignment ─────────────────────────────────────────────────
		Layout       layout         = Layout::InColumn;
		Align        alignChildrenH = Align::Stretch;
		Align        alignChildrenV = Align::Stretch;
		Align        alignSelfH     = Align::Stretch;
		Align        alignSelfV     = Align::Stretch;
		AttachLayout attach         = AttachLayout::Relative;
		BoxSizing    boxSizing      = BoxSizing::BorderBox;

		// ── Scroll state (host for any widget that scrolls its content) ──────
		float scrollX  = 0.f, scrollY  = 0.f;
		float contentW = 0.f, contentH = 0.f;
	};

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

	struct Callbacks {
		// ── Pointer ──────────────────────────────────────────────────────────
		std::function<void(SDL::MouseButton)>      onPress;
		std::function<void(SDL::MouseButton)>      onRelease;
		std::function<void(SDL::MouseButton)>      onClick;
		std::function<void(SDL::MouseButton)>      onDoubleClick;
		std::function<void(SDL::MouseButton, int)> onMultiClick;
		std::function<void()>           		   onMouseEnter;
		std::function<void()>           		   onMouseLeave;

		// ── Focus ────────────────────────────────────────────────────────────
		std::function<void()> onFocusGain;
		std::function<void()> onFocusLose;

		// ── Value-change ─────────────────────────────────────────────────────
		std::function<void(float)>                onChange;       ///< Generic float-valued change.
		std::function<void(SDL::Color)>           onColorChange;  ///< ColorPicker.
		std::function<void(const std::string &)>  onTextChange;
		std::function<void(bool)>                 onToggle;
		std::function<void(float)>                onScroll;
		std::function<void(SDL::FPoint, SDL::FPoint)> onScrollChange;

		// ── Item-list ────────────────────────────────────────────────────────
		std::function<void(int, bool)>  onTreeSelect;       ///< (index, hasChildren)
		std::function<void(int, int)>   onItemReorder;      ///< (fromIdx, toIdx)
	};

	// ==================================================================================
	// Tier 2 — Reusable add-ons
	// ==================================================================================

	// ── IconData ──────────────────────────────────────────────────────────────────
	/// @brief Decorative icon attached to a widget (Button, MenuBar item, …).
	///        When the host widget has text, the icon is drawn to its left;
	///        otherwise the icon is centered in the content box.
	struct IconData {
		std::string key;             ///< Resource-pool key of the texture (empty = no icon).
		float       pad = 4.f;       ///< Inset from the widget edges (px).

		float opacityNormal   = 1.f;
		float opacityHovered  = 1.f;
		float opacityPressed  = 0.85f;
		float opacityDisabled = 0.35f;

		SDL::Color tintNormalColor   = {255, 255, 255, 255};
		SDL::Color tintHoveredColor  = {255, 255, 255, 255};
		SDL::Color tintPressedColor  = {220, 220, 220, 255};
		SDL::Color tintDisabledColor = {180, 180, 180, 255};
	};

	// ── TooltipData ───────────────────────────────────────────────────────────────
	/// @brief Hover-tooltip — info bubble shown after a sustained hover.
	struct TooltipData {
		std::string text;
		float       delay = 1.f; ///< Hover duration before the bubble appears (seconds).
		bool        visible = true;
	};

	// ── BgGradient ────────────────────────────────────────────────────────────────
	enum class GradientAnchor : Uint8 {
		Top,
		Bottom,
		Left,
		Right,
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
		Center
	};

	/// @brief Gradient background — when present, replaces the solid `Style::bg*` fill.
	///        Each per-state `bg*` color is the gradient start; the matching
	///        `color2*` field below is the end color.
	struct BgGradient {
		SDL::Color color2         = { 40,  40,  60, 255};
		SDL::Color color2Hovered  = { 60,  62,  90, 255};
		SDL::Color color2Pressed  = { 20,  20,  35, 255};
		SDL::Color color2Checked  = { 80, 160, 240, 255};
		SDL::Color color2Focused  = { 20,  20,  35, 255};
		SDL::Color color2Disabled = { 28,  28,  38, 160};
		GradientAnchor start = GradientAnchor::Top;
		GradientAnchor end   = GradientAnchor::Bottom;
	};

	// ── TilesetData ──────────────────────────────────────────────────────────────
	/// @brief 9-slice tileset skin — when present, replaces the default
	///        solid/border/radius drawing with sliced tiles from a tileset texture.
	///
	/// Tile index layout (row-major, starting at @ref firstTileIdx):
	/// ```
	///  tl  tc  tr        0  1  2
	///  ml  mc  mr   →    3  4  5
	///  bl  bc  br        6  7  8
	/// ```
	struct TilesetData {
		int  tileW        = 16;
		int  tileH        = 16;
		int  tilesPerRow  = 3;
		int  firstTileIdx = 0;
		float borderW = 0.f; ///< Border thickness when slicing; 0 → use full tileW.
		float borderH = 0.f; ///< Border thickness when slicing; 0 → use full tileH.

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

	// ── TextCache ─────────────────────────────────────────────────────────────────
	/// @brief Cached TTF text object — created lazily by the renderer when a
	///        Label/Button/etc. has visible text.
	struct TextCache {
		SDL::Text text;
	};

	// ── TextEdit + TextSelection (shared by Input and TextArea) ───────────────────
	/// @brief Editable text buffer — UTF-8, LF line endings.
	///        Cursor and selection use byte offsets. Pair with @ref TextSelection
	///        for selection support and @ref TextSpans for rich text.
	struct TextEdit {
		std::string text;
		std::string placeholder;
		int   cursor     = 0;       ///< Byte offset of the insertion caret.
		float blinkTimer = 0.f;     ///< Phase [0,1) — caret visible while < 0.5.
		int   tabSize    = 4;       ///< Visual columns per tab stop (TextArea).
		bool  readOnly   = false;

		// ── Document navigation (LF-delimited lines) ─────────────────────────
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

	/// @brief Selection state — pair with @ref TextEdit.
	///        `anchor`/`focus` are byte offsets into `TextEdit::text`; the
	///        selected range is [min(anchor,focus), max(anchor,focus)).
	struct TextSelection {
		int        anchor          = -1;  ///< Fixed end of the selection (-1 = none).
		int        focus           = -1;  ///< Moving end of the selection (-1 = none).
		bool       dragging        = false;
		SDL::Color highlightColor  = {70, 130, 210, 90};

		[[nodiscard]] bool HasSelection() const noexcept {
			return anchor >= 0 && focus >= 0 && anchor != focus;
		}
		[[nodiscard]] int Min() const noexcept { return anchor < focus ? anchor : focus; }
		[[nodiscard]] int Max() const noexcept { return anchor > focus ? anchor : focus; }

		void Clear() noexcept { anchor = focus = -1; }
		void Set(int a, int f, int textSize) noexcept {
			anchor = std::clamp(a, 0, textSize);
			focus  = std::clamp(f, 0, textSize);
		}

		[[nodiscard]] std::string GetSelected(const std::string& src) const {
			if (!HasSelection()) return {};
			int a = std::clamp(Min(), 0, (int)src.size());
			int b = std::clamp(Max(), 0, (int)src.size());
			return (a < b) ? src.substr(a, b - a) : std::string{};
		}
		/// @brief Delete the selected range from `dst`, move `cursor` to the start,
		///        clear the selection, and return the number of bytes removed.
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

	// ── TextSpans (rich text runs) ────────────────────────────────────────────────

	/// @brief Per-run style override for a byte range in a text component.
	struct TextSpanStyle {
		bool       bold          = false;
		bool       italic        = false;
		bool       underline     = false;
		bool       strikethrough = false;
		bool       highlight     = false;
		SDL::Color color          = {0, 0, 0, 0}; ///< Alpha 0 = use widget default.
		SDL::Color highlightColor = {255, 255, 100, 80};
	};

	/// @brief Sorted, non-overlapping styled ranges layered over a TextEdit document.
	struct TextSpans {
		struct Span {
			int           start = 0;
			int           end   = 0; ///< [start, end) byte offsets.
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

		/// @brief Adjust span boundaries after an insert (delta>0) or erase (delta<0)
		///        at byte offset @p at.  Empty spans are removed.
		void Shift(int at, int delta) {
			for (auto& sp : spans) {
				if (sp.start >= at) sp.start = std::max(at, sp.start + delta);
				if (sp.end   >  at) sp.end   = std::max(at, sp.end   + delta);
			}
			std::erase_if(spans, [](const Span& s) { return s.start >= s.end; });
		}
	};

	// ── ItemListView (shared by ListBox / ComboBox / Tree dropdown) ──────────────
	/// @brief State common to all selectable item-list widgets.
	///        Specific widgets attach their own data alongside (e.g. ListBoxData).
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
		int        rows          = 0; ///< 0 = auto from children.
		GridSizing colSizing     = GridSizing::Fixed;
		GridSizing rowSizing     = GridSizing::Fixed;
		GridLines  lines         = GridLines::None;
		SDL::Color lineColor     = {55, 60, 88, 160};
		float      lineThickness = 1.f;

		// Computed each frame by the layout pass.
		std::vector<float> colWidths;
		std::vector<float> rowHeights;
	};

	/// @brief Cell placement of a child inside a Layout::InGrid container.
	struct GridCell {
		int col     = 0;
		int row     = 0;
		int colSpan = 1;
		int rowSpan = 1;
	};

	// ==================================================================================
	// Tier 3 — Widget-specific data
	// ==================================================================================

	// ── SliderData ────────────────────────────────────────────────────────────────
	/// @brief Slider behavior. Pair with @ref NumericValue for range/value.
	struct SliderData {
		Orientation        orientation  = Orientation::Horizontal;
		std::vector<float> markers;             ///< Normalized marker positions [0,1].

		// Drag state
		bool   drag         = false;
		float  dragStartPos = 0.f;
		double dragStartVal = 0.0;
	};

	// ── KnobData ──────────────────────────────────────────────────────────────────
	enum class KnobShape : Uint8 {
		Arc,           ///< Circular body with arc track + filled arc indicator.
		Potentiometer  ///< Solid dial with a single line/pointer indicator.
	};

	/// @brief Knob behavior. Pair with @ref NumericValue for range/value.
	struct KnobData {
		KnobShape shape = KnobShape::Arc;

		// Drag state (vertical drag and angular drag)
		bool   drag           = false;
		float  dragStartY     = 0.f;
		double dragStartVal   = 0.0;
		float  dragStartAngle = 0.f; ///< Angle (degrees) at drag start, for arc interaction.
	};

	// ── ProgressData ──────────────────────────────────────────────────────────────
	/// @brief Progress bar. Pair with @ref NumericValue for value.
	struct ProgressData {
		Orientation orientation = Orientation::Horizontal;
		bool isIndeterminate = false;
	};

	// ── ToggleData ────────────────────────────────────────────────────────────────
	struct ToggleData {
		bool  checked = false;
		float animT   = 0.f; ///< Animation phase [0..1].
	};

	// ── RadioData / RadioButtonData ───────────────────────────────────────────────
	struct RadioData {
		std::string group;
		bool        checked = false;
	};

	// ── SeparatorData ─────────────────────────────────────────────────────────────
	struct SeparatorData {
		Orientation orientation = Orientation::Horizontal;
	};

	// ── ScrollBarData ─────────────────────────────────────────────────────────────
	/// @brief Standalone scrollbar widget. (Inline scrollbars on Containers use
	///        @ref ContainerScrollState instead.)
	struct ScrollBarData {
		Orientation orientation = Orientation::Vertical;
		float       contentSize = 0.f;
		float       viewSize    = 0.f;
		float       offset      = 0.f;
		float       trackSize   = 8.f;

		// Drag state
		bool  drag         = false;
		float dragStartPos = 0.f;
		float dragStartOff = 0.f;
	};

	// ── ContainerScrollState ──────────────────────────────────────────────────────
	/// @brief Drag state of the inline scrollbars drawn automatically inside a
	///        Container with `(Auto)?Scrollable[XY]`. The actual scroll offset
	///        lives in `LayoutProps::scrollX/Y`; this component carries the
	///        thumb rects (for hit-testing) and the per-axis drag state.
	struct ContainerScrollState {
		// Thumb rects in screen space (refreshed every frame by the renderer).
		FRect thumbX = {};
		FRect thumbY = {};

		// Horizontal drag
		bool  dragX        = false;
		float dragStartX   = 0.f;
		float dragStartOff = 0.f;

		// Vertical drag
		bool  dragY          = false;
		float dragStartY     = 0.f;
		float dragStartOffY  = 0.f;
	};

	// ── InputData ─────────────────────────────────────────────────────────────────
	/// @brief Numeric / typed-input mode for the Input widget.
	struct InputData {
		InputFilterType filter = InputFilterType::Text;
		std::string     format = "{}"; ///< NumericValue representation

		// Optional child entities for the increment/decrement arrow buttons.
		ECS::EntityId incrementButton = ECS::NullEntity;
		ECS::EntityId decrementButton = ECS::NullEntity;

		// Drag-to-adjust state (vertical drag from the arrow column).
		bool   pressUp      = false;
		bool   pressDown    = false;
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
	/// @brief Scrollable list of selectable strings.
	///        Items / selection / itemHeight live in @ref ItemListView.
	///        Scroll position lives in `LayoutProps::scrollY`.
	struct ListBoxData {
		bool reorderable = false;

		// Drag-to-reorder state
		bool  dragActive = false;
		int   dragSrcIdx = -1;
		int   dragDstIdx = -1;
		float dragY      = 0.f;
		float dragStartY = 0.f;
		bool  dragMoved  = false;
	};

	// ── TextAreaData ──────────────────────────────────────────────────────────────
	/// @brief Multi-line text editor.
	///        Document/cursor/blink/readOnly/tabSize live in @ref TextEdit;
	///        selection in @ref TextSelection; rich text in @ref TextSpans.
	///        This struct only carries TextArea-specific state.
	struct TextAreaData {
		float scrollY = 0.f; ///< Vertical scroll offset (px from document top).
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
	/// @brief Dropdown list selector.
	///        Items / selection / hoverIndex / itemHeight / maxVisible live in
	///        @ref ItemListView. This struct carries dropdown-specific state.
	struct ComboBoxData {
		bool   open         = false;
		float  scrollOffset = 0.f;
		FRect  dropRect     = {};
	};

	// ── TabViewData ───────────────────────────────────────────────────────────────
	struct TabViewData {
		struct Tab {
			std::string label;
			bool        closable = false;
		};
		std::vector<Tab> tabs;             ///< Parallel with the widget's Children::ids.
		int   activeTab  = 0;
		float tabHeight  = 32.f;
		bool  tabsBottom = false;
		std::vector<float> tabWidths;      ///< Computed each frame by the renderer.
		TabLocation tabLocation = TabLocation::Top;
		std::function<void(int)> onTabChange;
	};

	// ── ExpanderData ──────────────────────────────────────────────────────────────
	struct ExpanderData {
		std::string label;
		bool        expanded = true;
		float       animT    = 1.f;
		float       headerH  = 28.f;
	};

	// ── SplitterData ──────────────────────────────────────────────────────────────
	/// @brief Resizable split panes (two children divided by a draggable handle).
	struct SplitterData {
		Orientation orientation = Orientation::Horizontal;
		float ratio      = 0.5f;
		float minRatio   = 0.05f;
		float maxRatio   = 0.95f;
		float handleSize = 6.f;

		// Drag state
		bool  dragging  = false;
		float dragStart = 0.f;
		float dragRatio = 0.f;
	};

	// ── SpinnerData ───────────────────────────────────────────────────────────────
	struct SpinnerData {
		float angle     = 0.f;
		float speed     = 6.28f; ///< rad/s.
		float arcSpan   = 0.65f; ///< Fraction of the full circle covered [0..1].
		float thickness = 3.f;
	};

	// ── BadgeData ─────────────────────────────────────────────────────────────────
	struct BadgeData {
		std::string text;
		std::string variant;  // e.g., "danger", "warning", "success", "info"
		SDL::Color  bgColor   = {220,  50,  40, 255};
		SDL::Color  textColor = {255, 255, 255, 255};
	};

	// ── ColorPickerData ───────────────────────────────────────────────────────────
	enum class ColorPickerPalette : Uint8 {
		Grayscale,   ///< 1-D bar: black → white.
		RGB8,        ///< 2-D HSV square + hue bar (8-bit output).
		RGBFloat,    ///< 2-D HSV square + hue bar (configurable precision step).
		GradientAB,  ///< 1-D bar: colorA → colorB.
	};

	struct ColorPickerData {
		ColorPickerPalette palette       = ColorPickerPalette::RGB8;
		SDL::Color         currentColor  = {255, 100,  50, 255};
		SDL::Color         colorA        = {  0,   0,   0, 255};
		SDL::Color         colorB        = {255, 255, 255, 255};
		float              precisionStep = 1.f / 255.f;
		bool               allowAlpha    = false;

		// Internal HSV state (RGB8 / RGBFloat)
		float hue   = 0.f;
		float sat   = 1.f;
		float val   = 1.f;
		float gradT = 0.f;

		// Drag state
		bool dragging    = false;
		bool draggingHue = false;
	};

	// ── PopupData ─────────────────────────────────────────────────────────────────
	/// @brief Floating window with optional title bar, drag, resize and close.
	struct PopupData {
		std::string title;
		bool  closable  = true;
		bool  draggable = true;
		bool  resizable = false;
		bool  modal     = false;
		bool  open      = true;
		float headerH   = 28.f;

		// Drag state
		bool        dragging   = false;
		SDL::FPoint dragOffset = {};
		SDL::FPoint pressPos   = {};

		// Resize state
		bool        resizing        = false;
		SDL::FPoint resizeStart     = {};
		SDL::FPoint resizeStartSize = {};

		// Custom header buttons (drawn left of the close button).
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

	struct TreeData {
		std::vector<TreeNodeData> nodes;
		int   selectedIndex = -1;
		float itemHeight    = 22.f;
		float indentSize    = 16.f;
		float iconSize      = 14.f;
		float scrollY       = 0.f;
		std::function<void(int, bool)> onToggleNode;
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

	struct MenuBarData {
		std::vector<MenuBarMenu> menus;
		std::vector<std::string> items;
		int openMenu = -1;
		int hovMenu  = -1;
		int hovItem  = -1;

		// Computed every render frame — used for hit-testing.
		struct MenuBtnRect { float x = 0.f, w = 0.f; };
		std::vector<MenuBtnRect> menuBtnRects;
		FRect dropRect = {};
		std::vector<FRect> itemRects;

		std::function<void(int)> onItemSelect;
	};

} // namespace SDL::UI
