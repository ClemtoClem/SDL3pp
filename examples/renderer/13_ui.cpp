/**
 * @file 13_ui.cpp
 * @brief SDL3pp UI system — complete widget showcase (all widget types).
 *
 * Tabs:
 *   1. Basics       — Label, Button, Toggle, RadioButton, Separator
 *   2. Controls     — Slider (H/V), Knob, Progress
 *   3. Input&Scroll — Input, ScrollBar, ScrollView (container scroll)
 *   4. Image&Canvas — Image (all fit modes), Canvas, debug info
 *   5. Text&Lists   — TextArea, ListBox (H+V scroll), Graph (line + bar)
 *
 * Tooltips are shown on every interactive widget (hover 1 s).
 */

#include <SDL3pp/SDL3pp.h>
#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_resources.h>
#include <SDL3pp/SDL3pp_image.h>
#include <SDL3pp/SDL3pp_mixer.h>
#include <SDL3pp/SDL3pp_ttf.h>
#include <SDL3pp/SDL3pp_ui.h>

#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <string>
#include <vector>

namespace pal {
    constexpr SDL::Color BG      = { 14, 14,  20, 255};
    constexpr SDL::Color HEADER  = { 20, 20,  30, 255};
    constexpr SDL::Color ACCENT  = { 70,130, 210, 255};
    constexpr SDL::Color WHITE   = {220,220, 225, 255};
    constexpr SDL::Color GREY    = {130,132, 145, 255};
    constexpr SDL::Color GREEN   = { 50,195, 100, 255};
    constexpr SDL::Color ORANGE  = {230,145,  30, 255};
    constexpr SDL::Color RED     = {200, 60,  50, 255};
    constexpr SDL::Color PURPLE  = {155, 75, 220, 255};
    constexpr SDL::Color BORDER  = { 50, 52,  72, 255};
    constexpr SDL::Color TAB_ON  = { 55,115, 210, 255};
    constexpr SDL::Color TAB_OFF = { 30, 32,  46, 255};
    constexpr SDL::Color TEAL    = { 30,180, 170, 255};
}

namespace key {
    constexpr const char* FONT     = "deja-vu-sans";
    constexpr const char* CRATE    = "crate";
    constexpr const char* CLICK    = "click";
    constexpr const char* HOVER    = "hover";
    constexpr const char* MENU_OPEN= "menu-open";
}

struct Main {
    static constexpr SDL::Point kWinSz     = {1280, 720};
    static constexpr int        kPageCount = 5;
    static constexpr const char* kPageNames[kPageCount] = {
        "Basics", "Controls", "Input & Scroll", "Image & Canvas", "Text & Lists"
    };

    // ── SDL::AppResult callbacks ──────────────────────────────────────────────────

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
        for (auto arg : args) {
            if      (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
            else if (arg == "--debug")   priority = SDL::LOG_PRIORITY_DEBUG;
            else if (arg == "--info")    priority = SDL::LOG_PRIORITY_INFO;
        }
        SDL::SetLogPriorities(priority);
        SDL::SetAppMetadata("SDL3pp UI Showcase", "2.0", "com.example.ui");
        SDL::Init(SDL::INIT_VIDEO);
        SDL::TTF::Init();
        SDL::MIX::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }
    static void Quit(Main* m, SDL::AppResult) {
        delete m;
        SDL::MIX::Quit();
        SDL::TTF::Quit();
        SDL::Quit();
    }
    static SDL::Window InitWindow() {
        return SDL::CreateWindowAndRenderer(
            "SDL3pp - UI Showcase (all widgets)", kWinSz,
            SDL_WINDOW_RESIZABLE, nullptr);
    }

    // ── Resources & core objects ──────────────────────────────────────────────────

    SDL::MixerRef mixer{ SDL::CreateMixerDevice(
        SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK,
        SDL::AudioSpec{.format=SDL::AUDIO_F32, .channels=2, .freq=48000}) };

    SDL::Window      window  { InitWindow()         };
    SDL::RendererRef renderer{ window.GetRenderer() };

    SDL::ResourceManager rm;
    SDL::ResourcePool&   uiPool{ *rm.CreatePool("ui") };

    SDL::ECS::World  world;
    SDL::UI::System  ui{ world, renderer, mixer, uiPool };

    // ── Tabs ──────────────────────────────────────────────────────────────────────

    std::array<SDL::ECS::EntityId, kPageCount> pages   {};
    std::array<SDL::ECS::EntityId, kPageCount> tabBtns {};
    int currentPage = 0;

    // ── Live-updated labels ───────────────────────────────────────────────────────

    SDL::ECS::EntityId progAnimated = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblProgress  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblEcho      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblEcsCount  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblHovered   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblFocused   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblPoolInfo  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId tog1         = SDL::ECS::NullEntity;
    SDL::ECS::EntityId tog2         = SDL::ECS::NullEntity;
    SDL::ECS::EntityId tog3         = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblTogStatus = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblKnob1     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblKnob2     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId sbV          = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblSB        = SDL::ECS::NullEntity;
    SDL::ECS::EntityId scrollContent= SDL::ECS::NullEntity;

    // ── Text & Lists page ─────────────────────────────────────────────────────────

    SDL::ECS::EntityId lblTaLen     = SDL::ECS::NullEntity; // TextArea char count
    SDL::ECS::EntityId lblListSel   = SDL::ECS::NullEntity; // ListBox selection echo
    SDL::ECS::EntityId graphLine    = SDL::ECS::NullEntity; // animated line graph
    SDL::ECS::EntityId graphBar     = SDL::ECS::NullEntity; // bar/spectrum graph

    // ── Timers & animation state ──────────────────────────────────────────────────

    SDL::FrameTimer m_frameTimer{ 60.f };
    float  m_animProgress = 0.f;
    bool   m_animRunning  = true;
    float  m_animSpeed    = 0.25f;
    float  m_canvasAngle  = 0.f;
    float  m_graphTimer   = 0.f;     // drives the animated graphs

    // ── Constructor ───────────────────────────────────────────────────────────────

    Main() {
        window.StartTextInput();
        _LoadResources();
        m_frameTimer.Begin();
        _BuildUI();
    }
    ~Main() { uiPool.Release(); }

    // ── Resource loading ──────────────────────────────────────────────────────────

    void _LoadResources() {
        const std::string base = std::string(SDL::GetBasePath()) + "../../../assets/";
        ui.LoadFont   (key::FONT,     base + "fonts/DejaVuSans.ttf");
        ui.LoadTexture(key::CRATE,    base + "textures/crate.jpg");
        ui.LoadAudio  (key::CLICK,    base + "sounds/effect-click.mp3");
        ui.LoadAudio  (key::HOVER,    base + "sounds/effect-hover.mp3");
        ui.LoadAudio  (key::MENU_OPEN,base + "sounds/effect-menu-open.mp3");
        ui.SetDefaultFont(key::FONT, 15.f);
    }

    // ── Tab switching ─────────────────────────────────────────────────────────────

    void SwitchPage(int idx) {
        if (idx == currentPage) return;
        currentPage = idx;
        for (int i = 0; i < kPageCount; ++i) {
            ui.SetVisible(pages[i], i == currentPage);
            SDL::UI::Style& ts = ui.GetStyle(tabBtns[i]);
            ts.bgColor   = (i == currentPage) ? pal::TAB_ON : pal::TAB_OFF;
            ts.bgHovered = (i == currentPage)
                ? SDL::Color{75,135,220,255} : SDL::Color{42,45,62,255};
        }
    }

    // ── Frame loop ────────────────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        m_frameTimer.Begin();
        const float dt = m_frameTimer.GetDelta();
        m_canvasAngle += dt * 90.f;
        m_graphTimer  += dt;

        // Progress bar animation
        if (m_animRunning) {
            m_animProgress += dt * m_animSpeed;
            if (m_animProgress > 1.f) m_animProgress -= 1.f;
        }
        ui.SetValue(progAnimated, m_animProgress);
        ui.SetText(lblProgress, std::format("{:.0f}%", m_animProgress * 100.f));

        // Animated sine graph (line)
        if (world.IsAlive(graphLine)) {
            constexpr int N = 128;
            std::vector<float> data(N);
            for (int i = 0; i < N; ++i) {
                float x = (float)i / (float)(N - 1) * 4.f * std::numbers::pi_v<float>;
                data[i] = std::sin(x + m_graphTimer * 2.f)
                        * 0.5f + std::sin(x * 2.f + m_graphTimer) * 0.3f;
            }
            ui.SetGraphData(graphLine, std::move(data));
        }

        // Animated bar graph (spectrum)
        if (world.IsAlive(graphBar)) {
            constexpr int N = 32;
            std::vector<float> bars(N);
            for (int i = 0; i < N; ++i) {
                float f = (float)(i + 1) / (float)N;
                bars[i] = std::abs(std::sin(f * 6.f + m_graphTimer * 1.5f))
                        * (1.f - f * 0.4f);
            }
            ui.SetGraphData(graphBar, std::move(bars));
        }

        uiPool.Update();
        ui.SetText(lblPoolInfo,
            std::format("Pool \"{}\"  {} entries  {:.0f}% loaded",
                uiPool.GetName(), uiPool.Size(), uiPool.LoadingProgress() * 100.f));
        ui.SetText(lblEcsCount, std::format("ECS entities: {}", world.EntityCount()));

        {
            std::string hov = "(none)", foc = "(none)";
            world.Each<SDL::UI::Widget, SDL::UI::WidgetState>(
                [&](SDL::ECS::EntityId, SDL::UI::Widget& w, SDL::UI::WidgetState& s) {
                    if (s.hovered) hov = w.name;
                    if (s.focused) foc = w.name;
                });
            ui.SetText(lblHovered, "Hovered: " + hov);
            ui.SetText(lblFocused, "Focused: " + foc);
        }

        {
            std::string s = "Toggles: ";
            s += (ui.IsChecked(tog1) ? "A=ON " : "A=off ");
            s += (ui.IsChecked(tog2) ? "B=ON " : "B=off ");
            s += (ui.IsChecked(tog3) ? "C=ON"  : "C=off");
            ui.SetText(lblTogStatus, s);
        }

        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        m_frameTimer.End();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN)
            if ((ev.key.mod & SDL::KMOD_CTRL) && ev.key.key == SDL::KEYCODE_Q)
                return SDL::APP_SUCCESS;
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // UI construction helpers
    // ═══════════════════════════════════════════════════════════════════════════════

    void _BuildUI() {
        ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG).Borders(SDL::FBox(0.f)).Radius(SDL::FCorners(0.f))
            .Children(_BuildHeader(), _BuildContent())
            .AsRoot();
    }

    SDL::ECS::EntityId _BuildHeader() {
        auto header = ui.Row("header", 8.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(52.f)
            .PaddingH(12.f).PaddingV(0.f)
            .BgColor(pal::HEADER).BorderColor(pal::BORDER)
            .WithStyle([](auto& s){ s.borders = SDL::FBox(1.f); s.radius = SDL::FCorners(0.f); });

        header.Child(ui.Label("title", "SDL3pp UI — Widget Showcase")
            .TextColor(pal::ACCENT).Grow(1).PaddingV(0));

        for (int i = 0; i < kPageCount; ++i) {
            tabBtns[i] = ui.Button(std::string("tab_") + kPageNames[i], kPageNames[i])
                .W(i == 4 ? 120 : 110).H(36)
                .AlignH(SDL::UI::Align::Center)
                .Style(SDL::UI::Theme::PrimaryButton(i == 0 ? pal::TAB_ON : pal::TAB_OFF))
                .WithStyle([](auto& s){ s.radius = SDL::FCorners(5.f); })
                .ClickSoundKey(key::CLICK)
                .Tooltip(std::string("Switch to ") + kPageNames[i])
                .OnClick([this, i]{ SwitchPage(i); });
            header.Child(tabBtns[i]);
        }
        return header;
    }

    SDL::ECS::EntityId _BuildContent() {
        auto content = ui.Column("content", 0.f, 0.f)
            .Grow(1).PaddingH(16.f).PaddingV(12.f)
            .BgColor(pal::BG)
            .WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); });

        pages[0] = _BuildBasicsPage();
        pages[1] = _BuildControlsPage();
        pages[2] = _BuildInputScrollPage();
        pages[3] = _BuildImageCanvasPage();
        pages[4] = _BuildTextListsPage();

        for (int i = 0; i < kPageCount; ++i) {
            content.Child(pages[i]);
            ui.GetStyle(pages[i]).showSound = key::MENU_OPEN;
            ui.SetVisible(pages[i], i == 0);
        }
        return content;
    }

    SDL::UI::Builder _Page(const std::string& n) {
        return ui.Column(n, 12.f, 0.f).Grow(1).BgColor(pal::BG)
                 .WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); });
    }

    // ── 2-column row helper ───────────────────────────────────────────────────────

    SDL::UI::Builder _TwoColRow(const std::string& n) {
        return ui.Row(n, 12.f, 0.f)
            .Grow(1).Style(SDL::UI::Theme::Transparent())
            .AlignH(SDL::UI::Align::Left);
    }
    SDL::UI::Builder _LeftCol(const std::string& n) {
        return ui.Column(n, 12.f, 0.f).W(SDL::UI::Value::Pcw(50.f) - 6.f);
    }
    SDL::UI::Builder _RightCol(const std::string& n) {
        return ui.Column(n, 12.f, 0.f).W(SDL::UI::Value::Pcw(50.f) - 6.f);
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Page 1 — Basics: Label, Button, Toggle, RadioButton
    // ═══════════════════════════════════════════════════════════════════════════════

    SDL::ECS::EntityId _BuildBasicsPage() {
        auto page = _Page("page_basics");

        // ── Labels ───────────────────────────────────────────────────────────────
        auto cardLbl = ui.Card("card_lbl");
        cardLbl.Children(
            ui.SectionTitle("Labels"),
            ui.Label("l_white",  "Normal (white)"   ).TextColor(pal::WHITE),
            ui.Label("l_accent", "Accent (blue)"    ).TextColor(pal::ACCENT),
            ui.Label("l_green",  "Success (green)"  ).TextColor(pal::GREEN),
            ui.Label("l_orange", "Warning (orange)" ).TextColor(pal::ORANGE),
            ui.Label("l_red",    "Error (red)"      ).TextColor(pal::RED),
            ui.Label("l_teal",   "Info (teal)"      ).TextColor(pal::TEAL),
            ui.Label("l_dis",    "Disabled label"   ).Enable(false)
        );

        // ── Buttons ───────────────────────────────────────────────────────────────
        auto cardBtn = ui.Card("card_btn");
        auto lblClick = ui.Label("lbl_click", "Click a button…").TextColor(pal::GREY);
        auto btnRow = ui.Row("btn_row", 8.f, 0.f)
            .Style(SDL::UI::Theme::Transparent()).H(50);

        struct BSpec { const char* n; const char* l; SDL::Color c; };
        static constexpr BSpec kBtns[] = {
            {"b_prim","Primary",{70,130,210,255}},
            {"b_succ","Success",{50,195,100,255}},
            {"b_warn","Warning",{230,145, 30,255}},
            {"b_dang","Danger", {200, 60, 50,255}},
            {"b_purp","Purple", {155, 75,220,255}}
        };
        for (auto& sp : kBtns) {
            std::string txt = std::string(sp.l) + " clicked!";
            std::string tip = std::string("Click to show \"") + sp.l + " clicked!\"";
            btnRow.Child(
                ui.Button(sp.n, sp.l).W(100).H(34)
                    .AlignH(SDL::UI::Align::Center)
                    .Style(SDL::UI::Theme::PrimaryButton(sp.c))
                    .ClickSoundKey(key::CLICK).HoverSoundKey(key::HOVER)
                    .Tooltip(tip)
                    .OnClick([this, txt, lblClick]{ ui.SetText(lblClick, txt); })
            );
        }
        auto btnRow2 = ui.Row("btn_row2", 8.f, 0.f)
            .Style(SDL::UI::Theme::Transparent());
        btnRow2.Children(
            ui.Button("b_ghost", "Ghost").W(100).H(34)
                .AlignH(SDL::UI::Align::Center)
                .Style(SDL::UI::Theme::GhostButton())
                .Tooltip("Ghost button — transparent until hover")
                .ClickSoundKey(key::CLICK),
            ui.Button("b_dis", "Disabled").W(100).H(34)
                .Style(SDL::UI::Theme::PrimaryButton({80,80,90,255}))
                .Enable(false).Tooltip("Disabled — cannot click")
        );
        cardBtn.Children(ui.SectionTitle("Buttons"), btnRow, btnRow2, lblClick);

        // ── Toggles ───────────────────────────────────────────────────────────────
        auto cardTog = ui.Card("card_tog");
        tog1 = ui.Toggle("tog_a", "Enable feature A")
            .Tooltip("Toggle feature A on/off");
        tog2 = ui.Toggle("tog_b", "Enable feature B")
            .Tooltip("Toggle feature B on/off");
        tog3 = ui.Toggle("tog_c", "Feature C (starts ON)").Check(true)
            .Tooltip("Toggle feature C — starts enabled");
        lblTogStatus = ui.Label("lbl_tog", "Toggles: A=off B=off C=ON")
            .TextColor(pal::GREY);
        cardTog.Children(
            ui.SectionTitle("Toggle switches"),
            tog1, tog2, tog3,
            ui.Toggle("tog_dis", "Disabled toggle").Enable(false),
            lblTogStatus
        );

        // ── Radio buttons ─────────────────────────────────────────────────────────
        auto cardRad = ui.Card("card_rad");
        auto lblRadSel = ui.Label("lbl_rad_sel", "Selected: (none)").TextColor(pal::ACCENT);

        auto mkRadio = [&](const char* id, const char* lbl, const char* grp) {
            return ui.Radio(id, grp, lbl)
                .Tooltip(std::string("Select \"") + lbl + "\" in group " + grp)
                .OnToggle([this, lblRadSel, lbl](bool checked){
                    if (checked) ui.SetText(lblRadSel, std::string("Selected: ") + lbl);
                });
        };

        cardRad.Children(
            ui.SectionTitle("Radio buttons (groups A & B)"),
            mkRadio("r1","Option A-1","grpA"),
            mkRadio("r2","Option A-2","grpA"),
            mkRadio("r3","Option A-3","grpA"),
            ui.Sep("sep_rad"),
            mkRadio("r4","Choice B-1","grpB"),
            mkRadio("r5","Choice B-2","grpB"),
            lblRadSel
        );

        auto cols = _TwoColRow("basics_cols");
        cols.Children(
            _LeftCol("bc_l").Children(cardLbl, cardBtn),
            _RightCol("bc_r").Children(cardTog, cardRad)
        );
        page.Child(cols);
        return page;
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Page 2 — Controls: Slider, Knob, Progress
    // ═══════════════════════════════════════════════════════════════════════════════

    SDL::ECS::EntityId _BuildControlsPage() {
        auto page = _Page("page_controls");

        // ── Sliders (horizontal) ──────────────────────────────────────────────────
        auto cardSld = ui.Card("card_sld");
        cardSld.Child(ui.SectionTitle("Sliders (horizontal)"));

        auto mkSliderRow = [&](const char* id, const char* lbl,
                               float mn, float mx, float v,
                               SDL::Color fill, const char* tip)
        {
            auto vLbl = ui.Label(std::string(id) + "_v", std::format("{:.2f}", v))
                .W(50).TextColor(pal::GREY);
            auto sld = ui.Slider(id, mn, mx, v).Grow(1).FillColor(fill)
                .Tooltip(tip)
                .OnChange([this, vLbl](float val){
                    ui.SetText(vLbl, std::format("{:.2f}", val)); });
            cardSld.Child(
                ui.Row(std::string(id) + "_row", 10.f, 0.f)
                    .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
                    .Children(
                        ui.Label(std::string(id) + "_lbl", lbl).W(110).TextColor(pal::WHITE),
                        sld, vLbl)
            );
        };
        mkSliderRow("sld_vol",  "Volume",     0,   1, .7f, pal::ACCENT, "Master volume [0–1]");
        mkSliderRow("sld_spd",  "Speed",      0,   5, 1.f, pal::GREEN,  "Playback speed [0–5]");
        mkSliderRow("sld_opc",  "Opacity",    0,   1, 1.f, pal::PURPLE, "Opacity [0–1]");
        mkSliderRow("sld_temp", "Temperature",0, 100,22.f, pal::ORANGE, "Temperature [0–100]");
        cardSld.Child(
            ui.Row("sld_dis_row", 10.f, 0.f)
                .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
                .Children(
                    ui.Label("sld_dis_lbl","Disabled").W(110),
                    ui.Slider("sld_dis", 0, 1, .3f).Grow(1).Enable(false)
                        .Tooltip("Disabled slider — read-only"))
        );

        // ── Vertical slider + Knobs ───────────────────────────────────────────────
        auto cardKnob = ui.Card("card_knob");
        cardKnob.Child(ui.SectionTitle("Vertical Slider + Knobs"));
        lblKnob1 = ui.Label("lbl_k1", "Knob 1: 0.50").TextColor(pal::GREY);
        lblKnob2 = ui.Label("lbl_k2", "Knob 2: 50.0").TextColor(pal::GREY);

        auto sldV = ui.Slider("sld_v", 0, 1, .5f, SDL::UI::Orientation::Vertical)
            .H(120).W(24).AlignH(SDL::UI::Align::Center).FillColor(pal::ACCENT)
            .Tooltip("Vertical slider [0–1]");

        auto knobRow = ui.Row("knob_row", 16.f, 0.f)
            .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
            .Children(
                sldV,
                ui.Column("k1_col", 4.f, 0.f).AlignH(SDL::UI::Align::Center).Children(
                    ui.Knob("knob1", 0, 1, .5f).W(64).H(64)
                        .FillColor(pal::ACCENT).ThumbColor(pal::ACCENT)
                        .Tooltip("Knob 1 — drag or scroll [0–1]")
                        .OnChange([this](float v){ ui.SetText(lblKnob1, std::format("Knob 1: {:.2f}", v)); }),
                    lblKnob1),
                ui.Column("k2_col", 4.f, 0.f).AlignH(SDL::UI::Align::Center).Children(
                    ui.Knob("knob2", 0, 100, 50.f).W(64).H(64)
                        .FillColor(pal::PURPLE).ThumbColor(pal::PURPLE)
                        .Tooltip("Knob 2 — drag or scroll [0–100]")
                        .OnChange([this](float v){ ui.SetText(lblKnob2, std::format("Knob 2: {:.1f}", v)); }),
                    lblKnob2),
                ui.Column("k_dis_col", 4.f, 0.f).AlignH(SDL::UI::Align::Center).Children(
                    ui.Knob("knob_dis", 0, 1, .3f).W(64).H(64).Enable(false)
                        .Tooltip("Disabled knob"),
                    ui.Label("lbl_kdis","Disabled").TextColor(pal::GREY))
            );
        cardKnob.Child(knobRow);

        // ── Progress bars ─────────────────────────────────────────────────────────
        auto cardProg = ui.Card("card_prog");
        cardProg.Children(
            ui.SectionTitle("Progress bars"),
            ui.Progress("prog_25", .25f).FillColor(pal::RED)   .Tooltip("25% — danger zone"),
            ui.Progress("prog_50", .50f).FillColor(pal::ORANGE).Tooltip("50% — half way"),
            ui.Progress("prog_75", .75f).FillColor(pal::GREEN) .Tooltip("75% — almost done"),
            ui.Sep("sep_prog")
        );

        progAnimated = ui.Progress("prog_anim", 0.f).Grow(1).FillColor(pal::ACCENT)
            .Tooltip("Animated progress");
        lblProgress  = ui.Label("lbl_prog", "0%").W(45).TextColor(pal::WHITE);
        auto btnPause = ui.Button("btn_pause", "Pause")
            .Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
            .W(80).H(28).AlignH(SDL::UI::Align::Center)
            .ClickSoundKey(key::CLICK)
            .Tooltip("Pause/resume the progress animation");
        btnPause.OnClick([this, btnPause](){
            m_animRunning = !m_animRunning;
            ui.SetText(btnPause, m_animRunning ? "Pause" : "Resume");
        });
        auto sldSpd = ui.Slider("sld_aspd", 0.05f, 2.f, m_animSpeed).Grow(1)
            .Tooltip("Animation speed")
            .OnChange([this](float v){ m_animSpeed = v; });

        cardProg.Children(
            ui.Row("anim_row", 10.f, 0.f)
                .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
                .Children(progAnimated, lblProgress, btnPause),
            ui.Row("spd_row", 8.f, 0.f)
                .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
                .Children(ui.Label("lbl_spd","Speed").W(50), sldSpd)
        );

        auto cols = _TwoColRow("ctrl_cols");
        cols.Children(
            _LeftCol("cc_l").Children(cardSld, cardKnob),
            _RightCol("cc_r").Child(cardProg)
        );
        page.Child(cols);
        return page;
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Page 3 — Input & Scroll: Input, ScrollBar, ScrollView (container)
    // ═══════════════════════════════════════════════════════════════════════════════

    SDL::ECS::EntityId _BuildInputScrollPage() {
        auto page = _Page("page_input_scroll");
        lblEcho = ui.Label("lbl_echo", "Echo: ").TextColor(pal::ACCENT);

        // ── Text inputs ───────────────────────────────────────────────────────────
        auto cardInp = ui.Card("card_inp");
        cardInp.Children(
            ui.SectionTitle("Text Input (single-line)"),
            ui.Input("inp_name", "Enter your name…")
                .Tooltip("Type your name here")
                .OnTextChange([this](const std::string& t){
                    ui.SetText(lblEcho, "Echo: " + t); }),
            lblEcho,
            ui.Sep("sep_i1"),
            ui.Input("inp_city",  "City name…")   .Tooltip("City input"),
            ui.Input("inp_email", "user@example.com").Tooltip("Email address"),
            ui.Input("inp_pre",   "").SetText("Pre-filled content").Tooltip("Pre-filled field"),
            ui.Input("inp_dis",   "Cannot edit…").Enable(false)
                .Tooltip("Disabled — read-only"),
            ui.Label("inp_hint",
                "← → move  Backspace/Del delete  Ctrl+A select all  Esc unfocus")
                .TextColor(pal::GREY)
        );

        // ── ScrollBar ─────────────────────────────────────────────────────────────
        auto cardSB = ui.Card("card_sb");
        lblSB = ui.Label("lbl_sb", "Offset: 0").TextColor(pal::GREY);
        sbV = ui.ScrollBar("sb_v", 200.f, 80.f, SDL::UI::Orientation::Vertical)
            .H(120).AlignH(SDL::UI::Align::Center)
            .Tooltip("Vertical scrollbar — drag thumb")
            .OnScroll([this](float off){
                ui.SetText(lblSB, std::format("Offset: {:.0f}", off)); });
        auto sbH = ui.ScrollBar("sb_h", 300.f, 100.f, SDL::UI::Orientation::Horizontal)
            .W(SDL::UI::Value::Pw(80.f)).AlignH(SDL::UI::Align::Center)
            .Tooltip("Horizontal scrollbar — drag thumb");

        cardSB.Children(
            ui.SectionTitle("ScrollBar (standalone, V + H)"),
            ui.Row("sb_row", 12.f, 0.f)
                .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
                .Children(sbV,
                    ui.Column("sb_info", 6.f, 0.f).Children(
                        ui.Label("lbl_sb_tip","Drag the thumb").TextColor(pal::GREY),
                        lblSB)),
            ui.Sep("sep_sb"),
            ui.Label("lbl_sbh","Horizontal:").TextColor(pal::WHITE),
            sbH
        );

        // ── ScrollView (auto-scroll container) ────────────────────────────────────
        auto cardSV = ui.Card("card_sv");
        cardSV.Child(ui.SectionTitle("Scrollable container (mouse wheel / drag)"));
        auto svBuilder = ui.ScrollView("sv", 6.f)
            .H(180).AutoScrollable(true, true).Padding(4);
        scrollContent = svBuilder.Id();
        static const char* kColors[] = {
            "Alice Blue","Aquamarine","Chartreuse","Coral","Crimson",
            "Dark Orchid","Deep Sky Blue","Forest Green","Gold","Hot Pink",
            "Indian Red","Khaki","Lavender","Lime Green","Magenta",
            "Navy Blue","Olive","Orchid","Pale Violet Red","Peru",
            "Royal Blue","Salmon","Sea Green","Sienna","Spring Green"
        };
        for (int i = 0; i < 25; ++i) {
            svBuilder.Child(
                ui.Label(std::format("sv_item{}", i),
                          std::format("{:02d}. {} — scroll with mouse wheel or drag the scrollbar", i+1, kColors[i]))
                    .TextColor(i%3==0 ? pal::ACCENT : i%3==1 ? pal::GREEN : pal::WHITE)
                    .Padding(2, 1)
            );
        }
        cardSV.Child(scrollContent);

        auto cols = _TwoColRow("is_cols");
        cols.Children(
            _LeftCol("is_l").Child(cardInp),
            _RightCol("is_r").Children(cardSB, cardSV)
        );
        page.Child(cols);
        return page;
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Page 4 — Image & Canvas + debug info
    // ═══════════════════════════════════════════════════════════════════════════════

    SDL::ECS::EntityId _BuildImageCanvasPage() {
        auto page = _Page("page_img_canvas");

        // ── Image widget ──────────────────────────────────────────────────────────
        auto cardImg = ui.Card("card_img");
        cardImg.Child(ui.SectionTitle(
            std::format("Image widget — pool key \"{}\"", key::CRATE)));

        auto imgRow = ui.Row("img_row", 8.f, 0.f)
            .Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Left);

        auto mkImg = [&](const char* id, const char* lbl, SDL::UI::ImageFit fit, const char* tip){
            auto col = ui.Column(std::string(id) + "_c", 4.f, 0.f);
            col.Children(
                ui.ImageWidget(id, key::CRATE, fit).W(90).H(90)
                    .WithStyle([](auto& s){ s.radius = SDL::FCorners(4.f); })
                    .Tooltip(tip),
                ui.Label(std::string(id) + "_l", lbl).TextColor(pal::GREY)
            );
            imgRow.Child(col);
        };
        mkImg("img_fill",  "Fill",    SDL::UI::ImageFit::Fill,    "Fill: stretch to cover entire area");
        mkImg("img_cont",  "Contain", SDL::UI::ImageFit::Contain, "Contain: fit inside preserving ratio");
        mkImg("img_cover", "Cover",   SDL::UI::ImageFit::Cover,   "Cover: fill and crop to ratio");
        mkImg("img_none",  "None",    SDL::UI::ImageFit::None,    "None: original pixel size, top-left");
        mkImg("img_tile",  "Tile",    SDL::UI::ImageFit::Tile,    "Tile: repeat texture");
        cardImg.Children(imgRow,
            ui.Label("img_hint",
                "Missing pool key → dark placeholder (no crash)").TextColor(pal::GREY));

        // ── Canvas ────────────────────────────────────────────────────────────────
        auto cardCvs = ui.Card("card_cvs");
        cardCvs.Child(ui.SectionTitle("Canvas widget (custom SDL rendering)"));
        auto canvas = ui.CanvasWidget("canvas", nullptr, nullptr,
            [this](SDL::RendererRef r, SDL::FRect rect) {
                // Background + checkerboard
                r.SetDrawColor({30,34,52,255});
                r.RenderFillRect(rect);
                const float cell = 20.f;
                r.SetDrawColor({50,56,80,50});
                for (float x = rect.x; x < rect.x + rect.w; x += cell)
                    for (float y = rect.y; y < rect.y + rect.h; y += cell) {
                        int ix = (int)((x - rect.x) / cell);
                        int iy = (int)((y - rect.y) / cell);
                        if ((ix + iy) % 2 == 0)
                            r.RenderFillRect(SDL::FRect{x, y, cell, cell});
                    }
                // Grid lines
                r.SetDrawColor({50,56,80,120});
                for (float x = rect.x; x <= rect.x+rect.w; x += cell)
                    r.RenderLine({x, rect.y}, {x, rect.y+rect.h});
                for (float y = rect.y; y <= rect.y+rect.h; y += cell)
                    r.RenderLine({rect.x, y}, {rect.x+rect.w, y});
                // Rotating square
                float cx = rect.x + rect.w * .5f, cy = rect.y + rect.h * .5f;
                float sz = std::min(rect.w, rect.h) * 0.35f;
                float a  = m_canvasAngle * (std::numbers::pi_v<float> / 180.f);
                std::array<SDL::FPoint, 5> pts;
                for (int i = 0; i < 4; ++i) {
                    float ang = a + i * std::numbers::pi_v<float> * 0.5f;
                    pts[i] = {cx + std::cos(ang)*sz, cy + std::sin(ang)*sz};
                }
                pts[4] = pts[0];
                r.SetDrawColor({70,130,210,255}); r.RenderLines(pts);
                // Pulsing center circle
                float pulse = std::abs(std::sin(a * 2.f)) * 8.f + 4.f;
                r.SetDrawColor({155,75,220,255}); r.RenderFillCircle({cx, cy}, pulse);
            }
        ).W(SDL::UI::Value::Pw(100)).H(150).Padding(0);
        cardCvs.Children(canvas,
            ui.Label("cvs_hint","Canvas: raw SDL draw calls, no widget overhead")
                .TextColor(pal::GREY));

        // ── Debug info ────────────────────────────────────────────────────────────
        lblPoolInfo = ui.Label("lbl_pool","Pool: …").TextColor(pal::ACCENT);
        lblEcsCount = ui.Label("lbl_ecs", "ECS entities: …").TextColor(pal::WHITE);
        lblHovered  = ui.Label("lbl_hov", "Hovered: (none)").TextColor(pal::WHITE);
        lblFocused  = ui.Label("lbl_foc", "Focused: (none)").TextColor(pal::WHITE);

        auto cardInfo = ui.Card("card_info");
        cardInfo.Children(
            ui.SectionTitle("Live debug — ECS / Resource pool"),
            ui.Sep("sep_sub"),
            lblPoolInfo, lblEcsCount, lblHovered, lblFocused,
            ui.Sep("sep_info"),
            ui.Label("n1","SetDefaultFont(path, ptsize) — stamps font on every new widget")
                .TextColor(pal::GREY),
            ui.Label("n2","ResourcePool stores Texture / Font / Audio by string key")
                .TextColor(pal::GREY),
            ui.Label("n3","Mixer::PlayAudio() — fire-and-forget, no handle needed")
                .TextColor(pal::GREY),
            ui.Sep("sep_info2"),
            ui.Button("btn_pool_log","Log pool to console")
                .Style(SDL::UI::Theme::GhostButton()).W(180).H(30)
                .Tooltip("Print pool stats to the SDL log")
                .ClickSoundKey(key::CLICK)
                .OnClick([this]{
                    SDL::LogInfo(SDL::LOG_CATEGORY_APPLICATION,
                        std::format("Pool '{}': {} entries, {:.1f}% loaded",
                            uiPool.GetName(), uiPool.Size(),
                            uiPool.LoadingProgress()*100.f).c_str());
                })
        );

        auto cols = _TwoColRow("ic_cols");
        cols.Children(
            _LeftCol("ic_l").Children(cardImg, cardCvs),
            _RightCol("ic_r").Child(cardInfo)
        );
        page.Child(cols);
        return page;
    }

    // ═══════════════════════════════════════════════════════════════════════════════
    // Page 5 — Text & Lists: TextArea, ListBox, Graph
    // ═══════════════════════════════════════════════════════════════════════════════

    SDL::ECS::EntityId _BuildTextListsPage() {
        auto page = _Page("page_text_lists");

        // ── TextArea ──────────────────────────────────────────────────────────────
        auto cardTa = ui.Card("card_ta");
        lblTaLen = ui.Label("lbl_ta_len","Characters: 0").TextColor(pal::GREY);
        const std::string initialText =
            "The TextArea widget supports:\n"
            "  • Multi-line editing (Enter for new line)\n"
            "  • Cursor movement with arrow keys\n"
            "  • Ctrl+A to select all, Ctrl+C/X/V copy/cut/paste\n"
            "  • Mouse click to place cursor, drag to select\n"
            "  • Horizontal scroll when lines exceed the widget width\n\n"
            "Edit this text freely!";

        auto ta = ui.TextArea("ta_main", initialText)
            .Grow(1).H(SDL::UI::Value::Px(160.f))
            .AutoScrollable(true, true)
            .Tooltip("Multi-line rich text editor — click, type, select, copy/paste")
            .OnTextChange([this](const std::string& t){
                ui.SetText(lblTaLen, std::format("Characters: {}", (int)t.size()));
            });

        // Pre-colour the first word of each bullet in a different colour
        if (auto* td = world.Get<SDL::UI::TextAreaData>(ta.Id())) {
            auto addSpan = [&](int start, int end, SDL::Color col){
                td->spans.push_back({start, end, {false, false, col}});
            };
            addSpan(4,  12, pal::ACCENT);   // "TextArea"
            addSpan(14, 21, pal::GREEN);    // "Multi-l"... let's colour "supports"
        }

        cardTa.Children(
            ui.SectionTitle("TextArea — rich multi-line editor"),
            ta,
            ui.Row("ta_btns", 8.f, 0.f)
                .Style(SDL::UI::Theme::Transparent())
                .Children(
                    lblTaLen,
                    ui.Label("lbl_ta_hint",
                        "Ctrl+A select all  Ctrl+C/X/V copy/cut/paste")
                        .TextColor(pal::GREY).Grow(1)
                )
        );

        // ── ListBox ───────────────────────────────────────────────────────────────
        auto cardLb = ui.Card("card_lb");
        lblListSel = ui.Label("lbl_lbsel","Selection: (none)").TextColor(pal::ACCENT);

        // Items: mix of short names and long entries to test H scroll
        std::vector<std::string> items = {
            "Apple",
            "Banana",
            "Cherry — a small, round stone fruit that is typically bright or dark red",
            "Date",
            "Elderberry",
            "Fig",
            "Grape — a berry of a woody vine, grown in clusters, used for wine and juice",
            "Honeydew",
            "Iced melon",
            "Jackfruit — the largest tree fruit in the world, native to South and Southeast Asia",
            "Kiwi",
            "Lemon — a yellow citrus fruit used for its juice, zest, and in cooking",
            "Mango",
            "Nectarine",
            "Orange — a citrus fruit with a tough bright reddish-yellow rind and sweet flesh",
            "Papaya",
            "Quince",
            "Raspberry",
            "Strawberry — a widely grown hybrid species whose fruit (also strawberry) is cherished worldwide",
            "Tangerine",
            "Ugli fruit",
            "Vanilla bean",
            "Watermelon",
            "Ximenia",
            "Yellow passion fruit",
        };

        auto lb = ui.ListBoxWidget("listbox", items)
            .H(180)
            .Tooltip("ListBox — click to select, scroll with mouse wheel\nLong items test horizontal scrollbar");
        const SDL::ECS::EntityId lbId = lb.Id();
        lb.OnClick([this, lbId](){
            int idx = ui.GetListBoxSelection(lbId);
            auto* lbd = ui.GetListBoxData(lbId);
            if (idx >= 0 && lbd && idx < (int)lbd->items.size())
                ui.SetText(lblListSel,
                    std::format("Selection [{}]: {}", idx, lbd->items[idx]));
            else
                ui.SetText(lblListSel, "Selection: (none)");
        });

        cardLb.Children(
            ui.SectionTitle("ListBox — V + H auto-scrollbars"),
            lb, lblListSel,
            ui.Label("lbl_lb_hint",
                "↑ ↓ keyboard navigation  •  Long items → horizontal scrollbar")
                .TextColor(pal::GREY)
        );

        // ── Graphs ────────────────────────────────────────────────────────────────
        auto cardGl = ui.Card("card_graph_line");
        graphLine = ui.GradedGraph("g_line")
            .H(160).Grow(1)
            .Tooltip("Animated sine-wave (line mode, updated every frame)");
        if (auto* gd = ui.GetGraphData(graphLine)) {
            gd->title    = "Sine Wave (animated)";
            gd->yLabel   = "amp";
            gd->minVal   = -1.f;
            gd->maxVal   =  1.f;
            gd->showFill = true;
            gd->barMode  = false;
            gd->lineColor = pal::ACCENT;
            gd->fillColor = {70,130,210, 45};
        }
        cardGl.Children(ui.SectionTitle("Graph — line mode (animated)"), graphLine);

        auto cardGb = ui.Card("card_graph_bar");
        graphBar = ui.GradedGraph("g_bar")
            .H(140).Grow(1)
            .Tooltip("Animated bar graph / spectrum (bar mode, updated every frame)");
        if (auto* gd = ui.GetGraphData(graphBar)) {
            gd->title    = "Spectrum (animated)";
            gd->xLabel   = "freq";
            gd->yLabel   = "mag";
            gd->minVal   = 0.f;
            gd->maxVal   = 1.f;
            gd->showFill = false;
            gd->barMode  = true;
            gd->lineColor = pal::GREEN;
            gd->fillColor = pal::GREEN;
            gd->xDivisions = 8;
            gd->yDivisions = 4;
        }
        cardGb.Children(ui.SectionTitle("Graph — bar mode (animated)"), graphBar);

        auto cols = _TwoColRow("tl_cols");
        cols.Children(
            _LeftCol("tl_l").Children(cardTa, cardLb),
            _RightCol("tl_r").Children(cardGl, cardGb)
        );
        page.Child(cols);
        return page;
    }

};

SDL3PP_DEFINE_CALLBACKS(Main)
