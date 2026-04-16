/**
 * @file 06_video_player.cpp
 * @brief SDL3pp VLC-like Video Player
 *
 * Features:
 *  - Open any format supported by FFmpeg (MP4, MKV, AVI, WebM, MP3, FLAC…)
 *  - Video rendering via SDL streaming texture
 *  - Multi-track audio / subtitle selection
 *  - Seek bar (click / drag to seek)
 *  - Volume control with mute toggle
 *  - Loop mode
 *  - Subtitle overlay (text / ASS markup stripped)
 *  - Metadata viewer (title, artist, album, date…)
 *  - Stream information panel
 *  - Collapsible side panel
 *
 * Keyboard shortcuts:
 *  Space        – Play / Pause
 *  S            – Stop
 *  L            – Toggle loop
 *  M            – Mute
 *  F            – Fullscreen toggle
 *  ←  / →      – Seek ±5 s
 *  Ctrl+← / →  – Seek ±60 s
 *  ↑  / ↓      – Volume ±5 %
 *  Ctrl+O       – Open file dialog
 *  Ctrl+Q       – Quit
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_videoFile.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>
#include <vector>

#define VIDEO_PLAYER_VERSION "1.0.0"

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
	constexpr const char* OPEN  = "icon_open";
	constexpr const char* MUTE  = "icon_mute";
	constexpr const char* VOL   = "icon_volume";
	constexpr const char* LOOP  = "icon_loop";
	constexpr const char* FULL  = "icon_fullscreen";
	constexpr const char* PANEL = "icon_panel";
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

// ─────────────────────────────────────────────────────────────────────────────
// Programmatic icon generator (16×16 RGBA surfaces)
// ─────────────────────────────────────────────────────────────────────────────

static SDL::Texture MakeIcon(SDL::RendererRef r, int id) {
	SDL_Surface* s = SDL_CreateSurface(16, 16, SDL_PIXELFORMAT_RGBA32);
	if (!s) return {};

	const SDL_PixelFormatDetails* fmt = SDL_GetPixelFormatDetails(s->format);
	auto px = [&](int x, int y, SDL::Color c) {
		if ((unsigned)x >= 16 || (unsigned)y >= 16) return;
		*(Uint32*)((Uint8*)s->pixels + y * s->pitch + x * 4) =
			SDL_MapRGBA(fmt, nullptr, c.r, c.g, c.b, c.a);
	};
	auto rc = [&](int x, int y, int w, int h, SDL::Color c) {
		for (int dy = 0; dy < h; dy++) for (int dx = 0; dx < w; dx++) px(x+dx, y+dy, c);
	};

	rc(0,0,16,16,{0,0,0,0});
	SDL::Color W = pal::WHITE;

	switch (id) {
	case 0: // PLAY ▶
		for (int y = 0; y < 12; y++) { int w = (y<6)?y/2+1:(12-y)/2+1; rc(2,2+y,w,1,W); }
		break;
	case 1: // PAUSE ⏸
		rc(3,2,3,12,W); rc(10,2,3,12,W);
		break;
	case 2: // STOP ⏹
		rc(2,2,12,12,W);
		break;
	case 3: // PREV ⏮
		rc(2,7,2,2,W);
		for (int y=0;y<10;y++){int w=(y<5)?y+1:10-y; rc(4,3+y,w,1,W);}
		break;
	case 4: // NEXT ⏭
		rc(12,7,2,2,W);
		for (int y=0;y<10;y++){int w=(y<5)?y+1:10-y; rc(10-w,3+y,w,1,W);}
		break;
	case 5: // OPEN (folder)
		rc(1,5,14,9,W); rc(1,3,6,2,W);
		break;
	case 6: // MUTE (crossed speaker)
		rc(1,5,5,6,W);
		for (int i=0;i<5;i++){px(6+i,3+i,W);px(6+i,12-i,W);}
		for (int i=2;i<14;i++){px(i,i,W);px(i,14-i,W);}
		break;
	case 7: // VOLUME (speaker + waves)
		rc(1,5,5,6,W);
		for (int i=0;i<5;i++){px(6+i,3+i,W);px(6+i,12-i,W);}
		px(11,7,W);px(11,8,W);px(12,5,W);px(12,10,W);px(13,4,W);px(13,11,W);
		break;
	case 8: // LOOP
		for (int i=2;i<14;i++){px(i,2,W);px(i,13,W);}
		for (int i=2;i<14;i++){px(2,i,W);px(13,i,W);}
		rc(6,0,4,3,W);
		break;
	case 9: // FULLSCREEN (corner brackets)
		rc(1,1,4,1,W);rc(1,1,1,4,W);rc(11,1,4,1,W);rc(14,1,1,4,W);
		rc(1,11,1,4,W);rc(1,14,4,1,W);rc(14,11,1,4,W);rc(11,14,4,1,W);
		break;
	case 10: // PANEL (sidebar icon)
		rc(1,1,5,14,W); rc(7,1,8,1,W);rc(7,5,8,1,W);rc(7,9,8,1,W);rc(7,13,8,1,W);
		break;
	default:
		rc(1,1,14,14,W);
	}

	SDL_Texture* tex = SDL_CreateTextureFromSurface(r, s);
	SDL_DestroySurface(s);
	return SDL::Texture(tex);
}

static void LoadIcons(SDL::RendererRef r, SDL::ResourcePool& pool) {
	pool.Add<SDL::Texture>(icon_key::PLAY,  MakeIcon(r, 0));
	pool.Add<SDL::Texture>(icon_key::PAUSE, MakeIcon(r, 1));
	pool.Add<SDL::Texture>(icon_key::STOP,  MakeIcon(r, 2));
	pool.Add<SDL::Texture>(icon_key::PREV,  MakeIcon(r, 3));
	pool.Add<SDL::Texture>(icon_key::NEXT,  MakeIcon(r, 4));
	pool.Add<SDL::Texture>(icon_key::OPEN,  MakeIcon(r, 5));
	pool.Add<SDL::Texture>(icon_key::MUTE,  MakeIcon(r, 6));
	pool.Add<SDL::Texture>(icon_key::VOL,   MakeIcon(r, 7));
	pool.Add<SDL::Texture>(icon_key::LOOP,  MakeIcon(r, 8));
	pool.Add<SDL::Texture>(icon_key::FULL,  MakeIcon(r, 9));
	pool.Add<SDL::Texture>(icon_key::PANEL, MakeIcon(r,10));
}

// =============================================================================
// Main application
// =============================================================================

struct Main {
	static constexpr SDL::Point kWinSz      = {1280, 760};
	static constexpr int        kSidePanelW = 290;
	static constexpr float      kTopBarH    = 42.f;
	static constexpr float      kSeekH      = 30.f;
	static constexpr float      kCtrlH      = 54.f;
	static constexpr float      kStatusH    = 22.f;

	// ── SDL objects ───────────────────────────────────────────────────────────

	static SDL::Window MakeWindow() {
		return SDL::CreateWindowAndRenderer(
			"SDL3pp Video Player " VIDEO_PLAYER_VERSION,
			kWinSz, SDL::WINDOW_RESIZABLE, nullptr);
	}

	SDL::MixerRef    mixer   { SDL::CreateMixerDevice(
		SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK,
		SDL::AudioSpec{SDL::AUDIO_F32, 2, 48000}) };
	SDL::Window      window  { MakeWindow()         };
	SDL::RendererRef renderer{ window.GetRenderer() };

	SDL::ResourceManager resources;
	SDL::ResourcePool&   pool_ui { *resources.CreatePool(pool_key::UI) };

	SDL::ECS::World  world;
	SDL::UI::System  ui { world, renderer, mixer, pool_ui };
	SDL::FrameTimer  frameTimer { 60.f };

	// ── Video player ──────────────────────────────────────────────────────────

	SDL::Video::VideoPlayer player;

	// ── UI entity IDs ─────────────────────────────────────────────────────────

	SDL::ECS::EntityId eVideoCanvas    = SDL::ECS::NullEntity;
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
	bool        showSidePanel = true;
	bool        fullscreen    = false;

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::SetLogPriorities(SDL::LOG_PRIORITY_WARN);
		for (auto arg : args) {
			if (arg == "--verbose") SDL::SetLogPriorities(SDL::LOG_PRIORITY_VERBOSE);
			if (arg == "--debug")   SDL::SetLogPriorities(SDL::LOG_PRIORITY_DEBUG);
		}
		SDL::SetAppMetadata("SDL3pp Video Player", VIDEO_PLAYER_VERSION,
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

		if (ev.type == SDL::EVENT_KEY_DOWN) {
			const auto key  = ev.key.key;
			const bool ctrl = (ev.key.mod & SDL::KMOD_CTRL) != 0;

			if (ctrl && key == SDL::KEYCODE_Q)  return SDL::APP_SUCCESS;
			if (ctrl && key == SDL::KEYCODE_O)  { _ShowOpenDialog(); return SDL::APP_CONTINUE; }

			if (key == SDL::KEYCODE_SPACE)  { player.TogglePlayPause(); _RefreshPlayBtn();   return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_S)      { player.Stop();            _RefreshPlayBtn();   return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_L)      { player.SetLoop(!player.IsLooping()); _RefreshLoopBtn(); return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_M)      { player.ToggleMute();      _RefreshMuteBtn();   return SDL::APP_CONTINUE; }
			if (key == SDL::KEYCODE_F)      { _ToggleFullscreen();                           return SDL::APP_CONTINUE; }

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
			using PS = SDL::Video::PlaybackState;
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
		ui.Iterate(dt);
		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();
		ui.Iterate(0.f);
		renderer.Present();

		return SDL::APP_CONTINUE;
	}

private:
	// ─────────────────────────────────────────────────────────────────────────
	// Resource loading
	// ─────────────────────────────────────────────────────────────────────────

	void _LoadResources() {
		std::string assetsPath = std::string(SDL::GetBasePath()) + "../../../assets/";
		ui.LoadFont(res_key::FONT, assetsPath + "fonts/Roboto-Regular.ttf");
		ui.SetDefaultFont(res_key::FONT, 14.f);
		LoadIcons(renderer, pool_ui);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// UI construction
	// ─────────────────────────────────────────────────────────────────────────

	void _BuildUI() {
		using namespace SDL::UI;

		// ── Shared style helpers ──────────────────────────────────────────────

		auto makeIconBtn = [&](const std::string& name,
								const std::string& iconK,
								const std::string& label = "") -> Builder {
			return ui.Button(name, label)
				.W(label.empty() ? Value::Px(32.f) : Value::Auto())
				.H(32.f)
				.BgColor(pal::NEUTRAL)
				.BgHover({45,45,65,255})
				.BgPress({25,25,40,255})
				.BorderColor(pal::BORDER)
				.BorderLeft(1).BorderRight(1).BorderTop(1).BorderBottom(1)
				.Radius(SDL::FCorners(6.f))
				.TextColor(pal::WHITE)
				.FontKey(res_key::FONT, 13.f)
				.Icon(iconK, 4.f)
				.PaddingH(label.empty() ? 4.f : 8.f);
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
			  .Grow(1.f)
			  .AlignH(Align::Start);

		auto topBar =
			ui.Row("topBar", 6.f, 8.f)
				.H(kTopBarH)
				.BgColor(pal::HEADER)
				.BorderBottom(1).BorderColor(pal::BORDER)
				.AlignChildrenV(Align::Center)
				.Children(
					makeIconBtn("btnOpen", icon_key::OPEN, " Ouvrir")
					.OnClick([this]{ _ShowOpenDialog(); }),
					ui.Sep("topSep").W(1.f).H(20.f).BgColor(pal::BORDER),
					eTitleLabel,
					makeIconBtn("btnFullTop", icon_key::FULL)
					.OnClick([this]{ _ToggleFullscreen(); }),
					makeIconBtn("btnPanelTop", icon_key::PANEL)
					.OnClick([this]{ _ToggleSidePanel(); })
				);

		// ── Video canvas + subtitle overlay ───────────────────────────────────

		eSubtitleLabel =
			ui.Label("subtitleLabel", "")
			  .BgColor({0,0,0,180})
			  .TextColor({255,255,210,255})
			  .FontKey(res_key::FONT, 16.f)
			  .Radius(SDL::FCorners(4.f))
			  .PaddingH(14.f).PaddingV(6.f)
			  .AlignH(Align::Center)
			  .X(Value::Pw(0.5f, 0.f))
			  .Y(Value::Ph(1.f, -54.f))
			  .Attach(AttachLayout::Absolute);
		ui.SetVisible(eSubtitleLabel, false);

		eVideoCanvas =
			ui.CanvasWidget("videoCanvas", nullptr, nullptr,
				[this](SDL::RendererRef r, SDL::FRect rect) {
					_DrawVideoCanvas(r, rect);
				})
			  .Grow(1.f)
			  .BgColor(pal::BG)
			  .Child(eSubtitleLabel);

		// ── Seek bar ──────────────────────────────────────────────────────────

		eSeekSlider =
			ui.Slider("seekSlider", 0.f, 1.f, 0.f, Orientation::Horizontal)
			  .Grow(1.f)
			  .H(kSeekH)
			  .BgColor({30,30,46,255})
			  .WithStyle([](Style& s){
				  s.track = {40,40,58,255};
				  s.fill  = {70,130,210,255};
				  s.thumb = {100,160,235,255};
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
			  .FontKey(res_key::FONT, 12.f)
			  .PaddingH(8.f);

		auto seekRow =
			ui.Row("seekRow", 0.f, 4.f)
			  .H(kSeekH + 8.f)
			  .BgColor(pal::PANEL)
			  .AlignChildrenV(Align::Center)
			  .Children(eSeekSlider, eTimeLabel);

		// ── Control bar ───────────────────────────────────────────────────────

		ePlayBtn =
			ui.Button("btnPlay", "")
			  .W(42.f).H(38.f)
			  .BgColor(pal::ACCENT)
			  .BgHover({90,150,230,255})
			  .BgPress({50,110,190,255})
			  .Radius(SDL::FCorners(8.f))
			  .Icon(icon_key::PLAY, 4.f)
			  .OnClick([this]{ player.TogglePlayPause(); _RefreshPlayBtn(); });

		eMuteBtn =
			makeIconBtn("btnMute", icon_key::VOL)
			  .H(32.f).W(32.f)
			  .OnClick([this]{ player.ToggleMute(); _RefreshMuteBtn(); });

		eVolumeSlider =
			ui.Slider("volSlider", 0.f, 1.f, 1.f, Orientation::Horizontal)
			  .W(90.f).H(20.f)
			  .WithStyle([](Style& s){
				  s.bgColor = {30,30,46,255};
				  s.track   = {40,40,58,255};
				  s.fill    = {70,130,210,255};
				  s.thumb   = {100,160,235,255};
			  })
			  .Radius(SDL::FCorners(3.f))
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
			  .FontKey(res_key::FONT, 11.f)
			  .W(32.f);

		eLoopBtn =
			makeIconBtn("btnLoop", icon_key::LOOP)
			  .H(32.f).W(32.f)
			  .OnClick([this]{ player.SetLoop(!player.IsLooping()); _RefreshLoopBtn(); });

		auto ctrlBar =
			ui.Row("ctrlBar", 6.f, 10.f)
				.H(kCtrlH)
				.BgColor(pal::HEADER)
				.BorderTop(1).BorderColor(pal::BORDER)
				.AlignChildrenV(Align::Center)
				.Children(
					makeIconBtn("btnPrev", icon_key::PREV).H(36.f).W(36.f)
					.OnClick([this]{ player.SeekRelative(-10.0); }),
					ePlayBtn,
					makeIconBtn("btnStop", icon_key::STOP).H(36.f).W(36.f)
					.OnClick([this]{ player.Stop(); _RefreshPlayBtn(); }),
					makeIconBtn("btnNext", icon_key::NEXT).H(36.f).W(36.f)
					.OnClick([this]{ player.SeekRelative(+10.0); }),
					// spacer via growing container
					ui.Container("ctrlSpacer1").W(4.f).H(1.f).BgColor(pal::TRANSP),
					eMuteBtn,
					eVolumeSlider,
					eVolPctLabel,
					// push remaining buttons to the right
					ui.Container("ctrlSpacerR").Grow(1.f).BgColor(pal::TRANSP),
					eLoopBtn,
					makeIconBtn("btnFullCtrl", icon_key::FULL).H(32.f).W(32.f)
					.OnClick([this]{ _ToggleFullscreen(); })
				);

		// ── Status bar ────────────────────────────────────────────────────────

		eStatusBar =
			ui.Label("statusBar", "Ouvrez un fichier (Ctrl+O)")
			  .H(kStatusH)
			  .Grow(1.f)
			  .BgColor({10,10,16,255})
			  .TextColor({90,95,120,255})
			  .FontKey(res_key::FONT, 11.f)
			  .PaddingH(10.f)
			  .AlignV(Align::Center);

		// ── Main column ───────────────────────────────────────────────────────

		auto mainCol =
			ui.Column("mainCol", 0.f, 0.f)
			  .Grow(1.f)
			  .Children(topBar, eVideoCanvas, seekRow, ctrlBar, eStatusBar);

		// ── Side panel ────────────────────────────────────────────────────────

		_BuildSidePanel();

		// ── Root ──────────────────────────────────────────────────────────────

		ui.Row("root", 0.f, 0.f)
		  .W(Value::Ww(1.f))
		  .H(Value::Wh(1.f))
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
			  .Grow(1.f)
			  .BgColor({18,18,30,255})
			  .TextColor({175,178,200,255})
			  .FontKey(res_key::FONT, 11.f)
			  .PaddingH(8.f).PaddingV(6.f);

		auto metaSection =
			ui.Column("metaSection", 0.f, 0.f)
				.H(Value::Auto(160.f))
				.Children(
					ui.Label("metaHdr", "MÉTADONNÉES")
					.Style(sectionHdrStyle).H(20.f).PaddingH(10.f),
					eMetadataArea
				);

		// ── Audio tracks ──────────────────────────────────────────────────────

		eAudioTrackList =
			ui.ListBoxWidget("audioList", {"(aucune piste)"})
			  .H(80.f)
			  .BgColor({18,18,30,255})
			  .TextColor({200,202,220,255})
			  .FontKey(res_key::FONT, 11.f)
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
						.Style(sectionHdrStyle).Grow(1.f),
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
			  .FontKey(res_key::FONT, 11.f)
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
						.Style(sectionHdrStyle).Grow(1.f),
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
			  .FontKey(res_key::FONT, 10.f)
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
					ui.Sep("sp1").H(1.f).BgColor(pal::BORDER),
					audioSection,
					ui.Sep("sp2").H(1.f).BgColor(pal::BORDER),
					subSection,
					ui.Sep("sp3").H(1.f).BgColor(pal::BORDER),
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
		window.SetTitle("SDL3pp Video Player – " + filename);

		// Title label
		SDL::Video::Metadata meta = player.GetMetadata();
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
		bool playing = (player.GetState() == SDL::Video::PlaybackState::Playing);
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

	void _RefreshMetadata(const SDL::Video::Metadata& meta) {
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
	// Video canvas rendering
	// ─────────────────────────────────────────────────────────────────────────

	void _DrawVideoCanvas(SDL::RendererRef r, SDL::FRect rect) {
		// Fill background
		r.SetDrawColor(pal::BG);
		r.RenderFillRect(rect);

		SDL::TextureRef tex = player.GetVideoTexture();
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
		float tw = 0.f, th = 0.f;
		SDL_GetTextureSize(tex, &tw, &th);
		if (tw <= 0.f || th <= 0.f) return;

		float scale = std::min(rect.w / tw, rect.h / th);
		float dw    = tw * scale;
		float dh    = th * scale;
		SDL_FRect dst = {
			rect.x + (rect.w - dw) * 0.5f,
			rect.y + (rect.h - dh) * 0.5f,
			dw, dh
		};
		SDL_RenderTexture(r, tex, nullptr, &dst);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Misc
	// ─────────────────────────────────────────────────────────────────────────

	void _ToggleSidePanel() {
		showSidePanel = !showSidePanel;
		ui.SetVisible(eSidePanel, showSidePanel);
	}

	void _ToggleFullscreen() {
		fullscreen = !fullscreen;
		window.SetFullscreen(fullscreen);
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
