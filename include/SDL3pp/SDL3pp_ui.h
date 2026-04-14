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
 * | `Content`        | Text, placeholder, cursor (Input)                        |
 * | `SliderData`     | min/max/value + drag (Slider, Progress, horizontal SB)   |
 * | `ScrollBarData`  | contentSize, viewSize, offset + drag                     |
 * | `ToggleData`     | checked + animT (Toggle)                                 |
 * | `RadioData`      | group name + checked (RadioButton)                       |
 * | `KnobData`       | normalised val [0,1] + drag                              |
 * | `ImageData`      | texture path + ImageFit                                  |
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
 *
 * ## Layout modes
 *
 *   Layout::InColumn  — children stacked vertically (default)
 *   Layout::InLine    — children placed horizontally (no wrap)
 *   Layout::Stack     — horizontal with line wrap
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
 *   .H(300).Grow(1)
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
 * SDL::ECS::World    ECS::World;
 * SDL::System ui(ECS::World, renderer, uiPool);  // ← single pool for all assets
 *
 * ui.Column("root").Pad(0).Gap(0)
 *   .Children(
 *     ui.Row("header").H(52).PadH(16).Gap(8)
 *       .Children(
 *         ui.Label("title","My App").TextColor({70,130,210,255}).Grow(1),
 *         ui.Button("ok","OK").W(100).H(36)
 *           .BgColor({70,130,210,255}).Radius(6)
 *           .ClickSound("assets/click.wav")
 *           .OnClick([]{ SDL::Log("clicked"); })
 *       ),
 *     ui.Slider("vol",0.f,1.f,0.8f).Grow(1)
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
 * ui.Frame(dt);
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

    enum class Layout : Uint16 {
        InLine,   ///< Children placed horizontally (no wrap).
        InColumn, ///< Children stacked vertically.
        Stack     ///< Like InLine but wraps when insufficient width.
    };

    enum class AttachLayout : Uint16 {
        Relative, ///< Normal flow.
        Absolute, ///< Absolute inside parent (bypasses flow; not scroll-offset).
        Fixed     ///< Fixed relative to root viewport.
    };

    enum class BoxSizing : Uint16 {
        ContentBox, ///< W/H = content only.
        PaddingBox, ///< W/H = content + padding.
        BorderBox,  ///< W/H = content + padding + bdColor.
        MarginBox   ///< W/H = content + padding + bdColor + margin.
    };

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
    inline bool Has(DirtyFlag &a, DirtyFlag b) { return (a & b) != DirtyFlag::None; }

    enum class BehaviorFlag : Uint16 {
        None            = 0,
        Enable          = 1 << 0,
        Visible         = 1 << 1,
        Hoverable       = 1 << 2,
        Selectable      = 1 << 3,
        Focusable       = 1 << 4,
        ScrollableX     = 1 << 5,
        ScrollableY     = 1 << 6,
        AutoScrollableX = 1 << 7,
        AutoScrollableY = 1 << 8,
        All             = 0x01FF
    };
    
    inline BehaviorFlag operator|(BehaviorFlag a, BehaviorFlag b) noexcept { return static_cast<BehaviorFlag>(static_cast<Uint16>(a) | static_cast<Uint16>(b)); }
    inline BehaviorFlag operator&(BehaviorFlag a, BehaviorFlag b) noexcept { return static_cast<BehaviorFlag>(static_cast<Uint16>(a) & static_cast<Uint16>(b)); }
    inline BehaviorFlag operator~(BehaviorFlag a) noexcept { return static_cast<BehaviorFlag>((~static_cast<Uint16>(a)) & static_cast<Uint16>(BehaviorFlag::All)); }
    inline BehaviorFlag &operator|=(BehaviorFlag &a, BehaviorFlag b) noexcept { a = a | b; return a; }
    inline BehaviorFlag &operator&=(BehaviorFlag &a, BehaviorFlag b) noexcept { a = a & b; return a; }
    inline bool operator!(BehaviorFlag a) noexcept { return a == BehaviorFlag::None; }
    inline bool Has(BehaviorFlag a, BehaviorFlag b) { return (a & b) != BehaviorFlag::None; }

    enum class Align : Uint8 {
        Start,
        Center,
        End,
        Stretch,

        Left   = Start,
        Top    = Start,
        Right  = End,
        Bottom = End
    };

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
        Fill,
        Contain,
        Cover,
        Tile,
        None
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
        Auto = 13
    };

    struct LayoutContext {
        FPoint windowSize     = {0.f, 0.f};

        FPoint rootSize       = {0.f, 0.f};
        FBox   rootPadding    = {0.f, 0.f, 0.f, 0.f};
        float  rootFontSize   = SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;

        FPoint parentSize     = {0.f, 0.f};
        FBox   parentPadding  = {0.f, 0.f, 0.f, 0.f};
        FBox   parentBorders  = {0.f, 0.f, 0.f, 0.f};
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
        static Value Ww  (float v, float o = 0.f) { return {v, Unit::Ww,   o}; }
        /** Percentage of Window Height */
        static Value Wh  (float v, float o = 0.f) { return {v, Unit::Wh,   o}; }
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
        static Value Auto(float v = 0.f)          { return {v, Unit::Auto, 0.f}; }

        [[nodiscard]] bool IsAuto() const noexcept { return unit == Unit::Auto; }
        Value operator+(float o) const noexcept { return {val, unit, offset + o}; }
        Value operator-(float o) const noexcept { return {val, unit, offset - o}; }

        [[nodiscard]] float Resolve(const LayoutContext &ctx) const noexcept {
            float base = 0.f;
            switch (unit) {
                case Unit::Px:
                case Unit::Auto:
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
                    base = (val / 100.f) * (ctx.parentSize.x - ctx.parentPadding.GetH() - ctx.parentBorders.GetH());
                    break;
                case Unit::Pch:
                    base = (val / 100.f) * (ctx.parentSize.y - ctx.parentPadding.GetV() - ctx.parentBorders.GetV());
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
        WidgetType  type      = WidgetType::Container;
        BehaviorFlag behavior = BehaviorFlag::Enable | BehaviorFlag::Visible;
        DirtyFlag   dirty     = DirtyFlag::All;
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
        bool usedDebugFont          = true;

        float opacity               = 1.f;

        std::string clickSound;
        std::string hoverSound;
        std::string scrollSound;
        std::string showSound;
        std::string hideSound;
    };

    struct LayoutProps {
        Value absX          = Value::Px(0);
        Value absY          = Value::Px(0);
        Value width         = Value::Auto();
        Value height        = Value::Auto();

        SDL::FBox margin    = {0.f, 0.f, 0.f, 0.f};
        SDL::FBox padding   = {8.f, 6.f, 8.f, 6.f};
        Layout layout       = Layout::InColumn;
        Align alignChildrenH   = Align::Stretch;  ///< Default cross-axis alignment for children in InColumn (horizontal).
        Align alignChildrenV   = Align::Stretch;  ///< Default cross-axis alignment for children in InRow / Stack (vertical).
        Align alignSelfH    = Align::Stretch;  ///< Cross-axis alignment in InColumn (horizontal).
        Align alignSelfV    = Align::Stretch;  ///< Cross-axis alignment in InRow / Stack (vertical).
        AttachLayout attach = AttachLayout::Relative;
        BoxSizing boxSizing = BoxSizing::BorderBox;
        float gap = 4.f;    ///< Gap between children in InColumn / InLine / Stack (px).  Does not apply to Separator.
        float grow = 0.f;   ///< Relative grow factor for distributing extra space in layout direction (default 0 = no grow).
        float scrollX = 0.f, scrollY = 0.f;
        float contentW = 0.f, contentH = 0.f;

        /// Thickness (px) of the auto inline scrollbar drawn by _DrawContainer.
        /// Applies to both axes.  Override via builder .ScrollbarThickness(n).
        float sbThickness = 8.f;
    };

    struct ComputedRect {
        FRect screen = {}, clip = {}, outer_clip = {};
        FPoint measured = {};
    };

    struct Children {
        std::vector<ECS::EntityId> ids;
        void Add(ECS::EntityId e) { ids.push_back(e); }
        void Remove(ECS::EntityId e) { std::erase(ids, e); }
    };

    struct Parent {
        ECS::EntityId id = ECS::NullEntity;
    };

    struct Content {
        std::string text, placeholder;
        int cursor = 0;
        float blinkTimer = 0.f;
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

        // ── Internal scroll ────────────────────────────────────────────────────
        float scrollY = 0.f;        ///< Vertical scroll offset (pixels from document top).

        // ── Rich text spans ────────────────────────────────────────────────────
        struct Span {
            int start = 0, end = 0; ///< [start, end) byte offsets into `text`.
            TextSpanStyle style;
        };
        std::vector<Span> spans;    ///< Sorted, non-overlapping styled ranges.

        // ── Helpers ────────────────────────────────────────────────────────────

        [[nodiscard]] bool HasSelection() const noexcept {
            return selAnchor >= 0 && selFocus >= 0 && selAnchor != selFocus;
        }
        [[nodiscard]] int SelMin() const noexcept {
            return (selAnchor < selFocus) ? selAnchor : selFocus;
        }
        [[nodiscard]] int SelMax() const noexcept {
            return (selAnchor > selFocus) ? selAnchor : selFocus;
        }
        [[nodiscard]] std::string GetSelectedText() const {
            if (!HasSelection()) return {};
            int a = std::clamp(SelMin(), 0, (int)text.size());
            int b = std::clamp(SelMax(), 0, (int)text.size());
            return (a < b) ? text.substr(a, b - a) : std::string{};
        }
        void ClearSelection() noexcept { selAnchor = selFocus = -1; }
        void SetSelection(int anchor, int focus) noexcept {
            int sz = (int)text.size();
            selAnchor = std::clamp(anchor, 0, sz);
            selFocus  = std::clamp(focus,  0, sz);
        }

        // ── Line / column navigation ───────────────────────────────────────────

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

        [[nodiscard]] float BorderW() const noexcept { return borderW > 0.f ? borderW : static_cast<float>(tileW); }
        [[nodiscard]] float BorderH() const noexcept { return borderH > 0.f ? borderH : static_cast<float>(tileH); }
    };

    struct WidgetState {
        bool hovered = false, pressed = false, focused = false, wasHovered = false;
    };

    struct Callbacks {
        std::function<void()>      onClick;
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

    struct Theme {
        static inline SDL::Color accentColor = {70, 130, 210, 255};
        static void ApplyDark(SDL::UI::System &);
        static void ApplyLight(SDL::UI::System &);

        static Style Card() {
            Style s;
            s.bgColor       = {26, 28, 40, 255};
            s.bdColor       = {50, 54, 78, 255};
            s.bdHovered = s.bdPressed = s.bdDisabled = s.bdColor;
            s.borders       = SDL::FBox(1.f);
            s.radius        = SDL::FCorners(8.f);
            return s;
        }
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
        static Style Transparent() {
            Style s;
            s.bgColor       = {0, 0, 0, 0};
            s.borders       = SDL::FBox(0.f);
            s.radius        = SDL::FCorners(0.f);
            return s;
        }
        static Style SectionTitle() {
            Style s         = Transparent();
            s.textColor     = accentColor;
            return s;
        }
        static Style DangerButton() { return PrimaryButton({200, 55, 45, 255}); }
        static Style SuccessButton() { return PrimaryButton({50, 180, 90, 255}); }
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
         * @param w    ECS World that owns all widget entities.
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
         * SDL::UI::System ui(world, renderer, uiPool);
         * ```
         */
        System(ECS::World &w, RendererRef r, MixerRef m, ResourcePool &pool)
            : m_world(w), m_renderer(r), m_mixer(m), m_pool(pool)
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
            m_world.Each<TextCache>([](ECS::EntityId, TextCache &tc) {
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
         * Set a default font applied to every new widget created after this call.
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
            m_defaultFontPath = path;
            m_defaultFontSize = ptsize;
        } 
        [[nodiscard]] const std::string &GetDefaultFontPath() const { return m_defaultFontPath; }
        [[nodiscard]] float GetDefaultFontSize() const { return m_defaultFontSize; }

        // ── Entity factories ──────────────────────────────────────────────────────────

        ECS::EntityId MakeContainer(const std::string &n = "Container") { return _Make(n, WidgetType::Container); }

        ECS::EntityId MakeLabel(const std::string &n, const std::string &t = "") { 
            ECS::EntityId e = _Make(n, WidgetType::Label);
            m_world.Get<Content>(e)->text = t;
            auto &l = *m_world.Get<LayoutProps>(e);
            l.padding.top = l.padding.bottom = 2.f;
            return e;
        }
        ECS::EntityId MakeButton(const std::string &n, const std::string &t = "") {
            ECS::EntityId e = _Make(n, WidgetType::Button);
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            m_world.Get<Content>(e)->text = t;
            return e;
        }
        ECS::EntityId MakeToggle(const std::string &n, const std::string &t = "") {
            ECS::EntityId e = _Make(n, WidgetType::Toggle);
            m_world.Get<Content>(e)->text = t;
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            m_world.Get<LayoutProps>(e)->height = Value::Px(28.f);
            m_world.Add<ToggleData>(e);
            return e;
        }
        ECS::EntityId MakeRadioButton(const std::string &n, const std::string &group, const std::string &t = "") {
            ECS::EntityId e = _Make(n, WidgetType::RadioButton);
            m_world.Get<Content>(e)->text = t;
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            m_world.Get<LayoutProps>(e)->height = Value::Px(24.f);
            m_world.Add<RadioData>(e, {group, false});
            return e;
        }
        ECS::EntityId MakeSlider(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.f, 
                            Orientation o = Orientation::Horizontal) {
            ECS::EntityId e = _Make(n, WidgetType::Slider);
            SliderData sd;
            sd.min = mn;
            sd.max = mx;
            sd.val = SDL::Clamp(v, mn, mx);
            sd.orientation = o;
            m_world.Add<SliderData>(e, sd);
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            auto &lp = *m_world.Get<LayoutProps>(e);
            if (o == Orientation::Horizontal)
                lp.height = Value::Px(24.f);
            else
                lp.width = Value::Px(24.f);
            return e;
        }
        ECS::EntityId MakeScrollBar(const std::string &n, float cs = 0.f, float vs = 0.f, 
                               Orientation o = Orientation::Vertical) {
            ECS::EntityId e = _Make(n, WidgetType::ScrollBar);
            ScrollBarData sd;
            sd.contentSize = cs;
            sd.viewSize = vs;
            sd.orientation = o;
            m_world.Add<ScrollBarData>(e, sd);
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            auto &lp = *m_world.Get<LayoutProps>(e);
            if (o == Orientation::Vertical)
                lp.width = Value::Px(10.f);
            else
                lp.height = Value::Px(10.f);
            return e;
        }
        ECS::EntityId MakeProgress(const std::string &n, float v = 0.f, float mx = 1.f) {
            ECS::EntityId e = _Make(n, WidgetType::Progress);
            m_world.Add<SliderData>(e, {0.f, mx, SDL::Clamp(v, 0.f, mx)});
            m_world.Get<LayoutProps>(e)->height = Value::Px(18.f);
            return e;
        }
        ECS::EntityId MakeSeparator(const std::string &n = "sep") {
            ECS::EntityId e = _Make(n, WidgetType::Separator);
            auto &lp = *m_world.Get<LayoutProps>(e);
            lp.height = Value::Px(1.f);
            lp.margin.top = lp.margin.bottom = 6.f;
            return e;
        }
        ECS::EntityId MakeInput(const std::string &n, const std::string &ph = "") {
            ECS::EntityId e = _Make(n, WidgetType::Input);
            m_world.Get<Content>(e)->placeholder = ph;
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            m_world.Get<LayoutProps>(e)->height = Value::Px(30.f);
            return e;
        }
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
            m_world.Add<KnobData>(e, kd);
            
            auto *w = m_world.Get<Widget>(e);
            if (w) w->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;

            auto &lp = *m_world.Get<LayoutProps>(e);
            lp.width = lp.height = Value::Px(56.f); // Taille par défaut sécurisée
            return e;
        }
        ECS::EntityId MakeImage(const std::string &n, const std::string &key = "", ImageFit fit = ImageFit::Contain) {
            ECS::EntityId e = _Make(n, WidgetType::Image);
            m_world.Add<ImageData>(e, {key, fit});
            return e;
        }
        ECS::EntityId MakeCanvas(const std::string &n,
            std::function<void(SDL::Event&)> cb_event = nullptr, 
            std::function<void(float)> cb_update = nullptr,
            std::function<void(RendererRef, FRect)> cb_render = nullptr) {
            ECS::EntityId e = _Make(n, WidgetType::Canvas);
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            m_world.Add<CanvasData>(e, {std::move(cb_event), std::move(cb_update), std::move(cb_render)});
            return e; 
        }
        ECS::EntityId MakeListBox(const std::string &n,
                                  const std::vector<std::string>& items = {}) {
            ECS::EntityId e = _Make(n, WidgetType::ListBox);
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable
                                              | BehaviorFlag::Selectable
                                              | BehaviorFlag::Focusable
                                              | BehaviorFlag::AutoScrollableY;
            auto &lb = m_world.Add<ListBoxData>(e);
            lb.items = items;
            m_world.Get<LayoutProps>(e)->padding = {2.f, 2.f, 2.f, 2.f};
            return e;
        }
        ECS::EntityId MakeGraph(const std::string &n) {
            ECS::EntityId e = _Make(n, WidgetType::Graph);
            m_world.Add<GraphData>(e);
            m_world.Get<LayoutProps>(e)->padding = {0.f, 0.f, 0.f, 0.f};
            return e;
        }
        ECS::EntityId MakeTextArea(const std::string &n, const std::string &text = "", const std::string &ph = "") {
            ECS::EntityId e = _Make(n, WidgetType::TextArea);
            m_world.Get<Widget>(e)->behavior |= BehaviorFlag::Hoverable | BehaviorFlag::Selectable | BehaviorFlag::Focusable;
            auto &ta = m_world.Add<TextAreaData>(e);
            ta.text = text;
            auto &lp = *m_world.Get<LayoutProps>(e);
            lp.padding = {6.f, 6.f, 6.f, 6.f};
            if (!ph.empty()) {
                // Store placeholder in Content component
                m_world.Get<Content>(e)->placeholder = ph;
            }
            return e;
        }

        // ── Builder factories (defined after Builder) ─────────────────────────────────

        inline Builder Container(const std::string &n = "Container");
        inline Builder Label(const std::string &n, const std::string &t = ""); 
        inline Builder Button(const std::string &n, const std::string &t = "");
        inline Builder Toggle(const std::string &n, const std::string &t = "");
        inline Builder Radio(const std::string &n, const std::string &grp, const std::string &t = "");
        inline Builder Slider(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.f,
                                Orientation o = Orientation::Horizontal);
        inline Builder ScrollBar(const std::string &n, float cs = 0.f, float vs = 0.f,
                                   Orientation o = Orientation::Vertical);
        inline Builder Progress(const std::string &n, float v = 0.f, float mx = 1.f);
        inline Builder Sep(const std::string &n = "sep");
        inline Builder Input(const std::string &n, const std::string &ph = "");
        inline Builder Knob(const std::string &n, float mn = 0.f, float mx = 1.f, float v = 0.5f);
        inline Builder ImageWidget(const std::string &n, const std::string &p = "", ImageFit f = ImageFit::Contain);
        inline Builder CanvasWidget(const std::string &n,
            std::function<void(SDL::Event&)> cb_event = nullptr, 
            std::function<void(float)> cb_update = nullptr,
            std::function<void(RendererRef, FRect)> cb_render = nullptr);
        inline Builder TextArea(const std::string &n, const std::string &text = "", const std::string &ph = "");
        inline Builder ListBoxWidget(const std::string &n, const std::vector<std::string>& items = {});
        inline Builder GradedGraph(const std::string &n);
        inline Builder Column(const std::string &n = "col", float gap = 4.f, float pad = 8.f);
        inline Builder Row(const std::string &n = "row", float gap = 8.f, float pad = 0.f);
        inline Builder Card(const std::string &n, float gap = 8.f);
        inline Builder SectionTitle(const std::string &text, SDL::Color color = {70, 130, 210, 255});
        inline Builder ScrollView(const std::string &n, float gap = 4.f);

        // ── Tree management ───────────────────────────────────────────────────────────

        void SetRoot(ECS::EntityId e) { m_root = e; }
        [[nodiscard]] ECS::EntityId GetRootId() const { return m_root; }

        void AppendChild(ECS::EntityId p, ECS::EntityId c) {
            if (!m_world.IsAlive(p) || !m_world.IsAlive(c)) 
                return;
            m_world.Get<Children>(p)->Add(c);
            m_world.Get<Parent>(c)->id = p;
        }
        void RemoveChild(ECS::EntityId p, ECS::EntityId c) {
            if (!m_world.IsAlive(p))
                return;
            m_world.Get<Children>(p)->Remove(c);
            if (m_world.IsAlive(c))
                m_world.Get<Parent>(c)->id = ECS::NullEntity;
        }

        // ── Component accessors ───────────────────────────────────────────────────────

        Style &GetStyle(ECS::EntityId e) { return *m_world.Get<Style>(e); }
        LayoutProps &GetLayout(ECS::EntityId e) { return *m_world.Get<LayoutProps>(e); }
        Content &GetContent(ECS::EntityId e) { return *m_world.Get<Content>(e); }

        // ── Setters ───────────────────────────────────────────────────────────────────

        void SetText(ECS::EntityId e, const std::string &t) {
            if (auto *c = m_world.Get<Content>(e)) {
                c->text = t;
                c->cursor = (int)t.size();
            }
        }

        void SetValue(ECS::EntityId e, float v) {
            if (auto *s = m_world.Get<SliderData>(e))
                s->val = SDL::Clamp(v, s->min, s->max);
            if (auto *k = m_world.Get<KnobData>(e)) {
                k->val = SDL::Clamp(v, k->min, k->max);
            }
        }

        // ── ListBox accessors ─────────────────────────────────────────────────────────

        void SetListBoxItems(ECS::EntityId e, std::vector<std::string> items) {
            auto *lb = m_world.Get<ListBoxData>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
            if (lb && lp) {
                lb->items = std::move(items);
                lp->scrollY = 0.f;
                if (lb->selectedIndex >= (int)lb->items.size())
                    lb->selectedIndex = -1;
            }
        }
        [[nodiscard]] int GetListBoxSelection(ECS::EntityId e) const {
            if (auto *lb = m_world.Get<ListBoxData>(e)) return lb->selectedIndex;
            return -1;
        }
        void SetListBoxSelection(ECS::EntityId e, int idx) {
            if (auto *lb = m_world.Get<ListBoxData>(e))
                lb->selectedIndex = (idx >= 0 && idx < (int)lb->items.size()) ? idx : -1;
        }
        ListBoxData* GetListBoxData(ECS::EntityId e) { return m_world.Get<ListBoxData>(e); }

        // ── Graph accessors ───────────────────────────────────────────────────────────

        void SetGraphData(ECS::EntityId e, std::vector<float> data) {
            if (auto *gd = m_world.Get<GraphData>(e)) gd->data = std::move(data);
        }
        void SetGraphRange(ECS::EntityId e, float minV, float maxV) {
            if (auto *gd = m_world.Get<GraphData>(e)) { gd->minVal = minV; gd->maxVal = maxV; }
        }
        void SetGraphXRange(ECS::EntityId e, float xMin, float xMax) {
            if (auto *gd = m_world.Get<GraphData>(e)) { gd->xMin = xMin; gd->xMax = xMax; }
        }
        GraphData* GetGraphData(ECS::EntityId e) { return m_world.Get<GraphData>(e); }

        void SetScrollOffset(ECS::EntityId e, float off) {
            if (auto *sb = m_world.Get<ScrollBarData>(e)) {
                float mx = SDL::Max(0.f, sb->contentSize - sb->viewSize);
                sb->offset = SDL::Clamp(off, 0.f, mx);
            }
        }

        void SetChecked(ECS::EntityId e, bool b) {
            if (auto *t = m_world.Get<ToggleData>(e)) {
                t->checked = b;
                t->animT = b ? 1.f : 0.f;
            }
            if (auto *r = m_world.Get<RadioData>(e))
                r->checked = b;
        }

        void SetEnabled(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::Enable;
                else w->behavior &= (~BehaviorFlag::Enable);
            }
        }

        void SetVisible(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                bool wasVisible = Has(w->behavior, BehaviorFlag::Visible);
                if (b && !wasVisible) {
                    w->behavior |= BehaviorFlag::Visible;
                    m_layoutDirty = true;
                    if (auto *s = m_world.Get<Style>(e); s && !s->showSound.empty()) {
                        if (auto sh = _EnsureAudio(s->showSound)) _PlayAudio(sh);
                    }
                } else if (!b && wasVisible) {
                    w->behavior &= (~BehaviorFlag::Visible);
                    m_layoutDirty = true;
                    if (auto *s = m_world.Get<Style>(e); s && !s->hideSound.empty()) {
                        if (auto sh = _EnsureAudio(s->hideSound)) _PlayAudio(sh);
                    }
                }
            }
        }

        void SetHoverable(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::Hoverable;
                else w->behavior &= (~BehaviorFlag::Hoverable);
            }
        }

        void SetSelectable(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::Selectable;
                else w->behavior &= (~BehaviorFlag::Selectable);
            }
        }

        void SetFocusable(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::Focusable;
                else w->behavior &= (~BehaviorFlag::Focusable);
            }
        }

        void SetScrollableX(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::ScrollableX;
                else w->behavior &= (~BehaviorFlag::ScrollableX);
            }
        }

        void SetScrollableY(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::ScrollableY;
                else w->behavior &= (~BehaviorFlag::ScrollableY);
            }
        }

        void SetScrollable(ECS::EntityId e, bool bx, bool by) {
            SetScrollableX(e, bx);
            SetScrollableY(e, by);
        }

        /// Scrollbar automatique horizontal (visible seulement si le contenu déborde).
        void SetAutoScrollableX(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
                if (b) w->behavior |= BehaviorFlag::AutoScrollableX;
                else w->behavior &= (~BehaviorFlag::AutoScrollableX);
            }
        }

        /// Scrollbar automatique vertical (visible seulement si le contenu déborde).
        void SetAutoScrollableY(ECS::EntityId e, bool b) {
            if (auto *w = m_world.Get<Widget>(e)) {
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
            if (auto *lp = m_world.Get<LayoutProps>(e))
                lp->sbThickness = SDL::Max(4.f, t);
        }

        void OnEventCanvas(ECS::EntityId e, std::function<void(SDL::Event&)> cb) {
            if (auto *c = m_world.Get<CanvasData>(e))
                c->eventCb = std::move(cb);
        }

        void OnUpdateCanvas(ECS::EntityId e, std::function<void(float)> cb) {
            if (auto *c = m_world.Get<CanvasData>(e))
                c->updateCb = std::move(cb);
        }

        void OnRenderCanvas(ECS::EntityId e, std::function<void(RendererRef, FRect)> cb) {
            if (auto *c = m_world.Get<CanvasData>(e))
                c->renderCb = std::move(cb);
        }

        // ── Tileset skin ──────────────────────────────────────────────────────────────

        /**
         * Attach (or replace) a tileset 9-slice skin on widget `e`.
         * Pass a default-constructed `TilesetStyle` with an empty `textureKey`
         * to remove the skin.
         */
        void SetTilesetStyle(ECS::EntityId e, TilesetStyle ts) {
            if (!m_world.IsAlive(e)) return;
            m_world.Add<TilesetStyle>(e, std::move(ts));
        }

        [[nodiscard]] TilesetStyle* GetTilesetStyle(ECS::EntityId e) {
            return m_world.Get<TilesetStyle>(e);
        }

        void RemoveTilesetStyle(ECS::EntityId e) {
            m_world.Remove<TilesetStyle>(e);
        }

        void SetImageKey(ECS::EntityId e, const std::string &key, ImageFit f = ImageFit::Contain) {
            if (auto *d = m_world.Get<ImageData>(e)) {
                d->key = key;
                d->fit = f;
            }
        }

        // ── TextArea accessors ────────────────────────────────────────────────────────

        void SetTextAreaContent(ECS::EntityId e, const std::string &t) {
            if (auto *ta = m_world.Get<TextAreaData>(e)) {
                ta->text = t;
                ta->cursorPos = 0;
                ta->ClearSelection();
                ta->scrollY = 0.f;
            }
        }
        [[nodiscard]] const std::string &GetTextAreaContent(ECS::EntityId e) const {
            static const std::string empty;
            const auto *ta = m_world.Get<TextAreaData>(e);
            return ta ? ta->text : empty;
        }
        void SetTextAreaHighlightColor(ECS::EntityId e, SDL::Color c) {
            if (auto *ta = m_world.Get<TextAreaData>(e)) ta->highlightColor = c;
        }
        void SetTextAreaTabSize(ECS::EntityId e, int sz) {
            if (auto *ta = m_world.Get<TextAreaData>(e)) ta->tabSize = SDL::Max(1, sz);
        }
        void AddTextAreaSpan(ECS::EntityId e, int start, int end, TextSpanStyle style) {
            if (auto *ta = m_world.Get<TextAreaData>(e)) ta->AddSpan(start, end, style);
        }
        void ClearTextAreaSpans(ECS::EntityId e) {
            if (auto *ta = m_world.Get<TextAreaData>(e)) ta->ClearSpans();
        }
        [[nodiscard]] TextAreaData* GetTextAreaData(ECS::EntityId e) {
            return m_world.Get<TextAreaData>(e);
        }

        // ── Getters ───────────────────────────────────────────────────────────────────

        [[nodiscard]] const std::string &GetText(ECS::EntityId e) const {
            static const std::string empty;
            const auto *c = m_world.Get<Content>(e);
            return c ? c->text : empty;
        }

        [[nodiscard]] float GetValue(ECS::EntityId e) const {
            const auto *s = m_world.Get<SliderData>(e);
            return s ? s->val : 0.f;
        }

        [[nodiscard]] float GetScrollOffset(ECS::EntityId e) const {
            const auto *sb = m_world.Get<ScrollBarData>(e);
            return sb ? sb->offset : 0.f;
        }

        [[nodiscard]] bool IsChecked(ECS::EntityId e) const {
            if (const auto *t = m_world.Get<ToggleData>(e))
                return t->checked;
            if (const auto *r = m_world.Get<RadioData>(e))
                return r->checked;
            return false;
        }

        [[nodiscard]] bool IsEnabled(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::Enable);
        }

        [[nodiscard]] bool IsVisible(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::Visible);
        }

        [[nodiscard]] bool IsHoverable(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::Hoverable);
        }

        [[nodiscard]] bool IsSelectable(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::Selectable);
        }

        [[nodiscard]] bool IsFocusable(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::Focusable);
        }

        [[nodiscard]] bool IsScrollableX(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::ScrollableX);
        }

        [[nodiscard]] bool IsScrollableY(ECS::EntityId e) const {
            const auto *w = m_world.Get<Widget>(e);
            return w && Has(w->behavior, BehaviorFlag::ScrollableY);
        }

        [[nodiscard]] bool IsHovered(ECS::EntityId e) const {
            const auto *s = m_world.Get<WidgetState>(e);
            return s && s->hovered;
        }

        [[nodiscard]] bool IsFocused(ECS::EntityId e) const { 
            return m_focused == e; 
        }
        
        [[nodiscard]] bool IsPressed(ECS::EntityId e) const {
            const auto *s = m_world.Get<WidgetState>(e);
            return s && s->pressed;
        }

        [[nodiscard]] FRect GetScreenRect(ECS::EntityId e) const {
            const auto *c = m_world.Get<ComputedRect>(e);
            return c ? c->screen : FRect{};
        }

        void LoadTexture(const std::string& key, const std::string& path) {
            _EnsureTexture(key, path);
        }

        void LoadFont(const std::string& key, const std::string& path) {
            _EnsureFont(key, 8.0f, path);
        }

        void LoadAudio(const std::string& key, const std::string& path) {
            _EnsureAudio(key, path);
        }

        // ── Callback registration ─────────────────────────────────────────────────────

        void OnClick(ECS::EntityId e, std::function<void()> cb) { m_world.Get<Callbacks>(e)->onClick = std::move(cb); }
        void OnChange(ECS::EntityId e, std::function<void(float)> cb) { m_world.Get<Callbacks>(e)->onChange = std::move(cb); }
        void OnTextChange(ECS::EntityId e, std::function<void(const std::string &)> cb) { m_world.Get<Callbacks>(e)->onTextChange = std::move(cb); }
        void OnToggle(ECS::EntityId e, std::function<void(bool)> cb) { m_world.Get<Callbacks>(e)->onToggle = std::move(cb); }
        void OnScroll(ECS::EntityId e, std::function<void(float)> cb) { m_world.Get<Callbacks>(e)->onScroll = std::move(cb); }
        void OnHoverEnter(ECS::EntityId e, std::function<void()> cb) { m_world.Get<Callbacks>(e)->onHoverEnter = std::move(cb); }
        void OnHoverLeave(ECS::EntityId e, std::function<void()> cb) { m_world.Get<Callbacks>(e)->onHoverLeave = std::move(cb); }
        void OnFocusGain(ECS::EntityId e, std::function<void()> cb) { m_world.Get<Callbacks>(e)->onFocusGain = std::move(cb); }
        void OnFocusLose(ECS::EntityId e, std::function<void()> cb) { m_world.Get<Callbacks>(e)->onFocusLose = std::move(cb); }

        [[nodiscard]] ECS::World &GetWorld() { return m_world; }

        // ── Frame pipeline ────────────────────────────────────────────────────────────

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
                default:
                    break;
            }
            _CanvasEvent(ev);
        }

        void Frame(float dt) {
            m_pool.Update();

            m_dt = dt;
            if (m_root == ECS::NullEntity || !m_world.IsAlive(m_root)) {
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
            _ProcessRender();
            _ProcessAnimate(dt);
            _ResetOneShots();
            m_mouseDelta  = {};
            m_layoutDirty = false;
        }

    private:
        ECS::World&   m_world;
        RendererRef   m_renderer;
        MixerRef      m_mixer;
        ResourcePool& m_pool;
#if UI_HAS_TTF
        // Owned TTF text engine.  Declared *after* m_world/m_pool (references)
        // so that its destruction order is controlled explicitly in ~System():
        // all TextCache ECS components are cleared first, then this resets.
        std::optional<SDL::RendererTextEngine> m_engine;
#endif
        std::string m_defaultFontPath;
        float       m_defaultFontSize = 0.f;
        ECS::EntityId m_root = ECS::NullEntity, m_focused = ECS::NullEntity, m_hovered = ECS::NullEntity, m_pressed = ECS::NullEntity;
        float m_dt = 0.f;
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

            auto* cache = m_world.Get<TextCache>(e);
            if (!cache) {
                cache = &m_world.Add<TextCache>(e);
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



        ECS::EntityId _Make(const std::string &n, WidgetType k) {
            ECS::EntityId e = m_world.CreateEntity();

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
                // Label, Progress, Separator, Image n'ont PAS Hoverable/Selectable par défaut
                default:
                    break;
            }

            m_world.Add<Widget>(e, {n, k, beh, DirtyFlag::All});
            auto &style = m_world.Add<Style>(e);
            m_world.Add<LayoutProps>(e);
            m_world.Add<Content>(e);
            m_world.Add<WidgetState>(e);
            m_world.Add<Callbacks>(e);
            m_world.Add<ComputedRect>(e);
            m_world.Add<Children>(e);
            m_world.Add<Parent>(e);

            // Containers get a scroll-drag state component for their inline scrollbars.
            if (k == WidgetType::Container || k == WidgetType::ListBox)
                m_world.Add<ContainerScrollState>(e);

            if (!m_defaultFontPath.empty()) {
                style.fontKey = m_defaultFontPath;
                style.usedDebugFont = false;
            } else {
                style.usedDebugFont = true;
            }
            if (m_defaultFontSize > 0.f)
                style.fontSize = m_defaultFontSize;
            return e;
        }

        /// Build a child LayoutContext from the current viewport + root info + the
        /// resolved parent content area and layout props.
        LayoutContext _MakeChildCtx(const LayoutContext &parentCtx,
                                    const FPoint& contentSize, const FBox& borders,
                                    const LayoutProps &lp) const noexcept { 
            LayoutContext cc;
            cc.windowSize     = parentCtx.windowSize;
            cc.rootSize       = parentCtx.rootSize;
            cc.rootPadding    = parentCtx.rootPadding;
            cc.rootFontSize   = parentCtx.rootFontSize;
            cc.parentSize     = contentSize;
            cc.parentPadding  = lp.padding;
            cc.parentBorders  = borders;
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
        /// @param lp     LayoutProps (contentW/H, sbThickness).
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
            const float t     = lp.sbThickness;

            // Première passe — sans tenir compte de l'axe croisé.
            showX = wantX && (!autoX || lp.contentW > viewW);
            showY = wantY && (!autoY || lp.contentH > viewH);

            // Deuxième passe — une barre visible réduit l'espace de l'autre axe.
            if (showY && !showX && wantX)
                showX = !autoX || (lp.contentW > viewW - t);
            if (showX && !showY && wantY)
                showY = !autoY || (lp.contentH > viewH - t);
        }

        // ── Layout ────────────────────────────────────────────────────────────────────

        void _ProcessLayout() {
            if (!m_world.IsAlive(m_root)) return; 

            auto *rootLp = m_world.Get<LayoutProps>(m_root);
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

            auto *rootCr = m_world.Get<ComputedRect>(m_root);
            if (rootCr)
                rootCr->screen = {0.f, 0.f, rootW, rootH};

            _Place(m_root);
            _UpdateClips(m_root, m_viewport);
        }

        FPoint _Measure(ECS::EntityId e, const LayoutContext &ctx) {
            if (!m_world.IsAlive(e)) 
                return {};
            auto *w  = m_world.Get<Widget>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
            auto *cr = m_world.Get<ComputedRect>(e);
            auto *s  = m_world.Get<Style>(e);
            if (!w || !lp || !cr)
                return {};
            if (!Has(w->behavior, BehaviorFlag::Visible)) {
                cr->measured = {};
                return {};
            }

            // Resolve explicit dimensions (Auto → 0 ici ; on élargit ci-dessous).
            bool wa = lp->width.IsAuto(), ha = lp->height.IsAuto();
            float fw = wa ? 0.f : lp->width.Resolve(ctx);
            float fh = ha ? 0.f : lp->height.Resolve(ctx);

            // Espace contenu brut disponible pour les enfants.
            float cW = SDL::Max(0.f, (wa ? ctx.parentSize.x : fw) - lp->padding.left - lp->padding.right);
            float cH = SDL::Max(0.f, (ha ? ctx.parentSize.y : fh) - lp->padding.top  - lp->padding.bottom);

            // Pour les containers avec scrollbars automatiques, on doit pré-calculer
            // si les barres seront visibles afin de réserver leur place dans l'espace
            // contenu.  On utilise les données contentW/H du frame précédent (première
            // frame = 0, ce qui est correct car le contenu ne déborde pas encore).
            if (w->type == WidgetType::Container) {
                bool showX = false, showY = false;
                _ContainerScrollbars(*w, *lp, cW, cH, showX, showY);
                if (showY) cW = SDL::Max(0.f, cW - lp->sbThickness);
                if (showX) cH = SDL::Max(0.f, cH - lp->sbThickness);
            }

            FPoint intr = _IntrinsicSize(e);

            // Contexte transmis aux enfants.
            LayoutContext cc = _MakeChildCtx(ctx, {cW, cH}, s ? s->borders : FBox(0.f), *lp);

            float chW = 0.f, chH = 0.f;
            float curLineW = 0.f, curLineH = 0.f;
            int vis = 0, lineVis = 0;
            auto *ch = m_world.Get<Children>(e);
            if (ch) {
                for (ECS::EntityId cid : ch->ids) {
                    if (!m_world.IsAlive(cid)) continue; 
                    auto *cw  = m_world.Get<Widget>(cid);
                    auto *cl  = m_world.Get<LayoutProps>(cid);
                    if (!cw || !cl || !Has(cw->behavior, BehaviorFlag::Visible)) continue;
                    
                    if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) {
                        _Measure(cid, cc);
                        continue;
                    }
                    
                    FPoint csz = _Measure(cid, cc);
                    float mw = csz.x + cl->margin.left + cl->margin.right;
                    float mh = csz.y + cl->margin.top  + cl->margin.bottom;
                    
                    if (lp->layout == Layout::InColumn) {
                        chW  = SDL::Max(chW, mw);
                        chH += mh + (vis > 0 ? lp->gap : 0.f);
                    } else if (lp->layout == Layout::InLine) {
                        chH  = SDL::Max(chH, mh);
                        chW += mw + (vis > 0 ? lp->gap : 0.f);
                    } else if (lp->layout == Layout::Stack) {
                        if (lineVis > 0 && cW > 0.f && curLineW + lp->gap + mw > cW) {
                            chW = SDL::Max(chW, curLineW);
                            chH += curLineH + lp->gap;
                            curLineW = mw;
                            curLineH = mh;
                            lineVis = 1;
                        } else {
                            curLineW += mw + (lineVis > 0 ? lp->gap : 0.f);
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
                lp->contentW = chW;
                lp->contentH = chH;
            }

            float bW = wa ? SDL::Max(intr.x, chW) + lp->padding.left + lp->padding.right : fw;
            float bH = ha ? SDL::Max(intr.y, chH) + lp->padding.top  + lp->padding.bottom : fh;
            cr->measured = {bW, bH};
            return cr->measured;
        }

        [[nodiscard]] FPoint _IntrinsicSize(ECS::EntityId e) {
            auto *w = m_world.Get<Widget>(e); 
            auto *s = m_world.Get<Style>(e);
            if (!w)
                return {};
            // Use a default style for metrics when none is set
            static const Style kDef{};
            const Style &st = s ? *s : kDef;
            float ch = _TH(st);
            switch (w->type) { 
            case WidgetType::Label:
            case WidgetType::Button: {
                auto *c = m_world.Get<Content>(e);
                if (!c || c->text.empty())
                    return {60.f, ch + 4.f};
                return {_TW(c->text, st), ch + 4.f};
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
            if (!m_world.IsAlive(e)) return; 

            auto *w  = m_world.Get<Widget>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
            auto *cr = m_world.Get<ComputedRect>(e);
            auto *ch = m_world.Get<Children>(e);
            if (!w || !lp || !cr || !ch || ch->ids.empty())
                return;

            const FRect &self = cr->screen;
            float cw  = self.w - lp->padding.left - lp->padding.right;
            float ch2 = self.h - lp->padding.top  - lp->padding.bottom;

            // Réserver la place des scrollbars inline pour les containers.
            if (w->type == WidgetType::Container) {
                bool showX = false, showY = false;
                _ContainerScrollbars(*w, *lp, cw, ch2, showX, showY);
                if (showY) cw  = SDL::Max(0.f, cw  - lp->sbThickness);
                if (showX) ch2 = SDL::Max(0.f, ch2 - lp->sbThickness);
            }

            // Point de départ du flux, décalé par le scroll.
            float cx  = self.x + lp->padding.left  - lp->scrollX;
            float cy  = self.y + lp->padding.top   - lp->scrollY;

            // First pass: compute grow budget over flow children only.
            // Placer les éléments Absolute/Fixed en priorité pour les sortir du flux
            for (ECS::EntityId cid : ch->ids) {
                if (!m_world.IsAlive(cid)) continue; 
                auto *cw2 = m_world.Get<Widget>(cid);
                auto *cl  = m_world.Get<LayoutProps>(cid);
                auto *cc  = m_world.Get<ComputedRect>(cid);
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
                float tFixed = 0.f, tGrow = 0.f;
                int vis = 0;
                for (ECS::EntityId cid : ch->ids) {
                    if (!m_world.IsAlive(cid)) continue; 
                    auto *cw2 = m_world.Get<Widget>(cid);
                    auto *cl  = m_world.Get<LayoutProps>(cid);
                    auto *cc  = m_world.Get<ComputedRect>(cid);
                    if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
                    if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;
                    
                    tGrow += cl->grow;
                    if (lp->layout == Layout::InColumn) {
                        tFixed += cl->margin.top + cl->margin.bottom;
                        if (cl->grow == 0.f) tFixed += cc->measured.y;
                    } else {
                        tFixed += cl->margin.left + cl->margin.right;
                        if (cl->grow == 0.f) tFixed += cc->measured.x;
                    }
                    ++vis;
                }

                float avail   = (lp->layout == Layout::InColumn) ? ch2 : cw;
                float gBudget = SDL::Max(0.f, avail - tFixed - lp->gap * SDL::Max(0, vis - 1));
                float gUnit   = (tGrow > 0.f) ? gBudget / tGrow : 0.f;

                bool first = true;
                for (ECS::EntityId cid : ch->ids) {
                    if (!m_world.IsAlive(cid)) continue; 
                    auto *cw2 = m_world.Get<Widget>(cid);
                    auto *cl  = m_world.Get<LayoutProps>(cid);
                    auto *cc  = m_world.Get<ComputedRect>(cid);
                    if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible)) continue;
                    if (cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

                    float childW = cc->measured.x, childH = cc->measured.y;
                    if (cl->grow > 0.f) {
                        if (lp->layout == Layout::InColumn) childH = gUnit * cl->grow;
                        else                                childW = gUnit * cl->grow;
                    }

                    if (!first) {
                        if (lp->layout == Layout::InColumn) cy += lp->gap;
                        else                                cx += lp->gap;
                    }
                    first = false;

                    float px = cx + cl->margin.left;
                    float py = cy + cl->margin.top;

                    if (lp->layout == Layout::InColumn) {
                        switch (cl->alignSelfH) {
                            case Align::Stretch: childW = SDL::Max(0.f, cw - cl->margin.left - cl->margin.right); [[fallthrough]];
                            case Align::Start:   break;
                            case Align::Center:  px = cx + (cw - childW) * 0.5f; break;
                            case Align::End:     px = cx + cw - childW - cl->margin.right; break;
                        }
                        cc->screen = {px, py, childW, childH};
                        cy += childH + cl->margin.top + cl->margin.bottom;
                    } else {
                        switch (cl->alignSelfV) {
                            case Align::Stretch: childH = SDL::Max(0.f, ch2 - cl->margin.top - cl->margin.bottom); [[fallthrough]];
                            case Align::Start:   break;
                            case Align::Center:  py = cy + (ch2 - childH) * 0.5f; break;
                            case Align::End:     py = cy + ch2 - childH - cl->margin.bottom; break;
                        }
                        cc->screen = {px, py, childW, childH};
                        cx += childW + cl->margin.left + cl->margin.right;
                    }
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
                        if (!m_world.IsAlive(cid)) { j++; continue; } 
                        auto *cw2 = m_world.Get<Widget>(cid);
                        auto *cl  = m_world.Get<LayoutProps>(cid);
                        auto *cc  = m_world.Get<ComputedRect>(cid);
                        
                        if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) || cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) { 
                            j++; continue; 
                        }
                        
                        float itemFixedW = cl->margin.left + cl->margin.right + (cl->grow == 0.f ? cc->measured.x : 0.f);
                        float itemH = cc->measured.y + cl->margin.top + cl->margin.bottom;

                        if (rowItems > 0 && cw > 0.f && rowFixedW + lp->gap + itemFixedW > cw) {
                            break; // Wrap ! On passe à la ligne suivante
                        }

                        rowFixedW += itemFixedW + (rowItems > 0 ? lp->gap : 0.f);
                        rowGrow += cl->grow;
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
                        if (!m_world.IsAlive(cid)) continue;
                        auto *cw2 = m_world.Get<Widget>(cid);
                        auto *cl  = m_world.Get<LayoutProps>(cid);
                        auto *cc  = m_world.Get<ComputedRect>(cid);
                        
                        if (!cw2 || !cl || !cc || !Has(cw2->behavior, BehaviorFlag::Visible) || cl->attach == AttachLayout::Absolute || cl->attach == AttachLayout::Fixed) continue;

                        float childW = cc->measured.x, childH = cc->measured.y;
                        if (cl->grow > 0.f) childW = gUnit * cl->grow;

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
            }
        }

        void _UpdateClips(ECS::EntityId e, FRect parentClip) {
            if (!m_world.IsAlive(e)) return; 
            
            auto *w   = m_world.Get<Widget>(e);
            auto *lp  = m_world.Get<LayoutProps>(e);
            auto *s   = m_world.Get<Style>(e);
            auto *cr  = m_world.Get<ComputedRect>(e);
            if (!w || !lp || !cr) return;

            cr->clip = cr->screen.GetIntersection(parentClip);
            cr->outer_clip = cr->clip.Extend(s->borders);

            FRect childClip = cr->clip; 
            
            if (w->type == WidgetType::Container) {
                childClip = cr->screen;
                childClip.x += lp->padding.left;
                childClip.y += lp->padding.top;
                childClip.w -= (lp->padding.left + lp->padding.right);
                childClip.h -= (lp->padding.top + lp->padding.bottom);
                
                childClip = childClip.GetIntersection(parentClip);
            }

            auto *ch = m_world.Get<Children>(e);
            if (ch) {
                for (ECS::EntityId c : ch->ids) {
                    _UpdateClips(c, childClip);
                }
            }
        }

        // ── Input ─────────────────────────────────────────────────────────────────────

        void _ProcessInput() {
            // ── Scrollbars inline des containers (drag en cours) ──────────────────
            // On traite le drag des thumbs de container AVANT le hit-test normal
            // car la souris peut sortir du thumb pendant le drag.
            _UpdateContainerScrollDrags();

            ECS::EntityId nh = _HitTest(m_root, m_mousePos);

            // Reset all hover flags.
            m_world.Each<WidgetState>([](ECS::EntityId, WidgetState &s) {
                s.wasHovered = s.hovered;
                s.hovered    = false;
            });

            // Only mark as hovered if the widget actually supports it.
            if (nh != ECS::NullEntity && m_world.IsAlive(nh)) { 
                auto *w = m_world.Get<Widget>(nh);
                if (w && Has(w->behavior, BehaviorFlag::Enable) && Has(w->behavior, BehaviorFlag::Hoverable)) {
                    m_world.Get<WidgetState>(nh)->hovered = true;
                } else {
                    nh = ECS::NullEntity;
                }
            }

            // Hover enter / leave callbacks + hover sound.
            if (nh != m_hovered) {
                if (m_hovered != ECS::NullEntity && m_world.IsAlive(m_hovered)) { 
                    auto *cb = m_world.Get<Callbacks>(m_hovered);
                    if (cb && cb->onHoverLeave) cb->onHoverLeave();
                }
                if (nh != ECS::NullEntity && m_world.IsAlive(nh)) { 
                    auto *cb = m_world.Get<Callbacks>(nh);
                    if (cb && cb->onHoverEnter) cb->onHoverEnter();
                    
                    auto *s = m_world.Get<Style>(nh);
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

                    if (m_hovered != ECS::NullEntity && m_world.IsAlive(m_hovered)) { 
                        auto *pw = m_world.Get<Widget>(m_hovered);
                        if (pw && Has(pw->behavior, BehaviorFlag::Enable) && Has(pw->behavior, BehaviorFlag::Selectable)) {
                            m_pressed = m_hovered;
                            if (auto *st = m_world.Get<WidgetState>(m_pressed)) st->pressed = true;

                            bool wantFocus = Has(pw->behavior, BehaviorFlag::Focusable);
                            _SetFocus(wantFocus ? m_pressed : ECS::NullEntity);

                            // Begin drag for interactive controls. 
                            if (pw->type == WidgetType::Slider) {
                                if (auto *sd = m_world.Get<SliderData>(m_pressed)) {
                                    sd->drag        = true;
                                    sd->dragStartPos = (sd->orientation == Orientation::Horizontal)
                                                         ? m_mousePos.x : m_mousePos.y;
                                    sd->dragStartVal = sd->val;
                                }
                            }
                            if (pw->type == WidgetType::ScrollBar) {
                                if (auto *sb = m_world.Get<ScrollBarData>(m_pressed)) {
                                    sb->drag        = true;
                                    sb->dragStartPos = (sb->orientation == Orientation::Vertical)
                                                         ? m_mousePos.y : m_mousePos.x;
                                    sb->dragStartOff = sb->offset;
                                }
                            }
                            if (pw->type == WidgetType::Knob) {
                                if (auto *kd = m_world.Get<KnobData>(m_pressed)) {
                                    kd->drag         = true;
                                    kd->dragStartY   = m_mousePos.y;
                                    kd->dragStartVal = kd->val;
                                }
                            }
                            if (pw->type == WidgetType::TextArea) {
                                auto *ta = m_world.Get<TextAreaData>(m_pressed);
                                auto *s2 = m_world.Get<Style>(m_pressed);
                                auto *cr = m_world.Get<ComputedRect>(m_pressed);
                                auto *lp2 = m_world.Get<LayoutProps>(m_pressed);
                                if (ta && s2 && cr && lp2) {
                                    float relX = m_mousePos.x - (cr->screen.x + lp2->padding.left);
                                    float relY = m_mousePos.y - (cr->screen.y + lp2->padding.top);
                                    int hitPos = _TAHitPos(ta, relX, relY, *s2);
                                    ta->cursorPos = hitPos;
                                    ta->SetSelection(hitPos, hitPos);
                                    ta->selectDragging = true;
                                }
                            }
                        }
                    }
                }
            }

            // ── Drag ──────────────────────────────────────────────────────────────
            if (m_mouseDown && m_pressed != ECS::NullEntity && m_world.IsAlive(m_pressed)) { 
                // TextArea drag-selection
                {
                    auto *pw2 = m_world.Get<Widget>(m_pressed);
                    if (pw2 && pw2->type == WidgetType::TextArea) {
                        auto *ta  = m_world.Get<TextAreaData>(m_pressed);
                        auto *s2  = m_world.Get<Style>(m_pressed);
                        auto *cr  = m_world.Get<ComputedRect>(m_pressed);
                        auto *lp2 = m_world.Get<LayoutProps>(m_pressed);
                        if (ta && ta->selectDragging && s2 && cr && lp2) {
                            float relX = m_mousePos.x - (cr->screen.x + lp2->padding.left);
                            float relY = m_mousePos.y - (cr->screen.y + lp2->padding.top);
                            int hitPos = _TAHitPos(ta, relX, relY, *s2);
                            ta->cursorPos = hitPos;
                            ta->selFocus  = hitPos;
                        }
                    }
                }
                auto *pw = m_world.Get<Widget>(m_pressed);
                if (pw) {
                    if (pw->type == WidgetType::Slider) {
                        if (auto *sd = m_world.Get<SliderData>(m_pressed); sd && sd->drag) {
                            auto *cr = m_world.Get<ComputedRect>(m_pressed);
                            auto *lp = m_world.Get<LayoutProps>(m_pressed);
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
                                        auto *cb = m_world.Get<Callbacks>(m_pressed);
                                        if (cb && cb->onChange) cb->onChange(nv);
                                    }
                                }
                            }
                        }
                    }
                    if (pw->type == WidgetType::ScrollBar) {
                        if (auto *sb = m_world.Get<ScrollBarData>(m_pressed); sb && sb->drag) {
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
                                auto *cb = m_world.Get<Callbacks>(m_pressed);
                                if (cb && cb->onScroll) cb->onScroll(noff);
                                if (cb && cb->onChange) cb->onChange(noff);
                            }
                        }
                    }
                    if (pw->type == WidgetType::Knob) {
                        if (auto *kd = m_world.Get<KnobData>(m_pressed); kd && kd->drag) {
                            float range = kd->max - kd->min;
                            if (range <= 0.f) range = 1.f;
                            float dragDeltaNorm = (kd->dragStartY - m_mousePos.y) * 0.005f;
                            float nv = SDL::Clamp(kd->dragStartVal + dragDeltaNorm * range, kd->min, kd->max);
                            if (nv != kd->val) {
                                kd->val = nv;
                                auto *cb = m_world.Get<Callbacks>(m_pressed);
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

                if (m_pressed != ECS::NullEntity && m_world.IsAlive(m_pressed)) { 
                    if (auto *st = m_world.Get<WidgetState>(m_pressed))
                        st->pressed = false;
                    if (m_pressed == m_hovered) {
                        auto *pw = m_world.Get<Widget>(m_pressed);
                        if (pw && Has(pw->behavior, BehaviorFlag::Enable)
                               && Has(pw->behavior, BehaviorFlag::Selectable))
                            _OnClick(m_pressed, *pw);
                    }
                    if (auto *sd = m_world.Get<SliderData>(m_pressed))    sd->drag = false;
                    if (auto *sb = m_world.Get<ScrollBarData>(m_pressed)) sb->drag = false;
                    if (auto *kd = m_world.Get<KnobData>(m_pressed))      kd->drag = false;
                    if (auto *ta = m_world.Get<TextAreaData>(m_pressed))  ta->selectDragging = false;
                    m_pressed = ECS::NullEntity;
                }
            }
        }

        // ── Helpers : drag des scrollbars inline de containers ────────────────────

        /// Parcourt tous les containers avec ContainerScrollState et met à jour
        /// scrollX/Y en fonction du déplacement de la souris (si drag en cours).
        void _UpdateContainerScrollDrags() {
            if (!m_mouseDown) return;
            m_world.Each<ContainerScrollState>([this](ECS::EntityId e, ContainerScrollState &css) {
                auto *lp = m_world.Get<LayoutProps>(e);
                auto *cr = m_world.Get<ComputedRect>(e);
                auto *w  = m_world.Get<Widget>(e);
                if (!lp || !cr || !w) return;

                const float innerW = cr->screen.w - lp->padding.left - lp->padding.right;
                const float innerH = cr->screen.h - lp->padding.top  - lp->padding.bottom;
                bool showX = false, showY = false;
                _ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);
                const float viewW = showY ? SDL::Max(0.f, innerW - lp->sbThickness) : innerW;
                const float viewH = showX ? SDL::Max(0.f, innerH - lp->sbThickness) : innerH;

                if (css.dragY && showY && lp->contentH > viewH) {
                    float barH   = viewH;
                    float ratio  = SDL::Clamp(viewH / lp->contentH, 0.05f, 1.f);
                    float thumbH = SDL::Max(lp->sbThickness * 2.f, barH * ratio);
                    float travel = barH - thumbH;
                    float dx     = m_mousePos.y - css.dragStartY_;
                    float maxOff = lp->contentH - viewH;
                    float newOff = (travel > 0.f)
                        ? SDL::Clamp(css.dragStartOffY + dx / travel * maxOff, 0.f, maxOff)
                        : 0.f;
                    lp->scrollY = newOff;
                    auto *cb = m_world.Get<Callbacks>(e);
                    if (cb && cb->onScroll) cb->onScroll(newOff);
                }
                if (css.dragX && showX && lp->contentW > viewW) {
                    float barW   = viewW;
                    float ratio  = SDL::Clamp(viewW / lp->contentW, 0.05f, 1.f);
                    float thumbW = SDL::Max(lp->sbThickness * 2.f, barW * ratio);
                    float travel = barW - thumbW;
                    float dx     = m_mousePos.x - css.dragStartX;
                    float maxOff = lp->contentW - viewW;
                    float newOff = (travel > 0.f)
                        ? SDL::Clamp(css.dragStartOff + dx / travel * maxOff, 0.f, maxOff)
                        : 0.f;
                    lp->scrollX = newOff;
                    auto *cb = m_world.Get<Callbacks>(e);
                    if (cb && cb->onScroll) cb->onScroll(newOff);
                }
            });
        }

        /// Teste si le clic courant tombe sur le thumb d'une scrollbar inline ;
        /// si oui, initialise le drag et retourne true (l'événement est consommé).
        bool _TryBeginContainerScrollDrag() {
            bool consumed = false;
            m_world.Each<ContainerScrollState>([this, &consumed](ECS::EntityId e, ContainerScrollState &css) {
                if (consumed) return;
                auto *lp = m_world.Get<LayoutProps>(e);
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
            m_world.Each<ContainerScrollState>([](ECS::EntityId, ContainerScrollState &css) {
                css.dragX = false;
                css.dragY = false;
            });
        }

        void _OnClick(ECS::EntityId e, const Widget &w) {
            auto *s = m_world.Get<Style>(e); 
            if (s && !s->clickSound.empty())
                if (auto sh = _EnsureAudio(s->clickSound))
                    _PlayAudio(sh);
            auto *cb = m_world.Get<Callbacks>(e);
            switch (w.type) {
                case WidgetType::Toggle:
                    if (auto *t = m_world.Get<ToggleData>(e)) {
                        t->checked = !t->checked;
                        if (cb && cb->onToggle)
                            cb->onToggle(t->checked);
                        if (cb && cb->onClick)
                            cb->onClick();
                    }
                    break;
                case WidgetType::RadioButton:
                    if (auto *r = m_world.Get<RadioData>(e); r && !r->checked) {
                        m_world.Each<RadioData>([&](ECS::EntityId eid, RadioData &rd) { if(rd.group==r->group) rd.checked=(eid==e); });
                        if (cb && cb->onToggle)
                            cb->onToggle(true);
                        if (cb && cb->onClick)
                            cb->onClick();
                    }
                    break;
                case WidgetType::ListBox: {
                    if (auto *lb = m_world.Get<ListBoxData>(e)) {
                        auto *cr2 = m_world.Get<ComputedRect>(e);
                        auto *lp2 = m_world.Get<LayoutProps>(e);
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
        }

        void _SetFocus(ECS::EntityId nf) {
            if (nf == m_focused)
                return;
            if (m_focused != ECS::NullEntity && m_world.IsAlive(m_focused)) { 
                if (auto *st = m_world.Get<WidgetState>(m_focused))
                    st->focused = false;
                auto *cb = m_world.Get<Callbacks>(m_focused);
                if (cb && cb->onFocusLose)
                    cb->onFocusLose();
            }
            m_focused = nf; 
            if (m_focused != ECS::NullEntity && m_world.IsAlive(m_focused)) {
                if (auto *st = m_world.Get<WidgetState>(m_focused))
                    st->focused = true;
                auto *cb = m_world.Get<Callbacks>(m_focused);
                if (cb && cb->onFocusGain)
                    cb->onFocusGain();
            }
        }

        [[nodiscard]] ECS::EntityId _HitTest(ECS::EntityId e, FPoint p) const {
            if (!m_world.IsAlive(e)) 
                return ECS::NullEntity;
            auto *w = m_world.Get<Widget>(e);
            auto *cr = m_world.Get<ComputedRect>(e);
            if (!w || !cr || !Has(w->behavior, BehaviorFlag::Visible))
                return ECS::NullEntity;
            if (!_Contains(cr->clip, p))
                return ECS::NullEntity;
            auto *ch = m_world.Get<Children>(e);
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
            if (m_focused == ECS::NullEntity || !m_world.IsAlive(m_focused)) return; 
            auto *w = m_world.Get<Widget>(m_focused);
            if (!w || !Has(w->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) return;

            if (w->type == WidgetType::Input) {
                auto *c = m_world.Get<Content>(m_focused);
                auto *cb = m_world.Get<Callbacks>(m_focused);
                if (!c) return;
                c->text.insert((size_t)c->cursor, txt);
                c->cursor += (int)SDL::Strlen(txt);
                if (cb && cb->onTextChange) cb->onTextChange(c->text);
            } else if (w->type == WidgetType::TextArea) {
                auto *ta = m_world.Get<TextAreaData>(m_focused);
                auto *cb = m_world.Get<Callbacks>(m_focused);
                if (!ta) return;
                ta->Insert(txt);
                if (cb && cb->onTextChange) cb->onTextChange(ta->text);
            }
        }

        void _HandleKeyDown(SDL::Keycode k, SDL::Keymod mod) {
            if (m_focused != ECS::NullEntity && m_world.IsAlive(m_focused)) { 
                auto *fw = m_world.Get<Widget>(m_focused);
                if (fw && fw->type == WidgetType::TextArea
                       && Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
                    _HandleKeyDownTextArea(k, mod);
                    return;
                }
                if (fw && fw->type == WidgetType::ListBox
                       && Has(fw->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) {
                    _HandleKeyDownListBox(k);
                    return;
                }
            }

            if (k == SDL::KEYCODE_TAB) {
                bool shiftPressed = (mod & SDL::KMOD_SHIFT);
                _CycleFocus(shiftPressed);
                return;
            }
            if (m_focused == ECS::NullEntity || !m_world.IsAlive(m_focused)) return; 

            auto *w = m_world.Get<Widget>(m_focused);
            if (!w || w->type != WidgetType::Input || !Has(w->behavior, BehaviorFlag::Enable | BehaviorFlag::Focusable)) return;

            auto *c = m_world.Get<Content>(m_focused);
            auto *cb = m_world.Get<Callbacks>(m_focused);
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

        /// Full keyboard handler for the focused TextArea.
        void _HandleKeyDownTextArea(SDL::Keycode k, SDL::Keymod mod) {
            auto *ta = m_world.Get<TextAreaData>(m_focused);
            auto *cb = m_world.Get<Callbacks>(m_focused);
            if (!ta) return;
            
            const bool ctrl  = (mod & SDL::KMOD_CTRL)  != 0;
            const bool shift = (mod & SDL::KMOD_SHIFT) != 0;

            auto moveCursor = [&](int newPos) {
                if (shift) {
                    if (!ta->HasSelection()) ta->selAnchor = ta->cursorPos;
                    ta->cursorPos = newPos;
                    ta->selFocus  = newPos;
                } else {
                    ta->cursorPos = newPos;
                    ta->ClearSelection();
                }
            };

            switch (k) { 
            // ── Focus / Tab ────────────────────────────────────────────────────
            case SDL::KEYCODE_TAB:
                if (ctrl) break; // Ctrl+Tab → cycle focus (fall through)
                if (shift) {
                    // Shift+Tab: back-dedent — remove up to tabSize leading spaces
                    int ls = ta->LineStart(ta->LineOf(ta->cursorPos));
                    int spaces = 0;
                    while (ls + spaces < (int)ta->text.size()
                           && ta->text[ls + spaces] == ' '
                           && spaces < ta->tabSize) ++spaces;
                    if (spaces > 0) {
                        ta->text.erase(ls, spaces);
                        ta->_ShiftSpans(ls, -spaces); // NOLINT (private, but same TU)
                        ta->cursorPos = std::max(ls, ta->cursorPos - spaces);
                        ta->ClearSelection();
                        if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                    }
                } else {
                    ta->Insert("\t");
                    if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                }
                return;
            case SDL::KEYCODE_ESCAPE:
                _SetFocus(ECS::NullEntity);
                return;

            // ── Newline ────────────────────────────────────────────────────────
            case SDL::KEYCODE_RETURN:
            case SDL::KEYCODE_RETURN2:
            case SDL::KEYCODE_KP_ENTER:
                ta->Insert("\n");
                if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                return;

            // ── Delete ─────────────────────────────────────────────────────────
            case SDL::KEYCODE_BACKSPACE:
                if (ta->HasSelection()) {
                    ta->DeleteSelection();
                } else if (ta->cursorPos > 0) {
                    ta->text.erase((size_t)(ta->cursorPos - 1), 1);
                    ta->_ShiftSpans(ta->cursorPos - 1, -1);
                    --ta->cursorPos;
                }
                if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                return;
            case SDL::KEYCODE_DELETE:
                if (ta->HasSelection()) {
                    ta->DeleteSelection();
                } else if (ta->cursorPos < (int)ta->text.size()) {
                    ta->text.erase((size_t)ta->cursorPos, 1);
                    ta->_ShiftSpans(ta->cursorPos, -1);
                }
                if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                return;

            // ── Horizontal motion ──────────────────────────────────────────────
            case SDL::KEYCODE_LEFT:
                if (!shift && ta->HasSelection())
                    moveCursor(ta->SelMin());
                else if (ctrl)
                    moveCursor(_TAWordLeft(ta, ta->cursorPos));
                else
                    moveCursor(SDL::Max(0, ta->cursorPos - 1));
                return;
            case SDL::KEYCODE_RIGHT:
                if (!shift && ta->HasSelection())
                    moveCursor(ta->SelMax());
                else if (ctrl)
                    moveCursor(_TAWordRight(ta, ta->cursorPos));
                else
                    moveCursor(SDL::Min((int)ta->text.size(), ta->cursorPos + 1));
                return;

            // ── Vertical motion ────────────────────────────────────────────────
            case SDL::KEYCODE_UP: {
                int line = ta->LineOf(ta->cursorPos);
                if (line > 0) {
                    int col = ta->ColOf(ta->cursorPos);
                    int ns = ta->LineStart(line - 1);
                    int ne = ta->LineEnd(line - 1);
                    moveCursor(std::min(ns + col, ne));
                }
                return;
            }
            case SDL::KEYCODE_DOWN: {
                int line = ta->LineOf(ta->cursorPos);
                if (line < ta->LineCount() - 1) {
                    int col = ta->ColOf(ta->cursorPos);
                    int ns = ta->LineStart(line + 1);
                    int ne = ta->LineEnd(line + 1);
                    moveCursor(std::min(ns + col, ne));
                }
                return;
            }

            // ── Home / End ─────────────────────────────────────────────────────
            case SDL::KEYCODE_HOME:
                if (ctrl) moveCursor(0);
                else      moveCursor(ta->LineStart(ta->LineOf(ta->cursorPos)));
                return;
            case SDL::KEYCODE_END:
                if (ctrl) moveCursor((int)ta->text.size());
                else      moveCursor(ta->LineEnd(ta->LineOf(ta->cursorPos)));
                return;

            // ── Page Up / Down ─────────────────────────────────────────────────
            case SDL::KEYCODE_PAGEUP: {
                auto *s2 = m_world.Get<Style>(m_focused);
                if (s2) {
                    float lineH = _TH(*s2) + 2.f;
                    int linesPerPage = SDL::Max(1, (int)(_TAViewH() / lineH));
                    int line = SDL::Max(0, ta->LineOf(ta->cursorPos) - linesPerPage);
                    moveCursor(ta->LineStart(line));
                }
                return;
            }
            case SDL::KEYCODE_PAGEDOWN: {
                auto *s2 = m_world.Get<Style>(m_focused);
                if (s2) {
                    float lineH = _TH(*s2) + 2.f;
                    int linesPerPage = SDL::Max(1, (int)(_TAViewH() / lineH));
                    int line = SDL::Min(ta->LineCount() - 1, ta->LineOf(ta->cursorPos) + linesPerPage);
                    moveCursor(ta->LineStart(line));
                }
                return;
            }

            // ── Clipboard ─────────────────────────────────────────────────────
            case SDL::KEYCODE_A:
                if (ctrl) {
                    ta->SetSelection(0, (int)ta->text.size());
                    ta->cursorPos = (int)ta->text.size();
                }
                return;
            case SDL::KEYCODE_C:
                if (ctrl && ta->HasSelection()) {
                    try { SDL::SetClipboardText(ta->GetSelectedText()); } catch (...) {}
                }
                return;
            case SDL::KEYCODE_X:
                if (ctrl) {
                    if (ta->HasSelection()) {
                        try { SDL::SetClipboardText(ta->GetSelectedText()); } catch (...) {}
                        ta->DeleteSelection();
                        if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                    }
                }
                return;
            case SDL::KEYCODE_V:
                if (ctrl) {
                    try {
                        if (SDL::HasClipboardText()) {
                            auto clip = SDL::GetClipboardText();
                            ta->Insert(static_cast<std::string_view>(clip));
                            if (cb && cb->onTextChange) cb->onTextChange(ta->text);
                        }
                    } catch (...) {}
                }
                return;
            case SDL::KEYCODE_Z:
                if (ctrl && !shift) {
                    // Undo not implemented — just reset selection
                    ta->ClearSelection();
                }
                return;

            default:
                break;
            }
        }

        // TextArea helpers ──────────────────────────────────────────────────────────

        /// View height of the focused TextArea (0 if not a TextArea).
        [[nodiscard]] float _TAViewH() const noexcept {
            if (m_focused == ECS::NullEntity || !m_world.IsAlive(m_focused)) return 0.f; 
            auto *cr = m_world.Get<ComputedRect>(m_focused);
            auto *lp = m_world.Get<LayoutProps>(m_focused);
            if (!cr || !lp) return 0.f;
            return SDL::Max(0.f, cr->screen.h - lp->padding.top - lp->padding.bottom);
        }

        /// Move left to the start of the previous word.
        [[nodiscard]] static int _TAWordLeft(const TextAreaData *ta, int pos) noexcept {
            if (pos <= 0) return 0; 
            --pos;
            while (pos > 0 && !std::isalnum((unsigned char)ta->text[pos - 1])) --pos;
            while (pos > 0 && std::isalnum((unsigned char)ta->text[pos - 1])) --pos;
            return pos;
        }

        /// Move right to the end of the next word.
        [[nodiscard]] static int _TAWordRight(const TextAreaData *ta, int pos) noexcept {
            int sz = (int)ta->text.size();
            while (pos < sz && !std::isalnum((unsigned char)ta->text[pos])) ++pos;
            while (pos < sz && std::isalnum((unsigned char)ta->text[pos])) ++pos;
            return pos;
        }

        /// Pixel X offset for a column within a line (tabs expanded).
        [[nodiscard]] float _TALineX(const TextAreaData *ta, int line, int col, const Style &s) { 
            int lineStart = ta->LineStart(line);
            col = std::clamp(col, 0, ta->LineEnd(line) - lineStart);
            float x = 0.f;
            float charW = (s.fontSize > 0.f) ? s.fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#if UI_HAS_TTF
            if (!s.usedDebugFont && !s.fontKey.empty() && s.fontSize > 0.f) {
                if (auto font = _EnsureFont(s.fontKey, s.fontSize)) {
                    int sw = 0, sh = 0;
                    font.GetStringSize(" ", &sw, &sh);
                    charW = (float)sw;
                }
            }
#endif
            int colCount = 0;
            for (int i = 0; i < col; ++i) {
                unsigned char ch = (unsigned char)ta->text[lineStart + i];
                if (ch == '\t') {
                    int spaces = ta->tabSize - (colCount % ta->tabSize);
                    if (spaces == 0) spaces = ta->tabSize;
                    x += spaces * charW;
                    colCount += spaces;
                } else {
#if UI_HAS_TTF
                    if (!s.usedDebugFont && !s.fontKey.empty() && s.fontSize > 0.f) {
                        if (auto font = _EnsureFont(s.fontKey, s.fontSize)) {
                            char buf[2] = {(char)ch, 0};
                            int cw = 0, ch2 = 0;
                            font.GetStringSize(buf, &cw, &ch2);
                            x += (float)cw;
                        } else { x += charW; }
                    } else
#endif
                    { x += charW; }
                    ++colCount;
                }
            }
            return x;
        }

        /// Convert a pixel position (relative to content area origin) to a document offset.
        [[nodiscard]] int _TAHitPos(const TextAreaData *ta, float px, float py, const Style &s) { 
            float lineH = _TH(s) + 2.f;
            float pyDoc = py + ta->scrollY;
            int line = std::clamp((int)(pyDoc / lineH), 0, ta->LineCount() - 1);
            int lineStart = ta->LineStart(line);
            int lineEnd   = ta->LineEnd(line);

            // Walk character by character finding the closest hit.
            float charW = (s.fontSize > 0.f) ? s.fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
            int best = lineStart;
            float bestDist = SDL::Abs(px);
            for (int i = lineStart; i <= lineEnd; ++i) {
                float xOff = _TALineX(ta, line, i - lineStart, s);
                float dist = SDL::Abs(px - xOff);
                if (dist < bestDist) { bestDist = dist; best = i; }
                if (xOff > px + charW * 3.f) break;
            }
            return best;
        }

        /// Scroll the TextArea so the cursor is visible.
        void _TAScrollToCursor(TextAreaData *ta, const ComputedRect *cr, const LayoutProps *lp, const Style *s) { 
            if (!ta || !cr || !lp || !s) return;
            float lineH = _TH(*s) + 2.f;
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

        void _HandleScroll(float dx, float dy) {
            ECS::EntityId e = _HitTest(m_root, m_mousePos); 
            while (e != ECS::NullEntity && m_world.IsAlive(e)) {
                auto *w  = m_world.Get<Widget>(e);
                auto *lp = m_world.Get<LayoutProps>(e);

                // ListBox has its own internal scroll.
                if (w && w->type == WidgetType::ListBox) {
                    if (auto *lb = m_world.Get<ListBoxData>(e)) {
                        auto *cr2 = m_world.Get<ComputedRect>(e);
                        auto *lp2 = m_world.Get<LayoutProps>(e);
                        if (cr2 && lp2 && dy != 0.f) {
                            float viewH  = cr2->screen.h - lp2->padding.top - lp2->padding.bottom;
                            float total  = (float)lb->items.size() * lb->itemHeight;
                            float maxOff = SDL::Max(0.f, total - viewH);
                            lp2->scrollY = SDL::Clamp(lp2->scrollY - dy * lb->itemHeight * 2.f, 0.f, maxOff);
                        }
                    }
                    return;
                }
                // TextArea has its own internal scroll.
                if (w && w->type == WidgetType::TextArea) {
                    if (auto *ta = m_world.Get<TextAreaData>(e)) {
                        auto *s2 = m_world.Get<Style>(e);
                        auto *cr = m_world.Get<ComputedRect>(e);
                        if (s2 && cr && dy != 0.f) {
                            float lineH  = _TH(*s2) + 2.f;
                            float viewH  = cr->screen.h - (lp ? lp->padding.top + lp->padding.bottom : 0.f);
                            float maxS   = SDL::Max(0.f, ta->LineCount() * lineH - viewH);
                            ta->scrollY  = SDL::Clamp(ta->scrollY - dy * lineH * 3.f, 0.f, maxS);
                        }
                    }
                    return;
                }
                if (!w || !lp) break;
                
                // ScrollableX/Y = scroll permanent ; AutoScrollableX/Y = scroll si débordement.
                bool scrollableV = Has(w->behavior, BehaviorFlag::ScrollableY | BehaviorFlag::AutoScrollableY);
                bool scrollableH = Has(w->behavior, BehaviorFlag::ScrollableX | BehaviorFlag::AutoScrollableX);

                if (scrollableV || scrollableH) {
                    auto *cr = m_world.Get<ComputedRect>(e);
                    const float innerW = cr ? (cr->screen.w - lp->padding.left - lp->padding.right) : 0.f;
                    const float innerH = cr ? (cr->screen.h - lp->padding.top  - lp->padding.bottom) : 0.f;

                    bool showX = false, showY = false;
                    _ContainerScrollbars(*w, *lp, innerW, innerH, showX, showY);
                    const float viewW = showY ? SDL::Max(0.f, innerW - lp->sbThickness) : innerW;
                    const float viewH = showX ? SDL::Max(0.f, innerH - lp->sbThickness) : innerH;

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

                    if (lastScrollX != lp->scrollX || lastScrollY != lp->scrollY) {
                        auto *s = m_world.Get<Style>(e);
                        if (s && !s->scrollSound.empty())
                            if (auto sh = _EnsureAudio(s->scrollSound))
                                _PlayAudio(sh);
                    }
                    return;
                }
                auto *par = m_world.Get<Parent>(e);
                e = (par && par->id != ECS::NullEntity) ? par->id : ECS::NullEntity;
            }
        }

        void _CanvasEvent(const SDL::Event& evt) {
            // Forward SDL events to every visible, enabled Canvas widget whose
            // clip rect contains the mouse cursor OR that currently holds focus.
            // This allows the game canvas to receive keyboard events even when
            // the mouse is elsewhere (e.g. paused panels on top).
            m_world.Each<CanvasData, Widget, ComputedRect>(
                [&](ECS::EntityId e, CanvasData& cd, Widget& w, ComputedRect& cr) {
                    if (!Has(w.behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;
                    if (!cd.eventCb) return;
                    const bool mouseInside = _Contains(cr.clip, m_mousePos);
                    const bool hasFocus    = (m_focused == e);
                    if (mouseInside || hasFocus)
                        cd.eventCb(const_cast<SDL::Event&>(evt));
                });
        }

        bool IsEffectivelyVisible(ECS::EntityId e, ECS::World& world) {
            while (e != ECS::NullEntity) { 
                auto* w = world.Get<Widget>(e);
                if (!w || !Has(w->behavior, BehaviorFlag::Visible)) return false;
                
                auto* p = world.Get<Parent>(e);
                e = p ? p->id : ECS::NullEntity;
            }
            return true;
        }

        void _CollectFocusables(ECS::EntityId e, std::vector<ECS::EntityId> &out) const {
            auto *w = m_world.Get<Widget>(e);
            if (!w || !Has(w->behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;

            if (Has(w->behavior, BehaviorFlag::Focusable))
                out.push_back(e);

            if (auto *ch = m_world.Get<Children>(e)) {
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

        // ── §8.10b  Canvas update ─────────────────────────────────────────────────

        void _ProcessCanvasUpdate(float dt) {
            // Call updateCb for every visible, enabled Canvas widget.
            // Layout hasn't run yet this frame, so avoid depending on ComputedRect here.
            m_world.Each<CanvasData, Widget>([dt](ECS::EntityId, CanvasData& cd, Widget& w) {
                if (!Has(w.behavior, BehaviorFlag::Visible | BehaviorFlag::Enable)) return;
                if (cd.updateCb) cd.updateCb(dt);
            });
        }

        // ── §8.11  Animate ────────────────────────────────────────────────────────

        void _ProcessAnimate(float dt) {
            if (dt <= 0.f)
                return;
                
            m_world.Each<ToggleData>([dt](ECS::EntityId, ToggleData &t) { 
                float target = t.checked ? 1.f : 0.f;
                t.animT += (target - t.animT) * SDL::Min(1.f, dt * 12.f); 
                t.animT = SDL::Clamp(t.animT, 0.f, 1.f);
            });
            
            if (m_focused != ECS::NullEntity && m_world.IsAlive(m_focused)) {
                if (auto *c = m_world.Get<Content>(m_focused)) {
                    c->blinkTimer += dt;
                    if (c->blinkTimer > 1.f) c->blinkTimer -= 1.f;
                } 
                if (auto *ta = m_world.Get<TextAreaData>(m_focused)) {
                    ta->blinkTimer += dt;
                    if (ta->blinkTimer > 1.f) ta->blinkTimer -= 1.f;
                    // Keep cursor visible after scroll
                    auto *cr2 = m_world.Get<ComputedRect>(m_focused);
                    auto *lp2 = m_world.Get<LayoutProps>(m_focused);
                    auto *s2  = m_world.Get<Style>(m_focused);
                    _TAScrollToCursor(ta, cr2, lp2, s2);
                }
            }
        }

        // ── §8.12  Render ─────────────────────────────────────────────────────────

        void _ProcessRender() {
            m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
            _RenderNode(m_root);
            m_renderer.ResetClipRect();
        }

        void _RenderNode(ECS::EntityId e) {
           if (!m_world.IsAlive(e)) return; 
            auto *w  = m_world.Get<Widget>(e);
            auto *cr = m_world.Get<ComputedRect>(e);
            if (!w || !cr) return;

            if (!Has(w->behavior, BehaviorFlag::Visible)) return;
            
            if (cr->outer_clip.w <= 0.f || cr->outer_clip.h <= 0.f) return;

            m_renderer.SetClipRect(FRectToRect(cr->outer_clip));
            _DrawWidget(e);
            
            auto *ch = m_world.Get<Children>(e);
            if (ch) {
                for (ECS::EntityId c : ch->ids) {
                    _RenderNode(c);
                }
            }
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
            if (!s.usedDebugFont && !s.fontKey.empty() && s.fontSize > 0.f) {
                if (auto font = _EnsureFont(s.fontKey, s.fontSize)) {
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

        /// Height of one text line in pixels, using TTF metrics when available.
        [[nodiscard]] float _TH(const Style &s) noexcept {
#if UI_HAS_TTF
            if (!s.usedDebugFont && !s.fontKey.empty() && s.fontSize > 0.f)
                if (auto f = _EnsureFont(s.fontKey, s.fontSize))
                    return (float)f.GetHeight();
#endif
            return (s.fontSize > 0.f) ? s.fontSize : SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
        }

        /// Width of a string in pixels, using TTF metrics when available.
        [[nodiscard]] float _TW(const std::string &t, const Style &s) {
            if (t.empty()) return 0.f;
#if UI_HAS_TTF
            if (!s.usedDebugFont && !s.fontKey.empty() && s.fontSize > 0.f) {
                if (auto font = _EnsureFont(s.fontKey, s.fontSize)) {
                    int w = 0, h = 0;
                    font.GetStringSize(t, &w, &h);
                    return (float)w;
                }
            }
#endif
            return (float)t.size() * ((s.fontSize > 0.f) ? s.fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE);
        }

        // ── Per-type draw ─────────────────────────────────────────────────────────

        void _DrawWidget(ECS::EntityId e) {
            auto *w = m_world.Get<Widget>(e);
            auto *s = m_world.Get<Style>(e);
            auto *st = m_world.Get<WidgetState>(e);
            auto *cr = m_world.Get<ComputedRect>(e);
            if (!w || !s || !st || !cr) return;
            const FRect &r = cr->screen;

            // ── Tileset skin (9-slice) — replaces solid-colour background ────────────
            // When present, the tileset is drawn first.  For Canvas widgets the game
            // content is rendered on top afterwards; for all other widget types the
            // tileset is the only background and we skip the normal draw entirely.
            auto *ts = m_world.Get<TilesetStyle>(e);
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
                    _DrawListBox(e, r, *s, *st);
                    break;
                case WidgetType::Graph:
                    _DrawGraph(e, r, *s, *st);
                    break;
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

            // ── Inline scrollbars ─────────────────────────────────────────────────
            auto *lp  = m_world.Get<LayoutProps>(e);
            auto *css = m_world.Get<ContainerScrollState>(e);
            if (!lp || !css) return;

            // Inner content area (padding applied, no scrollbar reservation yet).
            const float innerW = r.w - lp->padding.left - lp->padding.right;
            const float innerH = r.h - lp->padding.top  - lp->padding.bottom;

            bool showX = false, showY = false;
            _ContainerScrollbars(w, *lp, innerW, innerH, showX, showY);

            // Effective content area after reserving scrollbar space.
            const float viewW = showY ? SDL::Max(0.f, innerW - lp->sbThickness) : innerW;
            const float viewH = showX ? SDL::Max(0.f, innerH - lp->sbThickness) : innerH;
            const float t     = lp->sbThickness;

            // Clamp scroll offsets so they remain valid.
            lp->scrollX = SDL::Clamp(lp->scrollX, 0.f, SDL::Max(0.f, lp->contentW - viewW));
            lp->scrollY = SDL::Clamp(lp->scrollY, 0.f, SDL::Max(0.f, lp->contentH - viewH));

            // Track colour (semi-transparent overlay on the container bg).
            SDL::Color trackCol = s.track;
            trackCol.a = SDL::Clamp8((int)(trackCol.a * s.opacity * 0.85f));

            // ── Vertical scrollbar ────────────────────────────────────────────────
            css->thumbY = {}; 
            if (showY) {
                FRect barY = {r.x + r.w - t, r.y + lp->padding.top, t, viewH};
                // Track
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

            // ── Horizontal scrollbar ──────────────────────────────────────────────
            css->thumbX = {}; 
            if (showX) {
                float barW  = showY ? viewW : innerW;     // leave corner gap when both shown
                FRect barX  = {r.x + lp->padding.left, r.y + r.h - t, barW, t};
                // Track
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

            // ── Corner fill (when both bars shown) ────────────────────────────────
            if (showX && showY) {
                FRect corner = {r.x + r.w - t, r.y + r.h - t, t, t};
                _FillRR(corner, trackCol, SDL::FCorners(0.f), 1.f);
            }
        }
        void _DrawLabel(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
            auto *c = m_world.Get<Content>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
            if (!c)
                return;
            SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabled : st.hovered ? s.textHovered
                                                                     : s.textColor;
            _Text(e, c->text, r.x + (lp ? lp->padding.left : 4.f), r.y + (r.h - _TH(s)) * 0.5f, tc, s.opacity, s);
        }
        void _DrawButton(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
            SDL::Color bgColor = !Has(w.behavior, BehaviorFlag::Enable) ? s.bgDisabled
                : (st.pressed ? s.bgPressed
                    : (st.hovered ? s.bgHovered : s.bgColor));
            SDL::Color bdColor = (m_focused == e) ? s.bdFocused
                : (st.hovered ? s.bdHovered : s.bdColor);
            SDL::Color tc = !Has(w.behavior, BehaviorFlag::Enable) ? s.textDisabled
                : (st.hovered ? s.textHovered : s.textColor);
            _FillRR(r, bgColor, s.radius, s.opacity);
            _StrokeRR(r, bdColor, s.borders, s.radius, s.opacity);
            auto *c = m_world.Get<Content>(e);
            if (c && !c->text.empty()) {
                float tw = _TW(c->text, s), th = _TH(s);
                _Text(e, c->text, r.x + (r.w - tw) * 0.5f, r.y + (r.h - th) * 0.5f, tc, s.opacity, s);
            }
        }
        void _DrawToggle(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) { 
            auto *t = m_world.Get<ToggleData>(e);
            auto *c = m_world.Get<Content>(e);
            auto *w = m_world.Get<Widget>(e);
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
                _Text(e, c->text, tr_.x + TW + 10.f, r.y + (r.h - _TH(s)) * 0.5f, col, s.opacity, s);
            }
        }
        void _DrawRadio(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st) { 
            auto *rd = m_world.Get<RadioData>(e);
            auto *c = m_world.Get<Content>(e);
            auto *w = m_world.Get<Widget>(e);
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
                _Text(e, c->text, r.x + 30.f, r.y + (r.h - _TH(s)) * 0.5f, tc, s.opacity, s);
            }
        } 
        void _DrawSlider(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
            auto *sd = m_world.Get<SliderData>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
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
            auto *sb = m_world.Get<ScrollBarData>(e);
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
            auto *sd = m_world.Get<SliderData>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
            if (!sd || !lp) return;
            float tx = r.x + lp->padding.left, tw = r.x + r.w - lp->padding.right - tx, norm = (sd->max > sd->min) ? (sd->val - sd->min) / (sd->max - sd->min) : 0.f;
            FRect tr_ = {tx, r.y + (r.h - 8.f) * 0.5f, tw, 8.f};
            _FillRR(tr_, s.track, FCorners(4.f), s.opacity);
            if (norm > 0.f)
                _FillRR({tx, tr_.y, tw * norm, tr_.h}, s.fill, FCorners(4.f), s.opacity);
            _StrokeRR(tr_, s.bdColor, s.borders, FCorners(4.f), s.opacity);
        }
        void _DrawSeparator(const FRect &r, const Style &s) { _FillRect({r.x, r.y + r.h * 0.5f, r.w, 1.f}, s.separator, s.opacity); }
        void _DrawInput(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
            auto *c = m_world.Get<Content>(e);
            auto *lp = m_world.Get<LayoutProps>(e);
            if (!c || !lp) return;
            bool foc = (m_focused == e);
            SDL::Color bgColor = !Has(w.behavior, BehaviorFlag::Enable) ? s.bgDisabled : foc ? s.bgHovered
                                                    : st.hovered ? SDL::Color{30, 32, 44, 255}
                                                                 : s.bgColor;
            SDL::Color bdColor = !Has(w.behavior, BehaviorFlag::Enable) ? s.bdColor : foc ? s.bdFocused
                                                : st.hovered ? s.bdHovered
                                                             : s.bdColor;
            _FillRR(r, bgColor, s.radius, s.opacity);
            _StrokeRR(r, bdColor, SDL::Max(s.borders, 1.f), s.radius, s.opacity);
            float tx_ = r.x + lp->padding.left, ty_ = r.y + (r.h - _TH(s)) * 0.5f;
            bool showPH = c->text.empty() && !c->placeholder.empty() && !foc;
            if (showPH)
                _Text(e, c->placeholder, tx_, ty_, s.textPlaceholder, s.opacity, s);
            else {
                _Text(e, c->text, tx_, ty_, Has(w.behavior, BehaviorFlag::Enable) ? s.textColor : s.textDisabled, s.opacity, s);
                if (foc && c->blinkTimer < 0.5f) {
                    // Cursor X: width of text before cursor (TTF-aware)
                    float cx_ = tx_ + _TW(c->text.substr(0, (size_t)SDL::Max(0, c->cursor)), s);
                    _FillRect({cx_, ty_, 1.f, _TH(s)}, s.textColor, s.opacity);
                }
            }
        }
        // ── §8.x  TextArea ────────────────────────────────────────────────────────

        void _DrawTextArea(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) { 
            auto *ta  = m_world.Get<TextAreaData>(e);
            auto *lp  = m_world.Get<LayoutProps>(e);
            auto *cnt = m_world.Get<Content>(e);
            if (!ta || !lp) return;

            const bool foc  = (m_focused == e);
            const bool enabled = Has(w.behavior, BehaviorFlag::Enable);

            // ── Background & border ───────────────────────────────────────────────
            SDL::Color bgC = !enabled ? s.bgDisabled
                           : foc      ? s.bgFocused
                           : st.hovered ? SDL::Color{30, 32, 44, 255}
                                        : s.bgColor;
            SDL::Color bdC = !enabled ? s.bdDisabled
                           : foc      ? s.bdFocused
                           : st.hovered ? s.bdHovered : s.bdColor;
            _FillRR(r, bgC, s.radius, s.opacity);
            _StrokeRR(r, bdC, SDL::Max(s.borders, 1.f), s.radius, s.opacity);

            // ── Placeholder ───────────────────────────────────────────────────────
            if (ta->text.empty() && cnt && !cnt->placeholder.empty()) {
                _Text(e, cnt->placeholder, r.x + lp->padding.left, r.y + lp->padding.top,
                      s.textPlaceholder, s.opacity, s);
                return;
            }

            // ── Clip to content area ──────────────────────────────────────────────
            const float cX = r.x + lp->padding.left;
            const float cY = r.y + lp->padding.top;
            const float cW = SDL::Max(0.f, r.w + lp->padding.left + lp->padding.right);
            const float cH = SDL::Max(0.f, r.h + lp->padding.top  + lp->padding.bottom);

            SDL_Rect clip{(int)cX, (int)cY, (int)cW, (int)cH};
            SDL_SetRenderClipRect(m_renderer.Get(), &clip);

            const float lineH = _TH(s) + 2.f;
            const float startY = cY - ta->scrollY;

            // Visible line range
            int firstLine = SDL::Max(0, (int)(ta->scrollY / lineH) - 1);
            int lastLine  = SDL::Min(ta->LineCount() - 1, (int)((ta->scrollY + cH) / lineH) + 1);

            // ── Selection rects ───────────────────────────────────────────────────
            if (ta->HasSelection()) {
                int selMin = ta->SelMin(), selMax = ta->SelMax();
                SDL::Color hlC = ta->highlightColor;
                hlC.a = (Uint8)(hlC.a * s.opacity);
                m_renderer.SetDrawColor(hlC);
                m_renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);

                for (int ln = firstLine; ln <= lastLine; ++ln) {
                    int ls = ta->LineStart(ln);
                    int le = ta->LineEnd(ln);
                    if (le < selMin || ls > selMax) continue;
                    int hiStart = SDL::Max(ls, selMin);
                    int hiEnd   = SDL::Min(le, selMax);
                    float x0 = cX + _TALineX(ta, ln, hiStart - ls, s);
                    float x1 = cX + _TALineX(ta, ln, hiEnd   - ls, s);
                    if (hiEnd == le && selMax > le) x1 = cX + cW; // extend to EOL
                    float ly = startY + ln * lineH;
                    m_renderer.RenderFillRect(SDL::FRect{x0, ly, x1 - x0, lineH});
                }
            }

            // ── Lines of text ─────────────────────────────────────────────────────
            for (int ln = firstLine; ln <= lastLine; ++ln) {
                int ls = ta->LineStart(ln);
                int le = ta->LineEnd(ln);
                float ly = startY + ln * lineH;
                if (ly + lineH < cY || ly > cY + cH) continue;

                // Draw this line run-by-run (spans).
                float xOff = cX;
                for (int ci = ls; ci < le; ) {
                    // Find next span boundary.
                    int spanEnd = le;
                    const TextSpanStyle *spanStyle = ta->SpanStyleAt(ci);
                    // Advance until span changes.
                    int ni = ci + 1;
                    while (ni < le && ta->SpanStyleAt(ni) == spanStyle) ++ni;
                    spanEnd = ni;

                    // Expand tab runs within this span.
                    std::string run;
                    float charW = (s.fontSize > 0.f) ? s.fontSize : (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;
#if UI_HAS_TTF
                    if (!s.usedDebugFont && !s.fontKey.empty() && s.fontSize > 0.f) {
                        if (auto font = _EnsureFont(s.fontKey, s.fontSize)) {
                            int sw = 0, sh = 0;
                            font.GetStringSize(" ", &sw, &sh);
                            charW = (float)sw;
                        }
                    }
#endif
                    int visualCol = (ci > ls) ? (int)((xOff - cX) / charW) : 0; // approx
                    for (int k = ci; k < spanEnd; ++k) {
                        char ch = ta->text[k];
                        if (ch == '\t') {
                            // Flush current run, advance tab stop.
                            if (!run.empty()) {
                                SDL::Color tc = enabled ? s.textColor : s.textDisabled;
                                if (spanStyle && spanStyle->color.a > 0) tc = spanStyle->color;
                                _Text(e, run, xOff, ly, tc, s.opacity, s);
                                xOff += _TW(run, s);
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
                        xOff += _TW(run, s);
                    }
                    ci = spanEnd;
                }
            }

            // ── Cursor ────────────────────────────────────────────────────────────
            if (foc && ta->blinkTimer < 0.5f) {
                int curLine = ta->LineOf(ta->cursorPos);
                int curCol  = ta->ColOf(ta->cursorPos);
                float cx_   = cX + _TALineX(ta, curLine, curCol, s);
                float cy_   = startY + curLine * lineH;
                _FillRect({cx_, cy_, 1.5f, lineH}, s.textColor, s.opacity);
            }

            // ── Restore clip ──────────────────────────────────────────────────────
            SDL_SetRenderClipRect(m_renderer.Get(), nullptr);
        }
        
        void _DrawKnob(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &st, const Widget &w) {
            auto *kd = m_world.Get<KnobData>(e);
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
            float aRad = (135.f + norm * 270.f) * (SDL_PI_F / 180.f);
            float lx = cx_ + SDL::Cos(aRad) * iR;
            float ly = cy_ + SDL::Sin(aRad) * iR;
            
            _FillRR({lx - 4.f, ly - 4.f, 8.f, 8.f}, fillC, SDL::FCorners(4.f), s.opacity);
        }
        void _DrawImage(ECS::EntityId e, const FRect &r, const Style &s, const WidgetState &) { 
            auto *d = m_world.Get<ImageData>(e);
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
            auto *cd = m_world.Get<CanvasData>(e);
            if (!cd || !cd->renderCb) return;

            // The renderCb typically sets its own SDL viewport/scissor.
            // We save/restore the outer clip rect so the UI compositing
            // remains correct after the canvas has drawn into its rect.
            SDL_Rect prevClip = {};
            bool hadClip = SDL_GetRenderClipRect(m_renderer.Get(), &prevClip);

            cd->renderCb(m_renderer, r);

            // Restore renderer state for the parent UI layer.
            if (hadClip && prevClip.w > 0)
                SDL_SetRenderClipRect(m_renderer.Get(), &prevClip);
            else
                SDL_SetRenderClipRect(m_renderer.Get(), nullptr);
            SDL_SetRenderViewport(m_renderer.Get(), nullptr);
        }

        // ── §8.12b  ListBox draw ──────────────────────────────────────────────

        void _DrawListBox(ECS::EntityId e, const FRect &r, const Style &s,
                          const WidgetState &st) {
            auto *lb  = m_world.Get<ListBoxData>(e);
            auto *lp  = m_world.Get<LayoutProps>(e);
            auto *css = m_world.Get<ContainerScrollState>(e);
            if (!lb || !lp || !css) return;

            // Background + focused/hovered border
            SDL::Color bg = st.focused ? s.bgFocused
                          : st.hovered ? s.bgHovered
                          : s.bgColor;
            _FillRR(r, bg, s.radius, s.opacity);
            _StrokeRR(r, st.focused ? s.bdFocused : st.hovered ? s.bdHovered : s.bdColor,
                      s.borders, s.radius, s.opacity);

            const float ih     = lb->itemHeight;
            const float total  = (float)lb->items.size() * ih;
            const float innerH = r.h - lp->padding.top - lp->padding.bottom;
            const float t      = lp->sbThickness;
            const float iy     = r.y + lp->padding.top;
            const float charH  = _TH(s);

            // Expose total content height so the shared drag infrastructure can
            // compute thumb ratios correctly (used by _UpdateContainerScrollDrags).
            lp->contentH = total;

            // Decide whether the vertical scrollbar is needed.
            const bool  showY  = total > innerH + 0.5f;
            const float viewH  = innerH;
            const float itemW  = r.w - s.borders.left - s.borders.right
                                      - (showY ? t : 0.f);
            const float px     = r.x + lp->padding.left;

            // Clamp scroll position.
            lp->scrollY = SDL::Clamp(lp->scrollY, 0.f, SDL::Max(0.f, total - viewH));

            // Content clip — exclude the scrollbar column.
            SDL::Rect clip = {
                (int)r.x + 1,
                (int)iy,
                (int)(r.w - 2.f - (showY ? t : 0.f)),
                (int)viewH
            };
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

            // ── Vertical scrollbar (ContainerScrollState-based) ───────────────────
            // thumbY is updated every frame so _TryBeginContainerScrollDrag can
            // hit-test it and the shared drag handler can move lp->scrollY.
            css->thumbY = {};
            if (showY) {
                FRect barY = {r.x + r.w - t, iy, t, viewH};

                SDL::Color trackCol = s.track;
                trackCol.a = SDL::Clamp8((int)(trackCol.a * s.opacity * 0.85f));
                _FillRR(barY, trackCol, SDL::FCorners(t * 0.5f), 1.f);

                float ratio  = SDL::Clamp(viewH / total, 0.05f, 1.f);
                float maxOff = total - viewH;
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

        // ── §8.12c  Graph draw ────────────────────────────────────────────────

        void _DrawGraph(ECS::EntityId e, const FRect &r, const Style &s,
                        const WidgetState &st) { 
            auto *gd = m_world.Get<GraphData>(e);
            if (!gd) return;

            _FillRR(r, s.bgColor, s.radius, s.opacity);
            _StrokeRR(r, st.hovered ? s.bdHovered : s.bdColor, s.borders, s.radius, s.opacity);

            const float op    = s.opacity;
            const float charH = _TH(s);

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
                float tw = _TW(gd->title, s);
                _Text(e, gd->title, plot.x + (plot.w - tw) * 0.5f, r.y + 2.f, tc, op, s);
            }
            // X label
            if (!gd->xLabel.empty()) {
                SDL::Color tc = s.textColor;
                float tw = _TW(gd->xLabel, s);
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
                float tw = _TW(lbl, s);
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
                float tw = _TW(lbl, s);
                _Text(e, lbl, gx - tw * 0.5f, plot.y + plot.h + 3.f, ac, op, s);
            }

            if (gd->data.empty()) return;

            // Clip to plot area for data rendering
            m_renderer.SetClipRect(FRectToRect(plot));

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
        }

        // ── §8.12d  ListBox keyboard handler ──────────────────────────────────

        void _HandleKeyDownListBox(SDL::Keycode k) {
            auto *lb  = m_world.Get<ListBoxData>(m_focused); 
            auto *cb  = m_world.Get<Callbacks>(m_focused);
            auto *cr  = m_world.Get<ComputedRect>(m_focused);
            auto *lp  = m_world.Get<LayoutProps>(m_focused);
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

        // ── §8.13  9-slice tileset draw ───────────────────────────────────────

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
    };

    // =============================================================================
    // §9  Builder
    // =============================================================================

    struct Builder {
        System &sys; 
        ECS::EntityId id;
        Builder(System &s, ECS::EntityId e) : sys(s), id(e) {}
        operator ECS::EntityId() const noexcept { return id; }
        [[nodiscard]] ECS::EntityId Id() const noexcept { return id; }

        // Style
        Builder &Style(const Style &s) {
            sys.GetStyle(id) = s;
            return *this;
        }
        Builder &WithStyle(std::function<void(UI::Style &)> fn) {
            fn(sys.GetStyle(id));
            return *this;
        }
        Builder &BgColor(SDL::Color c) {
            sys.GetStyle(id).bgColor = c;
            return *this;
        }
        Builder &BgHover(SDL::Color c) {
            sys.GetStyle(id).bgHovered = c;
            return *this;
        }
        Builder &BgPress(SDL::Color c) {
            sys.GetStyle(id).bgPressed = c;
            return *this;
        }
        Builder &BgCheck(SDL::Color c) {
            sys.GetStyle(id).bgChecked = c;
            return *this;
        }
        Builder &BorderColor(SDL::Color c) {
            sys.GetStyle(id).bdColor = c;
            return *this;
        }
        Builder &BorderLeft(float w) {
            sys.GetStyle(id).borders.left = w;
            return *this;
        }
        Builder &BorderRight(float w) {
            sys.GetStyle(id).borders.right = w;
            return *this;
        }
        Builder &BorderTop(float w) {
            sys.GetStyle(id).borders.top = w;
            return *this;
        }
        Builder &BorderBottom(float w) {
            sys.GetStyle(id).borders.bottom = w;
            return *this;
        }
        Builder &Borders(SDL::FBox bd) {
            sys.GetStyle(id).borders = bd;
            return *this;
        }

        Builder &RadiusTopLeft(float r) {
            sys.GetStyle(id).radius.tl = r;
            return *this;
        }
        Builder &RadiusTopRight(float r) {
            sys.GetStyle(id).radius.tr = r;
            return *this;
        }
        Builder &RadiusBottomLeft(float r) {
            sys.GetStyle(id).radius.bl = r;
            return *this;
        }
        Builder &RadiusBottomRight(float r) {
            sys.GetStyle(id).radius.br = r;
            return *this;
        }
        Builder &Radius(SDL::FCorners rad) {
            sys.GetStyle(id).radius = rad;
            return *this;
        }

        Builder &TextColor(SDL::Color c) {
            sys.GetStyle(id).textColor = c;
            return *this;
        }
        Builder &FillColor(SDL::Color c) {
            sys.GetStyle(id).fill = c;
            return *this;
        }
        Builder &TrackColor(SDL::Color c) {
            sys.GetStyle(id).track = c;
            return *this;
        }
        Builder &ThumbColor(SDL::Color c) {
            sys.GetStyle(id).thumb = c;
            return *this;
        }
        Builder &Opacity(float o) {
            sys.GetStyle(id).opacity = o;
            return *this;
        }

        Builder &ImageKey(const std::string &key, ImageFit f = ImageFit::Contain) {
            sys.SetImageKey(id, key, f);
            return *this;
        }

        // If `key` is empty, used sdl debug font
        Builder &FontKey(const std::string &key, float sz = 0.f) {
            auto &s = sys.GetStyle(id);
            s.usedDebugFont = key.empty();
            s.fontKey = key;
            s.fontSize = sz;
            return *this;
        }
        Builder &FontSize(float s) {
            sys.GetStyle(id).fontSize = s;
            return *this;
        }

        Builder &ClickSoundKey(const std::string &key) {
            sys.GetStyle(id).clickSound = key;
            return *this;
        }
        Builder &HoverSoundKey(const std::string &key) {
            sys.GetStyle(id).hoverSound = key;
            return *this;
        }
        Builder &ScrollSoundKey(const std::string &key) {
            sys.GetStyle(id).scrollSound = key;
            return *this;
        }
        Builder &ShowSoundKey(const std::string &key) {
            sys.GetStyle(id).showSound = key;
            return *this;
        }
        Builder &HideSoundKey(const std::string &key) {
            sys.GetStyle(id).hideSound = key;
            return *this;
        }

        // Layout
        Builder &WithLayout(std::function<void(LayoutProps &)> fn) {
            fn(sys.GetLayout(id));
            return *this;
        }
        Builder &X(float px) {
            sys.GetLayout(id).absX = Value::Px(px);
            return *this;
        }
        Builder &X(Value v) {
            sys.GetLayout(id).absX = v;
            return *this;
        }
        Builder &Y(float px) {
            sys.GetLayout(id).absY = Value::Px(px);
            return *this;
        }
        Builder &Y(Value v) {
            sys.GetLayout(id).absY = v;
            return *this;
        }
        Builder &W(float px) {
            sys.GetLayout(id).width = Value::Px(px);
            return *this;
        }
        Builder &W(Value v) {
            sys.GetLayout(id).width = v;
            return *this;
        }
        Builder &H(float px) {
            sys.GetLayout(id).height = Value::Px(px);
            return *this;
        }
        Builder &H(Value v) {
            sys.GetLayout(id).height = v;
            return *this;
        }
        Builder &Grow(float g) {
            sys.GetLayout(id).grow = g;
            return *this;
        }
        Builder &Gap(float g) {
            sys.GetLayout(id).gap = g;
            return *this;
        }
        Builder &Layout(Layout l) {
            sys.GetLayout(id).layout = l;
            return *this;
        }
        Builder &AlignChildrenH(UI::Align a) {
            sys.GetLayout(id).alignChildrenH = a;
            return *this;
        }
        Builder &AlignChildrenV(UI::Align a) {
            sys.GetLayout(id).alignChildrenV = a;
            return *this;
        }
        Builder &AlignChildren(UI::Align h, UI::Align v) {
            auto &lp = sys.GetLayout(id);
            lp.alignChildrenH = h;
            lp.alignChildrenV = v;
            return *this;
        }
        Builder &AlignH(UI::Align a) {
            sys.GetLayout(id).alignSelfH = a;
            return *this;
        }
        Builder &AlignV(UI::Align a) {
            sys.GetLayout(id).alignSelfV = a;
            return *this;
        }
        Builder &Align(UI::Align h, UI::Align v) {
            auto &lp = sys.GetLayout(id);
            lp.alignSelfH = h;
            lp.alignSelfV = v;
            return *this;
        }
        Builder &BoxSizingMode(BoxSizing b) {
            sys.GetLayout(id).boxSizing = b;
            return *this;
        }
        Builder &Attach(AttachLayout a) {
            sys.GetLayout(id).attach = a;
            return *this;
        }
        Builder &Absolute(Value x, Value y) {
            auto &l = sys.GetLayout(id);
            l.attach = AttachLayout::Absolute;
            l.absX = x;
            l.absY = y;
            return *this;
        }
        Builder &Absolute(float x, float y) { return Absolute(Value::Px(x), Value::Px(y)); }
        Builder &Fixed(Value x, Value y) {
            auto &l = sys.GetLayout(id);
            l.attach = AttachLayout::Fixed;
            l.absX = x;
            l.absY = y;
            return *this;
        }

        Builder &Padding(float a) {
            auto &l = sys.GetLayout(id);
            l.padding.left = l.padding.top = l.padding.right = l.padding.bottom = a;
            return *this;
        }
        Builder &Padding(float h, float v) {
            auto &l = sys.GetLayout(id);
            l.padding.left = l.padding.right = h;
            l.padding.top = l.padding.bottom = v;
            return *this;
        }
        Builder &Padding(SDL::FBox p) {
            auto &l = sys.GetLayout(id);
            l.padding = p;
            return *this;
        }
        Builder &PaddingH(float h) {
            auto &l = sys.GetLayout(id);
            l.padding.left = l.padding.right = h;
            return *this;
        }
        Builder &PaddingV(float v) {
            auto &l = sys.GetLayout(id);
            l.padding.top = l.padding.bottom = v;
            return *this;
        }
        Builder &PaddingBottom(float v) {
            sys.GetLayout(id).padding.bottom = v;
            return *this;
        }
        Builder &PaddingTop(float v) {
            sys.GetLayout(id).padding.top = v;
            return *this;
        }
        Builder &PaddingLeft(float v) {
            sys.GetLayout(id).padding.left = v;
            return *this;
        }
        Builder &PaddingRight(float v) {
            sys.GetLayout(id).padding.right = v;
            return *this;
        }

        Builder &Margin(float a) {
            auto &l = sys.GetLayout(id);
            l.margin.left = l.margin.top = l.margin.right = l.margin.bottom = a;
            return *this;
        }
        Builder &Margin(float h, float v) {
            auto &l = sys.GetLayout(id);
            l.margin.left = l.margin.right = h;
            l.margin.top = l.margin.bottom = v;
            return *this;
        }
        Builder &Margin(SDL::FBox m) {
            auto &l = sys.GetLayout(id);
            l.margin = m;
            return *this;
        }
        Builder &MarginH(float h) {
            auto &l = sys.GetLayout(id);
            l.margin.left = l.margin.right = h;
            return *this;
        }
        Builder &MarginV(float v) {
            auto &l = sys.GetLayout(id);
            l.margin.top = l.margin.bottom = v;
            return *this;
        }
        Builder &MarginBottom(float v) {
            sys.GetLayout(id).margin.bottom = v;
            return *this;
        }
        Builder &MarginTop(float v) {
            sys.GetLayout(id).margin.top = v;
            return *this;
        }
        Builder &MarginLeft(float v) {
            sys.GetLayout(id).margin.left = v;
            return *this;
        }
        Builder &MarginRight(float v) {
            sys.GetLayout(id).margin.right = v;
            return *this;
        }

        // BehaviorFlag
        Builder &Enable(bool e = true) {
            sys.SetEnabled(id, e);
            return *this;
        }
        Builder &Visible(bool v = true) {
            sys.SetVisible(id, v);
            return *this;
        }
        Builder &Hoverable(bool v = true) {
            sys.SetHoverable(id, v);
            return *this;
        }
        Builder &Selectable(bool v = true) {
            sys.SetSelectable(id, v);
            return *this;
        }
        Builder &Focusable(bool v = true) {
            sys.SetFocusable(id, v);
            return *this;
        }
        Builder &ScrollableX(bool b = true) {
            sys.SetScrollableX(id, b);
            return *this;
        }
        Builder &ScrollableY(bool b = true) {
            sys.SetScrollableY(id, b);
            return *this;
        }
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

        Builder &SetText(const std::string &t) {
            sys.SetText(id, t);
            return *this;
        }
        Builder &SetValue(float v) {
            sys.SetValue(id, v);
            return *this;
        }
        Builder &Check(bool c = true) {
            sys.SetChecked(id, c);
            return *this;
        }

        // Callbacks
        Builder &OnRenderCanvas(std::function<void(RendererRef, FRect)> cb) {
            sys.OnRenderCanvas(id, std::move(cb));
            return *this;
        }

        // TextArea
        Builder &TextAreaContent(const std::string &t) {
            sys.SetTextAreaContent(id, t);
            return *this;
        }
        Builder &TextAreaHighlightColor(SDL::Color c) {
            sys.SetTextAreaHighlightColor(id, c);
            return *this;
        }
        Builder &TextAreaTabSize(int n) {
            sys.SetTextAreaTabSize(id, n);
            return *this;
        }
        Builder &TextAreaSpan(int start, int end, TextSpanStyle style) {
            sys.AddTextAreaSpan(id, start, end, style);
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

        Builder &OnClick(std::function<void()> cb) {
            sys.OnClick(id, std::move(cb));
            return *this;
        }
        Builder &OnChange(std::function<void(float)> cb) {
            sys.OnChange(id, std::move(cb));
            return *this;
        }
        Builder &OnTextChange(std::function<void(const std::string &)> cb) {
            sys.OnTextChange(id, std::move(cb));
            return *this;
        }
        Builder &OnToggle(std::function<void(bool)> cb) {
            sys.OnToggle(id, std::move(cb));
            return *this;
        }
        Builder &OnScroll(std::function<void(float)> cb) {
            sys.OnScroll(id, std::move(cb));
            return *this;
        }
        Builder &OnHoverEnter(std::function<void()> cb) {
            sys.OnHoverEnter(id, std::move(cb));
            return *this;
        }
        Builder &OnHoverLeave(std::function<void()> cb) {
            sys.OnHoverLeave(id, std::move(cb));
            return *this;
        }
        Builder &OnFocusGain(std::function<void()> cb) {
            sys.OnFocusGain(id, std::move(cb));
            return *this;
        }
        Builder &OnFocusLose(std::function<void()> cb) {
            sys.OnFocusLose(id, std::move(cb));
            return *this;
        }

        // Children
        Builder &Child(ECS::EntityId c) { 
            sys.AppendChild(id, c);
            return *this;
        }
        template <typename... Cs>
            requires(std::convertible_to<Cs, ECS::EntityId> && ...)
        Builder &Children(Cs &&...cs) {
            (sys.AppendChild(id, static_cast<ECS::EntityId>(std::forward<Cs>(cs))), ...);
            return *this;
        }

        Builder &AsRoot() {
            sys.SetRoot(id);
            return *this;
        }
    };

    // =============================================================================
    // §10  System builder factory implementations
    // =============================================================================

    inline Builder System::Container(const std::string &n) { return {*this, MakeContainer(n)}; }
    inline Builder System::Label(const std::string &n, const std::string &t) { return {*this, MakeLabel(n, t)}; } 
    inline Builder System::Button(const std::string &n, const std::string &t) { return {*this, MakeButton(n, t)}; }
    inline Builder System::Toggle(const std::string &n, const std::string &t) { return {*this, MakeToggle(n, t)}; }
    inline Builder System::Radio(const std::string &n, const std::string &g, const std::string &t) { return {*this, MakeRadioButton(n, g, t)}; }
    inline Builder System::Slider(const std::string &n, float mn, float mx, float v, Orientation o) { return {*this, MakeSlider(n, mn, mx, v, o)}; }
    inline Builder System::ScrollBar(const std::string &n, float cs, float vs, Orientation o) { return {*this, MakeScrollBar(n, cs, vs, o)}; }
    inline Builder System::Progress(const std::string &n, float v, float mx) { return {*this, MakeProgress(n, v, mx)}; }
    inline Builder System::Sep(const std::string &n) { return {*this, MakeSeparator(n)}; }
    inline Builder System::Input(const std::string &n, const std::string &ph) { return {*this, MakeInput(n, ph)}; }
    inline Builder System::Knob(const std::string &n, float mn, float mx, float v) { return {*this, MakeKnob(n, mn, mx, v)}; }
    inline Builder System::ImageWidget(const std::string &n, const std::string &p, ImageFit f) { return {*this, MakeImage(n, p, f)}; }
    inline Builder System::CanvasWidget(const std::string &n,
        std::function<void(SDL::Event&)> cb_event, 
        std::function<void(float)> cb_update,
        std::function<void(RendererRef, FRect)> cb_render) { return {*this, MakeCanvas(n, std::move(cb_event), std::move(cb_update), std::move(cb_render))}; }
    inline Builder System::TextArea(const std::string &n, const std::string &text, const std::string &ph) { return {*this, MakeTextArea(n, text, ph)}; }

    inline Builder System::Column(const std::string &n, float gap, float pad) {
        auto b = Container(n);
        b.Layout(Layout::InColumn).Gap(gap).Padding(pad);
        return b;
    }

    inline Builder System::Row(const std::string &n, float gap, float pad) {
        auto b = Container(n);
        b.Layout(Layout::InLine).Gap(gap).Padding(pad)
            .AlignH(Align::Center);
        return b;
    }

    inline Builder System::Card(const std::string &n, float gap) {
        auto b = Column(n, gap, 0.f);
        b.Style(Theme::Card()).PaddingH(14.f).PaddingV(12.f);
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

    // =============================================================================
    // §11  Theme implementations
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