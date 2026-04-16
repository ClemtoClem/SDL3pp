#ifndef SDL3PP_FILTERS_H_
#define SDL3PP_FILTERS_H_

/**
 * @file SDL3pp_filters.h
 * @brief Image filter pipeline for SDL::Surface.
 *
 * ## Overview
 *
 * All filters operate on an RGBA8888 working surface.  The helper
 * `SDL::ApplySurfaceFilter(src, filter)` handles format conversion transparently.
 *
 * ## SurfaceFilter categories
 *
 * ### A. Pointwise (per-pixel) filters
 * Each output pixel depends only on the corresponding input pixel.
 * - GrayscaleSurfaceFilter      – luminance-weighted average
 * - InvertSurfaceFilter         – 255 − channel
 * - BrightnessSurfaceFilter     – additive brightness shift
 * - ContrastSurfaceFilter       – linear contrast stretch
 * - ThresholdSurfaceFilter      – binarise at a luminance threshold
 * - SepiaSurfaceFilter          – apply vintage sepia matrix
 *
 * ### B. Convolution (neighbourhood) filters
 * Output pixel depends on a local kernel applied to the neighbourhood.
 * - BoxBlurSurfaceFilter        – uniform averaging kernel
 * - GaussianBlurSurfaceFilter   – Gaussian-weighted averaging
 * - SharpenSurfaceFilter        – unsharp mask kernel
 * - SobelSurfaceFilter          – edge gradient magnitude
 * - CannySurfaceFilter          – full Canny pipeline (blur→Sobel→NMS→hysteresis)
 * - LaplacianSurfaceFilter      – second-derivative edge detector
 * - EmbossSurfaceFilter         – emboss / relief kernel
 * - PrewittSurfaceFilter        – Prewitt gradient edge detector
 *
 * ### C. Morphological filters
 * - DilationSurfaceFilter       – local-max (expands bright regions)
 * - ErosionSurfaceFilter        – local-min (shrinks bright regions)
 * - MorphCloseSurfaceFilter     – dilation then erosion (fills holes)
 *
 * ### D. Statistical filters
 * - MedianSurfaceFilter         – median of neighbourhood (salt-and-pepper removal)
 * - MinSurfaceFilter            – local-minimum (erosion variant)
 * - MaxSurfaceFilter            – local-maximum (dilation variant)
 *
 * ### E. Bilateral filter
 * - BilateralSurfaceFilter      – edge-preserving spatial blur
 *
 * ### F. Dithering
 * - FloydSteinbergDitherSurfaceFilter – error-diffusion dithering to N colours
 *
 * ### G. Look-Up Table (LUT)
 * - LutSurfaceFilter            – apply a 256-entry per-channel LUT
 *
 * ## Usage
 *
 * ```cpp
 * SDL::Surface out = SDL::ApplySurfaceFilter(src, SDL::GrayscaleSurfaceFilter{});
 * SDL::Surface out = SDL::ApplySurfaceFilter(src, SDL::CannySurfaceFilter{1.4f, 40, 100});
 *
 * // Chain multiple filters
 * SDL::Surface out = SDL::ApplySurfaceFilters(src,
 *     SDL::GaussianBlurSurfaceFilter{1.0f},
 *     SDL::SobelSurfaceFilter{});
 * ```
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstdint>
#include <format>
#include <numbers>
#include <string>
#include <vector>

#include "SDL3pp_pixels.h"
#include "SDL3pp_stdinc.h"
#include "SDL3pp_surface.h"

namespace SDL {

// ─────────────────────────────────────────────────────────────────────────────
// Convenience loop macros
// ─────────────────────────────────────────────────────────────────────────────
#ifndef fori
#  define fori(var,init,end)    for (int var=(init); var<(end);  ++var)
#  define fori_eq(var,init,end) for (int var=(init); var<=(end); ++var)
#endif

	namespace detail {

		[[nodiscard]] inline Uint8 Clamp8(float v) noexcept {
			return static_cast<Uint8>(std::clamp(v, 0.f, 255.f));
		}
		[[nodiscard]] inline Uint8 Clamp8(int v) noexcept {
			return static_cast<Uint8>(std::clamp(v, 0, 255));
		}

		// BT.601 luma in integer arithmetic — no float multiplications
		[[nodiscard]] inline int LumaI(int r, int g, int b) noexcept {
			return (77*r + 150*g + 29*b) >> 8;
		}
		[[nodiscard]] inline int LumaI(ColorRaw c) noexcept {
			return LumaI(c.r, c.g, c.b);
		}

		// Convert to RGBA32: bytes always R=p[0],G=p[1],B=p[2],A=p[3] on all platforms
		[[nodiscard]] inline Surface ToRGBA(SurfaceConstRef src) {
			return ConvertSurface(src, PIXELFORMAT_RGBA32);
		}

		// ── PixBuf: direct byte access on a locked surface ───────────────────────────
		struct PixBuf {
			Uint8* pixels;
			int w, h, pitch;

			explicit PixBuf(SurfaceLock& lock) noexcept
				: pixels(static_cast<Uint8*>(lock.GetPixels()))
				, w(lock.GetSize().x), h(lock.GetSize().y), pitch(lock.GetPitch()) {}

			[[nodiscard]] ColorRaw Get(int x, int y) const noexcept {
				const Uint8* p = pixels + y*pitch + x*4;
				return {p[0], p[1], p[2], p[3]};
			}
			void Set(int x, int y, ColorRaw c) noexcept {
				Uint8* p = pixels + y*pitch + x*4;
				p[0]=c.r; p[1]=c.g; p[2]=c.b; p[3]=c.a;
			}
			[[nodiscard]] ColorRaw GetClamped(int x, int y) const noexcept {
				return Get(SDL::Clamp(x,0,w-1), SDL::Clamp(y,0,h-1));
			}
		};

		// ── ConstPixBuf: zero-copy read from SurfaceConstRef ────────────────────────
		// Saves one DuplicateSurface per filter call.
		struct ConstPixBuf {
			const Uint8* pixels;
			int w, h, pitch;
		private:
			SurfaceRaw m_raw;
			bool       m_locked;
		public:
			explicit ConstPixBuf(SurfaceConstRef s) noexcept {
				m_raw    = static_cast<SurfaceRaw>(s);
				m_locked = SDL::MustLock(s);
				if (m_locked) SDL::LockSurface(m_raw);
				pixels = static_cast<const Uint8*>(SDL::GetSurfacePixels(s));
				w      = SDL::GetSurfaceWidth(s);
				h      = SDL::GetSurfaceHeight(s);
				pitch  = SDL::GetSurfacePitch(s);
			}
			~ConstPixBuf() noexcept { if (m_locked) SDL_UnlockSurface(m_raw); }
			ConstPixBuf(const ConstPixBuf&)            = delete;
			ConstPixBuf& operator=(const ConstPixBuf&) = delete;

			[[nodiscard]] ColorRaw Get(int x, int y) const noexcept {
				const Uint8* p = pixels + y*pitch + x*4;
				return {p[0], p[1], p[2], p[3]};
			}
			[[nodiscard]] ColorRaw GetClamped(int x, int y) const noexcept {
				return Get(SDL::Clamp(x,0,w-1), SDL::Clamp(y,0,h-1));
			}
		};

		// ── 4-channel convolution ────────────────────────────────────────────────────
		[[nodiscard]] inline std::array<float,4>
		Convolve(const ConstPixBuf& buf, int x, int y,
						const float* kernel, int ksz) noexcept {
			const int half = ksz/2;
			std::array<float,4> acc{};
			fori (ky, 0, ksz)
				fori (kx, 0, ksz) {
					const float w = kernel[ky*ksz+kx];
					const ColorRaw p = buf.GetClamped(x+kx-half, y+ky-half);
					acc[0]+=w*p.r; acc[1]+=w*p.g; acc[2]+=w*p.b; acc[3]+=w*p.a;
				}
			return acc;
		}

		// ── Single-channel convolution (for grayscale filters — 4x fewer ops) ────────
		[[nodiscard]] inline float
		ConvolveGray(const ConstPixBuf& buf, int x, int y,
								const float* kernel, int ksz) noexcept {
			const int half = ksz/2;
			float acc = 0.f;
			fori (ky, 0, ksz)
				fori (kx, 0, ksz)
					acc += kernel[ky*ksz+kx] * static_cast<float>(
									buf.GetClamped(x+kx-half, y+ky-half).r);
			return acc;
		}

		// ── Gaussian kernels ─────────────────────────────────────────────────────────
		[[nodiscard]] inline std::vector<float> MakeGaussian1D(float sigma) {
			const int r  = SDL::Max(1, static_cast<int>(SDL::Ceil(sigma*3.f)));
			const int sz = 2*r+1;
			std::vector<float> k(sz);
			const float s2 = 2.f*sigma*sigma;
			float sum = 0.f;
			fori_eq (i, -r, r) { float v=SDL::Exp(-float(i*i)/s2); k[i+r]=v; sum+=v; }
			for (float& v : k) v/=sum;
			return k;
		}
		[[nodiscard]] inline std::vector<float> MakeGaussianKernel(float sigma) {
			const int r  = SDL::Max(1, static_cast<int>(SDL::Ceil(sigma*3.f)));
			const int sz = 2*r+1;
			std::vector<float> k(sz*sz);
			const float s2 = 2.f*sigma*sigma;
			float sum = 0.f;
			fori_eq (y, -r, r)
				fori_eq (x, -r, r) {
					float v = SDL::Exp(-(x*x+y*y)/s2);
					k[(y+r)*sz+(x+r)] = v; sum+=v;
				}
			for (float& v : k) v/=sum;
			return k;
		}

		// ── Separable 1-D passes (horizontal then vertical) ──────────────────────────
		inline void ConvH(const Uint8* __restrict src, int W, int H, int sp,
											Uint8* __restrict dst, int dp,
											const float* k, int ksz) noexcept {
			const int half=ksz/2;
			fori (y, 0, H)
				fori (x, 0, W) {
					float r=0,g=0,b=0,a=0;
					fori (kx, 0, ksz) {
						const int sx=SDL::Clamp(x+kx-half,0,W-1);
						const Uint8* p=src+y*sp+sx*4; const float w=k[kx];
						r+=w*p[0]; g+=w*p[1]; b+=w*p[2]; a+=w*p[3];
					}
					Uint8* d=dst+y*dp+x*4;
					d[0]=Clamp8(r); d[1]=Clamp8(g); d[2]=Clamp8(b); d[3]=Clamp8(a);
				}
		}
		inline void ConvV(const Uint8* __restrict src, int W, int H, int sp,
											Uint8* __restrict dst, int dp,
											const float* k, int ksz) noexcept {
			const int half=ksz/2;
			fori (y, 0, H)
				fori (x, 0, W) {
					float r=0,g=0,b=0,a=0;
					fori (ky, 0, ksz) {
						const int sy=SDL::Clamp(y+ky-half,0,H-1);
						const Uint8* p=src+sy*sp+x*4; const float w=k[ky];
						r+=w*p[0]; g+=w*p[1]; b+=w*p[2]; a+=w*p[3];
					}
					Uint8* d=dst+y*dp+x*4;
					d[0]=Clamp8(r); d[1]=Clamp8(g); d[2]=Clamp8(b); d[3]=Clamp8(a);
				}
		}
		inline void GaussianSep(const ConstPixBuf& src, PixBuf& tmp, PixBuf& dst,
														const float* k, int ksz) noexcept {
			ConvH(src.pixels, src.w, src.h, src.pitch, tmp.pixels, tmp.pitch, k, ksz);
			ConvV(tmp.pixels, tmp.w, tmp.h, tmp.pitch, dst.pixels, dst.pitch, k, ksz);
		}

	} // namespace detail

	// ─────────────────────────────────────────────────────────────────────────────
	class SurfaceFilter {
	public:
		virtual ~SurfaceFilter() = default;
		[[nodiscard]] virtual Surface Apply(SurfaceConstRef src) const = 0;
		[[nodiscard]] virtual std::string Name() const = 0;
	};

	[[nodiscard]] inline Surface ApplySurfaceFilter(SurfaceConstRef src, const SurfaceFilter& f) {
		return f.Apply(detail::ToRGBA(src));
	}
	template<std::derived_from<SurfaceFilter>... Fs>
	[[nodiscard]] Surface ApplySurfaceFilters(SurfaceConstRef src, const Fs&... fs) {
		Surface out = detail::ToRGBA(src);
		((out = fs.Apply(out)), ...);
		return out;
	}

	// ═════════════════════════════════════════════════════════════════════════════
	//  A. POINTWISE FILTERS  (1 DuplicateSurface + ConstPixBuf, loop over total)
	// ═════════════════════════════════════════════════════════════════════════════

	struct GrayscaleSurfaceFilter : SurfaceFilter {
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst = SDL::DuplicateSurface(src);
			auto lDst = dst.Lock(); detail::PixBuf bDst(lDst);
			const int total = bDst.w * bDst.h;
			fori (i, 0, total) {
				const Uint8* s = bSrc.pixels + i*4;
				const Uint8  g = static_cast<Uint8>(detail::LumaI(s[0],s[1],s[2]));
				Uint8* d = bDst.pixels + i*4;
				d[0]=g; d[1]=g; d[2]=g; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return "Grayscale"; }
	};

	struct InvertSurfaceFilter : SurfaceFilter {
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst = SDL::DuplicateSurface(src);
			auto lDst = dst.Lock(); detail::PixBuf bDst(lDst);
			const int total = bDst.w * bDst.h;
			fori (i, 0, total) {
				const Uint8* s = bSrc.pixels + i*4;
				Uint8*       d = bDst.pixels + i*4;
				d[0]=255-s[0]; d[1]=255-s[1]; d[2]=255-s[2]; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return "Invert"; }
	};

	struct BrightnessSurfaceFilter : SurfaceFilter {
		float amount;
		explicit BrightnessSurfaceFilter(float a=30.f) : amount(a) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			// Precalculate LUT once instead of calling Clamp8 per pixel
			std::array<Uint8,256> lut;
			fori_eq (i, 0, 255) lut[i]=detail::Clamp8(static_cast<float>(i)+amount);
			detail::ConstPixBuf bSrc(src);
			Surface dst = SDL::DuplicateSurface(src);
			auto lDst = dst.Lock(); detail::PixBuf bDst(lDst);
			const int total = bDst.w * bDst.h;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4; Uint8* d=bDst.pixels+i*4;
				d[0]=lut[s[0]]; d[1]=lut[s[1]]; d[2]=lut[s[2]]; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Brightness({:.1f})", amount);
		}
	};

	struct ContrastSurfaceFilter : SurfaceFilter {
		float factor;
		explicit ContrastSurfaceFilter(float f=1.5f) : factor(f) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			std::array<Uint8,256> lut;
			fori_eq (i, 0, 255)
				lut[i]=detail::Clamp8((static_cast<float>(i)-128.f)*factor+128.f);
			detail::ConstPixBuf bSrc(src);
			Surface dst = SDL::DuplicateSurface(src);
			auto lDst = dst.Lock(); detail::PixBuf bDst(lDst);
			const int total = bDst.w * bDst.h;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4; Uint8* d=bDst.pixels+i*4;
				d[0]=lut[s[0]]; d[1]=lut[s[1]]; d[2]=lut[s[2]]; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Contrast(factor:{:.4f})", factor);
		}
	};

	struct ThresholdSurfaceFilter : SurfaceFilter {
		float threshold;
		explicit ThresholdSurfaceFilter(float t=128.f) : threshold(t) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst = SDL::DuplicateSurface(src);
			auto lDst = dst.Lock(); detail::PixBuf bDst(lDst);
			const int thI = static_cast<int>(threshold);
			const int total = bDst.w * bDst.h;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4;
				const Uint8  v = detail::LumaI(s[0],s[1],s[2]) > thI ? 255 : 0;
				Uint8* d=bDst.pixels+i*4; d[0]=v; d[1]=v; d[2]=v; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Threshold({:.0f})", threshold);
		}
	};

	struct SepiaSurfaceFilter : SurfaceFilter {
		float intensity;
		explicit SepiaSurfaceFilter(float i=1.f) : intensity(std::clamp(i,0.f,1.f)) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			// 3×256 per-channel contribution LUTs — one float mul per channel per pixel
			std::array<float,256> rR,rG,rB, gR,gG,gB, bR,bG,bB;
			fori_eq (i, 0, 255) {
				const float f=static_cast<float>(i);
				rR[i]=f*0.393f; rG[i]=f*0.769f; rB[i]=f*0.189f;
				gR[i]=f*0.349f; gG[i]=f*0.686f; gB[i]=f*0.168f;
				bR[i]=f*0.272f; bG[i]=f*0.534f; bB[i]=f*0.131f;
			}
			detail::ConstPixBuf bSrc(src);
			Surface dst = SDL::DuplicateSurface(src);
			auto lDst = dst.Lock(); detail::PixBuf bDst(lDst);
			const int total = bDst.w * bDst.h;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4;
				const float r=s[0], g=s[1], b=s[2];
				const float sr=rR[(int)r]+rG[(int)g]+rB[(int)b];
				const float sg=gR[(int)r]+gG[(int)g]+gB[(int)b];
				const float sb=bR[(int)r]+bG[(int)g]+bB[(int)b];
				Uint8* d=bDst.pixels+i*4;
				d[0]=detail::Clamp8(r+(sr-r)*intensity);
				d[1]=detail::Clamp8(g+(sg-g)*intensity);
				d[2]=detail::Clamp8(b+(sb-b)*intensity);
				d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Sepia({:.0f}%)", intensity*100.f);
		}
	};

	// ═════════════════════════════════════════════════════════════════════════════
	//  B. CONVOLUTION FILTERS
	// ═════════════════════════════════════════════════════════════════════════════

	struct BoxBlurSurfaceFilter : SurfaceFilter {
		int radius;
		explicit BoxBlurSurfaceFilter(int r=2) : radius(r) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			const int sz=2*radius+1;
			const float w=1.f/static_cast<float>(sz);
			std::vector<float> k(sz, w);
			Surface tmp=SDL::DuplicateSurface(src), dst=SDL::DuplicateSurface(src);
			auto lTmp=tmp.Lock(), lDst=dst.Lock();
			detail::PixBuf bTmp(lTmp), bDst(lDst);
			detail::ConvH(bSrc.pixels,bSrc.w,bSrc.h,bSrc.pitch,bTmp.pixels,bTmp.pitch,k.data(),sz);
			detail::ConvV(bTmp.pixels,bTmp.w,bTmp.h,bTmp.pitch,bDst.pixels,bDst.pitch,k.data(),sz);
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Box Blur({})",radius); }
	};

	struct GaussianBlurSurfaceFilter : SurfaceFilter {
		float sigma;
		std::vector<float> coefs;

		explicit GaussianBlurSurfaceFilter(float s = 1.4f) : sigma(s) {
			coefs = detail::MakeGaussian1D(sigma);
		}

		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface tmp=SDL::DuplicateSurface(src), dst=SDL::DuplicateSurface(src);
			auto lTmp=tmp.Lock(), lDst=dst.Lock();
			detail::PixBuf bTmp(lTmp), bDst(lDst);
			detail::GaussianSep(bSrc, bTmp, bDst, coefs.data(), static_cast<int>(coefs.size()));
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Gaussian Blur({:.1f})",sigma); }
	};

	struct SharpenSurfaceFilter : SurfaceFilter {
		float strength;
		explicit SharpenSurfaceFilter(float s=1.f) : strength(s) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const float k[9]={0,-strength,0,-strength,1+4*strength,-strength,0,-strength,0};
			fori (y, 0, bDst.h)
				fori (x, 0, bDst.w) {
					auto acc=detail::Convolve(bSrc,x,y,k,3);
					bDst.Set(x,y,{detail::Clamp8(acc[0]),detail::Clamp8(acc[1]),
												detail::Clamp8(acc[2]),bSrc.Get(x,y).a});
				}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Sharpen({:.4f})",strength); }
	};

	struct SobelSurfaceFilter : SurfaceFilter {
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			Surface gray=GrayscaleSurfaceFilter{}.Apply(src);
			detail::ConstPixBuf bGray(gray);
			Surface dst=SDL::DuplicateSurface(gray);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			static constexpr float Gx[9]={-1,0,1,-2,0,2,-1,0,1};
			static constexpr float Gy[9]={-1,-2,-1, 0,0,0, 1,2,1};
			fori (y, 0, bDst.h)
				fori (x, 0, bDst.w) {
					const float gx=detail::ConvolveGray(bGray,x,y,Gx,3);
					const float gy=detail::ConvolveGray(bGray,x,y,Gy,3);
					const Uint8 m=detail::Clamp8(SDL::Sqrt(gx*gx+gy*gy));
					bDst.Set(x,y,{m,m,m,bGray.Get(x,y).a});
				}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return "Sobel"; }
	};

	struct PrewittSurfaceFilter : SurfaceFilter {
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			Surface gray=GrayscaleSurfaceFilter{}.Apply(src);
			detail::ConstPixBuf bGray(gray);
			Surface dst=SDL::DuplicateSurface(gray);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			static constexpr float Gx[9]={-1,0,1,-1,0,1,-1,0,1};
			static constexpr float Gy[9]={-1,-1,-1,0,0,0,1,1,1};
			fori (y, 0, bDst.h)
				fori (x, 0, bDst.w) {
					const float gx=detail::ConvolveGray(bGray,x,y,Gx,3);
					const float gy=detail::ConvolveGray(bGray,x,y,Gy,3);
					const Uint8 m=detail::Clamp8(SDL::Sqrt(gx*gx+gy*gy));
					bDst.Set(x,y,{m,m,m,bGray.Get(x,y).a});
				}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return "Prewitt"; }
	};

	struct LaplacianSurfaceFilter : SurfaceFilter {
		bool preBlur;
		explicit LaplacianSurfaceFilter(bool b=true) : preBlur(b) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			Surface in=GrayscaleSurfaceFilter{}.Apply(src);
			if (preBlur) in = GaussianBlurSurfaceFilter{1.f}.Apply(in);
			detail::ConstPixBuf bIn(in);
			Surface dst=SDL::DuplicateSurface(in);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			static constexpr float K[9]={0,1,0, 1,-4,1, 0,1,0};
			fori (y, 0, bDst.h)
				fori (x, 0, bDst.w) {
					const Uint8 v=detail::Clamp8(std::abs(detail::ConvolveGray(bIn,x,y,K,3)));
					bDst.Set(x,y,{v,v,v,bIn.Get(x,y).a});
				}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Laplacian(preBlur:{})", preBlur?"true":"false");
		}
	};

	struct EmbossSurfaceFilter : SurfaceFilter {
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			Surface gray=GrayscaleSurfaceFilter{}.Apply(src);
			detail::ConstPixBuf bGray(gray);
			Surface dst=SDL::DuplicateSurface(gray);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			static constexpr float K[9]={-2,-1,0,-1,1,1,0,1,2};
			fori (y, 0, bDst.h)
				fori (x, 0, bDst.w) {
					const Uint8 c=detail::Clamp8(detail::ConvolveGray(bGray,x,y,K,3)+128.f);
					bDst.Set(x,y,{c,c,c,bGray.Get(x,y).a});
				}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return "Emboss"; }
	};

	struct CannySurfaceFilter : SurfaceFilter {
		float sigma, loThresh, hiThresh;
		CannySurfaceFilter() = default;
		CannySurfaceFilter(float s, float lo, float hi) : sigma(s), loThresh(lo), hiThresh(hi) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			Surface blurred=GaussianBlurSurfaceFilter{sigma}.Apply(GrayscaleSurfaceFilter{}.Apply(src));
			detail::ConstPixBuf bBlur(blurred);
			Surface dst=SDL::DuplicateSurface(blurred);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const int W=bBlur.w, H=bBlur.h;
			auto idx=[W](int x,int y){return y*W+x;};

			static constexpr float GxK[9]={-1,0,1,-2,0,2,-1,0,1};
			static constexpr float GyK[9]={-1,-2,-1,0,0,0,1,2,1};
			std::vector<float> mag(W*H,0.f), angle(W*H,0.f);
			fori (y, 0, H) fori (x, 0, W) {
				const float gx = detail::ConvolveGray(bBlur,x,y,GxK,3);
				const float gy = detail::ConvolveGray(bBlur,x,y,GyK,3);
				mag[idx(x,y)] = SDL::Sqrt(gx*gx+gy*gy);
				float th = std::atan2(gy,gx)*(180.f/SDL::PI_F);
				angle[idx(x,y)] = th<0?th+180.f:th;
			}
			std::vector<float> nms(W*H,0.f);
			fori (y, 1, H-1) fori (x, 1, W-1) {
				const float a=angle[idx(x,y)], m=mag[idx(x,y)];
				float q,r;
				if      (a<22.5f||a>=157.5f) {q=mag[idx(x+1,y)];  r=mag[idx(x-1,y)];}
				else if (a<67.5f)            {q=mag[idx(x+1,y-1)];r=mag[idx(x-1,y+1)];}
				else if (a<112.5f)           {q=mag[idx(x,y-1)];  r=mag[idx(x,y+1)];}
				else                         {q=mag[idx(x-1,y-1)];r=mag[idx(x+1,y+1)];}
				nms[idx(x,y)]=(m>=q&&m>=r)?m:0.f;
			}
			static constexpr Uint8 kS=255,kW=75,kZ=0;
			std::vector<Uint8> edge(W*H,kZ);
			for (int i=0;i<W*H;++i)
				edge[i]=nms[i]>=hiThresh?kS:nms[i]>=loThresh?kW:kZ;
			fori (y, 1, H-1) fori (x, 1, W-1) {
				if (edge[idx(x,y)]!=kW) continue;
				bool conn=false;
				for (int dy = -1; (dy <= 1) && (!conn); ++dy)
					for (int dx = -1; (dx <= 1) && (!conn); ++dx)
						conn = edge[idx(x+dx,y+dy)] == kS;
				edge[idx(x,y)] = conn ? kS : kZ;
			}
			fori (y, 0, H) fori (x, 0, W) {
				const Uint8 c=edge[idx(x,y)];
				bDst.Set(x,y,{c,c,c,bBlur.Get(x,y).a});
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Canny(sigma:{:.1f},lo:{:.0f},hi:{:.0f})",sigma,loThresh,hiThresh);
		}
	};

	// ═════════════════════════════════════════════════════════════════════════════
	//  C. MORPHOLOGICAL
	// ═════════════════════════════════════════════════════════════════════════════

	struct DilationSurfaceFilter : SurfaceFilter {
		int radius;
		explicit DilationSurfaceFilter(int r=1) : radius(r) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			fori (y, 0, bDst.h) fori (x, 0, bDst.w) {
				Uint8 mR=0,mG=0,mB=0;
				fori_eq (dy, -radius, radius) {
					fori_eq (dx, -radius, radius) {
						const ColorRaw p=bSrc.GetClamped(x+dx,y+dy);
						if (p.r > mR) mR=p.r;
						if (p.g > mG) mG=p.g;
						if (p.b > mB) mB=p.b;
					}
				}
				bDst.Set(x,y,{mR,mG,mB,bSrc.Get(x,y).a});
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Dilation({})",radius); }
	};

	struct ErosionSurfaceFilter : SurfaceFilter {
		int radius;
		explicit ErosionSurfaceFilter(int r=1) : radius(r) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			fori (y, 0, bDst.h) fori (x, 0, bDst.w) {
				Uint8 mR=255,mG=255,mB=255;
				fori_eq (dy, -radius, radius) {
					fori_eq (dx, -radius, radius) {
						const ColorRaw p=bSrc.GetClamped(x+dx,y+dy);
						if (p.r < mR) mR = p.r;
						if (p.g < mG) mG = p.g;
						if (p.b < mB) mB = p.b;
					}
				}
				bDst.Set(x,y,{mR,mG,mB,bSrc.Get(x,y).a});
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Erosion({})",radius); }
	};

	struct MorphCloseSurfaceFilter : SurfaceFilter {
		int radius;
		explicit MorphCloseSurfaceFilter(int r=1) : radius(r) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			return ErosionSurfaceFilter{radius}.Apply(DilationSurfaceFilter{radius}.Apply(src));
		}
		[[nodiscard]] std::string Name() const override { return std::format("Morph Close({})",radius); }
	};

	// ═════════════════════════════════════════════════════════════════════════════
	//  D. STATISTICAL
	// ═════════════════════════════════════════════════════════════════════════════

	struct MedianSurfaceFilter : SurfaceFilter {
		int radius;
		explicit MedianSurfaceFilter(int r=1) : radius(r) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const int sz=(2*radius+1)*(2*radius+1);
			// Stack allocation for radius<=3 (sz<=49) — no heap pressure per pixel
			if (radius<=3) {
				std::array<Uint8,49> rv,gv,bv;
				fori (y, 0, bDst.h) fori (x, 0, bDst.w) {
					int k=0;
					fori_eq (dy, -radius, radius) {
						fori_eq (dx, -radius, radius) {
							const ColorRaw p=bSrc.GetClamped(x+dx,y+dy);
							rv[k]=p.r;gv[k]=p.g;bv[k]=p.b;++k;
						}
					}
					auto med=[&](std::array<Uint8,49>& v){
						std::nth_element(v.begin(),v.begin()+k/2,v.begin()+k); return v[k/2];
					};
					bDst.Set(x,y,{med(rv),med(gv),med(bv),bSrc.Get(x,y).a});
				}
			} else {
				std::vector<Uint8> rv(sz),gv(sz),bv(sz);
				fori (y, 0, bDst.h) fori (x, 0, bDst.w) {
					int k=0;
					fori_eq (dy, -radius, radius) {
						fori_eq (dx, -radius, radius) {
							const ColorRaw p=bSrc.GetClamped(x+dx,y+dy);
							rv[k]=p.r;gv[k]=p.g;bv[k]=p.b;++k;
						}
					}
					auto med=[&](std::vector<Uint8>& v){
						std::nth_element(v.begin(),v.begin()+sz/2,v.end()); return v[sz/2];
					};
					bDst.Set(x,y,{med(rv),med(gv),med(bv),bSrc.Get(x,y).a});
				}
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Median({})",radius); }
	};

	struct MinSurfaceFilter : SurfaceFilter {
		Uint8 minimum;
		explicit MinSurfaceFilter(Uint8 m=0) : minimum(m) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const int total=bDst.w*bDst.h; const Uint8 mn=minimum;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4; Uint8* d=bDst.pixels+i*4;
				d[0]=s[0]<mn?mn:s[0]; d[1]=s[1]<mn?mn:s[1];
				d[2]=s[2]<mn?mn:s[2]; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Min({})",minimum); }
	};

	struct MaxSurfaceFilter : SurfaceFilter {
		Uint8 maximum;
		explicit MaxSurfaceFilter(Uint8 m=255) : maximum(m) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const int total=bDst.w*bDst.h; const Uint8 mx=maximum;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4; Uint8* d=bDst.pixels+i*4;
				d[0]=s[0]>mx?mx:s[0]; d[1]=s[1]>mx?mx:s[1];
				d[2]=s[2]>mx?mx:s[2]; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Max({})",maximum); }
	};

	// ═════════════════════════════════════════════════════════════════════════════
	//  E. BILATERAL — precomputed spatial table + exp LUT for color distance
	// ═════════════════════════════════════════════════════════════════════════════
	struct BilateralSurfaceFilter : SurfaceFilter {
		float sigmaSpace, sigmaColor;
		BilateralSurfaceFilter(float ss=3.f, float sc=30.f) : sigmaSpace(ss), sigmaColor(sc) {}

		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);

			const int r=static_cast<int>(std::ceil(sigmaSpace*3.f));
			const float ss2=2.f*sigmaSpace*sigmaSpace;
			const float sc2=2.f*sigmaColor*sigmaColor;

			// Spatial weight table [2r+1]²  — built once, ~(kd)² floats
			const int kd=2*r+1;
			std::vector<float> spatW(kd*kd);
			fori_eq (dy, -r, r) fori_eq (dx, -r, r)
				spatW[(dy+r)*kd+(dx+r)]=SDL::Exp(-float(dx*dx+dy*dy)/ss2);

			// Color exp LUT: index = colorDist² / 3  (covers [0, 255²*3/3]=65025)
			// 65025 entries of float = ~256 KB — fits in L2 cache
			constexpr int kCLut=65026;
			std::vector<float> colorW(kCLut);
			const float inv3sc2=3.f/sc2;
			for (int i=0;i<kCLut;++i)
				colorW[i]=SDL::Exp(-static_cast<float>(i)*inv3sc2);

			fori (y, 0, bDst.h) fori (x, 0, bDst.w) {
				const ColorRaw ctr=bSrc.Get(x,y);
				float wr=0,wg=0,wb=0,tw=0;
				fori_eq (dy, -r, r) fori_eq (dx, -r, r) {
					const ColorRaw nb=bSrc.GetClamped(x+dx,y+dy);
					const float ws=spatW[(dy+r)*kd+(dx+r)];
					const int dr=nb.r-ctr.r, dg=nb.g-ctr.g, db=nb.b-ctr.b;
					const int cd2=(dr*dr+dg*dg+db*db)/3; // /3 to match LUT indexing
					const float wc=colorW[SDL::Min(cd2,kCLut-1)];
					const float w=ws*wc;
					wr+=w*nb.r; wg+=w*nb.g; wb+=w*nb.b; tw+=w;
				}
				bDst.Set(x,y,{detail::Clamp8(wr/tw),detail::Clamp8(wg/tw),
											detail::Clamp8(wb/tw),ctr.a});
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override {
			return std::format("Bilateral(sigma space:{},sigma color:{})",sigmaSpace,sigmaColor);
		}
	};

	// ═════════════════════════════════════════════════════════════════════════════
	//  F. FLOYD-STEINBERG DITHER — interleaved error buffer
	// ═════════════════════════════════════════════════════════════════════════════
	struct FloydSteinbergDitherSurfaceFilter : SurfaceFilter {
		int levels;
		explicit FloydSteinbergDitherSurfaceFilter(int l=2) : levels(SDL::Max(2,l)) {}
		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const int W=bDst.w, H=bDst.h;
			const float step=255.f/static_cast<float>(levels-1);

			// Interleaved RGBA error buffer — better spatial locality than 3 separate arrays
			std::vector<float> err(W*H*4, 0.f);
			for (int i=0;i<W*H;++i) {
				const Uint8* s=bSrc.pixels+i*4;
				err[i*4]=s[0]; err[i*4+1]=s[1]; err[i*4+2]=s[2];
			}

			auto spread=[&](int x, int y, float er, float eg, float eb) {
				auto add=[&](int nx,int ny,float w){
					if(nx<0||nx>=W||ny<0||ny>=H) return;
					float* e=err.data()+(ny*W+nx)*4;
					e[0]+=er*w; e[1]+=eg*w; e[2]+=eb*w;
				};
				add(x+1,y,7.f/16.f); add(x-1,y+1,3.f/16.f);
				add(x,  y+1,5.f/16.f); add(x+1,y+1,1.f/16.f);
			};

			fori (y, 0, H) fori (x, 0, W) {
				float* e=err.data()+(y*W+x)*4;
				const float nr=std::clamp(e[0],0.f,255.f);
				const float ng=std::clamp(e[1],0.f,255.f);
				const float nb=std::clamp(e[2],0.f,255.f);
				const float qr=std::round(nr/step)*step;
				const float qg=std::round(ng/step)*step;
				const float qb=std::round(nb/step)*step;
				bDst.Set(x,y,{detail::Clamp8(qr),detail::Clamp8(qg),
											detail::Clamp8(qb),bSrc.Get(x,y).a});
				spread(x,y,nr-qr,ng-qg,nb-qb);
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return std::format("Dither({})",levels); }
	};

	// ═════════════════════════════════════════════════════════════════════════════
	//  G. LUT
	// ═════════════════════════════════════════════════════════════════════════════
	struct LutSurfaceFilter : SurfaceFilter {
		using LutTable=std::array<Uint8,256>;
		LutTable lutR, lutG, lutB;
		LutSurfaceFilter() { for(int i=0;i<256;++i) lutR[i]=lutG[i]=lutB[i]=static_cast<Uint8>(i); }
		LutSurfaceFilter(LutTable r, LutTable g, LutTable b)
			: lutR(std::move(r)), lutG(std::move(g)), lutB(std::move(b)) {}

		[[nodiscard]] Surface Apply(SurfaceConstRef src) const override {
			detail::ConstPixBuf bSrc(src);
			Surface dst=SDL::DuplicateSurface(src);
			auto lDst=dst.Lock(); detail::PixBuf bDst(lDst);
			const int total=bDst.w*bDst.h;
			fori (i, 0, total) {
				const Uint8* s=bSrc.pixels+i*4; Uint8* d=bDst.pixels+i*4;
				d[0]=lutR[s[0]]; d[1]=lutG[s[1]]; d[2]=lutB[s[2]]; d[3]=s[3];
			}
			return dst;
		}
		[[nodiscard]] std::string Name() const override { return "LUT"; }

		[[nodiscard]] static LutSurfaceFilter MakeInvert() {
			LutTable r,g,b;
			for(int i=0;i<256;++i) r[i]=g[i]=b[i]=static_cast<Uint8>(255-i);
			return {r,g,b};
		}
		[[nodiscard]] static LutSurfaceFilter MakeWarmth(int amount=30) {
			LutTable r,g,b;
			for(int i=0;i<256;++i){
				r[i]=detail::Clamp8(static_cast<float>(i)+amount);
				g[i]=static_cast<Uint8>(i);
				b[i]=detail::Clamp8(static_cast<float>(i)-amount);
			}
			return {r,g,b};
		}
		[[nodiscard]] static LutSurfaceFilter MakeNightVision() {
			LutTable r,g,b;
			for(int i=0;i<256;++i){r[i]=0;g[i]=static_cast<Uint8>(i);b[i]=0;}
			return {r,g,b};
		}
		[[nodiscard]] static LutSurfaceFilter MakeVintage() {
			LutTable r,g,b;
			for(int i=0;i<256;++i){
				const float t=i/255.f;
				r[i]=detail::Clamp8(t*220.f+20.f);
				g[i]=detail::Clamp8(t*210.f+15.f);
				b[i]=detail::Clamp8(t*180.f+30.f);
			}
			return {r,g,b};
		}
	};

	// ─────────────────────────────────────────────────────────────────────────────
	class SurfaceFilterRegistry {
	public:
		void Add(std::unique_ptr<SurfaceFilter> f) { m_filters.push_back(std::move(f)); }
		void Next() { if(!m_filters.empty()) m_index=(m_index+1)%m_filters.size(); }
		void Prev() { if(!m_filters.empty()) m_index=(m_index+m_filters.size()-1)%m_filters.size(); }

		[[nodiscard]] Surface ApplyCurrent(SurfaceConstRef src) const {
			Surface rgba=detail::ToRGBA(src);
			if (m_filters.empty()) return rgba;
			return m_filters[m_index]->Apply(rgba);
		}
		[[nodiscard]] std::string CurrentName() const {
			return m_filters.empty() ? "(none)" : m_filters[m_index]->Name();
		}
		[[nodiscard]] size_t CurrentIndex() const noexcept { return m_index; }
		[[nodiscard]] size_t Count()        const noexcept { return m_filters.size(); }
		[[nodiscard]] bool   Empty()        const noexcept { return m_filters.empty(); }
		[[nodiscard]] const SurfaceFilter& operator[](size_t i) const { return *m_filters.at(i); }

	private:
		std::vector<std::unique_ptr<SurfaceFilter>> m_filters;
		size_t m_index = 0;
	};

} // namespace SDL

#endif // SDL3PP_FILTERS_H_