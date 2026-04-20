#ifndef SDL3PP_UI_H_
#define SDL3PP_UI_H_

/**
 * @file SDL3pp_ui.h
 * @brief Retained-mode UI system for SDL3pp — ECS-backed with full builder DSL.
 *
 * ## Architecture
 *
 * Every widget is an ECS entity (`SDL::ECS::EntityId`) driven through the pipeline:
 * Measure → Place → Clip → Input → Render → Animate.
 * 
 * | Component        | Purpose                                                  |
 * |------------------|----------------------------------------------------------|
 * | `Widget`         | WidgetType, name, enabled/visible, dirty flags           |
 * | `Style`          | Colors, bdColor, radius, font path, opacity, audio cues  |
 * | `LayoutProps`    | Value w/h/x/y, margins, padding, layout mode, grow, gap  |
 * | `LayoutGridProps`| columns, rows, GridSizing, GridLines, line color/thickness |
 * | `GridCell`       | col, row, colSpan, rowSpan (child position in InGrid)     |
 * | `Content`        | Text, placeholder, cursor (Input)                        |
 * | `SliderData`     | min/max/value + drag (Slider, Progress, horizontal SB)   |
 * | `ScrollBarData`  | contentSize, viewSize, offset + drag                     |
 * | `ToggleData`     | checked + animT (Toggle)                                 |
 * | `RadioData`      | group name + checked (RadioButton)                       |
 * | `KnobData`       | normalised val [0,1] + drag                              |
 * | `ImageData`      | texture path + ImageFit                                  |
 * | `IconData`       | icon key, padding, per-state opacity + tint (Button)     |
 * | `CanvasData`     | custom render callback                                   |
 * | `WidgetState`    | hover/press/focus                                        |
 * | `Callbacks`      | onClick, onChange, onScroll, onToggle, onTextChange, …   |
 * | `ComputedRect`   | screen rect, clip rect, measured size                    |
 * | `Children`       | ordered child entity IDs                                 |
 * | `Parent`         | parent entity ID                                         |
 *
 * ## Value — resolution-independent dimensions
 *
 *   Value::Px(v)   absolute pixels
 *   Value::Pw(v)   % parent resolved width
 *   Value::Ph(v)   % parent resolved height
 *   Value::Pcw(v)  % parent content width  (minus H padding)
 *   Value::Pch(v)  % parent content height (minus V padding)
 *   Value::Rw(v)   % root resolved width
 *   Value::Rh(v)   % root resolved height
 *   Value::Rcw(v)  % root content width  (minus H padding)
 *   Value::Rch(v)  % root content height (minus V padding)
 *   Value::Ww(v)   % window width (live)
 *   Value::Wh(v)   % window height (live)
 *   Value::Auto()  shrink-to-content + optional px offset
 *   Value::Grow(f)   grow % — take share of remaining space (.W() for InLine, .H() for InColumn)
 *
 * ## Layout modes
 *
 *   Layout::InColumn  — children stacked vertically (default)
 *   Layout::InLine    — children placed horizontally (no wrap)
 *   Layout::Stack     — horizontal with line wrap
 *   Layout::InGrid    — children placed on a configurable 2-D grid
 *
 * ## InGrid layout
 *
 * A Container with `Layout::InGrid` distributes its children across a grid of
 * columns and rows.  Each child can carry a `GridCell` component (set via
 * `.Cell(col, row)`) that pins it to a specific cell and optionally spans
 * multiple columns/rows.  Children without a `GridCell` are auto-placed
 * sequentially (left-to-right, top-to-bottom).
 *
 * | Component / setter         | Effect                                           |
 * |----------------------------|--------------------------------------------------|
 * | `LayoutGridProps::columns` | Number of columns (default 2).                   |
 * | `LayoutGridProps::rows`    | Fixed row count; 0 = auto from children.         |
 * | `GridSizing::Fixed`        | All tracks equal: container ÷ count.            |
 * | `GridSizing::Content`      | Each track adapts to its widest/tallest child.   |
 * | `GridLines::None/Rows/     | Separator lines between cells.                  |
 * |  Columns/Both`             |                                                  |
 *
 * ```cpp
 * // 3-column grid, content-sized rows, horizontal + vertical separators
 * ui.Grid("cards", 3, 8.f, 8.f)
 *   .W(Value::Pw(100))
 *   .GridColSizing(GridSizing::Fixed)
 *   .GridRowSizing(GridSizing::Content)
 *   .GridLineStyle(GridLines::Both)
 *   .GridLineColor({60, 65, 90, 200})
 *   .Children(
 *     // Auto-placed sequentially:
 *     ui.Label("a", "Cell A"),
 *     ui.Label("b", "Cell B"),
 *     // Explicit placement with col-span:
 *     ui.Button("wide", "Wide button").Cell(0, 1, 3, 1),
 *   )
 *   .AsRoot();
 * ```
 *
 * ## Widgets available
 *
 *   Container, Label, Button, Toggle, RadioButton, Slider (H/V),
 *   ScrollBar (H/V), Progress, Separator, Input, Knob, Image, Canvas
 *
 * ## Container — scrollbars automatiques
 *
 * Un `Container` peut afficher des scrollbars inline (superposées sur le bord
 * intérieur du container) en combinant les `BehaviorFlag` suivants :
 *
 * | Flag                  | Comportement                                              |
 * |-----------------------|-----------------------------------------------------------|
 * | `ScrollableX`         | Barre horizontale **toujours** visible                    |
 * | `ScrollableY`         | Barre verticale **toujours** visible                      |
 * | `AutoScrollableX`     | Barre horizontale visible **seulement si débordement**    |
 * | `AutoScrollableY`     | Barre verticale visible **seulement si débordement**      |
 *
 * Les deux drapeaux peuvent coexister (ex. `ScrollableY | AutoScrollableX`).
 * L'espace réservé aux barres est soustrait automatiquement de l'espace contenu
 * disponible pour les enfants, de sorte que le layout reste cohérent.
 *
 * ```cpp
 * // ScrollView vertical automatique (raccourci factory)
 * ui.ScrollView("list")
 *   .H(Value::Grow(100.f))
 *   .Children(...)
 *   .AsRoot();
 *
 * // Container avec les deux axes automatiques + épaisseur personnalisée
 * ui.Column("grid")
 *   .AutoScrollable(true, true)   // X et Y automatiques
 *   .ScrollbarThickness(10.f)
 *   .H(Value::Ph(80))
 *   .Children(...);
 * ```
 *
 * ## Resource management
 *
 * `System` does **not** own any resources.  It receives a `SDL::ResourcePool&`
 * from the calling code (created via `SDL::ResourceManager`).  **All** runtime
 * assets — textures, fonts, and sounds — are stored in and retrieved from that
 * single pool.
 *
 * | C++ type                  | Pool key                        | Builder setter         |
 * |---------------------------|---------------------------------|------------------------|
 * | `SDL::Texture`            | string in `ImageData::key`      | `.Image("key")`        |
 * | `SDL::RendererTextEngine` |                                 |                        |
 * | `SDL::Font`               | `"font:<key>|<ptsize>"`         | `.Font("key", ptsize)` |
 * | `SDL::Audio`              | string in `Style::clickSound` … | `.ClickSound("key")`   |
 *
 * All three types are looked up at runtime via `pool.Get<T>(key)`.  If a key
 * is not found the operation is silently skipped.
 *
 * ## Usage
 *
 * ```cpp
 * // ── Application startup ────────────────────────────────────────────────────────────
 * SDL::ResourceManager rm;
 * SDL::ResourcePool& uiPool = *rm.CreatePool("ui");
 *
 * // Sync add of an already-constructed texture
 * uiPool.Add<SDL::Texture>("hero", SDL::LoadTexture(renderer, "hero.png"));
 * // Or async from file (poll uiPool.LoadingProgress() on the load screen)
 * uiPool.LoadAsync<SDL::Texture>("bgColor", "bgColor.jpg",
 *     [&](const char*, void* buf, size_t n){
 *         return SDL::CreateTextureFromSurface(renderer,
 *                    SDL::LoadSurface_IO(SDL::IOFromConstMem({buf,n})));
 *     });
 *
 * // Optional audio (requires SDL3PP_ENABLE_MIXER)
 * #if UI_HAS_MIXER
 *     MIX::Init();
 *     SDL::Mixer mixer = SDL::CreateMixerDevice(
 *         SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK, SDL::AudioSpec{});
 *     uiPool.Add<SDL::Audio>("click", SDL::Audio(mixer, "click.wav", false) });
 * #endif
 *
 * // Fonts are resolved automatically on first use from the pool. 
 * // Pre-loading is also possible (key scheme: "font:<path>|<ptsize>"):
 * // uiPool.Add<SDL::TTF::Font>("font:assets/Roboto.ttf|15.0",
 * //     SDL::TTF::Font{ SDL::OpenFont("assets/Roboto.ttf", 15.f) });
 *
 * SDL::ECS::Context ctx;
 * SDL::System ui(ctx, renderer, uiPool);  // ← single pool for all assets
 *
 * ui.Column("root").Pad(0).Gap(0)
 *   .Children(
 *     ui.Row("header").H(52).PadH(16).Gap(8)
 *       .Children(
 *         ui.Label("title","My App").TextColor({70,130,210,255}).W(Value::Grow(100.f)),
 *         ui.Button("ok","OK").W(100).H(36)
 *           .BgColor({70,130,210,255}).Radius(6)
 *           .ClickSound("assets/click.wav")
 *           .OnClick([]{ SDL::Log("clicked"); })
 *       ),
 *     ui.Slider("vol",0.f,1.f,0.8f).H(Value::Grow(100.f))
 *       .OnChange([](float v){ SDL::Log("vol %.2f",v); }),
 *     ui.Knob("k",0.f,1.f,0.5f).W(64).H(64),
 *     ui.ScrollBar("sb",300.f,100.f).OnScroll([](float v){}),
 *     ui.Radio("r1","grp","Option A"),
 *     ui.Radio("r2","grp","Option B"),
 *     ui.ImageWidget("bgColor","assets/bgColor.png",SDL::ImageFit::Cover).H(200),
 *     ui.CanvasWidget("game",[](SDL::RendererRef r, SDL::FRect rect){
 *         r.SetDrawColor({255,0,0,255});
 *         r.RenderFillRect(rect);
 *     })
 *   )
 *   .AsRoot();
 *
 * // Each frame:
 * ui.Iterate(dt);
 * ```
 */

// ── Dependencies ──────────────────────────────────────────────────────────────────────
#include "SDL3pp_ecs.h"
#include "SDL3pp_render.h"
#include "SDL3pp_events.h"
#include "SDL3pp_mouse.h"
#include "SDL3pp_keyboard.h"
#include "SDL3pp_rect.h"
#include "SDL3pp_stdinc.h"
#include "SDL3pp_resources.h"
#include "SDL3pp_clipboard.h"
#include "SDL3pp_log.h"
// Optional subsystems — each header gates itself with its own enable guard.
#include "SDL3pp_mixer.h" // SDL3_mixer 3.0  (SDL3PP_ENABLE_MIXER)
#include "SDL3pp_ttf.h"   // SDL3_ttf  3.0  (SDL3PP_ENABLE_TTF)

// Detect optional subsystems through SDL3pp's own enable guards.
#if defined(SDL3PP_ENABLE_TTF)
#define UI_HAS_TTF 1
#else
#define UI_HAS_TTF 0
#endif
#if defined(SDL3PP_ENABLE_MIXER)
#define UI_HAS_MIXER 1
#else
#define UI_HAS_MIXER 0
#endif

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <numbers>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

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
		All             = 0x1001
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

	// ==================================================================================
	// Value
	// ==================================================================================

	enum class Unit : Uint8 {
		Px   = 0,  ///< Pixel
		Ww   = 1,  ///< Percentage of Window Width
		Wh   = 2,  ///< Percentage of Window Height
		Pw   = 3,  ///< Percentage of Parent Width
		Ph   = 4,  ///< Percentage of Parent Height
		Rw   = 5,  ///< Percentage of Root Width
		Rh   = 6,  ///< Percentage of Root Height
		Pcw  = 7,  ///< Percentage of Parent Content Width
		Pch  = 8,  ///< Percentage of Parent Content Height
		Rcw  = 9,  ///< Percentage of Root Content Width
		Rch  = 10, ///< Percentage of Root Content Height
		Pfs  = 11, ///< Percentage of Parent Font Size
		Rfs  = 12, ///< Percentage of Root Font Size
		Auto = 13, ///< Shrink-to-content + optional px offset
		Grow = 14  ///< Grow factor in percentage, take a share of remaining space in the layout direction
	};

	/**
	 * @brief Immutable context passed down the widget tree during the Measure pass.
	 *
	 * Carries the resolved sizes of the window, root widget, and immediate parent so
	 * that percentage-based `Value` units (Pw, Ph, Rw, Ww, …) can be resolved without
	 * up-tree traversal.
	 */
	struct LayoutContext {
		FPoint windowSize     = {0.f, 0.f};

		FPoint rootSize       = {0.f, 0.f};
		FBox   rootPadding    = {0.f, 0.f, 0.f, 0.f};
		float  rootFontSize   = SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;

		FPoint parentSize     = {0.f, 0.f};
		FBox   parentPadding  = {0.f, 0.f, 0.f, 0.f};
		float  parentFontSize = SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
	};

	struct Value {
		float val    = 0.f;
		Unit  unit   = Unit::Auto;
		float offset = 0.f;

		/** Pixel */
		static Value Px  (float v, float o = 0.f) { return {v, Unit::Px,   o}; }
		/** Percentage of Parent Width */
		static Value Pw  (float v, float o = 0.f) { return {v, Unit::Pw,   o}; }
		/** Percentage of Parent Height */
		static Value Ph  (float v, float o = 0.f) { return {v, Unit::Ph,   o}; }
		/** Percentage of Root Width */
		static Value Rw  (float v, float o = 0.f) { return {v, Unit::Rw,   o}; }
		/** Percentage of Root Height */
		static Value Rh  (float v, float o = 0.f) { return {v, Unit::Rh,   o}; }
		/** Percentage of Window Width */
		static Value Ww  (float v = 100.f, float o = 0.f) { return {v, Unit::Ww,   o}; }
		/** Percentage of Window Height */
		static Value Wh  (float v = 100.f, float o = 0.f) { return {v, Unit::Wh,   o}; }
		/** Percentage of Parent Content Width */
		static Value Pcw (float v, float o = 0.f) { return {v, Unit::Pcw,  o}; }
		/** Percentage of Parent Content Height */
		static Value Pch (float v, float o = 0.f) { return {v, Unit::Pch,  o}; }
		/** Percentage of Root Content Width */
		static Value Rcw (float v, float o = 0.f) { return {v, Unit::Rcw,  o}; }
		/** Percentage of Root Content Height */
		static Value Rch (float v, float o = 0.f) { return {v, Unit::Rch,  o}; }
		/** Percentage of Parent Font Size */
		static Value Pfs (float v, float o = 0.f) { return {v, Unit::Pfs,  o}; }
		/** Percentage of Root Font Size */
		static Value Rfs (float v, float o = 0.f) { return {v, Unit::Rfs,  o}; }
		/** Shrink-to-content */
        static Value Auto(float v = 0.f, float o = 0.f) { return {v, Unit::Auto, o}; }
		/** Grow factor: take a share of the remaining space in the layout direction.
		 *  .W(Value::Grow(g)) grows width in InLine parents; .H(Value::Grow(g)) grows height in InColumn parents. */
		static Value Grow(float g = 100.f, float o = 0.f) { return {g, Unit::Grow, o}; }

		/** @brief Returns true if this value is an absolute pixel quantity. */
		[[nodiscard]] bool IsPixel() const noexcept { return unit == Unit::Px; }
		/** @brief Returns true if this value uses shrink-to-content sizing. */
		[[nodiscard]] bool IsAuto() const noexcept { return unit == Unit::Auto; }
		/** @brief Returns true if this value grows to fill remaining space. */
		[[nodiscard]] bool IsGrow() const noexcept { return unit == Unit::Grow; }

		Value operator+(float o) const noexcept { return {val, unit, offset + o}; }
		Value operator-(float o) const noexcept { return {val, unit, offset - o}; }

		/**
		 * @brief Resolve this value to a pixel quantity using the given layout context.
		 * @param ctx  Layout context providing parent, root, and window sizes.
		 * @return     Resolved pixel value (plus any fixed offset).
		 */
		[[nodiscard]] float Resolve(const LayoutContext &ctx) const noexcept {
			float base = 0.f;
			switch (unit) {
				case Unit::Px:
				case Unit::Auto:
				case Unit::Grow:
					base = val;
					break;

				case Unit::Ww:
					base = (val / 100.f) * ctx.windowSize.x;
					break;
				case Unit::Wh:
					base = (val / 100.f) * ctx.windowSize.y;
					break;

				case Unit::Rw:
					base = (val / 100.f) * ctx.rootSize.x;
					break;
				case Unit::Rh:
					base = (val / 100.f) * ctx.rootSize.y;
					break;
				case Unit::Rcw:
					base = (val / 100.f) * (ctx.rootSize.x - ctx.rootPadding.GetH());
					break;
				case Unit::Rch:
					base = (val / 100.f) * (ctx.rootSize.y - ctx.rootPadding.GetV());
					break;
				case Unit::Rfs:
					base = (val / 100.0f) * ctx.rootFontSize;
					break;

				case Unit::Pw:
					base = (val / 100.f) * ctx.parentSize.x;
					break;
				case Unit::Ph:
					base = (val / 100.f) * ctx.parentSize.y;
					break;
				case Unit::Pcw:
					base = (val / 100.f) * (ctx.parentSize.x - ctx.parentPadding.GetH());
					break;
				case Unit::Pch:
					base = (val / 100.f) * (ctx.parentSize.y - ctx.parentPadding.GetV());
					break;
				case Unit::Pfs:
					base = (val / 100.f) * ctx.parentFontSize;
					break;
			}
			return base + offset;
		}
	};

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
		SDL::Color bgHovered        = {40, 42, 58, 255};
		SDL::Color bgPressed        = {14, 14, 20, 255};
		SDL::Color bgChecked        = {55, 115, 195, 255};
		SDL::Color bgFocused        = {14, 14, 20, 255};
		SDL::Color bgDisabled       = {22, 22, 28, 160};

		SDL::Color bdColor          = {55, 58, 78, 255};
		SDL::Color bdHovered        = {90, 95, 130, 255};
		SDL::Color bdPressed        = {90, 95, 130, 255};
		SDL::Color bdChecked        = {90, 95, 130, 255};
		SDL::Color bdFocused        = {70, 130, 210, 255};
		SDL::Color bdDisabled       = {90, 95, 130, 255};

		SDL::Color textColor        = {215, 215, 220, 255};
		SDL::Color textHovered      = {255, 255, 255, 255};
		SDL::Color textPressed      = {255, 255, 255, 255};
		SDL::Color textChecked      = {255, 255, 255, 255};
		SDL::Color textDisabled     = {110, 110, 120, 200};
		SDL::Color textPlaceholder  = {90, 92, 105, 200};

		SDL::Color track            = {42, 44, 58, 255};
		SDL::Color fill             = {70, 130, 210, 255};
		SDL::Color thumb            = {100, 160, 230, 255};
		SDL::Color separator        = {55, 58, 78, 255};

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

		SDL::Color tooltipBg    = { 30,  32,  44, 245};  ///< Fond de l'info-bulle.
		SDL::Color tooltipBd    = { 75,  80, 108, 255};  ///< Bordure de l'info-bulle.
		SDL::Color tooltipText  = {215, 218, 228, 255};  ///< Texte de l'info-bulle.
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

	struct KnobData {
		float min = 0.f, max = 1.f, step = 0.f, val = 0.f;
		float dragStartY = 0.f, dragStartVal = 0.f;
		bool drag = false;
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

		SDL::Color tintNormal   = {255, 255, 255, 255}; ///< Color-mod when idle.
		SDL::Color tintHovered  = {255, 255, 255, 255}; ///< Color-mod when hovered.
		SDL::Color tintPressed  = {220, 220, 220, 255}; ///< Color-mod when pressed.
		SDL::Color tintDisabled = {180, 180, 180, 255}; ///< Color-mod when disabled.
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
		bool       bold   = false;      ///< Bold weight (requires TTF bold font variant).
		bool       italic = false;      ///< Italic slant (requires TTF italic font variant).
		SDL::Color color  = {0,0,0,0}; ///< Text color; {0,0,0,0} = use widget default.
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
	};

	// ==================================================================================
	// Forward declarations
	// ==================================================================================
	class UIManager;
	class System;
	struct Builder;

	// ==================================================================================
	// Theme
	// ==================================================================================

	/// @brief Collection of pre-built `Style` factories and global theme controls.
	struct Theme {
		static inline SDL::Color accentColor = {70, 130, 210, 255};
		/** @brief Switch the global accent colour to a dark-theme preset. */
		static void ApplyDark(SDL::UI::System &);
		/** @brief Switch the global accent colour to a light-theme preset. */
		static void ApplyLight(SDL::UI::System &);

		/** @brief Returns a dark-card style (slight elevation, rounded corners). */
		static Style Card() {
			Style s;
			s.bgColor       = {26, 28, 40, 255};
			s.bdColor       = {50, 54, 78, 255};
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.borders       = SDL::FBox(1.f);
			s.radius        = SDL::FCorners(8.f);
			return s;
		}
		/**
		 * @brief Returns a filled accent-colored button style.
		 * @param a  Fill color (defaults to `accentColor`).
		 */
		static Style PrimaryButton(SDL::Color a = accentColor) {
			Style s;
			s.bgColor       = a;
			s.bgHovered     = a.Brighten(25);
			s.bgPressed     = a.Darken(35);
			s.bgDisabled    = {40, 42, 55, 160};
			s.bdColor       = a.Brighten(40);
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.borders       = SDL::FBox(1.f);
			s.radius        = SDL::FCorners(6.f);
			s.textColor     = s.textHovered = {255, 255, 255, 255};
			return s;
		}
		/** @brief Returns a ghost (outline-only, transparent background) button style. */
		static Style GhostButton() {
			Style s;
			s.bgColor       = {0, 0, 0, 0};
			s.bgHovered     = {50, 55, 80, 80};
			s.bgPressed     = {20, 22, 36, 100};
			s.bdColor       = {70, 75, 100, 200};
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.bdHovered     = {110, 120, 170, 255};
			s.borders       = SDL::FBox(1.f);
			s.radius        = SDL::FCorners(6.f);
			return s;
		}
		/** @brief Returns a fully transparent, border-less style (useful for invisible containers). */
		static Style Transparent() {
			Style s;
			s.bgColor       = {0, 0, 0, 0};
			s.borders       = SDL::FBox(0.f);
			s.radius        = SDL::FCorners(0.f);
			return s;
		}
		/** @brief Returns a transparent style with the accent color applied to the text (for section headings). */
		static Style SectionTitle() {
			Style s         = Transparent();
			s.textColor     = accentColor;
			return s;
		}
		/** @brief Returns a red-tinted primary button style for destructive actions. */
		static Style DangerButton() { return PrimaryButton({200, 55, 45, 255}); }
		/** @brief Returns a green-tinted primary button style for confirmatory actions. */
		static Style SuccessButton() { return PrimaryButton({50, 180, 90, 255}); }
		/** @brief Returns a light-theme card style (white/light-grey surface, suitable for light UIs). */
		static Style CardLight() {
			Style s;
			s.bgColor           = {248, 249, 252, 255};
			s.bdColor           = {210, 213, 225, 255};
			s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
			s.borders           = SDL::FBox(1.f);
			s.radius            = SDL::FCorners(8.f);
			s.textColor         = {30, 32, 42, 255};
			s.textHovered       = {10, 12, 22, 255};
			s.textDisabled      = {160, 163, 175, 200};
			s.textPlaceholder   = {160, 162, 175, 180};
			s.bgHovered         = {235, 238, 248, 255};
			s.bgPressed         = {215, 220, 238, 255};
			s.separator         = {210, 213, 225, 255};
			s.track             = {215, 218, 230, 255};
			s.fill              = {70, 130, 210, 255};
			s.thumb             = {100, 160, 230, 255};
			return s;
		}
	};

	// ==================================================================================
	// System
	// ==================================================================================

	class System {
	public: // TODO: Make this private, UIManager is the new public API
		/**
		 * Construct the UI system.
		 *
		 * @param w    ECS Context that owns all widget entities.
		 * @param r    Renderer used for all drawing.
		 * @param m    Mixer used for audio.
		 * @param pool ResourcePool that holds **all** UI assets — textures 
		 *             (`SDL::Texture`), fonts (`SDL::TTF::Font`), and sounds
		 *             (`SDL::Audio`).  The pool must outlive the System.
		 *
		 * ```cpp
		 * // In main: 
		 * SDL::ResourceManager rm;
		 * SDL::ResourcePool& uiPool = *rm.CreatePool("ui");
		 * uiPool.Add<SDL::Texture>("hero", SDL::LoadTexture(renderer, "hero.png"));
		 * SDL::UI::System ui(rm, renderer, uiPool);
		 * ```
		 */
		System(ECS::Context &ctx, RendererRef r, MixerRef m, ResourcePool &pool)
			: m_ctx(ctx), m_renderer(r), m_mixer(m), m_pool(pool)
		{
		}

		/**
		 * Destructor — must destroy all SDL::Text objects (TextCache ECS
		 * components) *before* the RendererTextEngine is destroyed.
		 *
		 * SDL_ttf requires every TTF_Text that was created with an engine to be
		 * destroyed before that engine is freed.  Because the engine is now owned
		 * by System (not the pool), we can guarantee the correct order here:
		 *   1. Iterate all TextCache ECS components and reset their SDL::Text.
		 *   2. m_engine optional<> is then destroyed at end of destructor.
		 */
		~System() {
#if UI_HAS_TTF
			// Step 1 — release all SDL::Text objects while the engine is still live.
			m_ctx.Each<TextCache>([](ECS::EntityId, TextCache &tc) {
				tc.text = SDL::Text{};  // calls TTF_DestroyText safely
			});
			// Step 2 — engine is destroyed when m_engine optional goes out of scope.
			m_engine.reset();
#endif
		}

		System(const System &) = delete;
		System &operator=(const System &) = delete;

		/// Direct access to the resource pool for runtime additions.
		[[nodiscard]] ResourcePool &GetPool() noexcept { return m_pool; }

		/**
		 * Set a font applied to every widget.
		 *
		 * Widgets that already set their own `fontKey`/`fontSize` keep them.
		 * Pass an empty path to clear the default.
		 *
		 * ```cpp 
		 * #if _HAS_TTF
		 *     ui.SetDefaultFont("assets/Roboto-Regular.ttf", 15.f);
		 * #endif
		 * ```
		 */
		void SetDefaultFont(const std::string &path, float ptsize) {
			m_usedDebugFontPerDefault = false;
			m_defaultFontPath = path;
			m_defaultFontSize = ptsize;
		}

		/** @brief Use default font applied to every widget created */
		void UseDebugFont(float ptsize) {
			m_usedDebugFontPerDefault = true;
			m_defaultFontSize = ptsize;
		}

		/** @brief Returns the path of the current default font (empty if none). */
		[[nodiscard]] const std::string &GetDefaultFontPath() const { return m_defaultFontPath; }
		
		/** @brief Returns the point size of the current default font (0 if unset). */
		[[nodiscard]] float GetDefaultFontSize() const { return m_defaultFontSize; }

		// ── Entity factories ──────────────────────────────────────────────────────────

		/** @brief Create a Container entity and return its ID. */
		ECS::EntityId MakeContainer(const std::string &n = "Container") { return _Make(n, WidgetType::Container); }

		/**
		 * @brief Create a Label entity with the given display text.
		 * @param n  Widget name (for debugging).
		 * @param t  Initial text content.
		 */
		ECS::EntityId MakeLabel(const std::string &n, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::Label);
			m_ctx.Get<Content>(e)->text = t;
			auto &l = *m_ctx.Get<LayoutProps>(e);
			l.padding.top = l.padding.bottom = 2.f;
			return e;
		}
		/**
		 * @brief Create a Button entity with the given label text.
		 * @param n  Widget name.
		 * @param t  Button label text.
		 */
		ECS::EntityId MakeButton(const std::string &n, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::Button);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<Content>(e)->text = t;
			return e;
		}
		/**
		 * @brief Create a Toggle (on/off switch) entity.
		 * @param n  Widget name.
		 * @param t  Label text displayed beside the switch.
		 */
		ECS::EntityId MakeToggle(const std::string &n, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::Toggle);
			m_ctx.Get<Content>(e)->text = t;
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(28.f);
			m_ctx.Add<ToggleData>(e);
			return e;
		}
		/**
		 * @brief Create a RadioButton entity belonging to the given group.
		 * @param n      Widget name.
		 * @param group  Named group; only one radio button per group can be checked.
		 * @param t      Label text displayed beside the button.
		 */
		ECS::EntityId MakeRadioButton(const std::string &n, const std::string &group, const std::string &t = "") {
			ECS::EntityId e = _Make(n, WidgetType::RadioButton);
			m_ctx.Get<Content>(e)->text = t;
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(24.f);
			m_ctx.Add<RadioData>(e, {group, false});
			return e;
		}
		/**
		 * @brief Create a Slider entity.
		 * @param n   Widget name.
		 * @param mn  Minimum value.
		 * @param mx  Maximum value.
		 * @param v   Initial value (clamped to [mn, mx]).
		 * @param o   Orientation (Horizontal or Vertical).
		 */
		ECS::EntityId MakeSlider(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.f,
							Orientation o = Orientation::Horizontal) {
			ECS::EntityId e = _Make(n, WidgetType::Slider);
			SliderData sd;
			sd.min = mn;
			sd.max = mx;
			sd.val = SDL::Clamp(v, mn, mx);
			sd.orientation = o;
			m_ctx.Add<SliderData>(e, sd);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Horizontal)
				lp.height = Value::Px(24.f);
			else
				lp.width = Value::Px(24.f);
			return e;
		}
		/**
		 * @brief Create a ScrollBar entity.
		 * @param n   Widget name.
		 * @param cs  Total content size.
		 * @param vs  Visible viewport size.
		 * @param o   Orientation (Vertical by default).
		 */
		ECS::EntityId MakeScrollBar(const std::string &n, float cs = 0.f, float vs = 0.f,
							   Orientation o = Orientation::Vertical) {
			ECS::EntityId e = _Make(n, WidgetType::ScrollBar);
			ScrollBarData sd;
			sd.contentSize = cs;
			sd.viewSize = vs;
			sd.orientation = o;
			m_ctx.Add<ScrollBarData>(e, sd);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			if (o == Orientation::Vertical)
				lp.width = Value::Px(10.f);
			else
				lp.height = Value::Px(10.f);
			return e;
		}
		/**
		 * @brief Create a Progress bar entity.
		 * @param n   Widget name.
		 * @param v   Initial value (clamped to [0, mx]).
		 * @param mx  Maximum value.
		 */
		ECS::EntityId MakeProgress(const std::string &n, float v = 0.f, float mx = 1.f) {
			ECS::EntityId e = _Make(n, WidgetType::Progress);
			m_ctx.Add<SliderData>(e, {0.f, mx, SDL::Clamp(v, 0.f, mx)});
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(18.f);
			return e;
		}
		/** @brief Create a 1-pixel horizontal separator entity. */
		ECS::EntityId MakeSeparator(const std::string &n = "sep") {
			ECS::EntityId e = _Make(n, WidgetType::Separator);
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.height = Value::Px(1.f);
			lp.margin.top = lp.margin.bottom = 6.f;
			return e;
		}
		/**
		 * @brief Create a single-line text Input entity.
		 * @param n   Widget name.
		 * @param ph  Placeholder text shown when empty and unfocused.
		 */
		ECS::EntityId MakeInput(const std::string &n, const std::string &ph = "") {
			ECS::EntityId e = _Make(n, WidgetType::Input);
			m_ctx.Get<Content>(e)->placeholder = ph;
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Get<LayoutProps>(e)->height = Value::Px(30.f);
			return e;
		}
		/**
		 * @brief Create a circular Knob entity.
		 * @param n   Widget name.
		 * @param mn  Minimum value.
		 * @param mx  Maximum value.
		 * @param v   Initial value (clamped to [mn, mx]).
		 */
		ECS::EntityId MakeKnob(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.5f) {
			ECS::EntityId e = _Make(n, WidgetType::Knob);
			
			// Initialisation explicite de tous les champs pour éviter le garbage memory
			KnobData kd;
			kd.min = mn;
			kd.max = mx;
			kd.val = SDL::Clamp(v, mn, mx);
			kd.drag = false;
			kd.dragStartY = 0.f;
			kd.dragStartVal = 0.f;
			m_ctx.Add<KnobData>(e, kd);
			
			auto *w = m_ctx.Get<Widget>(e);
			if (w) w->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;

			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.width = lp.height = Value::Px(56.f); // Taille par défaut sécurisée
			return e;
		}
		/**
		 * @brief Create an Image entity.
		 * @param n    Widget name.
		 * @param key  Resource-pool key of the texture to display.
		 * @param fit  How the image is scaled to fit the widget rect.
		 */
		ECS::EntityId MakeImage(const std::string &n, const std::string &key = "", ImageFit fit = ImageFit::Contain) {
			ECS::EntityId e = _Make(n, WidgetType::Image);
			m_ctx.Add<ImageData>(e, {key, fit});
			return e;
		}
		/**
		 * @brief Create a Canvas entity with custom callbacks.
		 * @param n         Widget name.
		 * @param cb_event  Called by `ProcessEvent` when the canvas is hit or focused.
		 * @param cb_update Called every frame before layout with the frame delta time.
		 * @param cb_render Called during the render pass with the renderer and screen rect.
		 */
		ECS::EntityId MakeCanvas(const std::string &n,
			std::function<void(SDL::Event&)> cb_event = nullptr,
			std::function<void(float)> cb_update = nullptr,
			std::function<void(RendererRef, FRect)> cb_render = nullptr) {
			ECS::EntityId e = _Make(n, WidgetType::Canvas);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			m_ctx.Add<CanvasData>(e, {std::move(cb_event), std::move(cb_update), std::move(cb_render)});
			return e; 
		}
		/**
		 * @brief Create a ListBox entity pre-populated with items.
		 * @param n      Widget name.
		 * @param items  Initial list of text items.
		 */
		ECS::EntityId MakeListBox(const std::string &n,
								  const std::vector<std::string>& items = {}) {
			ECS::EntityId e = _Make(n, WidgetType::ListBox);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable
											  | BehaviorFlag::Selectable
											  | BehaviorFlag::Focusable
											  | BehaviorFlag::AutoScrollableY
											  | BehaviorFlag::AutoScrollableX;
			auto &lb = m_ctx.Add<ListBoxData>(e);
			lb.items = items;
			m_ctx.Get<LayoutProps>(e)->padding = {2.f, 2.f, 2.f, 2.f};
			return e;
		}
		/** @brief Create an empty Graph (graduated data plot) entity. */
		ECS::EntityId MakeGraph(const std::string &n) {
			ECS::EntityId e = _Make(n, WidgetType::Graph);
			m_ctx.Add<GraphData>(e);
			m_ctx.Get<LayoutProps>(e)->padding = {0.f, 0.f, 0.f, 0.f};
			return e;
		}
		/**
		 * @brief Create a multi-line TextArea entity.
		 * @param n     Widget name.
		 * @param text  Initial document content.
		 * @param ph    Placeholder text shown when the document is empty and unfocused.
		 */
		ECS::EntityId MakeTextArea(const std::string &n, const std::string &text = "", const std::string &ph = "") {
			ECS::EntityId e = _Make(n, WidgetType::TextArea);
			m_ctx.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
			auto &ta = m_ctx.Add<TextAreaData>(e);
			ta.text = text;
			auto &lp = *m_ctx.Get<LayoutProps>(e);
			lp.padding = {6.f, 6.f, 6.f, 6.f};
			if (!ph.empty()) {
				// Store placeholder in Content component
				m_ctx.Get<Content>(e)->placeholder = ph;
			}
			return e;
		}

		// ── Builder factories (defined after Builder) ─────────────────────────────────

		/** @brief Create a Container and return a Builder for it. */
		inline Builder Container(const std::string &n = "Container");
		/** @brief Create a Label and return a Builder for it. */
		inline Builder Label(const std::string &n, const std::string &t = "");
		/** @brief Create a Button and return a Builder for it. */
		inline Builder Button(const std::string &n, const std::string &t = "");
		/** @brief Create a Toggle switch and return a Builder for it. */
		inline Builder Toggle(const std::string &n, const std::string &t = "");
		/** @brief Create a RadioButton and return a Builder for it. */
		inline Builder Radio(const std::string &n, const std::string &grp, const std::string &t = "");
		/** @brief Create a Slider and return a Builder for it. */
		inline Builder Slider(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.f,
								Orientation o = Orientation::Horizontal);
		/** @brief Create a ScrollBar and return a Builder for it. */
		inline Builder ScrollBar(const std::string &n, float cs = 0.f, float vs = 0.f,
								   Orientation o = Orientation::Vertical);
		/** @brief Create a Progress bar and return a Builder for it. */
		inline Builder Progress(const std::string &n, float v = 0.f, float mx = 1.f);
		/** @brief Create a Separator and return a Builder for it. */
		inline Builder Separator(const std::string &n = "sep");
		/** @brief Create a single-line Input and return a Builder for it. */
		inline Builder Input(const std::string &n, const std::string &ph = "");
		/** @brief Create a Knob and return a Builder for it. */
		inline Builder Knob(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.5f);
		/** @brief Create an Image widget and return a Builder for it. */
		inline Builder ImageWidget(const std::string &n, const std::string &p = "", ImageFit f = ImageFit::Contain);
		/** @brief Create a Canvas widget and return a Builder for it. */
		inline Builder CanvasWidget(const std::string &n,
			std::function<void(SDL::Event&)> cb_event = nullptr,
			std::function<void(float)> cb_update = nullptr,
			std::function<void(RendererRef, FRect)> cb_render = nullptr);
		/** @brief Create a multi-line TextArea and return a Builder for it. */
		inline Builder TextArea(const std::string &n, const std::string &text = "", const std::string &ph = "");
		/** @brief Create a ListBox and return a Builder for it. */
		inline Builder ListBoxWidget(const std::string &n, const std::vector<std::string>& items = {});
		/** @brief Create a Graph (graduated data plot) and return a Builder for it. */
		inline Builder GradedGraph(const std::string &n);
		/** @brief Create a vertical Column container (InColumn layout) and return a Builder for it. */
		inline Builder Column(const std::string &n = "col", float gap = 4.f, float pad = 8.f, float marg = 0.f);
		/** @brief Create a horizontal Row container (InLine layout) and return a Builder for it. */
		inline Builder Row(const std::string &n = "row", float gap = 8.f, float pad = 0.f, float marg = 0.f);
		/** @brief Create a Card container (Column with card styling) and return a Builder for it. */
		inline Builder Card(const std::string &n = "card", float gap = 8.f, float marg = 0.f);
		/** @brief Create a wrapping Stack container and return a Builder for it. */
		inline Builder Stack(const std::string &n = "stack", float gap = 0.f, float pad = 0.f, float marg = 0.f);
		/** @brief Create an accent-colored section title Label and return a Builder for it. */
		inline Builder SectionTitle(const std::string &text, SDL::Color color = {70, 130, 210, 255});
		/** @brief Create a vertical ScrollView (Column with auto vertical scrollbar) and return a Builder for it. */
		inline Builder ScrollView(const std::string &n, float gap = 4.f);
		/// Grid container: children placed on a `columns × auto-rows` grid.
		/// Use `.GridCols(n)`, `.GridRows(n)`, `.GridColSizing(...)` etc. on the returned Builder
		/// to customise, and `.Cell(col, row)` on each child to set its grid position.
		inline Builder Grid(const std::string &n = "grid", int columns = 2, float gap = 4.f, float pad = 8.f);

		// ── Tree management ───────────────────────────────────────────────────────────

		/** @brief Set the root widget entity; it is laid out to fill the full viewport. */
		void SetRoot(ECS::EntityId e) { m_root = e; }
		/** @brief Returns the current root entity (ECS::NullEntity if none). */
		[[nodiscard]] ECS::EntityId GetRootId() const { return m_root; }

		/**
		 * @brief Append child @p c to parent @p p.
		 * @param p  Parent entity (must be alive).
		 * @param c  Child entity (must be alive).
		 */
		void AppendChild(ECS::EntityId p, ECS::EntityId c) {
			if (!m_ctx.IsAlive(p) || !m_ctx.IsAlive(c)) 
				return;
			m_ctx.Get<Children>(p)->Add(c);
			m_ctx.Get<Parent>(c)->id = p;
		}
		/**
		 * @brief Remove child @p c from parent @p p.
		 * @param p  Parent entity.
		 * @param c  Child entity to detach.
		 */
		void RemoveChild(ECS::EntityId p, ECS::EntityId c) {
			if (!m_ctx.IsAlive(p))
				return;
			m_ctx.Get<Children>(p)->Remove(c);
			if (m_ctx.IsAlive(c))
				m_ctx.Get<Parent>(c)->id = ECS::NullEntity;
		}

		/** @brief Return a Builder wrapping an existing entity (for post-creation styling). */
		inline Builder GetBuilder(ECS::EntityId e);

		// ── Component accessors ───────────────────────────────────────────────────────

		/** @brief Direct access to the Style component of entity @p e. */
		Style &GetStyle(ECS::EntityId e) { return *m_ctx.Get<Style>(e); }
		/** @brief Direct access to the LayoutProps component of entity @p e. */
		LayoutProps &GetLayout(ECS::EntityId e) { return *m_ctx.Get<LayoutProps>(e); }
		/** @brief Direct access to the Content component of entity @p e. */
		Content &GetContent(ECS::EntityId e) { return *m_ctx.Get<Content>(e); }

		// ── Setters ───────────────────────────────────────────────────────────────────

		/**
		 * @brief Set the display text of a Label, Button, or Input widget.
		 * @param e  Target entity.
		 * @param t  New text content; cursor is moved to the end.
		 */
		void SetText(ECS::EntityId e, const std::string &t) {
			if (auto *c = m_ctx.Get<Content>(e)) {
				c->text = t;
				c->cursor = (int)t.size();
			}
		}

		/**
		 * @brief Set the numeric value of a Slider or Knob widget (clamped to [min, max]).
		 * @param e  Target entity.
		 * @param v  New value.
		 */
		void SetValue(ECS::EntityId e, float v) {
			if (auto *s = m_ctx.Get<SliderData>(e))
				s->val = SDL::Clamp(v, s->min, s->max);
			if (auto *k = m_ctx.Get<KnobData>(e)) {
				k->val = SDL::Clamp(v, k->min, k->max);
			}
		}

		// ── ListBox accessors ─────────────────────────────────────────────────────────

		/**
		 * @brief Replace the item list of a ListBox widget.
		 *
		 * Resets the scroll offset and clears the selection if it is out of range.
		 * @param e      Target entity.
		 * @param items  New item list.
		 */
		void SetListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
			auto *lb = m_ctx.Get<ListBoxData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (lb && lp) {
				lb->items = std::move(items);
				lp->scrollY = 0.f;
				if (lb->selectedIndex >= (int)lb->items.size())
					lb->selectedIndex = -1;
			}
		}
		/**
		 * @brief Return the index of the selected ListBox item, or -1 if none.
		 * @param e  Target entity.
		 */
		[[nodiscard]] int GetListBoxSelection(ECS::EntityId e) const {
			if (auto *lb = m_ctx.Get<ListBoxData>(e)) return lb->selectedIndex;
			return -1;
		}
		/**
		 * @brief Programmatically select a ListBox item.
		 * @param e    Target entity.
		 * @param idx  Item index to select, or -1 to clear the selection.
		 */
		void SetListBoxSelection(ECS::EntityId e, int idx) {
			if (auto *lb = m_ctx.Get<ListBoxData>(e))
				lb->selectedIndex = (idx >= 0 && idx < (int)lb->items.size()) ? idx : -1;
		}
		/** @brief Return a pointer to the ListBoxData component of @p e, or nullptr. */
		ListBoxData* GetListBoxData(ECS::EntityId e) { return m_ctx.Get<ListBoxData>(e); }

		// ── Tooltip accessors ─────────────────────────────────────────────────────────

		/// Attache (ou met à jour) un tooltip sur le widget @p e.
		void SetTooltip(ECS::EntityId e, const std::string &text, float delay = 1.f) {
			auto *td = m_ctx.Get<TooltipData>(e);
			if (!td) td = &m_ctx.Add<TooltipData>(e);
			td->text  = text;
			td->delay = delay;
		}
		/// Supprime le tooltip du widget @p e.
		void RemoveTooltip(ECS::EntityId e) { m_ctx.Remove<TooltipData>(e); }

		// ── Graph accessors ───────────────────────────────────────────────────────────

		/**
		 * @brief Replace the Y-data samples of a Graph widget.
		 * @param e     Target entity.
		 * @param data  New Y-values to plot (one per X sample).
		 */
		void SetGraphData(ECS::EntityId e, std::vector<float> data) {
			if (auto *gd = m_ctx.Get<GraphData>(e)) gd->data = std::move(data);
		}
		/**
		 * @brief Set the Y-axis display range of a Graph widget.
		 * @param e     Target entity.
		 * @param minV  Y axis minimum.
		 * @param maxV  Y axis maximum.
		 */
		void SetGraphRange(ECS::EntityId e, float minV, float maxV) {
			if (auto *gd = m_ctx.Get<GraphData>(e)) { gd->minVal = minV; gd->maxVal = maxV; }
		}
		/**
		 * @brief Set the X-axis display range of a Graph widget (used for tick labels).
		 * @param e     Target entity.
		 * @param xMin  X axis start value.
		 * @param xMax  X axis end value.
		 */
		void SetGraphXRange(ECS::EntityId e, float xMin, float xMax) {
			if (auto *gd = m_ctx.Get<GraphData>(e)) { gd->xMin = xMin; gd->xMax = xMax; }
		}
		/** @brief Return a pointer to the GraphData component of @p e, or nullptr. */
		GraphData* GetGraphData(ECS::EntityId e) { return m_ctx.Get<GraphData>(e); }

		/**
		 * @brief Programmatically set the scroll offset of a ScrollBar widget.
		 * @param e    Target entity.
		 * @param off  New offset (clamped to [0, contentSize - viewSize]).
		 */
		void SetScrollOffset(ECS::EntityId e, float off) {
			if (auto *sb = m_ctx.Get<ScrollBarData>(e)) {
				float mx = SDL::Max(0.f, sb->contentSize - sb->viewSize);
				sb->offset = SDL::Clamp(off, 0.f, mx);
			}
		}

		/**
		 * @brief Set the checked state of a Toggle or RadioButton widget.
		 * @param e  Target entity.
		 * @param b  New checked state.
		 */
		void SetChecked(ECS::EntityId e, bool b) {
			if (auto *t = m_ctx.Get<ToggleData>(e)) {
				t->checked = b;
				t->animT = b ? 1.f : 0.f;
			}
			if (auto *r = m_ctx.Get<RadioData>(e))
				r->checked = b;
		}

		/**
		 * @brief Enable or disable a widget (disabled widgets are not interactive but remain visible).
		 * @param e  Target entity.
		 * @param b  True to enable, false to disable.
		 */
		void SetEnabled(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Enable;
				else w->behavior &= (~BehaviorFlag::Enable);
			}
		}

		/**
		 * @brief Show or hide a widget (hidden widgets take no space and receive no input).
		 *
		 * Plays `showSound` / `hideSound` when the state actually changes and marks the
		 * layout dirty so the tree is re-measured next frame.
		 * @param e  Target entity.
		 * @param b  True to make visible, false to hide.
		 */
		void SetVisible(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				bool wasVisible = Has(w->behavior, BehaviorFlag::Visible);
				if (b && !wasVisible) {
					w->behavior |= BehaviorFlag::Visible;
					m_layoutDirty = true;
					if (auto *s = m_ctx.Get<Style>(e); s && !s->showSound.empty()) {
						if (auto sh = _EnsureAudio(s->showSound)) _PlayAudio(sh);
					}
				} else if (!b && wasVisible) {
					w->behavior &= (~BehaviorFlag::Visible);
					m_layoutDirty = true;
					if (auto *s = m_ctx.Get<Style>(e); s && !s->hideSound.empty()) {
						if (auto sh = _EnsureAudio(s->hideSound)) _PlayAudio(sh);
					}
				}
			}
		}

		/** @brief Enable or disable the Hoverable behavior flag on @p e. */
		void SetHoverable(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Hoverable;
				else w->behavior &= (~BehaviorFlag::Hoverable);
			}
		}

		/** @brief Enable or disable the Selectable behavior flag on @p e. */
		void SetSelectable(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Selectable;
				else w->behavior &= (~BehaviorFlag::Selectable);
			}
		}

		/** @brief Enable or disable the Focusable behavior flag on @p e. */
		void SetFocusable(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::Focusable;
				else w->behavior &= (~BehaviorFlag::Focusable);
			}
		}

		/** @brief Enable or disable the permanent horizontal scrollbar on @p e. */
		void SetScrollableX(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::ScrollableX;
				else w->behavior &= (~BehaviorFlag::ScrollableX);
			}
		}

		/** @brief Enable or disable the permanent vertical scrollbar on @p e. */
		void SetScrollableY(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::ScrollableY;
				else w->behavior &= (~BehaviorFlag::ScrollableY);
			}
		}

		/** @brief Shorthand to set both permanent scrollbar axes at once. */
		void SetScrollable(ECS::EntityId e, bool bx, bool by) {
			SetScrollableX(e, bx);
			SetScrollableY(e, by);
		}

		/// Scrollbar automatique horizontal (visible seulement si le contenu déborde).
		void SetAutoScrollableX(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::AutoScrollableX;
				else w->behavior &= (~BehaviorFlag::AutoScrollableX);
			}
		}

		/// Scrollbar automatique vertical (visible seulement si le contenu déborde).
		void SetAutoScrollableY(ECS::EntityId e, bool b) {
			if (auto *w = m_ctx.Get<Widget>(e)) {
				if (b) w->behavior |= BehaviorFlag::AutoScrollableY;
				else w->behavior &= (~BehaviorFlag::AutoScrollableY);
			}
		}

		/// Active les deux scrollbars automatiques.
		void SetAutoScrollable(ECS::EntityId e, bool bx, bool by) {
			SetAutoScrollableX(e, bx);
			SetAutoScrollableY(e, by);
		}

		/// Épaisseur (px) des scrollbars inline dessinées dans le container.
		void SetScrollbarThickness(ECS::EntityId e, float t) {
			if (auto *lp = m_ctx.Get<LayoutProps>(e))
				lp->scrollbarThickness = SDL::Max(4.f, t);
		}

		// ── Grid layout helpers ───────────────────────────────────────────────────

		/// Ensure a LayoutGridProps component exists on `e` and return a reference to it.
		LayoutGridProps &EnsureGridProps(ECS::EntityId e) {
			if (auto *gp = m_ctx.Get<LayoutGridProps>(e))
				return *gp;
			return m_ctx.Add<LayoutGridProps>(e);
		}

		/** @brief Set the column count for a Layout::InGrid container. */
		void SetGridCols(ECS::EntityId e, int n) { EnsureGridProps(e).columns = SDL::Max(1, n); }
		/** @brief Set the fixed row count for a Layout::InGrid container (0 = auto). */
		void SetGridRows(ECS::EntityId e, int n) { EnsureGridProps(e).rows    = SDL::Max(0, n); }
		/** @brief Set the column sizing mode for a Layout::InGrid container. */
		void SetGridColSizing(ECS::EntityId e, GridSizing s) { EnsureGridProps(e).colSizing = s; }
		/** @brief Set the row sizing mode for a Layout::InGrid container. */
		void SetGridRowSizing(ECS::EntityId e, GridSizing s) { EnsureGridProps(e).rowSizing = s; }
		/** @brief Set which separator lines are drawn inside a Layout::InGrid container. */
		void SetGridLines(ECS::EntityId e, GridLines l)      { EnsureGridProps(e).lines     = l; }
		/** @brief Set the color of the separator lines in a Layout::InGrid container. */
		void SetGridLineColor(ECS::EntityId e, SDL::Color c) { EnsureGridProps(e).lineColor = c; }
		/** @brief Set the thickness of the separator lines in a Layout::InGrid container. */
		void SetGridLineThickness(ECS::EntityId e, float t)  { EnsureGridProps(e).lineThickness = SDL::Max(0.f, t); }

		/// Ensure a GridCell component exists on `e` and set its position/span.
		void SetGridCell(ECS::EntityId e, int col, int row, int colSpan = 1, int rowSpan = 1) {
			GridCell *gc = m_ctx.Get<GridCell>(e);
			if (!gc) gc = &m_ctx.Add<GridCell>(e);
			gc->col     = SDL::Max(0, col);
			gc->row     = SDL::Max(0, row);
			gc->colSpan = SDL::Max(1, colSpan);
			gc->rowSpan = SDL::Max(1, rowSpan);
		}

		/**
		 * @brief Register (or replace) the event callback on a Canvas widget.
		 * @param e   Target entity.
		 * @param cb  New event handler.
		 */
		void OnEventCanvas(ECS::EntityId e, std::function<void(SDL::Event&)> cb) {
			if (auto *c = m_ctx.Get<CanvasData>(e))
				c->eventCb = std::move(cb);
		}

		/**
		 * @brief Register (or replace) the per-frame update callback on a Canvas widget.
		 * @param e   Target entity.
		 * @param cb  Callback receiving the frame delta time in seconds.
		 */
		void OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb) {
			if (auto *c = m_ctx.Get<CanvasData>(e))
				c->updateCb = std::move(cb);
		}

		/**
		 * @brief Register (or replace) the render callback on a Canvas widget.
		 * @param e   Target entity.
		 * @param cb  Callback receiving the renderer and the screen-space rect of the canvas.
		 */
		void OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb) {
			if (auto *c = m_ctx.Get<CanvasData>(e))
				c->renderCb = std::move(cb);
		}

		// ── Tileset skin ──────────────────────────────────────────────────────────────

		/**
		 * Attach (or replace) a tileset 9-slice skin on widget `e`.
		 * Pass a default-constructed `TilesetStyle` with an empty `textureKey`
		 * to remove the skin.
		 */
		void SetTilesetStyle(ECS::EntityId e, TilesetStyle ts) {
			if (!m_ctx.IsAlive(e)) return;
			m_ctx.Add<TilesetStyle>(e, std::move(ts));
		}

		/** @brief Return a pointer to the TilesetStyle component of @p e, or nullptr. */
		[[nodiscard]] TilesetStyle* GetTilesetStyle(ECS::EntityId e) {
			return m_ctx.Get<TilesetStyle>(e);
		}

		/** @brief Remove the TilesetStyle component from @p e (restores default rendering). */
		void RemoveTilesetStyle(ECS::EntityId e) {
			m_ctx.Remove<TilesetStyle>(e);
		}

		/**
		 * @brief Change the texture key and fit mode of an Image widget at runtime.
		 * @param e    Target entity.
		 * @param key  New resource-pool key.
		 * @param f    New fit mode.
		 */
		void SetImageKey(ECS::EntityId e, const std::string &key, ImageFit f = ImageFit::Contain) {
			if (auto *d = m_ctx.Get<ImageData>(e)) {
				d->key = key;
				d->fit = f;
			}
		}

		/// Return a reference to the IconData component, creating it if absent.
		IconData &GetOrAddIconData(ECS::EntityId e) {
			if (auto *ic = m_ctx.Get<IconData>(e)) return *ic;
			return m_ctx.Add<IconData>(e);
		}

		// ── TextArea accessors ────────────────────────────────────────────────────────

		/**
		 * @brief Replace the entire document content of a TextArea widget.
		 *
		 * Resets the cursor, selection, and vertical scroll to the top.
		 * @param e  Target entity.
		 * @param t  New document text (LF line endings).
		 */
		void SetTextAreaContent(ECS::EntityId e, const std::string &t) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
				ta->text = t;
				ta->cursorPos = 0;
				ta->ClearSelection();
				ta->scrollY = 0.f;
			}
		}
		/**
		 * @brief Return the document content of a TextArea widget.
		 * @param e  Target entity.
		 * @return   Reference to the document string, or an empty static string if not found.
		 */
		[[nodiscard]] const std::string &GetTextAreaContent(ECS::EntityId e) const {
			static const std::string empty;
			const auto *ta = m_ctx.Get<TextAreaData>(e);
			return ta ? ta->text : empty;
		}
		/** @brief Set the selection highlight color of a TextArea widget. */
		void SetTextAreaHighlightColor(ECS::EntityId e, SDL::Color c) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->highlightColor = c;
		}
		/** @brief Set the tab-stop size (in character columns) for a TextArea widget. */
		void SetTextAreaTabSize(ECS::EntityId e, int sz) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->tabSize = SDL::Max(1, sz);
		}
		/**
		 * @brief Add a styled span to a TextArea widget (rich text highlight).
		 * @param e      Target entity.
		 * @param start  Byte offset of the span start (inclusive).
		 * @param end    Byte offset of the span end (exclusive).
		 * @param style  Style to apply over the byte range.
		 */
		void AddTextAreaSpan(ECS::EntityId e, int start, int end, TextSpanStyle style) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->AddSpan(start, end, style);
		}
		/** @brief Remove all rich-text spans from a TextArea widget. */
		void ClearTextAreaSpans(ECS::EntityId e) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->ClearSpans();
		}
		/** @brief Return a pointer to the TextAreaData component of @p e, or nullptr. */
		[[nodiscard]] TextAreaData* GetTextAreaData(ECS::EntityId e) {
			return m_ctx.Get<TextAreaData>(e);
		}
		/**
		 * @brief Set whether a TextArea is read-only (selection and copy still work).
		 * @param e   Target entity.
		 * @param ro  True to prevent editing.
		 */
		void SetTextAreaReadOnly(ECS::EntityId e, bool ro) {
			if (auto *ta = m_ctx.Get<TextAreaData>(e)) ta->readOnly = ro;
		}
		/** @brief Return true if the TextArea widget at @p e is in read-only mode. */
		[[nodiscard]] bool GetTextAreaReadOnly(ECS::EntityId e) const {
			const auto *ta = m_ctx.Get<TextAreaData>(e);
			return ta && ta->readOnly;
		}

		// ── Getters ───────────────────────────────────────────────────────────────────

		/**
		 * @brief Return the display text of a Label, Button, or Input widget.
		 * @param e  Target entity.
		 * @return   Const reference to the text string, or an empty static string.
		 */
		[[nodiscard]] const std::string &GetText(ECS::EntityId e) const {
			static const std::string empty;
			const auto *c = m_ctx.Get<Content>(e);
			return c ? c->text : empty;
		}

		/** @brief Return the current numeric value of a Slider widget (0 if not found). */
		[[nodiscard]] float GetValue(ECS::EntityId e) const {
			const auto *s = m_ctx.Get<SliderData>(e);
			return s ? s->val : 0.f;
		}

		/** @brief Return the current scroll offset of a ScrollBar widget (0 if not found). */
		[[nodiscard]] float GetScrollOffset(ECS::EntityId e) const {
			const auto *sb = m_ctx.Get<ScrollBarData>(e);
			return sb ? sb->offset : 0.f;
		}

		/** @brief Return true if a Toggle or RadioButton widget is currently checked. */
		[[nodiscard]] bool IsChecked(ECS::EntityId e) const {
			if (const auto *t = m_ctx.Get<ToggleData>(e))
				return t->checked;
			if (const auto *r = m_ctx.Get<RadioData>(e))
				return r->checked;
			return false;
		}

		/** @brief Return true if widget @p e has the Enable behavior flag set. */
		[[nodiscard]] bool IsEnabled(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Enable);
		}

		/** @brief Return true if widget @p e has the Visible behavior flag set. */
		[[nodiscard]] bool IsVisible(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Visible);
		}

		/** @brief Return true if widget @p e has the Hoverable behavior flag set. */
		[[nodiscard]] bool IsHoverable(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Hoverable);
		}

		/** @brief Return true if widget @p e has the Selectable behavior flag set. */
		[[nodiscard]] bool IsSelectable(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Selectable);
		}

		/** @brief Return true if widget @p e has the Focusable behavior flag set. */
		[[nodiscard]] bool IsFocusable(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::Focusable);
		}

		/** @brief Return true if widget @p e has the ScrollableX behavior flag set. */
		[[nodiscard]] bool IsScrollableX(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::ScrollableX);
		}

		/** @brief Return true if widget @p e has the ScrollableY behavior flag set. */
		[[nodiscard]] bool IsScrollableY(ECS::EntityId e) const {
			const auto *w = m_ctx.Get<Widget>(e);
			return w && Has(w->behavior, BehaviorFlag::ScrollableY);
		}

		/** @brief Return true if the mouse cursor is currently over widget @p e. */
		[[nodiscard]] bool IsHovered(ECS::EntityId e) const {
			const auto *s = m_ctx.Get<WidgetState>(e);
			return s && s->hovered;
		}

		/** @brief Return true if widget @p e currently holds keyboard focus. */
		[[nodiscard]] bool IsFocused(ECS::EntityId e) const {
			return m_focused == e;
		}
		
		/** @brief Return true if widget @p e is currently being pressed (mouse button held). */
		[[nodiscard]] bool IsPressed(ECS::EntityId e) const {
			const auto *s = m_ctx.Get<WidgetState>(e);
			return s && s->pressed;
		}

		/**
		 * @brief Return the screen-space rectangle of widget @p e as computed in the last layout pass.
		 * @param e  Target entity.
		 * @return   Screen rect, or a zero-sized rect if the entity has no ComputedRect.
		 */
		[[nodiscard]] FRect GetScreenRect(ECS::EntityId e) const {
			const auto *c = m_ctx.Get<ComputedRect>(e);
			return c ? c->screen : FRect{};
		}

		/**
		 * @brief Pre-load a texture into the resource pool under the given key.
		 * @param key   Pool key used to retrieve the texture later.
		 * @param path  File path of the image.
		 */
		void LoadTexture(const std::string& key, const std::string& path) {
			_EnsureTexture(key, path);
		}

		/**
		 * @brief Pre-load a font into the resource pool under the given key (size 8 pt).
		 * @param key   Pool key used to retrieve the font later.
		 * @param path  File path of the TTF/OTF font.
		 */
		void LoadFont(const std::string& key, const std::string& path) {
			_EnsureFont(key, 8.0f, path);
		}

		/**
		 * @brief Pre-load an audio clip into the resource pool under the given key.
		 * @param key   Pool key used to retrieve the audio later.
		 * @param path  File path of the audio file.
		 */
		void LoadAudio(const std::string& key, const std::string& path) {
			_EnsureAudio(key, path);
		}

		// ── Callback registration ─────────────────────────────────────────────────────

		/** @brief Register (or replace) the click callback on widget @p e. */
		void OnClick(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onClick = std::move(cb); }
		/** @brief Register (or replace) the double-click callback on widget @p e. */
		void OnDoubleClick(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onDoubleClick = std::move(cb); }
		/** @brief Enable or disable event propagation to parent for widget @p e (default: true). */
		void SetDispatchEvent(ECS::EntityId e, bool b) { if (auto *w = m_ctx.Get<Widget>(e)) w->dispatchEvent = b; }
		/** @brief Register (or replace) the value-change callback on widget @p e (Slider, ScrollBar). */
		void OnChange(ECS::EntityId e, std::function<void(float)> cb) { m_ctx.Get<Callbacks>(e)->onChange = std::move(cb); }
		/** @brief Register (or replace) the text-change callback on widget @p e (Input, TextArea). */
		void OnTextChange(ECS::EntityId e, std::function<void(const std::string &)> cb) { m_ctx.Get<Callbacks>(e)->onTextChange = std::move(cb); }
		/** @brief Register (or replace) the toggle callback on widget @p e (Toggle, RadioButton). */
		void OnToggle(ECS::EntityId e, std::function<void(bool)> cb) { m_ctx.Get<Callbacks>(e)->onToggle = std::move(cb); }
		/** @brief Register (or replace) the scroll callback on widget @p e (Container, ScrollBar). */
		void OnScroll(ECS::EntityId e, std::function<void(float)> cb) { m_ctx.Get<Callbacks>(e)->onScroll = std::move(cb); }
		/** @brief Register (or replace) the hover-enter callback on widget @p e. */
		void OnHoverEnter(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onHoverEnter = std::move(cb); }
		/** @brief Register (or replace) the hover-leave callback on widget @p e. */
		void OnHoverLeave(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onHoverLeave = std::move(cb); }
		/** @brief Register (or replace) the focus-gain callback on widget @p e. */
		void OnFocusGain(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onFocusGain = std::move(cb); }
		/** @brief Register (or replace) the focus-lose callback on widget @p e. */
		void OnFocusLose(ECS::EntityId e, std::function<void()> cb) { m_ctx.Get<Callbacks>(e)->onFocusLose = std::move(cb); }

		/** @brief Return a reference to the underlying ECS context (for advanced use). */
		[[nodiscard]] ECS::Context &GetECSContext() { return m_ctx; }

		// ── Frame pipeline ────────────────────────────────────────────────────────────

		/**
		 * @brief Feed an SDL event into the UI system.
		 *
		 * Must be called once per event from the application event loop.  Handles
		 * mouse, keyboard, text-input, mouse-wheel, and drop-text events.
		 * @param ev  The SDL event to process.
		 */
		void ProcessEvent(const SDL::Event &ev) {
			switch (ev.type) {
				case SDL::EVENT_WINDOW_RESIZED:
				case SDL::EVENT_WINDOW_PIXEL_SIZE_CHANGED:
					m_layoutDirty = true; // Déclenche le recalcul complet
					break;
				case SDL::EVENT_MOUSE_BUTTON_DOWN:
					if (ev.button.button == SDL::BUTTON_LEFT) {
						m_mouseDown    = true;
						m_mousePressed = true;
					}
					break;
				case SDL::EVENT_MOUSE_BUTTON_UP:
					if (ev.button.button == SDL::BUTTON_LEFT) {
						m_mouseDown     = false;
						m_mouseReleased = true;
					}
					break;
				case SDL::EVENT_MOUSE_MOTION:
					m_mousePos   = {ev.motion.x, ev.motion.y};
					m_mouseDelta = {ev.motion.xrel, ev.motion.yrel};
					break;
				case SDL::EVENT_TEXT_INPUT:
					_HandleTextInput(ev.text.text);
					break;
				case SDL::EVENT_KEY_DOWN:
					_HandleKeyDown(ev.key.key, ev.key.mod);
					break;
				case SDL::EVENT_MOUSE_WHEEL:
					_HandleScroll(ev.wheel.x, ev.wheel.y);
					break;
				case SDL::EVENT_DROP_TEXT:
					if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
						auto *w = m_ctx.Get<Widget>(m_focused);
						if (w && Has(w->behavior, BehaviorFlag::Enable)) {
							auto processDrop = [&]<typename T>(T* data) {
								if (data) _InsertText(data, std::string_view(ev.drop.data));
							};
							if (w->type == WidgetType::Input) processDrop(m_ctx.Get<Content>(m_focused));
							else if (w->type == WidgetType::TextArea) processDrop(m_ctx.Get<TextAreaData>(m_focused));
						}
					}
					break;
				default:
					break;
			}
			_CanvasEvent(ev);
		}

		/**
		 * @brief Run one full UI frame: layout → input → render → animate.
		 *
		 * Must be called once per game frame after all `ProcessEvent` calls.
		 * @param dt  Frame delta time in seconds.
		 */
		void Iterate(float dt) {
			m_pool.Update();

			m_dt = dt;
			m_timeSinceLastClick += dt;
			if (m_root == ECS::NullEntity || !m_ctx.IsAlive(m_root)) {
				_ResetOneShots();
				return;
			}

			SDL::Point sz    = m_renderer.GetWindow().GetSize();
			SDL::FRect newVp = {0.f, 0.f, (float)sz.x, (float)sz.y};
			if (newVp.w != m_viewport.w || newVp.h != m_viewport.h)
				m_layoutDirty = true;
			m_viewport = newVp;

			// Canvas update callbacks run first so game logic sees current dt.
			_ProcessCanvasUpdate(dt);

			_ProcessLayout();
			_ProcessInput();
			_ProcessTooltip(dt);
			_ProcessRender();
			_ProcessAnimate(dt);
			_ResetOneShots();
			m_mouseDelta  = {};
			m_layoutDirty = false;
		}

	private:
		ECS::Context& m_ctx;
		RendererRef   m_renderer;
		MixerRef      m_mixer;
		ResourcePool& m_pool;
#if UI_HAS_TTF
		// Owned TTF text engine.  Declared *after* m_ctx/m_pool (references)
		// so that its destruction order is controlled explicitly in ~System():
		// all TextCache ECS components are cleared first, then this resets.
		std::optional<SDL::RendererTextEngine> m_engine;
#endif
		bool        m_usedDebugFontPerDefault = false;
		std::string m_defaultFontPath;
		float       m_defaultFontSize = 0.f;
		ECS::EntityId m_root = ECS::NullEntity, m_focused = ECS::NullEntity, m_hovered = ECS::NullEntity, m_pressed = ECS::NullEntity;
		float m_dt = 0.f;
		// ── Double-click tracking ──────────────────────────────────────────────────────
		float         m_timeSinceLastClick  = 1e9f;
		ECS::EntityId m_lastClickEntity     = ECS::NullEntity;
		int           m_clickCount          = 0;

		// ── Tooltip state ─────────────────────────────────────────────────────────────
		ECS::EntityId m_tooltipEntity  = ECS::NullEntity; ///< Entité dédiée au cache de texte du tooltip.
		ECS::EntityId m_tooltipTarget  = ECS::NullEntity; ///< Entité actuellement survol-minutée.
		float         m_tooltipTimer   = 0.f;             ///< Temps de survol accumulé (secondes).
		bool          m_tooltipVisible = false;           ///< Vrai si le tooltip est visible ce frame.
		FRect m_viewport = {};
		FPoint m_mousePos = {}, m_mouseDelta = {};
		bool m_mouseDown = false, m_mousePressed = false, m_mouseReleased = false;
		bool m_layoutDirty = true; ///< true whenever a resize or structural change requires a full re-layout
		void _ResetOneShots() { m_mousePressed = m_mouseReleased = false; }

		// ── Texture ──────────────────────────────────────────────────────────────────

		SDL::TextureRef _EnsureTexture(const std::string &key, const std::string &path = "") {
			if (key.empty()) return nullptr; 

			auto h = m_pool.Get<SDL::Texture>(key);
			if (h) return *h.get();

			SDL::Texture wrapper;
			try {
				wrapper = SDL::Texture(m_renderer, path.empty() ? key : path);
			} catch (...) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open font '{}'", path.empty() ? key : path));
				return nullptr;
			}

			m_pool.Add<SDL::Texture>(key, std::move(wrapper));
			auto h2 = m_pool.Get<SDL::Texture>(key);
			return h2 ? SDL::TextureRef(*h2.get()) : nullptr;
		}

		// ── Font ──────────────────────────────────────────────────────────────────────

		/// Ensure the RendererTextEngine exists as an owned member; return a pointer.
		/// The engine is lazy-initialised on first use and destroyed in ~System()
		/// *after* all TextCache ECS components have been cleared, which guarantees
		/// that no SDL::Text outlives its engine (avoiding the use-after-free that
		/// occurred when the engine was stored in the external ResourcePool).
		SDL::RendererTextEngine *_EnsureEngine() {
			if (!m_engine.has_value()) {
				try {
					m_engine.emplace(m_renderer);
				} catch (...) {
					SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
								 "UI::System: failed to create RendererTextEngine");
					return nullptr;
				}
			}
			return &m_engine.value();
		}

		SDL::FontRef _EnsureFont(const std::string &key, float ptsize, const std::string& path = "") { 
			if (key.empty() || ptsize <= 0.f)
				return nullptr;

			auto h = m_pool.Get<SDL::Font>(key);
			if (h) {
				if (h->GetSize() != ptsize) {
					h->SetSize(ptsize);
				}
				return *h.get();
			}

			// Slow key: load and insert.
			SDL::Font wrapper;
			try {
				wrapper = SDL::Font(path.empty() ? key : path, ptsize);
			} catch (...) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open font '{}' at {:.2f}pt", path.empty() ? key : path, ptsize));
				return nullptr;
			}

			// Store even when invalid — acts as a "failed" sentinel so we don't retry.
			m_pool.Add<SDL::Font>(key, std::move(wrapper));
			auto h2 = m_pool.Get<SDL::Font>(key);
			if (h2) {
				if (h2->GetSize() != ptsize) {
					h2->SetSize(ptsize);
				}
				return SDL::FontRef(*h2.get());
			}
			return nullptr;
		}

		SDL::TextRef _EnsureText(ECS::EntityId e, SDL::FontRef font, const std::string& text) { 
			if (!font || text.empty()) return nullptr;

			auto* engine = _EnsureEngine();
			if (!engine) return nullptr;

			auto* cache = m_ctx.Get<TextCache>(e);
			if (!cache) {
				cache = &m_ctx.Add<TextCache>(e);
			}

			if (cache->text) {
				if (cache->text.GetFont().Get() != font.Get()) {
					cache->text = engine->CreateText(font, text);
				} else {
					cache->text.SetString(text); 
				}

			} else {
				try {
					cache->text = engine->CreateText(font, text);
				} catch (const std::exception& ex) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
							 std::format("UI::System: Failed to create text for entity {}: {}", e, ex.what()));
				return nullptr;
				}
			}
			return SDL::TextRef(cache->text.Get());
		}
		
		// ── Font resolution ───────────────────────────────────────────────────────────

		struct ResolvedFont {
			std::string key;
			float size   = 0.f;
			bool isDebug = false;
		};

		/// Walk the FontType chain for entity @p e and return the effective font.
		///
		/// - FontType::Self    → own fontKey/fontSize (falls through to Inherited if unset).
		/// - FontType::Inherited → walks up parents until a Self ancestor is found, then Default.
		/// - FontType::Root    → uses root widget's Self font, then Default.
		/// - FontType::Default → system SetDefaultFont() path, or debug if none configured.
		/// - FontType::Debug   → always SDL3 built-in debug font.
		ResolvedFont _ResolveFont(ECS::EntityId e) {
			for (ECS::EntityId cur = e; cur != ECS::NullEntity && m_ctx.IsAlive(cur);) {
				auto* s = m_ctx.Get<Style>(cur);
				if (!s) break;

				switch (s->usedFont) {
					case FontType::Debug:
						return {"", s->fontSize > 0.f ? s->fontSize : m_defaultFontSize, true};
					case FontType::Default:
						if (!m_defaultFontPath.empty())
							return {m_defaultFontPath, m_defaultFontSize, false};
						return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
					case FontType::Self:
						if (!s->fontKey.empty() && s->fontSize > 0.f)
							return {s->fontKey, s->fontSize, false};
						break; // Self but unconfigured → walk up
					case FontType::Root: {
						ECS::EntityId root = cur;
						while (true) {
							auto* p = m_ctx.Get<Parent>(root);
							if (!p || p->id == ECS::NullEntity) break;
							root = p->id;
						}
						if (root != cur) {
							auto* rs = m_ctx.Get<Style>(root);
							if (rs && rs->usedFont == FontType::Self &&
								!rs->fontKey.empty() && rs->fontSize > 0.f)
								return {rs->fontKey, rs->fontSize, false};
						}
						// Root has no Self font → system default
						if (!m_defaultFontPath.empty())
							return {m_defaultFontPath, m_defaultFontSize, false};
						return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
					}
					case FontType::Inherited:
						break; // walk up
				}

				auto* p = m_ctx.Get<Parent>(cur);
				if (!p || p->id == ECS::NullEntity) break;
				cur = p->id;
			}
			// No configured ancestor → system default
			if (!m_defaultFontPath.empty())
				return {m_defaultFontPath, m_defaultFontSize, false};
			return {"", m_defaultFontSize, m_usedDebugFontPerDefault};
		}

		// ── Audio ─────────────────────────────────────────────────────────────────────

		SDL::AudioRef _EnsureAudio(const std::string& key, const std::string& path = "") { 
			if (key.empty())
				return nullptr;

			auto h = m_pool.Get<SDL::Audio>(key);
			if (h) return *h.get();

			SDL::Audio wrapper;
			try {
				wrapper = SDL::Audio(m_mixer, path.empty() ? key : path, false);
			} catch (...) { 
				SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, std::format("UI::System: cannot open audio '{}'", path.empty() ? key : path));
				return nullptr;
			}

			// Store even when invalid — acts as a "failed" sentinel so we don't retry.
			m_pool.Add<SDL::Audio>(key, std::move(wrapper));
			auto h2 = m_pool.Get<SDL::Audio>(key);
			return h2 ? SDL::AudioRef(*h2.get()) : nullptr;
		}

		void _PlayAudio(SDL::AudioRef audio) { 
			m_mixer.PlayAudio(audio);
		}


		// ── Make widget ─────────────────────────────────────────────────────────────────────────────
		ECS::EntityId _Make(const std::string &n, WidgetType k) {
			ECS::EntityId e = m_ctx.CreateEntity();

			// Compute the correct behavior flags for this widget type.
			// All widgets start Enabled + Visible.  Interactive widgets additionally
			// get Hoverable / Selectable / Focusable as appropriate.
			BehaviorFlag beh = BehaviorFlag::Enable | BehaviorFlag::Visible;
			switch (k) { 
				case WidgetType::Button:
				case WidgetType::Toggle:
				case WidgetType::RadioButton:
				case WidgetType::Slider:
				case WidgetType::Knob:
				case WidgetType::ScrollBar:
				case WidgetType::Input:
				case WidgetType::Canvas:
				case WidgetType::TextArea:
				case WidgetType::ListBox:
					beh |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
					break; 
				case WidgetType::Graph:
					beh |= BehaviorFlag::Hoverable;
					break;
				case WidgetType::Container:
					beh |= BehaviorFlag::AutoScrollableX | BehaviorFlag::AutoScrollableY;
				// Label, Progress, Separator, Image n'ont PAS Hoverable/Selectable/Focusable par défaut
				default:
					break;
			}

			m_ctx.Add<Widget>(e, {n, k, beh, DirtyFlag::All});
			auto &style = m_ctx.Add<Style>(e);
			m_ctx.Add<LayoutProps>(e);
			m_ctx.Add<Content>(e);
			m_ctx.Add<WidgetState>(e);
			m_ctx.Add<Callbacks>(e);
			m_ctx.Add<ComputedRect>(e);
			m_ctx.Add<Children>(e);
			m_ctx.Add<Parent>(e);

			// Containers get a scroll-drag state component for their inline scrollbars.
			if (k == WidgetType::Container || k == WidgetType::ListBox)
				m_ctx.Add<ContainerScrollState>(e);

			style.usedFont = FontType::Default;
			return e;
		}

		/// Build a child LayoutContext from the current viewport + root info + the
		/// resolved parent content area and layout props.
		LayoutContext _MakeChildCtx(const LayoutContext &parentCtx,
									const FPoint& contentSize,
									const LayoutProps &lp) const noexcept { 
			LayoutContext cc;
			cc.windowSize     = parentCtx.windowSize;
			cc.rootSize       = parentCtx.rootSize;
			cc.rootPadding    = parentCtx.rootPadding;
			cc.rootFontSize   = parentCtx.rootFontSize;
			cc.parentSize     = contentSize;
			cc.parentPadding  = lp.padding;
			cc.parentFontSize = parentCtx.rootFontSize; 
			return cc;
		}

		/// Calcule quelles scrollbars inline doivent être affichées pour un container.
		///
		/// ScrollableX/Y  → toujours affiché (permanent).
		/// AutoScrollableX/Y → affiché seulement si le contenu déborde.
		/// La compensation de l'axe croisé est prise en compte (une barre visible
		/// rétrécit l'autre axe et peut faire apparaître la deuxième barre).
		///
		/// @param w      Widget (BehaviorFlags).
		/// @param lp     LayoutProps (contentW/H, scrollbarThickness).
		/// @param viewW  Largeur intérieure disponible (padding déjà soustrait).
		/// @param viewH  Hauteur intérieure disponible (padding déjà soustrait).
		/// @param showX  [out] vrai si la barre horizontale doit être dessinée.
		/// @param showY  [out] vrai si la barre verticale doit être dessinée.
		static void _ContainerScrollbars(const Widget &w, const LayoutProps &lp,
										 float viewW, float viewH,
										 bool &showX, bool &showY) noexcept { 
			const bool wantX  = Has(w.behavior, BehaviorFlag::ScrollableX | BehaviorFlag::AutoScrollableX);
			const bool wantY  = Has(w.behavior, BehaviorFlag::ScrollableY | BehaviorFlag::AutoScrollableY);
			const bool autoX  = Has(w.behavior, BehaviorFlag::AutoScrollableX)
							 && !Has(w.behavior, BehaviorFlag::ScrollableX);
			const bool autoY  = Has(w.behavior, BehaviorFlag::AutoScrollableY)
							 && !Has(w.behavior, BehaviorFlag::ScrollableY);
			//const float t = lp.scrollbarThickness;

			// Première passe — sans tenir compte de l'axe croisé.
			showX = wantX && (!autoX || lp.contentW > viewW);
			showY = wantY && (!autoY || lp.contentH > viewH);

			// Deuxième passe — une barre visible réduit l'espace de l'autre axe.
			// Pour le mode auto, on garde le même seuil que la première passe (viewW/viewH)
			// afin d'éviter de déclencher une barre croisée quand contentW == viewW.
			if (showY && !showX && wantX)
				showX = !autoX || (lp.contentW > viewW);
			if (showX && !showY && wantY)
				showY = !autoY || (lp.contentH > viewH);
		}

		// ── Text Edition Helpers ──────────────────────────────────────────────────────

		/// Unifie l'accès au curseur entre Content et TextAreaData
		template <typename T>
		int& _GetCursor(T* data) {
			if constexpr (std::is_same_v<T, TextAreaData>) return data->cursorPos;
			else return data->cursor;
		}

		/// Insère du texte en écrasant la sélection éventuelle
		template <typename T>
		void _InsertText(T* data, std::string_view text) {
			if constexpr (std::is_same_v<T, TextAreaData>) {
				data->Insert(text); // TextAreaData s'occupe des Spans
			} else {
				if (data->HasSelection()) data->DeleteSelection();
				data->text.insert(_GetCursor(data), text);
				_GetCursor(data) += (int)text.size();
				data->ClearSelection();
			}
		}

		/// Déplace le curseur et gère la sélection continue (touche Shift)
		template <typename T>
		void _MoveTextCursor(T* data, int newPos, bool shift) {
			int& cursor = _GetCursor(data);
			if (shift) {
				if (!data->HasSelection()) data->selAnchor = cursor;
				cursor = newPos;
				data->selFocus = newPos;
			} else {
				cursor = newPos;
				data->ClearSelection();
			}
		}

		[[nodiscard]] static int _TextWordLeft(const std::string& text, int pos) noexcept {
			if (pos <= 0) return 0; 
			--pos;
			while (pos > 0 && !std::isalnum((unsigned char)text[pos - 1])) --pos;
			while (pos > 0 && std::isalnum((unsigned char)text[pos - 1])) --pos;
			return pos;
		}

		[[nodiscard]] static int _TextWordRight(const std::string& text, int pos) noexcept {
			int sz = (int)text.size();
			while (pos < sz && !std::isalnum((unsigned char)text[pos])) ++pos;
			while (pos < sz && std::isalnum((unsigned char)text[pos])) ++pos;
			return pos;
		}

		/// Gère tous les raccourcis communs (Ctrl+C, Ctrl+V, Flèches, Suppr...)
		template <typename T>
		bool _HandleCommonTextKeys(T* data, SDL::Keycode k, SDL::Keymod mod, Callbacks* cb, bool allowEdit = true) {
			int& cursor = _GetCursor(data);
			const bool ctrl  = (mod & SDL::KMOD_CTRL)  != 0;
			const bool shift = (mod & SDL::KMOD_SHIFT) != 0;

			switch (k) {
				case SDL::KEYCODE_BACKSPACE:
					if (!allowEdit) return true;
					if (data->HasSelection()) {
						data->DeleteSelection();
					} else if (cursor > 0) {
						data->text.erase((size_t)(cursor - 1), 1);
						if constexpr (std::is_same_v<T, TextAreaData>) data->_ShiftSpans(cursor - 1, -1);
						--cursor;
					}
					if (cb && cb->onTextChange) cb->onTextChange(data->text);
					return true;

				case SDL::KEYCODE_DELETE:
					if (!allowEdit) return true;
					if (data->HasSelection()) {
						data->DeleteSelection();
					} else if (cursor < (int)data->text.size()) {
						data->text.erase((size_t)cursor, 1);
						if constexpr (std::is_same_v<T, TextAreaData>) data->_ShiftSpans(cursor, -1);
					}
					if (cb && cb->onTextChange) cb->onTextChange(data->text);
					return true;

				case SDL::KEYCODE_LEFT:
					if (!shift && data->HasSelection()) _MoveTextCursor(data, data->SelMin(), false);
					else if (ctrl) _MoveTextCursor(data, _TextWordLeft(data->text, cursor), shift);
					else _MoveTextCursor(data, SDL::Max(0, cursor - 1), shift);
					return true;

				case SDL::KEYCODE_RIGHT:
					if (!shift && data->HasSelection()) _MoveTextCursor(data, data->SelMax(), false);
					else if (ctrl) _MoveTextCursor(data, _TextWordRight(data->text, cursor), shift);
					else _MoveTextCursor(data, SDL::Min((int)data->text.size(), cursor + 1), shift);
					return true;

				case SDL::KEYCODE_HOME:
					if constexpr (std::is_same_v<T, TextAreaData>) {
						if (ctrl) _MoveTextCursor(data, 0, shift);
						else      _MoveTextCursor(data, data->LineStart(data->LineOf(cursor)), shift);
					} else {
						_MoveTextCursor(data, 0, shift);
					}
					return true;

				case SDL::KEYCODE_END:
					if constexpr (std::is_same_v<T, TextAreaData>) {
						if (ctrl) _MoveTextCursor(data, (int)data->text.size(), shift);
						else      _MoveTextCursor(data, data->LineEnd(data->LineOf(cursor)), shift);
					} else {
						_MoveTextCursor(data, (int)data->text.size(), shift);
					}
					return true;

				case SDL::KEYCODE_A:
					if (ctrl) {
						data->SetSelection(0, (int)data->text.size());
						cursor = (int)data->text.size();
					}
					return true;

				case SDL::KEYCODE_C:
					if (ctrl && data->HasSelection()) {
						try { SDL::SetClipboardText(data->GetSelectedText().c_str()); } catch (...) {}
					}
					return true;

				case SDL::KEYCODE_X:
					if (ctrl && data->HasSelection()) {
						try { SDL::SetClipboardText(data->GetSelectedText().c_str()); } catch (...) {}
						if (allowEdit) {
							data->DeleteSelection();
							if (cb && cb->onTextChange) cb->onTextChange(data->text);
						}
					}
					return true;

				case SDL::KEYCODE_V:
					if (!allowEdit) return true;
					if (ctrl) {
						try {
							if (SDL::HasClipboardText()) {
								auto clip = SDL::GetClipboardText();
								_InsertText(data, static_cast<std::string_view>(clip));
								if (cb && cb->onTextChange) cb->onTextChange(data->text);
							}
						} catch (...) {}
					}
					return true;
			}
			return false;
		}

		// Helper pour fusionner les logiques de Clic et de Drag
		void _ProcessTextClickOrDrag(ECS::EntityId e, bool isDrag) {
			auto *w   = m_ctx.Get<Widget>(e);
			auto *s   = m_ctx.Get<Style>(e);
			auto *cr  = m_ctx.Get<ComputedRect>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			if (!w || !s || !cr || !lp) return;

			float relX = m_mousePos.x - (cr->screen.x + lp->padding.left);
			float relY = m_mousePos.y - (cr->screen.y + lp->padding.top);

			auto applyHit = [&](auto* data, int hitPos) {
				if (isDrag && !data->selectDragging) return;
				_GetCursor(data) = hitPos;
				if (isDrag) data->selFocus = hitPos;
				else { data->SetSelection(hitPos, hitPos); data->selectDragging = true; }
			};

			if (w->type == WidgetType::TextArea) {
				if (auto *ta = m_ctx.Get<TextAreaData>(e)) applyHit(ta, _TextAreaHitPos(ta, relX, relY, e));
			} else if (w->type == WidgetType::Input) {
				if (auto *c = m_ctx.Get<Content>(e)) applyHit(c, _InputHitPos(c, relX, e));
			}
		}

		// ── Multi-click selection ─────────────────────────────────────────────────────

		/// Double-click (count==2) on Input  → select all.
		/// Double-click (count==2) on TextArea → select current line.
		/// Triple-click or more (count>=3) on TextArea → select all text.
		void _HandleMultiClick(ECS::EntityId e, int count) {
			auto *w  = m_ctx.Get<Widget>(e);
			auto *cb = m_ctx.Get<Callbacks>(e);
			if (!w) return;

			if (w->type == WidgetType::Input) {
				if (auto *c = m_ctx.Get<Content>(e)) {
					c->SetSelection(0, (int)c->text.size());
					c->cursor = (int)c->text.size();
					if (cb && cb->onDoubleClick) cb->onDoubleClick();
				}
			} else if (w->type == WidgetType::TextArea) {
				if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
					if (count >= 3) {
						// Select all
						ta->SetSelection(0, (int)ta->text.size());
						ta->cursorPos = (int)ta->text.size();
					} else {
						// Select the line under the cursor
						auto *cr = m_ctx.Get<ComputedRect>(e);
						auto *lp = m_ctx.Get<LayoutProps>(e);
						if (cr && lp) {
							float relY  = m_mousePos.y - (cr->screen.y + lp->padding.top);
							float lineH = _TH(e) + 2.f;
							int   line  = std::clamp((int)((relY + ta->scrollY) / lineH), 0, ta->LineCount() - 1);
							ta->SetSelection(ta->LineStart(line), ta->LineEnd(line));
							ta->cursorPos = ta->LineEnd(line);
						}
					}
					if (cb && cb->onDoubleClick) cb->onDoubleClick();
				}
			}
		}

		// ── Layout ────────────────────────────────────────────────────────────────────

		void _ProcessLayout() {
			if (!m_ctx.IsAlive(m_root)) return; 

			auto *rootLp = m_ctx.Get<LayoutProps>(m_root);
			if (!rootLp) return;

			// Calcul de la taille racine sans écraser la valeur d'origine (Value::Auto)
			float rootW = rootLp->width.IsAuto()
							? m_viewport.w
							: rootLp->width.Resolve({{m_viewport.w, m_viewport.h}, {m_viewport.w, m_viewport.h}, rootLp->padding});
			float rootH = rootLp->height.IsAuto()
							? m_viewport.h
							: rootLp->height.Resolve({{m_viewport.w, m_viewport.h}, {m_viewport.w, m_viewport.h}, rootLp->padding});

			float rootFs = (m_defaultFontSize > 0.f) ? m_defaultFontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;

			LayoutContext rc;
			rc.windowSize     = {m_viewport.w, m_viewport.h};
			rc.rootSize       = {rootW, rootH};
			rc.rootPadding    = rootLp->padding;
			rc.rootFontSize   = rootFs;
			rc.parentSize     = {rootW, rootH};
			rc.parentPadding  = rootLp->padding;
			rc.parentFontSize = rootFs;

			_Measure(m_root, rc);

			auto *rootCr = m_ctx.Get<ComputedRect>(m_root);
			if (rootCr)
				rootCr->screen = {0.f, 0.f, rootW, rootH};

			_Place(m_root);
			_UpdateClips(m_root, m_viewport);
		}

		FPoint _Measure(ECS::EntityId e, const LayoutContext &ctx) {
			if (!m_ctx.IsAlive(e)) 
				return {};
			auto *w  = m_ctx.Get<Widget>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !lp || !cr)
				return {};
			if (!Has(w->behavior, BehaviorFlag::Visible)) {
				cr->measured = {};
				return {};
			}

			// Resolve explicit dimensions (Auto/Grow → 0 ici ; on élargit ci-dessous).
			bool wa = lp->width.IsAuto()  || lp->width.IsGrow();
			bool ha = lp->height.IsAuto() || lp->height.IsGrow();
			float fw = wa ? 0.f : lp->width.Resolve(ctx);
			float fh = ha ? 0.f : lp->height.Resolve(ctx);

			// Espace contenu brut disponible pour les enfants.
			float cW = SDL::Max(0.f, (wa ? ctx.parentSize.x : fw) - lp->padding.left - lp->padding.right);
			float cH = SDL::Max(0.f, (ha ? ctx.parentSize.y : fh) - lp->padding.top  - lp->padding.bottom);

			if (w->type == WidgetType::ListBox) {
				if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
					lp->contentH = (float)lb->items.size() * lb->itemHeight;
					// Largeur max des items pour la scrollbar horizontale
					{
						float maxW = 0.f;
						for (const auto &item : lb->items)
							maxW = SDL::Max(maxW, _TW(item, e));
						lp->contentW = maxW;
					}
				}
			} else if (w->type == WidgetType::TextArea) {
				if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
					float lineH = _TH(e) + 2.f;
					lp->contentH = ta->LineCount() * lineH;

					float maxW = 0.f;
					for (int i = 0; i < ta->LineCount(); ++i) {
						float lw = _TextAreaLineX(ta, i, ta->LineEnd(i) - ta->LineStart(i), e);
						maxW = SDL::Max(maxW, lw);
					}
					lp->contentW = maxW;
				}
			}

			// Pour les containers avec scrollbars automatiques, on doit pré-calculer
			// si les barres seront visibles afin de réserver leur place dans l'espace
			// contenu.  On utilise les données contentW/H du frame précédent (première
			// frame = 0, ce qui est correct car le contenu ne déborde pas encore).
			if (w->type == WidgetType::Container || w->type == WidgetType::ListBox || w->type == WidgetType::TextArea) {
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, cW, cH, showX, showY);
				if (showY) cW = SDL::Max(0.f, cW - lp->scrollbarThickness);
				if (showX) cH = SDL::Max(0.f, cH - lp->scrollbarThickness);
			}

			FPoint intr = _IntrinsicSize(e);

			// Contexte transmis aux enfants.
			LayoutContext cc = _MakeChildCtx(ctx, {cW, cH}, *lp);

			float chW = 0.f, chH = 0.f;
			float curLineW = 0.f, curLineH = 0.f;
			int vis = 0, lineVis = 0;
			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				if (lp->layout == Layout::InColumn || lp->layout == Layout::InLine) {
					// Two-pass: measure non-grow children first to compute the grow
					// budget, then re-measure grow children with the correct axis size
					// in their context so that Ph/Pw in grandchildren resolve correctly.
					// Grow factor: read from child's width (InLine) or height (InColumn).
					const bool isCol = (lp->layout == Layout::InColumn);
					auto growOf = [&](const LayoutProps* cl) -> float {
						return 0.01f * (isCol ? (cl->height.IsGrow() ? cl->height.val : 0.f)
						             : (cl->width.IsGrow() ? cl->width.val  : 0.f));
					};
					std::vector<ECS::EntityId> flow;
					float tFixed = 0.f, tGrow = 0.f;

					for (ECS::EntityId cid : ch->ids) {
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl2 = m_ctx.Get<LayoutProps>(cid);
						if (!cw2 || !cl2 || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
						if (cl2->attach == AttachLayout::Absolute || cl2->attach == AttachLayout::Fixed) {
							_Measure(cid, cc);
							continue;
						}
						flow.push_back(cid);
						float g = growOf(cl2);
						tGrow += g;
						// Count margins for ALL flow children — matches _Place tFixed logic.
						if (isCol) tFixed += cl2->margin.top  + cl2->margin.bottom;
						else       tFixed += cl2->margin.left + cl2->margin.right;
						if (g == 0.f) {
							FPoint csz = _Measure(cid, cc);
							if (isCol) {
								chW = SDL::Max(chW, csz.x + cl2->margin.left + cl2->margin.right);
								tFixed += csz.y;
							} else {
								chH = SDL::Max(chH, csz.y + cl2->margin.top + cl2->margin.bottom);
								tFixed += csz.x;
							}
						}
					}

					int fvis    = (int)flow.size();
					float avail   = isCol ? cH : cW;
					float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, fvis - 1));
					float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

					for (int i = 0; i < fvis; ++i) {
						ECS::EntityId cid = flow[i];
						auto *cl2  = m_ctx.Get<LayoutProps>(cid);
						auto *ccr2 = m_ctx.Get<ComputedRect>(cid);
						float g = growOf(cl2);

						if (g > 0.f) {
							float growSz = SDL::Max(0.f, gUnit * g);
							LayoutContext growCtx = cc;
							if (isCol) growCtx.parentSize.y = growSz;
							else       growCtx.parentSize.x = growSz;
							_Measure(cid, growCtx);
							// Store grow-resolved size so Ph/Pw in children resolve correctly.
							if (isCol) ccr2->measured.y = growSz;
							else       ccr2->measured.x = growSz;
						}

						float mw = ccr2->measured.x + cl2->margin.left + cl2->margin.right;
						float mh = ccr2->measured.y + cl2->margin.top  + cl2->margin.bottom;
						if (isCol) {
							chW  = SDL::Max(chW, mw);
							chH += mh + (i > 0 ? lp->gap : 0.f);
						} else {
							chH  = SDL::Max(chH, mh);
							chW += mw + (i > 0 ? lp->gap : 0.f);
						}
					}
					vis = fvis;

				} else {
					for (ECS::EntityId cid : ch->ids) {
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw  = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						if (!cw || !cl || !Has(cw->behavior, BehaviorFlag::Visible)) continue;

						if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
							_Measure(cid, cc);
							continue;
						}

						FPoint csz = _Measure(cid, cc);
						float mw = csz.x + cl->margin.left + cl->margin.right;
						float mh = csz.y + cl->margin.top  + cl->margin.bottom;

						if (lp->layout == Layout::Stack) {
							float flowW = mw;
							if (lineVis > 0 && cW > 0.f && curLineW + lp->gap + flowW > cW) {
								chW = SDL::Max(chW, curLineW);
								chH += curLineH + lp->gap;
								curLineW = flowW;
								curLineH = mh;
								lineVis = 1;
							} else {
								curLineW += flowW + (lineVis > 0 ? lp->gap : 0.f);
								curLineH = SDL::Max(curLineH, mh);
								lineVis++;
							}
						}
						++vis;
					}
					if (lp->layout == Layout::Stack && lineVis > 0) {
						chW = SDL::Max(chW, curLineW);
						chH += curLineH;
					}
				}

				// ── InGrid: post-loop track computation ──────────────────────────────
				if (lp->layout == Layout::InGrid) {
					auto *gp     = m_ctx.Get<LayoutGridProps>(e);
					int numCols  = gp ? SDL::Max(1, gp->columns) : 2;
					float gap    = lp->gap;

					// Determine effective number of rows (explicit or auto from children)
					int numRows  = gp ? gp->rows : 0;
					if (numRows <= 0) {
						int autoIdx = 0;
						for (ECS::EntityId cid : ch->ids) {
							if (!m_ctx.IsAlive(cid)) continue;
							auto *cw2 = m_ctx.Get<Widget>(cid);
							auto *cl  = m_ctx.Get<LayoutProps>(cid);
							if (!cw2 || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
							if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
							auto *gc  = m_ctx.Get<GridCell>(cid);
							int r = gc ? gc->row : (autoIdx / numCols);
							int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
							numRows = SDL::Max(numRows, r + rs);
							if (!gc) ++autoIdx;
						}
						numRows = SDL::Max(1, numRows);
					}

					GridSizing cSiz = gp ? gp->colSizing : GridSizing::Fixed;
					GridSizing rSiz = gp ? gp->rowSizing : GridSizing::Fixed;
					float baseCellW = SDL::Max(1.f, (cW - gap * (float)(numCols - 1)) / (float)numCols);
					float baseCellH = (!ha && numRows > 0) ? SDL::Max(1.f, (cH - gap * (float)(numRows - 1)) / (float)numRows) : 0.f;

					std::vector<float> colW(numCols, cSiz == GridSizing::Fixed ? baseCellW : 0.f);
					std::vector<float> rowH(numRows, (rSiz == GridSizing::Fixed && baseCellH > 0.f) ? baseCellH : 0.f);

					// Content-based sizing: derive track sizes from already-measured children
					if (cSiz == GridSizing::Content || rSiz == GridSizing::Content || baseCellH == 0.f) {
						int autoIdx = 0;
						for (ECS::EntityId cid : ch->ids) {
							if (!m_ctx.IsAlive(cid)) continue;
							auto *cw2 = m_ctx.Get<Widget>(cid);
							auto *cl  = m_ctx.Get<LayoutProps>(cid);
							auto *gc  = m_ctx.Get<GridCell>(cid);
							auto *ccr = m_ctx.Get<ComputedRect>(cid);
							if (!cw2 || !cl || !ccr || !Has(cw2->behavior, BehaviorFlag::Visible)) { if (!gc) ++autoIdx; continue; }
							if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

							int c  = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : ((autoIdx % numCols));
							int r  = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : ((autoIdx / numCols));
							int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
							int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
							if (!gc) ++autoIdx;

							if (cSiz == GridSizing::Content) {
								float childW = ccr->measured.x + cl->margin.left + cl->margin.right;
								float perCol = SDL::Max(0.f, (childW - gap * (float)(cs - 1)) / (float)cs);
								for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
									colW[ci] = SDL::Max(colW[ci], perCol);
							}
							if (rSiz == GridSizing::Content || baseCellH == 0.f) {
								float childH = ccr->measured.y + cl->margin.top + cl->margin.bottom;
								float perRow = SDL::Max(0.f, (childH - gap * (float)(rs - 1)) / (float)rs);
								for (int ri = r; ri < SDL::Min(r + rs, numRows); ++ri)
									rowH[ri] = SDL::Max(rowH[ri], perRow);
							}
						}
					}

					// Store computed tracks for use in _Place and _DrawContainer
					if (gp) {
						gp->colWidths  = colW;
						gp->rowHeights = rowH;
					}

					// Total grid content size
					chW = 0.f;
					for (int ci = 0; ci < numCols; ++ci)
						chW += colW[ci] + (ci > 0 ? gap : 0.f);
					chH = 0.f;
					for (int ri = 0; ri < numRows; ++ri)
						chH += rowH[ri] + (ri > 0 ? gap : 0.f);
				}
				// ── end InGrid ───────────────────────────────────────────────────────

				// Ne pas écraser le contenu déjà calculé pour ListBox / TextArea
				if (w->type != WidgetType::ListBox && w->type != WidgetType::TextArea) {
					lp->contentW = chW;
					lp->contentH = chH;
				}
			}

			float bW = wa ? SDL::Max(intr.x, chW) + lp->padding.left + lp->padding.right : fw;
			float bH = ha ? SDL::Max(intr.y, chH) + lp->padding.top  + lp->padding.bottom : fh;

			// Apply min/max size constraints (Px(-1) = no constraint).
			// Convention CSS : maxWidth appliqué d'abord, puis minWidth prime sur maxWidth.
			float rMinW = lp->minWidth.Resolve(ctx);
			float rMinH = lp->minHeight.Resolve(ctx);
			float rMaxW = lp->maxWidth.Resolve(ctx);
			float rMaxH = lp->maxHeight.Resolve(ctx);
			if (rMaxW >= 0.f) bW = SDL::Min(bW, rMaxW);
			if (rMaxH >= 0.f) bH = SDL::Min(bH, rMaxH);
			if (rMinW >= 0.f) bW = SDL::Max(bW, rMinW);
			if (rMinH >= 0.f) bH = SDL::Max(bH, rMinH);

			cr->measured = {bW, bH};
			return cr->measured;
		}

		[[nodiscard]] FPoint _IntrinsicSize(ECS::EntityId e) {
			auto *w = m_ctx.Get<Widget>(e);
			if (!w) return {};
			float ch = _TH(e);
			switch (w->type) {
			case WidgetType::Label:
			case WidgetType::Button: {
				auto *c = m_ctx.Get<Content>(e);
				if (!c || c->text.empty())
					return {60.f, ch + 4.f};
				return {_TW(c->text, e), ch + 4.f};
			}
			case WidgetType::Toggle:
				return {80.f, 28.f};
			case WidgetType::RadioButton:
				return {80.f, 24.f};
			case WidgetType::Slider:
				return {80.f, 24.f};
			case WidgetType::ScrollBar:
				return {10.f, 80.f};
			case WidgetType::Input:
				return {80.f, SDL::Max(30.f, ch + 8.f)};
			case WidgetType::TextArea:
				return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
			case WidgetType::ListBox:
				return {160.f, SDL::Max(80.f, ch * 4.f + 8.f)};
			case WidgetType::Graph:
				return {200.f, 120.f};
			case WidgetType::Progress:
				return {80.f, 18.f};
			case WidgetType::Separator:
				return {0.f, 1.f};
			case WidgetType::Knob:
				return {56.f, 56.f};
			default:
				return {};
			}
		}

		void _Place(ECS::EntityId e) {
			if (!m_ctx.IsAlive(e)) return; 

			auto *w  = m_ctx.Get<Widget>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			auto *ch = m_ctx.Get<Children>(e);
			if (!w || !lp || !cr || !ch || ch->ids.empty())
				return;

			const FRect &self = cr->screen;
			float cw  = self.w - lp->padding.left - lp->padding.right;
			float ch2 = self.h - lp->padding.top  - lp->padding.bottom;

			// Réserver la place des scrollbars inline pour les containers.
			if (w->type == WidgetType::Container || w->type == WidgetType::ListBox) {
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, cw, ch2, showX, showY);
				if (showY) cw  = SDL::Max(0.f, cw  - lp->scrollbarThickness);
				if (showX) ch2 = SDL::Max(0.f, ch2 - lp->scrollbarThickness);
			}

			// Point de départ du flux, décalé par le scroll.
			float cx  = self.x + lp->padding.left  - lp->scrollX;
			float cy  = self.y + lp->padding.top   - lp->scrollY;

			// First pass: compute grow budget over flow children only.
			// Placer les éléments Absolute/Fixed en priorité pour les sortir du flux
			for (ECS::EntityId cid : ch->ids) {
				if (!m_ctx.IsAlive(cid)) continue; 
				auto *cw2 = m_ctx.Get<Widget>(cid);
				auto *cl  = m_ctx.Get<LayoutProps>(cid);
				auto *cc  = m_ctx.Get<ComputedRect>(cid);
				if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;

				if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
					float ox = (cl->attach == AttachLayout::Fixed) ? 0.f : self.x;
					float oy = (cl->attach == AttachLayout::Fixed) ? 0.f : self.y;
					
					LayoutContext absCtx;
					absCtx.windowSize     = {m_viewport.w, m_viewport.h};
					absCtx.rootSize       = {m_viewport.w, m_viewport.h};
					absCtx.rootFontSize   = m_defaultFontSize > 0.f ? m_defaultFontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
					absCtx.parentSize     = {self.w, self.h};
					absCtx.parentPadding  = lp->padding;
					absCtx.parentFontSize = absCtx.rootFontSize;

					cc->screen = {ox + cl->absX.Resolve(absCtx), oy + cl->absY.Resolve(absCtx), cc->measured.x, cc->measured.y};
					_Place(cid);
				}
			}

			if (lp->layout == Layout::InColumn || lp->layout == Layout::InLine) {
				const bool isColP = (lp->layout == Layout::InColumn);
				auto growOfP = [&](const LayoutProps* cl) -> float {
					return 0.01f * (isColP ? (cl->height.IsGrow() ? cl->height.val : 0.f)
					              : (cl->width.IsGrow() ? cl->width.val : 0.f));
				};
				std::vector<ECS::EntityId> flowChildren;
				float tFixed = 0.f, tGrow = 0.f;

				// 1. Collecte et détermination de l'espace alloué
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					flowChildren.push_back(cid);
					float g = growOfP(cl);
					tGrow += g;
					if (lp->layout == Layout::InColumn) {
						tFixed += cl->margin.top + cl->margin.bottom;
						if (g == 0.f) tFixed += cc->measured.y;
					} else {
						tFixed += cl->margin.left + cl->margin.right;
						if (g == 0.f) tFixed += cc->measured.x;
					}
				}

				int vis = (int)flowChildren.size();
				float avail   = (lp->layout == Layout::InColumn) ? ch2 : cw;
				float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, vis - 1));
				float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

				// 2. Trouver le dernier élément qui grandit pour fermer la brèche exactement
				int lastGrowIdx = -1;
				for (int i = 0; i < vis; ++i) {
					auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
					if (growOfP(cl) > 0.f) lastGrowIdx = i;
				}

				std::vector<FRect> computed(vis);

				// 3. Pré-calcul de l'axe principal et application de l'étirement (Stretch) sur l'axe croisé
				for (int i = 0; i < vis; ++i) {
					ECS::EntityId cid = flowChildren[i];
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);

					float childW = cc->measured.x;
					float childH = cc->measured.y;
					float g = growOfP(cl);

					if (g > 0.f) {
						if (lp->layout == Layout::InColumn) childH = gUnit * g;
						else                                childW = gUnit * g;
					}
					
					if (lp->layout == Layout::InColumn) {
						if (cl->alignSelfH == Align::Stretch)
							childW = SDL::Max(0.f, cw - cl->margin.left - cl->margin.right);
					} else {
						if (cl->alignSelfV == Align::Stretch)
							childH = SDL::Max(0.f, ch2 - cl->margin.top - cl->margin.bottom);
					}
					computed[i] = {0.f, 0.f, childW, childH};
				}

				float currentX = cx;
				float currentY = cy;
				float rightX = cx + cw;
				float bottomY = cy + ch2;

				// 4. Placement BI-DIRECTIONNEL sur l'axe principal
				if (lastGrowIdx == -1) {
					// Aucun grow : placement standard de gauche à droite / haut en bas
					for (int i = 0; i < vis; ++i) {
						auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
						if (lp->layout == Layout::InColumn) {
							computed[i].y = currentY + cl->margin.top;
							currentY += computed[i].h + cl->margin.top + cl->margin.bottom + lp->gap;
						} else {
							computed[i].x = currentX + cl->margin.left;
							currentX += computed[i].w + cl->margin.left + cl->margin.right + lp->gap;
						}
					}
				} else {
					// Placement depuis la Droite / Bas (éléments APRES le dernier grow)
					for (int i = vis - 1; i > lastGrowIdx; --i) {
						auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
						if (lp->layout == Layout::InColumn) {
							bottomY -= cl->margin.bottom;
							bottomY -= computed[i].h;
							computed[i].y = bottomY;
							bottomY -= (cl->margin.top + lp->gap);
						} else {
							rightX -= cl->margin.right;
							rightX -= computed[i].w;
							computed[i].x = rightX;
							rightX -= (cl->margin.left + lp->gap);
						}
					}
					
					// Placement depuis la Gauche / Haut (éléments AVANT et JUSQU'AU dernier grow)
					for (int i = 0; i <= lastGrowIdx; ++i) {
						auto *cl = m_ctx.Get<LayoutProps>(flowChildren[i]);
						if (lp->layout == Layout::InColumn) {
							computed[i].y = currentY + cl->margin.top;
							
							// Comble exactement le trou restant pour effacer les erreurs de float
							if (i == lastGrowIdx) 
								computed[i].h = SDL::Max(0.f, bottomY - computed[i].y - cl->margin.bottom);
							
							currentY += computed[i].h + cl->margin.top + cl->margin.bottom + lp->gap;
						} else {
							computed[i].x = currentX + cl->margin.left;
							
							// Comble exactement le trou restant
							if (i == lastGrowIdx) 
								computed[i].w = SDL::Max(0.f, rightX - computed[i].x - cl->margin.right);
							
							currentX += computed[i].w + cl->margin.left + cl->margin.right + lp->gap;
						}
					}
				}

				// 5. Application de l'alignement de l'axe secondaire et appel récursif
				for (int i = 0; i < vis; ++i) {
					ECS::EntityId cid = flowChildren[i];
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					
					if (lp->layout == Layout::InColumn) {
						float px = cx + cl->margin.left;
						switch (cl->alignSelfH) {
							case Align::Center:  px = cx + (cw - computed[i].w) * 0.5f; break;
							case Align::End:     px = cx + cw - computed[i].w - cl->margin.right; break;
							default: break;
						}
						computed[i].x = px;
					} else {
						float py = cy + cl->margin.top;
						switch (cl->alignSelfV) {
							case Align::Center:  py = cy + (ch2 - computed[i].h) * 0.5f; break;
							case Align::End:     py = cy + ch2 - computed[i].h - cl->margin.bottom; break;
							default: break;
						}
						computed[i].y = py;
					}
					
					cc->screen = computed[i];
					_Place(cid);
				}
			} else if (lp->layout == Layout::Stack) {
				float startX = cx;
				size_t i = 0;
				while (i < ch->ids.size()) {
					size_t j = i;
					float rowFixedW = 0.f, rowGrow = 0.f, rowMaxH = 0.f;
					int rowItems = 0;

					// Chercher le nombre d'éléments qui tiennent sur cette ligne
					while (j < ch->ids.size()) {
						ECS::EntityId cid = ch->ids[j];
						if (!m_ctx.IsAlive(cid)) { j++; continue; } 
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						auto *cc  = m_ctx.Get<ComputedRect>(cid);
						
						if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) || cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) { 
							j++; continue; 
						}
						
						float itemGrow   = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
						float itemFixedW = cl->margin.left + cl->margin.right + (itemGrow == 0.f ? cc->measured.x : 0.f);
						float itemH = cc->measured.y + cl->margin.top + cl->margin.bottom;

						if (rowItems > 0 && cw > 0.f && rowFixedW + lp->gap + itemFixedW > cw) {
							break; // Wrap ! On passe à la ligne suivante
						}

						rowFixedW += itemFixedW + (rowItems > 0 ? lp->gap : 0.f);
						rowGrow += itemGrow;
						rowMaxH = SDL::Max(rowMaxH, itemH);
						rowItems++;
						j++;
					}

					if (rowItems == 0) { i++; continue; }

					float gBudget = SDL::Max(0.f, cw - rowFixedW);
					float gUnit   = (rowGrow > 0.f) ? gBudget / rowGrow : 0.f;

					// Placer et aligner les éléments sur la ligne trouvée
					bool firstInRow = true;
					for (size_t k = i; k < j; ++k) {
						ECS::EntityId cid = ch->ids[k]; 
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						auto *cc  = m_ctx.Get<ComputedRect>(cid);
						
						if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) || cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

						float childW = cc->measured.x, childH = cc->measured.y;
						float cg = cl->width.IsGrow() ? 0.01f * cl->width.val : 0.f;
						if (cg > 0.f) childW = gUnit * cg;

						if (!firstInRow) cx += lp->gap;
						firstInRow = false;

						float px = cx + cl->margin.left;
						float py = cy + cl->margin.top;

						switch (cl->alignSelfV) {
							case Align::Stretch: childH = SDL::Max(0.f, rowMaxH - cl->margin.top - cl->margin.bottom); [[fallthrough]];
							case Align::Start:   break;
							case Align::Center:  py = cy + (rowMaxH - childH) * 0.5f; break;
							case Align::End:     py = cy + rowMaxH - childH - cl->margin.bottom; break;
						}

						cc->screen = {px, py, childW, childH};
						cx += childW + cl->margin.left + cl->margin.right;
						_Place(cid);
					}

					cx = startX;
					cy += rowMaxH + lp->gap;
					i = j;
				}
			} else if (lp->layout == Layout::InGrid) {
				// ── InGrid placement ────────────────────────────────────────────────
				auto *gp    = m_ctx.Get<LayoutGridProps>(e);
				int numCols = gp ? SDL::Max(1, gp->columns) : 2;

				// Rebuild row count (same logic as _Measure)
				int numRows = gp ? gp->rows : 0;
				if (numRows <= 0) {
					int autoIdx = 0;
					for (ECS::EntityId cid : ch->ids) {
						if (!m_ctx.IsAlive(cid)) continue;
						auto *cw2 = m_ctx.Get<Widget>(cid);
						auto *cl  = m_ctx.Get<LayoutProps>(cid);
						if (!cw2 || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
						if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
						auto *gc  = m_ctx.Get<GridCell>(cid);
						int r  = gc ? gc->row : (autoIdx / numCols);
						int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
						numRows = SDL::Max(numRows, r + rs);
						if (!gc) ++autoIdx;
					}
					numRows = SDL::Max(1, numRows);
				}

				// Retrieve precomputed track sizes (fallback to uniform if not available)
				float gap = lp->gap;
				const std::vector<float> *colWidths  = (gp && (int)gp->colWidths.size()  == numCols) ? &gp->colWidths  : nullptr;
				const std::vector<float> *rowHeights = (gp && (int)gp->rowHeights.size() == numRows) ? &gp->rowHeights : nullptr;

				float uniformColW = SDL::Max(1.f, (cw - gap * (float)(numCols - 1)) / (float)numCols);
				float uniformRowH = SDL::Max(1.f, (ch2 - gap * (float)(numRows - 1)) / (float)numRows);

				// Pre-compute cumulative column X and row Y origins (relative to content origin)
				std::vector<float> colX(numCols), rowY(numRows);
				float accX = 0.f;
				for (int ci = 0; ci < numCols; ++ci) {
					colX[ci] = accX;
					accX += (colWidths ? (*colWidths)[ci] : uniformColW) + gap;
				}
				float accY = 0.f;
				for (int ri = 0; ri < numRows; ++ri) {
					rowY[ri] = accY;
					accY += (rowHeights ? (*rowHeights)[ri] : uniformRowH) + gap;
				}

				// Place each flow child at its grid cell
				int autoIdx = 0;
				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

					auto *gc  = m_ctx.Get<GridCell>(cid);
					int c  = gc ? SDL::Clamp(gc->col, 0, numCols - 1) : (autoIdx % numCols);
					int r  = gc ? SDL::Clamp(gc->row, 0, numRows - 1) : (autoIdx / numCols);
					int cs = gc ? SDL::Max(1, gc->colSpan) : 1;
					int rs = gc ? SDL::Max(1, gc->rowSpan) : 1;
					if (!gc) ++autoIdx;

					// Compute span pixel dimensions
					float cellW = 0.f;
					for (int ci = c; ci < SDL::Min(c + cs, numCols); ++ci)
						cellW += (colWidths ? (*colWidths)[ci] : uniformColW) + (ci > c ? gap : 0.f);
					float cellH = 0.f;
					for (int ri = r; ri < SDL::Min(r + rs, numRows); ++ri)
						cellH += (rowHeights ? (*rowHeights)[ri] : uniformRowH) + (ri > r ? gap : 0.f);

					// Child size: Stretch fills the cell, other alignments keep measured size
					float childW = cc->measured.x, childH = cc->measured.y;
					if (cl->alignSelfH == Align::Stretch)
						childW = SDL::Max(0.f, cellW - cl->margin.left - cl->margin.right);
					if (cl->alignSelfV == Align::Stretch)
						childH = SDL::Max(0.f, cellH - cl->margin.top  - cl->margin.bottom);

					float px = cx + colX[c] + cl->margin.left;
					float py = cy + rowY[r] + cl->margin.top;

					// Alignment inside cell (non-Stretch cases)
					switch (cl->alignSelfH) {
						case Align::Center: px = cx + colX[c] + (cellW - childW) * 0.5f; break;
						case Align::End:    px = cx + colX[c] + cellW - childW - cl->margin.right; break;
						default: break;
					}
					switch (cl->alignSelfV) {
						case Align::Center: py = cy + rowY[r] + (cellH - childH) * 0.5f; break;
						case Align::End:    py = cy + rowY[r] + cellH - childH - cl->margin.bottom; break;
						default: break;
					}

					cc->screen = {px, py, childW, childH};
					_Place(cid);
				}
			}

			if (w->type == WidgetType::Container) {
				float maxContentX = 0.f;
				float maxContentY = 0.f;

				for (ECS::EntityId cid : ch->ids) {
					if (!m_ctx.IsAlive(cid)) continue;
					auto *cw2 = m_ctx.Get<Widget>(cid);
					auto *cc  = m_ctx.Get<ComputedRect>(cid);
					auto *cl  = m_ctx.Get<LayoutProps>(cid);
					if (!cw2 || !cc || !cl || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
					if (cl->attach == AttachLayout::Fixed) continue; // Fixed ne scrolle pas

					// On calcule la position de l'enfant relativement au coin (0,0) du contenu
					// On rajoute lp->scrollX/Y car cr->screen est déjà décalé par l'affichage
					float childRelativeRight  = (cc->screen.x + cc->screen.w + cl->margin.right)  - (self.x + lp->padding.left) + lp->scrollX;
					float childRelativeBottom = (cc->screen.y + cc->screen.h + cl->margin.bottom) - (self.y + lp->padding.top)  + lp->scrollY;

					maxContentX = SDL::Max(maxContentX, childRelativeRight);
					maxContentY = SDL::Max(maxContentY, childRelativeBottom);
				}

				lp->contentW = maxContentX;
				lp->contentH = maxContentY;
			}
		}

		void _UpdateClips(ECS::EntityId e, FRect parentClip) {
			if (!m_ctx.IsAlive(e)) return; 
			
			auto *w   = m_ctx.Get<Widget>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			auto *s   = m_ctx.Get<Style>(e);
			auto *cr  = m_ctx.Get<ComputedRect>(e);
			if (!w || !lp || !cr) return;

			// 1. Calcul du clip de base du widget lui-même
			cr->clip = cr->screen.GetIntersection(parentClip);
			cr->outer_clip = cr->clip.Extend(s->borders);

			FRect childClip = cr->clip; 
			
			// 2. Si c'est un container, la zone allouée aux enfants est plus petite
			if (w->type == WidgetType::Container || w->type == WidgetType::ListBox || w->type == WidgetType::TextArea) {
				float innerW = cr->screen.w - lp->padding.left - lp->padding.right;
				float innerH = cr->screen.h - lp->padding.top  - lp->padding.bottom;
				
				// On vérifie quelles scrollbars sont physiquement présentes
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);

				// On soustrait les paddings ET l'épaisseur des scrollbars
				childClip = cr->screen;
				childClip.x += lp->padding.left;
				childClip.y += lp->padding.top;
				childClip.w = innerW - (showY ? lp->scrollbarThickness : 0.f);
				childClip.h = innerH - (showX ? lp->scrollbarThickness : 0.f);
				
				// On restreint au clip du parent
				childClip = childClip.GetIntersection(parentClip);
			}

			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				for (ECS::EntityId c : ch->ids) {
					_UpdateClips(c, childClip);
				}
			}
		}

		// ── Input ─────────────────────────────────────────────────────────────────────
		
		/// Convertit une position pixel X (relative à la zone de texte) en index de caractère pour Input
		[[nodiscard]] int _InputHitPos(const Content *c, float px, ECS::EntityId e) {
			if (!c || c->text.empty() || px <= 0.f) return 0;

			// Use prefix-width measurement to match exactly how the cursor is drawn
			// (_TW on the full prefix, including kerning between all characters).
			float bestDist = px; // distance from x=0 at i=0
			int   bestIdx  = 0;

			for (int i = 1; i <= (int)c->text.size(); ++i) {
				float xOff = _TW(c->text.substr(0, (size_t)i), e);
				float dist = SDL::Abs(px - xOff);
				if (dist < bestDist) { bestDist = dist; bestIdx = i; }
				if (xOff > px + 40.f) break; // early exit when far past the click
			}
			return bestIdx;
		}

		void _ProcessInput() {
			// ── Scrollbars inline des containers (drag en cours) ──────────────────
			// On traite le drag des thumbs de container AVANT le hit-test normal
			// car la souris peut sortir du thumb pendant le drag.
			_UpdateContainerScrollDrags();

			ECS::EntityId nh = _HitTest(m_root, m_mousePos);

			// Reset all hover flags.
			m_ctx.Each<WidgetState>([](ECS::EntityId, WidgetState &s) {
				s.wasHovered = s.hovered;
				s.hovered    = false;
			});

			// Only mark as hovered if the widget actually supports it.
			if (nh != ECS::NullEntity && m_ctx.IsAlive(nh)) { 
				auto *w = m_ctx.Get<Widget>(nh);
				if (w && Has(w->behavior, BehaviorFlag::Enable) && Has(w->behavior, BehaviorFlag::Hoverable)) {
					m_ctx.Get<WidgetState>(nh)->hovered = true;
				} else {
					nh = ECS::NullEntity;
				}
			}

			// Hover enter / leave callbacks + hover sound.
			if (nh != m_hovered) {
				if (m_hovered != ECS::NullEntity && m_ctx.IsAlive(m_hovered)) { 
					auto *cb = m_ctx.Get<Callbacks>(m_hovered);
					if (cb && cb->onHoverLeave) cb->onHoverLeave();
				}
				if (nh != ECS::NullEntity && m_ctx.IsAlive(nh)) { 
					auto *cb = m_ctx.Get<Callbacks>(nh);
					if (cb && cb->onHoverEnter) cb->onHoverEnter();
					
					auto *s = m_ctx.Get<Style>(nh);
					if (s && !s->hoverSound.empty())
						if (auto sh = _EnsureAudio(s->hoverSound))
							_PlayAudio(sh);
				}
				m_hovered = nh;
			}

			// ── Press ─────────────────────────────────────────────────────────────
			if (m_mousePressed) {
				// Vérifier d'abord si le clic tombe sur un thumb de scrollbar inline.
				// Si oui, on initie le drag et on consomme l'événement.
				if (!_TryBeginContainerScrollDrag()) {
					if (m_hovered == ECS::NullEntity)
						_SetFocus(ECS::NullEntity);

					if (m_hovered != ECS::NullEntity && m_ctx.IsAlive(m_hovered)) {
						auto *pw = m_ctx.Get<Widget>(m_hovered);
						if (pw && Has(pw->behavior, BehaviorFlag::Enable) && Has(pw->behavior, BehaviorFlag::Selectable)) {
							// ── Comptage des clics (double-clic) ─────────────────────────
							if (m_hovered == m_lastClickEntity && m_timeSinceLastClick < 0.4f)
								++m_clickCount;
							else {
								m_clickCount = 1;
								m_lastClickEntity = m_hovered;
							}
							m_timeSinceLastClick = 0.f;

							m_pressed = m_hovered;
							if (auto *st = m_ctx.Get<WidgetState>(m_pressed)) st->pressed = true;

							bool wantFocus = Has(pw->behavior, BehaviorFlag::Focusable);
							_SetFocus(wantFocus ? m_pressed : ECS::NullEntity);

							// Begin drag for interactive controls.
							if (pw->type == WidgetType::Slider) {
								if (auto *sd = m_ctx.Get<SliderData>(m_pressed)) {
									sd->drag        = true;
									sd->dragStartPos = (sd->orientation == Orientation::Horizontal)
														 ? m_mousePos.x : m_mousePos.y;
									sd->dragStartVal = sd->val;
								}
							} else if (pw->type == WidgetType::ScrollBar) {
								if (auto *sb = m_ctx.Get<ScrollBarData>(m_pressed)) {
									sb->drag        = true;
									sb->dragStartPos = (sb->orientation == Orientation::Vertical)
														 ? m_mousePos.y : m_mousePos.x;
									sb->dragStartOff = sb->offset;
								}
							} else if (pw->type == WidgetType::Knob) {
								if (auto *kd = m_ctx.Get<KnobData>(m_pressed)) {
									kd->drag         = true;
									kd->dragStartY   = m_mousePos.y;
									kd->dragStartVal = kd->val;
								}
							} else if (pw->type == WidgetType::TextArea || pw->type == WidgetType::Input) {
								if (m_clickCount >= 2)
									_HandleMultiClick(m_pressed, m_clickCount);
								else
									_ProcessTextClickOrDrag(m_pressed, false);
							}
						}
					}
				}
			}

			// ── Drag ──────────────────────────────────────────────────────────────
			if (m_mouseDown && m_pressed != ECS::NullEntity && m_ctx.IsAlive(m_pressed)) { 
				// TextArea drag-selection and Input drag-cursor-move can continue even if the mouse goes out of the widget bounds, so we check them before the hit-test of the current mouse position.
				{
					auto *pw2 = m_ctx.Get<Widget>(m_pressed);
					if (pw2 && (pw2->type == WidgetType::TextArea || pw2->type == WidgetType::Input)) {
						_ProcessTextClickOrDrag(m_pressed, true); // true = c'est un drag
					}
				}
				auto *pw = m_ctx.Get<Widget>(m_pressed);
				if (pw) {
					if (pw->type == WidgetType::Slider) {
						if (auto *sd = m_ctx.Get<SliderData>(m_pressed); sd && sd->drag) {
							auto *cr = m_ctx.Get<ComputedRect>(m_pressed);
							auto *lp = m_ctx.Get<LayoutProps>(m_pressed);
							if (cr && lp) {
								bool h = (sd->orientation == Orientation::Horizontal);
								float tl = h
									? (cr->screen.w - lp->padding.left - lp->padding.right - 16.f)
									: (cr->screen.h - lp->padding.top  - lp->padding.bottom - 16.f);
								if (tl > 0.f) {
									float cur = h ? m_mousePos.x : m_mousePos.y;
									float dx  = cur - sd->dragStartPos;
									float nv  = SDL::Clamp(
										sd->dragStartVal + dx / tl * (sd->max - sd->min),
										sd->min, sd->max);
									if (nv != sd->val) {
										sd->val = nv;
										auto *cb = m_ctx.Get<Callbacks>(m_pressed);
										if (cb && cb->onChange) cb->onChange(nv);
									}
								}
							}
						}
					} else if (pw->type == WidgetType::ScrollBar) {
						if (auto *sb = m_ctx.Get<ScrollBarData>(m_pressed); sb && sb->drag) {
							bool v    = (sb->orientation == Orientation::Vertical);
							float cur = v ? m_mousePos.y : m_mousePos.x;
							float dx  = cur - sb->dragStartPos;
							float ratio = (sb->viewSize > 0.f && sb->contentSize > sb->viewSize)
											? sb->viewSize / sb->contentSize : 1.f;
							float maxO = SDL::Max(0.f, sb->contentSize - sb->viewSize);
							float doff = (ratio > 0.f) ? dx / ratio : 0.f;
							float noff = SDL::Clamp(sb->dragStartOff + doff, 0.f, maxO);
							if (noff != sb->offset) {
								sb->offset = noff;
								auto *cb = m_ctx.Get<Callbacks>(m_pressed);
								if (cb && cb->onScroll) cb->onScroll(noff);
								if (cb && cb->onChange) cb->onChange(noff);
							}
						}
					} else if (pw->type == WidgetType::Knob) {
						if (auto *kd = m_ctx.Get<KnobData>(m_pressed); kd && kd->drag) {
							float range = kd->max - kd->min;
							if (range <= 0.f) range = 1.f;
							float dragDeltaNorm = (kd->dragStartY - m_mousePos.y) * 0.005f;
							float nv = SDL::Clamp(kd->dragStartVal + dragDeltaNorm * range, kd->min, kd->max);
							if (nv != kd->val) {
								kd->val = nv;
								auto *cb = m_ctx.Get<Callbacks>(m_pressed);
								if (cb && cb->onChange) cb->onChange(nv);
							}
						}
					}
				}
			}

			// ── Release ───────────────────────────────────────────────────────────
			if (m_mouseReleased) {
				// Relâcher les drags de scrollbars inline.
				_EndContainerScrollDrags();

				if (m_pressed != ECS::NullEntity && m_ctx.IsAlive(m_pressed)) { 
					if (auto *st = m_ctx.Get<WidgetState>(m_pressed))
						st->pressed = false;
					if (m_pressed == m_hovered) {
						auto *pw = m_ctx.Get<Widget>(m_pressed);
						if (pw && Has(pw->behavior, BehaviorFlag::Enable)
							   && Has(pw->behavior, BehaviorFlag::Selectable))
							_OnClick(m_pressed, *pw);
					}
					if (auto *sd = m_ctx.Get<SliderData>(m_pressed))    sd->drag = false;
					if (auto *sb = m_ctx.Get<ScrollBarData>(m_pressed)) sb->drag = false;
					if (auto *kd = m_ctx.Get<KnobData>(m_pressed))      kd->drag = false;
					if (auto *ta = m_ctx.Get<TextAreaData>(m_pressed))  ta->selectDragging = false;
					m_pressed = ECS::NullEntity;
				}
				if (auto *c = m_ctx.Get<Content>(m_pressed)) {
					c->selectDragging = false;
				}
			}
		}

		// ── Helpers : drag des scrollbars inline de containers ────────────────────

		/// Parcourt tous les containers avec ContainerScrollState et met à jour
		/// scrollX/Y en fonction du déplacement de la souris (si drag en cours).
		void _UpdateContainerScrollDrags() {
			if (!m_mouseDown) return;
			m_ctx.Each<ContainerScrollState>([this](ECS::EntityId e, ContainerScrollState &css) {
				auto *lp = m_ctx.Get<LayoutProps>(e);
				auto *cr = m_ctx.Get<ComputedRect>(e);
				auto *w  = m_ctx.Get<Widget>(e);
				if (!lp || !cr || !w) return;

				const float innerW = cr->screen.w - lp->padding.left - lp->padding.right;
				const float innerH = cr->screen.h - lp->padding.top  - lp->padding.bottom;
				bool showX = false, showY = false;
				_ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);
				const float viewW = showY ? SDL::Max(0.f, innerW - lp->scrollbarThickness) : innerW;
				const float viewH = showX ? SDL::Max(0.f, innerH - lp->scrollbarThickness) : innerH;

				if (css.dragY && showY && lp->contentH > viewH) {
					float barH   = viewH;
					float ratio  = SDL::Clamp(viewH / lp->contentH, 0.05f, 1.f);
					float thumbH = SDL::Max(lp->scrollbarThickness * 2.f, barH * ratio);
					float travel = barH - thumbH;
					float dx     = m_mousePos.y - css.dragStartY_;
					float maxOff = lp->contentH - viewH;
					float newOff = (travel > 0.f)
						? SDL::Clamp(css.dragStartOffY + dx / travel * maxOff, 0.f, maxOff)
						: 0.f;
					lp->scrollY = newOff;
					auto *cb = m_ctx.Get<Callbacks>(e);
					if (cb && cb->onScroll) cb->onScroll(newOff);
				}
				if (css.dragX && showX && lp->contentW > viewW) {
					float barW   = viewW;
					float ratio  = SDL::Clamp(viewW / lp->contentW, 0.05f, 1.f);
					float thumbW = SDL::Max(lp->scrollbarThickness * 2.f, barW * ratio);
					float travel = barW - thumbW;
					float dx     = m_mousePos.x - css.dragStartX;
					float maxOff = lp->contentW - viewW;
					float newOff = (travel > 0.f)
						? SDL::Clamp(css.dragStartOff + dx / travel * maxOff, 0.f, maxOff)
						: 0.f;
					lp->scrollX = newOff;
					auto *cb = m_ctx.Get<Callbacks>(e);
					if (cb && cb->onScroll) cb->onScroll(newOff);
				}
			});
		}

		/// Teste si le clic courant tombe sur le thumb d'une scrollbar inline ;
		/// si oui, initialise le drag et retourne true (l'événement est consommé).
		bool _TryBeginContainerScrollDrag() {
			bool consumed = false;
			m_ctx.Each<ContainerScrollState>([this, &consumed](ECS::EntityId e, ContainerScrollState &css) {
				if (consumed) return;
				auto *lp = m_ctx.Get<LayoutProps>(e);
				if (!lp) return;

				if (css.thumbY.w > 0.f && css.thumbY.h > 0.f && _Contains(css.thumbY, m_mousePos)) {
					css.dragY        = true;
					css.dragStartY_  = m_mousePos.y;
					css.dragStartOffY = lp->scrollY;
					consumed = true;
					return;
				}
				if (css.thumbX.w > 0.f && css.thumbX.h > 0.f && _Contains(css.thumbX, m_mousePos)) {
					css.dragX       = true;
					css.dragStartX  = m_mousePos.x;
					css.dragStartOff = lp->scrollX;
					consumed = true;
				}
			});
			return consumed;
		}

		/// Relâche tous les drags de scrollbars inline.
		void _EndContainerScrollDrags() {
			m_ctx.Each<ContainerScrollState>([](ECS::EntityId, ContainerScrollState &css) {
				css.dragX = false;
				css.dragY = false;
			});
		}

		void _OnClick(ECS::EntityId e, const Widget &w) {
			auto *s = m_ctx.Get<Style>(e);
			if (s && !s->clickSound.empty())
				if (auto sh = _EnsureAudio(s->clickSound))
					_PlayAudio(sh);
			auto *cb = m_ctx.Get<Callbacks>(e);
			switch (w.type) {
				case WidgetType::Toggle:
					if (auto *t = m_ctx.Get<ToggleData>(e)) {
						t->checked = !t->checked;
						if (cb && cb->onToggle)
							cb->onToggle(t->checked);
						if (cb && cb->onClick)
							cb->onClick();
					}
					break;
				case WidgetType::RadioButton:
					if (auto *r = m_ctx.Get<RadioData>(e); r && !r->checked) {
						m_ctx.Each<RadioData>([&](ECS::EntityId eid, RadioData &rd) { if(rd.group==r->group) rd.checked=(eid==e); });
						if (cb && cb->onToggle)
							cb->onToggle(true);
						if (cb && cb->onClick)
							cb->onClick();
					}
					break;
				case WidgetType::ListBox: {
					if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
						auto *cr2 = m_ctx.Get<ComputedRect>(e);
						auto *lp2 = m_ctx.Get<LayoutProps>(e);
						if (cr2 && lp2) {
							float iy  = cr2->screen.y + lp2->padding.top;
							int   idx = (int)((m_mousePos.y - iy + lp2->scrollY) / lb->itemHeight);
							if (idx >= 0 && idx < (int)lb->items.size()) {
								lb->selectedIndex = idx;
								if (cb && cb->onChange) cb->onChange((float)idx);
								if (cb && cb->onClick)  cb->onClick();
							}
						}
					}
					break;
				}
				default:
					if (cb && cb->onClick)
						cb->onClick();
					break;
			}
			// Fire onDoubleClick on every widget type when click count >= 2.
			if (m_clickCount >= 2 && cb && cb->onDoubleClick)
				cb->onDoubleClick();
		}

		void _SetFocus(ECS::EntityId nf) {
			if (nf == m_focused)
				return;
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) { 
				if (auto *st = m_ctx.Get<WidgetState>(m_focused))
					st->focused = false;
				auto *cb = m_ctx.Get<Callbacks>(m_focused);
				if (cb && cb->onFocusLose)
					cb->onFocusLose();
			}
			m_focused = nf; 
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
				if (auto *st = m_ctx.Get<WidgetState>(m_focused))
					st->focused = true;
				auto *cb = m_ctx.Get<Callbacks>(m_focused);
				if (cb && cb->onFocusGain)
					cb->onFocusGain();
			}
		}

		[[nodiscard]] ECS::EntityId _HitTest(ECS::EntityId e, FPoint p) const {
			if (!m_ctx.IsAlive(e)) 
				return ECS::NullEntity;
			auto *w = m_ctx.Get<Widget>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !cr || !Has(w->behavior, BehaviorFlag::Visible))
				return ECS::NullEntity;
			if (!_Contains(cr->clip, p))
				return ECS::NullEntity;
			auto *ch = m_ctx.Get<Children>(e);
			if (ch)
				for (int i = (int)ch->ids.size() - 1; i >= 0; --i) {
					ECS::EntityId h = _HitTest(ch->ids[i], p);
					if (h != ECS::NullEntity)
						return h;
				}
			return _Contains(cr->screen, p) ? e : ECS::NullEntity;
		}
		[[nodiscard]] static bool _Contains(const FRect &r, FPoint p) noexcept {
			return p.x >= r.x && p.x < r.x + r.w && p.y >= r.y && p.y < r.y + r.h;
		}

		void _HandleTextInput(const char *txt) {
			if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return; 
			auto *w = m_ctx.Get<Widget>(m_focused);
			if (!w || !Has(w->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) return;

			auto processInput = [&]<typename T>(T* data) {
				if (data) {
					_InsertText(data, std::string_view(txt));
					if (auto *cb = m_ctx.Get<Callbacks>(m_focused); cb && cb->onTextChange) 
						cb->onTextChange(data->text);
				}
			};

			if (w->type == WidgetType::Input) processInput(m_ctx.Get<Content>(m_focused));
			else if (w->type == WidgetType::TextArea) {
				auto* ta = m_ctx.Get<TextAreaData>(m_focused);
				if (ta && !ta->readOnly) processInput(ta);
			}
		}

		void _HandleKeyDownInput(SDL::Keycode k, SDL::Keymod mod) {
			auto *c = m_ctx.Get<Content>(m_focused);
			if (!c) return;
			
			if (k == SDL::KEYCODE_ESCAPE) { _SetFocus(ECS::NullEntity); return; }

			// Laisse le Helper s'occuper de tout
			_HandleCommonTextKeys(c, k, mod, m_ctx.Get<Callbacks>(m_focused));
		}

		void _HandleKeyDownTextArea(SDL::Keycode k, SDL::Keymod mod) {
			auto *ta = m_ctx.Get<TextAreaData>(m_focused);
			auto *cb = m_ctx.Get<Callbacks>(m_focused);
			if (!ta) return;

			if (k == SDL::KEYCODE_ESCAPE) { _SetFocus(ECS::NullEntity); return; }

			const bool editable = !ta->readOnly;

			// Si le Helper a géré une touche standard (Copier, Flèches, Suppr...), on s'arrête
			if (_HandleCommonTextKeys(ta, k, mod, cb, editable)) return;

			// Sinon, on traite les spécificités multilignes du TextArea
			const bool ctrl  = (mod & SDL::KMOD_CTRL)  != 0;
			const bool shift = (mod & SDL::KMOD_SHIFT) != 0;

			switch (k) { 
				case SDL::KEYCODE_TAB:
					if (ctrl) break; // Laisse passer pour basculer le focus du widget
					if (!editable) return;
					if (shift) {
						int ls = ta->LineStart(ta->LineOf(ta->cursorPos));
						int spaces = 0;
						while (ls + spaces < (int)ta->text.size() && ta->text[ls + spaces] == ' ' && spaces < ta->tabSize) ++spaces;
						if (spaces > 0) {
							ta->text.erase(ls, spaces);
							ta->_ShiftSpans(ls, -spaces);
							ta->cursorPos = std::max(ls, ta->cursorPos - spaces);
							ta->ClearSelection();
							if (cb && cb->onTextChange) cb->onTextChange(ta->text);
						}
					} else {
						_InsertText(ta, "\t");
						if (cb && cb->onTextChange) cb->onTextChange(ta->text);
					}
					return;

				case SDL::KEYCODE_RETURN:
				case SDL::KEYCODE_RETURN2:
				case SDL::KEYCODE_KP_ENTER:
					if (!editable) return;
					_InsertText(ta, "\n");
					if (cb && cb->onTextChange) cb->onTextChange(ta->text);
					return;

				case SDL::KEYCODE_UP: {
					int line = ta->LineOf(ta->cursorPos);
					if (line > 0) {
						int col = ta->ColOf(ta->cursorPos);
						int ns = ta->LineStart(line - 1);
						int ne = ta->LineEnd(line - 1);
						_MoveTextCursor(ta, std::min(ns + col, ne), shift);
					}
					return;
				}
				case SDL::KEYCODE_DOWN: {
					int line = ta->LineOf(ta->cursorPos);
					if (line < ta->LineCount() - 1) {
						int col = ta->ColOf(ta->cursorPos);
						int ns = ta->LineStart(line + 1);
						int ne = ta->LineEnd(line + 1);
						_MoveTextCursor(ta, std::min(ns + col, ne), shift);
					}
					return;
				}

				case SDL::KEYCODE_PAGEUP: {
					float lineH = _TH(m_focused) + 2.f;
					int linesPerPage = SDL::Max(1, (int)(_TextAreaViewH() / lineH));
					int line = SDL::Max(0, ta->LineOf(ta->cursorPos) - linesPerPage);
					_MoveTextCursor(ta, ta->LineStart(line), shift);
					return;
				}
				case SDL::KEYCODE_PAGEDOWN: {
					float lineH = _TH(m_focused) + 2.f;
					int linesPerPage = SDL::Max(1, (int)(_TextAreaViewH() / lineH));
					int line = SDL::Min(ta->LineCount() - 1, ta->LineOf(ta->cursorPos) + linesPerPage);
					_MoveTextCursor(ta, ta->LineStart(line), shift);
					return;
				}
				case SDL::KEYCODE_Z:
					if (ctrl && !shift) ta->ClearSelection(); // Undo not implemented
					return;

				default: break;
			}
		}

		// TextArea helpers ──────────────────────────────────────────────────────────

		/// View height of the focused TextArea (0 if not a TextArea).
		[[nodiscard]] float _TextAreaViewH() const noexcept {
			if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return 0.f; 
			auto *cr = m_ctx.Get<ComputedRect>(m_focused);
			auto *lp = m_ctx.Get<LayoutProps>(m_focused);
			if (!cr || !lp) return 0.f;
			return SDL::Max(0.f, cr->screen.h - lp->padding.top - lp->padding.bottom);
		}

		/// Pixel X offset for a column within a line (tabs expanded).
		[[nodiscard]] float _TextAreaLineX(const TextAreaData *ta, int line, int col, ECS::EntityId e) {
			int lineStart = ta->LineStart(line);
			col = std::clamp(col, 0, ta->LineEnd(line) - lineStart);

			// Measure space width for tab expansion (matches the renderer).
			auto rf = _ResolveFont(e);
			float charW = (rf.size > 0.f) ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#if UI_HAS_TTF
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f) {
				if (auto font = _EnsureFont(rf.key, rf.size)) {
					int sw = 0, sh = 0;
					font.GetStringSize(" ", &sw, &sh);
					charW = (float)sw;
				}
			}
#endif
			// Accumulate non-tab characters into runs and measure each run with _TW,
			// exactly as the renderer does — this includes inter-character kerning.
			float x = 0.f;
			int   colCount = 0;
			std::string run;
			for (int i = 0; i < col; ++i) {
				char ch = ta->text[lineStart + i];
				if (ch == '\t') {
					if (!run.empty()) { x += _TW(run, e); run.clear(); }
					int spaces = ta->tabSize - (colCount % ta->tabSize);
					if (spaces == 0) spaces = ta->tabSize;
					x += spaces * charW;
					colCount += spaces;
				} else {
					run += ch;
					++colCount;
				}
			}
			if (!run.empty()) x += _TW(run, e);
			return x;
		}

		/// Convert a pixel position (relative to content area origin) to a document offset.
		[[nodiscard]] int _TextAreaHitPos(const TextAreaData *ta, float px, float py, ECS::EntityId e) {
			float lineH = _TH(e) + 2.f;
			float pyDoc = py + ta->scrollY;
			int line = std::clamp((int)(pyDoc / lineH), 0, ta->LineCount() - 1);
			int lineStart = ta->LineStart(line);
			int lineEnd   = ta->LineEnd(line);

			auto rf = _ResolveFont(e);
			float charW = (rf.size > 0.f) ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
			int best = lineStart;
			float bestDist = SDL::Abs(px);
			for (int i = lineStart; i <= lineEnd; ++i) {
				float xOff = _TextAreaLineX(ta, line, i - lineStart, e);
				float dist = SDL::Abs(px - xOff);
				if (dist < bestDist) { bestDist = dist; best = i; }
				if (xOff > px + charW * 3.f) break;
			}
			return best;
		}

		/// Scroll the TextArea so the cursor is visible.
		void _TextAreaScrollToCursor(TextAreaData *ta, const ComputedRect *cr, const LayoutProps *lp, ECS::EntityId e) {
			if (!ta || !cr || !lp) return;
			float lineH = _TH(e) + 2.f;
			float viewH = cr->screen.h - lp->padding.top - lp->padding.bottom;
			int   curLine = ta->LineOf(ta->cursorPos);
			float curY    = curLine * lineH;
			if (curY < ta->scrollY)
				ta->scrollY = curY;
			else if (curY + lineH > ta->scrollY + viewH)
				ta->scrollY = curY + lineH - viewH;
			float maxScroll = SDL::Max(0.f, ta->LineCount() * lineH - viewH);
			ta->scrollY = SDL::Clamp(ta->scrollY, 0.f, maxScroll);
		}

		void _HandleKeyDown(SDL::Keycode k, SDL::Keymod mod) {
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) { 
				auto *fw = m_ctx.Get<Widget>(m_focused);
				if (fw) {
					if (fw->type == WidgetType::TextArea) {
						if (Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
							_HandleKeyDownTextArea(k, mod);
							return;
						}   
					} else if (fw->type == WidgetType::Input) {
						if (Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
							_HandleKeyDownInput(k, mod);
							return;
						}
					} else if (fw->type == WidgetType::ListBox) {
						if (Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
							_HandleKeyDownListBox(k);
							return;
						}
					}
				}
			}

			if (k == SDL::KEYCODE_TAB) {
				bool shiftPressed = (mod & SDL::KMOD_SHIFT);
				_CycleFocus(shiftPressed);
				return;
			}
			if (m_focused == ECS::NullEntity || !m_ctx.IsAlive(m_focused)) return; 

			auto *w = m_ctx.Get<Widget>(m_focused);
			if (!w || w->type != WidgetType::Input || !Has(w->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) return;

			auto *c = m_ctx.Get<Content>(m_focused);
			auto *cb = m_ctx.Get<Callbacks>(m_focused);
			if (!c) return;

			switch (k) { 
				case SDL::KEYCODE_BACKSPACE:
					if (c->cursor > 0) {
						c->text.erase((size_t)(c->cursor - 1), 1);
						--c->cursor;
						if (cb && cb->onTextChange) cb->onTextChange(c->text);
					}
					break;
				case SDL::KEYCODE_DELETE:
					if (c->cursor < (int)c->text.size()) {
						c->text.erase((size_t)c->cursor, 1);
						if (cb && cb->onTextChange) cb->onTextChange(c->text);
					}
					break;
				case SDL::KEYCODE_LEFT:
					c->cursor = SDL::Max(0, c->cursor - 1);
					break;
				case SDL::KEYCODE_RIGHT:
					c->cursor = SDL::Min((int)c->text.size(), c->cursor + 1);
					break;
				case SDL::KEYCODE_HOME:
					c->cursor = 0;
					break;
				case SDL::KEYCODE_END:
					c->cursor = (int)c->text.size();
					break;
				case SDL::KEYCODE_ESCAPE:
					_SetFocus(ECS::NullEntity);
					break;
				default:
					break;
			}
		}

		void _HandleScroll(float dx, float dy) {
			ECS::EntityId e = _HitTest(m_root, m_mousePos);
			while (e != ECS::NullEntity && m_ctx.IsAlive(e)) {
				auto *w  = m_ctx.Get<Widget>(e);
				auto *lp = m_ctx.Get<LayoutProps>(e);

				// ListBox has its own internal scroll.
				if (w && w->type == WidgetType::ListBox) {
					bool consumed = false;
					if (auto *lb = m_ctx.Get<ListBoxData>(e)) {
						auto *cr2 = m_ctx.Get<ComputedRect>(e);
						auto *lp2 = m_ctx.Get<LayoutProps>(e);
						if (cr2 && lp2 && dy != 0.f) {
							float prev   = lp2->scrollY;
							float viewH  = cr2->screen.h - lp2->padding.top - lp2->padding.bottom;
							float total  = (float)lb->items.size() * lb->itemHeight;
							float maxOff = SDL::Max(0.f, total - viewH);
							lp2->scrollY = SDL::Clamp(lp2->scrollY - dy * lb->itemHeight * 2.f, 0.f, maxOff);
							consumed = (lp2->scrollY != prev);
						}
					}
					if (consumed || !w->dispatchEvent) return;
					// Not consumed — bubble to parent.
					auto *par = m_ctx.Get<Parent>(e);
					e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
					continue;
				}
				// TextArea has its own internal scroll.
				if (w && w->type == WidgetType::TextArea) {
					bool consumed = false;
					if (auto *ta = m_ctx.Get<TextAreaData>(e)) {
						auto *cr = m_ctx.Get<ComputedRect>(e);
						if (cr && dy != 0.f) {
							float prev   = ta->scrollY;
							float lineH  = _TH(e) + 2.f;
							float viewH  = cr->screen.h - (lp ? lp->padding.top + lp->padding.bottom : 0.f);
							float maxS   = SDL::Max(0.f, ta->LineCount() * lineH - viewH);
							ta->scrollY  = SDL::Clamp(ta->scrollY - dy * lineH * 3.f, 0.f, maxS);
							consumed = (ta->scrollY != prev);
						}
					}
					if (consumed || !w->dispatchEvent) return;
					// Not consumed — bubble to parent.
					auto *par = m_ctx.Get<Parent>(e);
					e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
					continue;
				}
				if (!w || !lp) break;

				// ScrollableX/Y = scroll permanent ; AutoScrollableX/Y = scroll si débordement.
				bool scrollableV = Has(w->behavior, BehaviorFlag::ScrollableY | BehaviorFlag::AutoScrollableY);
				bool scrollableH = Has(w->behavior, BehaviorFlag::ScrollableX | BehaviorFlag::AutoScrollableX);

				if (scrollableV || scrollableH) {
					auto *cr = m_ctx.Get<ComputedRect>(e);
					const float innerW = cr ? (cr->screen.w - lp->padding.left - lp->padding.right) : 0.f;
					const float innerH = cr ? (cr->screen.h - lp->padding.top  - lp->padding.bottom) : 0.f;

					bool showX = false, showY = false;
					_ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);
					const float viewW = showY ? SDL::Max(0.f, innerW - lp->scrollbarThickness) : innerW;
					const float viewH = showX ? SDL::Max(0.f, innerH - lp->scrollbarThickness) : innerH;

					float lastScrollX = lp->scrollX;
					float lastScrollY = lp->scrollY;

					if (scrollableH && dx != 0.f) {
						float mx    = SDL::Max(0.f, lp->contentW - viewW);
						lp->scrollX = SDL::Clamp(lp->scrollX - dx * 20.f, 0.f, mx);
					}
					if (scrollableV && dy != 0.f) {
						float mx    = SDL::Max(0.f, lp->contentH - viewH);
						lp->scrollY = SDL::Clamp(lp->scrollY - dy * 20.f, 0.f, mx);
					}

					bool consumed = (lastScrollX != lp->scrollX || lastScrollY != lp->scrollY);
					if (consumed) {
						auto *s = m_ctx.Get<Style>(e);
						if (s && !s->scrollSound.empty())
							if (auto sh = _EnsureAudio(s->scrollSound))
								_PlayAudio(sh);
					}
					if (consumed || !w->dispatchEvent) return;
					// Not consumed and dispatchEvent=true — bubble to parent.
				}
				auto *par = m_ctx.Get<Parent>(e);
				e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
			}
		}

		void _CanvasEvent(const SDL::Event& evt) {
			// Forward SDL events to every visible, enabled Canvas widget whose
			// clip rect contains the mouse cursor OR that currently holds focus.
			// This allows the game canvas to receive keyboard events even when
			// the mouse is elsewhere (e.g. paused panels on top).
			m_ctx.Each<CanvasData, Widget, ComputedRect>(
				[&](ECS::EntityId e, CanvasData& cd, Widget& w, ComputedRect& cr) {
					if (!Has(w.behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;
					if (!cd.eventCb) return;
					const bool mouseInside = _Contains(cr.clip, m_mousePos);
					const bool hasFocus    = (m_focused == e);
					if (mouseInside || hasFocus)
						cd.eventCb(const_cast<SDL::Event&>(evt));
				});
		}

		bool IsEffectivelyVisible(ECS::EntityId e, ECS::Context& ecs_context) {
			while (e != ECS::NullEntity) { 
				auto* w = ecs_context.Get<Widget>(e);
				if (!w || !Has(w->behavior, BehaviorFlag::Visible)) return false;
				
				auto* p = ecs_context.Get<Parent>(e);
				e = p ? p->id : ECS::NullEntity;
			}
			return true;
		}

		void _CollectFocusables(ECS::EntityId e, std::vector<ECS::EntityId> &out) const {
			auto *w = m_ctx.Get<Widget>(e);
			if (!w || !Has(w->behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;

			if (Has(w->behavior, BehaviorFlag::Focusable)) {
				// Only add if the widget is actually on-screen (clip rect has positive area).
				auto *cr = m_ctx.Get<ComputedRect>(e);
				if (cr && cr->clip.w > 0.f && cr->clip.h > 0.f)
					out.push_back(e);
			}

			if (auto *ch = m_ctx.Get<Children>(e)) {
				for (ECS::EntityId c : ch->ids)
					_CollectFocusables(c, out);
			}
		}

		void _CycleFocus(bool reverse = false) {
			std::vector<ECS::EntityId> foc; 
			_CollectFocusables(m_root, foc);
			if (foc.empty()) return;

			auto it = std::ranges::find(foc, m_focused);
			ECS::EntityId nextFocus;

			if (it == foc.end()) {
				nextFocus = reverse ? foc.back() : foc.front();
			} else {
				if (reverse) {
					nextFocus = (it == foc.begin()) ? foc.back() : *std::prev(it);
				} else {
					nextFocus = (std::next(it) == foc.end()) ? foc.front() : *std::next(it);
				}
			}

			_SetFocus(nextFocus);
		}

		// ── Canvas update ─────────────────────────────────────────────────

		void _ProcessCanvasUpdate(float dt) {
			// Call updateCb for every visible, enabled Canvas widget.
			// Layout hasn't run yet this frame, so avoid depending on ComputedRect here.
			m_ctx.Each<CanvasData, Widget>([dt](ECS::EntityId, CanvasData& cd, Widget& w) {
				if (!Has(w.behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;
				if (cd.updateCb) cd.updateCb(dt);
			});
		}

		// ── Animate ────────────────────────────────────────────────────────

		void _ProcessAnimate(float dt) {
			if (dt <= 0.f)
				return;
				
			m_ctx.Each<ToggleData>([dt](ECS::EntityId, ToggleData &t) { 
				float target = t.checked ? 1.f : 0.f;
				t.animT += (target - t.animT) * SDL::Min(1.f, dt * 12.f); 
				t.animT = SDL::Clamp(t.animT, 0.f, 1.f);
			});
			
			if (m_focused != ECS::NullEntity && m_ctx.IsAlive(m_focused)) {
				if (auto *c = m_ctx.Get<Content>(m_focused)) {
					c->blinkTimer += dt;
					if (c->blinkTimer > 1.f) c->blinkTimer -= 1.f;
				} 
				if (auto *ta = m_ctx.Get<TextAreaData>(m_focused)) {
					ta->blinkTimer += dt;
					if (ta->blinkTimer > 1.f) ta->blinkTimer -= 1.f;
					// Keep cursor visible after scroll
					auto *cr2 = m_ctx.Get<ComputedRect>(m_focused);
					auto *lp2 = m_ctx.Get<LayoutProps>(m_focused);
					_TextAreaScrollToCursor(ta, cr2, lp2, m_focused);
				}
			}
		}

		// ── Tooltip ────────────────────────────────────────────────────────

		void _ProcessTooltip(float dt) {
			// Hovered entity changed → reset timer
			if (m_hovered != m_tooltipTarget) {
				m_tooltipTarget  = m_hovered;
				m_tooltipTimer   = 0.f;
				m_tooltipVisible = false;
			}
			if (m_tooltipTarget == ECS::NullEntity || !m_ctx.IsAlive(m_tooltipTarget)) {
				m_tooltipVisible = false;
				return;
			}
			auto *td = m_ctx.Get<TooltipData>(m_tooltipTarget);
			if (!td || td->text.empty()) {
				m_tooltipVisible = false;
				return;
			}
			m_tooltipTimer  += dt;
			m_tooltipVisible = (m_tooltipTimer >= td->delay);
		}

		void _DrawTooltip() {
			if (!m_tooltipVisible || m_tooltipTarget == ECS::NullEntity) return;
			auto *td = m_ctx.Get<TooltipData>(m_tooltipTarget);
			if (!td || td->text.empty()) return;

			// Couleurs et police depuis le widget survolé
			auto *hs = m_ctx.Get<Style>(m_tooltipTarget);
			static const Style kDef{};
			const Style &s = hs ? *hs : kDef;

			// Entité dédiée au cache de texte TTF (ne participe pas au layout)
			if (m_tooltipEntity == ECS::NullEntity || !m_ctx.IsAlive(m_tooltipEntity)) {
				m_tooltipEntity = m_ctx.CreateEntity();
				m_ctx.Add<Style>(m_tooltipEntity);
			}
			// Resolve the font from the hovered widget and pin it as Self on the
			// tooltip entity so _TH/_TW don't walk an invalid parent chain.
			auto &ts = *m_ctx.Get<Style>(m_tooltipEntity);
			{
				auto rf = _ResolveFont(m_tooltipTarget);
				ts.fontKey  = rf.key;
				ts.fontSize = rf.size;
				ts.usedFont = rf.isDebug ? FontType::Debug
				            : (rf.key.empty() ? FontType::Default : FontType::Self);
			}

			constexpr float kPadH = 8.f, kPadV = 5.f;
			const float tw = _TW(td->text, m_tooltipEntity);
			const float th = _TH(m_tooltipEntity);
			const float bw = tw + kPadH * 2.f + 2.f;
			const float bh = th + kPadV * 2.f;

			// Position : sous le curseur, basculée si proche du bord
			float x = m_mousePos.x + 14.f;
			float y = m_mousePos.y + 22.f;
			if (x + bw > m_viewport.w - 4.f) x = m_mousePos.x - bw - 6.f;
			if (y + bh > m_viewport.h - 4.f) y = m_mousePos.y - bh - 6.f;
			x = SDL::Clamp(x, 4.f, SDL::Max(4.f, m_viewport.w - bw - 4.f));
			y = SDL::Clamp(y, 4.f, SDL::Max(4.f, m_viewport.h - bh - 4.f));

			const FRect r = {x, y, bw, bh};
			m_renderer.ResetClipRect();
			_FillRR(r,   s.tooltipBg, SDL::FCorners(4.f), 1.f);
			_StrokeRR(r, s.tooltipBd, {1.f, 1.f, 1.f, 1.f}, SDL::FCorners(4.f), 1.f);
			_Text(m_tooltipEntity, td->text, x + kPadH, y + kPadV, s.tooltipText, 1.f, ts);
		}

		// ── Render ─────────────────────────────────────────────────────────

		void _ProcessRender() {
			m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
			_RenderNode(m_root);
			m_renderer.ResetClipRect();
			_DrawTooltip();
		}

		void _RenderNode(ECS::EntityId e) {
		   if (!m_ctx.IsAlive(e)) return; 
			auto *w  = m_ctx.Get<Widget>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !cr) return;

			if (!Has(w->behavior, BehaviorFlag::Visible)) return;
			
			if (cr->outer_clip.w <= 0.f || cr->outer_clip.h <= 0.f) return;

			m_renderer.SetClipRect(FRectToRect(cr->outer_clip));
			_DrawWidget(e);
			
			auto *ch = m_ctx.Get<Children>(e);
			if (ch) {
				for (ECS::EntityId c : ch->ids) {
					_RenderNode(c);
				}
			}
		}

		// ── Clip helpers ─────────────────────────────────────────────────────────

		/// Returns the intersection of two SDL::Rect clip regions.
		/// If either rect has zero area the result is also empty.
		static SDL::Rect _ClipIntersect(SDL::Rect a, SDL::Rect b) noexcept {
			int x1 = SDL::Max(a.x, b.x);
			int y1 = SDL::Max(a.y, b.y);
			int x2 = SDL::Min(a.x + a.w, b.x + b.w);
			int y2 = SDL::Min(a.y + a.h, b.y + b.h);
			return {x1, y1, SDL::Max(0, x2 - x1), SDL::Max(0, y2 - y1)};
		}

		// ── Primitives ────────────────────────────────────────────────────────────

		void _FillRect(const SDL::FRect& r, SDL::Color c, float op) {
			c.a = SDL::Clamp8(c.a * op);
			m_renderer.SetDrawColor(c);
			m_renderer.RenderFillRect(r);
		}
		void _FillRR(const SDL::FRect& r, SDL::Color c, const SDL::FCorners& rad, float op) {
			c.a = SDL::Clamp8(c.a * op);
			m_renderer.SetDrawColor(c);
			if (rad.bl > 0.f || rad.br > 0.f || rad.tl > 0.f || rad.tr > 0.f)
				m_renderer.RenderFillRoundedRect(r, rad);
			else
				m_renderer.RenderFillRect(r);
		}
		void _StrokeRR(const SDL::FRect& r, SDL::Color c, const SDL::FBox& b, const SDL::FCorners& rad, float op) {
			if (b.left <= 0.f || b.right <= 0.f || b.top <= 0.f || b.bottom <= 0.f) return;
			c.a = SDL::Clamp8(c.a * op);
			m_renderer.SetDrawColor(c);
			if (rad.bl > 0.f || rad.br > 0.f || rad.tl > 0.f || rad.tr > 0.f)
				m_renderer.RenderRoundedBorderedRect(r, b, rad);
			else
				m_renderer.RenderRect(r);
		}
		// ── Text rendering (TTF with debug fallback) ──────────────────────────────
		//
		// All draw helpers pass `const Style& s` so font metadata is always
		// available.  When SDL3PP_ENABLE_TTF is defined and fontKey/fontSize are
		// set on the style, SDL3_ttf renders the text via the pool-cached Font
		// and SDL::RendererTextEngine otherwise the built-in 8×8 debug font is used.
		
		void _Text(ECS::EntityId e, const std::string &text, float x, float y, SDL::Color c, float op, const Style &s) {
			if (text.empty()) return;
			c.a = SDL::Clamp8(c.a * op);
#if UI_HAS_TTF
			auto rf = _ResolveFont(e);
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f) {
				if (auto font = _EnsureFont(rf.key, rf.size)) {
					if (auto txt = _EnsureText(e, font, text)) {
						txt.SetColor(c);
						txt.DrawRenderer({x, y});
						return;
					}
				}
			}
#endif
			m_renderer.SetDrawColor(c);
			float scale = (s.fontSize > 0.f) ? s.fontSize / ((float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE) : 1.f;
			m_renderer.SetScale({scale, scale});
			m_renderer.RenderDebugText({x / scale, y / scale}, text);
			m_renderer.SetScale({1.f, 1.f});
		}

		/// Height of one text line in pixels, resolved through the FontType hierarchy.
		[[nodiscard]] float _TH(ECS::EntityId e) noexcept {
#if UI_HAS_TTF
			auto rf = _ResolveFont(e);
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f)
				if (auto f = _EnsureFont(rf.key, rf.size))
					return (float)f.GetHeight();
			return (rf.size > 0.f) ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#else
			auto* s = m_ctx.Get<Style>(e);
			return (s && s->fontSize > 0.f) ? s->fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#endif
		}

		/// Width of a string in pixels, resolved through the FontType hierarchy.
		[[nodiscard]] float _TW(const std::string &t, ECS::EntityId e) {
			if (t.empty()) return 0.f;
#if UI_HAS_TTF
			auto rf = _ResolveFont(e);
			if (!rf.isDebug && !rf.key.empty() && rf.size > 0.f) {
				if (auto font = _EnsureFont(rf.key, rf.size)) {
					int w = 0, h = 0;
					font.GetStringSize(t, &w, &h);
					return (float)w;
				}
			}
			return (float)t.size() * (rf.size > 0.f ? rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE);
#else
			auto* s = m_ctx.Get<Style>(e);
			float fs = (s && s->fontSize > 0.f) ? s->fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
			return (float)t.size() * fs;
#endif
		}

		// ── Per-type draw ─────────────────────────────────────────────────────────

		void _DrawWidget(ECS::EntityId e) {
			auto *w = m_ctx.Get<Widget>(e);
			auto *s = m_ctx.Get<Style>(e);
			auto *st = m_ctx.Get<WidgetState>(e);
			auto *cr = m_ctx.Get<ComputedRect>(e);
			if (!w || !s || !st || !cr) return;
			const FRect &r = cr->screen;

			// ── Tileset skin (9-slice) — replaces solid-colour background ────────────
			// When present, the tileset is drawn first.  For Canvas widgets the game
			// content is rendered on top afterwards; for all other widget types the
			// tileset is the only background and we skip the normal draw entirely.
			auto *ts = m_ctx.Get<TilesetStyle>(e);
			if (ts && !ts->textureKey.empty()) {
				auto tex = _EnsureTexture(ts->textureKey); 
				if (tex) {
					_Draw9Slice(r, *ts, tex);
					// Canvas: still draw game content on top of the skin.
					if (w->type == WidgetType::Canvas)
						_DrawCanvas(e, r);
					// Labels inside a skinned container still need their text drawn.
					else if (w->type == WidgetType::Label)
						_DrawLabel(e, r, *s, *st, *w);
					else if (w->type == WidgetType::Button)
						_DrawButton(e, r, *s, *st, *w);  // draws text + hover overlay
					return;
				}
			}

			// ── Default solid-colour rendering ────────────────────────────────────────
			switch (w->type) { 
				case WidgetType::Container:
					_DrawContainer(e, r, *s, *st, *w);
					break;
				case WidgetType::Label:
					_DrawLabel(e, r, *s, *st, *w);
					break;
				case WidgetType::Button:
					_DrawButton(e, r, *s, *st, *w);
					break;
				case WidgetType::Toggle:
					_DrawToggle(e, r, *s, *st);
					break;
				case WidgetType::RadioButton:
					_DrawRadio(e, r, *s, *st);
					break;
				case WidgetType::Slider:
					_DrawSlider(e, r, *s, *st, *w);
					break;
				case WidgetType::ScrollBar:
					_DrawScrollBar(e, r, *s, *st, *w);
					break;
				case WidgetType::Progress:
					_DrawProgress(e, r, *s, *st);
					break;
				case WidgetType::Separator:
					_DrawSeparator(r, *s);
					break;
				case WidgetType::Input:
					_DrawInput(e, r, *s, *st, *w);
					break;
				case WidgetType::TextArea:
					_DrawTextArea(e, r, *s, *st, *w);
					break;
				case WidgetType::Knob:
					_DrawKnob(e, r, *s, *st, *w);
					break;
				case WidgetType::Image:
					_DrawImage(e, r, *s, *st);
					break;
				case WidgetType::Canvas:
					_DrawCanvas(e, r);
					break;
				case WidgetType::ListBox:
					_DrawListBox(e, r, *s, *st, *w);
					break;
				case WidgetType::Graph:
					_DrawGraph(e, r, *s, *st);
					break;
			}
		}



		// ── 9-slice tileset draw ───────────────────────────────────────

		/**
		 * Draw a 9-slice widget using a tileset texture.
		 *
		 * The nine tiles are indexed relative to `ts.firstTileIdx`:
		 *   0=TL, 1=TC, 2=TR, 3=ML, 4=MC, 5=MR, 6=BL, 7=BC, 8=BR
		 *
		 * @param r   Screen rect of the widget.
		 * @param ts  Tileset style configuration.
		 * @param tex Resolved tileset texture.
		 */
		void _Draw9Slice(const FRect& r, const TilesetStyle& ts, TextureRef tex) { 
			if (!tex) return;

			const float bw  = SDL::Min(ts.BorderW(), r.w * 0.5f);
			const float bh  = SDL::Min(ts.BorderH(), r.h * 0.5f);
			const float cx  = r.x + bw;
			const float cy  = r.y + bh;
			const float cw  = r.w - 2.f * bw;
			const float ch  = r.h - 2.f * bh;

			tex.SetAlphaMod(SDL::Clamp8(static_cast<int>(255 * ts.opacity)));

			auto draw = [&](int rel, FRect dst) {
				FRect src = ts.TileRect(rel);
				m_renderer.RenderTexture(tex, src, dst);
			};

			// Corners (always drawn)
			draw(0, {r.x,            r.y,            bw, bh});
			draw(2, {r.x + r.w - bw, r.y,            bw, bh});
			draw(6, {r.x,            r.y + r.h - bh, bw, bh});
			draw(8, {r.x + r.w - bw, r.y + r.h - bh, bw, bh});

			// Top / bottom edges
			if (cw > 0.f) {
				draw(1, {cx, r.y,            cw, bh});
				draw(7, {cx, r.y + r.h - bh, cw, bh});
			}

			// Left / right edges
			if (ch > 0.f) {
				draw(3, {r.x,            cy, bw, ch});
				draw(5, {r.x + r.w - bw, cy, bw, ch});
			}

			// Center fill
			if (cw > 0.f && ch > 0.f)
				draw(4, {cx, cy, cw, ch});

			tex.SetAlphaMod(255);
		}

		void _DrawInlineScrollbars(ECS::EntityId e, const FRect &r, const Style &s, const Widget &w) {
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			auto *css = m_ctx.Get<ContainerScrollState>(e);
			if (!lp || !css) return;

			const float innerW = r.w - lp->padding.left - lp->padding.right;
			const float innerH = r.h - lp->padding.top  - lp->padding.bottom;

			// Déterminer la visibilité des scrollbars
			bool showX = false, showY = false;
			_ContainerScrollbars(w, *lp, innerW, innerH, showX, showY);

			const float viewW = showY ? SDL::Max(0.f, innerW - lp->scrollbarThickness) : innerW;
			const float viewH = showX ? SDL::Max(0.f, innerH - lp->scrollbarThickness) : innerH;
			const float t     = lp->scrollbarThickness;

			// SÉCURITÉ : Empêcher le contenu de rester scrollé dans le vide
			lp->scrollX = SDL::Clamp(lp->scrollX, 0.f, SDL::Max(0.f, lp->contentW - viewW));
			lp->scrollY = SDL::Clamp(lp->scrollY, 0.f, SDL::Max(0.f, lp->contentH - viewH));

			SDL::Color trackCol = s.track;
			trackCol.a = SDL::Clamp8((int)(trackCol.a * s.opacity * 0.85f));

			// ── Scrollbar Verticale ──
			css->thumbY = {}; 
			if (showY) {
				FRect barY = {r.x + r.w - t, r.y + lp->padding.top, t, viewH};
				_FillRR(barY, trackCol, SDL::FCorners(t * 0.5f), 1.f);

				if (lp->contentH > 0.f && lp->contentH > viewH) {
					float ratio  = SDL::Clamp(viewH / lp->contentH, 0.05f, 1.f);
					float maxOff = lp->contentH - viewH;
					float offN   = (maxOff > 0.f) ? lp->scrollY / maxOff : 0.f;
					float tH     = SDL::Max(t * 2.f, barY.h * ratio);
					float tY     = barY.y + (barY.h - tH) * offN;

					bool thumbHov = css->dragY || _Contains({barY.x, tY, t, tH}, m_mousePos);
					SDL::Color thumbCol = thumbHov ? s.thumb : s.fill;
					thumbCol.a = SDL::Clamp8((int)(thumbCol.a * s.opacity));

					css->thumbY = {barY.x + 1.f, tY, t - 2.f, tH};
					_FillRR(css->thumbY, thumbCol, SDL::FCorners((t - 2.f) * 0.5f), 1.f);
				}
			}

			// ── Scrollbar Horizontale ──
			css->thumbX = {}; 
			if (showX) {
				float barW  = showY ? viewW : innerW;
				FRect barX  = {r.x + lp->padding.left, r.y + r.h - t, barW, t};
				_FillRR(barX, trackCol, SDL::FCorners(t * 0.5f), 1.f);

				if (lp->contentW > 0.f && lp->contentW > viewW) {
					float ratio  = SDL::Clamp(viewW / lp->contentW, 0.05f, 1.f);
					float maxOff = lp->contentW - viewW;
					float offN   = (maxOff > 0.f) ? lp->scrollX / maxOff : 0.f;
					float tW     = SDL::Max(t * 2.f, barX.w * ratio);
					float tX     = barX.x + (barX.w - tW) * offN;

					bool thumbHov = css->dragX || _Contains({tX, barX.y, tW, t}, m_mousePos);
					SDL::Color thumbCol = thumbHov ? s.thumb : s.fill;
					thumbCol.a = SDL::Clamp8((int)(thumbCol.a * s.opacity));

					css->thumbX = {tX, barX.y + 1.f, tW, t - 2.f};
					_FillRR(css->thumbX, thumbCol, SDL::FCorners((t - 2.f) * 0.5f), 1.f);
				}
			}

			// ── Coin (quand les 2 barres sont affichées) ──
			if (showX && showY) {
				FRect corner = {r.x + r.w - t, r.y + r.h - t, t, t};
				_FillRR(corner, trackCol, SDL::FCorners(0.f), 1.f);
			}
		}
		
		void _DrawContainer(ECS::EntityId e, const FRect &r, const Style &s,
							const WidgetState &st, const Widget &w) {
			// ── Background & border ───────────────────────────────────────────────
			SDL::Color bgColor = st.pressed ? s.bgPressed
							   : st.hovered ? s.bgHovered
							   : s.bgColor;
			_FillRR(r, bgColor, s.radius, s.opacity);
			_StrokeRR(r, st.hovered ? s.bdHovered : s.bdColor, s.borders, s.radius, s.opacity);

			// ── InGrid separator lines ────────────────────────────────────────────
			auto *lp = m_ctx.Get<LayoutProps>(e);
			auto *gp = m_ctx.Get<LayoutGridProps>(e);
			if (lp && gp && lp->layout == Layout::InGrid && gp->lines != GridLines::None
				&& gp->lineThickness > 0.f && !gp->colWidths.empty() && !gp->rowHeights.empty()) {

				SDL::Color lc = gp->lineColor;
				lc.a = SDL::Clamp8((int)((float)lc.a * s.opacity));
				m_renderer.SetDrawColor(lc);

				float ox = r.x + lp->padding.left  - lp->scrollX;
				float oy = r.y + lp->padding.top   - lp->scrollY;
				float gap = lp->gap;
				float lt  = gp->lineThickness;
				float contentW = 0.f;
				for (float cw : gp->colWidths) contentW += cw;
				contentW += gap * (float)((int)gp->colWidths.size() - 1);
				float contentH = 0.f;
				for (float h : gp->rowHeights) contentH += h;
				contentH += gap * (float)((int)gp->rowHeights.size() - 1);

				// Vertical lines (between columns)
				if (gp->lines == GridLines::Columns || gp->lines == GridLines::Both) {
					float x = ox;
					for (int ci = 0; ci + 1 < (int)gp->colWidths.size(); ++ci) {
						x += gp->colWidths[ci];
						float lx = x + (gap - lt) * 0.5f;
						m_renderer.RenderFillRect(FRect{lx, oy, lt, contentH});
						x += gap;
					}
				}

				// Horizontal lines (between rows)
				if (gp->lines == GridLines::Rows || gp->lines == GridLines::Both) {
					float y = oy;
					for (int ri = 0; ri + 1 < (int)gp->rowHeights.size(); ++ri) {
						y += gp->rowHeights[ri];
						float ly = y + (gap - lt) * 0.5f;
						m_renderer.RenderFillRect(FRect{ox, ly, contentW, lt});
						y += gap;
					}
				}
			}

			// ── Inline scrollbars ─────────────────────────────────────────────────
			_DrawInlineScrollbars(e, r, s, w);
		}
		
		void _DrawLabel(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
			auto *c = m_ctx.Get<Content>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!c)
				return;
			SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabled : st.hovered ? s.textHovered
																	 : s.textColor;
			_Text(e, c->text, r.x + (lp ? lp->padding.left : 4.f), r.y + (r.h - _TH(e)) * 0.5f, tc, s.opacity, s);
		}

		void _DrawButton(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			const bool enabled = Has(w.behavior, BehaviorFlag::Enable);
			SDL::Color bgColor = !enabled ? s.bgDisabled
				: (st.pressed ? s.bgPressed
					: (st.hovered ? s.bgHovered : s.bgColor));
			SDL::Color bdColor = (m_focused == e) ? s.bdFocused
				: (st.hovered ? s.bdHovered : s.bdColor);
			SDL::Color tc = !enabled ? s.textDisabled
				: (st.hovered ? s.textHovered : s.textColor);
			_FillRR(r, bgColor, s.radius, s.opacity);
			_StrokeRR(r, bdColor, s.borders, s.radius, s.opacity);

			auto *c  = m_ctx.Get<Content>(e);
			auto *ic = m_ctx.Get<IconData>(e);

			// ── Icon ─────────────────────────────────────────────────────────
			float textX = 0.f;  // set when icon is drawn and text follows it
			if (ic && !ic->key.empty()) {
				auto tex = _EnsureTexture(ic->key);
				if (tex) {
					float sz    = SDL::Min(r.w, r.h) - ic->pad * 2.f;
					float textW = (c && !c->text.empty()) ? _TW(c->text, e) : 0.f;
					float iconX;
					if (textW > 0.f) {
						constexpr float kGap = 4.f;
						iconX = r.x + (r.w - (sz + kGap + textW)) * 0.5f;
						textX = iconX + sz + kGap;
					} else {
						iconX = r.x + (r.w - sz) * 0.5f;
					}
					float iconY = r.y + (r.h - sz) * 0.5f;

					float opacity = (!enabled ? ic->opacityDisabled
						: st.pressed ? ic->opacityPressed
						: st.hovered ? ic->opacityHovered
						: ic->opacityNormal) * s.opacity;
					const SDL::Color &tint = !enabled ? ic->tintDisabled
						: st.pressed ? ic->tintPressed
						: st.hovered ? ic->tintHovered
						: ic->tintNormal;

					tex.SetAlphaMod(SDL::Clamp8((int)(opacity * 255.f)));
					tex.SetColorMod(tint.r, tint.g, tint.b);
					m_renderer.RenderTexture(tex, std::nullopt, FRect{iconX, iconY, sz, sz});
					tex.SetAlphaMod(255);
					tex.SetColorMod(255, 255, 255);
				}
			}

			// ── Text ─────────────────────────────────────────────────────────
			if (c && !c->text.empty()) {
				float tw = _TW(c->text, e), th = _TH(e);
				float tx_ = (textX > 0.f) ? textX : r.x + (r.w - tw) * 0.5f;
				_Text(e, c->text, tx_, r.y + (r.h - th) * 0.5f, tc, s.opacity, s);
			}
		}

		void _DrawToggle(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) { 
			auto *t = m_ctx.Get<ToggleData>(e);
			auto *c = m_ctx.Get<Content>(e);
			auto *w = m_ctx.Get<Widget>(e);
			if (!t)
				return;
			constexpr float TW = 44.f, TH = 22.f;
			float ty = r.y + (r.h - TH) * 0.5f;
			FRect tr_ = {r.x + 8.f, ty, TW, TH};
			SDL::Color tc2 = {(Uint8)(s.track.r + (s.fill.r - s.track.r) * t->animT), (Uint8)(s.track.g + (s.fill.g - s.track.g) * t->animT), (Uint8)(s.track.b + (s.fill.b - s.track.b) * t->animT), s.track.a};
			_FillRR(tr_, tc2, SDL::FCorners(TH) * 0.5f, s.opacity);
			_StrokeRR(tr_, (m_focused == e) ? s.bdFocused : s.bdColor, s.borders, SDL::FCorners(TH) * 0.5f, s.opacity);
			float thumbR = (TH - 4.f) * 0.5f, thumbX = tr_.x + 2.f + thumbR + t->animT * (TW - 4.f - TH);
			_FillRR({thumbX - thumbR, ty + (TH - thumbR * 2.f) * 0.5f, thumbR * 2.f, thumbR * 2.f}, st.hovered ? s.thumb : SDL::Color{200, 202, 210, 255}, SDL::FCorners(thumbR), s.opacity);
			if (c && !c->text.empty()) {
				SDL::Color col = w && !Has(w->behavior, BehaviorFlag::Enable) ? s.textDisabled : s.textColor;
				_Text(e, c->text, tr_.x + TW + 10.f, r.y + (r.h - _TH(e)) * 0.5f, col, s.opacity, s);
			}
		}

		void _DrawRadio(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) { 
			auto *rd = m_ctx.Get<RadioData>(e);
			auto *c = m_ctx.Get<Content>(e);
			auto *w = m_ctx.Get<Widget>(e);
			if (!rd)
				return;
			const float OR = 9.f;
			float cx_ = r.x + 14.f, cy_ = r.y + r.h * 0.5f;
			SDL::Color bgColor = !w || !Has(w->behavior, BehaviorFlag::Enable) ? s.bgDisabled : st.pressed ? s.bgPressed
														   : st.hovered   ? s.bgHovered
																		  : s.bgColor;
			bgColor.a = (Uint8)(bgColor.a * s.opacity);
			m_renderer.SetDrawColor(bgColor);
			m_renderer.RenderCircle({cx_, cy_}, OR);
			SDL::Color bdColor = (m_focused == e) ? s.bdFocused : st.hovered ? s.bdHovered
																			: s.bdColor;
			bdColor.a = (Uint8)(bdColor.a * s.opacity);
			m_renderer.SetDrawColor(bdColor);
			m_renderer.RenderCircle({cx_, cy_}, OR);
			if (rd->checked) {
				SDL::Color fc = s.fill;
				fc.a = (Uint8)(fc.a * s.opacity);
				m_renderer.SetDrawColor(fc);
				m_renderer.RenderFillCircle({cx_, cy_}, OR * 0.5f);
			}
			if (c && !c->text.empty()) {
				SDL::Color tc = !w || !Has(w->behavior, BehaviorFlag::Enable) ? s.textDisabled : st.hovered ? s.textHovered
																				: s.textColor;
				_Text(e, c->text, r.x + 30.f, r.y + (r.h - _TH(e)) * 0.5f, tc, s.opacity, s);
			}
		}

		void _DrawSlider(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *sd = m_ctx.Get<SliderData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!sd || !lp) return;
			const float TH = 4.f, TR = 8.f;
			float norm = (sd->max > sd->min) ? (sd->val - sd->min) / (sd->max - sd->min) : 0.f;
			if (sd->orientation == Orientation::Horizontal) {
				float tx = r.x + lp->padding.left, bx_ = r.x + r.w - lp->padding.right, tw = bx_ - tx, mid = r.y + r.h * 0.5f;
				_FillRR({tx, mid - TH * 0.5f, tw, TH}, s.track, SDL::FCorners(TH * 0.5f), s.opacity);
				if (norm > 0.f)
					_FillRR({tx, mid - TH * 0.5f, tw * norm, TH}, Has(w.behavior, BehaviorFlag::Enable) ? s.fill : s.track, SDL::FCorners(TH * 0.5f), s.opacity);
				float tcx = tx + tw * norm;
				SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabled : (m_focused == e || sd->drag || st.hovered) ? s.thumb
																										 : SDL::Color{160, 170, 190, 255};
				_FillRR({tcx - TR, mid - TR, TR * 2.f, TR * 2.f}, tc, SDL::FCorners(TR), s.opacity);
				if (m_focused == e)
					_StrokeRR({tcx - TR, mid - TR, TR * 2.f, TR * 2.f}, s.bdFocused, s.borders, SDL::FCorners(TR), s.opacity);
			}
			else {
				float ty_ = r.y + lp->padding.top, by_ = r.y + r.h - lp->padding.bottom, th_ = by_ - ty_, mid = r.x + r.w * 0.5f;
				_FillRR({mid - TH * 0.5f, ty_, TH, th_}, s.track, SDL::FCorners(TH * 0.5f), s.opacity);
				if (norm > 0.f) {
					float fH = th_ * norm;
					_FillRR({mid - TH * 0.5f, by_ - fH, TH, fH}, Has(w.behavior, BehaviorFlag::Enable) ? s.fill : s.track, SDL::FCorners(TH * 0.5f), s.opacity);
				}
				float tcy = ty_ + th_ * (1.f - norm);
				SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabled : (m_focused == e || sd->drag || st.hovered) ? s.thumb
																										 : SDL::Color{160, 170, 190, 255};
				_FillRR({mid - TR, tcy - TR, TR * 2.f, TR * 2.f}, tc, SDL::FCorners(TR), s.opacity);
			}
		}

		void _DrawScrollBar(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &) { 
			auto *sb = m_ctx.Get<ScrollBarData>(e);
			if (!sb)
				return;
			_FillRR(r, s.track, s.radius, s.opacity);
			if (sb->contentSize <= 0.f || sb->viewSize >= sb->contentSize)
				return;
			float ratio = sb->viewSize / sb->contentSize, maxO = sb->contentSize - sb->viewSize;
			float offN = (maxO > 0.f) ? sb->offset / maxO : 0.f;
			if (sb->orientation == Orientation::Vertical) {
				float tH = SDL::Max(20.f, r.h * ratio), tY = r.y + (r.h - tH) * offN;
				_FillRR({r.x + 1.f, tY, r.w - 2.f, tH}, (st.hovered || sb->drag) ? s.thumb : s.fill, s.radius, s.opacity);
			}
			else {
				float tW = SDL::Max(20.f, r.w * ratio), tX = r.x + (r.w - tW) * offN;
				_FillRR({tX, r.y + 1.f, tW, r.h - 2.f}, (st.hovered || sb->drag) ? s.thumb : s.fill, s.radius, s.opacity);
			}
		}

		void _DrawProgress(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) { 
			auto *sd = m_ctx.Get<SliderData>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!sd || !lp) return;
			float tx = r.x + lp->padding.left, tw = r.x + r.w - lp->padding.right - tx, norm = (sd->max > sd->min) ? (sd->val - sd->min) / (sd->max - sd->min) : 0.f;
			FRect tr_ = {tx, r.y + (r.h - 8.f) * 0.5f, tw, 8.f};
			_FillRR(tr_, s.track, FCorners(4.f), s.opacity);
			if (norm > 0.f)
				_FillRR({tx, tr_.y, tw * norm, tr_.h}, s.fill, FCorners(4.f), s.opacity);
			_StrokeRR(tr_, s.bdColor, s.borders, FCorners(4.f), s.opacity);
		}
		
		void _DrawSeparator(const FRect &r, const Style &s) {
			_FillRect({r.x, r.y + r.h * 0.5f, r.w, 1.f}, s.separator, s.opacity);
		}
		
		void _DrawInput(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
			auto *c = m_ctx.Get<Content>(e);
			auto *lp = m_ctx.Get<LayoutProps>(e);
			if (!c || !lp) return;
			
			bool foc = (m_focused == e);
			bool enabled = Has(w.behavior, BehaviorFlag::Enable);

			// Fond et bordures
			SDL::Color bgColor = !enabled ? s.bgDisabled : foc ? s.bgFocused
													: st.hovered ? SDL::Color{30, 32, 44, 255}
																 : s.bgColor;
			SDL::Color bdColor = !enabled ? s.bdDisabled : foc ? s.bdFocused
												: st.hovered ? s.bdHovered
															 : s.bdColor;
			_FillRR(r, bgColor, s.radius, s.opacity);
			_StrokeRR(r, bdColor, SDL::Max(s.borders, 1.f), s.radius, s.opacity);
			
			float tx_ = r.x + lp->padding.left;
			float ty_ = r.y + (r.h - _TH(e)) * 0.5f;

			// ── Dessin de la sélection ──
			if (c->HasSelection() && !c->text.empty()) {
				float selStartX = tx_ + _TW(c->text.substr(0, c->SelMin()), e);
				float selWidth  = _TW(c->GetSelectedText(), e);

				SDL::Color hlC = c->highlightColor;
				hlC.a = SDL::Clamp8((int)((float)hlC.a * s.opacity));
				m_renderer.SetDrawColor(hlC);
				m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
				m_renderer.RenderFillRect(SDL::FRect{selStartX, ty_, selWidth, _TH(e)});
			}

			// ── Texte et Curseur ──
			bool showPH = c->text.empty() && !c->placeholder.empty() && !foc;
			if (showPH) {
				_Text(e, c->placeholder, tx_, ty_, s.textPlaceholder, s.opacity, s);
			} else {
				_Text(e, c->text, tx_, ty_, enabled ? s.textColor : s.textDisabled, s.opacity, s);

				if (foc && c->blinkTimer < 0.5f) {
					float cx_ = tx_ + _TW(c->text.substr(0, (size_t)SDL::Max(0, c->cursor)), e);
					_FillRect({cx_, ty_, 1.5f, _TH(e)}, s.textColor, s.opacity);
				}
			}
		}

		// ── TextArea ────────────────────────────────────────────────────────
		void _DrawTextArea(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
			auto *ta  = m_ctx.Get<TextAreaData>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			auto *cnt = m_ctx.Get<Content>(e);
			if (!ta || !lp) return;

			const bool foc     = (m_focused == e);
			const bool enabled = Has(w.behavior, BehaviorFlag::Enable);

			// ── 1. Fond & Bordures ────────────────────────────────────────────────
			SDL::Color bgC = !enabled ? s.bgDisabled
						   : foc      ? s.bgFocused
						   : st.hovered ? SDL::Color{30, 32, 44, 255}
										: s.bgColor;
			SDL::Color bdC = !enabled ? s.bdDisabled
						   : foc      ? s.bdFocused
						   : st.hovered ? s.bdHovered : s.bdColor;
			
			_FillRR(r, bgC, s.radius, s.opacity);
			_StrokeRR(r, bdC, SDL::Max(s.borders, 1.f), s.radius, s.opacity);

			// ── 2. Calcul des zones et scrollbars ─────────────────────────────────
			const float innerW = r.w - lp->padding.left - lp->padding.right;
			const float innerH = r.h - lp->padding.top - lp->padding.bottom;
			
			bool showX = false, showY = false;
			_ContainerScrollbars(w, *lp, innerW, innerH, showX, showY);
			
			// L'espace véritablement disponible pour lire le texte (sans les scrollbars)
			const float viewW = showY ? SDL::Max(0.f, innerW - lp->scrollbarThickness) : innerW;
			const float viewH = showX ? SDL::Max(0.f, innerH - lp->scrollbarThickness) : innerH;

			// ── 3. Application du Clipping strict pour le texte ───────────────────
			// Intersect with the current (parent) clip so content never overflows a
			// scrolled ancestor container.
			SDL::Rect prevClip = m_renderer.GetClipRect();

			SDL::Rect textClip = _ClipIntersect(prevClip, {
				(int)(r.x + lp->padding.left),
				(int)(r.y + lp->padding.top),
				(int)viewW,
				(int)viewH
			});
			m_renderer.SetClipRect(textClip);

			// ── 4. Rendu du Placeholder (si vide) ─────────────────────────────────
			if (ta->text.empty() && cnt && !cnt->placeholder.empty() && !foc) {
				_Text(e, cnt->placeholder, r.x + lp->padding.left, r.y + lp->padding.top,
					  s.textPlaceholder, s.opacity, s);
			} 
			else {
				// ── 5. Préparation des coordonnées de rendu scrollées ─────────────
				const float lineH  = _TH(e) + 2.f;
				// On applique le décalage du scroll (X et Y) sur le point de départ
				const float startX = r.x + lp->padding.left - lp->scrollX;
				const float startY = r.y + lp->padding.top  - lp->scrollY;

				// Optimisation : on ne dessine que les lignes visibles
				int firstLine = SDL::Max(0, (int)(lp->scrollY / lineH) - 1);
				int lastLine  = SDL::Min(ta->LineCount() - 1, (int)((lp->scrollY + viewH) / lineH) + 1);

				// ── 6. Rendu des sélections ───────────────────────────────────────
				if (ta->HasSelection()) {
					int selMin = ta->SelMin(), selMax = ta->SelMax();
					SDL::Color hlC = ta->highlightColor;
					hlC.a = SDL::Clamp8((int)((float)hlC.a * s.opacity));
					m_renderer.SetDrawColor(hlC);
					m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);

					for (int ln = firstLine; ln <= lastLine; ++ln) {
						int ls = ta->LineStart(ln);
						int le = ta->LineEnd(ln);
						if (le < selMin || ls > selMax) continue;
						
						int hiStart = SDL::Max(ls, selMin);
						int hiEnd   = SDL::Min(le, selMax);
						
						float x0 = startX + _TextAreaLineX(ta, ln, hiStart - ls, e);
						float x1 = startX + _TextAreaLineX(ta, ln, hiEnd   - ls, e);
						
						// Extension visuelle de la sélection jusqu'en fin de ligne si la sélection continue
						if (hiEnd == le && selMax > le) x1 = startX + lp->contentW + 8.f; 
						
						float ly = startY + ln * lineH;
						m_renderer.RenderFillRect(SDL::FRect{x0, ly, SDL::Max(2.f, x1 - x0), lineH});
					}
				}

				// ── 7. Rendu du texte (avec support du Rich Text / Spans) ─────────
				for (int ln = firstLine; ln <= lastLine; ++ln) {
					int ls = ta->LineStart(ln);
					int le = ta->LineEnd(ln);
					float ly = startY + ln * lineH;

					float xOff = startX; // Le curseur de rendu horizontal pour cette ligne
					
					for (int ci = ls; ci < le; ) {
						int spanEnd = le;
						const TextSpanStyle *spanStyle = ta->SpanStyleAt(ci);
						
						int ni = ci + 1;
						while (ni < le && ta->SpanStyleAt(ni) == spanStyle) ++ni;
						spanEnd = ni;

						std::string run;
						auto _rf = _ResolveFont(e);
						float charW = (_rf.size > 0.f) ? _rf.size : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#if UI_HAS_TTF
						if (!_rf.isDebug && !_rf.key.empty() && _rf.size > 0.f) {
							if (auto font = _EnsureFont(_rf.key, _rf.size)) {
								int sw = 0, sh = 0;
								font.GetStringSize(" ", &sw, &sh);
								charW = (float)sw;
							}
						}
#endif
						int visualCol = (ci > ls) ? (int)((xOff - startX) / charW) : 0; 
						
						for (int k = ci; k < spanEnd; ++k) {
							char ch = ta->text[k];
							if (ch == '\t') {
								if (!run.empty()) {
									SDL::Color tc = enabled ? s.textColor : s.textDisabled;
									if (spanStyle && spanStyle->color.a > 0) tc = spanStyle->color;
									_Text(e, run, xOff, ly, tc, s.opacity, s);
									xOff += _TW(run, e);
									run.clear();
								}
								int spaces = ta->tabSize - (visualCol % ta->tabSize);
								if (spaces == 0) spaces = ta->tabSize;
								xOff += spaces * charW;
								visualCol += spaces;
							} else {
								run += ch;
								++visualCol;
							}
						}
						if (!run.empty()) {
							SDL::Color tc = enabled ? s.textColor : s.textDisabled;
							if (spanStyle && spanStyle->color.a > 0) tc = spanStyle->color;
							_Text(e, run, xOff, ly, tc, s.opacity, s);
							xOff += _TW(run, e);
						}
						ci = spanEnd;
					}
				}

				// ── 8. Rendu du Curseur clignotant ────────────────────────────────
				if (foc && !ta->readOnly && ta->blinkTimer < 0.5f) {
					int curLine = ta->LineOf(ta->cursorPos);
					int curCol  = ta->ColOf(ta->cursorPos);
					
					float cx_ = startX + _TextAreaLineX(ta, curLine, curCol, e);
					float cy_ = startY + curLine * lineH;
					
					_FillRect({cx_, cy_, 1.5f, lineH}, s.textColor, s.opacity);
				}
			} // Fin du bloc (Texte non-vide)

			// ── 9. Restauration du Clip parent ────────────────────────────────────
			if (prevClip.w > 0 && prevClip.h > 0)
				m_renderer.SetClipRect(prevClip);
			else
				m_renderer.SetClipRect({});

			// ── 10. Rendu des Scrollbars unifiées ─────────────────────────────────
			// Dessinées en dernier, PARDESSUS le texte, hors du clip restrictif.
			_DrawInlineScrollbars(e, r, s, w);
		}

		void _DrawKnob(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
			auto *kd = m_ctx.Get<KnobData>(e);
			if (!kd) return;

			// Sécurité : Si le widget est trop petit, on ne dessine rien pour éviter le crash
			float minDim = SDL::Min(r.w, r.h);
			if (minDim < 10.f) return; 

			float cx_ = r.x + r.w * 0.5f;
			float cy_ = r.y + r.h * 0.5f;
			float oR = minDim * 0.5f - 2.f;
			float iR = oR * 0.55f;

			if (oR <= 0.f) return; // Ultime sécurité

			// Couleur de fond
			SDL::Color bgColor = !Has(w.behavior, BehaviorFlag::Enable) ? s.bgDisabled 
							: st.pressed ? s.bgPressed
							: st.hovered ? s.bgHovered : s.bgColor;
			bgColor.a = (Uint8)(bgColor.a * s.opacity);
			m_renderer.SetDrawColor(bgColor);
			m_renderer.RenderFillCircle({cx_, cy_}, oR);

			// Bordure
			SDL::Color bdColor = (m_focused == e) ? s.bdFocused 
							: st.hovered ? s.bdHovered : s.bdColor;
			bdColor.a = (Uint8)(bdColor.a * s.opacity);
			m_renderer.SetDrawColor(bdColor);
			m_renderer.RenderCircle({cx_, cy_}, oR);

			// Track (fond de l'arc) : 135° à 45° (sens horaire)
			SDL::Color trackC = s.track;
			trackC.a = (Uint8)(trackC.a * s.opacity);
			m_renderer.SetDrawColor(trackC);
			m_renderer.RenderArc({cx_, cy_}, iR, 135.f, 360.f);
			m_renderer.RenderArc({cx_, cy_}, iR, 0.f, 45.f);

			float norm = (kd->max > kd->min) ? (kd->val - kd->min) / (kd->max - kd->min) : 0.5f;
			norm = SDL::Clamp(norm, 0.f, 1.f);

			// Remplissage de la valeur
			SDL::Color fillC = Has(w.behavior, BehaviorFlag::Enable) ? s.fill : s.textDisabled;
			fillC.a = (Uint8)(fillC.a * s.opacity);
			m_renderer.SetDrawColor(fillC);
			
			// On ne dessine l'arc de remplissage que si la valeur est significative
			if (norm > 0.001f) {
				float endAngle = 135.f + (norm * 270.f);
				if (endAngle > 360.f) {
					m_renderer.RenderArc({cx_, cy_}, iR, 135.f, 360.f);
					m_renderer.RenderArc({cx_, cy_}, iR, 0.f, endAngle - 360.f);
				} else {
					m_renderer.RenderArc({cx_, cy_}, iR, 135.f, endAngle);
				}
			}

			// Point indicateur (Thumb)
			float aRad = (135.f + norm * 270.f) * (SDL::PI_F / 180.f);
			float lx = cx_ + SDL::Cos(aRad) * iR;
			float ly = cy_ + SDL::Sin(aRad) * iR;
			
			_FillRR({lx - 4.f, ly - 4.f, 8.f, 8.f}, fillC, SDL::FCorners(4.f), s.opacity);
		}

		void _DrawImage(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) { 
			auto *d = m_ctx.Get<ImageData>(e);
			if (!d || d->key.empty()) {
				_FillRR(r, s.bgColor, s.radius, s.opacity);
				_StrokeRR(r, s.bdColor, s.borders, s.radius, s.opacity);
				return;
			}
			
			auto texture = _EnsureTexture(d->key);
			if (!texture) {
				_FillRR(r, {60, 20, 20, 200}, s.radius, s.opacity);
				return;
			}

			SDL::Point tsz = texture.GetSize();
			float tw = (float)tsz.x, th = (float)tsz.y;
			FRect dst = r; 
			switch (d->fit) {
				case ImageFit::Fill:
					dst = r;
					break;
				case ImageFit::Contain: {
					float sc = SDL::Min(r.w / tw, r.h / th);
					dst = {r.x + (r.w - tw * sc) * 0.5f, r.y + (r.h - th * sc) * 0.5f, tw * sc, th * sc};
					break;
				}
				case ImageFit::Cover: {
					float sc = SDL::Max(r.w / tw, r.h / th);
					dst = {r.x + (r.w - tw * sc) * 0.5f, r.y + (r.h - th * sc) * 0.5f, tw * sc, th * sc};
					break;
				}
				case ImageFit::None:
					dst = {r.x, r.y, tw, th};
					break;
				case ImageFit::Tile:
					for (float iy = r.y; iy < r.y + r.h; iy += th) {
						for (float ix = r.x; ix < r.x + r.w; ix += tw) {
							FRect td{ix, iy, SDL::Min(tw, r.x + r.w - ix), SDL::Min(th, r.y + r.h - iy)};
							texture.SetAlphaMod(SDL::Clamp8(255 * s.opacity));
							m_renderer.RenderTexture(texture, std::nullopt, td);
						}
					}
					texture.SetAlphaMod(255);
					return;
			}
			texture.SetAlphaMod(SDL::Clamp8(255 * s.opacity));
			m_renderer.RenderTexture(texture, std::nullopt, dst);
			texture.SetAlphaMod(255);
		}
		
		void _DrawCanvas(ECS::EntityId e, const FRect &r) { 
			auto *cd = m_ctx.Get<CanvasData>(e);
			if (!cd || !cd->renderCb) return;

			// The renderCb typically sets its own SDL viewport/scissor.
			// We save/restore the outer clip rect so the UI compositing
			// remains correct after the canvas has drawn into its rect.
			SDL::Rect prevClip = m_renderer.GetClipRect();

			cd->renderCb(m_renderer, r);

			// Restore renderer state for the parent UI layer.
			if (prevClip.w > 0 && prevClip.h > 0)
				m_renderer.SetClipRect(prevClip);
			else
				m_renderer.SetClipRect({});
			m_renderer.SetViewport({});
		}

		// ── ListBox draw ──────────────────────────────────────────────       
		void _DrawListBox(ECS::EntityId e, const FRect &r, const Style &s,
						  const WidgetState &st, const Widget &w) {
			auto *lb  = m_ctx.Get<ListBoxData>(e);
			auto *lp  = m_ctx.Get<LayoutProps>(e);
			if (!lb || !lp) return;

			// Background + focused/hovered border
			SDL::Color bg = st.focused ? s.bgFocused
						  : st.hovered ? s.bgHovered
						  : s.bgColor;
			_FillRR(r, bg, s.radius, s.opacity);
			_StrokeRR(r, st.focused ? s.bdFocused : st.hovered ? s.bdHovered : s.bdColor,
					  s.borders, s.radius, s.opacity);

			const float ih     = lb->itemHeight;
			const float innerW = r.w - lp->padding.left - lp->padding.right;
			const float innerH = r.h - lp->padding.top - lp->padding.bottom;
			const float iy     = r.y + lp->padding.top;
			const float charH  = _TH(e);

			bool showX = false, showY = false;
			_ContainerScrollbars(w, *lp, innerW, innerH, showX, showY);
			
			const float viewH  = showX ? SDL::Max(0.f, innerH - lp->scrollbarThickness) : innerH;
			const float t      = lp->scrollbarThickness;
			const float itemW  = r.w - s.borders.left - s.borders.right - (showY ? t : 0.f);
			const float px     = r.x + lp->padding.left - lp->scrollX;

			// Save current (parent) clip and intersect with content area so items
			// never overflow a scrolled ancestor container.
			SDL::Rect prevClip = m_renderer.GetClipRect();

			SDL::Rect clip = _ClipIntersect(prevClip, {
				(int)r.x + 1,
				(int)iy,
				(int)(r.w - 2.f - (showY ? t : 0.f)),
				(int)viewH
			});
			m_renderer.SetClipRect(clip);

			int firstIdx = SDL::Max(0, (int)(lp->scrollY / ih));
			int lastIdx  = SDL::Min((int)lb->items.size(),
									firstIdx + (int)(viewH / ih) + 2);

			for (int i = firstIdx; i < lastIdx; ++i) {
				float ry    = iy + (float)i * ih - lp->scrollY;
				FRect itemR = {r.x + s.borders.left, ry, itemW, ih};
				bool  isSel = (i == lb->selectedIndex);
				bool  isHov = (m_mousePos.y >= ry && m_mousePos.y < ry + ih
							   && m_mousePos.x >= r.x && m_mousePos.x < r.x + r.w - (showY ? t : 0.f));

				if (isSel) {
					SDL::Color c = s.bgChecked;
					c.a = SDL::Clamp8((int)((float)c.a * s.opacity));
					m_renderer.SetDrawColor(c);
					m_renderer.RenderFillRect(itemR);
				} else if (isHov) {
					SDL::Color c = s.bgHovered;
					c.a = SDL::Clamp8((int)((float)c.a * s.opacity * 0.55f));
					m_renderer.SetDrawColor(c);
					m_renderer.RenderFillRect(itemR);
				}

				SDL::Color tc = isSel ? s.textChecked : s.textColor;
				_Text(e, lb->items[(size_t)i],
					  px, ry + (ih - charH) * 0.5f,
					  tc, s.opacity, s);
			}

			// RESTAURATION DU CLIP PARENT
			if (prevClip.w > 0 && prevClip.h > 0)
				m_renderer.SetClipRect(prevClip);
			else
				m_renderer.SetClipRect({});

			// Dessin des scrollbars (désormais pardessus et hors du clip des items)
			_DrawInlineScrollbars(e, r, s, w);
		}

		// ── Graph draw ────────────────────────────────────────────────
		void _DrawGraph(ECS::EntityId e, const FRect &r, const Style &s,
						const WidgetState &st) {
			auto *gd = m_ctx.Get<GraphData>(e);
			if (!gd) return;

			// Save the outer clip (set by _RenderNode) so we can restore it after
			// clipping to the plot area, and so all sub-clips are properly intersected.
			const SDL::Rect outerClip = m_renderer.GetClipRect();

			_FillRR(r, s.bgColor, s.radius, s.opacity);
			_StrokeRR(r, st.hovered ? s.bdHovered : s.bdColor, s.borders, s.radius, s.opacity);

			const float op    = s.opacity;
			const float charH = _TH(e);

			// Margins: leave space for Y tick labels (left) and X tick labels (bottom).
			float ml = 38.f, mr = 6.f, mt = 6.f, mb = charH + 10.f;
			if (!gd->title.empty())  mt += charH + 4.f;
			if (!gd->yLabel.empty()) ml += charH + 2.f;
			if (!gd->xLabel.empty()) mb += charH + 2.f;

			FRect plot = { r.x + ml, r.y + mt, r.w - ml - mr, r.h - mt - mb };
			if (plot.w < 4.f || plot.h < 4.f) return;

			// Title
			if (!gd->title.empty()) {
				SDL::Color tc = s.textColor;
				float tw = _TW(gd->title, e);
				_Text(e, gd->title, plot.x + (plot.w - tw) * 0.5f, r.y + 2.f, tc, op, s);
			}
			// X label
			if (!gd->xLabel.empty()) {
				SDL::Color tc = s.textColor;
				float tw = _TW(gd->xLabel, e);
				_Text(e, gd->xLabel, plot.x + (plot.w - tw) * 0.5f,
					  r.y + r.h - charH - 2.f, tc, op, s);
			}
			// Y label — one character per line (vertical text approximation)
			if (!gd->yLabel.empty()) {
				SDL::Color ac = gd->axisColor; ac.a = SDL::Clamp8((int)((float)ac.a * op));
				float totalH  = charH * (float)gd->yLabel.size();
				float startY  = plot.y + (plot.h - totalH) * 0.5f;
				for (int ci = 0; ci < (int)gd->yLabel.size(); ++ci) {
					std::string ch(1, gd->yLabel[(size_t)ci]);
					_Text(e, ch, r.x + 2.f, startY + (float)ci * charH, ac, op, s);
				}
			}

			// Plot background
			m_renderer.SetDrawColor({12, 13, 20, (uint8_t)(220 * op)});
			m_renderer.RenderFillRect(plot);

			float range = gd->maxVal - gd->minVal;
			if (range == 0.f) range = 1.f;

			// ── Grid & Y-axis tick labels ────────────────────────────────────────
			for (int i = 0; i <= gd->yDivisions; ++i) {
				float t   = (float)i / (float)gd->yDivisions;
				float gy  = plot.y + plot.h * t;
				float val = gd->maxVal - t * range;

				SDL::Color gc = gd->gridColor; gc.a = SDL::Clamp8((int)((float)gc.a * op));
				m_renderer.SetDrawColor(gc);
				m_renderer.RenderLine(SDL::FPoint{plot.x, gy}, SDL::FPoint{plot.x + plot.w, gy});

				std::string lbl;
				float absVal = std::abs(val);
				if (absVal >= 10000.f)      lbl = std::format("{:.0f}k", val / 1000.f);
				else if (absVal >= 1000.f)  lbl = std::format("{:.1f}k", val / 1000.f);
				else if (absVal >= 10.f)    lbl = std::format("{:.0f}", val);
				else if (absVal >= 0.01f)   lbl = std::format("{:.2f}", val);
				else                        lbl = std::format("{:.0f}", val);

				SDL::Color ac = gd->axisColor; ac.a = SDL::Clamp8((int)((float)ac.a * op));
				float tw = _TW(lbl, e);
				_Text(e, lbl, plot.x - tw - 3.f, gy - charH * 0.5f, ac, op, s);
			}

			// ── Grid & X-axis tick labels ────────────────────────────────────────
			for (int i = 0; i <= gd->xDivisions; ++i) {
				float t    = (float)i / (float)gd->xDivisions;
				float gx   = plot.x + plot.w * t;
				float xval = gd->xMin + t * (gd->xMax - gd->xMin);

				if (gd->logFreq && gd->xMin > 0.f && gd->xMax > gd->xMin) {
					float logMin = std::log10(gd->xMin);
					float logMax = std::log10(gd->xMax);
					xval = std::pow(10.f, logMin + t * (logMax - logMin));
					gx   = plot.x + t * plot.w;
				}

				SDL::Color gc = gd->gridColor; gc.a = SDL::Clamp8((int)((float)gc.a * op));
				m_renderer.SetDrawColor(gc);
				m_renderer.RenderLine(SDL::FPoint{gx, plot.y}, SDL::FPoint{gx, plot.y + plot.h});

				std::string lbl;
				float absX = std::abs(xval);
				if (absX >= 10000.f)       lbl = std::format("{:.0f}k", xval / 1000.f);
				else if (absX >= 1000.f)   lbl = std::format("{:.1f}k", xval / 1000.f);
				else if (absX >= 1.f)      lbl = std::format("{:.0f}", xval);
				else if (absX >= 0.01f)    lbl = std::format("{:.2f}", xval);
				else                       lbl = "0";

				SDL::Color ac = gd->axisColor; ac.a = SDL::Clamp8((int)((float)ac.a * op));
				float tw = _TW(lbl, e);
				_Text(e, lbl, gx - tw * 0.5f, plot.y + plot.h + 3.f, ac, op, s);
			}

			if (gd->data.empty()) return;

			// Clip data rendering to the plot area, intersected with the outer widget
			// clip so graph content never overflows a scrolled ancestor container.
			m_renderer.SetClipRect(_ClipIntersect(outerClip, FRectToRect(plot)));

			int n = (int)gd->data.size();

			auto yScr = [&](float v) -> float {
				return plot.y + plot.h * (1.f - (SDL::Clamp(v, gd->minVal, gd->maxVal) - gd->minVal) / range);
			};

			// Map data index → screen X, respecting log scale
			auto xScr = [&](int i) -> float {
				float t = (n > 1) ? (float)i / (float)(n - 1) : 0.f;
				if (gd->logFreq && gd->xMin > 0.f && gd->xMax > gd->xMin) {
					// Map freq linearly through log space
					float freqAtI = gd->xMin + (gd->xMax - gd->xMin) * t;
					float logMin  = std::log10(gd->xMin);
					float logMax  = std::log10(gd->xMax);
					float logFreq = (freqAtI > 0.f) ? std::log10(freqAtI) : logMin;
					t = (logFreq - logMin) / (logMax - logMin);
				}
				return plot.x + t * plot.w;
			};

			if (gd->barMode) {
				// ── Bar mode (spectrum) ───────────────────────────────────────────
				SDL::Color fc = gd->fillColor;  fc.a = SDL::Clamp8((int)((float)fc.a * op));
				SDL::Color lc = gd->lineColor;  lc.a = SDL::Clamp8((int)((float)lc.a * op));
				float barW = SDL::Max(1.f, plot.w / (float)n);
				for (int i = 0; i < n; ++i) {
					float x1 = plot.x + barW * (float)i;
					float y1 = yScr(gd->data[(size_t)i]);
					float y2 = plot.y + plot.h;
					m_renderer.SetDrawColor(fc);
					m_renderer.RenderFillRect(SDL::FRect{x1, y1, SDL::Max(1.f, barW - 1.f), y2 - y1});
					if (barW > 2.f) {
						m_renderer.SetDrawColor(lc);
						m_renderer.RenderFillRect(SDL::FRect{x1, y1, SDL::Max(1.f, barW - 1.f), 1.f});
					}
				}
			} else {
				// ── Line / fill mode (waveform) ───────────────────────────────────
				float baseline = yScr(SDL::Clamp(0.f, gd->minVal, gd->maxVal));

				if (gd->showFill) {
					SDL::Color fc = gd->fillColor; fc.a = SDL::Clamp8((int)((float)fc.a * op));
					m_renderer.SetDrawColor(fc);
					for (int i = 0; i < n - 1; ++i) {
						float x1 = xScr(i),     y1 = yScr(gd->data[(size_t)i]);
						float x2 = xScr(i + 1), y2 = yScr(gd->data[(size_t)(i + 1)]);
						// Vertical fill strip between curve and baseline
						float top = SDL::Min(SDL::Min(y1, y2), baseline);
						float bot = SDL::Max(SDL::Max(y1, y2), baseline);
						m_renderer.RenderFillRect(SDL::FRect{x1, top, SDL::Max(1.f, x2 - x1), bot - top});
					}
				}
				// Line
				SDL::Color lc = gd->lineColor; lc.a = SDL::Clamp8((int)((float)lc.a * op));
				m_renderer.SetDrawColor(lc);
				for (int i = 0; i < n - 1; ++i) {
					float x1 = xScr(i),     y1 = yScr(gd->data[(size_t)i]);
					float x2 = xScr(i + 1), y2 = yScr(gd->data[(size_t)(i + 1)]);
					m_renderer.RenderLine(SDL::FPoint{x1, y1}, SDL::FPoint{x2, y2});
				}
			}

			// Restore the outer widget clip.
			m_renderer.SetClipRect(outerClip);
		}

		// ── ListBox keyboard handler ──────────────────────────────────
		void _HandleKeyDownListBox(SDL::Keycode k) {
			auto *lb  = m_ctx.Get<ListBoxData>(m_focused); 
			auto *cb  = m_ctx.Get<Callbacks>(m_focused);
			auto *cr  = m_ctx.Get<ComputedRect>(m_focused);
			auto *lp  = m_ctx.Get<LayoutProps>(m_focused);
			if (!lb || lb->items.empty()) return;

			int prev = lb->selectedIndex;
			int n    = (int)lb->items.size();
			switch (k) { 
				case SDL::KEYCODE_UP:
					lb->selectedIndex = (lb->selectedIndex > 0) ? lb->selectedIndex - 1
									  : (prev < 0)              ? 0 : 0;
					break;
				case SDL::KEYCODE_DOWN:
					lb->selectedIndex = (lb->selectedIndex < 0)       ? 0
									  : (lb->selectedIndex < n - 1)   ? lb->selectedIndex + 1
									  : n - 1;
					break;
				case SDL::KEYCODE_HOME: lb->selectedIndex = 0;     break;
				case SDL::KEYCODE_END:  lb->selectedIndex = n - 1; break;
				case SDL::KEYCODE_RETURN:
				case SDL::KEYCODE_RETURN2:
				case SDL::KEYCODE_KP_ENTER:
					if (lb->selectedIndex >= 0 && cb && cb->onClick) cb->onClick();
					return;
				case SDL::KEYCODE_ESCAPE:
					_SetFocus(ECS::NullEntity);
					return;
				default: return;
			}
			// Auto-scroll to keep selection visible
			if (cr && lp && lb->selectedIndex >= 0) {
				float viewH = cr->screen.h - lp->padding.top - lp->padding.bottom;
				float itemY = (float)lb->selectedIndex * lb->itemHeight;
				if (itemY < lp->scrollY)
					lp->scrollY = itemY;
				else if (itemY + lb->itemHeight > lp->scrollY + viewH)
					lp->scrollY = SDL::Max(0.f, itemY + lb->itemHeight - viewH);
			}
			if (lb->selectedIndex != prev && cb && cb->onChange)
				cb->onChange((float)lb->selectedIndex);
		}
	};

	// =============================================================================
	// Builder
	// =============================================================================

	/**
	 * @brief Fluent builder DSL wrapping a widget entity.
	 *
	 * Every setter returns `*this` so calls can be chained:
	 * ```cpp
	 * ui.Button("ok", "OK")
	 *   .W(100).H(36)
	 *   .BgColor({70,130,210,255})
	 *   .OnClick([]{ ... })
	 *   .AttachTo(root);
	 * ```
	 */
	struct Builder {
		System &sys;
		ECS::EntityId id;
		Builder(System &s, ECS::EntityId e) : sys(s), id(e) {}
		/** @brief Implicit conversion to the underlying entity ID. */
		operator ECS::EntityId() const noexcept { return id; }
		/** @brief Return the underlying entity ID. */
		[[nodiscard]] ECS::EntityId Id() const noexcept { return id; }

		// Style
		/** @brief Replace the entire Style component with @p s.
		 *  @return Reference to this builder for chaining. */
		Builder &Style(const Style &s) {
			sys.GetStyle(id) = s;
			return *this;
		}
		/** @brief Apply an inline lambda to the Style component. */
		Builder &WithStyle(std::function<void(UI::Style &)> fn) {
			fn(sys.GetStyle(id));
			return *this;
		}
		/** @brief Set the background color (normal state). */
		Builder &BgColor(SDL::Color c) {
			sys.GetStyle(id).bgColor = c;
			return *this;
		}
		/** @brief Set the background color when the widget is hovered. */
		Builder &BgHover(SDL::Color c) {
			sys.GetStyle(id).bgHovered = c;
			return *this;
		}
		/** @brief Set the background color when the widget is pressed. */
		Builder &BgPress(SDL::Color c) {
			sys.GetStyle(id).bgPressed = c;
			return *this;
		}
		/** @brief Set the background color when the widget is checked (Toggle, selected ListBox item). */
		Builder &BgCheck(SDL::Color c) {
			sys.GetStyle(id).bgChecked = c;
			return *this;
		}
		/** @brief Set the border color (normal state). */
		Builder &BorderColor(SDL::Color c) {
			sys.GetStyle(id).bdColor = c;
			return *this;
		}
		/** @brief Set the left border thickness in pixels. */
		Builder &BorderLeft(float w) {
			sys.GetStyle(id).borders.left = w;
			return *this;
		}
		/** @brief Set the right border thickness in pixels. */
		Builder &BorderRight(float w) {
			sys.GetStyle(id).borders.right = w;
			return *this;
		}
		/** @brief Set the top border thickness in pixels. */
		Builder &BorderTop(float w) {
			sys.GetStyle(id).borders.top = w;
			return *this;
		}
		/** @brief Set the bottom border thickness in pixels. */
		Builder &BorderBottom(float w) {
			sys.GetStyle(id).borders.bottom = w;
			return *this;
		}
		/** @brief Set all four border thicknesses at once. */
		Builder &Borders(SDL::FBox bd) {
			sys.GetStyle(id).borders = bd;
			return *this;
		}

		/** @brief Set the top-left corner radius. */
		Builder &RadiusTopLeft(float r) {
			sys.GetStyle(id).radius.tl = r;
			return *this;
		}
		/** @brief Set the top-right corner radius. */
		Builder &RadiusTopRight(float r) {
			sys.GetStyle(id).radius.tr = r;
			return *this;
		}
		/** @brief Set the bottom-left corner radius. */
		Builder &RadiusBottomLeft(float r) {
			sys.GetStyle(id).radius.bl = r;
			return *this;
		}
		/** @brief Set the bottom-right corner radius. */
		Builder &RadiusBottomRight(float r) {
			sys.GetStyle(id).radius.br = r;
			return *this;
		}
		/** @brief Set all four corner radii at once. */
		Builder &Radius(SDL::FCorners rad) {
			sys.GetStyle(id).radius = rad;
			return *this;
		}

		/** @brief Set the text color (normal state). */
		Builder &TextColor(SDL::Color c) {
			sys.GetStyle(id).textColor = c;
			return *this;
		}
		/** @brief Set the fill color (Slider track fill, Toggle active state, etc.). */
		Builder &FillColor(SDL::Color c) {
			sys.GetStyle(id).fill = c;
			return *this;
		}
		/** @brief Set the track color (Slider / ScrollBar background rail). */
		Builder &TrackColor(SDL::Color c) {
			sys.GetStyle(id).track = c;
			return *this;
		}
		/** @brief Set the thumb color (Slider handle, ScrollBar thumb when hovered). */
		Builder &ThumbColor(SDL::Color c) {
			sys.GetStyle(id).thumb = c;
			return *this;
		}
		/** @brief Set the overall opacity multiplier [0..1] applied to all rendering. */
		Builder &Opacity(float o) {
			sys.GetStyle(id).opacity = o;
			return *this;
		}

		/** @brief Set the texture key and fit mode for an Image widget. */
		Builder &ImageKey(const std::string &key, ImageFit f = ImageFit::Contain) {
			sys.SetImageKey(id, key, f);
			return *this;
		}

		/// Attach an icon texture to a Button (or any widget rendered by _DrawButton).
		/// The icon is drawn to the left of any text label, or centred when no text is set.
		/// @param key  Resource-pool key of the texture.
		/// @param pad  Inset from widget edges in pixels (default 4).
		Builder &Icon(const std::string &key, float pad = 4.f) {
			auto &ic = sys.GetOrAddIconData(id);
			ic.key = key;
			ic.pad = pad;
			return *this;
		}

		/// Set the icon opacity for each interactive state (values in [0, 1]).
		Builder &IconOpacity(float normal, float hovered = 1.f,
							 float pressed = 0.85f, float disabled = 0.35f) {
			auto &ic = sys.GetOrAddIconData(id);
			ic.opacityNormal   = normal;
			ic.opacityHovered  = hovered;
			ic.opacityPressed  = pressed;
			ic.opacityDisabled = disabled;
			return *this;
		}

		/// Set the icon color modulation (tint) for each interactive state.
		Builder &IconTint(SDL::Color normal,
						  SDL::Color hovered  = {255, 255, 255, 255},
						  SDL::Color pressed  = {220, 220, 220, 255},
						  SDL::Color disabled = {180, 180, 180, 255}) {
			auto &ic = sys.GetOrAddIconData(id);
			ic.tintNormal   = normal;
			ic.tintHovered  = hovered;
			ic.tintPressed  = pressed;
			ic.tintDisabled = disabled;
			return *this;
		}

		/** @brief Set own font key and point size; sets FontType::Self. Pass empty key to force debug font. */
		Builder &Font(const std::string &key, float sz = 0.f) {
			auto &s = sys.GetStyle(id);
			s.fontKey  = key;
			s.usedFont = key.empty() ? FontType::Debug : FontType::Self;
			if (sz > 0.f) s.fontSize = sz;
			return *this;
		}
		/** @brief Set the font point size without changing the font key or FontType. */
		Builder &FontSize(float sz) {
			sys.GetStyle(id).fontSize = sz;
			return *this;
		}
		/** @brief Control how the font is resolved for this widget. */
		Builder &FontUsage(FontType ft) {
			sys.GetStyle(id).usedFont = ft;
			return *this;
		}
		/** @brief Inherit the font from the nearest ancestor configured with FontType::Self. */
		Builder &UseInheritedFont() {
			sys.GetStyle(id).usedFont = FontType::Inherited;
			return *this;
		}
		/** @brief Use the system-level default font (System::SetDefaultFont). */
		Builder &UseDefaultFont() {
			sys.GetStyle(id).usedFont = FontType::Default;
			return *this;
		}
		/** @brief Always use the SDL3 built-in debug font regardless of parent fonts. */
		Builder &UseDebugFont() {
			sys.GetStyle(id).usedFont = FontType::Debug;
			return *this;
		}
		/** @brief Use the root widget's configured font. */
		Builder &UseRootFont() {
			sys.GetStyle(id).usedFont = FontType::Root;
			return *this;
		}

		/** @brief Set the audio clip played on click. */
		Builder &ClickSound(const std::string &key) {
			sys.GetStyle(id).clickSound = key;
			return *this;
		}
		/** @brief Set the audio clip played on hover-enter. */
		Builder &HoverSound(const std::string &key) {
			sys.GetStyle(id).hoverSound = key;
			return *this;
		}
		/** @brief Set the audio clip played when the widget is scrolled. */
		Builder &ScrollSound(const std::string &key) {
			sys.GetStyle(id).scrollSound = key;
			return *this;
		}
		/** @brief Set the audio clip played when the widget becomes visible. */
		Builder &ShowSound(const std::string &key) {
			sys.GetStyle(id).showSound = key;
			return *this;
		}
		/** @brief Set the audio clip played when the widget is hidden. */
		Builder &HideSound(const std::string &key) {
			sys.GetStyle(id).hideSound = key;
			return *this;
		}

		// Layout
		/** @brief Apply an inline lambda to the LayoutProps component. */
		Builder &WithLayout(std::function<void(LayoutProps &)> fn) {
			fn(sys.GetLayout(id));
			return *this;
		}
		/** @brief Set the absolute X position (pixels) — only meaningful with Absolute/Fixed attach. */
		Builder &X(float px) {
			sys.GetLayout(id).absX = Value::Px(px);
			return *this;
		}
		/** @brief Set the absolute X position via a Value — only meaningful with Absolute/Fixed attach. */
		Builder &X(Value v) {
			sys.GetLayout(id).absX = v;
			return *this;
		}
		/** @brief Set the absolute Y position (pixels) — only meaningful with Absolute/Fixed attach. */
		Builder &Y(float px) {
			sys.GetLayout(id).absY = Value::Px(px);
			return *this;
		}
		/** @brief Set the absolute Y position via a Value — only meaningful with Absolute/Fixed attach. */
		Builder &Y(Value v) {
			sys.GetLayout(id).absY = v;
			return *this;
		}
		/** @brief Set the widget width (pixels). */
		Builder &W(float px) {
			sys.GetLayout(id).width = Value::Px(px);
			return *this;
		}
		/** @brief Set the widget width via a Value (Px, Pw, Grow, etc.). */
		Builder &W(Value v) {
			sys.GetLayout(id).width = v;
			return *this;
		}
		/** @brief Set the widget height (pixels). */
		Builder &H(float px) {
			sys.GetLayout(id).height = Value::Px(px);
			return *this;
		}
		/** @brief Set the widget height via a Value (Px, Ph, Grow, etc.). */
		Builder &H(Value v) {
			sys.GetLayout(id).height = v;
			return *this;
		}
		/** @brief Set the minimum width (pixels). */
		Builder &MinW(float px) { sys.GetLayout(id).minWidth  = Value::Px(px); return *this; }
		/** @brief Set the minimum width via a Value. */
		Builder &MinW(Value  v) { sys.GetLayout(id).minWidth  = v;             return *this; }
		/** @brief Set the minimum height (pixels). */
		Builder &MinH(float px) { sys.GetLayout(id).minHeight = Value::Px(px); return *this; }
		/** @brief Set the minimum height via a Value. */
		Builder &MinH(Value  v) { sys.GetLayout(id).minHeight = v;             return *this; }
		/** @brief Set the maximum width (pixels). */
		Builder &MaxW(float px) { sys.GetLayout(id).maxWidth  = Value::Px(px); return *this; }
		/** @brief Set the maximum width via a Value. */
		Builder &MaxW(Value  v) { sys.GetLayout(id).maxWidth  = v;             return *this; }
		/** @brief Set the maximum height (pixels). */
		Builder &MaxH(float px) { sys.GetLayout(id).maxHeight = Value::Px(px); return *this; }
		/** @brief Set the maximum height via a Value. */
		Builder &MaxH(Value  v) { sys.GetLayout(id).maxHeight = v;             return *this; }
		/** @brief Grow the widget's width to fill remaining horizontal space (InLine parents only). */
		Builder &GrowW(float g) { W(Value::Grow(g)); return *this; }
		/** @brief Grow the widget's height to fill remaining vertical space (InColumn parents only). */
		Builder &GrowH(float g) { H(Value::Grow(g)); return *this; }
		/** @brief Grow both width and height to fill remaining space. */
		Builder &Grow(float g) { W(Value::Grow(g)); H(Value::Grow(g)); return *this; }
		/** @brief Set the gap between children in pixel (InColumn / InLine / Stack layouts). */
		Builder &Gap(float g) {
			sys.GetLayout(id).gap = g;
			return *this;
		}
		/** @brief Set the layout mode for a container widget. */
		Builder &Layout(Layout l) {
			sys.GetLayout(id).layout = l;
			return *this;
		}
		/** @brief Set the default horizontal alignment for direct children (InColumn containers). */
		Builder &AlignChildrenH(UI::Align a) {
			sys.GetLayout(id).alignChildrenH = a;
			return *this;
		}
		/** @brief Set the default vertical alignment for direct children (InLine / Stack containers). */
		Builder &AlignChildrenV(UI::Align a) {
			sys.GetLayout(id).alignChildrenV = a;
			return *this;
		}
		/** @brief Set both horizontal and vertical default child alignment at once. */
		Builder &AlignChildren(UI::Align h, UI::Align v) {
			auto &lp = sys.GetLayout(id);
			lp.alignChildrenH = h;
			lp.alignChildrenV = v;
			return *this;
		}
		/** @brief Set this widget's own horizontal alignment within its parent. */
		Builder &AlignH(UI::Align a) {
			sys.GetLayout(id).alignSelfH = a;
			return *this;
		}
		/** @brief Set this widget's own vertical alignment within its parent. */
		Builder &AlignV(UI::Align a) {
			sys.GetLayout(id).alignSelfV = a;
			return *this;
		}
		/** @brief Set both horizontal and vertical self-alignment at once. */
		Builder &Align(UI::Align h, UI::Align v) {
			auto &lp = sys.GetLayout(id);
			lp.alignSelfH = h;
			lp.alignSelfV = v;
			return *this;
		}
		/** @brief Set the CSS-style box-sizing mode (what W/H dimensions include). */
		Builder &BoxSizingMode(BoxSizing b) {
			sys.GetLayout(id).boxSizing = b;
			return *this;
		}
		/** @brief Set the attach mode (Relative, Absolute, or Fixed). */
		Builder &Attach(AttachLayout a) {
			sys.GetLayout(id).attach = a;
			return *this;
		}
		/** @brief Position the widget absolutely within its parent using Values. */
		Builder &Absolute(Value x, Value y) {
			auto &l = sys.GetLayout(id);
			l.attach = AttachLayout::Absolute;
			l.absX = x;
			l.absY = y;
			return *this;
		}
		/** @brief Position the widget absolutely within its parent using pixel coordinates. */
		Builder &Absolute(float x, float y) { return Absolute(Value::Px(x), Value::Px(y)); }
		/** @brief Position the widget at a fixed position relative to the root viewport using Values. */
		Builder &Fixed(Value x, Value y) {
			auto &l = sys.GetLayout(id);
			l.attach = AttachLayout::Fixed;
			l.absX = x;
			l.absY = y;
			return *this;
		}

		/** @brief Set uniform padding on all four sides (pixels). */
		Builder &Padding(float a) {
			auto &l = sys.GetLayout(id);
			l.padding.left = l.padding.top = l.padding.right = l.padding.bottom = a;
			return *this;
		}
		/** @brief Set horizontal and vertical padding independently (pixels). */
		Builder &Padding(float h, float v) {
			auto &l = sys.GetLayout(id);
			l.padding.left = l.padding.right = h;
			l.padding.top = l.padding.bottom = v;
			return *this;
		}
		/** @brief Set padding on all four sides individually. */
		Builder &Padding(SDL::FBox p) {
			auto &l = sys.GetLayout(id);
			l.padding = p;
			return *this;
		}
		/** @brief Set left and right padding to the same value (pixels). */
		Builder &PaddingH(float h) {
			auto &l = sys.GetLayout(id);
			l.padding.left = l.padding.right = h;
			return *this;
		}
		/** @brief Set top and bottom padding to the same value (pixels). */
		Builder &PaddingV(float v) {
			auto &l = sys.GetLayout(id);
			l.padding.top = l.padding.bottom = v;
			return *this;
		}
		/** @brief Set the bottom padding (pixels). */
		Builder &PaddingBottom(float v) {
			sys.GetLayout(id).padding.bottom = v;
			return *this;
		}
		/** @brief Set the top padding (pixels). */
		Builder &PaddingTop(float v) {
			sys.GetLayout(id).padding.top = v;
			return *this;
		}
		/** @brief Set the left padding (pixels). */
		Builder &PaddingLeft(float v) {
			sys.GetLayout(id).padding.left = v;
			return *this;
		}
		/** @brief Set the right padding (pixels). */
		Builder &PaddingRight(float v) {
			sys.GetLayout(id).padding.right = v;
			return *this;
		}

		/** @brief Set uniform margin on all four sides (pixels). */
		Builder &Margin(float a) {
			auto &l = sys.GetLayout(id);
			l.margin.left = l.margin.top = l.margin.right = l.margin.bottom = a;
			return *this;
		}
		/** @brief Set horizontal and vertical margins independently (pixels). */
		Builder &Margin(float h, float v) {
			auto &l = sys.GetLayout(id);
			l.margin.left = l.margin.right = h;
			l.margin.top = l.margin.bottom = v;
			return *this;
		}
		/** @brief Set margin on all four sides individually. */
		Builder &Margin(SDL::FBox m) {
			auto &l = sys.GetLayout(id);
			l.margin = m;
			return *this;
		}
		/** @brief Set left and right margins to the same value (pixels). */
		Builder &MarginH(float h) {
			auto &l = sys.GetLayout(id);
			l.margin.left = l.margin.right = h;
			return *this;
		}
		/** @brief Set top and bottom margins to the same value (pixels). */
		Builder &MarginV(float v) {
			auto &l = sys.GetLayout(id);
			l.margin.top = l.margin.bottom = v;
			return *this;
		}
		/** @brief Set the bottom margin (pixels). */
		Builder &MarginBottom(float v) {
			sys.GetLayout(id).margin.bottom = v;
			return *this;
		}
		/** @brief Set the top margin (pixels). */
		Builder &MarginTop(float v) {
			sys.GetLayout(id).margin.top = v;
			return *this;
		}
		/** @brief Set the left margin (pixels). */
		Builder &MarginLeft(float v) {
			sys.GetLayout(id).margin.left = v;
			return *this;
		}
		/** @brief Set the right margin (pixels). */
		Builder &MarginRight(float v) {
			sys.GetLayout(id).margin.right = v;
			return *this;
		}

		// BehaviorFlag
		/** @brief Enable or disable the widget (disabled = not interactive, grayed visually). */
		Builder &Enable(bool e = true) {
			sys.SetEnabled(id, e);
			return *this;
		}
		/** @brief Disable the widget (shorthand for Enable(false)). */
		Builder &Disable() {
			sys.SetEnabled(id, false);
			return *this;
		}
		/** @brief Show or hide the widget (hidden = takes no space, receives no input). */
		Builder &Visible(bool v = true) {
			sys.SetVisible(id, v);
			return *this;
		}
		/** @brief Hide the widget (shorthand for Visible(false)). */
		Builder &Hide() {
			sys.SetVisible(id, false);
			return *this;
		}
		/** @brief Show the widget (shorthand for Visible(true)). */
		Builder &Show() {
			sys.SetVisible(id, true);
			return *this;
		}
		/** @brief Enable or disable hover detection on the widget. */
		Builder &Hoverable(bool v = true) {
			sys.SetHoverable(id, v);
			return *this;
		}
		/** @brief Enable or disable mouse-click selection on the widget. */
		Builder &Selectable(bool v = true) {
			sys.SetSelectable(id, v);
			return *this;
		}
		/** @brief Enable or disable keyboard focus on the widget. */
		Builder &Focusable(bool v = true) {
			sys.SetFocusable(id, v);
			return *this;
		}
		/** @brief Enable or disable the permanent horizontal scrollbar. */
		Builder &ScrollableX(bool b = true) {
			sys.SetScrollableX(id, b);
			return *this;
		}
		/** @brief Enable or disable the permanent vertical scrollbar. */
		Builder &ScrollableY(bool b = true) {
			sys.SetScrollableY(id, b);
			return *this;
		}
		/** @brief Shorthand to set both permanent scrollbar axes at once. */
		Builder &Scrollable(bool bx = true, bool by = true) {
			sys.SetScrollable(id, bx, by);
			return *this;
		}
		/// Scrollbar horizontal automatique : visible seulement si le contenu déborde.
		Builder &AutoScrollableX(bool b = true) {
			sys.SetAutoScrollableX(id, b);
			return *this;
		}
		/// Scrollbar vertical automatique : visible seulement si le contenu déborde.
		Builder &AutoScrollableY(bool b = true) {
			sys.SetAutoScrollableY(id, b);
			return *this;
		}
		/// Active les deux scrollbars automatiques.
		Builder &AutoScrollable(bool bx = true, bool by = true) {
			sys.SetAutoScrollable(id, bx, by);
			return *this;
		}
		/// Épaisseur en pixels des scrollbars inline (défaut : 8 px).
		Builder &ScrollbarThickness(float t) {
			sys.SetScrollbarThickness(id, t);
			return *this;
		}

		// ── Grid layout (container) ───────────────────────────────────────────────

		/// Number of columns in a Layout::InGrid container.
		Builder &GridCols(int n) { sys.SetGridCols(id, n); return *this; }
		/// Fixed number of rows (0 = auto-computed from children).
		Builder &GridRows(int n) { sys.SetGridRows(id, n); return *this; }
		/// Column sizing mode (Fixed = equal width, Content = fit content).
		Builder &GridColSizing(GridSizing s) { sys.SetGridColSizing(id, s); return *this; }
		/// Row sizing mode (Fixed = equal height, Content = fit content).
		Builder &GridRowSizing(GridSizing s) { sys.SetGridRowSizing(id, s); return *this; }
		/// Shorthand to set both column and row sizing at once.
		Builder &GridSizingMode(GridSizing cs, GridSizing rs) {
			sys.SetGridColSizing(id, cs);
			sys.SetGridRowSizing(id, rs);
			return *this;
		}
		/// Which separator lines to draw between cells.
		Builder &GridLineStyle(GridLines l) { sys.SetGridLines(id, l); return *this; }
		/// Color of the separator lines.
		Builder &GridLineColor(SDL::Color c) { sys.SetGridLineColor(id, c); return *this; }
		/// Thickness in pixels of the separator lines.
		Builder &GridLineThickness(float t) { sys.SetGridLineThickness(id, t); return *this; }

		// ── Grid cell (child) ─────────────────────────────────────────────────────

		/// Place this widget at the given grid cell.  colSpan/rowSpan default to 1.
		Builder &Cell(int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(id, col, row, colSpan, rowSpan);
			return *this;
		}

		/** @brief Set the text content of the widget at runtime. */
		Builder &SetText(const std::string &t) {
			sys.SetText(id, t);
			return *this;
		}
		/** @brief Set the numeric value of a Slider or Knob widget at runtime (clamped). */
		Builder &SetValue(float v) {
			sys.SetValue(id, v);
			return *this;
		}
		/** @brief Set the checked state of a Toggle or RadioButton widget at runtime. */
		Builder &Check(bool c = true) {
			sys.SetChecked(id, c);
			return *this;
		}

		// Callbacks
		/** @brief Register (or replace) the render callback on a Canvas widget. */
		Builder &OnRenderCanvas(std::function<void(RendererRef, FRect)> cb) {
			sys.OnRenderCanvas(id, std::move(cb));
			return *this;
		}

		// TextArea
		/** @brief Replace the document content of a TextArea widget. */
		Builder &TextAreaContent(const std::string &t) {
			sys.SetTextAreaContent(id, t);
			return *this;
		}
		/** @brief Set the selection highlight color of a TextArea widget. */
		Builder &TextAreaHighlightColor(SDL::Color c) {
			sys.SetTextAreaHighlightColor(id, c);
			return *this;
		}
		/** @brief Set the tab-stop size (in character columns) for a TextArea widget. */
		Builder &TextAreaTabSize(int n) {
			sys.SetTextAreaTabSize(id, n);
			return *this;
		}
		/** @brief Add a rich-text span to a TextArea widget (see AddTextAreaSpan). */
		Builder &TextAreaSpan(int start, int end, TextSpanStyle style) {
			sys.AddTextAreaSpan(id, start, end, style);
			return *this;
		}
		/** @brief Set or clear the read-only mode of a TextArea widget. */
		Builder &ReadOnly(bool ro = true) {
			sys.SetTextAreaReadOnly(id, ro);
			return *this;
		}

		// ── Tooltip ───────────────────────────────────────────────────────────────────

		/// Attache une info-bulle à ce widget (affichée après @p delay secondes de survol).
		Builder &Tooltip(const std::string &text, float delay = 1.f) {
			sys.SetTooltip(id, text, delay);
			return *this;
		}
		/// Couleur de fond de l'info-bulle (héritée du widget survolé).
		Builder &TooltipBg(SDL::Color c) {
			sys.GetStyle(id).tooltipBg = c;
			return *this;
		}
		/// Couleur de bordure de l'info-bulle.
		Builder &TooltipBd(SDL::Color c) {
			sys.GetStyle(id).tooltipBd = c;
			return *this;
		}
		/// Couleur du texte de l'info-bulle.
		Builder &TooltipTextColor(SDL::Color c) {
			sys.GetStyle(id).tooltipText = c;
			return *this;
		}

		/// Attach a 9-slice tileset skin to this widget.
		Builder &TilesetSkin(TilesetStyle ts) {
			sys.SetTilesetStyle(id, std::move(ts));
			return *this;
		}

		/// Remove a previously attached tileset skin.
		Builder &RemoveTilesetSkin() {
			sys.RemoveTilesetStyle(id);
			return *this;
		}

		/** @brief Register a callback invoked when the widget is clicked. */
		Builder &OnClick(std::function<void()> cb) {
			sys.OnClick(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked on double-click. For Input/TextArea it fires after the automatic selection. */
		Builder &OnDoubleClick(std::function<void()> cb) {
			sys.OnDoubleClick(id, std::move(cb));
			return *this;
		}
		/** @brief When set to false, unhandled scroll/wheel events are not propagated to parent widgets. */
		Builder &DispatchEvent(bool b) {
			sys.SetDispatchEvent(id, b);
			return *this;
		}
		/**
		 * @brief Register a callback invoked when the widget's numeric value changes.
		 * @param cb  Receives the new value (Slider, Knob, ScrollBar, etc.).
		 */
		Builder &OnChange(std::function<void(float)> cb) {
			sys.OnChange(id, std::move(cb));
			return *this;
		}
		/**
		 * @brief Register a callback invoked when the widget's text content changes.
		 * @param cb  Receives the new text string (Input, TextArea, etc.).
		 */
		Builder &OnTextChange(std::function<void(const std::string &)> cb) {
			sys.OnTextChange(id, std::move(cb));
			return *this;
		}
		/**
		 * @brief Register a callback invoked when a Toggle or RadioButton changes state.
		 * @param cb  Receives `true` if the widget is now checked.
		 */
		Builder &OnToggle(std::function<void(bool)> cb) {
			sys.OnToggle(id, std::move(cb));
			return *this;
		}
		/**
		 * @brief Register a callback invoked when the widget is scrolled.
		 * @param cb  Receives the scroll delta.
		 */
		Builder &OnScroll(std::function<void(float)> cb) {
			sys.OnScroll(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the mouse cursor enters the widget. */
		Builder &OnHoverEnter(std::function<void()> cb) {
			sys.OnHoverEnter(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the mouse cursor leaves the widget. */
		Builder &OnHoverLeave(std::function<void()> cb) {
			sys.OnHoverLeave(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the widget gains keyboard focus. */
		Builder &OnFocusGain(std::function<void()> cb) {
			sys.OnFocusGain(id, std::move(cb));
			return *this;
		}
		/** @brief Register a callback invoked when the widget loses keyboard focus. */
		Builder &OnFocusLose(std::function<void()> cb) {
			sys.OnFocusLose(id, std::move(cb));
			return *this;
		}

		// Children
		/** @brief Append a child entity to this container. */
		Builder &Child(ECS::EntityId c) {
			sys.AppendChild(id, c);
			return *this;
		}
		/// Append a child and pin it to a grid cell (shorthand for child.Cell(...)).
		/// Only meaningful when this container uses Layout::InGrid.
		Builder &Child(ECS::EntityId c, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(c, col, row, colSpan, rowSpan);
			sys.AppendChild(id, c);
			return *this;
		}
		/// Append a child Builder and pin it to a grid cell.
		Builder &Child(Builder c, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(c.id, col, row, colSpan, rowSpan);
			sys.AppendChild(id, c.id);
			return *this;
		}
		/**
		 * @brief Append multiple child entities to this container in one call.
		 * @param cs  Any number of entity IDs (or Builder instances) to append.
		 */
		template <typename... Cs>
			requires(std::convertible_to<Cs, ECS::EntityId> && ...)
		Builder &Children(Cs &&...cs) {
			(sys.AppendChild(id, static_cast<ECS::EntityId>(std::forward<Cs>(cs))), ...);
			return *this;
		}

		/** @brief Append this widget as a child of @p parent. */
		Builder &AttachTo(ECS::EntityId parent) {
			sys.AppendChild(parent, id);
			return *this;
		}
		/**
		 * @brief Append this widget as a child of @p parent and pin it to a grid cell.
		 * @param parent   Target container entity.
		 * @param col      Zero-based column index.
		 * @param row      Zero-based row index.
		 * @param colSpan  Number of columns to span (default 1).
		 * @param rowSpan  Number of rows to span (default 1).
		 */
		Builder &AttachTo(ECS::EntityId parent, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(id, col, row, colSpan, rowSpan);
			sys.AppendChild(parent, id);
			return *this;
		}
		/**
		 * @brief Append this widget as a child of @p parent (Builder overload) and pin it to a grid cell.
		 * @param parent   Target container Builder.
		 * @param col      Zero-based column index.
		 * @param row      Zero-based row index.
		 * @param colSpan  Number of columns to span (default 1).
		 * @param rowSpan  Number of rows to span (default 1).
		 */
		Builder &AttachTo(Builder parent, int col, int row, int colSpan = 1, int rowSpan = 1) {
			sys.SetGridCell(id, col, row, colSpan, rowSpan);
			sys.AppendChild(parent.id, id);
			return *this;
		}

		/** @brief Promote this widget to be the root of the UI tree. */
		Builder &AsRoot() {
			sys.SetRoot(id);
			return *this;
		}
	};

	// =============================================================================
	// System builder factory implementations
	// =============================================================================

	inline Builder System::GetBuilder(ECS::EntityId e) { return {*this, e}; }
	
	inline Builder System::Container(const std::string &n) { return {*this, MakeContainer(n)}; }
	inline Builder System::Label(const std::string &n, const std::string &t) { return {*this, MakeLabel(n, t)}; } 
	inline Builder System::Button(const std::string &n, const std::string &t) { return {*this, MakeButton(n, t)}; }
	inline Builder System::Toggle(const std::string &n, const std::string &t) { return {*this, MakeToggle(n, t)}; }
	inline Builder System::Radio(const std::string &n, const std::string &g, const std::string &t) { return {*this, MakeRadioButton(n, g, t)}; }
	inline Builder System::Slider(const std::string &n, float mn, float mx, float v, Orientation o) { return {*this, MakeSlider(n, mn, mx, v, o)}; }
	inline Builder System::ScrollBar(const std::string &n, float cs, float vs, Orientation o) { return {*this, MakeScrollBar(n, cs, vs, o)}; }
	inline Builder System::Progress(const std::string &n, float v, float mx) { return {*this, MakeProgress(n, v, mx)}; }
	inline Builder System::Separator(const std::string &n) { return {*this, MakeSeparator(n)}; }
	inline Builder System::Input(const std::string &n, const std::string &ph) { return {*this, MakeInput(n, ph)}; }
	inline Builder System::Knob(const std::string &n, float mn, float mx, float v) { return {*this, MakeKnob(n, mn, mx, v)}; }
	inline Builder System::ImageWidget(const std::string &n, const std::string &p, ImageFit f) { return {*this, MakeImage(n, p, f)}; }
	inline Builder System::CanvasWidget(const std::string &n,
		std::function<void(SDL::Event&)> cb_event, 
		std::function<void(float)> cb_update,
		std::function<void(RendererRef, FRect)> cb_render) { return {*this, MakeCanvas(n, std::move(cb_event), std::move(cb_update), std::move(cb_render))}; }
	inline Builder System::TextArea(const std::string &n, const std::string &text, const std::string &ph) { return {*this, MakeTextArea(n, text, ph)}; }

	inline Builder System::Column(const std::string &n, float gap, float pad, float marg) {
        auto b = Container(n);
        b.Layout(Layout::InColumn).Gap(gap).Padding(pad).Margin(marg);
        return b;
    }

    inline Builder System::Row(const std::string &n, float gap, float pad, float marg) {
        auto b = Container(n);
        b.Layout(Layout::InLine).Gap(gap).Padding(pad).Margin(marg)
            .AlignH(Align::Center);
        return b;
    }

    inline Builder System::Card(const std::string &n, float gap, float marg) {
        auto b = Container(n);
        b.Layout(Layout::InColumn).Gap(gap).Margin(marg);
        b.Style(Theme::Card()).PaddingH(14.f).PaddingV(12.f);
        return b;
    }

	inline Builder System::Stack(const std::string &n, float gap, float pad, float marg) {
		auto b = Container(n);
		b.Layout(Layout::Stack).Gap(gap).Padding(pad).Padding(marg)
			.W(SDL::UI::Value::Auto())
			.H(SDL::UI::Value::Auto())
			.AlignH(Align::Center);
		return b;
	}

	inline Builder System::SectionTitle(const std::string &text, SDL::Color col) {
		auto b = Label("title_" + text, text);
		b.TextColor(col).MarginBottom(4.f);
		return b;
	}

	inline Builder System::ScrollView(const std::string &n, float gap) {
		auto b = Column(n, gap, 0.f);
		// AutoScrollableY : la scrollbar verticale n'apparaît que si le contenu déborde.
		b.AutoScrollableY().Padding(0);
		return b;
	}

	inline Builder System::Grid(const std::string &n, int columns, float gap, float pad) {
		auto b = Container(n);
		b.Layout(Layout::InGrid).Gap(gap).Padding(pad);
		b.GridCols(columns);
		return b;
	}

	// =============================================================================
	// Theme implementations
	// =============================================================================

	inline void Theme::ApplyDark(System &) { accentColor = {70, 130, 210, 255}; }
	inline void Theme::ApplyLight(System &) { accentColor = {60, 120, 200, 255}; } 

	inline Builder System::ListBoxWidget(const std::string &n,
										 const std::vector<std::string>& items) {
		return Builder(*this, MakeListBox(n, items));
	}

	inline Builder System::GradedGraph(const std::string &n) {
		return Builder(*this, MakeGraph(n));
	}

} // namespace UI

} // namespace SDL

#endif /* SDL3PP_UI_H_ */