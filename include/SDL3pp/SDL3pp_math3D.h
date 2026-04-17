#ifndef SDL3PP_MATH3D_H_
#define SDL3PP_MATH3D_H_

/**
 * @file SDL3pp_math3D.h
 *
 * 3D mathematics for rendering: vectors, matrices, quaternions, and
 * geometric primitives.
 *
 * Conventions:
 *   - Right-handed coordinate system (camera looks down −Z)
 *   - Column-major matrix storage matching GLSL `mat4`:
 *     element at row `r`, column `c` is `FMatrix4::m[c * 4 + r]`
 *   - Perspective / ortho projections target **Vulkan** clip space:
 *     Y-down NDC, depth range Z ∈ [0, 1]
 *   - `nearZ` / `farZ` are positive distances from the eye
 *
 * No SDL headers are required; include this file standalone or after
 * including SDL3pp.h.
 */

#include <algorithm>
#include <cmath>
#include "SDL3pp_stdinc.h"
#include "SDL3pp_pixels.h"

namespace SDL {

/**
 * @defgroup CategoryMath3D 3D Mathematics
 *
 * Vectors (FVector2, FVector3, FVector4), FMatrix4, FQuaternion, FAABB, FPlane, FRay, and FFrustum.
 *
 * @{
 */

// ── Forward declarations ───────────────────────────────────────────────────────

struct FVector2;
struct FVector3;
struct FVector4;
struct FMatrix4;
struct FQuaternion;
struct FAABB;
struct FPlane;
struct FRay;
struct FFrustum;

// ── FVector2 ───────────────────────────────────────────────────────────────────────

/**
 * 2D floating-point vector.
 */
struct FVector2 : FPoint {

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FVector2() noexcept = default;
	constexpr FVector2(float x, float y) noexcept : FPoint(x, y) {}
	explicit constexpr FVector2(float s) noexcept : FPoint(s, s) {}
	constexpr FVector2(const FPoint& p) noexcept : FPoint(p.x, p.y) {}

	// ── Math operations ───────────────────────────────────────────────────

	/// Dot product.
	[[nodiscard]] constexpr float Dot(const FVector2& o) const noexcept {
		return x * o.x + y * o.y;
	}

	/// Squared length (avoids Sqrt).
	[[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y; }

	/// Euclidean length.
	[[nodiscard]] float Length() const noexcept { return SDL::Sqrt(LengthSq()); }

	/// Normalised copy (unit length). Returns Zero vector when near-Zero.
	[[nodiscard]] FVector2 Normalize() const noexcept {
		float len = Length();
		return (len > 1e-8f) ? FVector2(*this / len) : FVector2{};
	}

	/// Linear interpolation toward `to` by factor `t`.
	[[nodiscard]] constexpr FVector2 Lerp(const FVector2& to, float t) const noexcept {
		return {x + (to.x - x) * t, y + (to.y - y) * t};
	}
};

[[nodiscard]] constexpr FVector2 operator*(float s, const FVector2& v) noexcept { return FVector2(v * s); }
[[nodiscard]] constexpr FVector2 operator/(float s, const FVector2& v) noexcept { return FVector2(v * (1/s)); }

// ── FVector3 ───────────────────────────────────────────────────────────────────────

/**
 * 3D floating-point vector.
 */
struct FVector3 {
	float x = 0.f, y = 0.f, z = 0.f;

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FVector3() noexcept                             = default;
	constexpr FVector3(float x, float y, float z) noexcept   : x{x}, y{y}, z{z} {}
	explicit constexpr FVector3(float s) noexcept             : x{s}, y{s}, z{s} {}
	constexpr FVector3(const FVector2& v, float z = 0.f) noexcept : x{v.x}, y{v.y}, z{z} {}

	// ── Arithmetic ────────────────────────────────────────────────────────
	[[nodiscard]] constexpr FVector3 operator+(const FVector3& o) const noexcept { return {x + o.x, y + o.y, z + o.z}; }
	[[nodiscard]] constexpr FVector3 operator-(const FVector3& o) const noexcept { return {x - o.x, y - o.y, z - o.z}; }
	[[nodiscard]] constexpr FVector3 operator*(const FVector3& o) const noexcept { return {x * o.x, y * o.y, z * o.z}; }
	[[nodiscard]] constexpr FVector3 operator/(const FVector3& o) const noexcept { return {x / o.x, y / o.y, z / o.z}; }
	[[nodiscard]] constexpr FVector3 operator*(float s)        const noexcept { return {x * s,   y * s,   z * s};   }
	[[nodiscard]] constexpr FVector3 operator/(float s)        const noexcept { return {x / s,   y / s,   z / s};   }
	[[nodiscard]] constexpr FVector3 operator-()               const noexcept { return {-x, -y, -z};                 }

	constexpr FVector3& operator+=(const FVector3& o) noexcept { x += o.x; y += o.y; z += o.z; return *this; }
	constexpr FVector3& operator-=(const FVector3& o) noexcept { x -= o.x; y -= o.y; z -= o.z; return *this; }
	constexpr FVector3& operator*=(float s)       noexcept { x *= s;   y *= s;   z *= s;   return *this; }
	constexpr FVector3& operator/=(float s)       noexcept { x /= s;   y /= s;   z /= s;   return *this; }

	[[nodiscard]] constexpr bool operator==(const FVector3& o) const noexcept { return x == o.x && y == o.y && z == o.z; }
	[[nodiscard]] constexpr bool operator!=(const FVector3& o) const noexcept { return !(*this == o); }

	// ── Math operations ───────────────────────────────────────────────────

	/// Dot product.
	[[nodiscard]] constexpr float Dot(const FVector3& o) const noexcept {
		return x * o.x + y * o.y + z * o.z;
	}

	/// Cross product (`this × o`).
	[[nodiscard]] constexpr FVector3 Cross(const FVector3& o) const noexcept {
		return {y * o.z - z * o.y,
						z * o.x - x * o.z,
						x * o.y - y * o.x};
	}

	/// Squared length (avoids Sqrt).
	[[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y + z * z; }

	/// Euclidean length.
	[[nodiscard]] float Length() const noexcept { return SDL::Sqrt(LengthSq()); }

	/// Normalised copy (unit length). Returns Zero vector when near-Zero.
	[[nodiscard]] FVector3 Normalize() const noexcept {
		float len = Length();
		return len > 1e-8f ? (*this / len) : FVector3{};
	}

	/// Linear interpolation toward `to` by factor `t`.
	[[nodiscard]] constexpr FVector3 Lerp(const FVector3& to, float t) const noexcept {
		return {x + (to.x - x) * t,
						y + (to.y - y) * t,
						z + (to.z - z) * t};
	}

	/// Reflect this vector about a unit normal `n`.
	[[nodiscard]] constexpr FVector3 Reflect(const FVector3& n) const noexcept {
		return *this - n * (2.f * Dot(n));
	}

	/// XY components.
	[[nodiscard]] constexpr FVector2 XY() const noexcept { return {x, y}; }
};

[[nodiscard]] constexpr FVector3 operator*(float s, const FVector3& v) noexcept { return v * s; }
[[nodiscard]] constexpr FVector3 operator/(float s, const FVector3& v) noexcept { return v * (1/s); }

// ── FVector4 ───────────────────────────────────────────────────────────────────────

/**
 * 4D floating-point vector (homogeneous position, colour, or general use).
 *
 * Default `w = 0` (direction / colour); use `FVector4(v, 1.f)` for a position.
 */
struct FVector4 {
	float x = 0.f, y = 0.f, z = 0.f, w = 0.f;

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FVector4() noexcept = default;
	constexpr FVector4(float x, float y, float z, float w = 0.f) noexcept  : x{x}, y{y}, z{z}, w{w} {}
	explicit constexpr FVector4(float s) noexcept : x{s}, y{s}, z{s}, w{s} {}
	constexpr FVector4(const FVector3& v, float w = 0.f) noexcept : x{v.x}, y{v.y}, z{v.z}, w{w} {}
	constexpr FVector4(const FVector2& v, float z = 0.f, float w = 0.f) noexcept : x{v.x}, y{v.y}, z{z}, w{w} {}

	// ── Arithmetic ────────────────────────────────────────────────────────
	[[nodiscard]] constexpr FVector4 operator+(const FVector4& o) const noexcept { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
	[[nodiscard]] constexpr FVector4 operator-(const FVector4& o) const noexcept { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
	[[nodiscard]] constexpr FVector4 operator*(float s) const noexcept { return {x * s,   y * s,   z * s,   w * s};   }
	[[nodiscard]] constexpr FVector4 operator/(float s) const noexcept { return {x / s,   y / s,   z / s,   w / s};   }
	[[nodiscard]] constexpr FVector4 operator-() const noexcept { return {-x, -y, -z, -w}; }

	constexpr FVector4& operator+=(const FVector4& o) noexcept { x += o.x; y += o.y; z += o.z; w += o.w; return *this; }
	constexpr FVector4& operator-=(const FVector4& o) noexcept { x -= o.x; y -= o.y; z -= o.z; w -= o.w; return *this; }
	constexpr FVector4& operator*=(float s) noexcept { x *= s;   y *= s;   z *= s;   w *= s;   return *this; }
	constexpr FVector4& operator/=(float s) noexcept { x /= s;   y /= s;   z /= s;   w /= s;   return *this; }

	[[nodiscard]] constexpr bool operator==(const FVector4& o) const noexcept { return x == o.x && y == o.y && z == o.z && w == o.w; }
	[[nodiscard]] constexpr bool operator!=(const FVector4& o) const noexcept { return !(*this == o); }

	// ── Math operations ───────────────────────────────────────────────────

	/// 4D dot product.
	[[nodiscard]] constexpr float Dot(const FVector4& o) const noexcept {
		return x * o.x + y * o.y + z * o.z + w * o.w;
	}

	/// Squared length.
	[[nodiscard]] constexpr float LengthSq() const noexcept { return x * x + y * y + z * z + w * w; }

	/// Euclidean length.
	[[nodiscard]] float Length() const noexcept { return SDL::Sqrt(LengthSq()); }

	/// Normalised copy. Returns Zero vector when near-Zero.
	[[nodiscard]] FVector4 Normalize() const noexcept {
		float len = Length();
		return len > 1e-8f ? (*this / len) : FVector4{};
	}

	/// Perspective division: `{x/w, y/w, z/w}`.
	[[nodiscard]] FVector3 PerspDiv() const noexcept { return {x / w, y / w, z / w}; }

	/// Linear interpolation toward `to` by factor `t`.
	[[nodiscard]] constexpr FVector4 Lerp(const FVector4& to, float t) const noexcept {
		return {x + (to.x - x) * t,
						y + (to.y - y) * t,
						z + (to.z - z) * t,
						w + (to.w - w) * t};
	}

	/// XYZ components.
	[[nodiscard]] constexpr FVector3 XYZ() const noexcept { return {x, y, z}; }

	/// XY components.
	[[nodiscard]] constexpr FVector2 XY() const noexcept { return {x, y}; }
};

[[nodiscard]] constexpr FVector4 operator*(float s, const FVector4& v) noexcept { return v * s; }
[[nodiscard]] constexpr FVector4 operator/(float s, const FVector4& v) noexcept { return v * (1/s); }

// ── FMatrix4 ───────────────────────────────────────────────────────────────────────

/**
 * Column-major 4×4 floating-point matrix.
 *
 * Storage layout: `m[col * 4 + row]` — same convention as GLSL `mat4`.
 *
 * All projection functions target Vulkan clip space (Y-down, Z ∈ [0, 1]).
 */
struct FMatrix4 {
	float m[16] = {};

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FMatrix4() noexcept = default;

	// ── Element access ────────────────────────────────────────────────────

	/// Element at row `r`, column `c`.
	[[nodiscard]] constexpr float& At(int r, int c) noexcept       { return m[c * 4 + r]; }
	[[nodiscard]] constexpr float  At(int r, int c) const noexcept { return m[c * 4 + r]; }

	// ── Factory methods ───────────────────────────────────────────────────

	/// Identity matrix.
	[[nodiscard]] static constexpr FMatrix4 Identity() noexcept {
		FMatrix4 r;
		r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
		return r;
	}

	/**
	 * Translation matrix.
	 *
	 * @param x,y,z Translation components.
	 */
	[[nodiscard]] static constexpr FMatrix4 Translate(float x, float y, float z) noexcept {
		FMatrix4 r    = Identity();
		r.m[12]   = x;
		r.m[13]   = y;
		r.m[14]   = z;
		return r;
	}

	/// Translation matrix from a FVector3.
	[[nodiscard]] static constexpr FMatrix4 Translate(const FVector3& t) noexcept {
		return Translate(t.x, t.y, t.z);
	}

	/**
	 * Non-uniform scale matrix.
	 *
	 * @param x,y,z Scale factors per axis.
	 */
	[[nodiscard]] static constexpr FMatrix4 Scale(float x, float y, float z) noexcept {
		FMatrix4 r;
		r.m[0]  = x;
		r.m[5]  = y;
		r.m[10] = z;
		r.m[15] = 1.f;
		return r;
	}

	/// Uniform scale matrix.
	[[nodiscard]] static constexpr FMatrix4 Scale(float s) noexcept { return Scale(s, s, s); }

	/// Scale matrix from a FVector3.
	[[nodiscard]] static constexpr FMatrix4 Scale(const FVector3& s) noexcept { return Scale(s.x, s.y, s.z); }

	/**
	 * Rotation around the X axis.
	 *
	 * @param a Angle in radians (right-hand rule: thumb along +X).
	 */
	[[nodiscard]] static FMatrix4 RotateX(float a) noexcept {
		float c = SDL::Cos(a), s = SDL::Sin(a);
		FMatrix4  r = Identity();
		r.m[5]  =  c;
		r.m[6]  =  s;
		r.m[9]  = -s;
		r.m[10] =  c;
		return r;
	}

	/**
	 * Rotation around the Y axis.
	 *
	 * @param a Angle in radians.
	 */
	[[nodiscard]] static FMatrix4 RotateY(float a) noexcept {
		float c = SDL::Cos(a), s = SDL::Sin(a);
		FMatrix4  r = Identity();
		r.m[0]  =  c;
		r.m[2]  = -s;
		r.m[8]  =  s;
		r.m[10] =  c;
		return r;
	}

	/**
	 * Rotation around the Z axis.
	 *
	 * @param a Angle in radians.
	 */
	[[nodiscard]] static FMatrix4 RotateZ(float a) noexcept {
		float c = SDL::Cos(a), s = SDL::Sin(a);
		FMatrix4  r = Identity();
		r.m[0] =  c;
		r.m[1] =  s;
		r.m[4] = -s;
		r.m[5] =  c;
		return r;
	}

	/**
	 * Rotation around an arbitrary (normalised) axis — Rodrigues' formula.
	 *
	 * @param axis  Normalised rotation axis.
	 * @param angle Angle in radians.
	 */
	[[nodiscard]] static FMatrix4 Rotate(const FVector3& axis, float angle) noexcept {
		float c = SDL::Cos(angle);
		float s = SDL::Sin(angle);
		float t = 1.f - c;
		float x = axis.x, y = axis.y, z = axis.z;

		FMatrix4 r;
		// Column 0
		r.m[0]  = t*x*x + c;
		r.m[1]  = t*x*y + s*z;
		r.m[2]  = t*x*z - s*y;
		r.m[3]  = 0.f;
		// Column 1
		r.m[4]  = t*x*y - s*z;
		r.m[5]  = t*y*y + c;
		r.m[6]  = t*y*z + s*x;
		r.m[7]  = 0.f;
		// Column 2
		r.m[8]  = t*x*z + s*y;
		r.m[9]  = t*y*z - s*x;
		r.m[10] = t*z*z + c;
		r.m[11] = 0.f;
		// Column 3
		r.m[12] = 0.f;
		r.m[13] = 0.f;
		r.m[14] = 0.f;
		r.m[15] = 1.f;
		return r;
	}

	/**
	 * Perspective projection (right-handed, Vulkan clip space).
	 *
	 * Maps the view frustum to clip space with Y-down NDC and Z ∈ [0, 1].
	 *
	 * @param fovY   Vertical field-of-view in radians.
	 * @param aspect Width / height ratio.
	 * @param nearZ  Near clip distance (positive, > 0).
	 * @param farZ   Far clip distance (positive, > nearZ).
	 */
	[[nodiscard]] static FMatrix4 Perspective(float fovY, float aspect,
																				float nearZ, float farZ) noexcept {
		float f = 1.f / SDL::Tan(fovY * 0.5f);
		FMatrix4  r;
		r.m[0]  =  f / aspect;
		r.m[5]  = -f;                              // flip Y for Vulkan NDC
		r.m[10] =  farZ / (nearZ - farZ);          // near→0, far→1
		r.m[11] = -1.f;
		r.m[14] =  nearZ * farZ / (nearZ - farZ);
		return r;
	}

	/**
	 * Orthographic projection (right-handed, Vulkan clip space).
	 *
	 * Maps the view box to clip space with Y-down NDC and Z ∈ [0, 1].
	 *
	 * @param left,right  X bounds in view space.
	 * @param bottom,top  Y bounds in view space.
	 * @param nearZ,farZ  Positive clip distances from the eye.
	 */
	[[nodiscard]] static constexpr FMatrix4 Ortho(float left,  float right,
																						 float bottom, float top,
																						 float nearZ,  float farZ) noexcept {
		FMatrix4 r;
		r.m[0]  =  2.f / (right - left);
		r.m[5]  = -2.f / (top - bottom);           // flip Y for Vulkan NDC
		r.m[10] = -1.f / (farZ - nearZ);           // near→0, far→1
		r.m[12] = -(right + left) / (right - left);
		r.m[13] =  (top + bottom) / (top - bottom);
		r.m[14] = -nearZ / (farZ - nearZ);
		r.m[15] =  1.f;
		return r;
	}

	/**
	 * View matrix (right-handed LookAt).
	 *
	 * @param eye    Camera position in ECS::Context space.
	 * @param center Point the camera looks toward.
	 * @param up     ECS::Context up direction (typically {0, 1, 0}).
	 */
	[[nodiscard]] static FMatrix4 LookAt(const FVector3& eye,
																		const FVector3& center,
																		const FVector3& up) noexcept {
		FVector3 f = (center - eye).Normalize(); // forward  (+Z in ECS::Context → −Z in view)
		FVector3 r = f.Cross(up).Normalize();    // right
		FVector3 u = r.Cross(f);                 // re-orthogonalised up

		FMatrix4 res;
		// Column 0
		res.m[0]  =  r.x;
		res.m[1]  =  u.x;
		res.m[2]  = -f.x;
		res.m[3]  =  0.f;
		// Column 1
		res.m[4]  =  r.y;
		res.m[5]  =  u.y;
		res.m[6]  = -f.y;
		res.m[7]  =  0.f;
		// Column 2
		res.m[8]  =  r.z;
		res.m[9]  =  u.z;
		res.m[10] = -f.z;
		res.m[11] =  0.f;
		// Column 3 — translation
		res.m[12] = -r.Dot(eye);
		res.m[13] = -u.Dot(eye);
		res.m[14] =  f.Dot(eye);
		res.m[15] =  1.f;
		return res;
	}

	// ── Operators ─────────────────────────────────────────────────────────

	/// Column-major matrix multiplication: `C = this * b`.
	[[nodiscard]] constexpr FMatrix4 operator*(const FMatrix4& b) const noexcept {
		FMatrix4 r;
		for (int col = 0; col < 4; ++col)
			for (int row = 0; row < 4; ++row) {
				float v = 0.f;
				for (int k = 0; k < 4; ++k)
					v += m[k * 4 + row] * b.m[col * 4 + k];
				r.m[col * 4 + row] = v;
			}
		return r;
	}

	constexpr FMatrix4& operator*=(const FMatrix4& b) noexcept { *this = *this * b; return *this; }

	/// Transform a FVector4: `this * v`.
	[[nodiscard]] constexpr FVector4 operator*(const FVector4& v) const noexcept {
		return {m[ 0]*v.x + m[ 4]*v.y + m[ 8]*v.z + m[12]*v.w,
						m[ 1]*v.x + m[ 5]*v.y + m[ 9]*v.z + m[13]*v.w,
						m[ 2]*v.x + m[ 6]*v.y + m[10]*v.z + m[14]*v.w,
						m[ 3]*v.x + m[ 7]*v.y + m[11]*v.z + m[15]*v.w};
	}

	// ── Geometry helpers ──────────────────────────────────────────────────

	/**
	 * Transform a point (w = 1) and apply perspective division.
	 *
	 * Use when the matrix may contain a projection.
	 */
	[[nodiscard]] FVector3 TransformPoint(const FVector3& p) const noexcept {
		return (*this * FVector4{p, 1.f}).PerspDiv();
	}

	/**
	 * Transform a direction (w = 0) — ignores the translation column.
	 */
	[[nodiscard]] constexpr FVector3 TransformDir(const FVector3& d) const noexcept {
		return (*this * FVector4{d, 0.f}).XYZ();
	}

	/// Transpose.
	[[nodiscard]] constexpr FMatrix4 Transpose() const noexcept {
		FMatrix4 r;
		for (int c = 0; c < 4; ++c)
			for (int row = 0; row < 4; ++row)
				r.m[row * 4 + c] = m[c * 4 + row];
		return r;
	}

	/**
	 * General inverse (Gauss-Jordan elimination with partial pivoting).
	 *
	 * Returns Identity() if the matrix is singular.
	 */
	[[nodiscard]] FMatrix4 Inverse() const noexcept {
		float a[4][8];
		for (int r = 0; r < 4; ++r) {
			for (int c = 0; c < 4; ++c)
				a[r][c] = m[c * 4 + r]; // row-major copy of this matrix
			for (int c = 4; c < 8; ++c)
				a[r][c] = (c - 4 == r) ? 1.f : 0.f; // augment with identity
		}

		for (int col = 0; col < 4; ++col) {
			// Partial pivot
			int   pivot = col;
			float pval  = SDL::Abs(a[col][col]);
			for (int r = col + 1; r < 4; ++r) {
				if (SDL::Abs(a[r][col]) > pval) {
					pval  = SDL::Abs(a[r][col]);
					pivot = r;
				}
			}
			if (pivot != col)
				for (int c = 0; c < 8; ++c)
					std::swap(a[col][c], a[pivot][c]);

			float diag = a[col][col];
			if (SDL::Abs(diag) < 1e-8f) return Identity(); // singular

			float inv = 1.f / diag;
			for (int c = 0; c < 8; ++c) a[col][c] *= inv;

			for (int r = 0; r < 4; ++r) {
				if (r == col) continue;
				float f = a[r][col];
				for (int c = 0; c < 8; ++c)
					a[r][c] -= f * a[col][c];
			}
		}

		FMatrix4 res;
		for (int r = 0; r < 4; ++r)
			for (int c = 0; c < 4; ++c)
				res.m[c * 4 + r] = a[r][c + 4];
		return res;
	}

	/// Raw pointer to the 16 floats — for GPU uploads.
	[[nodiscard]] const float* data() const noexcept { return m; }
	[[nodiscard]] float*       data()       noexcept { return m; }
};

// ── FQuaternion ────────────────────────────────────────────────────────────────

/**
 * Unit quaternion representing a 3D rotation.
 *
 * Stored as `(x, y, z, w)` where `w` is the scalar part.
 * Assumes the quaternion is unit-length for rotation operations.
 */
struct FQuaternion {
	float x = 0.f, y = 0.f, z = 0.f, w = 1.f;

	// ── Constructors ──────────────────────────────────────────────────────
	constexpr FQuaternion() noexcept = default;
	constexpr FQuaternion(float x, float y, float z, float w) noexcept
		: x{x}, y{y}, z{z}, w{w} {}

	// ── Factory methods ───────────────────────────────────────────────────

	/// Identity quaternion (no rotation).
	[[nodiscard]] static constexpr FQuaternion Identity() noexcept { return {0.f, 0.f, 0.f, 1.f}; }

	/**
	 * Build from a normalised axis and an angle.
	 *
	 * @param axis  Normalised rotation axis.
	 * @param angle Rotation angle in radians.
	 */
	[[nodiscard]] static FQuaternion FromAxisAngle(const FVector3& axis, float angle) noexcept {
		float s = SDL::Sin(angle * 0.5f);
		float c = SDL::Cos(angle * 0.5f);
		return {axis.x * s, axis.y * s, axis.z * s, c};
	}

	/**
	 * Build from Euler angles (ZXY convention: roll, then pitch, then yaw).
	 *
	 * @param pitch Rotation around X in radians.
	 * @param yaw   Rotation around Y in radians.
	 * @param roll  Rotation around Z in radians.
	 */
	[[nodiscard]] static FQuaternion FromEuler(float pitch, float yaw, float roll) noexcept {
		float cp = SDL::Cos(pitch * 0.5f), sp = SDL::Sin(pitch * 0.5f);
		float cy = SDL::Cos(yaw   * 0.5f), sy = SDL::Sin(yaw   * 0.5f);
		float cr = SDL::Cos(roll  * 0.5f), sr = SDL::Sin(roll  * 0.5f);
		return {cr*sp*cy + sr*cp*sy,
						cr*cp*sy - sr*sp*cy,
						sr*cp*cy - cr*sp*sy,
						cr*cp*cy + sr*sp*sy};
	}

	/**
	 * Build the shortest rotation from direction `from` to direction `to`
	 * (both should be normalised).
	 */
	[[nodiscard]] static FQuaternion FromTo(const FVector3& from, const FVector3& to) noexcept {
		FVector3  axis = from.Cross(to);
		float dot  = from.Dot(to);
		// Cos(θ/2) = Sqrt((1+dot)/2),  Sin(θ/2) = |axis| / (2 * Cos(θ/2))
		float w    = SDL::Sqrt((1.f + dot) * 0.5f);
		float s    = (w > 1e-8f) ? 0.5f / w : 0.f;
		return FQuaternion{axis.x * s, axis.y * s, axis.z * s, w}.Normalize();
	}

	// ── Operations ────────────────────────────────────────────────────────

	/// Hamilton product.
	[[nodiscard]] constexpr FQuaternion operator*(const FQuaternion& o) const noexcept {
		return { w*o.x + x*o.w + y*o.z - z*o.y,
						 w*o.y - x*o.z + y*o.w + z*o.x,
						 w*o.z + x*o.y - y*o.x + z*o.w,
						 w*o.w - x*o.x - y*o.y - z*o.z};
	}

	constexpr FQuaternion& operator*=(const FQuaternion& o) noexcept { *this = *this * o; return *this; }

	/// Conjugate (equals inverse for unit quaternions).
	[[nodiscard]] constexpr FQuaternion Conjugate() const noexcept { return {-x, -y, -z, w}; }

	/// Squared norm.
	[[nodiscard]] constexpr float NormSq() const noexcept { return x*x + y*y + z*z + w*w; }

	/// Norm.
	[[nodiscard]] float Norm() const noexcept { return SDL::Sqrt(NormSq()); }

	/// Normalised copy.
	[[nodiscard]] FQuaternion Normalize() const noexcept {
		float n = Norm();
		return n > 1e-8f ? FQuaternion{x/n, y/n, z/n, w/n} : Identity();
	}

	/// Rotate a FVector3 by this quaternion.
	[[nodiscard]] constexpr FVector3 Rotate(const FVector3& v) const noexcept {
		FVector3 qv{x, y, z};
		FVector3 t = 2.f * qv.Cross(v);
		return v + w * t + qv.Cross(t);
	}

	/**
	 * Convert to a 4×4 rotation matrix (assumes unit quaternion).
	 */
	[[nodiscard]] constexpr FMatrix4 ToMat4() const noexcept {
		float x2 = x*x, y2 = y*y, z2 = z*z;
		float xy = x*y, xz = x*z, yz = y*z;
		float wx = w*x, wy = w*y, wz = w*z;

		FMatrix4 r;
		// Column 0
		r.m[0]  = 1.f - 2.f*(y2 + z2);
		r.m[1]  = 2.f*(xy + wz);
		r.m[2]  = 2.f*(xz - wy);
		r.m[3]  = 0.f;
		// Column 1
		r.m[4]  = 2.f*(xy - wz);
		r.m[5]  = 1.f - 2.f*(x2 + z2);
		r.m[6]  = 2.f*(yz + wx);
		r.m[7]  = 0.f;
		// Column 2
		r.m[8]  = 2.f*(xz + wy);
		r.m[9]  = 2.f*(yz - wx);
		r.m[10] = 1.f - 2.f*(x2 + y2);
		r.m[11] = 0.f;
		// Column 3
		r.m[12] = r.m[13] = r.m[14] = 0.f;
		r.m[15] = 1.f;
		return r;
	}

	/**
	 * Spherical linear interpolation (SLERP) from this quaternion toward `to`.
	 *
	 * @param to Target quaternion (unit length).
	 * @param t  Interpolation factor in [0, 1].
	 */
	[[nodiscard]] FQuaternion Slerp(const FQuaternion& to, float t) const noexcept {
		float dot = x*to.x + y*to.y + z*to.z + w*to.w;
		FQuaternion  end = to;
		if (dot < 0.f) { dot = -dot; end = {-to.x, -to.y, -to.z, -to.w}; } // shortest path
		dot = Clamp(dot, -1.f, 1.f);

		if (dot > 0.9995f) {
			// Quaternions nearly parallel → linear interpolation
			return FQuaternion{x + t*(end.x - x),
									y + t*(end.y - y),
									z + t*(end.z - z),
									w + t*(end.w - w)}.Normalize();
		}

		float theta0    = SDL::Acos(dot);
		float theta     = theta0 * t;
		float sinTheta0 = SDL::Sin(theta0);
		float s0        = SDL::Cos(theta) - dot * SDL::Sin(theta) / sinTheta0;
		float s1        = SDL::Sin(theta) / sinTheta0;

		return FQuaternion{s0*x + s1*end.x,
								s0*y + s1*end.y,
								s0*z + s1*end.z,
								s0*w + s1*end.w}.Normalize();
	}
};

// ── FAABB ───────────────────────────────────────────────────────────────────────

/**
 * Axis-Aligned Bounding Box in 3D space.
 *
 * The default-constructed FAABB is "empty" (Min > Max). Use `Expand()` to
 * grow it around a set of points.
 */
struct FAABB {
	FVector3 Min{ 1e30f,  1e30f,  1e30f};
	FVector3 Max{-1e30f, -1e30f, -1e30f};

	constexpr FAABB() noexcept = default;

	/// Construct from explicit Min and Max corners.
	constexpr FAABB(const FVector3& Min, const FVector3& Max) noexcept
		: Min{Min}, Max{Max} {}

	// ── Queries ───────────────────────────────────────────────────────────

	/// True when Min ≤ Max on all axes (non-empty).
	[[nodiscard]] constexpr bool IsValid() const noexcept {
		return Min.x <= Max.x && Min.y <= Max.y && Min.z <= Max.z;
	}

	/// Centre of the box.
	[[nodiscard]] constexpr FVector3 Center() const noexcept {
		return {(Min.x + Max.x) * 0.5f,
						(Min.y + Max.y) * 0.5f,
						(Min.z + Max.z) * 0.5f};
	}

	/// Half-extents: `(Max − Min) / 2`.
	[[nodiscard]] constexpr FVector3 HalfExtents() const noexcept {
		return {(Max.x - Min.x) * 0.5f,
						(Max.y - Min.y) * 0.5f,
						(Max.z - Min.z) * 0.5f};
	}

	/// size: `Max − Min`.
	[[nodiscard]] constexpr FVector3 size() const noexcept { return Max - Min; }

	/// Test whether point `p` is inside (inclusive).
	[[nodiscard]] constexpr bool Contains(const FVector3& p) const noexcept {
		return p.x >= Min.x && p.x <= Max.x
				&& p.y >= Min.y && p.y <= Max.y
				&& p.z >= Min.z && p.z <= Max.z;
	}

	/// Test whether FAABB `o` is fully contained.
	[[nodiscard]] constexpr bool Contains(const FAABB& o) const noexcept {
		return Contains(o.Min) && Contains(o.Max);
	}

	/// Test whether this FAABB overlaps with `o`.
	[[nodiscard]] constexpr bool Intersects(const FAABB& o) const noexcept {
		return Max.x >= o.Min.x && Min.x <= o.Max.x
				&& Max.y >= o.Min.y && Min.y <= o.Max.y
				&& Max.z >= o.Min.z && Min.z <= o.Max.z;
	}

	// ── Mutation ──────────────────────────────────────────────────────────

	/// Expand to include point `p`.
	constexpr void Expand(const FVector3& p) noexcept {
		if (p.x < Min.x) Min.x = p.x; else if (p.x > Max.x) Max.x = p.x;
		if (p.y < Min.y) Min.y = p.y; else if (p.y > Max.y) Max.y = p.y;
		if (p.z < Min.z) Min.z = p.z; else if (p.z > Max.z) Max.z = p.z;
	}

	/// Expand to enclose `o`.
	constexpr void Expand(const FAABB& o) noexcept { Expand(o.Min); Expand(o.Max); }

	// ── Transforms ────────────────────────────────────────────────────────

	/// Return a copy translated by `t`.
	[[nodiscard]] constexpr FAABB Translated(const FVector3& t) const noexcept {
		return {Min + t, Max + t};
	}

	/**
	 * FAABB of this box after applying matrix `mat`.
	 *
	 * Transforms all 8 corners and wraps the result.
	 */
	[[nodiscard]] FAABB Transformed(const FMatrix4& mat) const noexcept {
		const FVector3 corners[8] = {
			{Min.x, Min.y, Min.z},
			{Max.x, Min.y, Min.z},
			{Min.x, Max.y, Min.z},
			{Max.x, Max.y, Min.z},
			{Min.x, Min.y, Max.z},
			{Max.x, Min.y, Max.z},
			{Min.x, Max.y, Max.z},
			{Max.x, Max.y, Max.z}
		};
		FAABB result;
		for (const auto& c : corners)
			result.Expand(mat.TransformPoint(c));
		return result;
	}
};

// ── FPlane ──────────────────────────────────────────────────────────────────────

/**
 * Infinite plane: `normal · point + d = 0`.
 *
 * `normal` should be unit length for the signed distance to be in ECS::Context units.
 */
struct FPlane {
	FVector3  normal{0.f, 1.f, 0.f};
	float d = 0.f;

	constexpr FPlane() noexcept = default;

	/// Construct from a unit normal and the plane constant `d`.
	constexpr FPlane(const FVector3& normal, float d) noexcept
		: normal{normal}, d{d} {}

	/// Construct from a unit normal and a point on the plane.
	FPlane(const FVector3& n, const FVector3& point) noexcept {
		normal = n.Normalize();
		d      = -normal.Dot(point);
	}

	/// Construct from three points (CCW winding → normal toward viewer).
	[[nodiscard]] static FPlane FromTriangle(const FVector3& a,
																					 const FVector3& b,
																					 const FVector3& c) noexcept {
		FVector3 n = (b - a).Cross(c - a).Normalize();
		return {n, -n.Dot(a)};
	}

	/// Signed distance from `p` to the plane (positive = same side as normal).
	[[nodiscard]] constexpr float Distance(const FVector3& p) const noexcept {
		return normal.Dot(p) + d;
	}

	/// Normalised copy (unit-length normal).
	[[nodiscard]] FPlane Normalize() const noexcept {
		float len = normal.Length();
		return len > 1e-8f ? FPlane{normal / len, d / len} : *this;
	}
};

// ── FRay ────────────────────────────────────────────────────────────────────────

/**
 * FRay: a half-line with an origin and a (normalised) direction.
 */
struct FRay {
	FVector3 origin;
	FVector3 direction{0.f, 0.f, -1.f}; // default: looking down −Z

	constexpr FRay() noexcept = default;

	/// Construct from an origin and a direction (automatically normalised).
	FRay(const FVector3& origin, const FVector3& direction) noexcept
		: origin{origin}, direction{direction.Normalize()} {}

	/// Point on the ray at parameter `t`: `origin + direction * t`.
	[[nodiscard]] constexpr FVector3 At(float t) const noexcept {
		return {origin.x + direction.x * t,
						origin.y + direction.y * t,
						origin.z + direction.z * t};
	}

	/**
	 * Slab-test intersection with an FAABB.
	 *
	 * @param box       FAABB to test.
	 * @param tMin [out] Entry parameter (may be negative if origin is inside).
	 * @param tMax [out] Exit parameter.
	 * @returns true if the ray intersects the box.
	 */
	[[nodiscard]] bool Intersects(const FAABB& box,
																 float& tMin, float& tMax) const noexcept {
		tMin = 0.f;
		tMax = 1e30f;

		for (int i = 0; i < 3; ++i) {
			float orig = (&origin.x)[i];
			float dir  = (&direction.x)[i];
			float bmin = (&box.Min.x)[i];
			float bmax = (&box.Max.x)[i];

			if (SDL::Abs(dir) < 1e-8f) {
				if (orig < bmin || orig > bmax) return false;
			} else {
				float t1 = (bmin - orig) / dir;
				float t2 = (bmax - orig) / dir;
				if (t1 > t2) std::swap(t1, t2);
				tMin = SDL::Max(tMin, t1);
				tMax = SDL::Min(tMax, t2);
				if (tMin > tMax) return false;
			}
		}
		return true;
	}

	/**
	 * Intersection with a plane.
	 *
	 * @param plane  FPlane to test.
	 * @param t [out] Distance along the ray to the intersection.
	 * @returns true if the ray hits the plane (not parallel, in front).
	 */
	[[nodiscard]] bool Intersects(const FPlane& plane, float& t) const noexcept {
		float denom = plane.normal.Dot(direction);
		if (SDL::Abs(denom) < 1e-8f) return false; // parallel
		t = -(plane.normal.Dot(origin) + plane.d) / denom;
		return t >= 0.f;
	}

	/**
	 * Möller-Trumbore intersection with a triangle.
	 *
	 * @param v0,v1,v2  Triangle vertices.
	 * @param t   [out] Distance along the ray to the intersection.
	 * @param u,v [out] Barycentric coordinates of the hit point.
	 * @returns true on intersection.
	 */
	[[nodiscard]] bool Intersects(const FVector3& v0, const FVector3& v1, const FVector3& v2,
																 float& t, float& u, float& v) const noexcept {
		FVector3 e1 = v1 - v0;
		FVector3 e2 = v2 - v0;
		FVector3 h  = direction.Cross(e2);
		float a = e1.Dot(h);
		if (SDL::Abs(a) < 1e-8f) return false; // parallel

		float f  = 1.f / a;
		FVector3 s  = origin - v0;
		u = f * s.Dot(h);
		if (u < 0.f || u > 1.f) return false;

		FVector3 q = s.Cross(e1);
		v = f * direction.Dot(q);
		if (v < 0.f || u + v > 1.f) return false;

		t = f * e2.Dot(q);
		return t > 1e-8f;
	}
};

// ── FFrustum ────────────────────────────────────────────────────────────────────

/**
 * View frustum defined by six planes for visibility culling.
 *
 * FPlane order: left, right, bottom, top, near, far.
 */
struct FFrustum {
	FPlane planes[6];

	/**
	 * Extract the frustum from a combined view-projection matrix.
	 *
	 * Uses the Gribb-Hartmann method adapted for Vulkan clip space
	 * (Z ∈ [0, 1]).
	 *
	 * @param vp Combined view-projection matrix (column-major).
	 */
	[[nodiscard]] static FFrustum FromViewProj(const FMatrix4& vp) noexcept {
		// Read row i as a FVector4: {m[0*4+i], m[1*4+i], m[2*4+i], m[3*4+i]}
		auto row = [&](int i) -> FVector4 {
			return {vp.m[0*4+i], vp.m[1*4+i], vp.m[2*4+i], vp.m[3*4+i]};
		};
		FVector4 r0 = row(0), r1 = row(1), r2 = row(2), r3 = row(3);

		auto makePlane = [](const FVector4& v) -> FPlane {
			return {{v.x, v.y, v.z}, v.w};
		};

		FFrustum f;
		f.planes[0] = makePlane(r3 + r0).Normalize(); // left   (x >= -w)
		f.planes[1] = makePlane(r3 - r0).Normalize(); // right  (x <=  w)
		f.planes[2] = makePlane(r3 + r1).Normalize(); // bottom (y >= -w)
		f.planes[3] = makePlane(r3 - r1).Normalize(); // top    (y <=  w)
		f.planes[4] = makePlane(r2).Normalize();       // near   (z >=  0, Vulkan)
		f.planes[5] = makePlane(r3 - r2).Normalize(); // far    (z <=  w)
		return f;
	}

	/**
	 * Test whether an FAABB is at least partially inside the frustum.
	 *
	 * Returns false only when the box is entirely outside one of the planes
	 * (conservative: may return true for boxes that are actually culled by
	 * the intersection of two planes).
	 *
	 * @param box FAABB to test.
	 * @returns true if the box may be visible.
	 */
	[[nodiscard]] bool Intersects(const FAABB& box) const noexcept {
		for (const auto& plane : planes) {
			// "positive vertex": the corner furthest along the plane normal
			FVector3 pv{
				plane.normal.x >= 0.f ? box.Max.x : box.Min.x,
				plane.normal.y >= 0.f ? box.Max.y : box.Min.y,
				plane.normal.z >= 0.f ? box.Max.z : box.Min.z,
			};
			if (plane.Distance(pv) < 0.f) return false;
		}
		return true;
	}
};

/** @} */ // CategoryMath3D

} // namespace SDL

#endif // SDL3PP_MATH3D_H_
