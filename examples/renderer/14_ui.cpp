/**
 * @file 14_ui.cpp
 * @brief SDL3pp new UI module — complete widget showcase (all widget types).
 *
 * Tabs: Basics | Controls | Input & Scroll | Image & Canvas |
 *       Text & Lists | Grid | New Widgets | Advanced
 */

#define SDL3PP_ENABLE_NEW_UI
#define SDL3PP_MAIN_USE_CALLBACKS

#include <SDL3pp/SDL3pp.h>
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
	constexpr const char* FONT      = "deja-vu-sans";
	constexpr const char* CRATE     = "crate";
	constexpr const char* CLICK     = "click";
	constexpr const char* HOVER     = "hover";
	constexpr const char* MENU_OPEN = "menu-open";
}

struct Main {
	using NVf = SDL::UI::NumericValue<float>;
	using NVi = SDL::UI::NumericValue<int>;

	static constexpr SDL::Point kWinSz     = {1280, 720};
	static constexpr int        kPageCount = 8;
	static constexpr const char* kPageNames[kPageCount] = {
		"Basics", "Controls", "Input & Scroll", "Image & Canvas",
		"Text & Lists", "Grid", "New Widgets", "Advanced"
	};

	// ── SDL app callbacks ─────────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
		for (auto arg : args) {
			if      (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
			else if (arg == "--debug")   priority = SDL::LOG_PRIORITY_DEBUG;
			else if (arg == "--info")    priority = SDL::LOG_PRIORITY_INFO;
		}
		SDL::SetLogPriorities(priority);
		SDL::SetAppMetadata("SDL3pp UI New", "1.0", "com.example.ui");
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
			"SDL3pp - New UI Showcase", kWinSz,
			SDL::WINDOW_RESIZABLE, nullptr);
	}

	// ── Resources & core ─────────────────────────────────────────────────────

	SDL::MixerRef mixer{ SDL::CreateMixerDevice(
		SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK,
		SDL::AudioSpec{.format=SDL::AUDIO_F32, .channels=2, .freq=48000}) };

	SDL::Window      window  { InitWindow()         };
	SDL::RendererRef renderer{ window.GetRenderer() };

	SDL::ResourceManager rm;
	SDL::ResourcePool&   uiPool{ *rm.CreatePool("ui") };

	SDL::ECS::Context  ecs_context;
	SDL::UI::Context    ui{ ecs_context, renderer, mixer, uiPool };
	SDL::FrameTimer    timer{ 60.f };

	// ── Pages / tab buttons ───────────────────────────────────────────────────

	std::array<SDL::ECS::EntityId, kPageCount> pages  {};
	std::array<SDL::ECS::EntityId, kPageCount> tabBtns{};
	int currentPage = 0;

	// ── Live-update entities ──────────────────────────────────────────────────

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

	SDL::ECS::EntityId lblTaLen     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblListSel   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId graphLine    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId graphBar     = SDL::ECS::NullEntity;

	SDL::ECS::EntityId lblCombo     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblInputVal  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId badgeWidget  = SDL::ECS::NullEntity;
	int m_badgeCount = 0;

	SDL::ECS::EntityId colorPickerRgb  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblPickedColor  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId treeWidget      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId lblTreeSel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId popupWidget     = SDL::ECS::NullEntity;

	// ── Animation state ───────────────────────────────────────────────────────

	float m_animProgress = 0.f;
	bool  m_animRunning  = true;
	float m_animSpeed    = 0.25f;
	float m_canvasAngle  = 0.f;
	float m_graphTimer   = 0.f;

	// ── Constructor ───────────────────────────────────────────────────────────

	Main() {
		window.StartTextInput();
		_LoadResources();
		timer.Begin();
		_BuildUI();
	}
	~Main() { uiPool.Release(); }

	// ── Resource loading ──────────────────────────────────────────────────────

	void _LoadResources() {
		const std::string base = std::string(SDL::GetBasePath()) + "../../../assets/";
		ui.LoadFont   (key::FONT,      base + "fonts/DejaVuSans.ttf");
		ui.LoadTexture(key::CRATE,     base + "textures/crate.jpg");
		ui.LoadAudio  (key::CLICK,     base + "sounds/effect-click.mp3");
		ui.LoadAudio  (key::HOVER,     base + "sounds/effect-hover.mp3");
		ui.LoadAudio  (key::MENU_OPEN, base + "sounds/effect-menu-open.mp3");
		ui.SetDefaultFont(key::FONT, 15.f);
	}

	// ── Page switching ────────────────────────────────────────────────────────

	void SwitchPage(int idx) {
		if (idx == currentPage) return;
		currentPage = idx;
		for (int i = 0; i < kPageCount; ++i) {
			ui.SetVisible(pages[i], i == currentPage);
			if (auto* bg = ecs_context.Get<SDL::UI::BackgroundStyle>(tabBtns[i])) {
				bg->color        = (i == currentPage) ? pal::TAB_ON  : pal::TAB_OFF;
				bg->hoveredColor = (i == currentPage)
					? SDL::Color{75,135,220,255} : SDL::Color{42,45,62,255};
			}
		}
	}

	// ── Frame loop ────────────────────────────────────────────────────────────

	SDL::AppResult Iterate() {
		timer.Begin();
		const float dt = timer.GetDelta();
		m_canvasAngle += dt * 90.f;
		m_graphTimer  += dt;

		if (m_animRunning) {
			m_animProgress += dt * m_animSpeed;
			if (m_animProgress > 1.f) m_animProgress -= 1.f;
		}
		ui.SetValue(progAnimated, m_animProgress);
		ui.SetText(lblProgress, std::format("{:.0f}%", m_animProgress * 100.f));

		if (ecs_context.IsAlive(graphLine)) {
			constexpr int N = 128;
			std::vector<float> data(N);
			for (int i = 0; i < N; ++i) {
				float x = (float)i / (float)(N - 1) * 4.f * std::numbers::pi_v<float>;
				data[i] = std::sin(x + m_graphTimer * 2.f) * 0.5f
				        + std::sin(x * 2.f + m_graphTimer) * 0.3f;
			}
			ui.SetGraphData(graphLine, std::move(data));
		}

		if (ecs_context.IsAlive(graphBar)) {
			constexpr int N = 32;
			std::vector<float> bars(N);
			for (int i = 0; i < N; ++i) {
				float f = (float)(i + 1) / (float)N;
				bars[i] = std::abs(std::sin(f * 6.f + m_graphTimer * 1.5f)) * (1.f - f * 0.4f);
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
			ecs_context.Each<SDL::UI::Widget>(
				[&](SDL::ECS::EntityId eid, SDL::UI::Widget& w) {
					if (w.Is(SDL::UI::WidgetStateFlag::Hovered)) {
						if (auto* t = ecs_context.Get<SDL::UI::Tag>(eid)) hov = t->value;
					}
					if (w.Is(SDL::UI::WidgetStateFlag::Focused)) {
						if (auto* t = ecs_context.Get<SDL::UI::Tag>(eid)) foc = t->value;
					}
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

		ui.Update(dt);

		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();
		ui.Render();
		renderer.Present();
		timer.End();
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

	// ═══════════════════════════════════════════════════════════════════════════
	// Layout helpers
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::UI::ContainerBuilder _Page(std::string_view n = "") {
		auto &col = ui.Column(12.f, 0.f)
			.H(SDL::UI::Value::Grow(100.f))
			.BgColor(pal::BG).Borders(0.f).Radius(0.f)
			.SetAutoScrollable(true);
		if (!n.empty()) col.SetTag(n);
		return col;
	}

	SDL::UI::ContainerBuilder _TwoColRow(std::string_view n = "") {
		auto &row = ui.Row(12.f, 0.f)
			.H(SDL::UI::Value::Grow(100.f))
			.BgColor({0,0,0,0}).Borders(0.f).Radius(0.f)
			.AlignH(SDL::UI::Align::Left);
		if (!n.empty()) row.SetTag(n);
		return row;
	}

	SDL::UI::ContainerBuilder _LeftCol(std::string_view n = "") {
		auto& col = ui.Column(12.f, 0.f)
			.W(SDL::UI::Value::Pcw(50.f) - 6.f)
			.BgColor({0,0,0,0}).Borders(0.f).Radius(0.f);
		if (!n.empty()) col.SetTag(n);
		return col;
		
	}

	SDL::UI::ContainerBuilder _RightCol(std::string_view n = "") {
		auto& col = ui.Column(12.f, 0.f)
			.W(SDL::UI::Value::Pcw(50.f) - 6.f)
			.BgColor({0,0,0,0}).Borders(0.f).Radius(0.f);
		if (!n.empty()) col.SetTag(n);
		return col;
	}

	// ── Primary-button styling helper ─────────────────────────────────────────
	template <typename B>
	B& _PrimaryBtn(B& b, SDL::Color c) {
		b.BgColor(c)
		 .BgHoveredColor({SDL::Clamp8(c.r+20), SDL::Clamp8(c.g+20), SDL::Clamp8(c.b+20), 255})
		 .BgPressedColor({SDL::Clamp8(c.r*0.75f), SDL::Clamp8(c.g*0.75f), SDL::Clamp8(c.b*0.75f), 255})
		 .BdColor(c)
		 .Radius(5.f).Borders(1.f);
		return b;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// UI root
	// ═══════════════════════════════════════════════════════════════════════════

	void _BuildUI() {
		auto root = ui.Column(0.f, 0.f)
			.BgColor(pal::BG).Borders(0.f).Radius(0.f)
			.Children(_BuildHeader(), _BuildContent())
			.AsRoot();
		_BuildPopupOverlay(root);
	}

	// ── Header (title + tab buttons) ──────────────────────────────────────────

	SDL::ECS::EntityId _BuildHeader() {
		auto header = ui.Row(8.f, 0.f)
			.SetTag("header")
			.W(SDL::UI::Value::Ww(100.f)).H(52.f)
			.PaddingH(12.f).PaddingV(2.f)
			.BgColor(pal::HEADER).BdColor(pal::BORDER).Borders(1.f).Radius(0.f);

		header.Child(
			ui.Label("SDL3pp — New UI Showcase")
				.SetTag("title")
				.TextColor(pal::ACCENT)
				.W(SDL::UI::Value::Grow(100.f)).PaddingV(0));

		for (int i = 0; i < kPageCount; ++i) {
			auto btn = ui.Button(kPageNames[i]).SetTag(std::string("tab_") + kPageNames[i])
				.W(SDL::UI::Value::Auto()).H(20.f).MinW(90.f)
				.AlignH(SDL::UI::Align::Center)
				.Padding(SDL::FBox(2.f)).Borders(1.f).Radius(5.f)
				.ClickSound(key::CLICK)
				.Tooltip(std::string("Switch to ") + kPageNames[i])
				.OnClick([this, i]{ SwitchPage(i); });
			_PrimaryBtn(btn, (i == currentPage) ? pal::TAB_ON : pal::TAB_OFF);
			tabBtns[i] = btn.Id();
			header.Child(btn);
		}
		return header;
	}

	// ── Content (page switcher) ───────────────────────────────────────────────

	SDL::ECS::EntityId _BuildContent() {
		auto content = ui.Column(0.f, 0.f)
			.H(SDL::UI::Value::Grow(100.f))
			.PaddingH(16.f).PaddingV(12.f)
			.BgColor(pal::BG).Borders(0.f);

		pages[0] = _BuildBasicsPage();
		pages[1] = _BuildControlsPage();
		pages[2] = _BuildInputScrollPage();
		pages[3] = _BuildImageCanvasPage();
		pages[4] = _BuildTextListsPage();
		pages[5] = _BuildGridPage();
		pages[6] = _BuildNewWidgetsPage();
		pages[7] = _BuildAdvancedPage();

		currentPage = SDL::Clamp(currentPage, 0, kPageCount - 1);
		for (int i = 0; i < kPageCount; ++i) {
			content.Child(pages[i]);
			if (auto* ss = ecs_context.Get<SDL::UI::SoundStyle>(pages[i]))
				ss->showKey = key::MENU_OPEN;
			ui.SetVisible(pages[i], i == currentPage);
		}
		return content;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 1 — Basics: Label, Button, Toggle, Radio, Separator
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildBasicsPage() {
		auto page = _Page("page_basics");

		// ── Labels ────────────────────────────────────────────────────────────
		auto cardLbl = ui.Card();
		auto richLbl = ui.Label(
			"Rich: bold, italic, underline, highlight, color")
			.SpanBold     ( 6, 10)
			.SpanItalic   (12, 18)
			.SpanUnderline(20, 29)
			.SpanHighlight(31, 40, {255,220,50,100})
			.SpanColor    (42, 47, pal::ACCENT);
		cardLbl.Children(
			ui.SectionTitle("Labels"),
			ui.Label("Normal (white)"  ).TextColor(pal::WHITE),
			ui.Label("Accent (blue)"   ).TextColor(pal::ACCENT),
			ui.Label("Success (green)" ).TextColor(pal::GREEN),
			ui.Label("Warning (orange)").TextColor(pal::ORANGE),
			ui.Label("Error (red)"     ).TextColor(pal::RED),
			ui.Label("Info (teal)"     ).TextColor(pal::TEAL),
			ui.Label("Disabled label"  ).Disable(),
			richLbl
		);

		// ── Buttons ───────────────────────────────────────────────────────────
		auto cardBtn = ui.Card();
		auto lblClick = ui.Label("Click a button…").TextColor(pal::GREY);
		auto btnRow = ui.Row(8.f, 0.f).BgColor({0,0,0,0}).Borders(0.f).H(50.f);

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
			std::string tip = std::string("Click: \"") + sp.l + "\"";
			auto btn = ui.Button(sp.l).SetTag(sp.n).W(100.f).H(34.f)
				.AlignH(SDL::UI::Align::Center)
				.ClickSound(key::CLICK).HoverSound(key::HOVER)
				.Tooltip(tip)
				.OnClick([this, txt, lblClick]{ ui.SetText(lblClick, txt); });
			_PrimaryBtn(btn, sp.c);
			btnRow.Child(btn);
		}
		auto btnRow2 = ui.Row(8.f, 0.f).BgColor({0,0,0,0}).Borders(0.f);
		{
			auto ghost = ui.Button("Ghost").W(100.f).H(34.f)
				.AlignH(SDL::UI::Align::Center)
				.BgColor({0,0,0,0}).BgHoveredColor({50,52,72,200})
				.BdColor(pal::BORDER).Borders(1.f).Radius(5.f)
				.Tooltip("Ghost button — transparent until hover")
				.ClickSound(key::CLICK);
			auto dis = ui.Button("Disabled").W(100.f).H(34.f)
				.Radius(5.f).Borders(1.f).Disable()
				.Tooltip("Disabled — cannot click");
			_PrimaryBtn(dis, {80,80,90,255});
			btnRow2.Children(ghost, dis);
		}
		cardBtn.Children(ui.SectionTitle("Buttons"), btnRow, btnRow2, lblClick);

		// ── Toggles ───────────────────────────────────────────────────────────
		auto cardTog = ui.Card();
		tog1 = ui.Toggle("Enable feature A").Tooltip("Toggle feature A");
		tog2 = ui.Toggle("Enable feature B").Tooltip("Toggle feature B");
		tog3 = ui.Toggle("Feature C (starts ON)").Checked()
			.Tooltip("Toggle feature C — starts enabled");
		lblTogStatus = ui.Label("Toggles: A=off B=off C=ON").TextColor(pal::GREY);
		cardTog.Children(
			ui.SectionTitle("Toggle switches"),
			tog1, tog2, tog3,
			ui.Toggle("Disabled toggle").Disable(),
			lblTogStatus
		);

		// ── Radio buttons ─────────────────────────────────────────────────────
		auto cardRad = ui.Card();
		auto lblRadSel = ui.Label("Selected: (none)").TextColor(pal::ACCENT);

		auto mkRadio = [&](std::string_view lbl, std::string_view grp) {
			return ui.Radio(grp, lbl)
				.Tooltip(std::format("Select \"{}\"", lbl))
				.OnToggle([this, lblRadSel, lbl](bool checked){
					if (checked) ui.SetText(lblRadSel, std::format("Selected: {}", lbl));
				});
		};

		cardRad.Children(
			ui.SectionTitle("Radio buttons (groups A & B)"),
			mkRadio("Option A-1","grpA").SetTag("r1"),
			mkRadio("Option A-2","grpA").SetTag("r2"),
			mkRadio("Option A-3","grpA").SetTag("r3"),
			ui.Separator(),
			mkRadio("Choice B-1","grpB").SetTag("r4"),
			mkRadio("Choice B-2","grpB").SetTag("r5"),
			lblRadSel
		);

		auto cols = _TwoColRow("basics_cols");
		cols.Children(
			_LeftCol().Children(cardLbl, cardBtn),
			_RightCol().Children(cardTog, cardRad)
		);
		page.Child(cols);
		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 2 — Controls: Slider, Knob, Progress
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildControlsPage() {
		auto page = _Page("page_controls");

		// ── Sliders (horizontal) ──────────────────────────────────────────────
		auto cardSld = ui.Card();
		cardSld.Child(ui.SectionTitle("Sliders (horizontal)"));

		auto mkSliderRow = [&](const char* lbl,
		                       float mn, float mx, float v,
		                       SDL::Color fill, const char* tip)
		{
			auto vLbl = ui.Label(std::format("{:.2f}", v))
				.W(50.f).TextColor(pal::GREY);
			auto sld = ui.Slider<float>(NVf{v, mn, mx, .01f})
				.W(SDL::UI::Value::Grow(100.f)).FillColor(fill).Tooltip(tip)
				.OnChange<float>([this, vLbl](float val){
					ui.SetText(vLbl, std::format("{:.2f}", val)); });
			cardSld.Child(
				ui.Row(10.f, 0.f)
					.BgColor({0,0,0,0}).Borders(0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Label(lbl).W(110.f).TextColor(pal::WHITE),
						sld, vLbl)
			);
		};
		mkSliderRow("Volume",     0.f,   1.f, .7f,  pal::ACCENT, "Master volume [0–1]");
		mkSliderRow("Speed",      0.f,   5.f, 1.f,  pal::GREEN,  "Playback speed [0–5]");
		mkSliderRow("Opacity",    0.f,   1.f, 1.f,  pal::PURPLE, "Opacity [0–1]");
		mkSliderRow("Temperature",0.f, 100.f, 22.f, pal::ORANGE, "Temperature [0–100]");
		cardSld.Child(
			ui.Row(10.f, 0.f)
				.BgColor({0,0,0,0}).Borders(0.f).AlignH(SDL::UI::Align::Center)
				.Children(
					ui.Label("Disabled").W(110.f),
					ui.Slider<float>(NVf{.3f, 0.f, 1.f, .1f})
						.W(SDL::UI::Value::Grow(100.f)).Disable()
						.Tooltip("Disabled slider"))
		);

		// ── Vertical slider + Knobs ───────────────────────────────────────────
		auto cardKnob = ui.Card();
		cardKnob.Child(ui.SectionTitle("Vertical Slider + Knobs"));
		lblKnob1 = ui.Label("Knob 1: 0.50").TextColor(pal::GREY);
		lblKnob2 = ui.Label("Knob 2: 50.0").TextColor(pal::GREY);
		lblKnob3 = ui.Label("Knob 3: 50.0").TextColor(pal::GREY);

		auto sldV = ui.Slider<float>(NVf{.5f, 0.f, 1.f, .1f},
		                             SDL::UI::Orientation::Vertical)
			.H(120.f).W(24.f).AlignH(SDL::UI::Align::Center).FillColor(pal::ACCENT)
			.Tooltip("Vertical slider [0–1]");

		auto knobRow = ui.Row(16.f, 0.f)
			.BgColor({0,0,0,0}).Borders(0.f).AlignH(SDL::UI::Align::Center)
			.Children(
				sldV,
				ui.Column(4.f, 0.f).BgColor({0,0,0,0}).Borders(0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob<float>(NVf{.5f, 0.f, 1.f, .1f}).SetTag("knob-1")
							.W(64.f).H(64.f).FillColor(pal::ACCENT).ThumbColor(pal::ACCENT)
							.Tooltip("Knob 1 [0–1]")
							.OnChange<float>([this](float v){ ui.SetText(lblKnob1, std::format("Knob 1: {:.2f}", v)); }),
						lblKnob1
					),
				ui.Column(4.f, 0.f).BgColor({0,0,0,0}).Borders(0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob<float>(NVf{50.f, 0.f, 100.f, 1.f}).SetTag("knob-2")
							.W(64.f).H(64.f).FillColor(pal::PURPLE).ThumbColor(pal::PURPLE)
							.Tooltip("Knob 2 [0–100]")
							.OnChange<float>([this](float v){ ui.SetText(lblKnob2, std::format("Knob 2: {:.1f}", v)); }),
						lblKnob2
					),
				ui.Column(4.f, 0.f).BgColor({0,0,0,0}).Borders(0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob<float>(NVf{50.f, 0.f, 100.f, 1.f}, SDL::UI::KnobShape::Potentiometer).SetTag("knob-3")
							.W(64.f).H(64.f).FillColor(pal::GREEN).ThumbColor(pal::GREEN)
							.Tooltip("Knob 3 Potentiometer [0–100]")
							.OnChange<float>([this](float v){ ui.SetText(lblKnob3, std::format("Knob 3: {:.1f}", v)); }),
						lblKnob3
					),
				ui.Column(4.f, 0.f).BgColor({0,0,0,0}).Borders(0.f)
					.AlignH(SDL::UI::Align::Center)
					.Children(
						ui.Knob<float>(NVf{.3f, 0.f, 1.f, .1f}, SDL::UI::KnobShape::Potentiometer).SetTag("knob-dis")
							.W(64.f).H(64.f).Disable().Tooltip("Disabled knob"),
						ui.Label("Disabled").TextColor(pal::GREY)
					)
			);
		cardKnob.Child(knobRow);

		// ── Progress bars ─────────────────────────────────────────────────────
		auto cardProg = ui.Card();
		cardProg.Children(
			ui.SectionTitle("Progress bars"),
			ui.Progress<float>(NVf{.25f, 0.f, 1.f}).FillColor(pal::RED)
				.Tooltip("25% — danger zone"),
			ui.Progress<float>(NVf{.50f, 0.f, 1.f}).FillColor(pal::ORANGE)
				.Tooltip("50% — half way"),
			ui.Progress<float>(NVf{.75f, 0.f, 1.f}).FillColor(pal::GREEN)
				.Tooltip("75% — almost done"),
			ui.Separator()
		);

		progAnimated = ui.Progress<float>(NVf{0.f, 0.f, 1.f})
			.W(SDL::UI::Value::Grow(100.f)).FillColor(pal::ACCENT)
			.Tooltip("Animated progress");
		lblProgress  = ui.Label("0%").W(45.f).TextColor(pal::WHITE);
		auto btnPause = ui.Button("Pause")
			.W(80.f).H(28.f).AlignH(SDL::UI::Align::Center)
			.ClickSound(key::CLICK)
			.Tooltip("Pause/resume the progress animation");
		_PrimaryBtn(btnPause, pal::ORANGE);
		btnPause.OnClick([this, btnPause](){
			m_animRunning = !m_animRunning;
			ui.SetText(btnPause, m_animRunning ? "Pause" : "Resume");
		});
		auto sldSpd = ui.Slider<float>(NVf{m_animSpeed, 0.05f, 2.f, 0.05f})
			.W(SDL::UI::Value::Grow(100.f)).Tooltip("Animation speed")
			.OnChange<float>([this](float v){ m_animSpeed = v; });

		cardProg.Children(
			ui.Row(10.f, 0.f)
				.BgColor({0,0,0,0}).Borders(0.f).AlignH(SDL::UI::Align::Center)
				.Children(progAnimated, lblProgress, btnPause),
			ui.Row(8.f, 0.f)
				.BgColor({0,0,0,0}).Borders(0.f).AlignH(SDL::UI::Align::Center)
				.Children(ui.Label("Speed").W(50.f), sldSpd)
		);

		auto cols = _TwoColRow();
		cols.Children(
			_LeftCol().Children(cardSld, cardKnob),
			_RightCol().Child(cardProg)
		);
		page.Child(cols);
		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 3 — Input & Scroll
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildInputScrollPage() {
		auto page = _Page("page_input_scroll");
		lblEcho = ui.Label("Echo: ").TextColor(pal::ACCENT);

		// ── Text inputs ───────────────────────────────────────────────────────
		auto cardInp = ui.Card();
		cardInp.Children(
			ui.SectionTitle("Text Input (single-line)"),
			ui.Input("Enter your name…").SetTag("input-name")
				.Tooltip("Name field")
				.OnTextChange([this](const std::string& t){
					ui.SetText(lblEcho, "Echo: " + t); }),
			lblEcho,
			ui.Separator(),
			ui.Input("City name…").SetTag("input_city").Tooltip("City"),
			ui.Input("user@example.com").SetTag("input_email").Tooltip("Email"),
			ui.Input("").SetTag("input_pre").SetText("Pre-filled content").Tooltip("Pre-filled"),
			ui.Input("Cannot edit…").Disable().Tooltip("Disabled"),
			ui.Label("← → move  Backspace/Del delete  Ctrl+A select  Esc unfocus")
				.TextColor(pal::GREY)
		);

		// ── ScrollBar (standalone) ────────────────────────────────────────────
		auto cardSB = ui.Card();
		lblSB = ui.Label("Offset: 0").TextColor(pal::GREY);
		sbV = ui.ScrollBar(200.f, 80.f, SDL::UI::Orientation::Vertical)
			.H(120.f).AlignH(SDL::UI::Align::Center)
			.Tooltip("Vertical scrollbar — drag thumb")
			.OnScroll([this](float off){
				ui.SetText(lblSB, std::format("Offset: {:.0f}", off)); });
		auto sbH = ui.ScrollBar(300.f, 100.f, SDL::UI::Orientation::Horizontal)
			.W(SDL::UI::Value::Pw(80.f)).AlignH(SDL::UI::Align::Center)
			.Tooltip("Horizontal scrollbar");

		cardSB.Children(
			ui.SectionTitle("ScrollBar (standalone, V + H)"),
			ui.Row(12.f, 0.f)
				.BgColor({0,0,0,0}).Borders(0.f).AlignH(SDL::UI::Align::Center)
				.Children(sbV,
					ui.Column(6.f, 0.f).BgColor({0,0,0,0}).Borders(0.f)
						.Children(ui.Label("Drag the thumb").TextColor(pal::GREY), lblSB)),
			ui.Separator(),
			ui.Label("Horizontal:").TextColor(pal::WHITE),
			sbH
		);

		// ── ScrollView (container with auto-scroll) ────────────────────────────
		auto cardSV = ui.Card();
		cardSV.Child(ui.SectionTitle("Scrollable container (mouse wheel / drag)"));
		auto svBuilder = ui.ScrollView(6.f).H(180.f).SetAutoScrollable(true).Padding(4.f);
		static const char* kColors[] = {
			"Alice Blue","Aquamarine","Chartreuse","Coral","Crimson",
			"Dark Orchid","Deep Sky Blue","Forest Green","Gold","Hot Pink",
			"Indian Red","Khaki","Lavender","Lime Green","Magenta",
			"Navy Blue","Olive","Orchid","Pale Violet Red","Peru",
			"Royal Blue","Salmon","Sea Green","Sienna","Spring Green"
		};
		for (int i = 0; i < 25; ++i) {
			svBuilder.Child(
				ui.Label(std::format("{:02d}. {} — scroll with mouse wheel", i+1, kColors[i]))
					.TextColor(i%3==0 ? pal::ACCENT : i%3==1 ? pal::GREEN : pal::WHITE)
					.Padding(2.f, 1.f)
			);
		}
		cardSV.Child(svBuilder);

		auto cols = _TwoColRow();
		cols.Children(
			_LeftCol().Child(cardInp),
			_RightCol().Children(cardSB, cardSV)
		);
		page.Child(cols);
		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 4 — Image & Canvas + debug info
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildImageCanvasPage() {
		auto page = _Page("page_img_canvas");

		// ── Image widget ──────────────────────────────────────────────────────
		auto cardImg = ui.Card();
		cardImg.Child(ui.SectionTitle(
			std::format("Image widget — pool key \"{}\"", key::CRATE)));

		auto imgRow = ui.Row(8.f, 0.f)
			.BgColor({0,0,0,0}).Borders(0.f).AlignH(SDL::UI::Align::Left);

		auto mkImg = [&](std::string_view lbl, SDL::UI::ImageFit fit, std::string_view tip){
			auto col = ui.Column(4.f, 0.f).BgColor({0,0,0,0}).Borders(0.f);
			col.Children(
				ui.Image(key::CRATE, fit).W(90.f).H(90.f)
					.Tooltip(tip),
				ui.Label(lbl).TextColor(pal::GREY)
			);
			imgRow.Child(col);
		};
		mkImg("Fill",    SDL::UI::ImageFit::Fill,    "Fill: stretch");
		mkImg("Contain", SDL::UI::ImageFit::Contain, "Contain: preserve ratio");
		mkImg("Cover",   SDL::UI::ImageFit::Cover,   "Cover: fill+crop");
		mkImg("None",    SDL::UI::ImageFit::None,    "None: pixel size");
		mkImg("Tile",    SDL::UI::ImageFit::Tile,    "Tile: repeat");
		cardImg.Children(imgRow,
			ui.Label("Missing key → dark placeholder (no crash)").TextColor(pal::GREY));

		// ── Canvas ────────────────────────────────────────────────────────────
		auto cardCvs = ui.Card();
		cardCvs.Child(ui.SectionTitle("Canvas widget (custom SDL rendering)"));
		auto canvas = ui.Canvas(nullptr, nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect) {
				r.SetDrawColor({30,34,52,255});
				r.RenderFillRect(rect);
				const int N = 64;
				const float cx = rect.x + rect.w * .5f;
				const float cy = rect.y + rect.h * .5f;
				const float R  = std::min(rect.w, rect.h) * .38f;
				for (int i = 0; i < N; ++i) {
					float a  = (float)i / (float)N * 2.f * std::numbers::pi_v<float>
					         + m_canvasAngle * std::numbers::pi_v<float> / 180.f;
					float ri = R * (0.5f + 0.5f * std::sin(a * 3.f));
					float x  = cx + ri * std::cos(a);
					float y  = cy + ri * std::sin(a);
					Uint8 hue = (Uint8)((float)i / (float)N * 255.f);
					SDL::Color c = SDL::UI::HsvToColor((float)hue / 255.f, 1.f, 1.f);
					r.SetDrawColor(c);
					r.RenderFillRect(SDL::FRect{x - 2.f, y - 2.f, 4.f, 4.f});
				}
			})
			.W(SDL::UI::Value::Grow(100.f)).H(160.f)
			.Tooltip("Animated canvas — Lissajous points");
		cardCvs.Child(canvas);

		// ── Debug / pool info ─────────────────────────────────────────────────
		auto cardDbg = ui.Card();
		lblEcsCount = ui.Label("ECS entities: 0").TextColor(pal::GREY);
		lblHovered  = ui.Label("Hovered: (none)").TextColor(pal::GREY);
		lblFocused  = ui.Label("Focused: (none)").TextColor(pal::GREY);
		lblPoolInfo = ui.Label("Pool info").TextColor(pal::GREY);
		cardDbg.Children(
			ui.SectionTitle("Debug info (live)"),
			lblEcsCount, lblHovered, lblFocused, lblPoolInfo
		);

		auto cols = _TwoColRow();
		cols.Children(
			_LeftCol().Children(cardImg),
			_RightCol().Children(cardCvs, cardDbg)
		);
		page.Child(cols);
		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 5 — Text & Lists: TextArea, ListBox, Graph
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildTextListsPage() {
		auto page = _Page("page_text_lists");

		// ── TextArea ──────────────────────────────────────────────────────────
		auto cardTa = ui.Card();
		lblTaLen = ui.Label("0 chars").TextColor(pal::GREY);
		auto ta = ui.TextArea(
			"Hello, SDL3pp!\n"
			"This is a multi-line text area.\n"
			"You can edit, select, copy, paste.\n"
			"TAB inserts 4 spaces.\n"
			"Shift+Enter = newline in some modes.\n\n"
			"Try Rich text spans below...", "")
			.H(SDL::UI::Value::Grow(50.f))
			.Tooltip("Multi-line editable text")
			.OnTextChange([this](const std::string& t){
				ui.SetText(lblTaLen, std::format("{} chars", t.size())); });
		// Add some rich spans to the text area
		ui.AddTextAreaSpan(ta, 7, 13, {.bold=true, .color={100,200,255,255}});
		cardTa.Children(
			ui.SectionTitle("TextArea — multi-line editable"),
			ta, lblTaLen,
			ui.Label("Ctrl+A  select all  •  Ctrl+C/V copy/paste  •  TAB inserts spaces")
				.TextColor(pal::GREY)
		);

		// ── ListBox ───────────────────────────────────────────────────────────
		auto cardLb = ui.Card();
		lblListSel = ui.Label("Selection: (none)").TextColor(pal::ACCENT);
		std::vector<std::string> items = {
			"Apple","Banana","Blueberry","Cherry","Coconut",
			"Date","Elderberry","Fig","Grape","Guava",
			"Honeydew","Jackfruit","Kiwi","Lemon","Lime",
			"Mango","Melon","Nectarine","Orange","Papaya",
			"Peach","Pear","Plum","Raspberry","Strawberry",
			"Tangerine","Watermelon"
		};
		auto lb = ui.ListBoxWidget(items)
			.H(SDL::UI::Value::Grow(100.f))
			.Tooltip("Click to select, scroll with mouse wheel");
		const SDL::ECS::EntityId lbId = lb.Id();
		lb.OnClick([this, lbId](){
			int idx = ui.GetListBoxSelection(lbId);
			auto* lbd = ui.GetListBoxData(lbId);
			if (lbd && idx >= 0 && idx < (int)lbd->itemButtons.size()) {
				auto* ilv = ecs_context.Get<SDL::UI::ItemListView>(lbId);
				if (ilv && idx < (int)ilv->items.size())
					ui.SetText(lblListSel, std::format("Selection [{}]: {}", idx, ilv->items[idx]));
			}
		});

		cardLb.Children(
			ui.SectionTitle("ListBox — V + H auto-scrollbars"),
			lb, lblListSel,
			ui.Label("↑ ↓ navigation  •  Long items → H scrollbar")
				.TextColor(pal::GREY)
		);

		// ── Graphs ────────────────────────────────────────────────────────────
		auto cardGl = ui.Card().H(SDL::UI::Value::Grow(50.f)).MinH(150.f);
		graphLine = ui.GradedGraph()
			.W(SDL::UI::Value::Grow(100.f)).H(SDL::UI::Value::Grow(100.f))
			.Tooltip("Animated sine-wave (line mode)");
		if (auto* gd = ui.GetGraphData(graphLine)) {
			gd->title    = "Sine Wave";
			gd->yLabel   = "amp";
			gd->minVal   = -1.f;
			gd->maxVal   =  1.f;
			gd->showFill = true;
			gd->barMode  = false;
			gd->lineColor = pal::ACCENT;
			gd->fillColor = {70,130,210, 45};
		}
		cardGl.Children(ui.SectionTitle("Graph — line mode (animated)"), graphLine);

		auto cardGb = ui.Card().H(SDL::UI::Value::Grow(50.f)).MinH(150.f);
		graphBar = ui.GradedGraph()
			.W(SDL::UI::Value::Grow(100.f)).H(SDL::UI::Value::Grow(100.f))
			.Tooltip("Animated bar/spectrum (bar mode)");
		if (auto* gd = ui.GetGraphData(graphBar)) {
			gd->title      = "Spectrum";
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

		auto cols = _TwoColRow();
		cols.Children(
			_LeftCol().Children(cardTa, cardLb),
			_RightCol().Children(cardGl, cardGb)
		);
		page.Child(cols);
		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 6 — Grid
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildGridPage() {
		using namespace SDL::UI;
		auto page = _Page("page_grid");

		// ── Auto-placement (4 columns) ────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("4-column grid — auto placement"));
			auto grid = ui.Grid(4, 6.f, 6.f)
				.W(Value::Pcw(100.f))
				.GridColSizing(GridSizing::Fixed)
				.GridRowSizing(GridSizing::Content)
				.GridLineStyle(GridLines::Both)
				.GridLineColor({60, 65, 95, 180})
				.GridLineThickness(1.f)
				.Tooltip("12 cells, auto left-to-right top-to-bottom");

			const SDL::Color cellColors[] = {
				pal::ACCENT, pal::TEAL, pal::GREEN, pal::ORANGE,
				pal::PURPLE, pal::RED,  pal::WHITE, pal::GREY
			};
			for (int i = 0; i < 12; ++i) {
				SDL::Color col = cellColors[i % 8];
				grid.Child(
					ui.Label(std::format("#{}", i))
						.Padding(10.f).TextColor(col)
						.AlignH(Align::Center)
						.BgColor({25,26,38,255}).Radius(3.f).Borders(0.f)
				);
			}
			card.Child(grid);
			page.Child(card);
		}

		// ── Explicit placement + spanning ─────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("4×3 grid — explicit placement and colspan/rowspan"));
			auto grid = ui.Grid(4, 8.f, 8.f)
				.W(Value::Pcw(100.f))
				.GridColSizing(GridSizing::Content)
				.GridRowSizing(GridSizing::Content)
				.GridLineStyle(GridLines::Both)
				.GridLineColor({55, 60, 90, 160})
				.Tooltip("Explicit placement via .Child(e, col, row, colSpan, rowSpan)");

			grid.Child(ui.Button("Col 0,0").Padding(8.f), 0, 0);
			grid.Child(ui.Button("Col 1,0").Padding(8.f), 1, 0);
			grid.Child(
				ui.Label("Span col 2-3 (colSpan=2)")
					.Padding(8.f).AlignH(Align::Center).TextColor(pal::ACCENT)
					.BgColor({30,50,90,200}).Radius(4.f).Borders(0.f),
				2, 0, 2, 1);

			for (int c = 0; c < 4; ++c)
				grid.Child(
					ui.Label(std::format("L1 C{}", c))
						.Padding(8.f).AlignH(Align::Center)
						.BgColor({28,30,44,255}).Borders(0.f),
					c, 1);

			grid.Child(ui.Toggle("Toggle").Padding(8.f), 0, 2);
			grid.Child(ui.Label("Cell 1,2").Padding(8.f).AlignH(Align::Center), 1, 2);
			grid.Child(
				ui.Label("Span\nrows 2-3\n(rowSpan=2)")
					.Padding(8.f).AlignH(Align::Center).TextColor(pal::GREEN)
					.BgColor({20,55,35,210}).Radius(4.f).Borders(0.f),
				2, 2, 1, 2);
			grid.Child(ui.Label("Cell 3,2").Padding(8.f).AlignH(Align::Center), 3, 2);
			grid.Child(ui.Label("Cell 0,3").Padding(8.f).AlignH(Align::Center), 0, 3);
			grid.Child(ui.Label("Cell 1,3").Padding(8.f).AlignH(Align::Center), 1, 3);
			grid.Child(ui.Label("Cell 3,3").Padding(8.f).AlignH(Align::Center), 3, 3);

			card.Child(grid);
			page.Child(card);
		}

		// ── Fixed sizing + row lines ──────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("3-column grid — GridLines::Rows, Fixed sizing"));
			auto grid = ui.Grid(3, 6.f, 6.f)
				.W(Value::Pcw(100.f))
				.GridColSizing(GridSizing::Fixed).GridRowSizing(GridSizing::Fixed)
				.GridLineStyle(GridLines::Rows)
				.GridLineColor({90, 95, 130, 200})
				.GridLineThickness(2.f);

			const char* labels[] = {
				"Name",    "Value",   "Unit",
				"Width",   "1280",    "px",
				"Height",  "720",     "px",
				"FPS",     "60",      "Hz",
				"Opacity", "100",     "%",
			};
			for (int i = 0; i < 15; ++i) {
				SDL::Color tc = (i < 3) ? pal::ACCENT : pal::WHITE;
				grid.Child(
					ui.Label(labels[i])
						.Padding(6.f).TextColor(tc)
						.BgColor(i < 3 ? SDL::Color{22,28,48,255} : SDL::Color{18,18,26,255})
						.Borders(0.f)
				);
			}
			card.Child(grid);
			page.Child(card);
		}

		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 7 — New Widgets: ComboBox, InputValue, TabView, Expander, Splitter,
	//                       Spinner, Badge
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildNewWidgetsPage() {
		using namespace SDL::UI;
		auto page = _Page("page_new");

		// ── ComboBox + InputValue ─────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("ComboBox & InputValue"));

			lblCombo = ui.Label("Selected: (none)").TextColor(pal::GREY);

			const std::vector<std::string> itemsName = {"Apple","Banana","Cherry","Date","Elderberry","Fig","Grape","Honeydew"};
			auto combo = ui.ComboBox(itemsName)
				.W(Value::Pcw(100.f)).H(32.f)
				.Tooltip("Click to open dropdown")
				.OnChange<int>([this, itemsName](int idx){
					if (idx >= 0 && idx < (int)itemsName.size())
						ui.SetText(lblCombo, std::string("Selected: ") + itemsName[idx]);
				});

			lblInputVal = ui.Label("Value: 42").TextColor(pal::GREY);
			auto inputI = ui.InputValue<int>(NVi{42, 0, 100, 1})
				.W(Value::Pcw(100.f)).H(32.f)
				.Tooltip("Integer input — drag or +/– buttons")
				.OnChange<int>([this](int v){
					ui.SetText(lblInputVal, std::format("Value: {}", v)); });
			auto inputF = ui.InputValue<float>(NVf{0.5f, 0.f, 1.f, 0.1f})
				.W(Value::Pcw(100.f)).H(32.f)
				.Tooltip("Float input — drag or +/– buttons")
				.OnChange<float>([this](float v){
					ui.SetText(lblInputVal, std::format("Value: {:.3f}", v)); });

			card.Children(
				combo, lblCombo,
				ui.Separator(),
				inputI, lblInputVal,
				inputF
			);
			page.Child(card);
		}

		// ── Spinner + Badge ───────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("Spinner (animated) & Badge"));

			auto spinner = ui.Spinner(4.f)
				.W(48.f).H(48.f)
				.Tooltip("Animated loading indicator");

			badgeWidget = ui.Badge("0")
				.W(Value::Auto()).H(24.f)
				.Tooltip("Notification counter");

			auto btnInc = ui.Button("+ Increment badge")
				.W(Value::Pcw(100.f)).H(32.f)
				.OnClick([this]{
					++m_badgeCount;
					if (auto* bd = ecs_context.Get<SDL::UI::BadgeData>(badgeWidget))
						bd->text = std::to_string(m_badgeCount);
				});
			auto btnReset = ui.Button("Reset badge")
				.W(Value::Pcw(100.f)).H(32.f)
				.OnClick([this]{
					m_badgeCount = 0;
					if (auto* bd = ecs_context.Get<SDL::UI::BadgeData>(badgeWidget))
						bd->text = "0";
				});

			card.Children(
				ui.Row(8.f, 0.f).H(60.f)
					.BgColor({0,0,0,0}).Borders(0.f)
					.Children(spinner, badgeWidget),
				btnInc, btnReset
			);
			page.Child(card);
		}

		// ── Expander ──────────────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("Expander (collapsible section)"));

			auto exp1 = ui.Expander("Advanced Settings", false)
				.W(Value::Pcw(100.f))
				.Tooltip("Click header to expand/collapse");
			ui.Column(6.f, 4.f)
				.BgColor({25,26,38,200}).Borders(0.f)
				.Children(
					ui.Label("Setting A:  enabled").TextColor(pal::WHITE),
					ui.Label("Setting B:  42 px").TextColor(pal::WHITE),
					ui.Toggle("Option C")
				)
				.AttachTo(exp1.GetContentEntity());

			auto exp2 = ui.Expander("Developer Info", true)
				.W(Value::Pcw(100.f));
			ui.Column(4.f, 4.f)
				.BgColor({25,28,40,200}).Borders(0.f)
				.Children(
					ui.Label("SDK: SDL3pp").TextColor(pal::GREY),
					ui.Label("UI: ECS-backed retained-mode").TextColor(pal::GREY)
				)
				.AttachTo(exp2.GetContentEntity());

			card.Children(exp1, exp2);
			page.Child(card);
		}

		// ── TabView ───────────────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("TabView (tabbed container)"));

			auto tv = ui.TabView()
				.W(Value::Pcw(100.f)).H(140.f)
				.AddTab("Overview").AddTab("Details").AddTab("Settings")
				.Tooltip("Click tabs to switch content");

			ui.Column(6.f, 8.f)
				.BgColor({0,0,0,0}).Borders(0.f)
				.Children(
					ui.Label("Welcome to TabView!").TextColor(pal::ACCENT),
					ui.Label("Each tab shows different content.")
				)
				.AttachTo(tv.GetTabContent(0));

			ui.Column(6.f, 8.f)
				.BgColor({0,0,0,0}).Borders(0.f)
				.Children(
					ui.Label("Widget: TabView").TextColor(pal::GREEN),
					ui.Label("Children per tab shown/hidden on click.")
				)
				.AttachTo(tv.GetTabContent(1));

			ui.Column(6.f, 8.f)
				.BgColor({0,0,0,0}).Borders(0.f)
				.Children(
					ui.Slider<float>(NVf{0.5f, 0.f, 1.f, 0.01f})
						.W(Value::Pcw(100.f)),
					ui.Toggle("Enable feature")
				)
				.AttachTo(tv.GetTabContent(2));

			card.Child(tv);
			page.Child(card);
		}

		// ── Splitter ──────────────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("Splitter (resizable panes — drag the handle)"));

			auto spl = ui.Splitter(SDL::UI::Orientation::Horizontal, 0.4f)
				.W(Value::Pcw(100.f)).H(120.f)
				.Tooltip("Drag the vertical divider to resize the two panes");

			ui.Column(4.f, 6.f).BgColor({20,22,36,255}).Borders(0.f)
				.Children(
					ui.Label("Left Pane").TextColor(pal::ACCENT),
					ui.Label("Drag handle →").TextColor(pal::GREY)
				)
				.AttachTo(spl);

			ui.Column(4.f, 6.f).BgColor({22,20,36,255}).Borders(0.f)
				.Children(
					ui.Label("Right Pane").TextColor(pal::TEAL),
					ui.Label("← Drag handle").TextColor(pal::GREY)
				)
				.AttachTo(spl);

			card.Child(spl);
			page.Child(card);
		}

		return page;
	}

	// ═══════════════════════════════════════════════════════════════════════════
	// Page 8 — Advanced: ColorPicker, Tree, Popup, BgGradient, Tag demo
	// ═══════════════════════════════════════════════════════════════════════════

	SDL::ECS::EntityId _BuildAdvancedPage() {
		using namespace SDL::UI;
		auto page = _Page("page_advanced");

		auto cols  = _TwoColRow();
		auto left  = _LeftCol();
		auto right = _RightCol();

		// ── ColorPicker ───────────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("ColorPicker"));

			lblPickedColor = ui.Label("RGB:  70 130 210").TextColor(pal::GREY);

			colorPickerRgb = ui.ColorPicker(ColorPickerPalette::RGB8)
				.SetTag("cp_rgb")
				.W(220.f).H(200.f)
				.PickedColor(pal::ACCENT).AllowAlpha(false)
				.OnColorChange([this](SDL::Color c){
					ui.SetText(lblPickedColor,
						std::format("RGB: {:3d} {:3d} {:3d}", c.r, c.g, c.b));
				})
				.Tooltip("RGB8 picker — drag SV square and hue bar");

			auto cpGray = ui.ColorPicker(ColorPickerPalette::Grayscale)
				.W(Value::Pw(100.f)).H(28.f)
				.Tooltip("Grayscale picker");

			auto cpGrad = ui.ColorPicker(ColorPickerPalette::GradientAB)
				.W(Value::Pw(100.f)).H(28.f)
				.PickerColorA(pal::ACCENT).PickerColorB(pal::GREEN)
				.Tooltip("Gradient A→B picker");

			card.Children(
				colorPickerRgb, lblPickedColor,
				ui.Separator(),
				ui.Label("Grayscale:").TextColor(pal::GREY), cpGray,
				ui.Label("Gradient A→B:").TextColor(pal::GREY), cpGrad
			);
			left.Child(card);
		}

		// ── BgGradient demo ───────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("Background gradients"));

			struct GSpec { const char* n; const char* lbl;
			               SDL::Color c1; SDL::Color c2;
			               GradientAnchor start; GradientAnchor end; };
			static const GSpec kG[] = {
				{"gb_blue",  "Blue ↕",   { 30, 60,150,255}, { 80,160,255,255},
				                          GradientAnchor::Left,       GradientAnchor::Right},
				{"gb_green", "Green ↔",  { 20,110, 50,255}, { 60,200, 90,255},
				                          GradientAnchor::Top,        GradientAnchor::Bottom},
				{"gb_orange","Orange ↕", {150, 65, 10,255}, {240,155, 35,255},
				                          GradientAnchor::TopLeft,    GradientAnchor::BottomRight},
				{"gb_purple","Purple ↕", { 55, 18,110,255}, {150, 75,215,255},
				                          GradientAnchor::BottomLeft, GradientAnchor::TopRight},
			};
			auto btnRow = ui.Row(8.f, 0.f).BgColor({0,0,0,0}).Borders(0.f);
			for (auto& g : kG) {
				SDL::Color c1 = g.c1, c2 = g.c2;
				btnRow.Child(
					ui.Button(g.lbl).SetTag(g.n)
						.W(106.f).H(44.f).AlignH(Align::Center)
						.BgColor(c1)
						.BgHoveredColor({SDL::Clamp8(c1.r+25),SDL::Clamp8(c1.g+25),SDL::Clamp8(c1.b+25),255})
						.BgPressedColor({SDL::Clamp8(c1.r*0.6f),SDL::Clamp8(c1.g*0.6f),SDL::Clamp8(c1.b*0.6f),255})
						.Radius(6.f).Borders(0.f)
						.BgGradient(c2, g.start, g.end)
						.ClickSound(key::CLICK)
				);
			}
			card.Child(btnRow);
			left.Child(card);
		}

		// ── Tag search demo ───────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("Tag-based search (FindByTag)"));

			auto lblTagResult = ui.Label("(no search yet)").SetTag("lbl_tag_res").TextColor(pal::GREY);
			auto btn = ui.Button("FindByTag(\"cp_rgb\")").SetTag("btn_find_tag")
				.W(Value::Pcw(100.f)).H(32.f)
				.Tooltip("Searches for the ColorPicker entity by tag")
				.OnClick([this, lblTagResult]{
					auto found = ui.FindByTag("cp_rgb");
					ui.SetText(lblTagResult,
						found != SDL::ECS::NullEntity
							? std::format("Found entity #{}", (int)found)
							: "Not found!");
				});
			card.Children(
				ui.Label("All user-named widgets are automatically searchable by tag.")
					.TextColor(pal::GREY),
				btn, lblTagResult
			);
			left.Child(card);
		}

		// ── Tree ──────────────────────────────────────────────────────────────
		{
			auto card = ui.Card();
			card.Child(ui.SectionTitle("Tree widget"));

			auto lblSel = ui.Label("Selection: (none)").TextColor(pal::ACCENT);
			lblTreeSel = lblSel.Id();

			treeWidget = ui.Tree()
				.W(Value::Pw(100.f)).H(200.f)
				.TreeNode("src/",             0, true,  true)
				.TreeNode("main.cpp",         1, false, false)
				.TreeNode("SDL3pp_ui/",       1, true,  false)
				.TreeNode("UISystem.h",       2, false, false)
				.TreeNode("UIComponents.h",   2, false, false)
				.TreeNode("UIBuilder.h",      2, false, false)
				.TreeNode("assets/",          0, true,  true)
				.TreeNode("fonts/",           1, true,  false)
				.TreeNode("DejaVuSans.ttf",   2, false, false)
				.TreeNode("textures/",        1, true,  false)
				.TreeNode("crate.jpg",        2, false, false)
				.TreeNode("README.md",        0, false, false)
				.OnSelect([this](int sel){
					ui.SetText(lblTreeSel,
						sel >= 0 ? std::format("Selected node #{}", sel)
						         : "Selection: (none)");
				})
				.Tooltip("Click to select  •  arrow to expand  •  scroll");

			card.Children(treeWidget, lblSel);
			right.Child(card);
		}

		// ── Popup open button ─────────────────────────────────────────────────
		{
			auto card = ui.Card();
			auto openBtn = ui.Button("Open Popup")
				.SetTag("btn_open_popup")
				.W(130.f).H(32.f)
				.ClickSound(key::CLICK)
				.Tooltip("Open the floating popup window")
				.OnClick([this]{ ui.SetPopupOpen(popupWidget, true); });
			_PrimaryBtn(openBtn, pal::ACCENT);
			card.Children(
				ui.SectionTitle("Popup widget"),
				ui.Label("Floating window: drag title bar to move, corner to resize.")
					.TextColor(pal::GREY),
				openBtn
			);
			right.Child(card);
		}

		cols.Children(left, right);
		page.Child(cols);
		return page;
	}

	// ── Popup overlay (Fixed, appended to root) ───────────────────────────────

	void _BuildPopupOverlay(SDL::UI::ContainerBuilder& root) {
		auto popup = ui.Popup("Settings", true, true, true)
			.SetTag("popup1")
			.X(240.f).Y(170.f).W(290.f).H(230.f)
			.PopupOpen(false)
			.Tooltip("Drag title bar to move  •  drag corner to resize");
		popupWidget = popup.Id();

		auto closeBtn = ui.Button("Close")
			.SetTag("pop_close_btn").W(90.f).H(28.f).ClickSound(key::CLICK)
			.OnClick([this]{ ui.SetPopupOpen(popupWidget, false); });
		_PrimaryBtn(closeBtn, pal::RED);

		popup.Children(
			ui.Label("Floating popup window").TextColor(pal::WHITE),
			ui.Slider<float>(NVf{0.5f, 0.f, 1.f, 0.01f})
				.W(SDL::UI::Value::Pw(100.f)).FillColor(pal::ACCENT)
				.Tooltip("Slider inside the popup"),
			ui.Toggle("Enable option").Tooltip("Toggle inside popup"),
			ui.Separator(),
			closeBtn
		);
		root.Child(popup);
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
