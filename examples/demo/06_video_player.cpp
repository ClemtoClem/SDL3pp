/**
 * @file 06_video_player.cpp
 * @brief SDL3pp VLC-like Media Player
 *
 * Features:
 * - Open any format supported by FFmpeg (MP4, MKV, AVI, WebM, MP3, FLAC…)
 * - Media rendering via SDL streaming texture
 * - Multi-track audio / subtitle selection
 * - Seek bar (click / drag to seek)
 * - Volume control with mute toggle
 * - Loop mode
 * - Subtitle overlay (text / ASS markup stripped)
 * - Metadata viewer (title, artist, album, date…)
 * - Stream information panel
 * - Collapsible side panel
 * - VLC-like immersive fullscreen (Double-click or F)
 *
 * Keyboard shortcuts:
 * Space        – Play / Pause
 * S            – Stop
 * L            – Toggle loop
 * M            – Mute
 * F / Dbl-click– Fullscreen toggle
 * Escape       - Exit fullscreen
 * ←  / →      – Seek ±5 s
 * Ctrl+← / →  – Seek ±60 s
 * ↑  / ↓      – Volume ±5 %
 * Ctrl+O       – Open file dialog
 * Ctrl+Q       – Quit
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_media.h>

#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#define VIDEO_PLAYER_VERSION "1.1.0"

// ─────────────────────────────────────────────────────────────────────────────
// Resource / pool keys
// ─────────────────────────────────────────────────────────────────────────────

namespace pool_key { constexpr const char* UI = "ui"; }
namespace res_key  { constexpr const char* FONT = "font"; }
namespace icon_key {
	constexpr const char* PLAY  = "icon_play";
	constexpr const char* PAUSE = "icon_pause";
	constexpr const char* STOP  = "icon_stop";
	constexpr const char* PREV  = "icon_prev";
	constexpr const char* NEXT  = "icon_next";
	constexpr const char* OPEN  = "icon_folder";
	constexpr const char* MUTE  = "icon_volume_mute";
	constexpr const char* VOL   = "icon_volume_up";
	constexpr const char* LOOP  = "icon_repeat";
	constexpr const char* FULL  = "icon_fullscreen";
	constexpr const char* PANEL = "icon_minimize";
}

// ─────────────────────────────────────────────────────────────────────────────
// Colour palette
// ─────────────────────────────────────────────────────────────────────────────

namespace pal {
	constexpr SDL::Color BG     = {  12,  12,  18, 255};
	constexpr SDL::Color HEADER = {  18,  18,  28, 255};
	constexpr SDL::Color PANEL  = {  16,  16,  24, 255};
	constexpr SDL::Color PANEL2 = {  24,  24,  36, 255};
	constexpr SDL::Color ACCENT = {  70, 130, 210, 255};
	constexpr SDL::Color NEUTRAL= {  30,  30,  44, 255};
	constexpr SDL::Color BORDER = {  50,  52,  72, 255};
	constexpr SDL::Color WHITE  = { 220, 220, 225, 255};
	constexpr SDL::Color GREY   = { 130, 132, 145, 255};
	constexpr SDL::Color GREEN  = {  50, 195, 100, 255};
	constexpr SDL::Color TRANSP = {   0,   0,   0,   0};
}

// ─────────────────────────────────────────────────────────────────────────────
// Utility helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string FormatTime(double s) {
	if (s < 0) return "--:--";
	int sec = (int)s, h = sec / 3600; sec %= 3600;
	int m = sec / 60;                  sec %= 60;
	if (h > 0) return std::format("{:02d}:{:02d}:{:02d}", h, m, sec);
	return std::format("{:02d}:{:02d}", m, sec);
}

/// Strip ASS dialogue header/override tags: returns the bare text.
static std::string StripASS(const std::string& raw) {
	auto pos = raw.rfind(",,");
	std::string text = (pos != std::string::npos) ? raw.substr(pos + 2) : raw;
	std::string out; bool inTag = false;
	for (char c : text) {
		if (c == '{') { inTag = true; continue; }
		if (c == '}') { inTag = false; continue; }
		if (!inTag) out += c;
	}
	return out;
}

// =============================================================================
// Main application
// =============================================================================

struct Main {
	static constexpr SDL::Point kWinSz      = {1280, 760};
	static constexpr int        kSidePanelW = 290;
	static constexpr float      kTopBarH    = 48.f;
	static constexpr float      kSeekH      = 30.f;
	static constexpr float      kCtrlH      = 54.f;
	static constexpr float      kStatusH    = 22.f;

	// ── SDL objects ───────────────────────────────────────────────────────────

	static SDL::Window MakeWindow() {
		return SDL::CreateWindowAndRenderer(
			"SDL3pp - Media Player " VIDEO_PLAYER_VERSION,
			kWinSz, SDL::WINDOW_RESIZABLE, nullptr);
	}

	SDL::MixerRef    mixer   { SDL::CreateMixerDevice(
		SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK,
		SDL::AudioSpec{SDL::AUDIO_F32, 2, 48000}) };
	SDL::Window      window  { MakeWindow()         };
	SDL::RendererRef renderer{ window.GetRenderer() };

	SDL::ResourceManager resources;
	SDL::ResourcePool&   pool_ui { *resources.CreatePool(pool_key::UI) };

	SDL::ECS::Context ecs_context;
	SDL::UI::System   ui { ecs_context, renderer, mixer, pool_ui };
	SDL::FrameTimer   frameTimer { 60.f };

	// ── Media player ──────────────────────────────────────────────────────────

	SDL::Media::MediaPlayer player;

	// ── UI entity IDs ─────────────────────────────────────────────────────────

	SDL::ECS::EntityId eTopBar         = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMediaCanvas    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSeekRow        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eCtrlBar        = SDL::ECS::NullEntity;
	
	SDL::ECS::EntityId eSeekSlider     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTimeLabel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePlayBtn        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eVolumeSlider   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMuteBtn        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eLoopBtn        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTitleLabel     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eStatusBar      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSidePanel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eAudioTrackList = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubTrackList   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMetadataArea   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eInfoLabel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubtitleLabel  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eVolPctLabel    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eAudCountLabel  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubCountLabel  = SDL::ECS::NullEntity;

	// ── App state ─────────────────────────────────────────────────────────────

	std::string pendingOpenPath;
	bool        showSidePanel  = true;
	bool        fullscreen     = false;
	
	// Ergonomie / Plein écran
	float       mouseIdleTimer = 0.f;
	bool        cursorVisible  = true;

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::SetLogPriorities(SDL::LOG_PRIORITY_WARN);
		for (auto arg : args) {
			if (arg == "--verbose") SDL::SetLogPriorities(SDL::LOG_PRIORITY_VERBOSE);
			if (arg == "--debug")   SDL::SetLogPriorities(SDL::LOG_PRIORITY_DEBUG);
		}
		SDL::SetAppMetadata("SDL3pp Media Player", VIDEO_PLAYER_VERSION,
							"com.example.video_player");
		SDL::Init(SDL::INIT_VIDEO | SDL::INIT_AUDIO);
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

	Main() {
		window.StartTextInput();
		player.Init(renderer);
		_LoadResources();
		_BuildUI();
	}

	~Main() {
		player.Shutdown();
		resources.ReleaseAll();
	}

	// ── Events ────────────────────────────────────────────────────────────────

	SDL::AppResult Event(const SDL::Event& ev) {
		if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;

		// Gestion de l'inactivité de la souris pour masquer le curseur
		if (ev.type == SDL::EVENT_MOUSE_MOTION) {
			mouseIdleTimer = 0.f;
			if (!cursorVisible) {
				SDL_ShowCursor();
				cursorVisible = true;
			}
		}

		// Double clic sur la vidéo = mode plein écran immersif
		if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN) {
			if (ev.button.button == SDL_BUTTON_LEFT && ev.button.clicks >= 2) {
				if (ui.IsHovered(eMediaCanvas)) {
					_ToggleFullscreen();
					return SDL::APP_CONTINUE;
				}
			}
		}

		if (ev.type == SDL::EVENT_KEY_DOWN) {
			const auto key  = ev.key.key;
			const bool ctrl = (ev.key.mod & SDL::KMOD_CTRL) != 0;

			if (ctrl && key == SDL::KEYCODE_Q)  return SDL::APP_SUCCESS;
			if (ctrl && key == SDL::KEYCODE_O)  { _ShowOpenDialog(); return SDL::APP_CONTINUE; }

			if (key == SDL::KEYCODE_SPACE)  { player.TogglePlayPause(); _RefreshPlayBtn();   return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_S)      { player.Stop();            _RefreshPlayBtn();   return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_L)      { player.SetLoop(!player.IsLooping()); _RefreshLoopBtn(); return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_M)      { player.ToggleMute();      _RefreshMuteBtn();   return SDL::APP_CONTINUE; }
			
			// Raccourcis Plein Écran
			if (key == SDL::KEYCODE_F)      { _ToggleFullscreen();                           return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_ESCAPE && fullscreen) { _ToggleFullscreen();             return SDL::APP_CONTINUE; }

			if (key == SDL::KEYCODE_RIGHT)  { player.SeekRelative(ctrl ? 60.0 : 5.0);   return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_LEFT)   { player.SeekRelative(ctrl ? -60.0 : -5.0); return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_UP)     { player.SetVolume(player.GetVolume() + 0.05f); _RefreshVolumeSlider(); return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_DOWN)   { player.SetVolume(player.GetVolume() - 0.05f); _RefreshVolumeSlider(); return SDL::APP_CONTINUE; }
		}

		// Consume pending file path (set by dialog callback)
		if (!pendingOpenPath.empty()) {
			_OpenFile(pendingOpenPath);
			pendingOpenPath.clear();
		}

		ui.ProcessEvent(ev);
		return SDL::APP_CONTINUE;
	}

	// ── Iterate ───────────────────────────────────────────────────────────────

	SDL::AppResult Iterate() {
		frameTimer.Begin();
		const float dt = frameTimer.GetDelta();

		// Masquer le curseur de la souris après 2 secondes d'inactivité en plein écran
		if (fullscreen) {
			mouseIdleTimer += dt;
			if (mouseIdleTimer > 2.0f && cursorVisible) {
				SDL_HideCursor();
				cursorVisible = false;
			}
		} else {
			if (!cursorVisible) {
				SDL_ShowCursor();
				cursorVisible = true;
			}
		}

		resources.UpdateAll();
		player.Update(dt);

		// ── Seek bar sync (skip if user is hovering to avoid feedback) ────────
		if (!ui.IsHovered(eSeekSlider)) {
			double t = player.GetCurrentTime();
			double d = player.GetDuration();
			if (d > 0.0) ui.SetValue(eSeekSlider, (float)(t / d));
		}

		// ── Time label ────────────────────────────────────────────────────────
		ui.SetText(eTimeLabel,
			std::format("{} / {}", FormatTime(player.GetCurrentTime()),
								   FormatTime(player.GetDuration())));

		// ── Subtitle overlay ──────────────────────────────────────────────────
		if (eSubtitleLabel != SDL::ECS::NullEntity) {
			std::string sub = player.GetCurrentSubtitle();
			if (!sub.empty() && (sub.find("Dialogue:") == 0 || sub.find(",,") != std::string::npos))
				sub = StripASS(sub);
			ui.SetText(eSubtitleLabel, sub);
			ui.SetVisible(eSubtitleLabel, !sub.empty());
		}

		// ── Status bar ────────────────────────────────────────────────────────
		{
			using PS = SDL::Media::PlaybackState;
			auto info = player.GetPlaybackInfo();
			switch (info.state) {
			case PS::Playing:
			case PS::Paused:
				if (info.width > 0)
					ui.SetText(eStatusBar, std::format(
						"{}×{}  {:.1f} fps  |  {} Hz  {} ch",
						info.width, info.height, info.fps,
						info.audioSampleRate, info.audioChannels));
				break;
			case PS::Idle:
				ui.SetText(eStatusBar, "Ouvrez un fichier (Ctrl+O)");
				break;
			case PS::EndOfFile:
				ui.SetText(eStatusBar, "Lecture terminée");
				break;
			case PS::Error:
				ui.SetText(eStatusBar, "Erreur : " + player.GetLastError());
				break;
			default:
				break;
			}
		}

		// ── Render ────────────────────────────────────────────────────────────
		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();
		ui.Iterate(dt);
		renderer.Present();
		frameTimer.End();

		return SDL::APP_CONTINUE;
	}

private:
	// ─────────────────────────────────────────────────────────────────────────
	// Resource loading
	// ─────────────────────────────────────────────────────────────────────────

	void _LoadResources() {
		const std::string base  = std::string(SDL::GetBasePath()) + "../../../assets/";
		const std::string icons = base + "textures/icons/";

		ui.LoadFont(res_key::FONT, base + "fonts/Roboto-Regular.ttf");
		ui.SetDefaultFont(res_key::FONT, 14.f);

		ui.LoadTexture(icon_key::PLAY,  icons + "icon_play.png");
		ui.LoadTexture(icon_key::PAUSE, icons + "icon_pause.png");
		ui.LoadTexture(icon_key::STOP,  icons + "icon_stop.png");
		ui.LoadTexture(icon_key::PREV,  icons + "icon_prev.png");
		ui.LoadTexture(icon_key::NEXT,  icons + "icon_next.png");
		ui.LoadTexture(icon_key::OPEN,  icons + "icon_folder.png");
		ui.LoadTexture(icon_key::MUTE,  icons + "icon_volume_mute.png");
		ui.LoadTexture(icon_key::VOL,   icons + "icon_volume_up.png");
		ui.LoadTexture(icon_key::LOOP,  icons + "icon_repeat.png");
		ui.LoadTexture(icon_key::FULL,  icons + "icon_fullscreen.png");
		ui.LoadTexture(icon_key::PANEL, icons + "icon_minimize.png");
	}

	// ─────────────────────────────────────────────────────────────────────────
	// UI construction
	// ─────────────────────────────────────────────────────────────────────────

	void _BuildUI() {
		using namespace SDL::UI;

		// ── Shared style helpers ──────────────────────────────────────────────

		auto makeIconBtn = [&](const std::string& name,
								const std::string& iconK,
								const std::string& label = "",
								const std::string& tooltip = "") -> Builder {
			Builder btn = ui.Button(name, label)
				.H(42.f)
				.BgColor(pal::NEUTRAL)
				.BgHover({45,45,65,255})
				.BgPress({25,25,40,255})
				.BorderColor(pal::BORDER)
				.BorderLeft(1).BorderRight(1).BorderTop(1).BorderBottom(1)
				.Radius(SDL::FCorners(6.f))
				.TextColor(pal::WHITE);
			if (label.empty()) {
				btn.Font(res_key::FONT, 13.f)
				.W(Value::Px(42.f))
				.PaddingH(4.f);
			} else {
				btn.W(Value::Auto())
				.PaddingH(8.f);
			}
			if (!iconK.empty()) {
				btn.Icon(iconK, 4.f)
					.IconOpacity(0.65f, 1.f, 0.9f);
			}
			if (!tooltip.empty()) {
				btn.Tooltip(tooltip);
			}
			return btn;
		};

		auto labelStyle = [&]() -> Style {
			Style s;
			s.bgColor   = pal::TRANSP;
			s.textColor = pal::WHITE;
			s.fontKey   = res_key::FONT;
			s.fontSize  = 13.f;
			return s;
		};

		// ── Top bar ───────────────────────────────────────────────────────────

		eTitleLabel =
			ui.Label("titleLabel", "Aucun fichier chargé")
				.Style(labelStyle())
				.GrowW(100.f)
				.AlignH(Align::Start)
				.Font(res_key::FONT, 15.f);

		eTopBar =
			ui.Row("topBar", 6.f, 2.f)
				.H(kTopBarH)
				.GrowW(100.f)
				.BgColor(pal::HEADER)
				.BorderBottom(1).BorderColor(pal::BORDER)
				.AlignChildrenV(Align::Center)
				.Children(
					makeIconBtn("btnOpen", icon_key::OPEN, "", "Ouvrir un fichier média (Ctrl+O)")
					.OnClick([this]{ _ShowOpenDialog(); }),
					ui.Separator("topSep").W(1.f).H(kTopBarH-16.f).BgColor(pal::BORDER),
					eTitleLabel,
					makeIconBtn("btnFullTop", icon_key::FULL, "", "Basculer en mode plein écran")
					.OnClick([this]{ _ToggleFullscreen(); }),
					makeIconBtn("btnPanelTop", icon_key::PANEL, "", "Basculer le panneau latéral")
					.OnClick([this]{ _ToggleSidePanel(); })
				);

		// ── Media canvas + subtitle overlay ───────────────────────────────────

		eSubtitleLabel =
			ui.Label("subtitleLabel", "")
			  .BgColor({0,0,0,180})
			  .TextColor({255,255,210,255})
			  .Font(res_key::FONT, 15.f)
			  .Radius(SDL::FCorners(4.f))
			  .PaddingH(14.f).PaddingV(6.f)
			  .AlignH(Align::Center)
			  .X(Value::Pw(50.f, 0.f))
			  .Y(Value::Ph(100.f, -54.f))
			  .Attach(AttachLayout::Absolute);
		ui.SetVisible(eSubtitleLabel, false);

		eMediaCanvas =
			ui.CanvasWidget("videoCanvas", nullptr, nullptr,
				[this](SDL::RendererRef r, SDL::FRect rect) {
					_DrawMediaCanvas(r, rect);
				})
				.Grow(100.f)
				.BgColor(pal::BG)
				.Child(eSubtitleLabel);

		// ── Seek bar ──────────────────────────────────────────────────────────

		eSeekSlider =
			ui.Slider("seekSlider", 0.f, 1.f, 0.f, Orientation::Horizontal)
				.GrowW(100.f)
				.H(kSeekH)
				.BgColor({30,30,46,255})
				.WithStyle([](Style& s){
					s.trackColor = {40,40,58,255};
					s.fillColor  = {70,130,210,255};
					s.accent = {100,160,235,255};
				})
				.Radius(SDL::FCorners(4.f))
				.OnChange([this](float v) {
					double dur = player.GetDuration();
					if (dur > 0.0) player.Seek((double)v * dur);
				});

		eTimeLabel =
			ui.Label("timeLabel", "--:-- / --:--")
			  .BgColor(pal::TRANSP)
			  .TextColor({160,165,185,255})
			  .Font(res_key::FONT, 12.f)
			  .PaddingH(8.f);

		eSeekRow =
			ui.Row("seekRow", 0.f, 4.f)
			  .H(kSeekH + 8.f)
			  .BgColor(pal::PANEL)
			  .AlignChildrenV(Align::Center)
			  .Children(eSeekSlider, eTimeLabel);

		// ── Control bar ───────────────────────────────────────────────────────

		ePlayBtn =
			makeIconBtn("btnPlay", icon_key::PLAY, "", "Lecture / Pause")
				.BgColor(pal::ACCENT)
				.BgHover({90,150,230,255})
				.BgPress({50,110,190,255})
				.Radius(SDL::FCorners(8.f))
				.OnClick([this]{ player.TogglePlayPause(); _RefreshPlayBtn(); });

		eMuteBtn =
			makeIconBtn("btnMute", icon_key::VOL, "", "Muet / Son activé")
			  .OnClick([this]{ player.ToggleMute(); _RefreshMuteBtn(); });

		eVolumeSlider =
			ui.Slider("volSlider", 0.f, 1.f, 1.f, Orientation::Horizontal)
				.W(90.f).H(20.f)
				.WithStyle([](Style& s){
					s.bgColor = {30,30,46,255};
					s.trackColor   = {40,40,58,255};
					s.fillColor    = {70,130,210,255};
					s.accent   = {100,160,235,255};
				})
				.Radius(SDL::FCorners(3.f))
				.Tooltip("Volume")
				.OnChange([this](float v) {
					player.SetVolume(v);
					_RefreshMuteBtn();
					if (eVolPctLabel != SDL::ECS::NullEntity)
						ui.SetText(eVolPctLabel, std::format("{:.0f}%", v * 100.f));
				});

		eVolPctLabel =
			ui.Label("volPctLabel", "100%")
			  .BgColor(pal::TRANSP)
			  .TextColor(pal::GREY)
			  .Font(res_key::FONT, 11.f)
			  .W(32.f);

		eLoopBtn =
			makeIconBtn("btnLoop", icon_key::LOOP, "", "Mode boucle")
			  .OnClick([this]{ player.SetLoop(!player.IsLooping()); _RefreshLoopBtn(); });

		eCtrlBar =
			ui.Row("ctrlBar", 6.f, 10.f)
				.H(kCtrlH)
				.BgColor(pal::HEADER)
				.BorderTop(1).BorderColor(pal::BORDER)
				.AlignChildrenV(Align::Center)
				.Children(
					makeIconBtn("btnPrev", icon_key::PREV, "", "Reculer de 10 secondes")
						.OnClick([this]{ player.SeekRelative(-10.0); }),
					ePlayBtn,
					makeIconBtn("btnStop", icon_key::STOP, "", "Arrêter")
						.OnClick([this]{ player.Stop(); _RefreshPlayBtn(); }),
					makeIconBtn("btnNext", icon_key::NEXT, "", "Avancer de 10 secondes")
						.OnClick([this]{ player.SeekRelative(+10.0); }),
					// spacer via growing container
					ui.Container("ctrlSpacer1")
						.W(4.f).H(1.f).BgColor(pal::TRANSP),
					eMuteBtn,
					eVolumeSlider,
					eVolPctLabel,
					// push remaining buttons to the right
					ui.Container("ctrlSpacerR")
						.GrowW(100.f).BgColor(pal::TRANSP),
					eLoopBtn,
					makeIconBtn("btnFullCtrl", icon_key::FULL, "", "Plein écran")
						.OnClick([this]{ _ToggleFullscreen(); })
				);

		// ── Status bar ────────────────────────────────────────────────────────

		eStatusBar =
			ui.Label("statusBar", "Ouvrez un fichier (Ctrl+O)")
				.H(kStatusH)
				.GrowW(100.f)
				.BgColor({10,10,16,255})
				.TextColor({90,95,120,255})
				.Font(res_key::FONT, 11.f)
				.PaddingH(10.f)
				.AlignV(Align::Center);

		// ── Main column ───────────────────────────────────────────────────────

		auto mainCol =
			ui.Column("mainCol", 0.f, 0.f)
				.Grow(100.f)
				.Children(eTopBar, eMediaCanvas, eSeekRow, eCtrlBar, eStatusBar);

		// ── Side panel ────────────────────────────────────────────────────────

		_BuildSidePanel();

		// ── Root ──────────────────────────────────────────────────────────────

		ui.Row("root", 0.f, 0.f)
			.Margin(SDL::FBox(0.f))
			.W(Value::Ww(100.f))
			.H(Value::Wh(100.f))
			.BgColor(pal::BG)
			.AlignChildrenV(Align::Stretch)
			.Children(mainCol, eSidePanel)
			.AsRoot();
	}

	void _BuildSidePanel() {
		using namespace SDL::UI;

		Style sectionHdrStyle;
		sectionHdrStyle.bgColor   = pal::PANEL2;
		sectionHdrStyle.textColor = pal::ACCENT;
		sectionHdrStyle.fontKey   = res_key::FONT;
		sectionHdrStyle.fontSize  = 11.f;

		// ── Metadata ──────────────────────────────────────────────────────────

		eMetadataArea =
			ui.TextArea("metadataArea", "(aucune métadonnée)")
				.GrowH(100.f)
				.BgColor({18,18,30,255})
				.TextColor({175,178,200,255})
				.Font(res_key::FONT, 11.f)
				.PaddingH(8.f).PaddingV(6.f);

		auto metaSection =
			ui.Column("metaSection", 0.f, 0.f)
				.H(Value::Auto(160.f))
				.Children(
					ui.Label("metaHdr", "MÉTADONNÉES")
						.Font(res_key::FONT, 11.f)
						.Style(sectionHdrStyle).H(20.f).PaddingH(10.f),
					eMetadataArea
				);

		// ── Audio tracks ──────────────────────────────────────────────────────

		eAudioTrackList =
			ui.ListBoxWidget("audioList", {"(aucune piste)"})
				.H(80.f)
				.BgColor({18,18,30,255})
				.TextColor({200,202,220,255})
				.Font(res_key::FONT, 11.f)
				.OnChange([this](float idx){ _OnAudioTrackSelected((int)idx); });

		eAudCountLabel =
			ui.Label("audCountLbl", "0")
			  	.Style(sectionHdrStyle).W(Value::Auto());

		auto audioSection =
			ui.Column("audioSection", 0.f, 0.f)
				.Children(
					ui.Row("audHdrRow", 4.f, 8.f).H(22.f).BgColor(pal::PANEL2)
					.AlignChildrenV(Align::Center)
					.Children(
						ui.Label("audHdrLbl","PISTES AUDIO")
							.Style(sectionHdrStyle).GrowW(100.f),
						eAudCountLabel
					),
					eAudioTrackList
				);

		// ── Subtitle tracks ───────────────────────────────────────────────────

		eSubTrackList =
			ui.ListBoxWidget("subList", {"(désactivés)"})
				.H(80.f)
				.BgColor({18,18,30,255})
				.TextColor({200,202,220,255})
				.Font(res_key::FONT, 11.f)
				.OnChange([this](float idx){ _OnSubTrackSelected((int)idx); });

		eSubCountLabel =
			ui.Label("subCountLbl", "0")
			  	.Style(sectionHdrStyle).W(Value::Auto());

		auto subSection =
			ui.Column("subSection", 0.f, 0.f)
				.Children(
					ui.Row("subHdrRow", 4.f, 8.f).H(22.f).BgColor(pal::PANEL2)
					.AlignChildrenV(Align::Center)
					.Children(
						ui.Label("subHdrLbl","SOUS-TITRES")
							.Style(sectionHdrStyle).GrowW(100.f),
						eSubCountLabel
					),
					eSubTrackList
				);

		// ── Stream info ───────────────────────────────────────────────────────

		eInfoLabel =
			ui.TextArea("infoLabel", "")
				.H(90.f)
				.BgColor({18,18,30,255})
				.TextColor({140,145,170,255})
				.Font(res_key::FONT, 10.f)
				.PaddingH(8.f).PaddingV(6.f);

		auto infoSection =
			ui.Column("infoSection", 0.f, 0.f)
				.Children(
					ui.Label("infoHdr","INFORMATIONS")
						.Style(sectionHdrStyle).H(20.f).PaddingH(10.f),
					eInfoLabel
				);

		// ── Assemble ──────────────────────────────────────────────────────────

		eSidePanel =
			ui.ScrollView("sidePanel")
				.W((float)kSidePanelW)
				.BgColor(pal::PANEL)
				.BorderLeft(1).BorderColor(pal::BORDER)
				.Children(
					metaSection,
					ui.Separator("sp1").BgColor(pal::BORDER),
					audioSection,
					ui.Separator("sp2").BgColor(pal::BORDER),
					subSection,
					ui.Separator("sp3").BgColor(pal::BORDER),
					infoSection
				);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// File opening
	// ─────────────────────────────────────────────────────────────────────────

	void _ShowOpenDialog() {
		SDL::ShowOpenFileDialog(
			[](void* ud, const char* const* files, int) {
				if (!files || !files[0]) return;
				static_cast<Main*>(ud)->pendingOpenPath = files[0];
			}, this, window);
	}

	void _OpenFile(const std::string& path) {
		player.Stop();

		if (!player.Load(path)) {
			ui.SetText(eTitleLabel, "Erreur d'ouverture");
			ui.SetText(eStatusBar, "Erreur : " + player.GetLastError());
			return;
		}

		// Update window title
		const std::string filename = path.substr(path.rfind('/') + 1);
		window.SetTitle("SDL3pp Media Player – " + filename);

		// Title label
		SDL::Media::Metadata meta = player.GetMetadata();
		std::string title = meta.Title();
		if (title.empty()) title = filename;
		ui.SetText(eTitleLabel, title);

		// Populate side panel
		_RefreshMetadata(meta);
		_RefreshTrackLists();
		_RefreshInfoLabel();

		player.Play();
		_RefreshPlayBtn();
	}

	// ─────────────────────────────────────────────────────────────────────────
	// UI refresh helpers
	// ─────────────────────────────────────────────────────────────────────────

	void _RefreshPlayBtn() {
		bool playing = (player.GetState() == SDL::Media::PlaybackState::Playing);
		ui.SetImageKey(ePlayBtn,
			playing ? icon_key::PAUSE : icon_key::PLAY,
			SDL::UI::ImageFit::Contain);
	}

	void _RefreshMuteBtn() {
		bool muted = player.IsMuted() || player.GetVolume() < 0.01f;
		ui.SetImageKey(eMuteBtn,
			muted ? icon_key::MUTE : icon_key::VOL,
			SDL::UI::ImageFit::Contain);
	}

	void _RefreshLoopBtn() {
		ui.GetStyle(eLoopBtn).bgColor =
			player.IsLooping() ? SDL::Color{40,80,150,255} : pal::NEUTRAL;
	}

	void _RefreshVolumeSlider() {
		ui.SetValue(eVolumeSlider, player.GetVolume());
		if (eVolPctLabel != SDL::ECS::NullEntity)
			ui.SetText(eVolPctLabel,
					   std::format("{:.0f}%", player.GetVolume() * 100.f));
		_RefreshMuteBtn();
	}

	void _RefreshMetadata(const SDL::Media::Metadata& meta) {
		std::string text;
		const std::array<std::pair<const char*, std::string>, 7> common {{
			{"Titre",       meta.Title()  },
			{"Artiste",     meta.Artist() },
			{"Album",       meta.Album()  },
			{"Date",        meta.Date()   },
			{"Genre",       meta.Genre()  },
			{"Piste",       meta.Track()  },
			{"Encodeur",    meta.Get("encoder")},
		}};
		for (auto& [label, val] : common)
			if (!val.empty()) text += std::string(label) + ": " + val + "\n";

		for (auto& [k, v] : meta.tags) {
			bool already = false;
			for (auto& [l, val] : common) if (val == v) { already = true; break; }
			if (!already && !v.empty()) text += k + ": " + v + "\n";
		}
		ui.SetText(eMetadataArea, text.empty() ? "(aucune métadonnée)" : text);
	}

	void _RefreshTrackLists() {
		// Audio tracks
		auto audioTracks = player.GetAudioTracks();
		std::vector<std::string> audioItems;
		for (auto& t : audioTracks) {
			std::string lbl = std::format("#{} {}", t.index,
				t.language.empty() ? t.codecName : t.language);
			if (!t.title.empty()) lbl += " – " + t.title;
			if (t.isDefault) lbl += " ★";
			audioItems.push_back(lbl);
		}
		if (audioItems.empty()) audioItems.push_back("(aucune piste)");
		ui.SetListBoxItems(eAudioTrackList, audioItems);
		if (eAudCountLabel != SDL::ECS::NullEntity)
			ui.SetText(eAudCountLabel, std::to_string(audioTracks.size()));

		// Subtitle tracks
		auto subTracks = player.GetSubtitleTracks();
		std::vector<std::string> subItems = {"(désactivés)"};
		for (auto& t : subTracks) {
			std::string lbl = std::format("#{} {}", t.index,
				t.language.empty() ? t.codecName : t.language);
			if (!t.title.empty()) lbl += " – " + t.title;
			if (t.isForced) lbl += " (forcé)";
			subItems.push_back(lbl);
		}
		ui.SetListBoxItems(eSubTrackList, subItems);
		if (eSubCountLabel != SDL::ECS::NullEntity)
			ui.SetText(eSubCountLabel, std::to_string(subTracks.size()));
	}

	void _RefreshInfoLabel() {
		auto info = player.GetPlaybackInfo();
		std::string text;
		if (info.width > 0)
			text += std::format("Vidéo : {}×{}  {:.2f} fps\n",
								info.width, info.height, info.fps);
		if (info.audioSampleRate > 0)
			text += std::format("Audio : {} Hz  {} canaux\n",
								info.audioSampleRate, info.audioChannels);
		if (info.duration > 0)
			text += "Durée : " + FormatTime(info.duration) + "\n";
		if (!info.filePath.empty()) {
			const std::string& fp = info.filePath;
			text += "Fichier : " + fp.substr(fp.rfind('/') + 1);
		}
		ui.SetText(eInfoLabel, text.empty() ? "(aucune info)" : text);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Track selection callbacks
	// ─────────────────────────────────────────────────────────────────────────

	void _OnAudioTrackSelected(int listIdx) {
		auto tracks = player.GetAudioTracks();
		if (listIdx >= 0 && listIdx < (int)tracks.size())
			player.SetAudioTrack(tracks[listIdx].index);
	}

	void _OnSubTrackSelected(int listIdx) {
		if (listIdx == 0) { player.DisableSubtitles(); return; }
		auto tracks = player.GetSubtitleTracks();
		int  ri     = listIdx - 1;
		if (ri >= 0 && ri < (int)tracks.size())
			player.SetSubtitleTrack(tracks[ri].index);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Media canvas rendering
	// ─────────────────────────────────────────────────────────────────────────

	void _DrawMediaCanvas(SDL::RendererRef r, SDL::FRect rect) {
		// Fill background
		r.SetDrawColor(pal::BG);
		r.RenderFillRect(rect);

		SDL::TextureRef tex = player.GetMediaTexture();
		if (!tex) {
			// Placeholder when no video is loaded
			SDL::FRect ph = {
				rect.x + rect.w * 0.3f, rect.y + rect.h * 0.35f,
				rect.w * 0.4f,          rect.h * 0.30f
			};
			r.SetDrawColor({28,28,44,255});
			r.RenderFillRect(ph);
			return;
		}

		// Letterbox: keep aspect ratio
		SDL::FPoint tsz = SDL::GetTextureSizeFloat(tex);
		if (tsz.x <= 0.f || tsz.y <= 0.f) return;

		float scale = std::min(rect.w / tsz.x, rect.h / tsz.y);
		SDL::FRect dst = {
			rect.x + (rect.w - tsz.x * scale) * 0.5f,
			rect.y + (rect.h - tsz.y * scale) * 0.5f,
			tsz.x * scale, tsz.y * scale
		};
		r.RenderTexture(tex, std::nullopt, dst);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Misc
	// ─────────────────────────────────────────────────────────────────────────

	void _ToggleSidePanel() {
		showSidePanel = !showSidePanel;
		if (!fullscreen) {
			ui.SetVisible(eSidePanel, showSidePanel);
		}
	}

	void _ToggleFullscreen() {
		fullscreen = !fullscreen;
		window.SetFullscreen(fullscreen);

		// En plein écran immersif, on cache tous les éléments d'UI sauf le canvas vidéo
		bool showUI = !fullscreen;
		ui.SetVisible(eTopBar, showUI);
		ui.SetVisible(eSeekRow, showUI);
		ui.SetVisible(eCtrlBar, showUI);
		ui.SetVisible(eStatusBar, showUI);

		if (fullscreen) {
			ui.SetVisible(eSidePanel, false);
		} else {
			// Restaure l'état précédent du panneau latéral en sortant du plein écran
			ui.SetVisible(eSidePanel, showSidePanel);
			
			// Force l'affichage de la souris si elle était cachée
			if (!cursorVisible) {
				SDL_ShowCursor();
				cursorVisible = true;
			}
		}
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
