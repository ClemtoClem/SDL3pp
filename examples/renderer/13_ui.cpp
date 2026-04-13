/**
 * @file 13_ui.cpp
 * @brief SDL3pp UI system - full widget showcase with ResourceManager.
 */

#include <SDL3pp/SDL3pp.h>
#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_resources.h>
#include <SDL3pp/SDL3pp_image.h>
#include <SDL3pp/SDL3pp_mixer.h>   // SDL3_mixer 3.0 - guards with SDL3PP_ENABLE_MIXER
#include <SDL3pp/SDL3pp_ttf.h>     // SDL3_ttf  3.0 - guards with SDL3PP_ENABLE_TTF
#include <SDL3pp/SDL3pp_ui.h>

#include <array>
#include <format>
#include <string>

namespace pal {
    constexpr SDL::Color BG      = { 14, 14,  20,255};
    constexpr SDL::Color HEADER  = { 20, 20,  30,255};
    constexpr SDL::Color ACCENT  = { 70,130, 210,255};
    constexpr SDL::Color WHITE   = {220,220, 225,255};
    constexpr SDL::Color GREY    = {130,132, 145,255};
    constexpr SDL::Color GREEN   = { 50,195, 100,255};
    constexpr SDL::Color ORANGE  = {230,145,  30,255};
    constexpr SDL::Color RED     = {200, 60,  50,255};
    constexpr SDL::Color PURPLE  = {155, 75, 220,255};
    constexpr SDL::Color BORDER  = { 50, 52,  72,255};
    constexpr SDL::Color TAB_ON  = { 55,115, 210,255};
    constexpr SDL::Color TAB_OFF = { 30, 32,  46,255};
}

namespace key {
    constexpr const char* FONT   = "deja-vu-sans";
    constexpr const char* CRATE  = "crate";
    constexpr const char* CLICK  = "click";
    constexpr const char* HOVER  = "hover";
    constexpr const char* MENU_OPEN = "menu-open";
}

struct Main {
    static constexpr SDL::Point kWinSz     = {1280, 720};
    static constexpr int        kPageCount = 4;
    static constexpr const char* kPageNames[kPageCount] = {
        "Basics", "Controls", "Input & Scroll", "Image & Canvas"
    };

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::SetAppMetadata("SDL3pp UI Showcase", "1.0", "com.example.ui");
        SDL::Init(SDL::INIT_VIDEO);
        SDL::TTF::Init();
        SDL::MIX::Init();
        *out = new Main(args);
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
            "SDL3pp - UI", kWinSz,
            SDL_WINDOW_RESIZABLE, nullptr);
    }

    SDL::MixerRef mixer{ SDL::CreateMixerDevice(
        SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK, SDL::AudioSpec{ 
        .format=SDL::AUDIO_F32, .channels=2, .freq=48000}) };

    SDL::Window      window  { InitWindow()          };
    SDL::RendererRef renderer{ window.GetRenderer()  };
    
    SDL::ResourceManager rm;
    SDL::ResourcePool&   uiPool{ *rm.CreatePool("ui") };

    SDL::ECS::World world;
    SDL::UI::System ui{ world, renderer, mixer, uiPool };

    std::array<SDL::ECS::EntityId, kPageCount> pages   {};
    std::array<SDL::ECS::EntityId, kPageCount> tabBtns {};
    int currentPage = 0;

    SDL::ECS::EntityId progAnimated  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblProgress   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblEcho       = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblEcsCount   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblHovered    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblFocused    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblPoolInfo   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId tog1          = SDL::ECS::NullEntity;
    SDL::ECS::EntityId tog2          = SDL::ECS::NullEntity;
    SDL::ECS::EntityId tog3          = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblTogStatus  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblKnob1      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblKnob2      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId sbV           = SDL::ECS::NullEntity;
    SDL::ECS::EntityId lblSB         = SDL::ECS::NullEntity;
    SDL::ECS::EntityId scrollContent = SDL::ECS::NullEntity;

    SDL::FrameTimer m_frameTimer{ 60.f };
    float  m_animProgress = 0.f;
    bool   m_animRunning  = true;
    float  m_animSpeed    = 0.25f;
    float  m_canvasAngle  = 0.f;

    Main(SDL::AppArgs args) {
        window.StartTextInput();
        _LoadResources();
        m_frameTimer.Begin();
        _BuildUI();
    }

    ~Main() {
        uiPool.Release();   
    }

    void _LoadResources() {
        const std::string basePath = std::string(SDL::GetBasePath()) + "../../../assets/";

        ui.LoadFont   (key::FONT,  basePath + "fonts/DejaVuSans.ttf");
        ui.LoadTexture(key::CRATE, basePath + "textures/crate.jpg");
        ui.LoadAudio  (key::CLICK, basePath + "sounds/effect-click.mp3");
        ui.LoadAudio  (key::HOVER, basePath + "sounds/effect-hover.mp3");
        ui.LoadAudio  (key::MENU_OPEN, basePath + "sounds/effect-menu-open.mp3");

        ui.SetDefaultFont(key::FONT, 15.f);

        SDL::LogInfo(SDL::LOG_CATEGORY_APPLICATION,
            std::format("UI resources loaded - pool '{}' has {} entries (progress {:.0f}%)",
            uiPool.GetName(), uiPool.Size(), uiPool.LoadingProgress() * 100.f).c_str());
    }

    void SwitchPage(int idx) {
        if (idx == currentPage) return;
        currentPage = idx;
        for (int i = 0; i < kPageCount; ++i) {
            ui.SetVisible(pages[i], i == currentPage);
            SDL::UI::Style& ts = ui.GetStyle(tabBtns[i]);
            ts.bgColor   = (i == currentPage) ? pal::TAB_ON : pal::TAB_OFF;
            ts.bgHovered = (i == currentPage)
                ? SDL::Color{75,135,220,255}
                : SDL::Color{42, 45, 62,255};
        }
    }

    SDL::AppResult Iterate() {
        m_frameTimer.Begin();
        const float dt = m_frameTimer.GetDelta();
        m_canvasAngle += dt * 90.f;

        if (m_animRunning) {
            m_animProgress += dt * m_animSpeed;
            if (m_animProgress > 1.f) m_animProgress -= 1.f;
        }
        ui.SetValue(progAnimated, m_animProgress);
        ui.SetText(lblProgress, std::format("{:.0f}%", m_animProgress * 100.f));

        uiPool.Update();

        ui.SetText(lblPoolInfo, std::format(
            "Pool \"{}\"  entries: {}  loading: {:.0f}%",
            uiPool.GetName(), uiPool.Size(), uiPool.LoadingProgress() * 100.f));

        ui.SetText(lblEcsCount, std::format("ECS entities: {}", world.EntityCount()));

        {
            std::string hov = "(none)";
            std::string foc = "(none)";
            world.Each<SDL::UI::Widget, SDL::UI::WidgetState>(
                [&](SDL::ECS::EntityId, SDL::UI::Widget& w, SDL::UI::WidgetState& s){
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
        ui.Frame(dt);
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

    void _BuildUI() {
        ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG)
            .Borders(SDL::FBox(0.f))
            .Radius(SDL::FCorners(0.f))
            .Children(_BuildHeader(), _BuildContent())
            .AsRoot();
    }

    SDL::ECS::EntityId _BuildHeader() {
        auto header = ui.Row("header", 8.f, 0.f)
            .H(52.f).PaddingH(16.f).PaddingV(0.f)
            .BgColor(pal::HEADER)
            .BorderColor(pal::BORDER)
            .WithStyle([](auto& s){ s.borders = SDL::FBox(1.f); s.radius = SDL::FCorners(0.f); });

        header.Child(
            ui.Label("title", "SDL3pp UI - Widget Showcase")
              .TextColor(pal::ACCENT).Grow(1).PaddingV(0)
        );

        for (int i = 0; i < kPageCount; ++i) {
            const bool first = (i == 0);
            tabBtns[i] = ui.Button(
                    std::string("tab_") + kPageNames[i], kPageNames[i])
                .W(130).H(36)
                .AlignSelf(SDL::UI::Align::Center)
                .Style(SDL::UI::Theme::PrimaryButton(
                    first ? pal::TAB_ON : pal::TAB_OFF))
                .WithStyle([](auto& s){ s.radius = SDL::FCorners(5.f); })
                .ClickSoundKey(key::CLICK)
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

    SDL::ECS::EntityId _BuildBasicsPage() {
        auto page = _Page("page_basics");

        auto cardLbl = ui.Card("card_lbl");
        cardLbl.Children(
            ui.SectionTitle("Labels"),
            ui.Label("l_white",  "Normal (white)"  ).TextColor(pal::WHITE),
            ui.Label("l_accent", "Accent"          ).TextColor(pal::ACCENT),
            ui.Label("l_green",  "Success (green)" ).TextColor(pal::GREEN),
            ui.Label("l_orange", "Warning (orange)").TextColor(pal::ORANGE),
            ui.Label("l_red",    "Error (red)"     ).TextColor(pal::RED),
            ui.Label("l_dis",    "Disabled"        ).Enable(false)
        );

        auto cardBtn = ui.Card("card_btn");
        auto lblClick = ui.Label("lbl_click", "Click a button...");
        auto btnRow = ui.Row("btn_row", 8.f, 0.f)
            .Style(SDL::UI::Theme::Transparent())
            .H(50);

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
            btnRow.Child(
                ui.Button(sp.n, sp.l).W(100).H(34)
                    .AlignSelf(SDL::UI::Align::Center)
                    .Style(SDL::UI::Theme::PrimaryButton(sp.c))
                    .ClickSoundKey(key::CLICK)
                    .HoverSoundKey(key::HOVER)
                    .OnClick([this, txt, lblClick]{ ui.SetText(lblClick, txt); })
            );
        }
        cardBtn.Children(ui.SectionTitle("Buttons"), btnRow, lblClick);

        auto cardTog = ui.Card("card_tog");
        tog1 = ui.Toggle("tog_a", "Enable feature A");
        tog2 = ui.Toggle("tog_b", "Enable feature B");
        tog3 = ui.Toggle("tog_c", "Feature C (starts ON)").Check(true);
        lblTogStatus = ui.Label("lbl_tog", "Toggles: A=off B=off C=ON")
            .TextColor(pal::GREY);
        cardTog.Children(
            ui.SectionTitle("Toggle switches"),
            tog1, tog2, tog3,
            ui.Toggle("tog_dis", "Disabled toggle").Enable(false),
            lblTogStatus
        );

        auto cardRad = ui.Card("card_rad");
        auto lblRadSel = ui.Label("lbl_rad_sel", "Selected: (none)")
            .TextColor(pal::ACCENT);

        auto mkRadio = [&](const char* id, const char* lbl, const char* grp) {
            return ui.Radio(id, grp, lbl)
                .OnToggle([this, lblRadSel, lbl](bool checked){
                    if (checked)
                        ui.SetText(lblRadSel, std::string("Selected: ") + lbl);
                });
        };

        cardRad.Children(
            ui.SectionTitle("Radio buttons (group A / group B)"),
            mkRadio("r1","Option A-1","grpA"),
            mkRadio("r2","Option A-2","grpA"),
            mkRadio("r3","Option A-3","grpA"),
            ui.Sep("sep_rad"),
            mkRadio("r4","Choice B-1","grpB"),
            mkRadio("r5","Choice B-2","grpB"),
            lblRadSel
        );

        auto cols = ui.Row("basics_cols", 12.f, 0.f)
            .Grow(1).Style(SDL::UI::Theme::Transparent())
            .Align(SDL::UI::Align::Start);
        auto leftC  = ui.Column("bc_left",  12.f, 0.f).Grow(1).Children(cardLbl, cardBtn);
        auto rightC = ui.Column("bc_right", 12.f, 0.f).Grow(1).Children(cardTog, cardRad);
        cols.Children(leftC, rightC);
        page.Child(cols);
        return page;
    }

    SDL::ECS::EntityId _BuildControlsPage() {
        auto page = _Page("page_controls");

        auto cardSld = ui.Card("card_sld");
        cardSld.Child(ui.SectionTitle("Sliders (horizontal)"));

        auto mkSliderRow = [&](
            const char* id, const char* lbl,
            float mn, float mx, float v, SDL::Color fill)
        {    
            auto vLbl = ui.Label(std::string(id) + "_v",
                                  std::format("{:.2f}", v))
                .W(50).TextColor(pal::GREY);
            
            auto sld = ui.Slider(id, mn, mx, v)
                .Grow(1).FillColor(fill)
                .OnChange([this, vLbl](float val){
                    ui.SetText(vLbl, std::format("{:.2f}", val));
                });
            
            cardSld.Child(
                ui.Row(std::string(id) + "_row", 10.f, 0.f)
                    .Style(SDL::UI::Theme::Transparent())
                    .Align(SDL::UI::Align::Center)
                    .Children(
                        ui.Label(std::string(id) + "_lbl", lbl)
                        .W(120).TextColor(pal::WHITE),
                        sld, vLbl
                    )
            );
        };
        mkSliderRow("sld_vol",  "Volume",  0, 1, .7f, pal::ACCENT);
        mkSliderRow("sld_spd",  "Speed",   0, 5, 1.f, pal::GREEN);
        mkSliderRow("sld_opc",  "Opacity", 0, 1, 1.f, pal::PURPLE);
        cardSld.Child(
            ui.Row("sld_dis_row", 10.f, 0.f)
                .Style(SDL::UI::Theme::Transparent()).Align(SDL::UI::Align::Center)
                .Children(
                    ui.Label("sld_dis_lbl", "Disabled").W(120),
                    ui.Slider("sld_dis", 0, 1, .3f).Grow(1).Enable(false))
        );

        auto cardKnob = ui.Card("card_knob");
        cardKnob.Child(ui.SectionTitle("Vertical Slider + Knobs"));

        lblKnob1 = ui.Label("lbl_k1", "Knob 1: 0.50").TextColor(pal::GREY);
        lblKnob2 = ui.Label("lbl_k2", "Knob 2: 50").TextColor(pal::GREY);

        auto sldV = ui.Slider("sld_v", 0, 1, .5f, SDL::UI::Orientation::Vertical)
            .H(120).W(24)
            .AlignSelf(SDL::UI::Align::Center)
            .FillColor(pal::ACCENT);

        auto knobRow = ui.Row("knob_row", 16.f, 0.f)
            .Style(SDL::UI::Theme::Transparent())
            .Align(SDL::UI::Align::Center)
            .Children(sldV,
                ui.Column("k1_col", 4.f, 0.f).Align(SDL::UI::Align::Center)
                    .Children(
                        ui.Knob("knob1", 0, 1, .5f).W(64).H(64)
                        .FillColor(pal::ACCENT).ThumbColor(pal::ACCENT)
                        .OnChange([this](float val){
                            ui.SetText(lblKnob1, std::format("Knob 1: {:.2f}", val)); }), lblKnob1),
                ui.Column("k2_col", 4.f, 0.f).Align(SDL::UI::Align::Center)
                    .Children(
                        ui.Knob("knob2", 0, 100, 50.f).W(64).H(64)
                        .FillColor(pal::PURPLE).ThumbColor(pal::PURPLE)
                        .OnChange([this](float val){
                            ui.SetText(lblKnob2, std::format("Knob 2: {:.0f}", val)); }), lblKnob2),
                ui.Knob("knob_dis", 0, 1, .3f).W(64).H(64).Enable(false)
            );
        cardKnob.Child(knobRow);

        auto cardProg = ui.Card("card_prog");
        cardProg.Children(
            ui.SectionTitle("Progress bars"),
            ui.Progress("prog_25", .25f).FillColor(pal::RED),
            ui.Progress("prog_50", .50f).FillColor(pal::ORANGE),
            ui.Progress("prog_75", .75f).FillColor(pal::GREEN),
            ui.Sep("sep_prog")
        );

        progAnimated = ui.Progress("prog_anim", 0.f).Grow(1).FillColor(pal::ACCENT);
        lblProgress  = ui.Label("lbl_prog", "0%").W(45).TextColor(pal::WHITE);
        auto btnPause = ui.Button("btn_pause", "Pause")
            .Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
            .W(80).H(28).AlignSelf(SDL::UI::Align::Center)
            .ClickSoundKey(key::CLICK);
        btnPause.OnClick([this, btnPause](){
            m_animRunning = !m_animRunning;
            ui.SetText(btnPause, m_animRunning ? "Pause" : "Resume");
        });
        auto sldSpd = ui.Slider("sld_aspd", 0.05f, 2.f, m_animSpeed).Grow(1)
            .OnChange([this](float val){ m_animSpeed = val; });

        cardProg.Children(
            ui.Row("anim_row", 10.f, 0.f)
              .Style(SDL::UI::Theme::Transparent()).Align(SDL::UI::Align::Center)
              .Children(progAnimated, lblProgress, btnPause),
            ui.Row("spd_row", 8.f, 0.f)
              .Style(SDL::UI::Theme::Transparent()).Align(SDL::UI::Align::Center)
              .Children(ui.Label("lbl_spd","Speed").W(50), sldSpd)
        );

        auto cols = ui.Row("ctrl_cols", 12.f, 0.f)
            .Grow(1).Style(SDL::UI::Theme::Transparent())
            .Align(SDL::UI::Align::Start);
        auto left  = ui.Column("cc_l", 12.f, 0.f).Grow(1)
            .Children(cardSld, cardKnob);
        auto right = ui.Column("cc_r", 12.f, 0.f).Grow(1)
            .Child(cardProg);
        cols.Children(left, right);
        page.Child(cols);
        return page;
    }

    SDL::ECS::EntityId _BuildInputScrollPage() {
        auto page = _Page("page_input_scroll");

        lblEcho = ui.Label("lbl_echo", "Echo: ").TextColor(pal::ACCENT);

        auto cardInp = ui.Card("card_inp");
        cardInp.Children(
            ui.SectionTitle("Text inputs"),
            ui.Input("inp1", "Enter your name...")
              .OnTextChange([this](const std::string& t){
                  ui.SetText(lblEcho, "Echo: " + t); }),
            lblEcho,
            ui.Sep("sep_i1"),
            ui.Input("inp2", "City name..."),
            ui.Input("inp3", "").SetText("Pre-filled"),
            ui.Input("inp4", "Cannot edit...").Enable(false),
            ui.Label("hint",
                "Keys: ← → move  Backspace/Del delete  Esc unfocus  Tab cycle")
              .TextColor(pal::GREY)
        );

        auto cardSB = ui.Card("card_sb");
        lblSB = ui.Label("lbl_sb", "Offset: 0.00").TextColor(pal::GREY);
        sbV = ui.ScrollBar("sb_v", 200.f, 80.f, SDL::UI::Orientation::Vertical)
            .H(120).AlignSelf(SDL::UI::Align::Center)
            .OnScroll([this](float off){
                ui.SetText(lblSB, std::format("Offset: {:.0f}", off)); });
        auto sbH = ui.ScrollBar("sb_h", 300.f, 100.f,
                                SDL::UI::Orientation::Horizontal)
            .W(SDL::UI::Value::Pw(80.f)).AlignSelf(SDL::UI::Align::Center);

        cardSB.Children(
            ui.SectionTitle("ScrollBar (vertical + horizontal)"),
            ui.Row("sb_row", 12.f, 0.f)
              .Style(SDL::UI::Theme::Transparent()).Align(SDL::UI::Align::Center)
              .Children(sbV,
                  ui.Column("sb_info", 6.f, 0.f).Children(
                      ui.Label("lbl_sb_tip","Drag the thumb").TextColor(pal::GREY),
                      lblSB)),
            ui.Sep("sep_sb"),
            ui.Label("lbl_sbh","Horizontal:").TextColor(pal::WHITE),
            sbH
        );

        auto cardSV = ui.Card("card_sv");
        cardSV.Child(ui.SectionTitle("Scrollable container (mouse wheel)"));
        auto svBuilder = ui.ScrollView("sv", 6.f)
            .H(150)
            .ScrollableY()
            .Padding(4);

        scrollContent = svBuilder.Id();
        for (int i = 1; i <= 20; ++i) {
            svBuilder.Child( 
                ui.Label(std::format("sv_item{}", i),
                          std::format("Scrollable item {:02d} - use mouse wheel", i))
                  .TextColor(i%3==0 ? pal::ACCENT : i%3==1 ? pal::GREEN : pal::WHITE)
                  .Padding(2, 1)
            );
        }
        cardSV.Child(scrollContent);

        auto cols = ui.Row("is_cols", 12.f, 0.f)
            .Grow(1).Style(SDL::UI::Theme::Transparent())
            .Align(SDL::UI::Align::Start);
        auto left  = ui.Column("is_l", 12.f, 0.f).Grow(1).Child(cardInp);
        auto right = ui.Column("is_r", 12.f, 0.f).Grow(1).Children(cardSB, cardSV);
        cols.Children(left, right);
        page.Child(cols);
        return page;
    }

    SDL::ECS::EntityId _BuildImageCanvasPage() {
        auto page = _Page("page_img_canvas");

        auto cardImg = ui.Card("card_img");
        cardImg.Child(ui.SectionTitle(
            std::format("Image widget - pool key \"{}\"", key::CRATE)));

        auto imgRow = ui.Row("img_row", 8.f, 0.f)
            .Style(SDL::UI::Theme::Transparent()).Align(SDL::UI::Align::Start);

        auto mkImg = [&](const char* id, const char* lbl, SDL::UI::ImageFit fit){
            auto col = ui.Column(std::string(id) + "_c", 4.f, 0.f);
            col.Children(
                ui.ImageWidget(id, key::CRATE, fit).W(90).H(90)
                  .WithStyle([](auto& s){ s.radius = SDL::FCorners(4.f); }),
                ui.Label(std::string(id) + "_l", lbl).TextColor(pal::GREY)
            );
            imgRow.Child(col);
        };
        mkImg("img_fill",  "Fill",    SDL::UI::ImageFit::Fill);
        mkImg("img_cont",  "Contain", SDL::UI::ImageFit::Contain);
        mkImg("img_cover", "Cover",   SDL::UI::ImageFit::Cover);
        mkImg("img_none",  "None",    SDL::UI::ImageFit::None);
        mkImg("img_tile",  "Tile",    SDL::UI::ImageFit::Tile);
        cardImg.Children(imgRow,
            ui.Label("img_hint",
                "If the key is absent from the pool the image shows a dark "
                "placeholder - no crash.")
              .TextColor(pal::GREY));

        auto cardCvs = ui.Card("card_cvs");
        cardCvs.Child(ui.SectionTitle("Canvas widget (custom rendering)"));

        auto canvas = ui.CanvasWidget("canvas",
            nullptr, nullptr,
            [this](SDL::RendererRef r, SDL::FRect rect) {
                r.SetDrawColor({30,34,52,255});
                r.RenderFillRect(rect);

                const float cellSize = 20.0f;

                r.SetDrawColor({50, 56, 80, 60});
                for (float x = rect.x; x < rect.x + rect.w; x += cellSize) {
                    for (float y = rect.y; y < rect.y + rect.h; y += cellSize) {
                        // On calcule l'index de la colonne et de la ligne
                        int ix = static_cast<int>((x - rect.x) / cellSize);
                        int iy = static_cast<int>((y - rect.y) / cellSize);

                        // Si la somme des index est paire, on dessine
                        if ((ix + iy) % 2 == 0) {
                            r.RenderFillRect(SDL::FRect{x, y, cellSize, cellSize});
                        }
                    }
                }
                r.SetDrawColor({50, 56, 80, 140});
                for (float x = rect.x; x <= rect.x + rect.w; x += cellSize) {
                    r.RenderLine({x, rect.y}, {x, rect.y + rect.h});
                }
                for (float y = rect.y; y <= rect.y + rect.h; y += cellSize) {
                    r.RenderLine({rect.x, y}, {rect.x + rect.w, y});
                }

                float cx = rect.x+rect.w*.5f, cy = rect.y+rect.h*.5f;
                float sz = std::min(rect.w,rect.h)*0.35f;
                float a  = m_canvasAngle * (SDL::PI_F/180.f);
                std::array<SDL::FPoint,5> pts;
                for (int i=0;i<4;++i){
                    float ang = a + i*SDL::PI_F*0.5f;
                    pts[i] = {cx+std::cos(ang)*sz, cy+std::sin(ang)*sz};
                }
                pts[4] = pts[0];
                r.SetDrawColor({70,130,210,255}); r.RenderLines(pts);
                float pulse = std::abs(std::sin(a*2.f))*8.f+4.f;
                r.SetDrawColor({155,75,220,255}); r.RenderFillCircle({cx,cy},pulse);
            }
        ).W(SDL::UI::Value::Pw(100)).H(160).Padding(0);

        cardCvs.Children(canvas,
            ui.Label("cvs_hint",
                "Canvas: pure SDL draw calls - no widget overhead.")
              .TextColor(pal::GREY));

        lblPoolInfo  = ui.Label("lbl_pool", "Pool: ...").TextColor(pal::ACCENT);
        lblEcsCount  = ui.Label("lbl_ecs",  "ECS entities: ...").TextColor(pal::WHITE);
        lblHovered   = ui.Label("lbl_hov",  "Hovered: (none)").TextColor(pal::WHITE);
        lblFocused   = ui.Label("lbl_foc",  "Focused: (none)").TextColor(pal::WHITE);

        auto cardInfo = ui.Card("card_info");
        cardInfo.Children(
            ui.SectionTitle("Subsystems - Resource pool - Scene Graph + ECS (live)"),
            ui.Sep("sep_sub"),
            lblPoolInfo, lblEcsCount, lblHovered, lblFocused,
            ui.Sep("sep_info"),
            ui.Label("n1",
                "SetDefaultFont(path, ptsize)  stamps font on every new widget's UI::Style.")
              .TextColor(pal::GREY),
            ui.Label("n2",
                "UI::FontCache (SDL3_ttf)  lazily opens fonts; SDL3_ttf caches glyph atlas.")
              .TextColor(pal::GREY),
            ui.Label("n3",
                "pool.Add<Texture>  |  pool.Add<UI::Sound>{Audio(mixer,path,false),mixer}")
              .TextColor(pal::GREY),
            ui.Label("n4",
                "MixerRef::PlayAudio(AudioRef(audio))  (fire-and-forget)")
              .TextColor(pal::GREY),
            ui.Sep("sep_info2"),
            ui.Button("btn_add_tex", "Add dummy pool entry")
              .Style(SDL::UI::Theme::PrimaryButton(pal::PURPLE))
              .W(200).H(30).ClickSoundKey(key::CLICK),
            ui.Button("btn_pool_prog", "Log pool to console")
              .Style(SDL::UI::Theme::GhostButton()).W(180).H(30)
              .OnClick([this]{
                  SDL::LogInfo(SDL::LOG_CATEGORY_APPLICATION,
                      std::format("Pool '{}': {} entries, progress {:.2f}%",
                      uiPool.GetName(), uiPool.Size(),
                      uiPool.LoadingProgress()*100.f).c_str());
              })
        );

        auto cols = ui.Row("ic_cols", 12.f, 0.f)
            .Grow(1).Style(SDL::UI::Theme::Transparent())
            .Align(SDL::UI::Align::Start);
        
        auto left  = ui.Column("ic_l", 12.f, 0.f).Grow(1)
            .Children(cardImg, cardCvs);
        auto right = ui.Column("ic_r", 12.f, 0.f).Grow(1)
            .Child(cardInfo);
        
        cols.Children(left, right);
        page.Child(cols);
        return page;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)