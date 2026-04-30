/**
 * @file 06_signal-generator.cpp
 * @brief Signal generator with UI — 4 parameterizable oscillators.
 *
 * Uses SDL3pp_audioProcessing for signal synthesis, FFT spectrum analysis,
 * RMS/Peak metering, and waveform display.
 *
 * Left panel  — output device selector + 4 oscillator cards
 *               (shape, frequency, amplitude, on/off per oscillator)
 * Right panel — waveform graph + FFT spectrum + level meters
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

#include <cstdlib>
#include <deque>
#include <format>
#include <span>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
//  Colour palette
// ─────────────────────────────────────────────────────────────────────────────
namespace pal {
	constexpr SDL::Color BG      = {  10,  12,  20, 255}; ///< Background color
	constexpr SDL::Color BORDER  = {  40,  44,  64, 255}; ///< Border color
	constexpr SDL::Color PANEL   = {  16,  18,  28, 255}; ///< Panel background color
	constexpr SDL::Color ACCENT  = {  60, 140, 220, 255}; ///< Accent color
	constexpr SDL::Color ACCENTH = {  85, 165, 245, 255}; ///< Accent hovered color
	constexpr SDL::Color ACCENTP = {  40, 110, 190, 255}; ///< Accent pressed color
	constexpr SDL::Color GREEN   = {  45, 195, 110, 255}; ///< Green color
	constexpr SDL::Color GREENH  = {  70, 220, 135, 255}; ///< Green hovered color
	constexpr SDL::Color ORANGE  = { 230, 145,  30, 255}; ///< Orange color
	constexpr SDL::Color ORANGEH = { 245, 165,  50, 255}; ///< Orange hovered color
	constexpr SDL::Color RED     = { 200,  60,  50, 255}; ///< Red color
	constexpr SDL::Color REDH    = { 220,  80,  65, 255}; ///< Red hovered color
	constexpr SDL::Color PURPLE  = { 155,  75, 220, 255}; ///< Purple color
	constexpr SDL::Color PURPLEH = { 175, 100, 235, 255}; ///< Purple hovered color
	constexpr SDL::Color WHITE   = { 220, 220, 228, 255}; ///< White color
	constexpr SDL::Color GREY    = { 120, 125, 145, 255}; ///< Grey color
}

// ─────────────────────────────────────────────────────────────────────────────
//  Oscillator shape
// ─────────────────────────────────────────────────────────────────────────────
enum class OscShape { Sine = 0, Square, Triangle, Sawtooth, Noise, COUNT };
static constexpr int kNumShapes = static_cast<int>(OscShape::COUNT);

static constexpr const char* kShapeLabels[kNumShapes] = {
	"Sine", "Carré", "Tri", "Dent", "Bruit"
};
// Active-state colour for each shape button
static constexpr SDL::Color kShapeColor[kNumShapes] = {
	pal::ACCENT,   // Sine     - blue
	pal::GREEN,    // Square   - green
	pal::ORANGE,   // Triangle - orange
	pal::RED,      // Sawtooth - red
	pal::PURPLE,   // Noise    - purple
};
static constexpr SDL::Color kShapeColorH[kNumShapes] = {
	pal::ACCENTH,
	pal::GREENH,
	pal::ORANGEH,
	pal::REDH,
	pal::PURPLEH,
};
static constexpr SDL::Color kShapeColorP[kNumShapes] = {
	{ 40, 110, 190, 255},
	{ 30, 160,  85, 255},
	{200, 120,  20, 255},
	{165,  45,  35, 255},
	{120,  55, 175, 255},
};

// Per-oscillator accent colours (used for freq/amp sliders)
static constexpr int kNumOsc = 4;
static constexpr SDL::Color kOscAccent[kNumOsc] = {
	pal::ACCENT,
	pal::GREEN,
	pal::ORANGE,
	pal::PURPLE,
};

// ─────────────────────────────────────────────────────────────────────────────
//  Oscillator — sample-by-sample synthesis with phase accumulator
// ─────────────────────────────────────────────────────────────────────────────
struct Oscillator {
	bool     enabled   = false;
	OscShape shape     = OscShape::Sine;
	float    freq      = 440.f;
	float    amplitude = 0.5f;
	float    phase     = 0.f;   // radians, [0, 2π)

	float nextSample(float sr) noexcept {
		if (!enabled) return 0.f;
		float s = _wave(shape, phase);
		phase += 2.f * SDL::PI_F * freq / sr;
		if (phase >= 2.f * SDL::PI_F) phase -= 2.f * SDL::PI_F;
		return s * amplitude;
	}

	// Reset phase (called when oscillator is re-enabled)
	void resetPhase() noexcept { phase = 0.f; }

private:
	static float _wave(OscShape sh, float p) noexcept {
		// p is phase in [0, 2π)
		const float t = p * (0.5f / SDL::PI_F);  // [0, 1)
		switch (sh) {
			case OscShape::Sine:
				return SDL::Sin(p);
			case OscShape::Square:
				// Band-limited: sum of odd harmonics (truncated Fourier series)
				{
					float s = 0.f;
					for (int h = 1; h <= 31; h += 2)
						s += (1.f / static_cast<float>(h)) * SDL::Sin(static_cast<float>(h) * p);
					return s * (4.f / SDL::PI_F);
				}
			case OscShape::Triangle:
				// Band-limited: alternating odd harmonics
				{
					float s = 0.f, sgn = 1.f;
					for (int h = 1; h <= 31; h += 2, sgn = -sgn)
						s += sgn / static_cast<float>(h * h) * SDL::Sin(static_cast<float>(h) * p);
					return s * (8.f / (SDL::PI_F * SDL::PI_F));
				}
			case OscShape::Sawtooth:
				// Band-limited: sum of all harmonics
				{
					float s = 0.f;
					for (int h = 1; h <= 32; ++h)
						s += ((h & 1) ? -1.f : 1.f) / static_cast<float>(h) * SDL::Sin(static_cast<float>(h) * p);
					return s * (2.f / SDL::PI_F);
				}
			case OscShape::Noise:
				// White noise — independent of phase
				return (static_cast<float>(std::rand()) / static_cast<float>(RAND_MAX)) * 2.f - 1.f;
			default:
				return 0.f;
		}
		(void)t;
	}
};

// ─────────────────────────────────────────────────────────────────────────────
//  Audio constants
// ─────────────────────────────────────────────────────────────────────────────
static constexpr float kSampleRate   = 44100.f;
static constexpr int   kBlockSize    = 1024;        // samples generated per fill
static constexpr float kBufferSec    = 0.10f;       // keep ≥ this ahead in stream
static constexpr int   kSpecBufSize  = 4096;        // samples kept for spectrum

// ─────────────────────────────────────────────────────────────────────────────
//  Main application
// ─────────────────────────────────────────────────────────────────────────────
struct Main {
	static constexpr SDL::Point kWinSz = {1280, 720};

	static SDL::AppResult Init(Main** out, SDL::AppArgs /*args*/) {
		SDL::SetAppMetadata("SDL3pp Signal Generator", "1.0",
							"com.example.audio-signal-generator");
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
								  "SDL3pp - Signal Generator",
								  kWinSz, SDL_WINDOW_RESIZABLE, nullptr) };
	SDL::RendererRef renderer{ window.GetRenderer() };

	// ── UI ────────────────────────────────────────────────────────────────────
	SDL::ResourceManager rm;
	SDL::ResourcePool&   pool{ *rm.CreatePool("ui") };
	SDL::ECS::Context      ecs_context;
	SDL::UI::System      ui{ ecs_context, renderer, SDL::MixerRef{nullptr}, pool };
	SDL::FrameTimer      timer{ 60.f };

	// ── UI entity IDs ─────────────────────────────────────────────────────────
	SDL::ECS::EntityId m_lbDevices   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblStatus   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_graphWave   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_graphSpec   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_progRMS     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_progPeak    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblRMS      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblPeak     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_sldMaster   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId m_lblMaster   = SDL::ECS::NullEntity;

	// Per-oscillator UI entity IDs
	struct OscUI {
		SDL::ECS::EntityId tog                         = SDL::ECS::NullEntity;
		SDL::ECS::EntityId shapeBtns[kNumShapes]       = {};
		SDL::ECS::EntityId sldFreq                     = SDL::ECS::NullEntity;
		SDL::ECS::EntityId sldAmp                      = SDL::ECS::NullEntity;
		SDL::ECS::EntityId lblFreq                     = SDL::ECS::NullEntity;
		SDL::ECS::EntityId lblAmp                      = SDL::ECS::NullEntity;
	};
	OscUI m_oscUI[kNumOsc];

	// ── Audio ─────────────────────────────────────────────────────────────────
	SDL::OwnArray<SDL::AudioDeviceRef> m_devices;
	SDL::AudioStream   m_stream;
	int                m_selDevice = -1;
	float              m_master    = 0.7f;

	Oscillator m_osc[kNumOsc];

	// ── DSP display state ─────────────────────────────────────────────────────
	std::deque<float> m_specBuf;  // rolling buffer for spectrum (main thread)
	SDL::Signal       m_waveSig;  // last generated block for waveform

	// ─────────────────────────────────────────────────────────────────────────
	Main() {
		// Default oscillator parameters — C major triad
		m_osc[0] = {false, OscShape::Sine,     261.63f, 0.5f, 0.f};
		m_osc[1] = {false, OscShape::Triangle, 329.63f, 0.5f, 0.f};
		m_osc[2] = {false, OscShape::Square,   392.00f, 0.4f, 0.f};

		_OpenDefaultDevice();

		timer.Begin();
		_BuildUI();
		_Enumerate();  // populates listbox after UI is built
	}

	~Main() {
		_CloseDevice();
		pool.Release();
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Device management
	// ─────────────────────────────────────────────────────────────────────────
	void _OpenDefaultDevice() {
		_CloseDevice();
		try {
			SDL::AudioSpec spec{};
			spec.format   = SDL::AUDIO_F32;
			spec.channels = 1;
			spec.freq     = static_cast<int>(kSampleRate);
			m_stream = SDL::AudioDeviceRef{SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK}
						   .OpenStream(spec, nullptr, nullptr);
			m_stream.ResumeDevice();
		} catch (const std::exception& e) {
			_SetStatus(std::format("Erreur device: {}", e.what()));
		}
	}

	void _OpenDevice(int idx) {
		if (idx < 0 || idx >= (int)m_devices.size()) return;
		_CloseDevice();
		m_selDevice = idx;
		try {
			SDL::AudioSpec spec{};
			spec.format   = SDL::AUDIO_F32;
			spec.channels = 1;
			spec.freq     = static_cast<int>(kSampleRate);
			m_stream = m_devices[idx].OpenStream(spec, nullptr, nullptr);
			m_stream.ResumeDevice();
			try {
				_SetStatus("Sortie: " + std::string(SDL::GetAudioDeviceName(m_devices[idx])));
			} catch (...) {
				_SetStatus(std::format("Sortie: Device {}", idx));
			}
		} catch (const std::exception& e) {
			_SetStatus(std::format("Erreur: {}", e.what()));
		}
	}

	void _CloseDevice() {
		if (m_stream) {
			m_stream.PauseDevice();
			m_stream = SDL::AudioStream{};
		}
		m_selDevice = -1;
	}

	void _Enumerate() {
		try {
			m_devices = SDL::GetAudioPlaybackDevices();
		} catch (...) {
			m_devices = {};
		}
		if (m_lbDevices != SDL::ECS::NullEntity) {
			std::vector<std::string> names;
			names.reserve(m_devices.size() + 1);
			names.emplace_back("▶ Défaut système");
			for (size_t i = 0; i < m_devices.size(); ++i) {
				try {
					names.emplace_back(SDL::GetAudioDeviceName(m_devices[i]));
				} catch (...) {
					names.emplace_back(std::format("Device {}", i));
				}
			}
			ui.SetListBoxItems(m_lbDevices, std::move(names));
		}
	}

	void _SetStatus(const std::string& s) {
		if (m_lblStatus != SDL::ECS::NullEntity) ui.SetText(m_lblStatus, s);
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Audio generation — called from Iterate() to keep stream full
	// ─────────────────────────────────────────────────────────────────────────
	void _FillStream() {
		if (!m_stream) return;

		const int minQueued = static_cast<int>(kSampleRate * kBufferSec * sizeof(float));

		while (m_stream.GetQueued() < minQueued) {
			// Build one block as an SDL::Signal (from SDL3pp_audioProcessing)
			SDL::Signal block(kBlockSize, kSampleRate);

			for (int i = 0; i < kBlockSize; ++i) {
				float s = 0.f;
				for (auto& osc : m_osc)
					s += osc.nextSample(kSampleRate);
				block[i] = s * m_master;
			}

			// Soft-clip to prevent harsh clipping artefacts
			block = SDL::ClipAudioFilter{0.98f}.Apply(block);

			// Keep display copy of waveform (last block)
			m_waveSig = block;

			// Roll spectrum buffer
			for (float v : block.samples) {
				m_specBuf.push_back(v);
				if ((int)m_specBuf.size() > kSpecBufSize)
					m_specBuf.pop_front();
			}

			// Feed to stream
			m_stream.PutData(std::span<const float>{
				block.samples.data(), block.samples.size()});
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Graph update — waveform + spectrum + meters
	// ─────────────────────────────────────────────────────────────────────────
	void _UpdateDisplays() {
		// ── Waveform ──────────────────────────────────────────────────────────
		if (!m_waveSig.Empty()) {
			ui.SetGraphData(m_graphWave, m_waveSig.samples);
			ui.SetGraphRange(m_graphWave, -1.f, 1.f);
			float dur = static_cast<float>(m_waveSig.Size()) / kSampleRate * 1000.f;
			ui.SetGraphXRange(m_graphWave, 0.f, dur);
		}

		// ── Spectrum (FFT on rolling buffer) ─────────────────────────────────
		if ((int)m_specBuf.size() >= 512) {
			int sn = std::min((int)m_specBuf.size(), kSpecBufSize);
			std::vector<float> buf(sn);
			auto it = m_specBuf.end() - sn;
			std::copy(it, m_specBuf.end(), buf.begin());

			// Hanning window
			auto win = SDL::detail::WindowHanning(sn);
			for (int i = 0; i < sn; ++i) buf[i] *= win[i];

			SDL::Signal sig{buf, kSampleRate};
			auto fftBins  = SDL::FFT(sig);
			auto dbBins   = SDL::FFTMagnitudeDB(fftBins);
			auto freqAxis = SDL::FFTFrequencies((int)fftBins.size(), kSampleRate);

			std::vector<float> specData;
			specData.reserve(freqAxis.size());
			for (int k = 0; k < (int)freqAxis.size(); ++k) {
				if (freqAxis[k] > 20000.f) break;
				specData.push_back(SDL::Max(-80.f, dbBins[k]));
			}

			ui.SetGraphData(m_graphSpec, specData);
		}

		// ── Level meters ──────────────────────────────────────────────────────
		if (!m_waveSig.Empty()) {
			float rms  = SDL::RMS(m_waveSig);
			float peak = SDL::Peak(m_waveSig);
			// Convert to dB (−80..0)
			float rmsDB  = rms  > 1e-7f ? 20.f * std::log10(rms)  : -80.f;
			float peakDB = peak > 1e-7f ? 20.f * std::log10(peak) : -80.f;
			// Normalise to [0,1] for progress bar  (-80dB → 0,  0dB → 1)
			float rmsN  = (rmsDB  + 80.f) / 80.f;
			float peakN = (peakDB + 80.f) / 80.f;
			ui.SetValue(m_progRMS,  SDL::Clamp(rmsN,  0.f, 1.f));
			ui.SetValue(m_progPeak, SDL::Clamp(peakN, 0.f, 1.f));
			ui.SetText(m_lblRMS,  std::format("RMS  {:+.1f} dB", rmsDB));
			ui.SetText(m_lblPeak, std::format("Peak {:+.1f} dB", peakDB));
		}
	}

	// ─────────────────────────────────────────────────────────────────────────
	SDL::AppResult Iterate() {
		timer.Begin();
		const float dt = timer.GetDelta();

		pool.Update();
		_FillStream();
		_UpdateDisplays();

		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();
		ui.Iterate(dt);
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

	// ─────────────────────────────────────────────────────────────────────────
	//  UI construction
	// ─────────────────────────────────────────────────────────────────────────
	void _BuildUI() {
		const std::string base = std::string(SDL::GetBasePath()) + "../../../assets/";
		ui.LoadFont("deja-vu-sans", base + "fonts/DejaVuSans.ttf");
		ui.SetDefaultFont("deja-vu-sans", 12.f);

		auto root = ui.Row("root", 0.f, 0.f)
			.W(SDL::UI::Value::Rw(100.f))
			.H(SDL::UI::Value::Rh(100.f))
			.BgColor(pal::BG)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			});
		root.Children(_BuildLeft(), _BuildRight());
		root.AsRoot();
	}

	// ── Left panel ────────────────────────────────────────────────────────────
	SDL::ECS::EntityId _BuildLeft() {
		auto col = ui.Column("left", 8.f, 0.f)
			.W(310.f)
			.PaddingH(8.f).PaddingV(8.f)
			.BgColor(pal::PANEL)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			});

		col.Child(_BuildDeviceCard());
		for (int i = 0; i < kNumOsc; ++i)
			col.Child(_BuildOscCard(i));

		m_lblStatus = ui.Label("lbl_status", "Défaut système actif")
			.TextColor(pal::GREY).FontSize(11.f).Id();
		col.Child(m_lblStatus);
		return col;
	}

	SDL::ECS::EntityId _BuildDeviceCard() {
		auto card = ui.Card("card_dev");
		card.Child(ui.SectionTitle("Sortie audio"));

		auto lb = ui.ListBoxWidget("lbDevices")
			.H(90.f)
			.OnChange<int>([this](int idx){
				if (idx == 0) _OpenDefaultDevice();
				else _OpenDevice(idx - 1);  // offset by the "Défaut" entry
			});
		m_lbDevices = lb.Id();
		card.Child(lb);

		card.Child(
			ui.Button("btn_refresh", "Rafraîchir")
				.H(24.f).FontSize(11.f)
				.BgColor({28,32,48,255}).BgHover({40,44,62,255}).BgPress({20,24,36,255})
				.BorderColor(pal::BORDER).TextColor(pal::GREY)
				.Radius(SDL::FCorners(3.f))
				.OnClick([this]{ _Enumerate(); })
		);
		return card;
	}

	SDL::ECS::EntityId _BuildOscCard(int idx) {
		std::string idBase = std::format("osc{}", idx);
		auto& oi = m_oscUI[idx];
		auto& osc = m_osc[idx];
		SDL::Color accent = kOscAccent[idx];

		auto card = ui.Card(idBase + "_card");

		// ── Header: title + toggle ────────────────────────────────────────────
		auto hdr = ui.Row(idBase + "_hdr", 6.f, 0.f)
			.Style(SDL::UI::Theme::Transparent())
			.AlignH(SDL::UI::Align::Center)
			.H(28.f);

		hdr.Child(
			ui.Label(idBase + "_title",
					 std::format("Oscillateur {}", idx + 1))
			  .TextColor(accent).FontSize(12.f).Grow(100.f)
		);

		auto tog = ui.Toggle(idBase + "_tog", "")
			.W(40.f).H(20.f)
			.AlignH(SDL::UI::Align::Right)
			.OnToggle([this, idx](bool on){
				m_osc[idx].enabled = on;
				if (on) m_osc[idx].resetPhase();
			});
		oi.tog = tog.Id();
		hdr.Child(tog);
		card.Child(hdr);

		// ── Shape buttons ─────────────────────────────────────────────────────
		card.Child(
			ui.Label(idBase + "_shape_hdr", "Forme:")
			  .TextColor(pal::GREY).FontSize(11.f)
		);
		auto shapeRow = ui.Row(idBase + "_shapes", 3.f, 0.f)
			.Style(SDL::UI::Theme::Transparent()).H(24.f);

		for (int s = 0; s < kNumShapes; ++s) {
			bool active = (osc.shape == static_cast<OscShape>(s));
			auto btn = ui.Button(std::format("{}sh{}", idBase, s), kShapeLabels[s])
				.H(22.f).Grow(100.f).FontSize(10.f)
				.Radius(SDL::FCorners(3.f))
				.BorderColor(pal::BORDER)
				.BgColor (active ? kShapeColor[s]  : SDL::Color{28, 32, 48, 255})
				.BgHover (active ? kShapeColorH[s] : SDL::Color{40, 44, 62, 255})
				.BgPress (active ? kShapeColorP[s] : SDL::Color{20, 24, 36, 255})
				.TextColor(active ? pal::WHITE : pal::GREY)
				.OnClick([this, idx, s]{ _SelectShape(idx, s); });
			oi.shapeBtns[s] = btn.Id();
			shapeRow.Child(btn);
		}
		card.Child(shapeRow);

		// ── Frequency ─────────────────────────────────────────────────────────
		oi.lblFreq = ui.Label(idBase + "_flbl",
							  std::format("Fréq: {:.0f} Hz", osc.freq))
			.TextColor(pal::WHITE).FontSize(11.f).Id();
		card.Child(oi.lblFreq);

		auto sldFreq = ui.Slider(idBase + "_sfreq", 20.f, 4000.f, osc.freq)
			.H(16.f).FillColor(accent)
			.OnChange<float>([this, idx](float v){
				m_osc[idx].freq = v;
				ui.SetText(m_oscUI[idx].lblFreq, std::format("Fréq: {:.0f} Hz", v));
			});
		oi.sldFreq = sldFreq.Id();
		card.Child(sldFreq);

		// ── Amplitude ─────────────────────────────────────────────────────────
		oi.lblAmp = ui.Label(idBase + "_albl",
							 std::format("Amp: {:.2f}", osc.amplitude))
			.TextColor(pal::WHITE).FontSize(11.f).Id();
		card.Child(oi.lblAmp);

		auto sldAmp = ui.Slider(idBase + "_samp", 0.f, 1.f, osc.amplitude)
			.H(16.f).FillColor(accent)
			.OnChange<float>([this, idx](float v){
				m_osc[idx].amplitude = v;
				ui.SetText(m_oscUI[idx].lblAmp, std::format("Amp: {:.2f}", v));
			});
		oi.sldAmp = sldAmp.Id();
		card.Child(sldAmp);

		return card;
	}

	// ── Right panel ───────────────────────────────────────────────────────────
	SDL::ECS::EntityId _BuildRight() {
		auto col = ui.Column("right", 8.f, 0.f)
			.Grow(100.f)
			.PaddingH(10.f).PaddingV(8.f)
			.BgColor(pal::BG)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			});

		col.Children(
			_BuildMasterRow(),
			_BuildWaveCard(),
			_BuildSpecCard(),
			_BuildMeterRow()
		);
		return col;
	}

	SDL::ECS::EntityId _BuildMasterRow() {
		auto row = ui.Row("row_master", 8.f, 0.f)
			.Style(SDL::UI::Theme::Transparent())
			.H(28.f)
			.W(SDL::UI::Value::Pw(50.f))
			.AlignH(SDL::UI::Align::Center);

		m_lblMaster = ui.Label("lbl_master",
							   std::format("Volume: {:.0f}%", m_master * 100.f))
			.TextColor(pal::WHITE).FontSize(12.f).W(100.f).Id();

		auto sld = ui.Slider("sld_master", 0.f, 1.f, m_master)
			.Grow(100.f).H(16.f).FillColor(pal::ACCENT)
			.OnChange<float>([this](float v){
				m_master = v;
				ui.SetText(m_lblMaster, std::format("Volume: {:.0f}%", v * 100.f));
			});
		m_sldMaster = sld.Id();

		row.Children(m_lblMaster, sld);
		return row;
	}

	SDL::ECS::EntityId _BuildWaveCard() {
		auto card = ui.Card("card_wave");
		card.Child(ui.SectionTitle("Forme d'onde — signal composite"));

		auto graph = ui.GradedGraph("graphWave")
			.Grow(100.f)
			.H(SDL::UI::Value::Pch(30.f));
		m_graphWave = graph.Id();
		card.Child(graph);

		if (auto* gd = ui.GetGraphData(m_graphWave)) {
			gd->minVal     = -1.f; gd->maxVal = 1.f;
			gd->xMin       =  0.f; gd->xMax   = 23.f;  // ~1024/44100*1000
			gd->xDivisions = 8;    gd->yDivisions = 4;
			gd->showFill   = false; gd->barMode = false; gd->logFreq = false;
			gd->lineColor  = pal::ACCENT;
			gd->fillColor  = SDL::Color{60, 140, 220, 40};
			gd->xLabel     = "ms"; gd->yLabel = "amp";
		}
		return card;
	}

	SDL::ECS::EntityId _BuildSpecCard() {
		auto card = ui.Card("card_spec");
		card.Child(ui.SectionTitle("Spectre FFT (Hanning, 4096 pts)"));

		auto graph = ui.GradedGraph("graphSpec")
			.Grow(100.f)
			.H(SDL::UI::Value::Pch(30.f));
		m_graphSpec = graph.Id();
		card.Child(graph);

		if (auto* gd = ui.GetGraphData(m_graphSpec)) {
			gd->minVal     = -80.f; gd->maxVal = 0.f;
			gd->xMin       =   0.f; gd->xMax   = 20000.f;
			gd->xDivisions = 8;     gd->yDivisions = 5;
			gd->showFill   = true;  gd->barMode = false; gd->logFreq = true;
			gd->lineColor  = pal::ACCENT;
			gd->fillColor  = SDL::Color{60, 140, 220, 50};
			gd->xLabel     = "Hz";  gd->yLabel = "dB";
		}
		return card;
	}

	SDL::ECS::EntityId _BuildMeterRow() {
		auto card = ui.Card("card_meters");
		card.Child(ui.SectionTitle("Niveaux"));

		// RMS
		m_lblRMS = ui.Label("lbl_rms", "RMS  -80.0 dB")
			.TextColor(pal::WHITE).FontSize(11.f).W(120.f).Id();
		m_progRMS = ui.Progress("prog_rms", 0.f, 1.f)
			.Grow(100.f).H(10.f).FillColor(pal::GREEN).Id();
		auto rmsRow = ui.Row("rms_row", 8.f, 0.f)
			.Style(SDL::UI::Theme::Transparent())
			.W(SDL::UI::Value::Pw(70.f))
			.H(20.f)
			.Children(m_lblRMS, m_progRMS);

		// Peak
		m_lblPeak = ui.Label("lbl_peak", "Peak -80.0 dB")
			.TextColor(pal::WHITE).FontSize(11.f).W(120.f).Id();
		m_progPeak = ui.Progress("prog_peak", 0.f, 1.f)
			.Grow(100.f).H(10.f).FillColor(pal::RED).Id();
		auto peakRow = ui.Row("peak_row", 8.f, 0.f)
			.Style(SDL::UI::Theme::Transparent())
			.W(SDL::UI::Value::Pw(70.f))
			.H(20.f)
			.Children(m_lblPeak, m_progPeak);

		card.Children(rmsRow, peakRow);
		return card;
	}

	// ─────────────────────────────────────────────────────────────────────────
	//  Shape selection
	// ─────────────────────────────────────────────────────────────────────────
	void _SelectShape(int oscIdx, int shapeIdx) {
		if (oscIdx < 0 || oscIdx >= kNumOsc || shapeIdx < 0 || shapeIdx >= kNumShapes) return;
		m_osc[oscIdx].shape = static_cast<OscShape>(shapeIdx);

		auto& oi = m_oscUI[oscIdx];
		for (int s = 0; s < kNumShapes; ++s) {
			bool active = (s == shapeIdx);
			SDL::UI::Style& st = ui.GetStyle(oi.shapeBtns[s]);
			st.bgColor   = active ? kShapeColor[s]  : SDL::Color{28, 32, 48, 255};
			st.bgHoveredColor = active ? kShapeColorH[s] : SDL::Color{40, 44, 62, 255};
			st.bgPressedColor = active ? kShapeColorP[s] : SDL::Color{20, 24, 36, 255};
			st.textColor = active ? pal::WHITE       : pal::GREY;
		}
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
