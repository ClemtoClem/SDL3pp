/**
 * @file 05_spectrum_analyzer.cpp
 * @brief Real-time microphone spectrum analyzer using SDL3pp_audio,
 *        SDL3pp_audioProcessing and SDL3pp_ui.
 *
 * Left panel  - device list (ListBox) + analysis parameters
 * Right panel - waveform (GradedGraph, line mode) + spectrum (GradedGraph, bar+log mode)
 *
 * Public-domain example.
 */

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_audio.h>
#include <SDL3pp/SDL3pp_audioProcessing.h>
#include <SDL3pp/SDL3pp_resources.h>

#include <algorithm>
#include <atomic>
#include <deque>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace pal {
	constexpr SDL::Color BG       = {  10,  12,  20, 255}; ///< Background color
	constexpr SDL::Color BORDER   = {  40,  44,  64, 255}; ///< Border color
	constexpr SDL::Color PANEL    = {  16,  18,  28, 255}; ///< Panel background color
	constexpr SDL::Color ACCENT   = {  60, 140, 220, 255}; ///< Accent color
	constexpr SDL::Color ACCENTH  = {  85, 165, 245, 255}; ///< Accent hovered color
	constexpr SDL::Color ACCENTP  = {  40, 110, 190, 255}; ///< Accent pressed color
	constexpr SDL::Color GREEN    = {  45, 195, 110, 255}; ///< Green color
	constexpr SDL::Color GREENH   = {  70, 220, 135, 255}; ///< Green hovered color
	constexpr SDL::Color ORANGE   = { 230, 145,  30, 255}; ///< Orange color
	constexpr SDL::Color ORANGEH  = { 245, 165,  50, 255}; ///< Orange hovered color
	constexpr SDL::Color RED      = { 200,  60,  50, 255}; ///< Red color
	constexpr SDL::Color REDH     = { 220,  80,  65, 255}; ///< Red hovered color
	constexpr SDL::Color PURPLE   = { 155,  75, 220, 255}; ///< Purple color
	constexpr SDL::Color PURPLEH  = { 175, 100, 235, 255}; ///< Purple hovered color
	constexpr SDL::Color WHITE    = { 220, 220, 228, 255}; ///< White color
	constexpr SDL::Color GREY     = { 120, 125, 145, 255}; ///< Grey color
}

// ─────────────────────────────────────────────────────────────────────────────
//  Sample-size presets
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kSampleSizes[]  = {256, 512, 1024, 2048, 4096};
static constexpr int kNumSizes       = 5;
static constexpr int kDefaultSizeIdx = 3; // 2048

// ─────────────────────────────────────────────────────────────────────────────
//  Sample-rate presets
// ─────────────────────────────────────────────────────────────────────────────
static constexpr int kSampleRates[]   = {8000, 16000, 22050, 44100, 48000};
static constexpr int kNumRates        = 5;
static constexpr int kDefaultRateIdx  = 3; // 44100

// ─────────────────────────────────────────────────────────────────────────────
//  Window-function presets
// ─────────────────────────────────────────────────────────────────────────────
enum class WindowType { Rectangular = 0, Hanning, Hamming, Blackman };

struct WindowPreset { WindowType type; const char* label; const char* tooltip; };
static constexpr WindowPreset kWindows[] = {
	{ WindowType::Rectangular, "Rect",     "Aucune — résolution fréq. maximale, fuites importantes" },
	{ WindowType::Hanning,     "Hann",     "Hanning — bon compromis général"                       },
	{ WindowType::Hamming,     "Hamming",  "Hamming — lobes secondaires réduits"                   },
	{ WindowType::Blackman,    "Blackman", "Blackman — fuites minimales, résolution réduite"        },
};
static constexpr int kNumWindows       = 4;
static constexpr int kDefaultWindowIdx = 1; // Hanning

// ─────────────────────────────────────────────────────────────────────────────
//  Main application struct
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
	// ── window / renderer ─────────────────────────────────────────────────────
	static constexpr SDL::Point kWinSz = {1280, 720};

	static SDL::AppResult Init(Main** out, SDL::AppArgs /*args*/) {
		SDL::SetAppMetadata("SDL3pp Spectrum Analyzer", "1.0",
							"com.example.audio-spectrum");
		SDL::Init(SDL::INIT_VIDEO | SDL::INIT_AUDIO);
		SDL::TTF::Init();
		*out = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
		SDL::TTF::Quit();
		SDL::Quit();
	}

	// ── SDL objects ───────────────────────────────────────────────────────────
	SDL::Window      window  { SDL::CreateWindowAndRenderer(
								   "SDL3pp - Spectrum Analyzer",
								   kWinSz, SDL_WINDOW_RESIZABLE, nullptr) };
	SDL::RendererRef renderer{ window.GetRenderer() };

	// ── UI ────────────────────────────────────────────────────────────────────
	SDL::ResourceManager rm;
	SDL::ResourcePool&   pool{ *rm.CreatePool("ui") };
	SDL::ECS::Context      ecs_context;
	SDL::UI::System      ui{ ecs_context, renderer, SDL::MixerRef{nullptr}, pool };

	SDL::FrameTimer timer{ 60.f };

	// ── UI entity IDs ─────────────────────────────────────────────────────────
	SDL::ECS::EntityId m_lbDevices  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblDevice  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblStatus  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblGain    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblFMin    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblFMax    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_graphWave  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_graphSpec  = SDL::ECS::NullEntity;
	// sample size / rate / window selection buttons
	SDL::ECS::EntityId m_szBtns[kNumSizes]   {};
	SDL::ECS::EntityId m_raBtns[kNumRates]   {};
	SDL::ECS::EntityId m_winBtns[kNumWindows]{};

	// ── Audio ─────────────────────────────────────────────────────────────────
	SDL::OwnArray<SDL::AudioDeviceRef> m_devices;
	SDL::AudioStream   m_recStream;          // active recording stream (null = none)

	// ── Analysis state ────────────────────────────────────────────────────────
	std::deque<float> m_pcmBuf;             // rolling PCM ring buffer (main-thread only)
	float      m_gain       = 1.f;
	float      m_freqMin    = 0.f;
	float      m_freqMax    = 20000.f;
	int        m_sampleSize = kSampleSizes[kDefaultSizeIdx];
	float      m_sampleRate = static_cast<float>(kSampleRates[kDefaultRateIdx]);
	WindowType m_windowType = kWindows[kDefaultWindowIdx].type;
	int        m_selDevice  = -1;               // selected device index

	// ─────────────────────────────────────────────────────────────────────────
	Main() {
		timer.Begin();
		_Enumerate();
		_BuildUI();
	}

	~Main() {
		_CloseDevice();
		pool.Release();
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Device management
	// ─────────────────────────────────────────────────────────────────────────
	void _Enumerate() {
		try {
			m_devices = SDL::GetAudioRecordingDevices();
		} catch (...) {
			m_devices = {};
		}
		// Update listbox if it was already built
		if (m_lbDevices != SDL::ECS::NullEntity) {
			std::vector<std::string> names;
			names.reserve(m_devices.size());
			for (size_t i = 0; i < m_devices.size(); ++i) {
				try {
					names.emplace_back(SDL::GetAudioDeviceName(m_devices[i]));
				} catch (...) {
					names.emplace_back(std::format("Device {}", i));
				}
			}
			ui.SetListBoxItems(m_lbDevices, std::move(names));
			_SetStatus(std::format("{} input device(s) found", m_devices.size()));
		}
	}

	void _OpenDevice(int idx) {
		if (idx < 0 || idx >= (int)m_devices.size()) return;
		_CloseDevice();
		m_selDevice  = idx;
		m_pcmBuf.clear();

		try {
			SDL::AudioSpec reqSpec{};
			reqSpec.format   = SDL::AUDIO_F32;
			reqSpec.channels = 1;
			reqSpec.freq     = static_cast<int>(m_sampleRate);

			// OpenStream on the physical device ref:
			// SDL_OpenAudioDeviceStream(devid, spec, nullptr, nullptr)
			m_recStream = m_devices[idx].OpenStream(reqSpec, nullptr, nullptr);
			m_recStream.ResumeDevice();

			std::string name;
			try {
				name = SDL::GetAudioDeviceName(m_devices[idx]);
			} catch (...) {
				name = std::format("Device {}", idx);
			}
			ui.SetText(m_lblDevice, name);
			_SetStatus("Recording: " + name);
		} catch (const std::exception& e) {
			_SetStatus(std::format("Error opening device: {}", e.what()));
		}
	}

	void _CloseDevice() {
		if (m_recStream) {
			m_recStream.PauseDevice();
			m_recStream = SDL::AudioStream{};
		}
		m_selDevice = -1;
		m_pcmBuf.clear();
	}

	void _ReopenDevice() {
		int prev = m_selDevice;
		_CloseDevice();
		if (prev >= 0) _OpenDevice(prev);
	}

	void _SetStatus(const std::string& msg) {
		if (m_lblStatus != SDL::ECS::NullEntity)
			ui.SetText(m_lblStatus, msg);
	}

	// ── Window function application ───────────────────────────────────────────
	void _ApplyWindow(std::vector<float>& buf) {
		const int n = (int)buf.size();
		std::vector<float> win;
		switch (m_windowType) {
			case WindowType::Rectangular: return;  // no-op
			case WindowType::Hamming:     win = SDL::detail::WindowHamming(n);  break;
			case WindowType::Blackman:    win = SDL::detail::WindowBlackman(n); break;
			default:                      win = SDL::detail::WindowHanning(n);  break;
		}
		for (int i = 0; i < n; ++i) buf[i] *= win[i];
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Audio + DSP  (called every frame from Iterate)
	// ─────────────────────────────────────────────────────────────────────────
	void _ProcessAudio() {
		if (!m_recStream) return;

		// Drain all available samples from the stream
		int avail = m_recStream.GetAvailable();
		if (avail <= 0) return;

		int nSamples = avail / (int)sizeof(float);
		if (nSamples <= 0) return;

		std::vector<float> tmp(nSamples, 0.f);
		int read = m_recStream.GetData(std::span<float>{tmp.data(), tmp.size()});
		int nRead = read / (int)sizeof(float);

		// Apply gain and push into rolling buffer
		for (int i = 0; i < nRead; ++i)
			m_pcmBuf.push_back(tmp[i] * m_gain);

		// Trim buffer to reasonable size (keep enough for display + FFT)
		const int kMaxBuf = m_sampleSize * 8;
		while ((int)m_pcmBuf.size() > kMaxBuf)
			m_pcmBuf.pop_front();
	}

	void _UpdateGraphs() {
		const int n = (int)m_pcmBuf.size();
		if (n < 16) return;

		// ── Waveform ──────────────────────────────────────────────────────────
		{
			int wn = (n < m_sampleSize) ? n : m_sampleSize;
			std::vector<float> wave(wn);
			auto start = m_pcmBuf.end() - wn;
			std::copy(start, m_pcmBuf.end(), wave.begin());

			ui.SetGraphData(m_graphWave, wave);
			ui.SetGraphRange(m_graphWave, -1.f, 1.f);
			// X axis: time in ms
			float dur = static_cast<float>(wn) / m_sampleRate * 1000.f;
			ui.SetGraphXRange(m_graphWave, 0.f, dur);

			if (auto* gd = ui.GetGraphData(m_graphWave)) {
				gd->xDivisions = 8;
				gd->yDivisions = 4;
				gd->showFill   = false;
				gd->barMode    = false;
				gd->logFreq    = false;
				gd->lineColor  = pal::GREEN;
				gd->fillColor  = SDL::Color(pal::GREEN).SetA(100);
				gd->xLabel     = "ms";
				gd->yLabel     = "amp";
			}
		}

		// ── Spectrum ──────────────────────────────────────────────────────────
		{
			int sn = (n < m_sampleSize) ? n : m_sampleSize;
			std::vector<float> wave(sn);
			auto start = m_pcmBuf.end() - sn;
			std::copy(start, m_pcmBuf.end(), wave.begin());

			// Apply selected window function
			_ApplyWindow(wave);

			// FFT → dB magnitude (positive frequencies only)
			SDL::Signal sig{wave, m_sampleRate};
			auto fftBins  = SDL::FFT(sig);
			auto dbBins   = SDL::FFTMagnitudeDB(fftBins);
			auto freqAxis = SDL::FFTFrequencies((int)fftBins.size(), m_sampleRate);

			// Collect bins within [freqMin, freqMax]
			std::vector<float> specData;
			specData.reserve(freqAxis.size());
			for (int k = 0; k < (int)freqAxis.size(); ++k) {
				float f = freqAxis[k];
				if (f < m_freqMin) continue;
				if (f > m_freqMax) break;
				float db = SDL::Max(-80.f, dbBins[k]);
				specData.push_back(db);
			}

			ui.SetGraphData(m_graphSpec, specData);
			ui.SetGraphRange(m_graphSpec, -80.f, 0.f);
			ui.SetGraphXRange(m_graphSpec, m_freqMin, m_freqMax);

			if (auto* gd = ui.GetGraphData(m_graphSpec)) {
				gd->xDivisions = 8;
				gd->yDivisions = 5;
				gd->showFill   = true;
				gd->barMode    = false;
				gd->logFreq    = (m_freqMin < 100.f); // log scale when showing sub-100 Hz
				gd->lineColor  = pal::ACCENT;
				gd->fillColor  = SDL::Color(pal::ACCENT).SetA(100);
				gd->xLabel     = "Hz";
				gd->yLabel     = "dB";
			}
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Iterate / Event
	// ─────────────────────────────────────────────────────────────────────────
	SDL::AppResult Iterate() {
		timer.Begin();
		const float dt = timer.GetDelta();

		rm.UpdateAll();

		_ProcessAudio();
		_UpdateGraphs();

		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();
		ui.Iterate(dt);
		renderer.Present();

		timer.End();
		return SDL::APP_CONTINUE;
	}

	SDL::AppResult Event(const SDL::Event& ev) {
		if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
		else if (ev.type == SDL::EVENT_KEY_DOWN) {
			if ((ev.key.mod & SDL::KMOD_CTRL) && ev.key.key == SDL::KEYCODE_Q)
				return SDL::APP_SUCCESS;
		}
		ui.ProcessEvent(ev);
		return SDL::APP_CONTINUE;
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  UI construction
	// ─────────────────────────────────────────────────────────────────────────
	void _BuildUI() {
		const std::string basePath = std::string(SDL::GetBasePath()) + "../../../assets/";
		ui.LoadFont("arial",  basePath + "fonts/arial.ttf");
		ui.SetDefaultFont("arial", 12.f);

		auto root = ui.Row("root", 0.f, 0.f)
			.W(SDL::UI::Value::Ww(100.f))
			.H(SDL::UI::Value::Wh(100.f))
			.BgColor(pal::BG)
			.Scrollable(false, false)
			.AutoScrollable(false, false)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			});
		root.Children(_BuildLeft(), _BuildRight());
		root.AsRoot();
	}

	// ── Left panel ────────────────────────────────────────────────────────────
	SDL::ECS::EntityId _BuildLeft() {
		auto col = ui.Column("left", 10.f, 0.f)
			.W(310.f)
			.PaddingH(10.f).PaddingV(10.f)
			.BgColor(pal::PANEL)
			.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); s.radius = SDL::FCorners(0.f); });

		col.Children(_BuildDeviceCard(), _BuildParamCard(), _BuildStatusArea());
		return col;
	}

	SDL::ECS::EntityId _BuildDeviceCard() {
		auto card = ui.Card("card_dev");
		card.Child(ui.SectionTitle("Input device"));

		// ListBox for devices
		{
			std::vector<std::string> names;
			names.reserve(m_devices.size());
			for (size_t i = 0; i < m_devices.size(); ++i) {
				try {
					names.emplace_back(SDL::GetAudioDeviceName(m_devices[i]));
				} catch (...) {
					names.emplace_back(std::format("Device {}", i));
				}
			}
			auto lb = ui.ListBoxWidget("lbDevices", names)
				.H(140.f)
				.OnChange([this](float idx){
					int i = (int)idx;
					_OpenDevice(i);
				});
			m_lbDevices = lb.Id();
			card.Child(lb);
		}

		// Refresh button
		auto btnRefresh = ui.Button("btn_refresh", "Refresh list")
			.H(28.f)
			.BgColor(SDL::Color{30, 34, 52, 255})
			.BgHover(SDL::Color{42, 46, 68, 255})
			.BgPress(SDL::Color{20, 24, 38, 255})
			.BorderColor(pal::BORDER)
			.TextColor(pal::GREY)
			.FontSize(13.f)
			.Radius(SDL::FCorners(4.f))
			.OnClick([this]{ _Enumerate(); });
		card.Child(btnRefresh);

		// Selected device name label
		m_lblDevice = ui.Label("lbl_device_name", "(none selected)")
			.TextColor(pal::ACCENT)
			.FontSize(13.f)
			.Id();
		card.Child(ui.Label("lbl_dev_hdr", "Active: ").TextColor(pal::GREY).FontSize(12.f));
		card.Child(m_lblDevice);

		return card;
	}

	SDL::ECS::EntityId _BuildParamCard() {
		auto card = ui.Card("card_params");
		card.Child(ui.SectionTitle("Analysis parameters"));

		// ── Gain ──────────────────────────────────────────────────────────────
		m_lblGain = ui.Label("lbl_gain", std::format("Gain: {:.1f}x", m_gain))
			.TextColor(pal::WHITE).FontSize(13.f).Id();
		auto sldGain = ui.Slider("sld_gain", 0.5f, 20.f, m_gain)
			.H(18.f).FillColor(pal::GREEN)
			.OnChange([this](float v){
				m_gain = v;
				ui.SetText(m_lblGain, std::format("Gain: {:.1f}x", v));
			});
		card.Children(m_lblGain, sldGain);

		card.Child(ui.Separator("sep_gain"));

		// ── Freq Min ─────────────────────────────────────────────────────────
		m_lblFMin = ui.Label("lbl_fmin", std::format("Freq min: {:.0f} Hz", m_freqMin))
			.TextColor(pal::WHITE).FontSize(13.f).Id();
		auto sldFMin = ui.Slider("sld_fmin", 0.f, 4000.f, m_freqMin)
			.H(18.f).FillColor(pal::ACCENT)
			.OnChange([this](float v){
				m_freqMin = v;
				if (m_freqMin >= m_freqMax - 200.f)
					m_freqMin = SDL::Max(0.f, m_freqMax - 200.f);
				ui.SetText(m_lblFMin, std::format("Freq min: {:.0f} Hz", m_freqMin));
			});
		card.Children(m_lblFMin, sldFMin);

		// ── Freq Max ─────────────────────────────────────────────────────────
		m_lblFMax = ui.Label("lbl_fmax", std::format("Freq max: {:.0f} Hz", m_freqMax))
			.TextColor(pal::WHITE).FontSize(13.f).Id();
		auto sldFMax = ui.Slider("sld_fmax", 1000.f, 20000.f, m_freqMax)
			.H(18.f).FillColor(pal::ACCENT)
			.OnChange([this](float v){
				m_freqMax = v;
				if (m_freqMax <= m_freqMin + 200.f)
					m_freqMax = SDL::Min(20000.f, m_freqMin + 200.f);
				ui.SetText(m_lblFMax, std::format("Freq max: {:.0f} Hz", m_freqMax));
			});
		card.Children(m_lblFMax, sldFMax);

		card.Child(ui.Separator("sep_freq"));

		// ── Sample size ───────────────────────────────────────────────────────
		card.Child(ui.Label("lbl_sz_hdr", "Sample size:").TextColor(pal::GREY).FontSize(12.f));
		{
			auto row = ui.Row("row_sz", 4.f, 0.f)
				.Style(SDL::UI::Theme::Transparent())
				.H(30.f);
			for (int i = 0; i < kNumSizes; ++i) {
				bool active = (kSampleSizes[i] == m_sampleSize);
				auto btn = ui.Button(std::format("btn_sz{}", i),
									 std::to_string(kSampleSizes[i]))
					.H(26.f)
					.Grow(100.f)
					.FontSize(12.f)
					.Radius(SDL::FCorners(3.f))
					.BgColor (active ? pal::ACCENT  : SDL::Color{28, 32, 48, 255})
					.BgHover (active ? pal::ACCENTH : SDL::Color{40, 44, 62, 255})
					.BgPress (active ? pal::ACCENTP : SDL::Color{20, 24, 36, 255})
					.BorderColor(pal::BORDER)
					.TextColor(active ? pal::WHITE : pal::GREY)
					.OnClick([this, i]{ _SelectSampleSize(i); });
				m_szBtns[i] = btn.Id();
				row.Child(btn);
			}
			card.Child(row);
		}

		card.Child(ui.Separator("sep_sz"));

		// ── Sample rate ───────────────────────────────────────────────────────
		card.Child(ui.Label("lbl_ra_hdr", "Sample rate (Hz):").TextColor(pal::GREY).FontSize(12.f));
		{
			auto row = ui.Row("row_ra", 4.f, 0.f)
				.Style(SDL::UI::Theme::Transparent())
				.H(30.f);
			for (int i = 0; i < kNumRates; ++i) {
				bool active = (kSampleRates[i] == (int)m_sampleRate);
				std::string lbl = (kSampleRates[i] >= 1000)
					? std::format("{}k", kSampleRates[i] / 1000)
					: std::to_string(kSampleRates[i]);
				auto btn = ui.Button(std::format("btn_ra{}", i), lbl)
					.H(26.f)
					.Grow(100.f)
					.FontSize(12.f)
					.Radius(SDL::FCorners(3.f))
					.BgColor (active ? pal::ORANGE : SDL::Color{28, 32, 48, 255})
					.BgHover (active ? SDL::Color{245, 165, 50, 255} : SDL::Color{40, 44, 62, 255})
					.BgPress (active ? SDL::Color{200, 120, 20, 255} : SDL::Color{20, 24, 36, 255})
					.BorderColor(pal::BORDER)
					.TextColor(active ? pal::WHITE : pal::GREY)
					.OnClick([this, i]{ _SelectSampleRate(i); });
				m_raBtns[i] = btn.Id();
				row.Child(btn);
			}
			card.Child(row);
		}

		card.Child(ui.Separator("sep_ra"));

		// ── Window function ───────────────────────────────────────────────────
		card.Child(ui.Label("lbl_win_hdr", "Fenêtre spectrale:").TextColor(pal::GREY).FontSize(12.f));
		{
			constexpr SDL::Color kActBg  = {110,  60, 210, 255};
			constexpr SDL::Color kActBgH = {135,  80, 235, 255};
			constexpr SDL::Color kActBgP = { 85,  45, 175, 255};

			auto row = ui.Row("row_win", 4.f, 0.f)
				.Style(SDL::UI::Theme::Transparent())
				.H(30.f);
			for (int i = 0; i < kNumWindows; ++i) {
				bool active = (kWindows[i].type == m_windowType);
				auto btn = ui.Button(std::format("btn_win{}", i), kWindows[i].label)
					.H(26.f)
					.Grow(100.f)
					.FontSize(12.f)
					.Radius(SDL::FCorners(3.f))
					.BgColor (active ? kActBg  : SDL::Color{ 28,  32,  48, 255})
					.BgHover (active ? kActBgH : SDL::Color{ 40,  44,  62, 255})
					.BgPress (active ? kActBgP : SDL::Color{ 20,  24,  36, 255})
					.BorderColor(pal::BORDER)
					.TextColor(active ? pal::WHITE : pal::GREY)
					.OnClick([this, i]{ _SelectWindowType(i); });
				m_winBtns[i] = btn.Id();
				row.Child(btn);
			}
			card.Child(row);
		}

		return card;
	}

	SDL::ECS::EntityId _BuildStatusArea() {
		m_lblStatus = ui.Label("lbl_status",
			std::format("{} device(s) found — select one above", m_devices.size()))
			.TextColor(pal::GREY)
			.FontSize(12.f)
			.Id();
		return m_lblStatus;
	}

	// ── Right panel ───────────────────────────────────────────────────────────
	SDL::ECS::EntityId _BuildRight() {
		auto col = ui.Column("right", 10.f, 0.f)
			.Grow(100.f)
			.Padding(10.f)
			.BgColor(pal::BG)
			.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); s.radius = SDL::FCorners(0.f); });

		col.Children(_BuildWaveCard(), _BuildSpecCard());
		return col;
	}

	SDL::ECS::EntityId _BuildWaveCard() {
		auto card = ui.Card("card_wave");
		card.Child(ui.SectionTitle("Waveform"));

		auto graph = ui.GradedGraph("graphWave")
			.Grow(100.f)
			.H(SDL::UI::Value::Pch(50.f)-50.0f);
		m_graphWave = graph.Id();
		card.Child(graph);

		if (auto* gd = ui.GetGraphData(m_graphWave)) {
			gd->minVal      = -1.f;
			gd->maxVal      =  1.f;
			gd->xMin        =  0.f;
			gd->xMax        =  50.f;
			gd->xDivisions  =  8;
			gd->yDivisions  =  4;
			gd->showFill    = false;
			gd->barMode     = false;
			gd->logFreq     = false;
			gd->lineColor   = pal::GREEN;
			gd->fillColor   = SDL::Color{45, 220, 140, 40};
			gd->xLabel      = "ms";
			gd->yLabel      = "amp";
			gd->title       = "";
		}
		return card;
	}

	SDL::ECS::EntityId _BuildSpecCard() {
		auto card = ui.Card("card_spec");
		card.Child(ui.SectionTitle("Frequency spectrum"));

		auto graph = ui.GradedGraph("graphSpec")
			.Grow(100.f)
			.H(SDL::UI::Value::Pch(50.f)-50.0f);
		m_graphSpec = graph.Id();
		card.Child(graph);

		if (auto* gd = ui.GetGraphData(m_graphSpec)) {
			gd->minVal      = -80.f;
			gd->maxVal      =   0.f;
			gd->xMin        =   0.f;
			gd->xMax        =  20000.f;
			gd->xDivisions  =  8;
			gd->yDivisions  =  5;
			gd->showFill    = true;
			gd->barMode     = false;
			gd->logFreq     = true;
			gd->lineColor   = pal::ACCENT;
			gd->fillColor   = SDL::Color{70, 130, 220, 55};
			gd->xLabel      = "Hz";
			gd->yLabel      = "dB";
			gd->title       = "";
		}
		return card;
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Button selection helpers
	// ─────────────────────────────────────────────────────────────────────────
	void _SelectSampleSize(int idx) {
		if (idx < 0 || idx >= kNumSizes) return;
		m_sampleSize = kSampleSizes[idx];
		m_pcmBuf.clear();

		// Update button highlight
		for (int i = 0; i < kNumSizes; ++i) {
			bool active = (i == idx);
			SDL::UI::Style& s = ui.GetStyle(m_szBtns[i]);
			s.bgColor   = active ? pal::ACCENT  : SDL::Color{28, 32, 48, 255};
			s.bgHovered = active ? pal::ACCENTH : SDL::Color{40, 44, 62, 255};
			s.bgPressed = active ? pal::ACCENTP : SDL::Color{20, 24, 36, 255};
			s.textColor = active ? pal::WHITE   : pal::GREY;
		}
		_SetStatus(std::format("Sample size: {}", m_sampleSize));
	}

	void _SelectSampleRate(int idx) {
		if (idx < 0 || idx >= kNumRates) return;
		m_sampleRate = static_cast<float>(kSampleRates[idx]);
		m_pcmBuf.clear();

		// Update button highlight
		for (int i = 0; i < kNumRates; ++i) {
			bool active = (i == idx);
			SDL::UI::Style& s = ui.GetStyle(m_raBtns[i]);
			s.bgColor   = active ? pal::ORANGE                    : SDL::Color{28, 32, 48, 255};
			s.bgHovered = active ? SDL::Color{245, 165,  50, 255} : SDL::Color{40, 44, 62, 255};
			s.bgPressed = active ? SDL::Color{200, 120,  20, 255} : SDL::Color{20, 24, 36, 255};
			s.textColor = active ? pal::WHITE : pal::GREY;
		}

		// Re-open device with new rate
		_ReopenDevice();
		_SetStatus(std::format("Sample rate: {} Hz", kSampleRates[idx]));
	}

	void _SelectWindowType(int idx) {
		if (idx < 0 || idx >= kNumWindows) return;
		m_windowType = kWindows[idx].type;

		constexpr SDL::Color kActBg  = {110,  60, 210, 255};
		constexpr SDL::Color kActBgH = {135,  80, 235, 255};
		constexpr SDL::Color kActBgP = { 85,  45, 175, 255};

		for (int i = 0; i < kNumWindows; ++i) {
			bool active = (i == idx);
			SDL::UI::Style& s = ui.GetStyle(m_winBtns[i]);
			s.bgColor   = active ? kActBg  : SDL::Color{ 28,  32,  48, 255};
			s.bgHovered = active ? kActBgH : SDL::Color{ 40,  44,  62, 255};
			s.bgPressed = active ? kActBgP : SDL::Color{ 20,  24,  36, 255};
			s.textColor = active ? pal::WHITE : pal::GREY;
		}
		_SetStatus(std::format("Fenêtre: {}", kWindows[idx].label));
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
