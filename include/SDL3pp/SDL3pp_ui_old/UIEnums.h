#pragma once
#include "../SDL3pp_stdinc.h"

namespace SDL {
namespace UI {
	// ==================================================================================
	// Helper macro — generates the bitwise operators for a "flags"-style enum.
	// ==================================================================================
	#define SDL_UI_DECLARE_FLAG_OPERATORS(EnumT, UnderlyingT)                                     \
		inline EnumT  operator|  (EnumT a, EnumT b) noexcept { return static_cast<EnumT>(static_cast<UnderlyingT>(a) | static_cast<UnderlyingT>(b)); } \
		inline EnumT  operator&  (EnumT a, EnumT b) noexcept { return static_cast<EnumT>(static_cast<UnderlyingT>(a) & static_cast<UnderlyingT>(b)); } \
		inline EnumT  operator~  (EnumT a)          noexcept { return static_cast<EnumT>((~static_cast<UnderlyingT>(a)) & static_cast<UnderlyingT>(EnumT::All)); } \
		inline EnumT& operator|= (EnumT& a, EnumT b) noexcept { a = a | b; return a; }            \
		inline EnumT& operator&= (EnumT& a, EnumT b) noexcept { a = a & b; return a; }            \
		inline bool   operator!  (EnumT a)          noexcept { return a == EnumT::None; }         \
		inline bool   Has(EnumT a, EnumT b)         noexcept { return (a & b) != EnumT::None; }


	// ==================================================================================
	// Enums
	// ==================================================================================

	/// @brief Layout mode controlling how a container arranges its children.
	enum class Layout : Uint16 {
		InLine,   ///< Children placed horizontally (no wrap).
		InColumn, ///< Children stacked vertically.
		Stack,    ///< Like InLine but wraps when insufficient width.
		InGrid    ///< Children placed on a 2-D grid (see LayoutGridProps / GridCell).
	};

	/// @brief How column or row sizes are determined in a Layout::InGrid container.
	enum class GridSizing : Uint8 {
		Fixed,   ///< Uniform cells: available space divided equally by column/row count.
		Content  ///< Adaptive cells: each column/row is sized to its widest/tallest child.
	};

	/// @brief Which separator lines are drawn inside a Layout::InGrid container.
	enum class GridLines : Uint8 {
		None,    ///< No visible separator lines.
		Rows,    ///< Horizontal lines between rows only.
		Columns, ///< Vertical lines between columns only.
		Both     ///< Both horizontal and vertical lines.
	};

	/// @brief How a widget is positioned relative to its parent or the root viewport.
	enum class AttachLayout : Uint16 {
		Relative,          ///< Normal flow.
		Absolute,          ///< Absolute inside parent (bypasses flow; not scroll-offset).
		Fixed,             ///< Fixed relative to root viewport.

		Parent = Absolute, ///< Positioned within / clipped by the tree parent (default flow).
		Root   = Fixed,    ///< Portaled to the root entity: clipped only by the root rect.
		Window,            ///< Portaled to the live window: clipped only by the viewport.
	};

	/// @brief Controls what the explicit W/H values include (CSS box-model equivalent).
	enum class BoxSizing : Uint16 {
		ContentBox, ///< W/H = content only.
		PaddingBox, ///< W/H = content + padding.
		BorderBox,  ///< W/H = content + padding + bdColor.
		MarginBox   ///< W/H = content + padding + bdColor + margin.
	};

	/// @brief Bitmask that tracks which sub-systems need to reprocess a widget this frame.
	enum class DirtyFlag : Uint16 {
		None   		= 0,
		Input 		= 1 << 0,
		Focus 		= 1 << 1,
		Animation 	= 1 << 2,
		Style  		= 1 << 3,
		Measure 	= 1 << 4,
		Layout 		= 1 << 5,
		Clip   		= 1 << 6,
		Culling 	= 1 << 7,
		Index  		= 1 << 8,
		Batching 	= 1 << 9,
		Render 		= 1 << 10,
		All    		= 0x7FF
	};
	SDL_UI_DECLARE_FLAG_OPERATORS(DirtyFlag, Uint16)

	/// @brief Bitmask controlling what interactions a widget participates in.
	enum class WidgetBehaviorFlag : Uint16 {
		None            = 0,
		Visible         = 1 << 0,
		Enable          = 1 << 1,
		Hoverable       = 1 << 2,
		Selectable      = 1 << 3,
		Focusable       = 1 << 4,
		Resizable       = 1 << 5,
		Draggable       = 1 << 6,
		ScrollableX     = 1 << 7,
		ScrollableY     = 1 << 8,
		AutoScrollableX = 1 << 9,
		AutoScrollableY = 1 << 10,
		DispatchEvent   = 1 << 11, ///< Dispatch unused event to parent widgets.
		All             = 0x0FFF
	};
	SDL_UI_DECLARE_FLAG_OPERATORS(WidgetBehaviorFlag, Uint16)

	enum class WidgetStateFlag : Uint8 {
		None			= 0,
		Hovered			= 1 << 0,
		Pressed			= 1 << 1,
		Focused			= 1 << 2,
		Resized 		= 1 << 3,
		Dragged			= 1 << 4,
		Checked			= 1 << 5,
		All				= 0x3F
	};
	SDL_UI_DECLARE_FLAG_OPERATORS(WidgetStateFlag, Uint8)

	/// @brief Alignment of children along the cross axis of a layout container.
	enum class Align : Uint8 {
		Start,
		Center,
		End,
		Stretch,

		Left   = Start,
		Top    = Start,
		Middle = Center,
		Right  = End,
		Bottom = End
	};

	/// @brief Orientation for sliders, scroll bars, separators, and progress bars.
	enum class Orientation : Uint8 {
		Horizontal,
		Vertical
	};

	/// @brief Filter / value mode for the Input widget.
	///
	/// Selects how typed characters are validated and (for numeric modes) how
	/// the value is parsed and formatted. Numeric modes also enable the right-
	/// edge ↑/↓ arrow buttons.
	enum class InputFilterType : Uint8 {
		None,
		// text
		Text,
		Multiline,
		// structured text
		Email,
		URL,
		Phone,
		Username,
		// numeric (typed numeric inputs back NumericValue<T> created by MakeInputValue)
		Integer,
		Float,
		Hex,
		// charset (single-char restriction)
		Digits,
		Alpha,
		Alnum,
		// advanced (filename-safe / URL-safe slug)
		Slug,
		Filename,
		// fully user-driven (the application installs its own onValidate callback)
		Custom,

		// ── Legacy aliases (kept for backwards compatibility with older code) ──
		IntegerValue = Integer, ///< @deprecated Use Integer.
		FloatValue   = Float,   ///< @deprecated Use Float.
		Mail         = Email,   ///< @deprecated Use Email.
		Url          = URL,     ///< @deprecated Use URL.
	};

	enum class ImageFit : Uint8 {
		Fill,    ///< Stretch to fill the widget's content box (may distort aspect ratio).
		Contain, ///< Scale to fit the widget's content box while preserving aspect ratio (may letterbox).
		Cover,   ///< Scale to fill the widget's content box while preserving aspect ratio (may crop).
		Tile,    ///< Repeat the image to fill the widget's content box (preserves aspect ratio).
		None     ///< No scaling; place the image at the top-left of the content box (may overflow).
	};

	// @brief Timing function for a transition / animation (CSS-equivalent easings).
	enum class Easing : Uint8 {
		Linear,     ///< Constant rate.
		EaseIn,     ///< Slow start (quadratic).
		EaseOut,    ///< Slow end (quadratic) — the UI default.
		EaseInOut,  ///< Slow start and end (cubic smoothstep).
		EaseOutBack ///< Overshoots slightly then settles (springy).
	};

	/// @brief Which transition channels of a widget the AnimationSystem drives.
	enum class AnimChannel : Uint16 {
		None     = 0,
		Hover    = 1 << 0, ///< Blend toward hovered colors while pointer is over.
		Press    = 1 << 1, ///< Blend toward pressed colors while held.
		Focus    = 1 << 2, ///< Blend toward focused colors while focused.
		Checked  = 1 << 3, ///< Blend toward checked colors while checked.
		Opacity  = 1 << 4, ///< Fade opacity in/out on show/hide.
		Toggle   = 1 << 5, ///< Slide the Toggle knob between off/on.
		Expand   = 1 << 6, ///< Animate Expander / collapsible reveal.
		Scroll   = 1 << 7, ///< Smooth scrolling toward the target offset.
		Color    = Hover | Press | Focus | Checked,
		All      = 0xFFFF
	};
	SDL_UI_DECLARE_FLAG_OPERATORS(AnimChannel, Uint16)


} // namespace UI

} // namespace SDL
