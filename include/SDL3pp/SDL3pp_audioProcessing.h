#ifndef SDL3PP_AUDIO_PROCESSING_H_
#define SDL3PP_AUDIO_PROCESSING_H_

/**
 * @file SDL3pp_audio_processing.h
 * @brief Audio signal processing toolkit for SDL3pp.
 *
 * ## Overview
 *
 * All processing operates on an `SDL::Signal` — a plain container holding a
 * `std::vector<float>` of PCM samples plus a sample rate.  Every filter and
 * generator returns a new Signal, leaving the original unchanged.
 *
 * No external DSP library is required.  FFT, all filters, generators and
 * demodulators are implemented from first principles.
 *
 * ## Categories
 *
 * ### A. Signal generators
 * Produce a new Signal filled with a periodic or stochastic waveform.
 * - GenerateSine          – pure sinusoid
 * - GenerateSquare        – band-limited square (Fourier series, N harmonics)
 * - GenerateTriangle      – band-limited triangle
 * - GenerateSawtooth      – band-limited sawtooth
 * - GeneratePulse         – variable duty-cycle rectangular pulse
 * - GenerateNoise         – uniform white noise
 * - GenerateHarmonics     – additive synthesis from amplitude table
 * - GenerateChirp         – linear frequency sweep (chirp)
 * - GenerateSilence       – zero-filled buffer
 *
 * ### B. Spectral analysis (FFT)
 * - FFT                   – Cooley-Tukey radix-2 DIT (returns complex spectrum)
 * - IFFT                  – inverse FFT (complex → real)
 * - FFTMagnitude          – |X[k]| for each bin
 * - FFTMagnitudeDB        – 20·log10|X[k]| (dB spectrum)
 * - FFTPhase              – ∠X[k] in radians
 * - FFTFrequencies        – frequency axis for each bin
 * - FFTPowerSpectrum      – |X[k]|² / N²  (one-sided)
 * - ComputeSpectrum       – convenience: FFT + magnitude of a Signal
 *
 * ### C. IIR filters (BiQuad, Direct Form I)
 * Each filter struct both carries coefficients and applies them.
 * - LowPassAudioFilter    – 2nd-order Butterworth LP  (Fc, Q)
 * - HighPassAudioFilter   – 2nd-order Butterworth HP  (Fc, Q)
 * - BandPassAudioFilter   – 2nd-order constant-skirt BP (Fc, Q)
 * - BandStopAudioFilter   – 2nd-order band-reject (Fc, Q)
 * - NotchAudioFilter      – deep notch at Fc (Fc, Q)
 * - PeakingEQAudioFilter  – peaking / dip bell curve (Fc, Q, gain dB)
 * - LowShelfAudioFilter   – first-order low shelf (Fc, gain dB)
 * - HighShelfAudioFilter  – first-order high shelf (Fc, gain dB)
 * - AllPassAudioFilter    – phase-shifting all-pass (Fc, Q)
 *
 * ### D. FIR filters (windowed-sinc)
 * - FIRLowPassAudioFilter   – low-pass  (order, Fc, window)
 * - FIRHighPassAudioFilter  – high-pass (order, Fc, window)
 * - FIRBandPassAudioFilter  – band-pass (order, Flo, Fhi, window)
 * - FIRBandStopAudioFilter  – band-stop (order, Flo, Fhi, window)
 *
 * ### E. Time-domain effects
 * - GainAudioFilter       – multiply by constant factor (dB or linear)
 * - NormalizeAudioFilter  – scale to peak amplitude 1.0
 * - DCRemoveAudioFilter   – subtract mean (remove DC offset)
 * - ClipAudioFilter       – hard clip at ±threshold
 * - DelayAudioFilter      – integer sample delay (FIR identity shifted)
 * - FadeInAudioFilter     – cosine fade-in over N samples
 * - FadeOutAudioFilter    – cosine fade-out over N samples
 *
 * ### F. Demodulation
 * - AMDemodAudioFilter    – envelope detection AM   (carrier Fc)
 * - FMDemodAudioFilter    – phase-differentiator FM (carrier Fc, Δf)
 * - SSBDemodAudioFilter   – single-sideband via Hilbert transform (Fc)
 *
 * ### G. Signal analysis helpers
 * - RMS                   – root-mean-square energy
 * - Peak                  – absolute peak sample value
 * - SNR                   – signal-to-noise ratio in dB
 * - THD                   – total harmonic distortion at fundamental Fc
 * - Autocorrelation       – normalised circular autocorrelation → Signal
 *
 * ### H. SDL Renderer drawing helpers
 * - DrawSignal            – waveform in a FRect viewport
 * - DrawSpectrum          – magnitude / dB spectrum bar graph
 * - DrawSpectrogram       – rolling STFT spectrogram (colour map)
 *
 * ## Usage
 *
 * ```cpp
 * // Generate 440 Hz A-note with 4 harmonics at 44100 Hz for 0.5 s
 * SDL::Signal sig = SDL::GenerateHarmonics(440.f, {1.f, 0.5f, 0.25f, 0.12f},
 *                                          44100.f, 0.5f);
 *
 * // Apply a low-pass IIR filter at 2000 Hz
 * SDL::Signal filtered = SDL::ApplyAudioFilter(sig,
 *                            SDL::LowPassAudioFilter{2000.f, sig.sampleRate});
 *
 * // Chain filters
 * SDL::Signal out = SDL::ApplyAudioFilters(sig,
 *     SDL::DCRemoveAudioFilter{},
 *     SDL::NormalizeAudioFilter{},
 *     SDL::LowPassAudioFilter{8000.f, sig.sampleRate});
 *
 * // FFT analysis
 * auto spec   = SDL::ComputeSpectrum(sig);
 * auto freqs  = SDL::FFTFrequencies((int)spec.size(), sig.sampleRate);
 * auto specDB = SDL::FFTMagnitudeDB(SDL::FFT(sig));
 *
 * // Draw in SDL renderer
 * SDL::DrawSignal(renderer, sig,   SDL::FRect{10,10,780,180}, {0,255,0,255});
 * SDL::DrawSpectrum(renderer, sig, SDL::FRect{10,200,780,180}, true, {255,180,0,255});
 * ```
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <concepts>
#include <cstdint>
#include <format>
#include <numbers>
#include <numeric>
#include <random>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

#include "SDL3pp_render.h"
#include "SDL3pp_stdinc.h"

namespace SDL {

// ─────────────────────────────────────────────────────────────────────────────
// Convenience loop macros
// ─────────────────────────────────────────────────────────────────────────────
#ifndef fori
#  define fori(var,init,end)    for (int var=(init); var<(end);  ++var)
#  define fori_eq(var,init,end) for (int var=(init); var<=(end); ++var)
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Signal — core data type
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief A mono PCM audio buffer.
 *
 * Samples are stored as `float` in the range [-1, 1] (soft convention;
 * values outside that range are allowed and indicate clipping risk).
 * `sampleRate` is in Hz (e.g. 44100.f).
 */
struct Signal {
	std::vector<float> samples;
	float              sampleRate = 44100.f;

	Signal() = default;
	Signal(int n, float sr) : samples(n, 0.f), sampleRate(sr) {}
	Signal(std::vector<float> s, float sr)
		: samples(std::move(s)), sampleRate(sr) {}

	[[nodiscard]] int   Size()     const noexcept { return (int)samples.size(); }
	[[nodiscard]] float Duration() const noexcept {
		return sampleRate > 0.f ? static_cast<float>(samples.size()) / sampleRate : 0.f;
	}
	[[nodiscard]] bool Empty() const noexcept { return samples.empty(); }

	[[nodiscard]] float  operator[](int i) const noexcept { return samples[i]; }
	[[nodiscard]] float& operator[](int i)       noexcept { return samples[i]; }

	/// Time (seconds) corresponding to sample index i.
	[[nodiscard]] float TimeAt(int i) const noexcept {
		return sampleRate > 0.f ? static_cast<float>(i) / sampleRate : 0.f;
	}

	// ── In-place arithmetic ──────────────────────────────────────────────────
	Signal& operator+=(const Signal& o) {
		const int n = std::min(Size(), o.Size());
		fori (i, 0, n) samples[i] += o.samples[i];
		return *this;
	}
	Signal& operator*=(float gain) {
		for (float& s : samples) s *= gain;
		return *this;
	}
	[[nodiscard]] Signal operator+(const Signal& o) const { Signal r=*this; r+=o; return r; }
	[[nodiscard]] Signal operator*(float gain)       const { Signal r=*this; r*=gain; return r; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Complex type alias (used by FFT section)
// ─────────────────────────────────────────────────────────────────────────────

using Complex = std::complex<float>;

// ─────────────────────────────────────────────────────────────────────────────
// detail namespace — internal DSP helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

// ── Next power-of-2 ─────────────────────────────────────────────────────────
[[nodiscard]] inline int NextPow2(int n) noexcept {
	int p = 1;
	while (p < n) p <<= 1;
	return p;
}

// ── Bit-reversal permutation ─────────────────────────────────────────────────
inline void BitReversePermute(std::vector<Complex>& x) noexcept {
	const int N = (int)x.size();
	for (int i=1, j=0; i<N; ++i) {
		int bit = N >> 1;
		for (; j & bit; bit >>= 1) j ^= bit;
		j ^= bit;
		if (i < j) std::swap(x[i], x[j]);
	}
}

// ── In-place Cooley-Tukey FFT (DIT, radix-2) ────────────────────────────────
// x must have a power-of-2 length.  inverse=true → IFFT (without 1/N scaling).
inline void FFTInPlace(std::vector<Complex>& x, bool inverse) noexcept {
	const int N = (int)x.size();
	BitReversePermute(x);
	const float sign = inverse ? 1.f : -1.f;
	for (int len=2; len<=N; len<<=1) {
		const float ang = sign * 2.f * SDL::PI_F / static_cast<float>(len);
		const Complex wlen(SDL::Cos(ang), SDL::Sin(ang));
		for (int i=0; i<N; i+=len) {
			Complex w(1.f, 0.f);
			for (int j=0; j<len/2; ++j) {
				Complex u = x[i+j];
				Complex v = x[i+j+len/2] * w;
				x[i+j]        = u + v;
				x[i+j+len/2]  = u - v;
				w *= wlen;
			}
		}
	}
}

// ── Sinc function ──────────────────────────────────────────────────────────
[[nodiscard]] inline float Sinc(float x) noexcept {
	if (std::abs(x) < 1e-7f) return 1.f;
	const float px = SDL::PI_F * x;
	return SDL::Sin(px) / px;
}

// ── Window functions (all return a vector of length n) ────────────────────
[[nodiscard]] inline std::vector<float> WindowRectangular(int n) {
	return std::vector<float>(n, 1.f);
}
[[nodiscard]] inline std::vector<float> WindowHanning(int n) {
	std::vector<float> w(n);
	const float k = 2.f * SDL::PI_F / static_cast<float>(n-1);
	fori (i, 0, n) w[i] = 0.5f - 0.5f * SDL::Cos(k * static_cast<float>(i));
	return w;
}
[[nodiscard]] inline std::vector<float> WindowHamming(int n) {
	std::vector<float> w(n);
	const float k = 2.f * SDL::PI_F / static_cast<float>(n-1);
	fori (i, 0, n) w[i] = 0.54f - 0.46f * SDL::Cos(k * static_cast<float>(i));
	return w;
}
[[nodiscard]] inline std::vector<float> WindowBlackman(int n) {
	std::vector<float> w(n);
	const float k  = 2.f * SDL::PI_F / static_cast<float>(n-1);
	const float k2 = 2.f * k;
	fori (i, 0, n) {
		const float fi = static_cast<float>(i);
		w[i] = 0.42f - 0.5f*SDL::Cos(k*fi) + 0.08f*SDL::Cos(k2*fi);
	}
	return w;
}

// ── FIR kernel convolution (linear, truncated) ───────────────────────────────
[[nodiscard]] inline Signal ConvolveFIR(const Signal& in,
																				const std::vector<float>& k) {
	const int N = in.Size();
	const int M = (int)k.size();
	Signal out(N, in.sampleRate);
	fori (i, 0, N) {
		float acc = 0.f;
		fori (j, 0, M) {
			const int idx = i - j;
			if (idx >= 0) acc += k[j] * in.samples[idx];
		}
		out.samples[i] = acc;
	}
	return out;
}

// ── Build windowed-sinc LP kernel (order=M, normalised Fc=fc/sr) ─────────────
enum class WindowType { Rectangular, Hanning, Hamming, Blackman };

[[nodiscard]] inline std::vector<float>
MakeSincLP(int order, float fc, WindowType wt) {
	const int M    = (order % 2 == 0) ? order+1 : order; // ensure odd
	const int half = M / 2;
	std::vector<float> window;
	switch (wt) {
		case WindowType::Hanning:    window = WindowHanning(M);    break;
		case WindowType::Hamming:    window = WindowHamming(M);    break;
		case WindowType::Blackman:   window = WindowBlackman(M);   break;
		default:                     window = WindowRectangular(M);break;
	}
	std::vector<float> k(M);
	float sum = 0.f;
	fori (i, 0, M) {
		k[i] = Sinc(2.f * fc * static_cast<float>(i - half)) * window[i];
		sum += k[i];
	}
	for (float& v : k) v /= sum;
	return k;
}

// ── Spectral inversion: HP from LP ──────────────────────────────────────────
[[nodiscard]] inline std::vector<float>
InvertSpectrum(std::vector<float> k) {
	for (float& v : k) v = -v;
	k[k.size()/2] += 1.f;
	return k;
}

// ── First-order Hilbert approximation via FIR (for AM/SSB demod) ─────────────
[[nodiscard]] inline std::vector<float> MakeHilbert(int order) {
	const int M    = (order % 2 == 0) ? order+1 : order;
	const int half = M / 2;
	auto window    = WindowHamming(M);
	std::vector<float> k(M, 0.f);
	fori (i, 0, M) {
		if ((i - half) % 2 != 0) {
			const float n = static_cast<float>(i - half);
			k[i] = (2.f / (SDL::PI_F * n)) * window[i];
		}
	}
	return k;
}

// ── BiQuad state (Direct Form I) ─────────────────────────────────────────────
struct BiQuadState { float x1=0,x2=0,y1=0,y2=0; };

[[nodiscard]] inline float BiQuadStep(float x, float b0, float b1, float b2,
																			float a1, float a2,
																			BiQuadState& s) noexcept {
	const float y = b0*x + b1*s.x1 + b2*s.x2 - a1*s.y1 - a2*s.y2;
	s.x2=s.x1; s.x1=x; s.y2=s.y1; s.y1=y;
	return y;
}

// ── Prewarped bilinear-transform helper ──────────────────────────────────────
// w0 = 2π·Fc/Fs (digital angular frequency)
// Returns omega_a = 2·Fs·tan(w0/2) (analogue prewarp)
[[nodiscard]] inline float Prewarp(float fc, float sr) noexcept {
	const float w0 = 2.f * SDL::PI_F * fc / sr;
	return 2.f * sr * SDL::Tan(w0 * 0.5f);
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// FIR window type — public alias
// ─────────────────────────────────────────────────────────────────────────────

using AudioWindowType = detail::WindowType;

// ═════════════════════════════════════════════════════════════════════════════
//  A. SIGNAL GENERATORS
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Generate a pure sinusoid.
 * @param freq      Frequency in Hz.
 * @param amplitude Peak amplitude (0..1).
 * @param sr        Sample rate in Hz.
 * @param duration  Signal length in seconds.
 * @param phase     Initial phase in radians.
 */
[[nodiscard]] inline Signal GenerateSine(float freq, float amplitude=1.f,
																				 float sr=44100.f, float duration=1.f,
																				 float phase=0.f) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float k = 2.f * SDL::PI_F * freq / sr;
	fori (i, 0, N) s[i] = amplitude * SDL::Sin(k * static_cast<float>(i) + phase);
	return s;
}

/**
 * @brief Band-limited square wave synthesised from Fourier series.
 * @param harmonics Number of odd harmonics to include.
 */
[[nodiscard]] inline Signal GenerateSquare(float freq, float amplitude=1.f,
																					 float sr=44100.f, float duration=1.f,
																					 int harmonics=32) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float k = 2.f * SDL::PI_F * freq / sr;
	for (int h=1; h<=harmonics; h+=2) {
		const float hf = static_cast<float>(h);
		fori (i, 0, N)
			s[i] += (4.f / (SDL::PI_F * hf))
							* SDL::Sin(k * hf * static_cast<float>(i));
	}
	for (float& v : s.samples) v *= amplitude;
	return s;
}

/**
 * @brief Band-limited triangle wave synthesised from Fourier series.
 */
[[nodiscard]] inline Signal GenerateTriangle(float freq, float amplitude=1.f,
																						 float sr=44100.f, float duration=1.f,
																						 int harmonics=32) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float k  = 2.f * SDL::PI_F * freq / sr;
	const float pi2 = SDL::PI_F * SDL::PI_F;
	float sign = 1.f;
	for (int h=1; h<=harmonics; h+=2) {
		const float hf = static_cast<float>(h);
		const float c  = sign * 8.f / (pi2 * hf * hf);
		fori (i, 0, N)
			s[i] += c * SDL::Sin(k * hf * static_cast<float>(i));
		sign = -sign;
	}
	for (float& v : s.samples) v *= amplitude;
	return s;
}

/**
 * @brief Band-limited sawtooth wave synthesised from Fourier series.
 */
[[nodiscard]] inline Signal GenerateSawtooth(float freq, float amplitude=1.f,
																						 float sr=44100.f, float duration=1.f,
																						 int harmonics=32) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float k = 2.f * SDL::PI_F * freq / sr;
	for (int h=1; h<=harmonics; ++h) {
		const float hf = static_cast<float>(h);
		const float c  = (h % 2 == 0 ? 1.f : -1.f)
										 * 2.f / (SDL::PI_F * hf);
		fori (i, 0, N)
			s[i] += c * SDL::Sin(k * hf * static_cast<float>(i));
	}
	for (float& v : s.samples) v *= amplitude;
	return s;
}

/**
 * @brief Rectangular pulse train.
 * @param dutyCycle Duty cycle in [0,1].
 */
[[nodiscard]] inline Signal GeneratePulse(float freq, float amplitude=1.f,
																					float sr=44100.f, float duration=1.f,
																					float dutyCycle=0.5f) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float period = sr / freq;
	fori (i, 0, N) {
		const float phase = SDL::Fmod(static_cast<float>(i), period) / period;
		s[i] = (phase < dutyCycle) ? amplitude : -amplitude;
	}
	return s;
}

/**
 * @brief Uniform white noise in [-amplitude, +amplitude].
 * @param seed  Random seed (default 42 for reproducibility; 0 = random device).
 */
[[nodiscard]] inline Signal GenerateNoise(float amplitude=1.f,
																					float sr=44100.f, float duration=1.f,
																					unsigned int seed=42u) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	std::mt19937 rng(seed == 0u
									 ? std::random_device{}()
									 : seed);
	std::uniform_real_distribution<float> dist(-amplitude, amplitude);
	for (float& v : s.samples) v = dist(rng);
	return s;
}

/**
 * @brief Additive synthesis: sum of sinusoids with individual amplitudes.
 *
 * `amplitudes[0]` is the fundamental, `amplitudes[1]` the 2nd harmonic, etc.
 *
 * ```cpp
 * // Rich organ-like tone with 5 harmonics
 * auto sig = SDL::GenerateHarmonics(440.f, {1.f, 0.6f, 0.4f, 0.2f, 0.1f},
 *                                   44100.f, 1.f);
 * ```
 */
[[nodiscard]] inline Signal GenerateHarmonics(float fundamental,
																							std::span<const float> amplitudes,
																							float sr=44100.f, float duration=1.f) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float k = 2.f * SDL::PI_F * fundamental / sr;
	fori (h, 0, (int)amplitudes.size()) {
		const float hf = static_cast<float>(h + 1);
		const float amp = amplitudes[h];
		if (amp == 0.f) continue;
		fori (i, 0, N)
			s[i] += amp * SDL::Sin(k * hf * static_cast<float>(i));
	}
	return s;
}

/**
 * @brief Linear frequency sweep (chirp) from f0 to f1.
 */
[[nodiscard]] inline Signal GenerateChirp(float f0, float f1,
																					float amplitude=1.f,
																					float sr=44100.f, float duration=1.f) {
	const int N = static_cast<int>(sr * duration);
	Signal s(N, sr);
	const float c   = (f1 - f0) / (2.f * duration);
	const float twoPi = 2.f * SDL::PI_F;
	fori (i, 0, N) {
		const float t = static_cast<float>(i) / sr;
		s[i] = amplitude * SDL::Sin(twoPi * (f0 * t + c * t * t));
	}
	return s;
}

/**
 * @brief Zero-filled silent signal.
 */
[[nodiscard]] inline Signal GenerateSilence(float sr=44100.f, float duration=1.f) {
	return Signal(static_cast<int>(sr * duration), sr);
}

// ═════════════════════════════════════════════════════════════════════════════
//  B. SPECTRAL ANALYSIS (FFT)
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Forward FFT.
 *
 * The input is zero-padded to the next power-of-2 if necessary.
 * @returns Complex spectrum of length N (power-of-2 ≥ sig.Size()).
 */
[[nodiscard]] inline std::vector<Complex> FFT(const Signal& sig) {
	const int N = detail::NextPow2(sig.Size());
	std::vector<Complex> x(N);
	fori (i, 0, sig.Size()) x[i] = Complex(sig[i], 0.f);
	detail::FFTInPlace(x, false);
	return x;
}

/// Overload for raw float buffer.
[[nodiscard]] inline std::vector<Complex> FFT(std::span<const float> buf) {
	const int N = detail::NextPow2((int)buf.size());
	std::vector<Complex> x(N);
	fori (i, 0, (int)buf.size()) x[i] = Complex(buf[i], 0.f);
	detail::FFTInPlace(x, false);
	return x;
}

/**
 * @brief Inverse FFT.
 *
 * @returns Real part of the inverse transform, scaled by 1/N.
 */
[[nodiscard]] inline std::vector<float> IFFT(std::vector<Complex> X) {
	const int N = (int)X.size();
	detail::FFTInPlace(X, true);
	std::vector<float> out(N);
	const float invN = 1.f / static_cast<float>(N);
	fori (i, 0, N) out[i] = X[i].real() * invN;
	return out;
}

/**
 * @brief Magnitude spectrum |X[k]| for each bin.
 */
[[nodiscard]] inline std::vector<float> FFTMagnitude(const std::vector<Complex>& X) {
	std::vector<float> mag(X.size());
	fori (i, 0, (int)X.size()) mag[i] = std::abs(X[i]);
	return mag;
}

/**
 * @brief Magnitude spectrum in dB: 20·log10(|X[k]| + ε).
 */
[[nodiscard]] inline std::vector<float> FFTMagnitudeDB(const std::vector<Complex>& X) {
	std::vector<float> db(X.size());
	fori (i, 0, (int)X.size())
		db[i] = 20.f * std::log10(std::abs(X[i]) + 1e-12f);
	return db;
}

/**
 * @brief Phase spectrum ∠X[k] in radians.
 */
[[nodiscard]] inline std::vector<float> FFTPhase(const std::vector<Complex>& X) {
	std::vector<float> ph(X.size());
	fori (i, 0, (int)X.size()) ph[i] = std::arg(X[i]);
	return ph;
}

/**
 * @brief One-sided power spectral density: |X[k]|² / N².
 *
 * Returns only the first N/2+1 bins (positive frequencies).
 */
[[nodiscard]] inline std::vector<float> FFTPowerSpectrum(const std::vector<Complex>& X) {
	const int N   = (int)X.size();
	const int out = N / 2 + 1;
	const float invN2 = 1.f / static_cast<float>(N * N);
	std::vector<float> psd(out);
	fori (k, 0, out) {
		psd[k] = std::norm(X[k]) * invN2;
		if (k > 0 && k < N/2) psd[k] *= 2.f; // fold negative frequencies
	}
	return psd;
}

/**
 * @brief Frequency axis in Hz for each positive-frequency FFT bin.
 * @param fftSize   Full FFT length (as returned by FFT).
 * @param sr        Sample rate in Hz.
 * @returns Vector of length fftSize/2 + 1.
 */
[[nodiscard]] inline std::vector<float> FFTFrequencies(int fftSize, float sr) {
	const int out = fftSize / 2 + 1;
	std::vector<float> f(out);
	const float step = sr / static_cast<float>(fftSize);
	fori (k, 0, out) f[k] = step * static_cast<float>(k);
	return f;
}

/**
 * @brief Convenience: compute one-sided magnitude spectrum from a Signal.
 *
 * @returns Magnitude bins (length = fftSize/2 + 1 where fftSize = NextPow2(N)).
 */
[[nodiscard]] inline std::vector<float> ComputeSpectrum(const Signal& sig) {
	auto X   = FFT(sig);
	auto mag = FFTMagnitude(X);
	mag.resize(X.size() / 2 + 1);
	return mag;
}

// ═════════════════════════════════════════════════════════════════════════════
//  AudioFilter base class + free-function helpers
// ═════════════════════════════════════════════════════════════════════════════

class AudioFilter {
public:
	virtual ~AudioFilter() = default;
	[[nodiscard]] virtual Signal      Apply(const Signal& in) const = 0;
	[[nodiscard]] virtual std::string Name()                  const = 0;
};

[[nodiscard]] inline Signal ApplyAudioFilter(const Signal& in, const AudioFilter& f) {
	return f.Apply(in);
}

template<std::derived_from<AudioFilter>... Fs>
[[nodiscard]] Signal ApplyAudioFilters(const Signal& in, const Fs&... fs) {
	Signal out = in;
	((out = fs.Apply(out)), ...);
	return out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  C. IIR FILTERS (BiQuad, Direct Form I)
//  Each filter stores its coefficients and applies them to a Signal.
//  Conventions: Fc = cutoff/centre in Hz, Q = quality factor (>0).
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief 2nd-order Butterworth low-pass filter.
 */
struct LowPassAudioFilter : AudioFilter {
	float fc, sr, Q;
	explicit LowPassAudioFilter(float fc=1000.f, float sr=44100.f, float Q=0.7071f)
		: fc(fc), sr(sr), Q(Q) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float w0   = 2.f * SDL::PI_F * fc / sr;
		const float cosW = SDL::Cos(w0);
		const float sinW = SDL::Sin(w0);
		const float alpha = sinW / (2.f * Q);
		const float a0    = 1.f + alpha;
		const float b0    = (1.f - cosW) * 0.5f / a0;
		const float b1    = (1.f - cosW)         / a0;
		const float b2    = b0;
		const float a1    = -2.f * cosW          / a0;
		const float a2    = (1.f - alpha)        / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("LowPass(Fc:{:.1f}Hz,Q:{:.3f})", fc, Q);
	}
};

/**
 * @brief 2nd-order Butterworth high-pass filter.
 */
struct HighPassAudioFilter : AudioFilter {
	float fc, sr, Q;
	explicit HighPassAudioFilter(float fc=1000.f, float sr=44100.f, float Q=0.7071f)
		: fc(fc), sr(sr), Q(Q) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float cosW  = SDL::Cos(w0);
		const float sinW  = SDL::Sin(w0);
		const float alpha = sinW / (2.f * Q);
		const float a0    = 1.f + alpha;
		const float b0    =  (1.f + cosW) * 0.5f / a0;
		const float b1    = -(1.f + cosW)         / a0;
		const float b2    = b0;
		const float a1    = -2.f * cosW            / a0;
		const float a2    = (1.f - alpha)           / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("HighPass(Fc:{:.1f}Hz,Q:{:.3f})", fc, Q);
	}
};

/**
 * @brief 2nd-order constant-skirt band-pass filter.
 */
struct BandPassAudioFilter : AudioFilter {
	float fc, sr, Q;
	explicit BandPassAudioFilter(float fc=1000.f, float sr=44100.f, float Q=1.f)
		: fc(fc), sr(sr), Q(Q) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float sinW  = SDL::Sin(w0);
		const float alpha = sinW / (2.f * Q);
		const float a0    = 1.f + alpha;
		const float b0    =  sinW * 0.5f / a0;
		const float b1    = 0.f;
		const float b2    = -b0;
		const float a1    = -2.f * SDL::Cos(w0) / a0;
		const float a2    = (1.f - alpha)        / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("BandPass(Fc:{:.1f}Hz,Q:{:.3f})", fc, Q);
	}
};

/**
 * @brief 2nd-order band-reject (band-stop / notch-wide) filter.
 */
struct BandStopAudioFilter : AudioFilter {
	float fc, sr, Q;
	explicit BandStopAudioFilter(float fc=1000.f, float sr=44100.f, float Q=1.f)
		: fc(fc), sr(sr), Q(Q) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float cosW  = SDL::Cos(w0);
		const float alpha = SDL::Sin(w0) / (2.f * Q);
		const float a0    = 1.f + alpha;
		const float b0    = 1.f          / a0;
		const float b1    = -2.f * cosW  / a0;
		const float b2    = b0;
		const float a1    = b1;
		const float a2    = (1.f - alpha) / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("BandStop(Fc:{:.1f}Hz,Q:{:.3f})", fc, Q);
	}
};

/**
 * @brief Deep notch filter at Fc (infinite attenuation at exactly Fc).
 * Identical to BandStop but with very high Q.
 */
struct NotchAudioFilter : AudioFilter {
	float fc, sr, Q;
	explicit NotchAudioFilter(float fc=1000.f, float sr=44100.f, float Q=30.f)
		: fc(fc), sr(sr), Q(Q) {}
	[[nodiscard]] Signal Apply(const Signal& in) const override {
		return BandStopAudioFilter{fc, sr, Q}.Apply(in);
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("Notch(Fc:{:.1f}Hz,Q:{:.1f})", fc, Q);
	}
};

/**
 * @brief Peaking equaliser bell curve (boost or cut at Fc).
 * @param gainDB  Positive = boost, negative = cut.
 */
struct PeakingEQAudioFilter : AudioFilter {
	float fc, sr, Q, gainDB;
	PeakingEQAudioFilter(float fc=1000.f, float sr=44100.f, float Q=1.f, float gainDB=6.f)
		: fc(fc), sr(sr), Q(Q), gainDB(gainDB) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float A     = std::pow(10.f, gainDB / 40.f);   // sqrt(10^(dB/20))
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float cosW  = SDL::Cos(w0);
		const float alpha = SDL::Sin(w0) / (2.f * Q);
		const float a0    = 1.f + alpha / A;
		const float b0    = (1.f + alpha * A) / a0;
		const float b1    = -2.f * cosW       / a0;
		const float b2    = (1.f - alpha * A) / a0;
		const float a1    = b1;
		const float a2    = (1.f - alpha / A) / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("PeakingEQ(Fc:{:.1f}Hz,Q:{:.2f},{}dB)", fc, Q, gainDB);
	}
};

/**
 * @brief Low-shelf filter (boost/cut frequencies below Fc).
 */
struct LowShelfAudioFilter : AudioFilter {
	float fc, sr, gainDB;
	LowShelfAudioFilter(float fc=500.f, float sr=44100.f, float gainDB=6.f)
		: fc(fc), sr(sr), gainDB(gainDB) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float A     = std::pow(10.f, gainDB / 40.f);
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float cosW  = SDL::Cos(w0);
		const float sinW  = SDL::Sin(w0);
		const float alpha = sinW / 2.f * std::sqrt((A + 1.f/A) * (1.f/0.9f - 1.f) + 2.f);
		const float a0    = (A+1.f) + (A-1.f)*cosW + 2.f*std::sqrt(A)*alpha;
		const float b0    = A * ((A+1.f) - (A-1.f)*cosW + 2.f*std::sqrt(A)*alpha) / a0;
		const float b1    = 2.f*A * ((A-1.f) - (A+1.f)*cosW)                       / a0;
		const float b2    = A * ((A+1.f) - (A-1.f)*cosW - 2.f*std::sqrt(A)*alpha)  / a0;
		const float a1    = -2.f * ((A-1.f) + (A+1.f)*cosW)                         / a0;
		const float a2    = ((A+1.f) + (A-1.f)*cosW - 2.f*std::sqrt(A)*alpha)       / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("LowShelf(Fc:{:.1f}Hz,{}dB)", fc, gainDB);
	}
};

/**
 * @brief High-shelf filter (boost/cut frequencies above Fc).
 */
struct HighShelfAudioFilter : AudioFilter {
	float fc, sr, gainDB;
	HighShelfAudioFilter(float fc=8000.f, float sr=44100.f, float gainDB=6.f)
		: fc(fc), sr(sr), gainDB(gainDB) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float A     = std::pow(10.f, gainDB / 40.f);
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float cosW  = SDL::Cos(w0);
		const float sinW  = SDL::Sin(w0);
		const float alpha = sinW / 2.f * std::sqrt((A + 1.f/A) * (1.f/0.9f - 1.f) + 2.f);
		const float a0    = (A+1.f) - (A-1.f)*cosW + 2.f*std::sqrt(A)*alpha;
		const float b0    = A * ((A+1.f) + (A-1.f)*cosW + 2.f*std::sqrt(A)*alpha) / a0;
		const float b1    = -2.f*A * ((A-1.f) + (A+1.f)*cosW)                      / a0;
		const float b2    = A * ((A+1.f) + (A-1.f)*cosW - 2.f*std::sqrt(A)*alpha)  / a0;
		const float a1    = 2.f * ((A-1.f) - (A+1.f)*cosW)                          / a0;
		const float a2    = ((A+1.f) - (A-1.f)*cosW - 2.f*std::sqrt(A)*alpha)       / a0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("HighShelf(Fc:{:.1f}Hz,{}dB)", fc, gainDB);
	}
};

/**
 * @brief All-pass filter (flat magnitude, frequency-dependent phase shift).
 */
struct AllPassAudioFilter : AudioFilter {
	float fc, sr, Q;
	explicit AllPassAudioFilter(float fc=1000.f, float sr=44100.f, float Q=0.7071f)
		: fc(fc), sr(sr), Q(Q) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float w0    = 2.f * SDL::PI_F * fc / sr;
		const float cosW  = SDL::Cos(w0);
		const float alpha = SDL::Sin(w0) / (2.f * Q);
		const float a0    = 1.f + alpha;
		const float b0    = (1.f - alpha) / a0;
		const float b1    = -2.f * cosW   / a0;
		const float b2    = 1.f;
		const float a1    = b1;
		const float a2    = b0;

		Signal out(in.Size(), in.sampleRate);
		detail::BiQuadState st;
		fori (i, 0, in.Size())
			out[i] = detail::BiQuadStep(in[i], b0,b1,b2,a1,a2, st);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("AllPass(Fc:{:.1f}Hz,Q:{:.3f})", fc, Q);
	}
};

// ═════════════════════════════════════════════════════════════════════════════
//  D. FIR FILTERS (windowed-sinc)
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief FIR low-pass filter.
 * @param order  Filter order (length − 1). Odd orders are auto-incremented to even.
 * @param fc     Cutoff frequency in Hz.
 * @param wt     Window function for kernel shaping.
 */
struct FIRLowPassAudioFilter : AudioFilter {
	int order; float fc, sr; AudioWindowType wt;
	FIRLowPassAudioFilter(int order=64, float fc=1000.f, float sr=44100.f,
												 AudioWindowType wt=AudioWindowType::Hamming)
		: order(order), fc(fc), sr(sr), wt(wt) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		return detail::ConvolveFIR(in, detail::MakeSincLP(order, fc/sr, wt));
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FIR-LP(ord:{},Fc:{:.1f}Hz)", order, fc);
	}
};

/**
 * @brief FIR high-pass filter (spectral inversion of sinc-LP).
 */
struct FIRHighPassAudioFilter : AudioFilter {
	int order; float fc, sr; AudioWindowType wt;
	FIRHighPassAudioFilter(int order=64, float fc=1000.f, float sr=44100.f,
													AudioWindowType wt=AudioWindowType::Hamming)
		: order(order), fc(fc), sr(sr), wt(wt) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		return detail::ConvolveFIR(in,
			detail::InvertSpectrum(detail::MakeSincLP(order, fc/sr, wt)));
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FIR-HP(ord:{},Fc:{:.1f}Hz)", order, fc);
	}
};

/**
 * @brief FIR band-pass filter (HP + LP via convolution of two kernels).
 * @param flo  Lower cutoff in Hz.
 * @param fhi  Upper cutoff in Hz.
 */
struct FIRBandPassAudioFilter : AudioFilter {
	int order; float flo, fhi, sr; AudioWindowType wt;
	FIRBandPassAudioFilter(int order=64, float flo=500.f, float fhi=2000.f,
													float sr=44100.f, AudioWindowType wt=AudioWindowType::Hamming)
		: order(order), flo(flo), fhi(fhi), sr(sr), wt(wt) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		// LP at fhi, then HP at flo → band-pass
		auto lp = FIRLowPassAudioFilter{order, fhi, sr, wt}.Apply(in);
		return FIRHighPassAudioFilter{order, flo, sr, wt}.Apply(lp);
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FIR-BP(ord:{},{:.0f}-{:.0f}Hz)", order, flo, fhi);
	}
};

/**
 * @brief FIR band-stop filter (LP + HP combined).
 */
struct FIRBandStopAudioFilter : AudioFilter {
	int order; float flo, fhi, sr; AudioWindowType wt;
	FIRBandStopAudioFilter(int order=64, float flo=500.f, float fhi=2000.f,
													float sr=44100.f, AudioWindowType wt=AudioWindowType::Hamming)
		: order(order), flo(flo), fhi(fhi), sr(sr), wt(wt) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		auto lo  = FIRLowPassAudioFilter{order, flo, sr, wt}.Apply(in);
		auto hi  = FIRHighPassAudioFilter{order, fhi, sr, wt}.Apply(in);
		const int N = std::min(lo.Size(), hi.Size());
		Signal out(N, in.sampleRate);
		fori (i, 0, N) out[i] = lo[i] + hi[i];
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FIR-BS(ord:{},{:.0f}-{:.0f}Hz)", order, flo, fhi);
	}
};

// ═════════════════════════════════════════════════════════════════════════════
//  E. TIME-DOMAIN EFFECTS
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Linear amplitude gain (multiplicative).
 * @param gainDB  Gain in dB. Set useLinear=true to pass a raw linear factor.
 */
struct GainAudioFilter : AudioFilter {
	float gain; bool useLinear;
	explicit GainAudioFilter(float gain=6.f, bool useLinear=false)
		: gain(gain), useLinear(useLinear) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const float g = useLinear ? gain : std::pow(10.f, gain / 20.f);
		Signal out = in;
		out *= g;
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return useLinear
			? std::format("Gain(x{:.3f})", gain)
			: std::format("Gain({}dB)", gain);
	}
};

/**
 * @brief Normalise signal so that the peak sample equals `target` amplitude.
 */
struct NormalizeAudioFilter : AudioFilter {
	float target;
	explicit NormalizeAudioFilter(float target=1.f) : target(target) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		if (in.Empty()) return in;
		float pk = 0.f;
		for (float v : in.samples) pk = std::max(pk, std::abs(v));
		if (pk < 1e-12f) return in;
		Signal out = in;
		out *= target / pk;
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("Normalize({:.2f})", target);
	}
};

/**
 * @brief Remove DC offset (subtract mean).
 */
struct DCRemoveAudioFilter : AudioFilter {
	[[nodiscard]] Signal Apply(const Signal& in) const override {
		if (in.Empty()) return in;
		const float mean = std::accumulate(in.samples.begin(), in.samples.end(), 0.f)
											 / static_cast<float>(in.Size());
		Signal out = in;
		for (float& v : out.samples) v -= mean;
		return out;
	}
	[[nodiscard]] std::string Name() const override { return "DCRemove"; }
};

/**
 * @brief Hard clip at ±threshold.
 */
struct ClipAudioFilter : AudioFilter {
	float threshold;
	explicit ClipAudioFilter(float threshold=1.f) : threshold(threshold) {}
	[[nodiscard]] Signal Apply(const Signal& in) const override {
		Signal out = in;
		for (float& v : out.samples) v = std::clamp(v, -threshold, threshold);
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("Clip(±{:.3f})", threshold);
	}
};

/**
 * @brief Integer-sample delay (prepends zeros, truncates end).
 */
struct DelayAudioFilter : AudioFilter {
	int delaySamples;
	explicit DelayAudioFilter(int samples=100) : delaySamples(samples) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const int N = in.Size();
		Signal out(N, in.sampleRate);
		const int d = std::clamp(delaySamples, 0, N);
		fori (i, d, N) out[i] = in[i - d];
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("Delay({}smp)", delaySamples);
	}
};

/**
 * @brief Cosine fade-in over the first `fadeSamples` samples.
 */
struct FadeInAudioFilter : AudioFilter {
	int fadeSamples;
	explicit FadeInAudioFilter(int samples=4410) : fadeSamples(samples) {}
	[[nodiscard]] Signal Apply(const Signal& in) const override {
		Signal out = in;
		const int fd = std::min(fadeSamples, out.Size());
		const float k = SDL::PI_F / static_cast<float>(fd);
		fori (i, 0, fd)
			out[i] *= 0.5f - 0.5f * SDL::Cos(k * static_cast<float>(i));
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FadeIn({}smp)", fadeSamples);
	}
};

/**
 * @brief Cosine fade-out over the last `fadeSamples` samples.
 */
struct FadeOutAudioFilter : AudioFilter {
	int fadeSamples;
	explicit FadeOutAudioFilter(int samples=4410) : fadeSamples(samples) {}
	[[nodiscard]] Signal Apply(const Signal& in) const override {
		Signal out = in;
		const int N  = out.Size();
		const int fd = std::min(fadeSamples, N);
		const float k = SDL::PI_F / static_cast<float>(fd);
		fori (i, 0, fd) {
			const int idx = N - fd + i;
			out[idx] *= 0.5f + 0.5f * SDL::Cos(k * static_cast<float>(i));
		}
		return out;
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FadeOut({}smp)", fadeSamples);
	}
};

// ═════════════════════════════════════════════════════════════════════════════
//  F. DEMODULATION
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief AM envelope-detection demodulator.
 *
 * Steps:
 *   1. Multiply by cos(2π·Fc·n/sr)  – frequency-shift to baseband
 *   2. Low-pass filter at Fc/2
 *   3. Full-wave rectify + LP again  – extract envelope
 *
 * @param fc         Carrier frequency in Hz.
 * @param lpOrder    FIR LP filter order for anti-aliasing.
 */
struct AMDemodAudioFilter : AudioFilter {
	float fc, sr;
	int   lpOrder;
	AMDemodAudioFilter(float fc=1000.f, float sr=44100.f, int lpOrder=128)
		: fc(fc), sr(sr), lpOrder(lpOrder) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		const int N = in.Size();
		Signal mixed(N, sr);
		const float k = 2.f * SDL::PI_F * fc / sr;
		fori (i, 0, N) mixed[i] = in[i] * SDL::Cos(k * static_cast<float>(i));

		// LP at fc/2 to remove the 2·Fc component
		const float lpFc = fc * 0.5f;
		Signal lp = FIRLowPassAudioFilter{lpOrder, lpFc, sr}.Apply(mixed);

		// Envelope via full-wave rectification + smoothing LP
		fori (i, 0, lp.Size()) lp[i] = std::abs(lp[i]);
		return FIRLowPassAudioFilter{lpOrder, lpFc, sr}.Apply(lp);
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("AMDemod(Fc:{:.1f}Hz)", fc);
	}
};

/**
 * @brief FM discriminator demodulator (phase-differentiator method).
 *
 * The instantaneous frequency is estimated as:
 *   f_inst[n] = sr / (2π) · angle( x[n] · conj(x[n-1]) )
 * where the analytic signal is obtained via a Hilbert FIR filter.
 *
 * @param fc          Carrier frequency in Hz.
 * @param freqDev     Maximum frequency deviation in Hz.
 * @param hilbOrder   FIR Hilbert filter order.
 */
struct FMDemodAudioFilter : AudioFilter {
	float fc, sr, freqDev;
	int   hilbOrder;
	FMDemodAudioFilter(float fc=1000.f, float sr=44100.f,
										 float freqDev=75.f, int hilbOrder=128)
		: fc(fc), sr(sr), freqDev(freqDev), hilbOrder(hilbOrder) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		// Build analytic signal: real = in, imag = Hilbert(in)
		auto hilbKernel = detail::MakeHilbert(hilbOrder);
		Signal imag     = detail::ConvolveFIR(in, hilbKernel);

		const int N = std::min(in.Size(), imag.Size());
		Signal out(N, sr);

		float prevRe = 0.f, prevIm = 0.f;
		const float twoPiInv = sr / (2.f * SDL::PI_F);
		// Remove DC carrier frequency offset
		const float carrierK = 2.f * SDL::PI_F * fc / sr;
		fori (i, 0, N) {
			// Shift analytic signal by -fc (down-convert)
			const float cosC  =  SDL::Cos(carrierK * static_cast<float>(i));
			const float sinC  = -SDL::Sin(carrierK * static_cast<float>(i));
			const float re    = in[i] * cosC - imag[i] * sinC;
			const float im    = in[i] * sinC + imag[i] * cosC;

			// Phase differentiator: angle of z[n] · conj(z[n-1])
			const float dPhi  = re * prevIm - im * prevRe; // Im of z·z*_prev
			const float dMag  = re * prevRe + im * prevIm; // Re of z·z*_prev
			out[i]            = twoPiInv * std::atan2(dPhi, dMag);
			prevRe = re; prevIm = im;
		}
		// Normalise by frequency deviation
		if (freqDev > 0.f) {
			for (float& v : out.samples) v /= freqDev;
		}
		// Remove residual DC
		return DCRemoveAudioFilter{}.Apply(out);
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("FMDemod(Fc:{:.1f}Hz,Δf:{:.1f}Hz)", fc, freqDev);
	}
};

/**
 * @brief Single-sideband (SSB/USB) demodulator via Hilbert transform.
 *
 * Reconstructs the baseband signal by multiplying the analytic signal
 * by the complex carrier exp(-j2π·Fc·n/sr).
 */
struct SSBDemodAudioFilter : AudioFilter {
	float fc, sr;
	int   hilbOrder;
	SSBDemodAudioFilter(float fc=1000.f, float sr=44100.f, int hilbOrder=128)
		: fc(fc), sr(sr), hilbOrder(hilbOrder) {}

	[[nodiscard]] Signal Apply(const Signal& in) const override {
		auto hilbKernel = detail::MakeHilbert(hilbOrder);
		Signal imag     = detail::ConvolveFIR(in, hilbKernel);

		const int N = std::min(in.Size(), imag.Size());
		Signal out(N, sr);
		const float k = 2.f * SDL::PI_F * fc / sr;
		fori (i, 0, N) {
			const float fi = static_cast<float>(i);
			out[i] = in[i] * SDL::Cos(k * fi) + imag[i] * SDL::Sin(k * fi);
		}
		return FIRLowPassAudioFilter{128, fc * 0.5f, sr}.Apply(out);
	}
	[[nodiscard]] std::string Name() const override {
		return std::format("SSBDemod(Fc:{:.1f}Hz)", fc);
	}
};

// ═════════════════════════════════════════════════════════════════════════════
//  G. SIGNAL ANALYSIS HELPERS (free functions)
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Root-mean-square energy.
 */
[[nodiscard]] inline float RMS(const Signal& s) {
	if (s.Empty()) return 0.f;
	float acc = 0.f;
	for (float v : s.samples) acc += v * v;
	return std::sqrt(acc / static_cast<float>(s.Size()));
}

/**
 * @brief Absolute peak sample value.
 */
[[nodiscard]] inline float Peak(const Signal& s) {
	float pk = 0.f;
	for (float v : s.samples) pk = std::max(pk, std::abs(v));
	return pk;
}

/**
 * @brief Signal-to-noise ratio in dB.
 *
 * @param signal  Clean reference signal.
 * @param noise   Noise-only signal (or difference: measured − reference).
 */
[[nodiscard]] inline float SNR(const Signal& signal, const Signal& noise) {
	const float sigPwr   = RMS(signal);
	const float noisePwr = RMS(noise);
	if (noisePwr < 1e-30f) return 999.f;
	return 20.f * std::log10(sigPwr / noisePwr);
}

/**
 * @brief Total harmonic distortion at `fundamental` Hz.
 *
 * THD = √(Σ |H_k|²) / |H_1|  for k = 2, 3, …, maxHarmonic.
 * Computed from the FFT magnitude spectrum.
 */
[[nodiscard]] inline float THD(const Signal& s, float fundamental, int maxHarmonic=10) {
	auto X    = FFT(s);
	auto mag  = FFTMagnitude(X);
	const int N    = (int)X.size();
	const float binHz = s.sampleRate / static_cast<float>(N);

	auto BinOf = [&](float freq) {
		return std::clamp(static_cast<int>(std::round(freq / binHz)), 0, N/2);
	};

	const float h1 = mag[BinOf(fundamental)];
	if (h1 < 1e-30f) return 0.f;

	float sumsq = 0.f;
	for (int h = 2; h <= maxHarmonic; ++h) {
		const float v = mag[BinOf(fundamental * static_cast<float>(h))];
		sumsq += v * v;
	}
	return std::sqrt(sumsq) / h1;
}

/**
 * @brief Normalised circular autocorrelation as a Signal.
 *
 * Uses FFT convolution: R[k] = IFFT(|FFT(x)|²) / R[0].
 * Useful for pitch detection and periodicity analysis.
 */
[[nodiscard]] inline Signal Autocorrelation(const Signal& s) {
	if (s.Empty()) return s;
	auto X   = FFT(s);
	std::vector<Complex> Xsq(X.size());
	fori (k, 0, (int)X.size()) Xsq[k] = std::norm(X[k]);   // |X|²
	auto r   = IFFT(Xsq);
	const float r0 = std::abs(r[0]) + 1e-30f;
	Signal out(r, s.sampleRate);
	for (float& v : out.samples) v /= r0;
	return out;
}

// ═════════════════════════════════════════════════════════════════════════════
//  H. SDL RENDERER DRAWING HELPERS
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Draw a waveform inside a viewport rectangle.
 *
 * Renders the signal as a polyline.  The vertical center of `rect` maps to 0,
 * and the full height maps to the range [-1, 1].  Values outside that range
 * are visible but may go beyond the rect boundary.
 *
 * @param renderer  Active SDL renderer.
 * @param sig       Signal to draw.
 * @param rect      Viewport rectangle in screen coordinates.
 * @param color     Line colour.
 * @param maxSamples  Maximum number of samples to render (auto-decimate if 0).
 */
inline void DrawSignal(RendererRef renderer, const Signal& sig,
											 const FRect& rect, ColorRaw color,
											 int maxSamples = 0) {
	if (sig.Empty()) return;
	renderer.SetDrawColor(color);

	const int N  = sig.Size();
	const int M  = (maxSamples > 0) ? std::min(maxSamples, N) : N;
	const float xScale = rect.w / static_cast<float>(M - 1);
	const float yMid   = rect.y + rect.h * 0.5f;
	const float yScale = rect.h * 0.5f;

	float prevX = rect.x;
	float prevY = yMid - sig[0] * yScale;

	for (int i = 1; i < M; ++i) {
		const int   si  = static_cast<int>(static_cast<float>(i) * static_cast<float>(N) / static_cast<float>(M));
		const float x   = rect.x + static_cast<float>(i) * xScale;
		const float y   = yMid - std::clamp(sig[si], -1.f, 1.f) * yScale;
		renderer.RenderLine({prevX, prevY}, {x, y});
		prevX = x; prevY = y;
	}
}

/**
 * @brief Draw the magnitude (or dB) spectrum as a filled bar graph.
 *
 * @param renderer  Active SDL renderer.
 * @param sig       Input signal (FFT is computed internally).
 * @param rect      Viewport rectangle in screen coordinates.
 * @param dB        If true, display in dBFS with a floor at -80 dB.
 * @param color     Bar colour.
 * @param fftSize   FFT size (0 = NextPow2(sig.Size())).
 */
inline void DrawSpectrum(RendererRef renderer, const Signal& sig,
												 const FRect& rect, bool dB,
												 ColorRaw color, int fftSize = 0) {
	if (sig.Empty()) return;

	const int targetN = (fftSize > 0) ? detail::NextPow2(fftSize)
																		 : detail::NextPow2(sig.Size());
	// Build a temporary zero-padded signal
	Signal padded(targetN, sig.sampleRate);
	fori (i, 0, std::min(sig.Size(), targetN)) padded[i] = sig[i];

	auto X        = FFT(padded);
	const int bins = targetN / 2 + 1;

	renderer.SetDrawColor(color);
	const float barW    = rect.w / static_cast<float>(bins);
	const float dbFloor = -80.f;

	for (int k = 0; k < bins; ++k) {
		float level;
		if (dB) {
			const float db = 20.f * std::log10(std::abs(X[k]) + 1e-12f);
			level = std::clamp((db - dbFloor) / (-dbFloor), 0.f, 1.f);
		} else {
			const float normFactor = 2.f / static_cast<float>(targetN);
			level = std::clamp(std::abs(X[k]) * normFactor, 0.f, 1.f);
		}
		const float bh = rect.h * level;
		const float bx = rect.x + static_cast<float>(k) * barW;
		const float by = rect.y + rect.h - bh;
		renderer.RenderFillRect(FRect{bx, by, barW - 1.f, bh});
	}
}

/**
 * @brief Draw a rolling Short-Time Fourier Transform spectrogram.
 *
 * Each column of `rect` corresponds to one FFT frame (hop = fftSize/2).
 * Magnitude is colour-mapped from black (silence) → colour (loud).
 *
 * @param renderer  Active SDL renderer.
 * @param sig       Input signal.
 * @param rect      Viewport rectangle.
 * @param fftSize   STFT frame length in samples (should be a power of 2).
 * @param color     Full-intensity colour (silence blends toward black).
 */
inline void DrawSpectrogram(RendererRef renderer, const Signal& sig,
														 const FRect& rect, int fftSize,
														 ColorRaw color) {
	if (sig.Empty() || fftSize < 4) return;

	const int N       = sig.Size();
	const int hop     = fftSize / 2;
	const int frames  = std::max(1, (N - fftSize) / hop + 1);
	const int bins    = fftSize / 2 + 1;

	const float colW  = rect.w / static_cast<float>(frames);
	const float rowH  = rect.h / static_cast<float>(bins);
	auto window       = detail::WindowHamming(fftSize);

	for (int f = 0; f < frames; ++f) {
		const int start = f * hop;
		// Windowed frame
		std::vector<Complex> frame(fftSize);
		fori (i, 0, fftSize) {
			const int si = start + i;
			const float s = (si < N) ? sig[si] : 0.f;
			frame[i] = Complex(s * window[i], 0.f);
		}
		detail::FFTInPlace(frame, false);
		const float normFactor = 2.f / static_cast<float>(fftSize);

		const float cx = rect.x + static_cast<float>(f) * colW;
		for (int k = 0; k < bins; ++k) {
			const float mag = std::abs(frame[k]) * normFactor;
			const float lev = std::clamp(mag, 0.f, 1.f);

			// Map frequency bin k → screen y (k=0 at bottom, k=bins-1 at top)
			const float cy = rect.y + rect.h - (static_cast<float>(k) + 1.f) * rowH;

			renderer.SetDrawColor({
				static_cast<Uint8>(color.r * lev),
				static_cast<Uint8>(color.g * lev),
				static_cast<Uint8>(color.b * lev),
				color.a
			});
			renderer.RenderFillRect(FRect{cx, cy, colW, rowH});
		}
	}
}

/**
 * @brief Draw horizontal and vertical axis labels for a spectrum/spectrogram.
 *
 * Uses SDL_RenderDebugText, so no font asset is required.
 * Marks 100 Hz, 1 kHz, 5 kHz, 10 kHz, 20 kHz on the X axis,
 * and 0 dB, -20 dB, -40 dB, -60 dB, -80 dB on the Y axis.
 */
inline void DrawSpectrumAxis(RendererRef renderer, const FRect& rect,
														 float sampleRate, ColorRaw color) {
	renderer.SetDrawColor(color);
	const float nyquist = sampleRate * 0.5f;

	// Vertical grid lines with frequency labels
	static constexpr float kFreqs[] = {100.f, 500.f, 1000.f, 5000.f, 10000.f, 20000.f};
	for (float f : kFreqs) {
		if (f >= nyquist) continue;
		const float x = rect.x + rect.w * f / nyquist;
		renderer.RenderLine({x, rect.y}, {x, rect.y + rect.h});
		std::string lbl = (f < 1000.f)
			? std::format("{:.0f}Hz", f)
			: std::format("{:.0f}kHz", f / 1000.f);
		renderer.RenderDebugText({x + 2.f, rect.y + rect.h - 14.f}, lbl);
	}

	// Horizontal dB reference lines
	static constexpr float kDB[] = {0.f, -20.f, -40.f, -60.f, -80.f};
	constexpr float dbFloor = -80.f;
	for (float db : kDB) {
		const float y = rect.y + rect.h * (db - dbFloor) / (-dbFloor);
		const float yn = rect.y + rect.h - (y - rect.y);
		renderer.RenderLine({rect.x, yn}, {rect.x + rect.w, yn});
		renderer.RenderDebugTextFormat({rect.x + 2.f, yn - 14.f},
														 "{}dB", static_cast<int>(db));
	}
}

// ═════════════════════════════════════════════════════════════════════════════
//  AudioFilterRegistry
// ═════════════════════════════════════════════════════════════════════════════

/**
 * @brief Named collection of AudioFilters with cursor navigation.
 *
 * ```cpp
 * SDL::AudioFilterRegistry reg;
 * reg.Add(std::make_unique<SDL::LowPassAudioFilter>(2000.f, 44100.f));
 * reg.Add(std::make_unique<SDL::FMDemodAudioFilter>(1000.f, 44100.f, 75.f));
 *
 * SDL::Signal out = reg.ApplyCurrent(signal);
 * std::string name = reg.CurrentName();
 * reg.Next();
 * ```
 */
class AudioFilterRegistry {
public:
	void Add(std::unique_ptr<AudioFilter> f) { m_filters.push_back(std::move(f)); }

	void Next() {
		if (!m_filters.empty()) m_index = (m_index + 1) % m_filters.size();
	}
	void Prev() {
		if (!m_filters.empty())
			m_index = (m_index + m_filters.size() - 1) % m_filters.size();
	}

	[[nodiscard]] Signal ApplyCurrent(const Signal& in) const {
		if (m_filters.empty()) return in;
		return m_filters[m_index]->Apply(in);
	}
	[[nodiscard]] std::string CurrentName() const {
		return m_filters.empty() ? "(none)" : m_filters[m_index]->Name();
	}
	[[nodiscard]] size_t CurrentIndex() const noexcept { return m_index; }
	[[nodiscard]] size_t Count()        const noexcept { return m_filters.size(); }
	[[nodiscard]] bool   Empty()        const noexcept { return m_filters.empty(); }
	[[nodiscard]] const AudioFilter& operator[](size_t i) const {
		return *m_filters.at(i);
	}

private:
	std::vector<std::unique_ptr<AudioFilter>> m_filters;
	size_t                                    m_index = 0;
};

} // namespace SDL

#endif // SDL3PP_AUDIO_PROCESSING_H_