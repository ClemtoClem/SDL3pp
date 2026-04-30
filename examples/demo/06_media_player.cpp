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
#include <sstream>
#include <string>
#include <vector>

#define VIDEO_PLAYER_VERSION "1.1.0"

// ─────────────────────────────────────────────────────────────────────────────
// Resource / pool keys
// ─────────────────────────────────────────────────────────────────────────────

namespace pool_key { constexpr const char* UI = "ui"; }
namespace res_key  { constexpr const char* FONT = "font"; }
namespace icon_key {
	constexpr const char* PLAY     = "icon_play";
	constexpr const char* PAUSE    = "icon_pause";
	constexpr const char* STOP     = "icon_stop";
	constexpr const char* PREV     = "icon_prev";
	constexpr const char* NEXT     = "icon_next";
	constexpr const char* OPEN     = "icon_folder";
	constexpr const char* MUTE     = "icon_volume_mute";
	constexpr const char* VOL      = "icon_volume_up";
	constexpr const char* LOOP     = "icon_repeat";
	constexpr const char* FULL     = "icon_fullscreen";
	constexpr const char* PANEL    = "icon_flip_h";
	constexpr const char* PLAYLIST = "icon_find";
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
// Playlist
// =============================================================================

struct PlaylistEntry {
	std::string path;
	std::string title;
};

static std::string PlaylistEntryLabel(const PlaylistEntry& e, int idx) {
	std::string name = e.title.empty()
		? e.path.substr(e.path.rfind('/') + 1)
		: e.title;
	return std::format("{:3d}. {}", idx + 1, name);
}

static bool IsMediaFile(const std::string& path) {
	static const std::array<std::string, 18> exts {{
		".mp4", ".mkv", ".avi", ".webm", ".mov", ".flv", ".m4v", ".wmv",
		".mp3", ".flac", ".ogg", ".wav", ".aac", ".m4a", ".opus",
		".ts",  ".mpg", ".mpeg"
	}};
	std::string lo = path;
	for (char& c : lo) c = (char)std::tolower((unsigned char)c);
	for (const auto& e : exts)
		if (lo.size() >= e.size() && lo.substr(lo.size() - e.size()) == e)
			return true;
	return false;
}

// =============================================================================
// Subtitle configuration
// =============================================================================

struct SubtitleConfig {
	float      fontSize  = 18.f;
	SDL::Color textColor = {255, 255, 210, 255};
	SDL::Color bgColor   = {  0,   0,   0, 180};
	Uint32     fontStyle = 0; // TTF_STYLE_NORMAL = 0
	bool       posBottom = true;
	float      marginV   = 50.f;
};

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
	SDL::ECS::EntityId eChapterLabel   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTitleLabel     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eStatusBar      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSidePanel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eAudioTrackList = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubTrackList   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMetadataArea   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eInfoLabel      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eVolPctLabel    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eAudCountLabel  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubCountLabel  = SDL::ECS::NullEntity;

	// ── Subtitle config popup entities ────────────────────────────────────────

	SDL::ECS::EntityId eSubPopup         = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubFontSzBox     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubStyleCombo    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubPosCombo      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubMarginBox     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubTextClrPick   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubBgClrPick     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubBgAlphaSlider = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubBgAlphaLabel  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eSubPreview       = SDL::ECS::NullEntity;

	// ── Playlist popup entities ───────────────────────────────────────────────

	SDL::ECS::EntityId ePlaylistPopup    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePlaylistList     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePlaylistCountLbl = SDL::ECS::NullEntity;

	// ── App state ─────────────────────────────────────────────────────────────

	std::string pendingOpenPath;
	std::string pendingDropPath;  // file dropped via OS drag-and-drop
	bool        showSidePanel  = true;
	bool        fullscreen     = false;

	// Ergonomie / Plein écran
	float       mouseIdleTimer = 0.f;
	bool        cursorVisible  = true;

	// ── Playlist state ────────────────────────────────────────────────────────

	std::vector<PlaylistEntry> m_playlist;
	int                        m_playlistIdx = -1;

	// ── Subtitle config + rendering resources ─────────────────────────────────

	SubtitleConfig subtitleCfg;
	std::string    m_fontPath;
#ifdef SDL3PP_ENABLE_TTF
	std::unique_ptr<SDL::RendererTextEngine> m_subEngine;
	SDL::Font                                m_subFont;
#endif

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::SetLogPriorities(SDL::LOG_PRIORITY_WARN);
		for (auto arg : args) {
			if (arg == "--verbose") SDL::SetLogPriorities(SDL::LOG_PRIORITY_VERBOSE);
			if (arg == "--debug")   SDL::SetLogPriorities(SDL::LOG_PRIORITY_DEBUG);
		}
		SDL::SetAppMetadata("SDL3pp Media Player", VIDEO_PLAYER_VERSION,
							"com.example.media_player");
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
#ifdef SDL3PP_ENABLE_TTF
		m_subEngine = std::make_unique<SDL::RendererTextEngine>(renderer);
		if (!m_fontPath.empty())
			m_subFont = SDL::Font(m_fontPath, subtitleCfg.fontSize);
#endif
	}

	~Main() {
		player.Shutdown();
#ifdef SDL3PP_ENABLE_TTF
		m_subEngine.reset();
#endif
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

		// OS drag-and-drop: add media files to the playlist
		if (ev.type == SDL::EVENT_DROP_FILE && ev.drop.data) {
			_AddToPlaylist(std::string(ev.drop.data), true);
			return SDL::APP_CONTINUE;
		}

		// OS drag-and-drop: add media files to the playlist
		if (ev.type == SDL::EVENT_DROP_FILE && ev.drop.data) {
			_AddToPlaylist(std::string(ev.drop.data), true);
			return SDL::APP_CONTINUE;
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

		// ── Playlist auto-advance ─────────────────────────────────────────────
		if (player.GetState() == SDL::Media::PlaybackState::EndOfFile
		    && !player.IsLooping() && !m_playlist.empty()) {
			int next = m_playlistIdx + 1;
			if (next < (int)m_playlist.size()) {
				_PlayFromPlaylist(next);
			}
		}

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

		// ── Chapter label ─────────────────────────────────────────────────────
		if (eChapterLabel != SDL::ECS::NullEntity) {
			auto chapters = player.GetChapters();
			if (!chapters.empty()) {
				int ci = player.GetCurrentChapter();
				if (ci >= 0 && ci < (int)chapters.size()) {
					const std::string& title = chapters[ci].title;
					ui.SetText(eChapterLabel,
						title.empty() ? std::format("Chapitre {}", ci + 1) : title);
					ui.SetVisible(eChapterLabel, true);
				}
			} else {
				ui.SetVisible(eChapterLabel, false);
			}
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

		m_fontPath = base + "fonts/Roboto-Regular.ttf";
		ui.LoadFont(res_key::FONT, m_fontPath);
		ui.SetDefaultFont(res_key::FONT, 14.f);

		ui.LoadTexture(icon_key::PLAY,		std::format("{}{}.png", icons, icon_key::PLAY));
		ui.LoadTexture(icon_key::PAUSE,		std::format("{}{}.png", icons, icon_key::PAUSE));
		ui.LoadTexture(icon_key::STOP,		std::format("{}{}.png", icons, icon_key::STOP));
		ui.LoadTexture(icon_key::PREV,		std::format("{}{}.png", icons, icon_key::PREV));
		ui.LoadTexture(icon_key::NEXT,		std::format("{}{}.png", icons, icon_key::NEXT));
		ui.LoadTexture(icon_key::OPEN,		std::format("{}{}.png", icons, icon_key::OPEN));
		ui.LoadTexture(icon_key::MUTE,		std::format("{}{}.png", icons, icon_key::MUTE));
		ui.LoadTexture(icon_key::VOL,		std::format("{}{}.png", icons, icon_key::VOL));
		ui.LoadTexture(icon_key::LOOP,		std::format("{}{}.png", icons, icon_key::LOOP));
		ui.LoadTexture(icon_key::FULL,		std::format("{}{}.png", icons, icon_key::FULL));
		ui.LoadTexture(icon_key::PANEL,		std::format("{}{}.png", icons, icon_key::PANEL));
		ui.LoadTexture(icon_key::PLAYLIST,	std::format("{}{}.png", icons, icon_key::PLAYLIST));
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

		// ── Media canvas ──────────────────────────────────────────────────────

		eMediaCanvas =
			ui.CanvasWidget("videoCanvas", nullptr, nullptr,
				[this](SDL::RendererRef r, SDL::FRect rect) {
					_DrawMediaCanvas(r, rect);
				})
				.Grow(100.f)
				.BgColor(pal::BG);

		// ── Seek bar ──────────────────────────────────────────────────────────

		eSeekSlider =
			ui.Slider("seekSlider", 0.f, 1.f, 0.f, Orientation::Horizontal)
				.GrowW(100.f)
				.H(kSeekH)
				.BgColor({30,30,46,255})
				.WithStyle([](Style& s){
					s.trackColor = {40,40,58,255};
					s.fillColor  = {70,130,210,255};
					s.thumbColor = {100,160,235,255};
				})
				.Radius(SDL::FCorners(4.f))
				.OnChange<float>([this](float v) {
					double dur = player.GetDuration();
					if (dur > 0.0) player.Seek((double)v * dur);
				});

		eTimeLabel =
			ui.Label("timeLabel", "--:-- / --:--")
			  .BgColor(pal::TRANSP)
			  .TextColor({160,165,185,255})
			  .Font(res_key::FONT, 12.f)
			  .PaddingH(8.f);

		eChapterLabel =
			ui.Label("chapterLabel", "")
			  .BgColor(pal::TRANSP)
			  .TextColor({180, 150, 60, 255})
			  .Font(res_key::FONT, 11.f)
			  .PaddingH(8.f);
		ui.SetVisible(eChapterLabel, false);

		eSeekRow =
			ui.Row("seekRow", 0.f, 4.f)
			  .H(kSeekH + 8.f)
			  .BgColor(pal::PANEL)
			  .AlignChildrenV(Align::Center)
			  .Children(eSeekSlider, eTimeLabel, eChapterLabel);

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
					s.thumbColor   = {100,160,235,255};
				})
				.Radius(SDL::FCorners(3.f))
				.Tooltip("Volume")
				.OnChange<float>([this](float v) {
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
					makeIconBtn("btnChapPrev", icon_key::PREV, "", "Chapitre précédent")
						.OnClick([this]{ _SeekPrevChapter(); }),
					makeIconBtn("btnPrev", "", "-10s", "Reculer de 10 secondes")
						.OnClick([this]{ player.SeekRelative(-10.0); }),
					ePlayBtn,
					makeIconBtn("btnStop", icon_key::STOP, "", "Arrêter")
						.OnClick([this]{ player.Stop(); _RefreshPlayBtn(); }),
					makeIconBtn("btnNext", "", "+10s", "Avancer de 10 secondes")
						.OnClick([this]{ player.SeekRelative(+10.0); }),
					makeIconBtn("btnChapNext", icon_key::NEXT, "", "Chapitre suivant")
						.OnClick([this]{ _SeekNextChapter(); }),
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
					makeIconBtn("btnSubCfg", "", "SUB", "Configurer les sous-titres")
						.OnClick([this]{ _ShowSubtitlePopup(); }),
					makeIconBtn("btnPlaylist", icon_key::PLAYLIST, "", "Playlist")
						.OnClick([this]{ _ShowPlaylistPopup(); }),
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

		// ── Subtitle config popup ─────────────────────────────────────────────

		_BuildSubtitlePopup();

		// ── Playlist popup ────────────────────────────────────────────────────

		_BuildPlaylistPopup();

		// ── Root ──────────────────────────────────────────────────────────────

		ui.Row("root", 0.f, 0.f)
			.Margin(SDL::FBox(0.f))
			.W(Value::Ww(100.f))
			.H(Value::Wh(100.f))
			.BgColor(pal::BG)
			.AlignChildrenV(Align::Stretch)
			.Children(mainCol, eSidePanel, eSubPopup, ePlaylistPopup)
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
				.OnChange<int>([this](int idx){ _OnAudioTrackSelected((int)idx); });

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
				.OnChange<int>([this](int idx){ _OnSubTrackSelected((int)idx); });

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
		_RefreshChapterMarkers();

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

	void _RefreshChapterMarkers() {
		auto chapters = player.GetChapters();
		double dur = player.GetDuration();
		std::vector<float> markers;
		if (dur > 0.0 && chapters.size() > 1) {
			for (auto& ch : chapters)
				markers.push_back((float)(ch.startTime / dur));
		}
		ui.SetSliderMarkers(eSeekSlider, std::move(markers));
	}

	void _SeekPrevChapter() {
		auto chapters = player.GetChapters();
		if (chapters.empty()) { player.SeekRelative(-10.0); return; }
		int ci = player.GetCurrentChapter();
		double t = player.GetCurrentTime();
		// If we're more than 2s into the chapter, go back to its start; else go to previous
		if (ci > 0 && (t - chapters[ci].startTime) < 2.0) --ci;
		player.SeekToChapter(ci);
	}

	void _SeekNextChapter() {
		auto chapters = player.GetChapters();
		if (chapters.empty()) { player.SeekRelative(+10.0); return; }
		int ci = player.GetCurrentChapter();
		player.SeekToChapter(ci + 1);
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
			_DrawSubtitles(r, rect);
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
		_DrawSubtitles(r, rect);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Subtitle rendering (direct TTF, multi-line, centered, anti-overflow)
	// ─────────────────────────────────────────────────────────────────────────

	void _DrawSubtitleText(SDL::RendererRef r, SDL::FRect rect, const std::string& text) {
#ifdef SDL3PP_ENABLE_TTF
		if (text.empty() || !m_subEngine || !m_subFont) return;

		// Apply font config
		m_subFont.SetSize(subtitleCfg.fontSize);
		m_subFont.SetStyle(subtitleCfg.fontStyle);

		// Split into lines on \n
		std::vector<std::string> lines;
		std::istringstream ss(text);
		std::string ln;
		while (std::getline(ss, ln))
			lines.push_back(ln);
		if (lines.empty()) return;

		// Measure
		constexpr float kPadH = 14.f, kPadV = 6.f, kLineGap = 3.f;
		int lineH = 0;
		std::vector<int> lineW(lines.size(), 0);
		int maxLineW = 0;
		for (int i = 0; i < (int)lines.size(); ++i) {
			int w = 0, h = 0;
			m_subFont.GetStringSize(lines[i], &w, &h);
			lineW[i] = w;
			maxLineW  = std::max(maxLineW, w);
			lineH     = std::max(lineH, h);
		}
		if (lineH == 0) lineH = (int)subtitleCfg.fontSize;

		// Block dimensions — clamp width to canvas
		float maxAvail = rect.w - 24.f;
		float blockW   = std::min((float)maxLineW + 2.f * kPadH, maxAvail);
		float blockH   = (float)lines.size() * (float)lineH
		               + ((float)lines.size() - 1.f) * kLineGap
		               + 2.f * kPadV;

		// Vertical position
		float bx = rect.x + (rect.w - blockW) * 0.5f;
		float by = subtitleCfg.posBottom
		         ? rect.y + rect.h - blockH - subtitleCfg.marginV
		         : rect.y + subtitleCfg.marginV;
		by = std::clamp(by, rect.y + 4.f, rect.y + rect.h - blockH - 4.f);

		// Background
		SDL::FRect bgRect{bx, by, blockW, blockH};
		r.SetDrawBlendMode(SDL::BLENDMODE_BLEND);
		r.SetDrawColor(subtitleCfg.bgColor);
		r.RenderFillRect(bgRect);

		// Draw each line centered in the block
		for (int i = 0; i < (int)lines.size(); ++i) {
			float lx = bx + kPadH + ((float)maxLineW - (float)lineW[i]) * 0.5f;
			lx = std::max(lx, bx + kPadH);
			float ly = by + kPadV + (float)i * ((float)lineH + kLineGap);
			try {
				SDL::Text t = m_subEngine->CreateText(m_subFont, lines[i]);
				t.SetColor(subtitleCfg.textColor);
				t.DrawRenderer({lx, ly});
			} catch (...) {}
		}
#endif
	}

	void _DrawSubtitles(SDL::RendererRef r, SDL::FRect rect) {
		std::string sub = player.GetCurrentSubtitle();
		if (sub.empty()) return;
		if (sub.find("Dialogue:") == 0 || sub.find(",,") != std::string::npos)
			sub = StripASS(sub);
		if (sub.empty()) return;
		// Replace \N (ASS soft line break) with \n
		for (size_t p = 0; (p = sub.find("\\N", p)) != std::string::npos; p += 1)
			sub.replace(p, 2, "\n");
		_DrawSubtitleText(r, rect, sub);
	}

	// ─────────────────────────────────────────────────────────────────────────
	// Subtitle config popup
	// ─────────────────────────────────────────────────────────────────────────

	void _BuildSubtitlePopup() {
		using namespace SDL::UI;

		constexpr float popW = 300.f, popH = 510.f;

		Style secHdr;
		secHdr.bgColor   = pal::PANEL2;
		secHdr.textColor = pal::ACCENT;
		secHdr.fontKey   = res_key::FONT;
		secHdr.fontSize  = 11.f;

		auto rowStyle = [&]() -> Style {
			Style s;
			s.bgColor   = pal::TRANSP;
			s.textColor = pal::WHITE;
			s.fontKey   = res_key::FONT;
			s.fontSize  = 13.f;
			return s;
		};
		auto makeRowLabel = [&](const std::string& name, const std::string& text) {
			return ui.Label(name, text)
				.Style(rowStyle())
				.W(90.f)
				.AlignV(Align::Center);
		};

		// ── Typography ────────────────────────────────────────────────────────

		eSubFontSzBox =
			ui.InputValue<float>("subFontSz", 8.f, 72.f, subtitleCfg.fontSize, 1.f)
				.GrowW(100.f).H(26.f)
				.Font(res_key::FONT, 13.f)
				.OnChange<float>([this](float v) {
					subtitleCfg.fontSize = v;
					_RefreshSubFont();
				});

		eSubStyleCombo =
			ui.ComboBox("subStyle",
				{"Normal", "Gras", "Italique", "Gras + Italique"}, 0)
				.GrowW(100.f).H(26.f)
				.Font(res_key::FONT, 13.f)
				.OnChange<int>([this](int v) {
#ifdef SDL3PP_ENABLE_TTF
					const Uint32 styles[] = {
						SDL::STYLE_NORMAL,
						SDL::STYLE_BOLD,
						SDL::STYLE_ITALIC,
						SDL::STYLE_BOLD | SDL::STYLE_ITALIC
					};
					subtitleCfg.fontStyle = styles[std::clamp((int)v, 0, 3)];
#else
					subtitleCfg.fontStyle = 0;
#endif
					_RefreshSubFont();
				});

		// ── Position ──────────────────────────────────────────────────────────

		eSubPosCombo =
			ui.ComboBox("subPos", {"Bas", "Haut"}, 0)
				.GrowW(100.f).H(26.f)
				.Font(res_key::FONT, 13.f)
				.OnChange<int>([this](int v) {
					subtitleCfg.posBottom = (v == 0);
				});

		eSubMarginBox =
			ui.InputValue<float>("subMargin", 0.f, 200.f, subtitleCfg.marginV, 1.f)
				.GrowW(100.f).H(26.f)
				.Font(res_key::FONT, 13.f)
				.OnChange<float>([this](float v) {
					subtitleCfg.marginV = v;
				});

		// ── Text color ────────────────────────────────────────────────────────

		eSubTextClrPick = ui.MakeColorPicker("subTextClr");
		ui.SetPickedColor(eSubTextClrPick, subtitleCfg.textColor);
		ui.GetBuilder(eSubTextClrPick)
			.GrowW(100.f).H(170.f)
			.OnChange<float>([this](float) {
				subtitleCfg.textColor = ui.GetPickedColor(eSubTextClrPick);
			});

		// ── Background color ──────────────────────────────────────────────────

		eSubBgClrPick = ui.MakeColorPicker("subBgClr");
		ui.SetPickedColor(eSubBgClrPick, {subtitleCfg.bgColor.r,
		                                   subtitleCfg.bgColor.g,
		                                   subtitleCfg.bgColor.b, 255});
		if (auto* d = ui.GetColorPickerData(eSubBgClrPick)) d->showAlpha = false;
		ui.GetBuilder(eSubBgClrPick)
			.GrowW(100.f).H(170.f)
			.OnChange<float>([this](float) {
				SDL::Color c = ui.GetPickedColor(eSubBgClrPick);
				subtitleCfg.bgColor.r = c.r;
				subtitleCfg.bgColor.g = c.g;
				subtitleCfg.bgColor.b = c.b;
			});

		// ── Background alpha ──────────────────────────────────────────────────

		eSubBgAlphaLabel =
			ui.Label("subBgAlphaLbl",
				std::format("{:.0f}%", subtitleCfg.bgColor.a / 255.f * 100.f))
				.BgColor(pal::TRANSP)
				.TextColor(pal::GREY)
				.Font(res_key::FONT, 12.f)
				.W(38.f)
				.AlignV(Align::Center);

		eSubBgAlphaSlider =
			ui.Slider("subBgAlpha", 0.f, 1.f,
				subtitleCfg.bgColor.a / 255.f, Orientation::Horizontal)
				.GrowW(100.f).H(20.f)
				.WithStyle([](Style& s) {
					s.trackColor = {40,40,58,255};
					s.fillColor  = {70,130,210,255};
					s.thumbColor = {100,160,235,255};
				})
				.OnChange<float>([this](float v) {
					subtitleCfg.bgColor.a = (Uint8)(v * 255.f);
					ui.SetText(eSubBgAlphaLabel,
						std::format("{:.0f}%", v * 100.f));
				});

		// ── Preview ───────────────────────────────────────────────────────────

		eSubPreview =
			ui.CanvasWidget("subPreview", nullptr, nullptr,
				[this](SDL::RendererRef r, SDL::FRect rect) {
					r.SetDrawColor({10, 12, 20, 255});
					r.RenderFillRect(rect);
					_DrawSubtitleText(r, rect,
						"Exemple de sous-titre\nSur deux lignes");
				})
				.GrowW(100.f).H(90.f)
				.BgColor({10, 12, 20, 255});

		// ── Assemble popup ────────────────────────────────────────────────────

		auto content =
			ui.Column("subCfgCol", 0.f, 0.f)
				.GrowW(100.f)
				.BgColor(pal::BG)
				.Children(
					// Typographie
					ui.Label("subHdrTypo", "TYPOGRAPHIE")
						.Style(secHdr).H(20.f).GrowW(100.f).PaddingH(8.f),
					ui.Row("subRowSz", 4.f, 6.f).H(32.f).GrowW(100.f)
						.BgColor(pal::BG).AlignChildrenV(Align::Center)
						.Children(makeRowLabel("subLblSz","Taille"), eSubFontSzBox),
					ui.Row("subRowSt", 4.f, 6.f).H(32.f).GrowW(100.f)
						.BgColor(pal::BG).AlignChildrenV(Align::Center)
						.Children(makeRowLabel("subLblSt","Style"), eSubStyleCombo),
					ui.Separator("subSep1").BgColor(pal::BORDER),
					// Position
					ui.Label("subHdrPos", "POSITION")
						.Style(secHdr).H(20.f).GrowW(100.f).PaddingH(8.f),
					ui.Row("subRowPos", 4.f, 6.f).H(32.f).GrowW(100.f)
						.BgColor(pal::BG).AlignChildrenV(Align::Center)
						.Children(makeRowLabel("subLblPos","Placement"), eSubPosCombo),
					ui.Row("subRowMg", 4.f, 6.f).H(32.f).GrowW(100.f)
						.BgColor(pal::BG).AlignChildrenV(Align::Center)
						.Children(makeRowLabel("subLblMg","Marge (px)"), eSubMarginBox),
					ui.Separator("subSep2").BgColor(pal::BORDER),
					// Couleur texte
					ui.Label("subHdrTxtClr", "COULEUR DU TEXTE")
						.Style(secHdr).H(20.f).GrowW(100.f).PaddingH(8.f),
					ui.GetBuilder(eSubTextClrPick),
					ui.Separator("subSep3").BgColor(pal::BORDER),
					// Fond
					ui.Label("subHdrBgClr", "ARRIÈRE-PLAN")
						.Style(secHdr).H(20.f).GrowW(100.f).PaddingH(8.f),
					ui.GetBuilder(eSubBgClrPick),
					ui.Row("subRowAlpha", 4.f, 6.f).H(28.f).GrowW(100.f)
						.BgColor(pal::BG).AlignChildrenV(Align::Center)
						.Children(
							makeRowLabel("subLblAlpha","Opacité"),
							eSubBgAlphaSlider,
							eSubBgAlphaLabel
						),
					ui.Separator("subSep4").BgColor(pal::BORDER),
					// Aperçu
					ui.Label("subHdrPrev", "APERÇU")
						.Style(secHdr).H(20.f).GrowW(100.f).PaddingH(8.f),
					eSubPreview
				);

		eSubPopup =
			ui.Popup("subCfgPopup", "Sous-titrage", true, true, false)
				.W(popW).H(popH)
				.Fixed(Value::Ww(50.f) - popW * 0.5f,
				       Value::Wh(50.f) - popH * 0.5f)
				.BgColor(pal::BG)
				.Children(
					ui.ScrollView("subCfgScroll")
						.Grow(100.f)
						.BgColor(pal::BG)
						.Children(content)
				).Id();

		ui.SetPopupOpen(eSubPopup, false);
	}

	void _ShowSubtitlePopup() {
		ui.SetPopupOpen(eSubPopup, true);
	}

	void _RefreshSubFont() {
#ifdef SDL3PP_ENABLE_TTF
		if (m_subFont)
			m_subFont.SetSize(subtitleCfg.fontSize);
#endif
	}


	// ─────────────────────────────────────────────────────────────────────────
	// Playlist popup
	// ─────────────────────────────────────────────────────────────────────────

	void _BuildPlaylistPopup() {
		using namespace SDL::UI;

		constexpr float popW = 400.f, popH = 480.f;

		Style secHdr;
		secHdr.bgColor   = pal::PANEL2;
		secHdr.textColor = pal::ACCENT;
		secHdr.fontKey   = res_key::FONT;
		secHdr.fontSize  = 11.f;

		// Count label
		ePlaylistCountLbl =
			ui.Label("plCountLbl", "0 fichier(s)")
			  .Style(secHdr)
			  .W(Value::Auto());

		// Reorderable list
		ePlaylistList =
			ui.ListBoxWidget("playlistList", {})
			  .Grow(100.f)
			  .BgColor({18, 18, 30, 255})
			  .TextColor({200, 202, 220, 255})
			  .Font(res_key::FONT, 12.f)
			  .Reorderable(true)
			  .OnReorder([this](int from, int to) {
					// Sync m_playlist to match list reorder
					auto item = m_playlist[(size_t)from];
					m_playlist.erase(m_playlist.begin() + from);
					m_playlist.insert(m_playlist.begin() + to, item);
					// Update playing index
					if (m_playlistIdx == from)
						m_playlistIdx = to;
					else if (from < m_playlistIdx && to >= m_playlistIdx)
						--m_playlistIdx;
					else if (from > m_playlistIdx && to <= m_playlistIdx)
						++m_playlistIdx;
			  })
			  .OnChange<float>([this](float idx) {
					if ((int)idx == m_playlistIdx) return;
			  });

		// Toolbar buttons
		auto btnAdd =
			ui.Button("plBtnAdd", "+ Ajouter")
			  .H(28.f).W(Value::Auto()).PaddingH(10.f)
			  .BgColor(pal::NEUTRAL).BgHover({45,45,65,255})
			  .BorderColor(pal::BORDER).BorderLeft(1).BorderRight(1).BorderTop(1).BorderBottom(1)
			  .Radius(SDL::FCorners(4.f))
			  .TextColor(pal::WHITE).Font(res_key::FONT, 12.f)
			  .Tooltip("Ajouter un fichier média")
			  .OnClick([this]{ _ShowOpenDialog(); });

		auto btnRemove =
			ui.Button("plBtnRemove", "- Supprimer")
			  .H(28.f).W(Value::Auto()).PaddingH(10.f)
			  .BgColor(pal::NEUTRAL).BgHover({65,25,25,255})
			  .BorderColor(pal::BORDER).BorderLeft(1).BorderRight(1).BorderTop(1).BorderBottom(1)
			  .Radius(SDL::FCorners(4.f))
			  .TextColor(pal::WHITE).Font(res_key::FONT, 12.f)
			  .Tooltip("Supprimer l\'élément sélectionné")
			  .OnClick([this]{
					int sel = ui.GetListBoxSelection(ePlaylistList);
					if (sel >= 0 && sel < (int)m_playlist.size()) {
						m_playlist.erase(m_playlist.begin() + sel);
						if (m_playlistIdx == sel) m_playlistIdx = -1;
						else if (m_playlistIdx > sel) --m_playlistIdx;
						_RefreshPlaylistUI();
					}
			  });

		auto btnClear =
			ui.Button("plBtnClear", "Vider")
			  .H(28.f).W(Value::Auto()).PaddingH(10.f)
			  .BgColor(pal::NEUTRAL).BgHover({65,25,25,255})
			  .BorderColor(pal::BORDER).BorderLeft(1).BorderRight(1).BorderTop(1).BorderBottom(1)
			  .Radius(SDL::FCorners(4.f))
			  .TextColor(pal::WHITE).Font(res_key::FONT, 12.f)
			  .Tooltip("Vider la playlist")
			  .OnClick([this]{
					m_playlist.clear();
					m_playlistIdx = -1;
					_RefreshPlaylistUI();
			  });

		auto btnPlay =
			ui.Button("plBtnPlay", "▶ Lire")
			  .H(28.f).W(Value::Auto()).PaddingH(10.f)
			  .BgColor(pal::ACCENT).BgHover({90,150,230,255})
			  .BorderColor(pal::BORDER).BorderLeft(1).BorderRight(1).BorderTop(1).BorderBottom(1)
			  .Radius(SDL::FCorners(4.f))
			  .TextColor(pal::WHITE).Font(res_key::FONT, 12.f)
			  .Tooltip("Lire l\'élément sélectionné")
			  .OnClick([this]{
					int sel = ui.GetListBoxSelection(ePlaylistList);
					if (sel >= 0 && sel < (int)m_playlist.size())
						_PlayFromPlaylist(sel);
			  });

		auto hint =
			ui.Label("plHint", "Glissez des fichiers ici ou dans la fenêtre")
			  .H(20.f).GrowW(100.f)
			  .BgColor(pal::TRANSP)
			  .TextColor(pal::GREY)
			  .Font(res_key::FONT, 10.f)
			  .AlignH(Align::Center).AlignV(Align::Center);

		auto content =
			ui.Column("plCol", 0.f, 0.f)
			  .Grow(100.f).BgColor(pal::BG)
			  .Children(
					ui.Row("plHdrRow", 4.f, 6.f).H(26.f).BgColor(pal::PANEL2)
					  .AlignChildrenV(Align::Center)
					  .Children(
							ui.Label("plHdrLbl", "PLAYLIST").Style(secHdr).GrowW(100.f),
							ePlaylistCountLbl
					  ),
					ePlaylistList,
					ui.Separator("plSep").BgColor(pal::BORDER),
					ui.Row("plToolbar", 4.f, 6.f).H(36.f).BgColor(pal::BG)
					  .AlignChildrenV(Align::Center)
					  .Children(btnAdd, btnRemove, btnClear,
								ui.Container("plSpc").GrowW(100.f).BgColor(pal::TRANSP),
								btnPlay),
					hint
			  );

		ePlaylistPopup =
			ui.Popup("playlistPopup", "Playlist", true, true, true)
			  .W(popW).H(popH)
			  .Fixed(Value::Ww(50.f) - popW * 0.5f,
			         Value::Wh(50.f) - popH * 0.5f)
			  .BgColor(pal::BG)
			  .Children(content).Id();

		ui.SetPopupOpen(ePlaylistPopup, false);
	}

	void _ShowPlaylistPopup() {
		ui.SetPopupOpen(ePlaylistPopup, true);
	}

	void _AddToPlaylist(const std::string& path, bool playIfEmpty) {
		if (!IsMediaFile(path)) return;
		PlaylistEntry entry;
		entry.path  = path;
		entry.title = path.substr(path.rfind('/') + 1);
		m_playlist.push_back(entry);
		_RefreshPlaylistUI();
		if (playIfEmpty && m_playlist.size() == 1)
			_PlayFromPlaylist(0);
	}

	void _PlayFromPlaylist(int idx) {
		if (idx < 0 || idx >= (int)m_playlist.size()) return;
		m_playlistIdx = idx;
		_RefreshPlaylistUI();
		_OpenFile(m_playlist[(size_t)idx].path);
	}

	void _RefreshPlaylistUI() {
		if (ePlaylistList == SDL::ECS::NullEntity) return;
		std::vector<std::string> labels;
		for (int i = 0; i < (int)m_playlist.size(); ++i) {
			std::string lbl = PlaylistEntryLabel(m_playlist[(size_t)i], i);
			if (i == m_playlistIdx) lbl = "► " + lbl;
			labels.push_back(lbl);
		}
		ui.SetListBoxItems(ePlaylistList, labels);
		if (m_playlistIdx >= 0 && m_playlistIdx < (int)m_playlist.size())
			ui.SetListBoxSelection(ePlaylistList, m_playlistIdx);
		if (ePlaylistCountLbl != SDL::ECS::NullEntity)
			ui.SetText(ePlaylistCountLbl,
				std::format("{} fichier(s)", m_playlist.size()));
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
