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
	static constexpr int        kPageCount = 8;
	static constexpr const char* kPageNames[kPageCount] = {
		"Basics", "Controls", "Input & Scroll", "Image & Canvas", "Text & Lists", "Grid", "New Widgets", "Advanced"
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

	SDL::ECS::Context  ecs_context;
	SDL::UI::System  ui{ ecs_context, renderer, mixer, uiPool };

	// ── Tabs ──────────────────────────────────────────────────────────────────────

	std::array<SDL::ECS::EntityId, kPageCount> pages   {};
	std::array<SDL::ECS::EntityId, kPageCount> tabBtns {};
	int currentPage = 6;

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
	SDL::ECS::EntityId lblKnob3     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId sbV          = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblSB        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId scrollContent= SDL::ECS::NullEntity;

	// ── Text & Lists page ─────────────────────────────────────────────────────────

	SDL::ECS::EntityId lblTaLen     = SDL::ECS::NullEntity; // TextArea char count
	SDL::ECS::EntityId lblListSel   = SDL::ECS::NullEntity; // ListBox selection echo
	SDL::ECS::EntityId graphLine    = SDL::ECS::NullEntity; // animated line graph
	SDL::ECS::EntityId graphBar     = SDL::ECS::NullEntity; // bar/spectrum graph

	// ── New-widgets page state ─────────────────────────────────────────────────────
	SDL::ECS::EntityId lblCombo     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblSpin      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblColor     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId spinnerWidget= SDL::ECS::NullEntity;
	SDL::ECS::EntityId badgeWidget  = SDL::ECS::NullEntity;
	int  m_badgeCount = 0;

	// ── Advanced-widgets page state ───────────────────────────────────────────
	SDL::ECS::EntityId colorPickerRgb  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId colorPickerGray = SDL::ECS::NullEntity;
	SDL::ECS::EntityId colorPickerGrad = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblPickedColor  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId treeWidget      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblTreeSel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId popupWidget     = SDL::ECS::NullEntity;

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
			ts.bgHoveredColor = (i == currentPage)
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
		if (ecs_context.IsAlive(graphLine)) {
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
		if (ecs_context.IsAlive(graphBar)) {
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
		ui.SetText(lblEcsCount, std::format("ECS entities: {}", ecs_context.EntityCount()));

		{
			std::string hov = "(none)", foc = "(none)";
			ecs_context.Each<SDL::UI::Widget, SDL::UI::WidgetState>(
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
		auto root = ui.Column("root", 0.f, 0.f)
			.BgColor(pal::BG).Borders(SDL::FBox(0.f)).Radius(SDL::FCorners(0.f))
			.Children(_BuildHeader(), _BuildContent());
		_BuildPopupOverlay(root);
		root.AsRoot();
	}

	SDL::ECS::EntityId _BuildHeader() {
		auto header = ui.Row("header", 8.f, 0.f)
			.W(SDL::UI::Value::Ww(100.f)).H(52.f)
			.PaddingH(12.f).PaddingV(2.f)
			.BgColor(pal::HEADER).BorderColor(pal::BORDER)
			.WithStyle([](auto& s){ s.borders = SDL::FBox(1.f); s.radius = SDL::FCorners(0.f); });

		header.Child(ui.Label("title", "SDL3pp UI — Widget Showcase")
			.TextColor(pal::ACCENT).W(SDL::UI::Value::Grow(100.f)).PaddingV(0));

		for (int i = 0; i < kPageCount; ++i) {
			tabBtns[i] = ui.Button(std::string("tab_") + kPageNames[i], kPageNames[i])
				.W(SDL::UI::Value::Auto())
				.H(20.f)
				.MinW(100)
				.AlignH(SDL::UI::Align::Center)
				.Style(SDL::UI::Theme::PrimaryButton(i == currentPage ? pal::TAB_ON : pal::TAB_OFF))
				.Padding(SDL::FBox(2.f))
				.WithStyle([](auto& s){ s.borders = SDL::FBox(1.f); s.radius = SDL::FCorners(5.f); })
				.ClickSound(key::CLICK)
				.Tooltip(std::string("Switch to ") + kPageNames[i])
				.OnClick([this, i]{ SwitchPage(i); });
			header.Child(tabBtns[i]);
		}
		return header;
	}

	SDL::ECS::EntityId _BuildContent() {
		auto content = ui.Column("content", 0.f, 0.f)
			.H(SDL::UI::Value::Grow(100.f)).PaddingH(16.f).PaddingV(12.f)
			.BgColor(pal::BG)
			.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); });

		pages[0] = _BuildBasicsPage();
		pages[1] = _BuildControlsPage();
		pages[2] = _BuildInputScrollPage();
		pages[3] = _BuildImageCanvasPage();
		pages[4] = _BuildTextListsPage();
		pages[5] = _BuildGridPage();
		pages[6] = _BuildNewWidgetsPage();
		pages[7] = _BuildAdvancedPage();

		currentPage = SDL::Clamp(currentPage, 0, kPageCount-1);
		for (int i = 0; i < kPageCount; ++i) {
			content.Child(pages[i]);
			ui.GetStyle(pages[i]).showSound = key::MENU_OPEN;
			ui.SetVisible(pages[i], i == currentPage);
		}
		return content;
	}

	SDL::UI::Builder _Page(const std::string& n) {
		return ui.Column(n, 12.f, 0.f)
			.Style(SDL::UI::Theme::Transparent())
			.H(SDL::UI::Value::Grow(100.f))
			.BgColor(pal::BG)
			.AutoScrollable(true)
			.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); });
	}

	// ── 2-column row helper ───────────────────────────────────────────────────────

	SDL::UI::Builder _TwoColRow(const std::string& n) {
		return ui.Row(n, 12.f, 0.f)
			.H(SDL::UI::Value::Grow(100.f)).Style(SDL::UI::Theme::Transparent())
			.AlignH(SDL::UI::Align::Left);
	}
	SDL::UI::Builder _LeftCol(const std::string& n) {
		return ui.Column(n, 12.f, 0.f)
			.W(SDL::UI::Value::Pcw(50.f) - 6.f)
			.Style(SDL::UI::Theme::Transparent());
	}
	SDL::UI::Builder _RightCol(const std::string& n) {
		return ui.Column(n, 12.f, 0.f)
			.W(SDL::UI::Value::Pcw(50.f) - 6.f)
			.Style(SDL::UI::Theme::Transparent());
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
					.ClickSound(key::CLICK).HoverSound(key::HOVER)
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
				.ClickSound(key::CLICK),
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
			ui.Separator("sep_rad"),
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
			auto sld = ui.Slider(id, mn, mx, v).W(SDL::UI::Value::Grow(100.f)).FillColor(fill)
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
					ui.Slider("sld_dis", 0, 1, .3f).W(SDL::UI::Value::Grow(100.f)).Enable(false)
						.Tooltip("Disabled slider — read-only"))
		);

		// ── Vertical slider + Knobs ───────────────────────────────────────────────
		auto cardKnob = ui.Card("card_knob");
		cardKnob.Child(ui.SectionTitle("Vertical Slider + Knobs"));
		lblKnob1 = ui.Label("lbl_k1", "Knob 1: 0.50").TextColor(pal::GREY);
		lblKnob2 = ui.Label("lbl_k2", "Knob 2: 50.0").TextColor(pal::GREY);
		lblKnob3 = ui.Label("lbl_k3", "Knob 3: 50.0").TextColor(pal::GREY);

		auto sldV = ui.Slider("sld_v", 0, 1, .5f, SDL::UI::Orientation::Vertical)
			.H(120).W(24).AlignH(SDL::UI::Align::Center).FillColor(pal::ACCENT)
			.Tooltip("Vertical slider [0–1]");

		auto knobRow = ui.Row("knob_row", 16.f, 0.f)
			.Style(SDL::UI::Theme::Transparent()).AlignH(SDL::UI::Align::Center)
			.Children(
				sldV,
				ui.Column("k1_col", 4.f, 0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob("knob1", 0, 1, .5f)
							.W(64).H(64)
							.FillColor(pal::ACCENT).ThumbColor(pal::ACCENT)
							.Tooltip("Knob 1 — drag or scroll [0–1]")
							.OnChange([this](float v){ ui.SetText(lblKnob1, std::format("Knob 1: {:.2f}", v)); }),
						lblKnob1
					),
				ui.Column("k2_col", 4.f, 0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob("knob2", 0, 100, 50.f)
							.W(64).H(64)
							.FillColor(pal::PURPLE).ThumbColor(pal::PURPLE)
							.Tooltip("Knob 2 — drag or scroll [0–100]")
							.OnChange([this](float v){ ui.SetText(lblKnob2, std::format("Knob 2: {:.1f}", v)); }),
						lblKnob2
					),
				ui.Column("k3_col", 4.f, 0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob("knob3", 0, 100, 50.f, SDL::UI::KnobShape::Potentiometer)
							.W(64).H(64)
							.FillColor(pal::GREEN).ThumbColor(pal::GREEN)
							.Tooltip("Knob 3 — drag or scroll [0–100]")
							.OnChange([this](float v){ ui.SetText(lblKnob3, std::format("Knob 3: {:.1f}", v)); }),
						lblKnob3
					),
				ui.Column("k_dis_col", 4.f, 0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob("knob_dis", 0, 1, .3f, SDL::UI::KnobShape::Potentiometer)
							.W(64).H(64)
							.Enable(false)
							.Tooltip("Disabled knob"),
						ui.Label("lbl_kdis","Disabled").TextColor(pal::GREY)
					)
			);
		cardKnob.Child(knobRow);

		// ── Progress bars ─────────────────────────────────────────────────────────
		auto cardProg = ui.Card("card_prog");
		cardProg.Children(
			ui.SectionTitle("Progress bars"),
			ui.Progress("prog_25", .25f).FillColor(pal::RED)   .Tooltip("25% — danger zone"),
			ui.Progress("prog_50", .50f).FillColor(pal::ORANGE).Tooltip("50% — half way"),
			ui.Progress("prog_75", .75f).FillColor(pal::GREEN) .Tooltip("75% — almost done"),
			ui.Separator("sep_prog")
		);

		progAnimated = ui.Progress("prog_anim", 0.f).W(SDL::UI::Value::Grow(100.f)).FillColor(pal::ACCENT)
			.Tooltip("Animated progress");
		lblProgress  = ui.Label("lbl_prog", "0%").W(45).TextColor(pal::WHITE);
		auto btnPause = ui.Button("btn_pause", "Pause")
			.Style(SDL::UI::Theme::PrimaryButton(pal::ORANGE))
			.W(80).H(28).AlignH(SDL::UI::Align::Center)
			.ClickSound(key::CLICK)
			.Tooltip("Pause/resume the progress animation");
		btnPause.OnClick([this, btnPause](){
			m_animRunning = !m_animRunning;
			ui.SetText(btnPause, m_animRunning ? "Pause" : "Resume");
		});
		auto sldSpd = ui.Slider("sld_aspd", 0.05f, 2.f, m_animSpeed).W(SDL::UI::Value::Grow(100.f))
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
			ui.Separator("sep_i1"),
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
			ui.Separator("sep_sb"),
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
			ui.Separator("sep_sub"),
			lblPoolInfo, lblEcsCount, lblHovered, lblFocused,
			ui.Separator("sep_info"),
			ui.Label("n1","SetDefaultFont(path, ptsize) — stamps font on every new widget")
				.TextColor(pal::GREY),
			ui.Label("n2","ResourcePool stores Texture / Font / Audio by string key")
				.TextColor(pal::GREY),
			ui.Label("n3","Mixer::PlayAudio() — fire-and-forget, no handle needed")
				.TextColor(pal::GREY),
			ui.Separator("sep_info2"),
			ui.Button("btn_pool_log","Log pool to console")
				.Style(SDL::UI::Theme::GhostButton()).W(180).H(30)
				.Tooltip("Print pool stats to the SDL log")
				.ClickSound(key::CLICK)
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
		auto cardTa = ui.Card("card_ta")
			.H(SDL::UI::Value::Grow(50.f))
			.MinH(150.f);
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
			.H(SDL::UI::Value::Grow(100.f))
			.AutoScrollable(true, true)
			.Tooltip("Multi-line rich text editor — click, type, select, copy/paste")
			.OnTextChange([this](const std::string& t){
				ui.SetText(lblTaLen, std::format("Characters: {}", (int)t.size()));
			});

		// Pre-colour the first word of each bullet in a different colour
		if (auto* td = ecs_context.Get<SDL::UI::TextAreaData>(ta.Id())) {
			auto addSpan = [&](int start, int end, SDL::Color col){
				td->spans.push_back({start, end, {false, false, col}});
			};
			addSpan(4,  12, pal::ACCENT);   // "TextArea"
			addSpan(13, 20, pal::GREEN);    // "Multi-l"... let's colour "supports"
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
						.TextColor(pal::GREY).W(SDL::UI::Value::Grow(100.f))
				)
		);

		// ── ListBox ───────────────────────────────────────────────────────────────
		auto cardLb = ui.Card("card_lb")
			.H(SDL::UI::Value::Grow(50.f))
			.MinH(150.f);
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
			.H(SDL::UI::Value::Grow(100.f))
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
		auto cardGl = ui.Card("card_graph_line")
			.H(SDL::UI::Value::Grow(50.f))
			.MinH(150.f);
		graphLine = ui.GradedGraph("g_line")
			.W(SDL::UI::Value::Grow(100.f))
			.H(SDL::UI::Value::Grow(100.f))
			.Tooltip("Animated sine-wave (line mode, updated every frame)");
		if (auto* gd = ui.GetGraphData(graphLine)) {
			gd->title     = "Sine Wave (animated)";
			gd->yLabel    = "amp";
			gd->minVal    = -1.f;
			gd->maxVal    =  1.f;
			gd->showFill  = true;
			gd->barMode   = false;
			gd->lineColor = pal::ACCENT;
			gd->fillColor = {70,130,210, 45};
		}
		cardGl.Children(ui.SectionTitle("Graph — line mode (animated)"), graphLine);

		auto cardGb = ui.Card("card_graph_bar")
			.H(SDL::UI::Value::Grow(50.f))
			.MinH(150.f);
		graphBar = ui.GradedGraph("g_bar")
			.W(SDL::UI::Value::Grow(100.f))
			.H(SDL::UI::Value::Grow(100.f))
			.Tooltip("Animated bar graph / spectrum (bar mode, updated every frame)");
		if (auto* gd = ui.GetGraphData(graphBar)) {
			gd->title      = "Spectrum (animated)";
			gd->xLabel     = "freq";
			gd->yLabel     = "mag";
			gd->minVal     = 0.f;
			gd->maxVal     = 1.f;
			gd->showFill   = false;
			gd->barMode    = true;
			gd->lineColor  = pal::GREEN;
			gd->fillColor  = pal::GREEN;
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

	// ══════════════════════════════════════════════════════════════════════════════
	// Page 6 — Grid: Layout::InGrid avec positionnement et spanning de cellules
	// ══════════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildGridPage() {
		using namespace SDL::UI;
		auto page = _Page("page_grid");

		// ── Section 1 : grille fixe 4 colonnes, auto-placement ───────────────────
		{
			auto card = ui.Card("card_auto_grid");
			card.Child(ui.SectionTitle("Grille 4 colonnes — placement automatique"));

			// GridSizing::Fixed : colonnes/lignes de taille égale (conteneur / count).
			// GridLines::Both   : lignes de séparation horizontales + verticales.
			auto grid = ui.Grid("auto_grid", /*columns=*/4, /*gap=*/6.f, /*pad=*/6.f)
				.W(Value::Pcw(100))
				.GridColSizing(GridSizing::Fixed)
				.GridRowSizing(GridSizing::Content)
				.GridLineStyle(GridLines::Both)
				.GridLineColor({60, 65, 95, 180})
				.GridLineThickness(1.f)
				.Tooltip("4 colonnes — les 12 widgets sont placés automatiquement\nleft-to-right, top-to-bottom");

			const SDL::Color cellColors[] = {
				pal::ACCENT, pal::TEAL, pal::GREEN, pal::ORANGE,
				pal::PURPLE, pal::RED,  pal::WHITE, pal::GREY,
			};
			for (int i = 0; i < 12; ++i) {
				SDL::Color col = cellColors[i % 8];
				grid.Child(
					ui.Label(std::format("auto_{}", i), std::format("#{}", i))
						.Padding(10).TextColor(col)
						.AlignH(Align::Center)
						.WithStyle([](Style &s){ s.bgColor = {25,26,38,255}; s.radius = {3,3,3,3}; })
				);
			}
			card.Child(grid);
			page.Child(card);
		}

		// ── Section 2 : placement explicite + spanning ───────────────────────────
		{
			auto card = ui.Card("card_span_grid");
			card.Child(ui.SectionTitle("Grille 4×3 — placement explicite et colspan/rowspan"));

			// GridSizing::Content : chaque colonne/ligne s'adapte à son contenu.
			auto grid = ui.Grid("span_grid", /*columns=*/4, /*gap=*/8.f, /*pad=*/8.f)
				.W(Value::Pcw(100))
				.GridColSizing(GridSizing::Content)
				.GridRowSizing(GridSizing::Content)
				.GridLineStyle(GridLines::Both)
				.GridLineColor({55, 60, 90, 160})
				.Tooltip("Placement explicite via .Cell(col, row, colSpan, rowSpan)");

			// Ligne 0 : 3 cellules simples + 1 cellule span col=2
			grid.Child(ui.Button("b00", "Col 0,0").Padding(8), 0, 0);
			grid.Child(ui.Button("b10", "Col 1,0").Padding(8), 1, 0);
			// Span horizontal : occupe les colonnes 2 et 3 sur la ligne 0
			grid.Child(
				ui.Label("span_h", "Span col 2-3 (colSpan=2)")
					.Padding(8).AlignH(Align::Center).TextColor(pal::ACCENT)
					.WithStyle([](Style &s){ s.bgColor = {30,50,90,200}; s.radius = {4,4,4,4}; }),
				/*col=*/2, /*row=*/0, /*colSpan=*/2, /*rowSpan=*/1
			);

			// Ligne 1 : 4 cellules simples
			for (int c = 0; c < 4; ++c) {
				grid.Child(
					ui.Label(std::format("l1_{}", c), std::format("L1 C{}", c))
						.Padding(8).AlignH(Align::Center)
						.WithStyle([](Style &s){ s.bgColor = {28,30,44,255}; }),
					c, 1
				);
			}

			// Ligne 2 : cellule normale + span vertical (rowSpan=2 sur lignes 2-3)
			grid.Child(ui.Toggle("tog_20", "Toggle").Padding(8), 0, 2);
			grid.Child(ui.Label("l21", "Cell 1,2").Padding(8).AlignH(Align::Center), 1, 2);
			// Span vertical : occupe les lignes 2 et 3, colonne 2
			grid.Child(
				ui.Label("span_v", "Span\nrows 2-3\n(rowSpan=2)")
					.Padding(8).AlignH(Align::Center).TextColor(pal::GREEN)
					.WithStyle([](Style &s){ s.bgColor = {20,55,35,210}; s.radius = {4,4,4,4}; }),
				/*col=*/2, /*row=*/2, /*colSpan=*/1, /*rowSpan=*/2
			);
			grid.Child(ui.Label("l22", "Cell 3,2").Padding(8).AlignH(Align::Center), 3, 2);

			// Ligne 3 : 2 cellules + span vertical déjà occupé en col 2
			grid.Child(ui.Label("l30", "Cell 0,3").Padding(8).AlignH(Align::Center), 0, 3);
			grid.Child(ui.Label("l31", "Cell 1,3").Padding(8).AlignH(Align::Center), 1, 3);
			grid.Child(ui.Label("l33", "Cell 3,3").Padding(8).AlignH(Align::Center), 3, 3);

			card.Child(grid);
			page.Child(card);
		}

		// ── Section 3 : grille fixe, taille égale, lignes uniquement en lignes ──
		{
			auto card = ui.Card("card_row_lines");
			card.Child(ui.SectionTitle("Grille 3 colonnes — GridLines::Rows, taille Fixed"));

			auto grid = ui.Grid("row_lines_grid", /*columns=*/3, /*gap=*/6.f, /*pad=*/6.f)
				.W(Value::Pcw(100))
				.GridSizingMode(GridSizing::Fixed, GridSizing::Fixed)
				.GridLineStyle(GridLines::Rows)
				.GridLineColor({90, 95, 130, 200})
				.GridLineThickness(2.f);

			const char* labels[] = {
				"Nom",     "Valeur",  "Unité",
				"Largeur", "320",     "px",
				"Hauteur", "180",     "px",
				"FPS",     "60",      "Hz",
				"Opacité", "100",     "%",
			};
			for (int i = 0; i < 15; ++i) {
				SDL::Color tc = (i < 3) ? pal::ACCENT : pal::WHITE;
				grid.Child(
					ui.Label(std::format("rl_{}", i), labels[i])
						.Padding(6).TextColor(tc)
						.WithStyle([i](Style &s){ if (i < 3) s.bgColor = {22,28,48,255}; })
				);
			}
			card.Child(grid);
			page.Child(card);
		}

		return page;
	}

	// ══════════════════════════════════════════════════════════════════════════════
	// Page 7 — New Widgets: ComboBox, SpinBox, TabView, Expander, Splitter,
	//                       Spinner, Badge
	// ══════════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildNewWidgetsPage() {
		using namespace SDL::UI;
		auto page = _Page("page_new");

		// ── ComboBox + SpinBox ─────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_combo_spin");
			card.Child(ui.SectionTitle("ComboBox & SpinBox"));

			lblCombo = ui.Label("lbl_combo_val", "Selected: (none)")
				.TextColor(pal::GREY);
			auto combo = ui.ComboBox("combo1",
				{"Apple", "Banana", "Cherry", "Date", "Elderberry",
				 "Fig", "Grape", "Honeydew"}, 0)
				.W(Value::Pcw(100)).H(32.f)
				.Tooltip("Click to open dropdown")
				.OnChange([this](float idx){
					const char* items[] = {"Apple","Banana","Cherry","Date","Elderberry","Fig","Grape","Honeydew"};
					ui.SetText(lblCombo, std::string("Selected: ") + items[(int)idx]);
				});

			lblSpin = ui.Label("lbl_spin_val", "Value: 0")
				.TextColor(pal::GREY);
			auto spin = ui.SpinBox("spin1", 0.f, 100.f, 42.f, /*intMode=*/true)
				.W(Value::Pcw(100)).H(32.f)
				.Tooltip("Drag up/down to change value, or click +/– buttons")
				.OnChange([this](float v){
					ui.SetText(lblSpin, std::format("Value: {:.0f}", v));
				});
			auto spinFloat = ui.SpinBox("spin_float", 0.f, 1.f, 0.5f, false)
				.SpinDecimals(3).W(Value::Pcw(100)).H(32.f)
				.Tooltip("Float spin box — drag or +/– buttons")
				.OnChange([](float v){ (void)v; });

			card.Children(
				combo, lblCombo,
				ui.Separator("sep_cs"),
				spin, lblSpin,
				spinFloat
			);
			page.Child(card);
		}

		// ── Spinner + Badge ───────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_spin_badge");
			card.Child(ui.SectionTitle("Spinner (animated) & Badge"));

			spinnerWidget = ui.Spinner("sp1", 4.f)
				.W(48.f).H(48.f)
				.Tooltip("Animated loading indicator (auto-rotates each frame)");

			badgeWidget = ui.Badge("badge1", "0")
				.W(Value::Auto()).H(24.f)
				.Tooltip("Small notification counter");

			auto btnInc = ui.Button("btn_badge_inc", "+ Increment badge")
				.W(Value::Pcw(100)).H(32.f)
				.OnClick([this]{
					++m_badgeCount;
					ui.GetECSContext().Get<SDL::UI::BadgeData>(badgeWidget)->text =
						std::to_string(m_badgeCount);
				});
			auto btnReset = ui.Button("btn_badge_reset", "Reset badge")
				.W(Value::Pcw(100)).H(32.f)
				.OnClick([this]{
					m_badgeCount = 0;
					ui.GetECSContext().Get<SDL::UI::BadgeData>(badgeWidget)->text = "0";
				});

			card.Children(
				ui.Row("spin_row", 8.f, 0.f).H(60.f).Children(spinnerWidget, badgeWidget),
				btnInc, btnReset
			);
			page.Child(card);
		}

		// ── Expander ───────────────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_expander");
			card.Child(ui.SectionTitle("Expander (collapsible section)"));

			auto exp1 = ui.Expander("exp1", "Advanced Settings", false)
				.W(Value::Pcw(100))
				.Tooltip("Click header to expand/collapse");
			auto content1 = ui.Column("exp1_body", 6.f, 4.f)
				.BgColor({25, 26, 38, 200})
				.Children(
					ui.Label("exp1_l1", "Setting A:  enabled").TextColor(pal::WHITE),
					ui.Label("exp1_l2", "Setting B:  42 px").TextColor(pal::WHITE),
					ui.Toggle("exp1_tog", "Option C")
				);
			exp1.Child(content1);
			// Start collapsed — hide child
			ui.SetVisible(content1, false);

			auto exp2 = ui.Expander("exp2", "Developer Info", true)
				.W(Value::Pcw(100));
			auto content2 = ui.Column("exp2_body", 4.f, 4.f)
				.BgColor({25, 28, 40, 200})
				.Children(
					ui.Label("exp2_l1", "SDK: SDL3pp").TextColor(pal::GREY),
					ui.Label("exp2_l2", "UI: ECS-backed retained-mode").TextColor(pal::GREY)
				);
			exp2.Child(content2);

			card.Children(exp1, exp2);
			page.Child(card);
		}

		// ── TabView ────────────────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_tabview");
			card.Child(ui.SectionTitle("TabView (tabbed container)"));

			auto tv = ui.TabView("tv1")
				.W(Value::Pcw(100)).H(140.f)
				.AddTab("Overview").AddTab("Details").AddTab("Settings")
				.Tooltip("Click tabs to switch content");

			// Child 0 — Overview tab
			auto tab0 = ui.Column("tv1_t0", 6.f, 8.f)
				.Children(
					ui.Label("tv1_l0a", "Welcome to TabView!").TextColor(pal::ACCENT),
					ui.Label("tv1_l0b", "Each tab shows a different child widget.")
				);
			// Child 1 — Details tab
			auto tab1 = ui.Column("tv1_t1", 6.f, 8.f)
				.Children(
					ui.Label("tv1_l1a", "Widget: TabView").TextColor(pal::GREEN),
					ui.Label("tv1_l1b", "Children per tab are shown/hidden on click.")
				);
			// Child 2 — Settings tab
			auto tab2 = ui.Column("tv1_t2", 6.f, 8.f)
				.Children(
					ui.Slider("tv1_slider", 0.f, 1.f, 0.5f).W(Value::Pcw(100)),
					ui.Toggle("tv1_tog", "Enable feature")
				);
			tv.Children(tab0, tab1, tab2);
			// Show only first tab initially
			ui.SetVisible(tab1, false);
			ui.SetVisible(tab2, false);

			card.Child(tv);
			page.Child(card);
		}

		// ── Splitter ───────────────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_splitter");
			card.Child(ui.SectionTitle("Splitter (resizable panes — drag the handle)"));

			auto spl = ui.Splitter("spl1", SDL::UI::Orientation::Horizontal, 0.4f)
				.W(Value::Pcw(100)).H(120.f)
				.Tooltip("Drag the vertical divider to resize the two panes");
			auto left = ui.Column("spl_left", 4.f, 6.f)
				.BgColor({20, 22, 36, 255})
				.Children(
					ui.Label("spl_ll", "Left Pane").TextColor(pal::ACCENT),
					ui.Label("spl_ll2", "Drag handle →").TextColor(pal::GREY)
				);
			auto right = ui.Column("spl_right", 4.f, 6.f)
				.BgColor({22, 20, 36, 255})
				.Children(
					ui.Label("spl_rl", "Right Pane").TextColor(pal::TEAL),
					ui.Label("spl_rl2", "← Drag handle").TextColor(pal::GREY)
				);
			spl.Children(left, right);

			card.Child(spl);
			page.Child(card);
		}

		return page;
	}

	// ══════════════════════════════════════════════════════════════════════════
	// Page 8 — Advanced: ColorPicker, Tree, Popup, BgGradient
	// ══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildAdvancedPage() {
		using namespace SDL::UI;
		auto page = _Page("page_advanced");

		auto cols  = _TwoColRow("adv_cols");
		auto left  = _LeftCol("adv_l");
		auto right = _RightCol("adv_r");

		// ── ColorPicker ───────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_pickers");
			card.Child(ui.SectionTitle("ColorPicker"));

			lblPickedColor = ui.Label("lbl_picked", "RGB:  70 130 210")
				.TextColor(pal::GREY);

			colorPickerRgb = ui.ColorPicker("cp_rgb", ColorPickerPalette::RGB8)
				.W(220).H(200)
				.PickedColor(pal::ACCENT)
				.PickerShowAlpha(false)
				.OnChange([this](float){
					auto c = ui.GetPickedColor(colorPickerRgb);
					ui.SetText(lblPickedColor,
						std::format("RGB: {:3d} {:3d} {:3d}", c.r, c.g, c.b));
				})
				.Tooltip("RGB8 picker — drag SV square and hue bar");

			colorPickerGray = ui.ColorPicker("cp_gray", ColorPickerPalette::Grayscale)
				.W(Value::Pw(100)).H(28)
				.Tooltip("Grayscale picker — drag to pick a grey value");

			colorPickerGrad = ui.ColorPicker("cp_grad", ColorPickerPalette::GradientAB)
				.W(Value::Pw(100)).H(28)
				.PickerColorA(pal::ACCENT)
				.PickerColorB(pal::GREEN)
				.Tooltip("Gradient A\xe2\x86\x92""B picker");

			card.Children(
				colorPickerRgb, lblPickedColor,
				ui.Separator("sep_cp"),
				ui.Label("lbl_cp_gs", "Grayscale:").TextColor(pal::GREY),
				colorPickerGray,
				ui.Label("lbl_cp_gr", "Gradient A\xe2\x86\x92""B:").TextColor(pal::GREY),
				colorPickerGrad
			);
			left.Child(card);
		}

		// ── BgGradient demo ───────────────────────────────────────────────────
		{
			auto card = ui.Card("card_gradient");
			card.Child(ui.SectionTitle("Background gradients (BgGradient)"));

			struct GSpec { const char* n; const char* lbl;
			               SDL::Color c1; SDL::Color c2; GradientAnchor start; GradientAnchor end; };
			static const GSpec kG[] = {
				{"gb_blue",  "Blue \xe2\x86\x95",
					{ 30, 60,150,255}, { 80,160,255,255},
					GradientAnchor::Left, GradientAnchor::Right },
				{"gb_green", "Green \xe2\x86\x94",
					{ 20,110, 50,255}, { 60,200, 90,255},
					GradientAnchor::Top, GradientAnchor::Bottom },
				{"gb_orange","Orange \xe2\x86\x95",
					{150, 65, 10,255}, {240,155, 35,255},
					GradientAnchor::TopLeft, GradientAnchor::BottomRight },
				{"gb_purple","Purple \xe2\x86\x95",
					{ 55, 18,110,255}, {150, 75,215,255},
					GradientAnchor::BottomLeft, GradientAnchor::TopRight },
			};
			auto btnRow = ui.Row("grad_row", 8.f, 0.f).Style(Theme::Transparent());
			for (auto& g : kG) {
				SDL::Color c1 = g.c1, c2 = g.c2;
				btnRow.Child(
					ui.Button(g.n, g.lbl)
						.W(106).H(44).AlignH(Align::Center)
						.WithStyle([c1](Style& s){
							s.bgColor        = c1;
							s.bgHoveredColor = {SDL::Clamp8(c1.r+25),
							                    SDL::Clamp8(c1.g+25),
							                    SDL::Clamp8(c1.b+25),255};
							s.bgPressedColor = {SDL::Clamp8(c1.r*0.6f),
												SDL::Clamp8(c1.g*0.6f),
							                    SDL::Clamp8(c1.b*0.6f),255};
							s.radius = {6,6,6,6};
						})
						.BgGradient(c2, g.start, g.end)
						.ClickSound(key::CLICK)
				);
			}
			card.Child(btnRow);
			left.Child(card);
		}

		// ── Tree ──────────────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_tree");
			card.Child(ui.SectionTitle("Tree widget"));

			auto lblSel = ui.Label("lbl_tree_sel", "Selection: (none)")
				.TextColor(pal::ACCENT);
			lblTreeSel = lblSel.Id();

			treeWidget = ui.Tree("tree1")
				.W(Value::Pw(100)).H(200)
				.TreeNode("src/",           0, true,  true)
				.TreeNode("main.cpp",       1, false, false)
				.TreeNode("SDL3pp_ui/",     1, true,  false)
				.TreeNode("system.h",       2, false, false)
				.TreeNode("components.h",   2, false, false)
				.TreeNode("builder.h",      2, false, false)
				.TreeNode("assets/",        0, true,  true)
				.TreeNode("fonts/",         1, true,  false)
				.TreeNode("DejaVuSans.ttf", 2, false, false)
				.TreeNode("textures/",      1, true,  false)
				.TreeNode("crate.jpg",      2, false, false)
				.TreeNode("sounds/",        1, true,  false)
				.TreeNode("click.mp3",      2, false, false)
				.TreeNode("README.md",      0, false, false)
				.OnClick([this]{
					int sel = ui.GetTreeSelection(treeWidget);
					ui.SetText(lblTreeSel,
						sel >= 0 ? std::format("Selected node #{}", sel)
						         : "Selection: (none)");
				})
				.Tooltip("Click to select  •  arrow to expand/collapse  •  scroll wheel");

			card.Children(treeWidget, lblSel);
			right.Child(card);
		}

		// ── Popup ─────────────────────────────────────────────────────────────
		{
			auto card = ui.Card("card_popup_open");
			card.Children(
				ui.SectionTitle("Popup widget"),
				ui.Label("lbl_popup_hint",
					"Floating window: drag title bar to move, corner to resize.")
					.TextColor(pal::GREY),
				ui.Button("btn_open_popup", "Open Popup")
					.W(130).H(32)
					.Style(Theme::PrimaryButton(pal::ACCENT))
					.ClickSound(key::CLICK)
					.Tooltip("Open the floating popup window")
					.OnClick([this]{ ui.SetPopupOpen(popupWidget, true); })
			);
			right.Child(card);
		}

		cols.Children(left, right);
		page.Child(cols);
		return page;
	}

	void _BuildPopupOverlay(SDL::UI::Builder& root) {
		using namespace SDL::UI;

		auto popup = ui.Popup("popup1", "Settings", true, true, true)
			.X(240.f).Y(170.f)
			.W(290).H(230)
			.PopupOpen(false)
			.Tooltip("Drag title bar to move  •  drag corner to resize");
		popupWidget = popup.Id();

		popup.Children(
			ui.Label("pop_l1", "Floating popup window").TextColor(pal::WHITE),
			ui.Slider("pop_sld", 0.f, 1.f, 0.5f)
				.W(Value::Pw(100)).FillColor(pal::ACCENT)
				.Tooltip("Slider inside the popup"),
			ui.Toggle("pop_tog", "Enable option")
				.Tooltip("Toggle inside the popup"),
			ui.Separator("pop_sep"),
			ui.Button("pop_close_btn", "Close")
				.W(90).H(28)
				.Style(Theme::PrimaryButton(pal::RED))
				.ClickSound(key::CLICK)
				.OnClick([this]{ ui.SetPopupOpen(popupWidget, false); })
		);

		root.Child(popup);
	}

};

SDL3PP_DEFINE_CALLBACKS(Main)
