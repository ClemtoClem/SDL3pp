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
 * |------------------|------------------------------------------------------------|
 * | Component        | Purpose                                                    |
 * |------------------|------------------------------------------------------------|
 * | `Widget`         | WidgetType, name, enabled/visible, dirty flags             |
 * | `Style`          | Colors, bdColor, radius, font path, opacity, audio cues    |
 * | `LayoutProps`    | Value w/h/x/y, margins, padding, layout mode, grow, gap    |
 * | `LayoutGridProps`| columns, rows, GridSizing, GridLines, line color/thickness |
 * | `GridCell`       | col, row, colSpan, rowSpan (child position in InGrid)      |
 * | `EditableContent`        | Text, placeholder, cursor (Input)                          |
 * | `SliderData`     | min/max/value + drag (Slider, Progress, horizontal SB)     |
 * | `ScrollBarData`  | contentSize, viewSize, offset + drag                       |
 * | `ToggleData`     | checked + animT (Toggle)                                   |
 * | `RadioData`      | group name + checked (RadioButton)                         |
 * | `KnobData`       | normalised val [0,1] + drag                                |
 * | `ImageData`      | texture path + ImageFit                                    |
 * | `IconData`       | icon key, padding, per-state opacity + tint (Button)       |
 * | `CanvasData`     | custom render callback                                     |
 * | `WidgetState`    | hover/press/focus                                          |
 * | `Callbacks`      | onClick, onChange, onScroll, onToggle, onTextChange, …     |
 * | `ComputedRect`   | screen rect, clip rect, measured size                      |
 * | `Children`       | ordered child entity IDs                                   |
 * | `Parent`         | parent entity ID                                           |
 * |------------------|------------------------------------------------------------|
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
 * |----------------------------|--------------------------------------------------|
 * | Component / setter         | Effect                                           |
 * |----------------------------|--------------------------------------------------|
 * | `LayoutGridProps::columns` | Number of columns (default 2).                   |
 * | `LayoutGridProps::rows`    | Fixed row count; 0 = auto from children.         |
 * | `GridSizing::Fixed`        | All tracks equal: container ÷ count.             |
 * | `GridSizing::Content`      | Each track adapts to its widest/tallest child.   |
 * | `GridLines::None/Rows/     | Separator lines between cells.                   |
 * |  Columns/Both`             |                                                  |
 * |----------------------------|--------------------------------------------------|
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
 * |-----------------------|-----------------------------------------------------------|
 * | Flag                  | Comportement                                              |
 * |-----------------------|-----------------------------------------------------------|
 * | `ScrollableX`         | Barre horizontale **toujours** visible                    |
 * | `ScrollableY`         | Barre verticale **toujours** visible                      |
 * | `AutoScrollableX`     | Barre horizontale visible **seulement si débordement**    |
 * | `AutoScrollableY`     | Barre verticale visible **seulement si débordement**      |
 * |-----------------------|-----------------------------------------------------------|
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
 * |---------------------------|---------------------------------|------------------------|
 * | C++ type                  | Pool key                        | Builder setter         |
 * |---------------------------|---------------------------------|------------------------|
 * | `SDL::Texture`            | string in `ImageData::key`      | `.Image("key")`        |
 * | `SDL::RendererTextEngine` |                                 |                        |
 * | `SDL::Font`               | `"font:<key>|<ptsize>"`         | `.Font("key", ptsize)` |
 * | `SDL::Audio`              | string in `Style::clickSound` … | `.ClickSound("key")`   |
 * |---------------------------|---------------------------------|------------------------|
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

// ── Sub-modules ───────────────────────────────────────────────────────────────────────
#include "SDL3pp_ui/UIEnums.h"
#include "SDL3pp_ui/UIValue.h"
#include "SDL3pp_ui/UIComponents.h"
#include "SDL3pp_ui/UIFactory.h"
#include "SDL3pp_ui/UILayoutSystem.h"
#include "SDL3pp_ui/UIEventSystem.h"
#include "SDL3pp_ui/UIRenderSystem.h"
#include "SDL3pp_ui/UISystem.h"
#include "SDL3pp_ui/UIBuilder.h"

#endif /* SDL3PP_UI_H_ */
