#pragma once
#include "../SDL3pp_stdinc.h"

namespace SDL {
namespace UI {

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
		Relative, ///< Normal flow.
		Absolute, ///< Absolute inside parent (bypasses flow; not scroll-offset).
		Fixed     ///< Fixed relative to root viewport.
	};

	/// @brief Controls what the explicit W/H values include (CSS box-model equivalent).
	enum class BoxSizing : Uint16 {
		ContentBox, ///< W/H = content only.
		PaddingBox, ///< W/H = content + padding.
		BorderBox,  ///< W/H = content + padding + bdColor.
		MarginBox   ///< W/H = content + padding + bdColor + margin.
	};

	/// @brief Bitmask that tracks which sub-systems need to reprocess a widget this frame.
	enum class DirtyFlag : Uint8 {
		None = 0,
		Style = 1 << 0,
		Layout = 1 << 1,
		Render = 1 << 2,
		All = Style | Layout | Render
	};
	inline DirtyFlag operator|(DirtyFlag a, DirtyFlag b) noexcept { return static_cast<DirtyFlag>(static_cast<Uint8>(a) | static_cast<Uint8>(b)); }
	inline DirtyFlag operator&(DirtyFlag a, DirtyFlag b) noexcept { return static_cast<DirtyFlag>(static_cast<Uint8>(a) & static_cast<Uint8>(b)); }
	inline DirtyFlag operator~(DirtyFlag a) noexcept { return static_cast<DirtyFlag>((~static_cast<Uint8>(a)) & static_cast<Uint8>(DirtyFlag::All)); }
	inline DirtyFlag &operator|=(DirtyFlag &a, DirtyFlag b) noexcept { a = a | b; return a; }
	inline DirtyFlag &operator&=(DirtyFlag &a, DirtyFlag b) noexcept { a = a & b; return a; }
	inline bool operator!(DirtyFlag a) noexcept { return a == DirtyFlag::None; }
	/** @brief Returns true if all bits of @p b are set in @p a. */
	inline bool Has(DirtyFlag &a, DirtyFlag b) { return (a & b) != DirtyFlag::None; }

	/// @brief Bitmask controlling what interactions a widget participates in.
	enum class BehaviorFlag : Uint16 {
		None            = 0,
		Enable          = 1 << 0,
		Visible         = 1 << 1,
		Hoverable       = 1 << 2,
		Selectable      = 1 << 3,
		Focusable       = 1 << 4,
		Resizable       = 1 << 5,
		Draggable       = 1 << 6,
		ScrollableX     = 1 << 7,
		ScrollableY     = 1 << 8,
		AutoScrollableX = 1 << 9,
		AutoScrollableY = 1 << 10,
		PropagateEvent  = 1 << 11, ///< Propagated unused event to parent widgets.
		All             = 0x0FFF
	};
	
	inline BehaviorFlag operator|(BehaviorFlag a, BehaviorFlag b) noexcept { return static_cast<BehaviorFlag>(static_cast<Uint16>(a) | static_cast<Uint16>(b)); }
	inline BehaviorFlag operator&(BehaviorFlag a, BehaviorFlag b) noexcept { return static_cast<BehaviorFlag>(static_cast<Uint16>(a) & static_cast<Uint16>(b)); }
	inline BehaviorFlag operator~(BehaviorFlag a) noexcept { return static_cast<BehaviorFlag>((~static_cast<Uint16>(a)) & static_cast<Uint16>(BehaviorFlag::All)); }
	inline BehaviorFlag &operator|=(BehaviorFlag &a, BehaviorFlag b) noexcept { a = a | b; return a; }
	inline BehaviorFlag &operator&=(BehaviorFlag &a, BehaviorFlag b) noexcept { a = a & b; return a; }
	inline bool operator!(BehaviorFlag a) noexcept { return a == BehaviorFlag::None; }
	/** @brief Returns true if all bits of @p b are set in @p a. */
	inline bool Has(BehaviorFlag a, BehaviorFlag b) { return (a & b) != BehaviorFlag::None; }

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

	/*enum class Easing : Uint8 {
		Linear,
		EaseIn,
		EaseOut,
		EaseInOut,
		Bounce
	};*/

	enum class ImageFit : Uint8 {
		Fill,    ///< Stretch to fill the widget's content box (may distort aspect ratio).
		Contain, ///< Scale to fit the widget's content box while preserving aspect ratio (may letterbox).
		Cover,   ///< Scale to fill the widget's content box while preserving aspect ratio (may crop).
		Tile,    ///< Repeat the image to fill the widget's content box (preserves aspect ratio).
		None     ///< No scaling; place the image at the top-left of the content box (may overflow).
	};


} // namespace UI
} // namespace SDL
