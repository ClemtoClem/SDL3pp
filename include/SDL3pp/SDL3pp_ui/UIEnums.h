#pragma once
#include "../SDL3pp_stdinc.h"

namespace SDL::UI {

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
	// Widget identification
	// ==================================================================================

	/// @brief Identifies the high-level kind of a widget. Used for built-in renderers
	///        and dispatch fallbacks; new widgets are encouraged to drive behavior
	///        via component presence rather than enum comparison.
	enum class WidgetType : Uint8 {
		Unknown = 0,
		Container,
		Label,
		Input,
		Button,
		Toggle,
		Radio,
		Knob,
		Slider,
		ScrollBar,
		Progress,
		Separator,
		Image,
		Canvas,
		TextArea,
		ListBox,
		Graph,
		ComboBox,
		TabView,
		Expander,
		Splitter,
		Spinner,
		Badge,
		ColorPicker,
		Popup,
		Tree,
		MenuBar
	};

	// ==================================================================================
	// Layout
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
		None,
		Rows,
		Columns,
		Both
	};

	/// @brief How a widget is positioned relative to its parent or the root viewport.
	enum class AttachLayout : Uint16 {
		Relative,
		Absolute, ///< Absolute inside parent (bypasses flow; not scroll-offset).
		Fixed     ///< Fixed relative to root viewport.
	};

	/// @brief Rendering / event plane (z-order layer).
	///
	/// Resolved per frame by IndexSystem into a single ordered draw + hit-test list.
	/// Higher layers draw on top of and receive events before lower ones; within the
	/// same layer, document (tree pre-order) position decides. A widget on the default
	/// `Content` layer inherits its parent's resolved layer, so an entire popup subtree
	/// sits on the popup plane without tagging every descendant.
	enum class UILayer : int {
		Background = -1000, ///< Behind normal content (backdrops, watermarks).
		Content    = 0,     ///< Default plane for in-flow widgets.
		Floating   = 1000,  ///< Lifted in-flow widgets (drag ghosts, raised cards).
		Dropdown   = 2000,  ///< ComboBox / MenuBar drop-downs.
		Popup      = 3000,  ///< Popups and dialogs anchored to content.
		Tooltip    = 4000,  ///< Hover tooltips.
		Modal      = 5000,  ///< Modal dialogs and their scrim.
		Top        = 6000   ///< Always-on-top (debug overlays, notifications).
	};

	/// @brief Anchor used by a floating widget (popup, dropdown, tooltip) for both
	///        positioning and clipping — i.e. which entity it is "portaled" onto.
	enum class PopupAttach : Uint8 {
		Parent, ///< Positioned within / clipped by the tree parent (default flow).
		Root,   ///< Portaled to the root entity: clipped only by the root rect.
		Window, ///< Portaled to the live window: clipped only by the viewport.
		Target  ///< Portaled to an explicit entity (LayerProps::attachTarget).
	};

	/// @brief Controls what the explicit W/H values include (CSS box-model equivalent).
	enum class BoxSizing : Uint16 {
		ContentBox, ///< W/H = content only.
		PaddingBox, ///< W/H = content + padding.
		BorderBox,  ///< W/H = content + padding + border.
		MarginBox   ///< W/H = content + padding + border + margin.
	};

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

	// ==================================================================================
	// Bitmask flags — dirty / behavior / state
	// ==================================================================================

	/// @brief Bitmask that tracks which sub-systems need to reprocess a widget this frame.
	enum class DirtyFlag : Uint16 {
		None   		= 0,
		Input 		= 1 << 0, // reserved for future EventSystem optimizations
		Focus 		= 1 << 1, // reserved for future FocusSystem optimizations
		Animation 	= 1 << 2, // reserved for future AnimationSystem optimizations
		Style  		= 1 << 3, // reserved for future StyleSystem optimizations
		Measure 	= 1 << 4, // reserved for future MeasureSystem optimizations
		Layout 		= 1 << 5, // reserved for future LayoutSystem optimizations
		Clip   		= 1 << 6, // reserved for future ClipSystem optimizations
		Culling 	= 1 << 7, // reserved for future CullingSystem optimizations
		Index  		= 1 << 8, // reserved for future IndexSystem optimizations
		Batching 	= 1 << 9, // reserved for future BatchingSystem optimizations
		Render 		= 1 << 10, // reserved for future RenderSystem optimizations
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
		Editable        = 1 << 7,
		ScrollableX     = 1 << 8,
		ScrollableY     = 1 << 9,
		AutoScrollableX = 1 << 10,
		AutoScrollableY = 1 << 11,
		DispatchEvent   = 1 << 12, ///< Dispatch unused event to parent widgets.
		All             = 0x1FFF
	};
	SDL_UI_DECLARE_FLAG_OPERATORS(WidgetBehaviorFlag, Uint16)

	/// @brief Bitmask describing the live interaction state of a widget.
	enum class WidgetStateFlag : Uint8 {
		None    = 0,
		Hovered = 1 << 0,
		Pressed = 1 << 1,
		Focused = 1 << 2,
		Resized = 1 << 3,
		Dragged = 1 << 4,
		Checked = 1 << 5,
		All     = 0x3F
	};
	SDL_UI_DECLARE_FLAG_OPERATORS(WidgetStateFlag, Uint8)

	// ==================================================================================
	// Style enums
	// ==================================================================================

	/// @brief Source of the resolved font for a text-bearing widget.
	enum class FontType : Uint8 {
		Inherited, ///< Walk ancestors to find a configured font; fall back to Default.
		Self,      ///< Use the font configured on this entity; if empty, behaves as Inherited.
		Root,      ///< Use the root widget's font; fall back to Default.
		Default,   ///< Use the engine's default font.
		Debug      ///< Force the SDL3 built-in debug font.
	};

	/// @brief Filter / value mode for the Input widget.
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
		// numeric
		Integer,
		Float,
		Hex,
		// charset
		Digits,
		Alpha,
		Alnum,
		// advanced
		Slug,
		Filename,
		// custom
		Custom
	};

	/// @brief How an Image widget fits its source into the content box.
	enum class ImageFit : Uint8 {
		Fill,    ///< Stretch to fill (may distort aspect ratio).
		Contain, ///< Scale to fit while preserving aspect ratio.
		Cover,   ///< Scale to cover while preserving aspect ratio (may crop).
		Tile,    ///< Repeat to fill.
		None     ///< No scaling; top-left placement.
	};

	/// @brief Horizontal text alignment.
	enum class TextHAlign : Uint8 { Left, Center, Right };

	/// @brief Vertical text alignment.
	enum class TextVAlign : Uint8 { Top, Center, Bottom };

	/// @brief Tab bar location in a TabView widget.
	enum class TabLocation : Uint8 { Top, Bottom };

	// ==================================================================================
	// Animation enums
	// ==================================================================================

	/// @brief Timing function for a transition / animation (CSS-equivalent easings).
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

	/// @brief Anchor used by a BackgroundStyle gradient.
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

} // namespace SDL::UI