#pragma once
#include "value.h"
#include "../SDL3pp_ecs.h"
#include "../SDL3pp_events.h"
#include "../SDL3pp_ttf.h"
#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace SDL {
namespace UI {

	// ==================================================================================
	// WidgetType
	// ==================================================================================

	enum class WidgetType : Uint8 {
		Container, ///< Container is a basic layout widget that can have children and optional scrollbars.  It does not render anything itself but can have background and border styles.
		Label,     ///< Label is a simple text display widget with optional word wrap and text alignment.
		Input,     ///< Input is a single-line text editor with cursor, selection, and copy/paste support.  It can be used for password fields (with placeholder chars) or as a base for more complex text widgets.
		Button,    ///< Button is a clickable widget that triggers an action when pressed.  It can display text and/or an image, and has visual states for normal, hovered, pressed, and disabled.
		Toggle,    ///< Toggle is a binary switch that can be checked or unchecked, and maintains its state until toggled again.
		RadioButton, ///< RadioButton is a toggle that belongs to a named group and auto-unchecks when another in the same group is checked.
		Knob,      ///< Knob is a circular slider with infinite rotation and optional snapping.
		Slider,    ///< Slider can be horizontal or vertical based on its orientation property.
		ScrollBar, ///< ScrollBar can be horizontal or vertical based on its orientation property.
		Progress,  ///< Progress bar can be horizontal or vertical based on its orientation property.
		Separator, ///< Horizontal or vertical line (non-interactive).
		Image,     ///< Image from texture with fit modes (Cover, Contain, Fill, Tile, None).
		Canvas,    ///< Custom render callback with full access to the renderer and layout rect.
		TextArea,  ///< Multi-line rich text editor with selection, copy/paste, drag-drop.
		ListBox,   ///< Scrollable list of selectable text items with keyboard navigation.
		Graph,     ///< Graduated data plot (Excel-style axes, grid, fill, bar or line mode).
		ComboBox,    ///< Dropdown selector — click to open a popup list of items.
		TabView,     ///< Tabbed container — one child visible at a time via a tab bar.
		Expander,    ///< Collapsible section — header click shows/hides child content.
		Splitter,    ///< Resizable split panes — two children divided by a draggable handle.
		Spinner,     ///< Animated loading indicator (rotating arc, updated by _ProcessAnimate).
		Badge,       ///< Small notification pill displaying a text count.
		ColorPicker, ///< Color palette picker (Grayscale, RGB8, RGBFloat, GradientAB).
		Popup,       ///< Floating window with title bar, draggable, resizable, closable.
		Tree,        ///< Hierarchical tree view with expandable/collapsible nodes.
		MenuBar,     ///< Horizontal application-style menu bar with dropdown sub-menus.
	};

	// ==================================================================================
	// ECS Components
	// ==================================================================================

	struct Widget {
		std::string name;
		WidgetType  type         = WidgetType::Container;
		BehaviorFlag behavior    = BehaviorFlag::Enable | BehaviorFlag::Visible;
		DirtyFlag   dirty        = DirtyFlag::All;
		bool dispatchEvent       = true; ///< When false, unhandled scroll events are NOT propagated to parent widgets.
	};

	enum class FontType: Uint8 {
		Inherited, // Used herited to from last widget used self font in heraichie until root. If root dosen't have confogurer font, used default font or debug font.
		Self,    // Used self font configured, if fontKey is empty used herited font
		Root,    // Used default font firstly, if fontKey is empty used default font
		Default, // Used default font or debug sdl3 utf8 font if fontKey is empty if or m_usedDebugFontPerDefault is true
		Debug    // Used debug sdl3 utf8 font per
	};

	struct Style {
		SDL::Color bgColor          = {22, 22, 30, 255};
		SDL::Color bgHoveredColor	= {40, 42, 58, 255};
		SDL::Color bgPressedColor	= {14, 14, 20, 255};
		SDL::Color bgCheckedColor	= {55, 115, 195, 255};
		SDL::Color bgFocusedColor	= {14, 14, 20, 255};
		SDL::Color bgDisabledColor	= {22, 22, 28, 160};

		SDL::Color bdColor          = {55, 58, 78, 255};
		SDL::Color bdHoveredColor	= {90, 95, 130, 255};
		SDL::Color bdPressedColor	= {90, 95, 130, 255};
		SDL::Color bdCheckedColor	= {90, 95, 130, 255};
		SDL::Color bdFocusedColor	= {70, 130, 210, 255};
		SDL::Color bdDisabledColor	= {90, 95, 130, 255};

		SDL::Color textColor        	= {215, 215, 220, 255};
		SDL::Color textHoveredColor		= {255, 255, 255, 255};
		SDL::Color textPressedColor		= {255, 255, 255, 255};
		SDL::Color textCheckedColor		= {255, 255, 255, 255};
		SDL::Color textDisabledColor	= {110, 110, 120, 200};
		SDL::Color textPlaceholderColor	= {90, 92, 105, 200};

		SDL::Color trackColor       = {42, 44, 58, 255};
		SDL::Color fillColor        = {70, 130, 210, 255};
		SDL::Color thumbColor       = {100, 160, 230, 255};
		SDL::Color separatorColor   = {55, 58, 78, 255};

		SDL::Color tooltipBgColor	= { 30,  32,  44, 245};  ///< Fond de l'info-bulle.
		SDL::Color tooltipBdColor	= { 75,  80, 108, 255};  ///< Bordure de l'info-bulle.
		SDL::Color tooltipTextColor	= {215, 218, 228, 255};  ///< Texte de l'info-bulle.

		SDL::FBox     borders       = {1.f, 1.f, 1.f, 1.f};
		SDL::FCorners radius        = {5.f, 5.f, 5.f, 5.f};

		std::string fontKey;
		float fontSize              = 0.f;
		FontType usedFont           = FontType::Inherited;

		float opacity               = 1.f;

		std::string clickSound;
		std::string hoverSound;
		std::string scrollSound;
		std::string showSound;
		std::string hideSound;
	};

	// ==================================================================================
	// BgGradient — optional gradient background component
	// ==================================================================================

	/// @brief Gradient anchor for BgGradient backgrounds.
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

	/// @brief Optional ECS component that replaces a widget's solid background with a gradient.
	///
	/// When present, each per-state `bgColor` from `Style` is used as the gradient
	/// start color, and the matching `color2*` field here is the end color.
	/// Attach via the builder: `.BgGradient(endColor)` or per-state setters.
	struct BgGradient {
		SDL::Color color2         = { 40,  40,  60, 255}; ///< Normal state end color.
		SDL::Color color2Hovered  = { 60,  62,  90, 255}; ///< Hovered end color.
		SDL::Color color2Pressed  = { 20,  20,  35, 255}; ///< Pressed end color.
		SDL::Color color2Checked  = { 80, 160, 240, 255}; ///< Checked end color.
		SDL::Color color2Focused  = { 20,  20,  35, 255}; ///< Focused end color.
		SDL::Color color2Disabled = { 28,  28,  38, 160}; ///< Disabled end color.
		GradientAnchor start      = GradientAnchor::Top;  ///< Gradient start.
		GradientAnchor end		  = GradientAnchor::Bottom; ///< Gradient end.
	};

	struct LayoutProps {
		Value absX          = Value::Px(0);
		Value absY          = Value::Px(0);
		Value width         = Value::Auto();
		Value height        = Value::Auto();
		Value minWidth      = Value::Px(-1.f);  ///< Minimum width;  Px(-1) = no constraint.
		Value minHeight     = Value::Px(-1.f);  ///< Minimum height; Px(-1) = no constraint.
		Value maxWidth      = Value::Px(-1.f);  ///< Maximum width;  Px(-1) = no constraint.
		Value maxHeight     = Value::Px(-1.f);  ///< Maximum height; Px(-1) = no constraint.

		SDL::FBox margin    = {0.f, 0.f, 0.f, 0.f};
		SDL::FBox padding   = {8.f, 6.f, 8.f, 6.f};
		Layout layout       = Layout::InColumn;
		Align alignChildrenH= Align::Stretch;  ///< Default cross-axis alignment for children in InColumn (horizontal).
		Align alignChildrenV= Align::Stretch;  ///< Default cross-axis alignment for children in InRow / Stack (vertical).
		Align alignSelfH    = Align::Stretch;  ///< Cross-axis alignment in InColumn (horizontal).
		Align alignSelfV    = Align::Stretch;  ///< Cross-axis alignment in InRow / Stack (vertical).
		AttachLayout attach = AttachLayout::Relative;
		BoxSizing boxSizing = BoxSizing::BorderBox;
		float gap 			= 4.f;             ///< Gap between children in InColumn / InLine / Stack (px).  Does not apply to Separator.
		float scrollX = 0.f, scrollY = 0.f;
		float contentW = 0.f, contentH = 0.f;

		/// Thickness (px) of the auto inline scrollbar drawn by _DrawContainer.
		/// Applies to both axes.  Override via builder .ScrollbarThickness(n).
		float scrollbarThickness = 8.f;
	};

	/// @brief Grid layout configuration — attach to a Container with Layout::InGrid.
	///
	/// Add this component via the builder: `.GridProps(cols, rows, colSizing, rowSizing)`
	/// or individually via `.GridCols(n)`, `.GridRows(n)`, `.GridColSizing(...)`, etc.
	///
	/// Column widths and row heights are recomputed every frame during the Measure pass
	/// and stored in `colWidths` / `rowHeights` for use by the Place pass and renderer.
	struct LayoutGridProps {
		int        columns       = 2;                      ///< Number of columns (min 1).
		int        rows          = 0;                      ///< Number of rows; 0 = auto-computed from children.
		GridSizing colSizing     = GridSizing::Fixed;      ///< How column widths are determined.
		GridSizing rowSizing     = GridSizing::Fixed;      ///< How row heights are determined.
		GridLines  lines         = GridLines::None;        ///< Which separator lines are drawn.
		SDL::Color lineColor     = {55, 60, 88, 160};     ///< Color of the separator lines.
		float      lineThickness = 1.f;                   ///< Thickness in pixels of the separator lines.

		// Computed each frame by _Measure; consumed by _Place and _DrawContainer.
		std::vector<float> colWidths;   ///< Resolved pixel width of each column (filled by _Measure).
		std::vector<float> rowHeights;  ///< Resolved pixel height of each row (filled by _Measure).
	};

	/// @brief Grid cell placement — attach to a child widget inside a Layout::InGrid container.
	///
	/// Specifies where the child sits in the parent grid and how many cells it spans.
	/// If no GridCell component is present on a child, it is auto-placed sequentially
	/// (left-to-right, top-to-bottom) filling columns first.
	///
	/// Set via the builder: `.Cell(col, row)` or `.Cell(col, row, colSpan, rowSpan)`.
	struct GridCell {
		int col     = 0;  ///< Column index (0-based).
		int row     = 0;  ///< Row index (0-based).
		int colSpan = 1;  ///< Number of columns occupied (min 1).
		int rowSpan = 1;  ///< Number of rows occupied (min 1).
	};

	struct ComputedRect {
		FRect screen = {}, clip = {}, outer_clip = {};
		FPoint measured = {};
	};

	/// @brief Ordered list of child entity IDs maintained by the tree-management API.
	struct Children {
		std::vector<ECS::EntityId> ids;
		/** @brief Append a child entity. */
		void Add(ECS::EntityId e) { ids.push_back(e); }
		/** @brief Remove a child entity (no-op if not present). */
		void Remove(ECS::EntityId e) { std::erase(ids, e); }
	};

	struct Parent {
		ECS::EntityId id = ECS::NullEntity;
	};

	struct Content {
		std::string text, placeholder;
		int cursor = 0;
		float blinkTimer = 0.f;

		// ── Selection ──────────────────────────────────────────────────────────
		int  selAnchor = -1;
		int  selFocus  = -1;
		bool selectDragging = false;
		SDL::Color highlightColor = {70, 130, 210, 90};

		/** @brief Returns true when a non-empty selection exists. */
		[[nodiscard]] bool HasSelection() const noexcept {
			return selAnchor >= 0 && selFocus >= 0 && selAnchor != selFocus;
		}
		/** @brief Returns the byte offset of the selection start (min of anchor/focus). */
		[[nodiscard]] int SelMin() const noexcept { return (selAnchor < selFocus) ? selAnchor : selFocus; }
		/** @brief Returns the byte offset of the selection end (max of anchor/focus). */
		[[nodiscard]] int SelMax() const noexcept { return (selAnchor > selFocus) ? selAnchor : selFocus; }

		/** @brief Returns the currently selected substring, or empty if no selection. */
		[[nodiscard]] std::string GetSelectedText() const {
			if (!HasSelection()) return {};
			int a = std::clamp(SelMin(), 0, (int)text.size());
			int b = std::clamp(SelMax(), 0, (int)text.size());
			return (a < b) ? text.substr(a, b - a) : std::string{};
		}
		
		/** @brief Clears the selection (sets both anchor and focus to -1). */
		void ClearSelection() noexcept { selAnchor = selFocus = -1; }

		/**
		 * @brief Set the selection range, clamping to valid text positions.
		 * @param anchor  Fixed end of the selection (where drag/shift started).
		 * @param focus   Moving end of the selection (current cursor position).
		 */
		void SetSelection(int anchor, int focus) noexcept {
			int sz = (int)text.size();
			selAnchor = std::clamp(anchor, 0, sz);
			selFocus  = std::clamp(focus,  0, sz);
		}

		/** @brief Deletes the selected text and moves the cursor to the selection start. */
		void DeleteSelection() {
			if (!HasSelection()) return;
			int a = std::clamp(SelMin(), 0, (int)text.size());
			int b = std::clamp(SelMax(), 0, (int)text.size());
			if (a < b) text.erase(a, b - a);
			cursor = a;
			ClearSelection();
		}
	};

	struct SliderData {
		float min = 0.f, max = 1.f, val = 0.f;
		bool drag = false;
		float dragStartPos = 0.f, dragStartVal = 0.f;
		Orientation orientation = Orientation::Horizontal;
		std::vector<float> markers; ///< Normalized marker positions [0,1] (e.g. chapters).
	};

	struct ScrollBarData {
		float contentSize = 0.f, viewSize = 0.f, offset = 0.f;
		bool drag = false;
		float dragStartPos = 0.f, dragStartOff = 0.f;
		Orientation orientation = Orientation::Vertical;
	};

	/**
	 * Tracks the interactive drag state of the **inline** scrollbars drawn
	 * automatically inside a Container that has AutoScrollableX / AutoScrollableY.
	 *
	 * Added by `_Make` when the widget type is `Container`; queried by `_ProcessInput`
	 * and written by `_DrawContainer` (thumb rects) so hit-testing is possible.
	 */
	struct ContainerScrollState {
		// Thumb rects in screen space (updated every frame by _DrawContainer).
		FRect thumbX = {};   ///< Horizontal thumb rect (empty when not shown).
		FRect thumbY = {};   ///< Vertical thumb rect   (empty when not shown).

		// Drag state for horizontal bar.
		bool  dragX        = false;
		float dragStartX   = 0.f; ///< Mouse X at drag start.
		float dragStartOff = 0.f; ///< scrollX at drag start.

		// Drag state for vertical bar.
		bool  dragY         = false;
		float dragStartY_   = 0.f; ///< Mouse Y at drag start.
		float dragStartOffY = 0.f; ///< scrollY at drag start.
	};

	struct ToggleData {
		bool checked = false;
		float animT = 0.f;
	};

	struct RadioData {
		std::string group;
		bool checked = false;
	};

	/// @brief Visual style for the Knob widget.
	enum class KnobShape : Uint8 {
		Arc,          ///< Circular body with arc track + filled arc indicator (default).
		Potentiometer ///< Solid dial with a single line/pointer indicator.
	};

	struct KnobData {
		float min = 0.f, max = 1.f, step = 0.f, val = 0.f;
		float dragStartY = 0.f, dragStartVal = 0.f;
		float dragStartAngle = 0.f; ///< Angle at drag start (degrees) for arc interaction.
		bool  drag = false;
		KnobShape shape = KnobShape::Arc;
	};

	struct ImageData {
		std::string key;
		ImageFit fit = ImageFit::Contain;
	};

	/// @brief Icon properties for a Button (or any widget rendered by _DrawButton).
	/// When a text label is also present the icon is drawn to its left;
	/// when no text is set the icon is centred inside the widget.
	struct IconData {
		std::string key;               ///< Resource-pool key of the texture (empty = no icon).
		float       pad = 4.f;         ///< Inset from widget edges (px).

		float opacityNormal   = 1.f;   ///< Icon alpha when idle.
		float opacityHovered  = 1.f;   ///< Icon alpha when hovered.
		float opacityPressed  = 0.85f; ///< Icon alpha when pressed.
		float opacityDisabled = 0.35f; ///< Icon alpha when disabled.

		SDL::Color tintNormalColor   = {255, 255, 255, 255}; ///< Color-mod when idle.
		SDL::Color tintHoveredColor  = {255, 255, 255, 255}; ///< Color-mod when hovered.
		SDL::Color tintPressedColor  = {220, 220, 220, 255}; ///< Color-mod when pressed.
		SDL::Color tintDisabledColor = {180, 180, 180, 255}; ///< Color-mod when disabled.
	};

	struct CanvasData {
		std::function<void(SDL::Event&)> eventCb; ///< Event
		std::function<void(float)> updateCb; ///< delta time
		std::function<void(RendererRef, FRect)> renderCb;  ///< Renderer and clip rect
	};

	struct TextCache {
		SDL::Text text;
	};

	// ==================================================================================
	// ListBoxData — ECS component for the ListBox widget
	// ==================================================================================

	/// @brief Scrollable list of selectable text items.
	struct ListBoxData {
		std::vector<std::string> items;      ///< All items in the list.
		int   selectedIndex = -1;            ///< Currently selected item (-1 = none).
		float itemHeight    = 22.f;          ///< Pixel height of each row.
		bool  reorderable   = false;         ///< Enable drag-to-reorder items.
		// Drag-to-reorder state (managed by System, do not set manually).
		bool  dragActive  = false;
		int   dragSrcIdx  = -1;
		int   dragDstIdx  = -1;
		float dragY       = 0.f;
		float dragStartY  = 0.f;             ///< Mouse Y at drag-begin (for threshold).
		bool  dragMoved   = false;           ///< True once mouse exceeded drag threshold.
		std::function<void(int, int)> onReorder; ///< Called with (fromIdx, toIdx) after reorder.
		// Scroll position is stored in LayoutProps::scrollY (shared with container
		// drag infrastructure). ContainerScrollState::thumbY is updated each frame
		// by _DrawListBox so that thumb drag works out of the box.
	};

	// ==================================================================================
	// TooltipData — ECS component attachable to any widget
	// ==================================================================================

	/// @brief Tooltip info-bulle affichée après un survol prolongé.
	struct TooltipData {
		std::string text;         ///< Texte affiché dans la bulle.
		float       delay = 1.f;  ///< Durée de survol (secondes) avant affichage.
	};

	// ==================================================================================
	// GraphData — ECS component for the Graph widget (Excel-style graduated plot)
	// ==================================================================================

	/// @brief Data and visual config for a graduated graph widget.
	struct GraphData {
		std::vector<float> data;             ///< Y values to plot (one per X sample).
		float minVal     = 0.f;              ///< Y axis minimum.
		float maxVal     = 1.f;              ///< Y axis maximum.
		float xMin       = 0.f;              ///< X axis start value (used for tick labels).
		float xMax       = 1.f;              ///< X axis end value  (used for tick labels).
		int   xDivisions = 8;                ///< Number of vertical grid lines.
		int   yDivisions = 5;                ///< Number of horizontal grid lines.
		SDL::Color lineColor = {70, 130, 210, 255};   ///< Line / bar-top color.
		SDL::Color fillColor = {70, 130, 210,  55};   ///< Fill-under-line color.
		SDL::Color gridColor = {55,  60,  88, 200};   ///< Grid line color.
		SDL::Color axisColor = {175, 180, 200, 220};  ///< Tick-label color.
		std::string xLabel;                  ///< Horizontal axis label (drawn below).
		std::string yLabel;                  ///< Vertical axis label (drawn left, rotated).
		std::string title;                   ///< Graph title (drawn above plot area).
		bool  showFill = true;               ///< Fill the area under the curve.
		bool  barMode  = false;              ///< Draw as vertical bars instead of a line.
		bool  logFreq  = false;              ///< Logarithmic X axis (frequency spectrum).
	};

	// ==================================================================================
	// TextSpanStyle — per-run style override for TextArea rich text
	// ==================================================================================

	/**
	 * @brief Style modifier applied to a byte-range of text in a TextArea.
	 *
	 * Spans are sorted and non-overlapping. At any position, only the innermost
	 * (last-pushed) span is applied. A zero-alpha color falls back to the widget's
	 * default `textColor`.
	 */
	struct TextSpanStyle {
		bool       bold          = false;      ///< Bold weight (requires TTF bold font variant).
		bool       italic        = false;      ///< Italic slant (requires TTF italic font variant).
		bool       underline     = false;      ///< Draw a line below the text run.
		bool       strikethrough = false;      ///< Draw a line through the middle of the text run.
		bool       highlight     = false;      ///< Fill background behind the text run with highlightColor.
		SDL::Color color         = {0,0,0,0}; ///< Text color; {0,0,0,0} = use widget default.
		SDL::Color highlightColor = {255,255,100,80}; ///< Background fill color when highlight==true.
	};

	// ==================================================================================
	// TextAreaData — ECS component for the TextArea widget
	// ==================================================================================

	/**
	 * @brief Document model and edit state for the TextArea widget.
	 *
	 * The document is stored as a flat UTF-8 string with LF (`\n`) line endings.
	 * Cursor and selection use byte offsets. Rich text is expressed via sorted,
	 * non-overlapping Span records layered over the flat text.
	 *
	 * Tab stops are visual: a tab advances to the next multiple of `tabSize`
	 * character-widths (monospace grid, or approximate for proportional fonts).
	 *
	 * Drag-and-drop:
	 *  - SDL `EVENT_DROP_TEXT` can be forwarded to `TextAreaData::Insert()`.
	 *  - The Canvas event callback in the example demonstrates this pattern.
	 */
	struct TextAreaData {
		std::string text;           ///< Document content (LF line endings, UTF-8).

		// ── Cursor ─────────────────────────────────────────────────────────────
		int   cursorPos  = 0;       ///< Byte offset of the insertion caret in `text`.
		float blinkTimer = 0.f;     ///< Cursor blink phase [0..1); visible when < 0.5.

		// ── Selection ──────────────────────────────────────────────────────────
		int        selAnchor = -1;  ///< Anchor of selection (-1 = no selection).
		int        selFocus  = -1;  ///< Moving end of selection (-1 = no selection).
		SDL::Color highlightColor = {70, 130, 210, 90}; ///< Selection background.

		// ── Tab ────────────────────────────────────────────────────────────────
		int tabSize = 4;            ///< Visual character columns per tab stop.

		// ── Drag-selection state ───────────────────────────────────────────────
		bool selectDragging = false; ///< True while mouse-drag selection is in progress.

		// ── Read-only mode ────────────────────────────────────────────────────
		bool readOnly = false;      ///< When true, text cannot be edited but selection and copy still work.

		// ── Internal scroll ────────────────────────────────────────────────────
		float scrollY = 0.f;        ///< Vertical scroll offset (pixels from document top).

		// ── Rich text spans ────────────────────────────────────────────────────
		struct Span {
			int start = 0, end = 0; ///< [start, end) byte offsets into `text`.
			TextSpanStyle style;
		};
		std::vector<Span> spans;    ///< Sorted, non-overlapping styled ranges.

		// ── Helpers ────────────────────────────────────────────────────────────

		/** @brief Returns true when a non-empty selection exists. */
		[[nodiscard]] bool HasSelection() const noexcept {
			return selAnchor >= 0 && selFocus >= 0 && selAnchor != selFocus;
		}
		/** @brief Returns the byte offset of the selection start (min of anchor/focus). */
		[[nodiscard]] int SelMin() const noexcept {
			return (selAnchor < selFocus) ? selAnchor : selFocus;
		}
		/** @brief Returns the byte offset of the selection end (max of anchor/focus). */
		[[nodiscard]] int SelMax() const noexcept {
			return (selAnchor > selFocus) ? selAnchor : selFocus;
		}
		/** @brief Returns the currently selected substring, or empty if no selection. */
		[[nodiscard]] std::string GetSelectedText() const {
			if (!HasSelection()) return {};
			int a = std::clamp(SelMin(), 0, (int)text.size());
			int b = std::clamp(SelMax(), 0, (int)text.size());
			return (a < b) ? text.substr(a, b - a) : std::string{};
		}
		/** @brief Clears the selection (sets both anchor and focus to -1). */
		void ClearSelection() noexcept { selAnchor = selFocus = -1; }
		/**
		 * @brief Set the selection range, clamping to valid text positions.
		 * @param anchor  Fixed end of the selection.
		 * @param focus   Moving end of the selection (current cursor position).
		 */
		void SetSelection(int anchor, int focus) noexcept {
			int sz = (int)text.size();
			selAnchor = std::clamp(anchor, 0, sz);
			selFocus  = std::clamp(focus,  0, sz);
		}

		// ── Line / column navigation ───────────────────────────────────────────

		/** @brief Returns the total number of lines in the document (always ≥ 1). */
		[[nodiscard]] int LineCount() const noexcept {
			int n = 1;
			for (char c : text) if (c == '\n') ++n;
			return n;
		}

		/// Byte offset of the first character of `line` (0-based).
		[[nodiscard]] int LineStart(int line) const noexcept {
			int cur = 0;
			for (int i = 0; i < (int)text.size(); ++i) {
				if (cur == line) return i;
				if (text[i] == '\n') ++cur;
			}
			return (int)text.size();
		}

		/// Byte offset just past the last character of `line` (before '\n' or EOF).
		[[nodiscard]] int LineEnd(int line) const noexcept {
			int i = LineStart(line);
			while (i < (int)text.size() && text[i] != '\n') ++i;
			return i;
		}

		/// 0-based line number containing byte offset `pos`.
		[[nodiscard]] int LineOf(int pos) const noexcept {
			pos = std::clamp(pos, 0, (int)text.size());
			int line = 0;
			for (int i = 0; i < pos; ++i) if (text[i] == '\n') ++line;
			return line;
		}

		/// Byte distance from the start of the line containing `pos`.
		[[nodiscard]] int ColOf(int pos) const noexcept {
			return pos - LineStart(LineOf(pos));
		}

		// ── Edit operations ────────────────────────────────────────────────────

		/// Delete the selected text; cursor moves to selection start.
		void DeleteSelection() {
			if (!HasSelection()) return;
			int a = std::clamp(SelMin(), 0, (int)text.size());
			int b = std::clamp(SelMax(), 0, (int)text.size());
			if (a < b) { text.erase(a, b - a); _ShiftSpans(a, -(b - a)); }
			cursorPos = a;
			ClearSelection();
		}

		/// Insert `s` at the cursor (deletes any selection first).
		void Insert(std::string_view s) {
			if (HasSelection()) DeleteSelection();
			int cp = std::clamp(cursorPos, 0, (int)text.size());
			text.insert(cp, s);
			_ShiftSpans(cp, (int)s.size());
			cursorPos = cp + (int)s.size();
			ClearSelection();
		}

		// ── Rich text ─────────────────────────────────────────────────────────

		/// Add a styled span; spans are kept sorted.
		void AddSpan(int start, int end, TextSpanStyle style) {
			if (start >= end) return;
			spans.push_back({start, end, style});
			std::sort(spans.begin(), spans.end(),
					  [](const Span &a, const Span &b){ return a.start < b.start; });
		}
		/** @brief Removes all rich-text spans from the document. */
		void ClearSpans() noexcept { spans.clear(); }

		/// Returns the innermost span covering `pos`, or nullptr.
		[[nodiscard]] const TextSpanStyle *SpanStyleAt(int pos) const noexcept {
			const TextSpanStyle *found = nullptr;
			for (auto &sp : spans)
				if (pos >= sp.start && pos < sp.end) found = &sp.style;
			return found;
		}

		void _ShiftSpans(int at, int delta) {
			for (auto &sp : spans) {
				if (sp.start >= at) sp.start = std::max(at, sp.start + delta);
				if (sp.end   >  at) sp.end   = std::max(at, sp.end   + delta);
			}
			std::erase_if(spans, [](const Span &s){ return s.start >= s.end; });
		}
	};

	// ==================================================================================
	// TilesetStyle — 9-slice tileset skin for widgets
	// ==================================================================================

	/**
	 * @brief Tileset-based 9-slice skin for any widget.
	 *
	 * When a `TilesetStyle` component is attached to a widget entity (via
	 * `System::SetTilesetStyle` or the `.TilesetSkin()` builder), the default
	 * solid-colour rendering is replaced by a 9-slice draw using tiles from a
	 * tileset texture stored in the resource pool.
	 *
	 * Tile index layout (row-major, starting at `firstTileIdx`):
	 * ```
	 *  tl  tc  tr        0  1  2
	 *  ml  mc  mr   →    3  4  5
	 *  bl  bc  br        6  7  8
	 * ```
	 * Indices are relative to `firstTileIdx` and wrap across `tilesPerRow`.
	 *
	 * Usage:
	 * ```cpp
	 * SDL::UI::TilesetStyle skin;
	 * skin.textureKey  = "ui_tileset";
	 * skin.tileW = skin.tileH = 8;
	 * skin.tilesPerRow = 3;
	 * skin.firstTileIdx = 0;
	 * skin.borderW = skin.borderH = 8.f;   // use full tile as border
	 * ui.SetTilesetStyle(myButton, skin);
	 *
	 * // Or via the builder DSL:
	 * ui.Button("ok","OK").TilesetSkin(skin);
	 * ```
	 */
	struct TilesetStyle {
		std::string textureKey;   ///< Key in the resource pool (SDL::Texture).
		int  tileW        = 16;   ///< Width  of one tile in the tileset (px).
		int  tileH        = 16;   ///< Height of one tile in the tileset (px).
		int  tilesPerRow  = 3;    ///< Number of tiles per row in the tileset.
		int  firstTileIdx = 0;    ///< Absolute index of the top-left corner tile.

		/// Border thickness used when slicing. 0 → full tile size.
		float borderW = 0.f;
		float borderH = 0.f;

		float opacity = 1.f;      ///< Overall alpha multiplier [0..1].

		// ── Derived helpers ──────────────────────────────────────────────────

		/// Source rect for a tile at relative index `rel` (0..8).
		[[nodiscard]] FRect TileRect(int rel) const noexcept {
			int abs  = firstTileIdx + rel;
			int row  = abs / tilesPerRow;
			int col  = abs % tilesPerRow;
			return {static_cast<float>(col * tileW),
					static_cast<float>(row * tileH),
					static_cast<float>(tileW),
					static_cast<float>(tileH)};
		}

		/** @brief Returns the effective horizontal border size (borderW or full tileW if 0). */
		[[nodiscard]] float BorderW() const noexcept { return borderW > 0.f ? borderW : static_cast<float>(tileW); }
		/** @brief Returns the effective vertical border size (borderH or full tileH if 0). */
		[[nodiscard]] float BorderH() const noexcept { return borderH > 0.f ? borderH : static_cast<float>(tileH); }
	};


	// ==================================================================================
	// ComboBoxData
	// ==================================================================================

	/// @brief Dropdown list selector.
	struct ComboBoxData {
		std::vector<std::string> items;      ///< All selectable items.
		int    selectedIndex = -1;           ///< Currently selected item (-1 = none).
		bool   open          = false;        ///< Dropdown overlay is visible.
		int    hoverIndex    = -1;           ///< Item highlighted in the open dropdown.
		float  itemHeight    = 22.f;         ///< Height of each dropdown row (px).
		int    maxVisible    = 6;            ///< Maximum rows shown before scrolling.
		float  scrollOffset  = 0.f;         ///< Scroll position inside the dropdown.
		FRect  dropRect      = {};           ///< Screen-space dropdown rect (computed each frame).
		std::vector<float> tabWidths;        ///< (unused — for future use)
	};

	// ==================================================================================
	// InputData — numeric / typed-input mode for the Input widget
	// ==================================================================================

	/// @brief Per-Input numeric / filter state. Attached only when an Input
	///        widget is configured with a non-Text @ref InputType. Plain text
	///        Inputs do not carry this component.
	///
	/// When attached, the Input widget renders stacked ↑/↓ arrow buttons on
	/// its right edge (for numeric modes), supports vertical drag-to-adjust
	/// from those arrows, and filters typed characters via a regex selected
	/// by @ref type.
	struct InputData {
		/// @brief Whether @ref val should be treated as an integer or a float.
		enum class ValueKind : Uint8 { None, Int, Float };

		InputType type      = InputType::Text;       ///< Filter / value mode.
		ValueKind kind      = ValueKind::None;       ///< Numeric kind (or None for non-numeric).
		double    min       = 0.0;                   ///< Lower bound (numeric modes).
		double    max       = 100.0;                 ///< Upper bound (numeric modes).
		double    val       = 0.0;                   ///< Current numeric value.
		double    step      = 1.0;                   ///< Increment per arrow click / Up-Down key.
		int       decimals  = 2;                     ///< Decimals shown for FloatValue mode.

		// ── Internal interaction state (managed by System) ────────────────
		bool   drag         = false;
		float  dragStartY   = 0.f;
		double dragStartVal = 0.0;
		bool   pressUp      = false;
		bool   pressDown    = false;
	};

	// ==================================================================================
	// TabViewData
	// ==================================================================================

	/// @brief Tabbed container — shows one child per tab at a time.
	struct TabViewData {
		struct Tab {
			std::string label;                ///< Tab header text.
			bool        closable = false;     ///< Show a close (×) button on this tab.
		};
		std::vector<Tab>   tabs;            ///< Parallel with the widget's Children.ids.
		int   activeTab  = 0;              ///< Index of the currently visible child.
		float tabHeight  = 32.f;           ///< Height of the tab bar (px).
		bool  tabsBottom = false;          ///< Place the tab bar at the bottom.
		std::vector<float> tabWidths;      ///< Computed per-tab widths (set by _DrawTabView).
	};

	// ==================================================================================
	// ExpanderData
	// ==================================================================================

	/// @brief Collapsible section with a clickable header and hideable body.
	struct ExpanderData {
		std::string label;           ///< Header text.
		bool        expanded = true; ///< Current expansion state.
		float       animT    = 1.f;  ///< Animation progress [0=collapsed .. 1=expanded].
		float       headerH  = 28.f; ///< Header height in pixels.
	};

	// ==================================================================================
	// SplitterData
	// ==================================================================================

	/// @brief Resizable split panes — two children separated by a draggable handle.
	struct SplitterData {
		Orientation orientation = Orientation::Horizontal; ///< Handle moves along this axis.
		float ratio      = 0.5f;  ///< Split ratio in [minRatio, maxRatio].
		float minRatio   = 0.05f; ///< Minimum ratio of the first pane.
		float maxRatio   = 0.95f; ///< Maximum ratio of the first pane.
		float handleSize = 6.f;   ///< Draggable divider thickness (px).
		// Drag state
		bool  dragging  = false;
		float dragStart = 0.f;
		float dragRatio = 0.f;
	};

	// ==================================================================================
	// SpinnerData
	// ==================================================================================

	/// @brief Animated rotating-arc loading indicator.
	struct SpinnerData {
		float angle     = 0.f;   ///< Current arc start angle (radians).
		float speed     = 6.28f; ///< Angular velocity (rad/s ≈ 1 full rotation/s).
		float arcSpan   = 0.65f; ///< Fraction of the full circle covered [0..1].
		float thickness = 3.f;   ///< Arc stroke thickness (px).
	};

	// ==================================================================================
	// BadgeData
	// ==================================================================================

	/// @brief Small notification pill displaying a text count.
	struct BadgeData {
		std::string text;                              ///< Badge text (e.g. "42" or "new").
		SDL::Color  bgColor   = {220,  50,  40, 255}; ///< Background of the badge pill.
		SDL::Color  textColor = {255, 255, 255, 255}; ///< Text color.
	};

	// ==================================================================================
	// ColorPickerData — ECS component for the ColorPicker widget
	// ==================================================================================

	/// @brief Palette mode for the ColorPicker widget.
	enum class ColorPickerPalette : Uint8 {
		Grayscale,   ///< 1-D horizontal bar: black → white.
		RGB8,        ///< 2-D HSV square + hue bar (8-bit integer output).
		RGBFloat,    ///< 2-D HSV square + hue bar with configurable precision step.
		GradientAB,  ///< 1-D horizontal bar: colorA → colorB.
	};

	/// @brief State and configuration for the ColorPicker widget.
	struct ColorPickerData {
		ColorPickerPalette palette = ColorPickerPalette::RGB8;
		SDL::Color currentColor    = {255, 100,  50, 255}; ///< Currently selected color.
		SDL::Color colorA          = {  0,   0,   0, 255}; ///< Start color (GradientAB).
		SDL::Color colorB          = {255, 255, 255, 255}; ///< End color (GradientAB).
		float      precisionStep   = 1.f / 255.f;          ///< Quantization step (RGBFloat).
		bool       showAlpha       = false;                 ///< Show an alpha channel bar.
		// Internal HSV state (used for RGB8/RGBFloat)
		float hue    = 0.f;  ///< [0..1] hue.
		float sat    = 1.f;  ///< [0..1] saturation.
		float val    = 1.f;  ///< [0..1] value/brightness.
		float gradT  = 0.f;  ///< [0..1] normalized pick position (Grayscale/GradientAB).
		bool dragging    = false;
		bool draggingHue = false;
	};

	// ==================================================================================
	// PopupData — ECS component for the Popup widget
	// ==================================================================================

	/// @brief State and configuration for the Popup (floating window) widget.
	///
	/// Position and size are set via the builder: `.Fixed(x,y).W(w).H(h)`.
	/// Children are placed below the header automatically (like an Expander).
	struct PopupData {
		std::string title;            ///< Title bar text.
		bool  closable   = true;      ///< Show a close (×) button on the right of the header.
		bool  draggable  = true;      ///< Clicking and dragging the title bar moves the window.
		bool  resizable  = false;     ///< Bottom-right resize grip.
		float headerH    = 28.f;      ///< Title bar height (px).
		bool  open       = true;      ///< Window is visible. Set false to close (widget hidden).
		// Drag state
		bool        dragging   = false;
		SDL::FPoint dragOffset = {};  ///< Mouse offset from top-left at drag start.
		SDL::FPoint pressPos   = {};  ///< Mouse position at press time (for drag-vs-click detection).
		// Resize state
		bool        resizing        = false;
		SDL::FPoint resizeStart     = {};
		SDL::FPoint resizeStartSize = {};
		// Custom header buttons (drawn right of the title, left of the close button)
		struct HeaderBtn {
			std::string iconKey;           ///< Resource-pool icon texture key.
			std::function<void()> onClick; ///< Callback when clicked.
		};
		std::vector<HeaderBtn> headerButtons;
	};

	// ==================================================================================
	// TreeNodeData / TreeData — ECS components for the Tree widget
	// ==================================================================================

	/// @brief A single node in the Tree widget.
	struct TreeNodeData {
		std::string label;
		std::string iconKey;       ///< Optional icon resource key.
		int  level       = 0;      ///< Indent level (0 = root).
		bool hasChildren = false;  ///< Whether this node can be expanded.
		bool expanded    = false;  ///< Current expansion state (relevant when hasChildren).
	};

	/// @brief State and configuration for the Tree widget.
	struct TreeData {
		std::vector<TreeNodeData> nodes;      ///< All nodes in display order.
		int   selectedIndex = -1;            ///< Currently selected node (-1 = none).
		float itemHeight    = 22.f;          ///< Row height (px).
		float indentSize    = 16.f;          ///< Horizontal indent per level (px).
		float iconSize      = 14.f;          ///< Icon width/height (px).
		float scrollY       = 0.f;           ///< Internal vertical scroll offset (px).
	};

	// ==================================================================================
	// MenuBarData — ECS component for the MenuBar widget
	// ==================================================================================

	struct MenuBarItem {
		std::string label;          ///< Display text.
		std::string shortcutText;   ///< Keyboard shortcut label shown on the right (e.g. "Ctrl+Z").
		std::string iconKey;        ///< Resource-pool key for an icon texture (empty = none).
		std::function<void()> action; ///< Callback fired when the item is activated.
		std::vector<MenuBarItem> sub; ///< Child items for a sub-menu (empty = leaf item).
		bool separator = false;     ///< When true renders a horizontal rule; other fields ignored.
		bool enabled   = true;      ///< Non-interactive and greyed-out when false.
		bool checkable = false;     ///< Item displays a checkmark glyph when checked.
		bool checked   = false;     ///< Current checked state (meaningful only when checkable).
		static MenuBarItem Sep() { MenuBarItem i; i.separator = true; return i; }
	};
	struct MenuBarMenu {
		std::string label;
		std::vector<MenuBarItem> items;
		bool enabled = true;
	};
	struct MenuBarData {
		std::vector<MenuBarMenu> menus;
		int openMenu = -1; ///< Index of the currently-open top-level menu; -1 = none.
		int hovMenu  = -1; ///< Hovered top-level button; -1 = none.
		int hovItem  = -1; ///< Hovered item inside the open dropdown; -1 = none.
		// Computed every render frame — used for hit-testing.
		struct MenuBtnRect { float x = 0.f, w = 0.f; };
		std::vector<MenuBtnRect> menuBtnRects;
		FRect dropRect = {};
		std::vector<FRect> itemRects;
	};

	struct WidgetState {
		bool hovered = false, pressed = false, focused = false, wasHovered = false;
	};

	struct Callbacks {
		std::function<void()>      onClick;
		std::function<void()>      onDoubleClick;
		std::function<void(float)> onChange;
		std::function<void(const std::string &)> onTextChange;
		std::function<void(bool)>  onToggle;
		std::function<void(float)> onScroll;
		std::function<void()> onHoverEnter, onHoverLeave, onFocusGain, onFocusLose;
		/// Tree-specific: called when a node is selected. Parameters: node index, hasChildren.
		std::function<void(int, bool)> onTreeSelect;
	};

} // namespace UI
} // namespace SDL
