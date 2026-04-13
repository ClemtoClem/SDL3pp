#ifndef SDL3PP_RECT_H_
#define SDL3PP_RECT_H_

#include <SDL3/SDL_rect.h>
#include "SDL3pp_error.h"
#include "SDL3pp_optionalRef.h"
#include "SDL3pp_spanRef.h"
#include "SDL3pp_stdinc.h"

namespace SDL {

/**
 * @defgroup CategoryRect Rectangle Functions
 *
 * Some helper functions for managing rectangles and 2D points, in both integer
 * and floating point versions.
 *
 * @{
 */

/// Alias to raw representation for Point.
using PointRaw = SDL_Point;

// Forward decl
struct Point;

/// Alias to raw representation for FPoint.
using FPointRaw = SDL_FPoint;

// Forward decl
struct FPoint;

/// Alias to raw representation for Rect.
using RectRaw = SDL_Rect;

// Forward decl
struct Rect;

// Forward decl
struct Box;

// Forward decl
struct Corners;

/// Alias to raw representation for FRect.
using FRectRaw = SDL_FRect;

// Forward decl
struct FRect;

// Forward decl
struct FBox;

// Forward decl
struct FCorners;

/// Comparison operator for Point.
constexpr bool operator==(const PointRaw& lhs, const PointRaw& rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

/// Comparison operator for FPoint.
constexpr bool operator==(const FPointRaw& lhs, const FPointRaw& rhs) noexcept {
  return lhs.x == rhs.x && lhs.y == rhs.y;
}

/// Comparison operator for Rect.
inline bool operator==(const RectRaw& lhs, const RectRaw& rhs) noexcept {
  return SDL_RectsEqual(&lhs, &rhs);
}

/// Comparison operator for FRect.
inline bool operator==(const FRectRaw& lhs, const FRectRaw& rhs) noexcept {
  return SDL_RectsEqualFloat(&lhs, &rhs);
}

/// Min
inline PointRaw Min(const PointRaw& p, int s) {
  return {SDL::Min(p.x, s), SDL::Min(p.y, s)};
}
inline FPointRaw Min(const FPointRaw& p, float s) {
  return {SDL::Min(p.x, s), SDL::Min(p.y, s)};
}
inline PointRaw Min(const PointRaw& a, const PointRaw& b) {
  return {SDL::Min(a.x, b.x), SDL::Min(a.y, b.y)};
}
inline FPointRaw Min(const FPointRaw& a, const FPointRaw& b) {
  return {SDL::Min(a.x, b.x), SDL::Min(a.y, b.y)};
}
inline RectRaw Min(const RectRaw& a, int s) {
  return {SDL::Min(a.x, s), SDL::Min(a.y, s), SDL::Min(a.w, s), SDL::Min(a.h, s)};
}
inline FRectRaw Min(const FRectRaw& a, float s) {
  return {SDL::Min(a.x, s), SDL::Min(a.y, s), SDL::Min(a.w, s), SDL::Min(a.h, s)};
}
inline RectRaw Min(const RectRaw& a, const RectRaw& b) {
  return {SDL::Min(a.x, b.x), SDL::Min(a.y, b.y), SDL::Min(a.w, b.w), SDL::Min(a.h, b.h)};
}
inline FRectRaw Min(const FRectRaw& a, const FRectRaw& b) {
  return {SDL::Min(a.x, b.x), SDL::Min(a.y, b.y), SDL::Min(a.w, b.w), SDL::Min(a.h, b.h)};
}

/// Max
inline PointRaw Max(const PointRaw& p, int s) {
  return {SDL::Max(p.x, s), SDL::Max(p.y, s)};
}
inline FPointRaw Max(const FPointRaw& p, float s) {
  return {SDL::Max(p.x, s), SDL::Max(p.y, s)};
}
inline PointRaw Max(const PointRaw& a, const PointRaw& b) {
  return {SDL::Max(a.x, b.x), SDL::Max(a.y, b.y)};
}
inline FPointRaw Max(const FPointRaw& a, const FPointRaw& b) {
  return {SDL::Max(a.x, b.x), SDL::Max(a.y, b.y)};
}
inline RectRaw Max(const RectRaw& a, int s) {
  return {SDL::Max(a.x, s), SDL::Max(a.y, s), SDL::Max(a.w, s), SDL::Max(a.h, s)};
}
inline FRectRaw Max(const FRectRaw& a, float s) {
  return {SDL::Max(a.x, s), SDL::Max(a.y, s), SDL::Max(a.w, s), SDL::Max(a.h, s)};
}
inline RectRaw Max(const RectRaw& a, const RectRaw& b) {
  return {SDL::Max(a.x, b.x), SDL::Max(a.y, b.y), SDL::Max(a.w, b.w), SDL::Max(a.h, b.h)};
}
inline FRectRaw Max(const FRectRaw& a, const FRectRaw& b) {
  return {SDL::Max(a.x, b.x), SDL::Max(a.y, b.y), SDL::Max(a.w, b.w), SDL::Max(a.h, b.h)};
}

/// Clamp
inline PointRaw Clamp(const PointRaw& p, int lo, int hi) {
  return {SDL::Clamp(p.x, lo, hi), SDL::Clamp(p.y, lo, hi)};
}
inline FPointRaw Clamp(const FPointRaw& p, float lo, float hi) {
  return {SDL::Clamp(p.x, lo, hi), SDL::Clamp(p.y, lo, hi)};
}
inline PointRaw Clamp(const PointRaw& p, const PointRaw& lo, const PointRaw& hi) {
  return {SDL::Clamp(p.x, lo.x, hi.x), SDL::Clamp(p.y, lo.y, hi.y)};
}
inline FPointRaw Clamp(const FPointRaw& p, const FPointRaw& lo, const FPointRaw& hi) {
  return {SDL::Clamp(p.x, lo.x, hi.x), SDL::Clamp(p.y, lo.y, hi.y)};
}
inline RectRaw Clamp(const RectRaw& a, int lo, int hi) {
  return {SDL::Clamp(a.x, lo, hi), SDL::Clamp(a.y, lo, hi), SDL::Clamp(a.w, lo, hi), SDL::Clamp(a.h, lo, hi)};
}
inline FRectRaw Clamp(const FRectRaw& a, float lo, float hi) {
  return {SDL::Clamp(a.x, lo, hi), SDL::Clamp(a.y, lo, hi), SDL::Clamp(a.w, lo, hi), SDL::Clamp(a.h, lo, hi)};
}
inline RectRaw Clamp(const RectRaw& a, const RectRaw& rlo, const RectRaw& rhi) {
  return {SDL::Clamp(a.x, rlo.x, rhi.x), SDL::Clamp(a.y, rlo.y, rhi.y), SDL::Clamp(a.w, rlo.w, rhi.w), SDL::Clamp(a.h, rlo.h, rhi.h)};
}
inline FRectRaw Clamp(const FRectRaw& a, const FRectRaw& rlo, const FRectRaw& rhi) {
  return {SDL::Clamp(a.x, rlo.x, rhi.x), SDL::Clamp(a.y, rlo.y, rhi.y), SDL::Clamp(a.w, rlo.w, rhi.w), SDL::Clamp(a.h, rlo.h, rhi.h)};
}

/**
 * The structure that defines a point (using integers).
 *
 * Inspired by
 * https://github.com/libSDL2pp/libSDL2pp/blob/master/SDL2pp/Point.hh
 *
 * @since This struct is available since SDL 3.2.0.
 *
 * @cat wrap-extending-struct
 *
 * @sa Rect.GetEnclosingPoints
 * @sa Point.InRect
 */
struct Point : PointRaw {
  /**
   * Wraps Point.
   *
   * @param p the value to be wrapped
   */
  constexpr Point(const PointRaw& p = {}) noexcept
    : PointRaw(p) {
  }

  /**
   * Constructs from its fields.
   *
   * @param x the value for x.
   * @param y the value for y.
   */
  constexpr Point(int x, int y) noexcept
    : PointRaw{x, y} {
  }

  /**
   * Wraps Point.
   *
   * @param p the value to be wrapped
   */
  constexpr explicit Point(const FPointRaw& p)
    : SDL_Point{int(p.x), int(p.y)} {
  }

  /**
   * Check if valid.
   *
   * @returns True if valid state, false otherwise.
   */
  constexpr explicit operator bool() const noexcept {
    return *this != PointRaw{};
  }

  /**
   * Get x coordinate
   *
   * @returns x coordinate
   */
  constexpr int GetX() const noexcept { return x; }

  /**
   * Set the x coordinate.
   *
   * @param newX the new x coordinate.
   * @returns Reference to self.
   */
  constexpr Point& SetX(int newX) noexcept {
    x = newX;
    return *this;
  }

  /**
   * Get y coordinate
   *
   * @returns y coordinate
   */
  constexpr int GetY() const noexcept { return y; }

  /**
   * Set the y coordinate.
   *
   * @param newY the new y coordinate.
   * @returns Reference to self.
   */
  constexpr Point& SetY(int newY) noexcept {
    y = newY;
    return *this;
  }

  /**
   * Determine whether a point resides inside a rectangle.
   *
   * A point is considered part of a rectangle if both `p` and `r` are not
   * nullptr, and `p`'s x and y coordinates are >= to the rectangle's top left
   * corner, and < the rectangle's x+w and y+h. So a 1x1 rectangle considers
   * point (0,0) as "inside" and (0,1) as not.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @param r the rectangle to test.
   * @returns true if this is contained by `r`, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool InRect(const RectRaw& r) const;

  /**
   * Get point's memberwise negation
   *
   * @returns New Point representing memberwise negation
   */
  constexpr Point operator-() const { return Point(-x, -y); }

  /**
   * Get point's memberwise addition with another point
   *
   * @param[in] other Point to add
   *
   * @returns New Point representing memberwise addition with another point
   */
  constexpr Point operator+(const Point& other) const {
    return Point(x + other.x, y + other.y);
  }

  /**
   * Get point's memberwise subtraction with another point
   *
   * @param[in] other Point to subtract
   *
   * @returns New Point representing memberwise subtraction of another point
   */
  constexpr Point operator-(const Point& other) const {
    return Point(x - other.x, y - other.y);
  }

  /**
   * Get point's memberwise division by an integer
   *
   * @param[in] value Divisor
   *
   * @returns New Point representing memberwise division of
   *          point by an integer
   */
  constexpr Point operator/(int value) const {
    return Point(x / value, y / value);
  }

  /**
   * Get point's memberwise division by an integer
   *
   * @param[in] value Divisor
   *
   * @returns New Point representing memberwise division of
   *          point by an integer
   */
  constexpr FPoint operator/(float value) const;

  /**
   * Get point's memberwise division by another point
   *
   * @param[in] other Divisor
   *
   * @returns New Point representing memberwise division of
   *          point by another point
   */
  constexpr Point operator/(const Point& other) const {
    return Point(x / other.x, y / other.y);
  }

  /**
   * Get point's memberwise remainder from division
   *        by an integer
   *
   * @param[in] value Divisor
   *
   * @returns New Point representing memberwise remainder
   *          from division by an integer
   */
  constexpr Point operator%(int value) const {
    return Point(x % value, y % value);
  }

  /**
   * Get point's memberwise remainder from division
   *        by another point
   *
   * @param[in] other Divisor
   *
   * @returns New Point representing memberwise remainder
   *          from division by another point
   */
  constexpr Point operator%(const Point& other) const {
    return Point(x % other.x, y % other.y);
  }

  /**
   * Get point's memberwise multiplication by an
   *        integer
   *
   * @param[in] value Multiplier
   *
   * @returns New Point representing memberwise multiplication
   *          of point by an integer
   */
  constexpr Point operator*(int value) const {
    return Point(x * value, y * value);
  }

  /**
   * Get point's memberwise multiplication by an
   *        integer
   *
   * @param[in] value Multiplier
   *
   * @returns New Point representing memberwise multiplication
   *          of point by an integer
   */
  constexpr FPoint operator*(float value) const;

  /**
   * Get point's memberwise multiplication by another
   *        point
   *
   * @param[in] other Multiplier
   *
   * @returns New Point representing memberwise multiplication
   *          of point by another point
   */
  constexpr Point operator*(const Point& other) const {
    return Point(x * other.x, y * other.y);
  }

  /**
   * Memberwise add another point
   *
   * @param[in] other Point to add to the current one
   *
   * @returns Reference to self
   */
  constexpr Point& operator+=(const Point& other) {
    x += other.x;
    y += other.y;
    return *this;
  }

  /**
   * Memberwise subtract another point
   *
   * @param[in] other Point to subtract from the current one
   *
   * @returns Reference to self
   */
  constexpr Point& operator-=(const Point& other) {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  /**
   * Memberwise divide by an integer
   *
   * @param[in] value Divisor
   *
   * @returns Reference to self
   */
  constexpr Point& operator/=(int value) {
    x /= value;
    y /= value;
    return *this;
  }

  /**
   * Memberwise divide by another point
   *
   * @param[in] other Divisor
   *
   * @returns Reference to self
   */
  constexpr Point& operator/=(const Point& other) {
    x /= other.x;
    y /= other.y;
    return *this;
  }

  /**
   * Memberwise remainder from division by an integer
   *
   * @param[in] value Divisor
   *
   * @returns Reference to self
   */
  constexpr Point& operator%=(int value) {
    x %= value;
    y %= value;
    return *this;
  }

  /**
   * Memberwise remainder from division by another
   *        point
   *
   * @param[in] other Divisor
   *
   * @returns Reference to self
   */
  constexpr Point& operator%=(const Point& other) {
    x %= other.x;
    y %= other.y;
    return *this;
  }

  /**
   * Memberwise multiply by an integer
   *
   * @param[in] value Multiplier
   *
   * @returns Reference to self
   */
  constexpr Point& operator*=(int value) {
    x *= value;
    y *= value;
    return *this;
  }

  /**
   * Memberwise multiply by another point
   *
   * @param[in] other Multiplier
   *
   * @returns Reference to self
   */
  constexpr Point& operator*=(const Point& other) {
    x *= other.x;
    y *= other.y;
    return *this;
  }

  /**
   * Get a point with coordinates modified so it fits
   *        into a given rect
   *
   * @param[in] rect Rectangle to Clamp with
   *
   * @returns Clamped point
   */
  constexpr Point GetClamped(const Rect& rect) const;

  /**
   * Clamp point coordinates to make it fit into a
   *        given rect
   *
   * @param[in] rect Rectangle to Clamp with
   *
   * @returns Reference to self
   */
  constexpr Point& Clamp(const Rect& rect);

  /**
   * Get a point wrapped within a specified rect
   *
   * @param[in] rect Rectangle to wrap with
   *
   * @returns Wrapped point
   */
  constexpr Point GetWrapped(const Rect& rect) const;

  /**
   * Wrap point coordinates within a specified rect
   *
   * @param[in] rect Rectangle to wrap with
   *
   * @returns Reference to self
   */
  constexpr Point& Wrap(const Rect& rect);

  /**
   * Converts to FPoint
   *
   * @return FPoint
   */
  constexpr operator FPoint() const;
};

struct Size : Point {
    constexpr Size() = default;
    constexpr Size(int w_, int h_) : Point(w_, h_) {}

    int& w() noexcept { return x; }
    int& h() noexcept { return y; }

    const int& w() const noexcept { return x; }
    const int& h() const noexcept { return y; }

    Size& Extend(float factor) {
        x = static_cast<int>(x * factor);
        y = static_cast<int>(y * factor);
        return *this;
    }
};

/**
 * The structure that defines a point (using floating point values).
 *
 * @since This struct is available since SDL 3.2.0.
 *
 * @cat wrap-extending-struct
 *
 * @sa FRect.GetEnclosingPoints
 * @sa FPoint.InRect
 */
struct FPoint : FPointRaw {
  /**
   * Wraps FPoint.
   *
   * @param p the value to be wrapped
   */
  constexpr FPoint(const FPointRaw& p = {}) noexcept
    : FPointRaw(p) {
  }

  /**
   * Constructs from its fields.
   *
   * @param x the value for x.
   * @param y the value for y.
   */
  constexpr FPoint(float x, float y) noexcept
    : FPointRaw{x, y} {
  }

  /**
   * Check if valid.
   *
   * @returns True if valid state, false otherwise.
   */
  constexpr explicit operator bool() const noexcept {
    return *this != FPointRaw{};
  }

  /**
   * Get the x coordinate.
   *
   * @returns current x value.
   */
  constexpr float GetX() const noexcept { return x; }

  /**
   * Set the x coordinate.
   *
   * @param newX the new x coordinate.
   * @returns Reference to self.
   */
  constexpr FPoint& SetX(float newX) noexcept {
    x = newX;
    return *this;
  }

  /**
   * Get the y coordinate.
   *
   * @returns current y coordinate.
   */
  constexpr float GetY() const noexcept { return y; }

  /**
   * Set the y coordinate.
   *
   * @param newY the new y coordinate.
   * @returns Reference to self.
   */
  constexpr FPoint& SetY(float newY) noexcept {
    y = newY;
    return *this;
  }

  /**
   * Determine whether a point resides inside a floating point rectangle.
   *
   * A point is considered part of a rectangle if both `p` and `r` are not
   * nullptr, and `p`'s x and y coordinates are >= to the rectangle's top left
   * corner, and <= the rectangle's x+w and y+h. So a 1x1 rectangle considers
   * point (0,0) and (0,1) as "inside" and (0,2) as not.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @param r the rectangle to test.
   * @returns true if this is contained by `r`, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool InRect(const FRectRaw& r) const;

  /**
   * Get point's memberwise negation
   *
   * @returns New Point representing memberwise negation
   */
  [[nodiscard]] constexpr FPoint operator-() const noexcept { return FPoint(-x, -y); }

  /**
   * Get point's memberwise addition with another point
   *
   * @param[in] other Point to add
   *
   * @returns New Point representing memberwise addition with another point
   */
  [[nodiscard]] constexpr FPoint operator+(const FPoint& other) const noexcept {
    return FPoint(x + other.x, y + other.y);
  }

  /**
   * Get point's memberwise subtraction with another point
   *
   * @param[in] other Point to subtract
   *
   * @returns New Point representing memberwise subtraction of another point
   */
  [[nodiscard]] constexpr FPoint operator-(const FPoint& other) const noexcept {
    return FPoint(x - other.x, y - other.y);
  }

  /**
   * Get point's memberwise division by an float
   *
   * @param[in] value Divisor
   *
   * @returns New Point representing memberwise division of
   *          point by an float
   */
  [[nodiscard]] constexpr FPoint operator/(float value) const noexcept {
    return FPoint(x / value, y / value);
  }

  /**
   * Get point's memberwise division by another point
   *
   * @param[in] other Divisor
   *
   * @returns New Point representing memberwise division of
   *          point by another point
   */
  [[nodiscard]] constexpr FPoint operator/(const FPoint& other) const noexcept {
    return FPoint(x / other.x, y / other.y);
  }

  /**
   * Get point's memberwise multiplication by an
   *        float
   *
   * @param[in] value Multiplier
   *
   * @returns New Point representing memberwise multiplication
   *          of point by an float
   */
  [[nodiscard]] constexpr FPoint operator*(float value) const noexcept {
    return FPoint(x * value, y * value);
  }

  /**
   * Get point's memberwise multiplication by another
   *        point
   *
   * @param[in] other Multiplier
   *
   * @returns New Point representing memberwise multiplication
   *          of point by another point
   */
  [[nodiscard]] constexpr FPoint operator*(const FPoint& other) const noexcept {
    return FPoint(x * other.x, y * other.y);
  }

  /**
   * Memberwise add another point
   *
   * @param[in] other Point to add to the current one
   *
   * @returns Reference to self
   */
  [[nodiscard]] constexpr FPoint& operator+=(const FPoint& other) noexcept {
    x += other.x;
    y += other.y;
    return *this;
  }

  /**
   * Memberwise subtract another point
   *
   * @param[in] other Point to subtract from the current one
   *
   * @returns Reference to self
   */
  [[nodiscard]] constexpr FPoint& operator-=(const FPoint& other) noexcept {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  /**
   * Memberwise divide by an float
   *
   * @param[in] value Divisor
   *
   * @returns Reference to self
   */
  [[nodiscard]] constexpr FPoint& operator/=(float value) noexcept {
    x /= value;
    y /= value;
    return *this;
  }

  /**
   * Memberwise divide by another point
   *
   * @param[in] other Divisor
   *
   * @returns Reference to self
   */
  [[nodiscard]] constexpr FPoint& operator/=(const FPoint& other) noexcept {
    x /= other.x;
    y /= other.y;
    return *this;
  }

  /**
   * Memberwise multiply by an float
   *
   * @param[in] value Multiplier
   *
   * @returns Reference to self
   */
  [[nodiscard]] constexpr FPoint& operator*=(float value) noexcept {
    x *= value;
    y *= value;
    return *this;
  }

  /**
   * Memberwise multiply by another point
   *
   * @param[in] other Multiplier
   *
   * @returns Reference to self
   */
  [[nodiscard]] constexpr FPoint& operator*=(const FPoint& other) noexcept {
    x *= other.x;
    y *= other.y;
    return *this;
  }

  /**
   * Get a point with coordinates modified so it fits
   *        into a given rect
   *
   * @param[in] rect Rectangle to Clamp with
   *
   * @returns Clamped point
   */
  constexpr FPoint GetClamped(const FRect& rect) const;

  /**
   * Clamp point coordinates to make it fit into a
   *        given rect
   *
   * @param[in] rect Rectangle to Clamp with
   *
   * @returns Reference to self
   */
  constexpr FPoint& Clamp(const FRect& rect);

  /**
   * Get a point wrapped within a specified rect
   *
   * @param[in] rect Rectangle to wrap with
   *
   * @returns Wrapped point
   */
  constexpr FPoint GetWrapped(const FRect& rect) const;

  /**
   * Wrap point coordinates within a specified rect
   *
   * @param[in] rect Rectangle to wrap with
   *
   * @returns Reference to self
   */
  constexpr FPoint& Wrap(const FRect& rect);
};

struct FSize : FPoint {
    constexpr FSize() = default;
    constexpr FSize(float w_, float h_) : FPoint(w_, h_) {}

    float& w() noexcept { return x; }
    float& h() noexcept { return y; }

    const float& w() const noexcept { return x; }
    const float& h() const noexcept { return y; }

    FSize& Extend(float factor) {
        x = x * factor;
        y = y * factor;
        return *this;
    }
};


/**
 * A rectangle, with the origin at the upper left (using integers).
 *
 * @since This struct is available since SDL 3.2.0.
 *
 * @cat wrap-extending-struct
 *
 * @sa Rect.Empty
 * @sa Rect.Equal
 * @sa Rect.HasIntersection
 * @sa Rect.GetIntersection
 * @sa Rect.GetLineIntersection
 * @sa Rect.GetUnion
 * @sa Rect.GetEnclosingPoints
 */
struct Rect : RectRaw {
  /**
   * Wraps Rect.
   *
   * @param r the value to be wrapped
   */
  constexpr Rect(const RectRaw& r = {}) noexcept
    : RectRaw(r) {
  }

  /**
   * Constructs from its fields.
   *
   * @param x the left x.
   * @param y the top y.
   * @param w the width.
   * @param h the height.
   */
  constexpr Rect(int x, int y, int w, int h) noexcept
    : RectRaw{x, y, w, h} {
  }

  /**
   * Construct from offset and size
   *
   * @param corner the top-left corner
   * @param size the size
   */
  constexpr Rect(const PointRaw& corner, const PointRaw& size)
    : Rect{corner.x, corner.y, size.x, size.y} {
  }

  /// Compares with the underlying type
  bool operator==(const RectRaw& other) const { return Equal(other); }

  /// Compares with the underlying type
  bool operator==(const Rect& other) const {
    return *this == (const RectRaw&)(other);
  }

  /// @sa Empty()
  explicit operator bool() const { return !Empty(); }

  /**
   * Get left x coordinate.
   *
   * @returns coordinate of the left x
   */
  constexpr int GetX() const noexcept { return x; }

  /**
   * Set the left x coordinate.
   *
   * @param newX the new left x.
   * @returns Reference to self.
   */
  constexpr Rect& SetX(int newX) noexcept {
    x = newX;
    return *this;
  }

  /**
   * Get top y coordinate.
   *
   * @returns coordinate of the top y.
   */
  constexpr int GetY() const noexcept { return y; }

  /**
   * Set the top y coordinate.
   *
   * @param newY the new top y.
   * @returns Reference to self.
   */
  constexpr Rect& SetY(int newY) noexcept {
    y = newY;
    return *this;
  }

  /**
   * Get width of the rect
   *
   * @returns Width of the rect
   */
  constexpr int GetW() const noexcept { return w; }

  /**
   * Set the width of the rect.
   *
   * @param newW the new width.
   * @returns Reference to self.
   */
  constexpr Rect& SetW(int newW) noexcept {
    w = newW;
    return *this;
  }

  /**
   * Get height of the rect
   *
   * @returns Height of the rect
   */
  constexpr int GetH() const noexcept { return h; }

  /**
   * Set the height of the rect.
   *
   * @param newH the new height.
   * @returns Reference to self.
   */
  constexpr Rect& SetH(int newH) noexcept {
    h = newH;
    return *this;
  }

  /**
   * Calculate a minimal rectangle enclosing a set of points.
   *
   * If `clip` is not nullptr then only points inside of the clipping rectangle
   * are considered.
   *
   * @param points a span of SDL_Point structures representing points to be
   *               enclosed.
   * @param clip an SDL_Rect used for clipping or std::nullopt to enclose all
   *             points.
   * @returns a SDL_Rect structure filled in with the minimal enclosing
   *          rectangle or an empty rect if all the points were outside of the
   *          clipping rectangle.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  static Rect GetEnclosingPoints(
    SpanRef<const PointRaw> points,
    OptionalRef<const RectRaw> clip = std::nullopt);

  /**
   * Construct the rect from given center coordinates, width and height
   *
   * @param[in] cx X coordinate of the rectangle center
   * @param[in] cy Y coordinate of the rectangle center
   * @param[in] w Width of the rectangle
   * @param[in] h Height of the rectangle
   */
  static constexpr Rect FromCenter(int cx, int cy, int w, int h) {
    return Rect(cx - w / 2, cy - h / 2, w, h);
  }

  /**
   * Construct the rect from given center coordinates and size
   *
   * @param[in] center Coordinates of the rectangle center
   * @param[in] size Dimensions of the rectangle
   */
  static constexpr Rect FromCenter(Point center, Point size) {
    return Rect(center - size / 2, size);
  }

  /**
   * Construct the rect from given corners coordinates
   *
   * @param[in] x1 X coordinate of the top left rectangle corner
   * @param[in] y1 Y coordinate of the top left rectangle corner
   * @param[in] x2 X coordinate of the bottom right rectangle corner
   * @param[in] y2 Y coordinate of the bottom right rectangle corner
   */
  static constexpr Rect FromCorners(int x1, int y1, int x2, int y2) {
    return Rect(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
  }

  /**
   * Construct the rect from given centers coordinates
   *
   * @param[in] p1 Coordinates of the top left rectangle corner
   * @param[in] p2 Coordinates of the bottom right rectangle corner
   */
  static constexpr Rect FromCorners(Point p1, Point p2) {
    return Rect(p1, p2 - p1 + Point(1, 1));
  }

  /**
   * Get X coordinate of the rect second corner
   *
   * @returns X coordinate of the rect second corner
   */
  constexpr int GetX2() const { return x + w - 1; }

  /**
   * Set X coordinate of the rect second corner
   *
   * @param[in] x2 New X coordinate value
   *
   * This modifies rectangle width internally
   *
   * @returns Reference to self
   */
  constexpr Rect& SetX2(int x2) {
    w = x2 - x + 1;
    return *this;
  }

  /**
   * Get Y coordinate of the rect second corner
   *
   * @returns Y coordinate of the rect second corner
   */
  constexpr int GetY2() const { return y + h - 1; }

  /**
   * Set Y coordinate of the rect second corner
   *
   * @param[in] y2 New Y coordinate value
   *
   * This modifies rectangle height internally
   *
   * @returns Reference to self
   */
  constexpr Rect& SetY2(int y2) {
    h = y2 - y + 1;
    return *this;
  }

  /**
   * Get top left corner of the rect
   *
   * @returns Top left corner of the rect
   */
  constexpr Point GetTopLeft() const { return Point(x, y); }

  /**
   * Get top right corner of the rect
   *
   * @returns Top right corner of the rect
   */
  constexpr Point GetTopRight() const { return Point(GetX2(), y); }

  /**
   * Get bottom left corner of the rect
   *
   * @returns bottom left corner of the rect
   */
  constexpr Point GetBottomLeft() const { return Point(x, GetY2()); }

  /**
   * Get bottom right corner of the rect
   *
   * @returns Bottom right corner of the rect
   */
  constexpr Point GetBottomRright() const { return Point(GetX2(), GetY2()); }

  /**
   * Get size of the rect
   *
   * @returns size of the rect
   */
  constexpr Point GetSize() const { return Point(w, h); }

  /**
   * Get centroid of the rect
   *
   * @returns Centroid of the rect
   */
  constexpr Point GetCentroid() const { return Point(x + w / 2, y + h / 2); }

  /**
   * Calculate the intersection of a rectangle and line segment
   *
   * @param[in,out] p1 Starting coordinates of the line
   * @param[in,out] p2 Ending coordinates of the line
   *
   * @returns True if there is an intersection, false otherwise
   *
   * This function is used to clip a line segment to a
   * rectangle. A line segment contained entirely within the
   * rectangle or that does not intersect will remain unchanged.
   * A line segment that crosses the rectangle at either or both
   * ends will be clipped to the boundary of the rectangle and
   * the new coordinates saved in p1 and/or p2 as necessary.
   */
  bool GetLineIntersection(PointRaw* p1, PointRaw* p2) const {
    return GetLineIntersection(p1 ? &p1->x : nullptr,
                               p1 ? &p1->y : nullptr,
                               p2 ? &p2->x : nullptr,
                               p2 ? &p2->y : nullptr);
  }

  /**
   * Calculate the intersection of a rectangle and line segment.
   *
   * This function is used to clip a line segment to a rectangle. A line segment
   * contained entirely within the rectangle or that does not intersect will
   * remain unchanged. A line segment that crosses the rectangle at either or
   * both ends will be clipped to the boundary of the rectangle and the new
   * coordinates saved in `X1`, `Y1`, `X2`, and/or `Y2` as necessary.
   *
   * @param X1 a pointer to the starting X-coordinate of the line.
   * @param Y1 a pointer to the starting Y-coordinate of the line.
   * @param X2 a pointer to the ending X-coordinate of the line.
   * @param Y2 a pointer to the ending Y-coordinate of the line.
   * @returns true if there is an intersection, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool GetLineIntersection(int* X1, int* Y1, int* X2, int* Y2) const;

  /**
   * Convert an SDL_Rect to SDL_FRect
   *
   * @return A FRect filled in with the floating point representation of
   *              `rect`.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  operator SDL_FRect() const;

  /// @sa operator ToFRect()
  constexpr operator FRect() const;

  /**
   * Determine whether a rectangle has no area.
   *
   * A rectangle is considered "Empty" for this function if `r` is nullptr, or
   * if `r`'s width and/or height are <= 0.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @returns true if the rectangle is "Empty", false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool Empty() const;

  /**
   * Determine whether two rectangles are equal.
   *
   * Rectangles are considered equal if both are not nullptr and each of their
   * x, y, width and height match.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @param other the second rectangle to test.
   * @returns true if the rectangles are equal, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool Equal(const RectRaw& other) const;

  /**
   * Check whether the rect contains given point
   *
   * @param p Point to check
   *
   * @returns True if the point is contained in the rect
   */
  bool Contains(const PointRaw& p) const { return SDL_PointInRect(&p, this); }

  /**
   * Check whether the rect contains given point
   *
   * @param other Point to check
   *
   * @returns True if the point is contained in the rect
   */
  bool Contains(const RectRaw& other) const { return GetUnion(other) == *this; }

  /**
   * Determine whether two rectangles intersect.
   *
   * @param other an SDL_Rect structure representing the second rectangle.
   * @returns true if there is an intersection, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   *
   * @sa Rect.GetIntersection
   */
  bool HasIntersection(const RectRaw& other) const;

  /**
   * Calculate the intersection of two rectangles.
   *
   * If `result` is nullptr then this function will return false.
   *
   * @param other an SDL_Rect structure representing the second rectangle.
   * @returns a Rect structure filled in with the intersection of if there is
   *          intersection, std::nullopt otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   *
   * @sa Rect.HasIntersection
   */
  Rect GetIntersection(const RectRaw& other) const;

  /**
   * Calculate the union of two rectangles.
   *
   * @param other an SDL_Rect structure representing the second rectangle.
   * @returns Rect representing union of two rectangles
   * @throws Error on failure.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  Rect GetUnion(const RectRaw& other) const;

  /**
   * Get a rect extended by specified amount of pixels
   *
   * @param[in] amount Number of pixels to extend by
   *
   * @returns Extended rect
   */
  constexpr Rect GetExtension(unsigned int amount) const {
    Rect r = *this;
    r.Extend(amount);
    return r;
  }

  /**
   * Get a rect extended by specified amount of pixels
   *
   * @param[in] hAmount Number of pixels to extend by
   *                    in horizontal direction
   * @param[in] vAmount Number of pixels to extend by
   *                    in vertical direction
   *
   * @returns Extended rect
   */
  constexpr Rect GetExtension(unsigned int hAmount, unsigned int vAmount) const {
    Rect r = *this;
    r.Extend(hAmount, vAmount);
    return r;
  }

  /**
   * Extend a rect by specified amount of pixels
   *
   * @param[in] amount Number of pixels to extend by
   *
   * @returns Reference to self
   */
  constexpr Rect& Extend(unsigned int amount) { return Extend(amount, amount); }

  /**
   * Extend a rect by specified amount of pixels
   *
   * @param[in] hAmount Number of pixels to extend by
   *                    in horizontal direction
   * @param[in] vAmount Number of pixels to extend by
   *                    in vertical direction
   *
   * @returns Reference to self
   */
  constexpr Rect& Extend(unsigned int hAmount, unsigned int vAmount) {
    x -= hAmount;
    y -= vAmount;
    w += hAmount * 2;
    h += vAmount * 2;
    return *this;
  }

  /**
   * Get rectangle moved by a given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Moved rectangle
   */
  constexpr Rect operator+(const Point& offset) const {
    return Rect(x + offset.x, y + offset.y, w, h);
  }

  /**
   * Get rectangle moved by an opposite of given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Moved rectangle
   */
  constexpr Rect operator-(const Point& offset) const {
    return Rect(x - offset.x, y - offset.y, w, h);
  }

  /**
   * Move by then given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Reference to self
   */
  constexpr Rect& operator+=(const Point& offset) {
    x += offset.x;
    y += offset.y;
    return *this;
  }

  /**
   * Move by an opposite of the given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Reference to self
   */
  constexpr Rect& operator-=(const Point& offset) {
    x -= offset.x;
    y -= offset.y;
    return *this;
  }

  /**
   * Check whether two rectangles are not equal
   * 
   * @param other the second rectangle to test.
   * @return true if the rectangles are not equal, false otherwise.
   */
  bool operator!=(const Rect& other) const { return !(*this == other); }

};

/**
 * A rectangle stored using floating point values.
 *
 * The origin of the coordinate space is in the top-left, with increasing values
 * moving down and right. The properties `x` and `y` represent the coordinates
 * of the top-left corner of the rectangle.
 *
 * @since This struct is available since SDL 3.2.0.
 *
 * @cat wrap-extending-struct
 *
 * @sa FRect.Empty
 * @sa FRect.Equal
 * @sa FRect.EqualEpsilon
 * @sa FRect.HasIntersection
 * @sa FRect.GetIntersection
 * @sa FRect.GetLineIntersection
 * @sa FRect.GetUnion
 * @sa FRect.GetEnclosingPoints
 * @sa FPoint.InRect
 */
struct FRect : FRectRaw {
  /**
   * Wraps FRect.
   *
   * @param r the value to be wrapped
   */
  constexpr FRect(const FRectRaw& r = {}) noexcept
    : FRectRaw(r) {
  }

  /**
   * Constructs from its fields.
   *
   * @param x the left x.
   * @param y the top y.
   * @param w the width.
   * @param h the height.
   */
  constexpr FRect(float x, float y, float w, float h) noexcept
    : FRectRaw{x, y, w, h} {
  }

  /**
   * Constructs from top-left corner plus size
   */
  constexpr FRect(const FPointRaw& corner, const FPointRaw& size)
    : FRect{corner.x, corner.y, size.x, size.y} {
  }

  /// @sa Empty()
  operator bool() const { return !Empty(); }

  /**
   * Get left x coordinate.
   *
   * @returns coordinate of the left x
   */
  constexpr float GetX() const noexcept { return x; }

  /**
   * Set the left x coordinate.
   *
   * @param newX the new left x.
   * @returns Reference to self.
   */
  constexpr FRect& SetX(float newX) noexcept {
    x = newX;
    return *this;
  }

  /**
   * Get top y coordinate.
   *
   * @returns coordinate of the top y.
   */
  constexpr float GetY() const noexcept { return y; }

  /**
   * Set the top y coordinate.
   *
   * @param newY the new top y.
   * @returns Reference to self.
   */
  constexpr FRect& SetY(float newY) noexcept {
    y = newY;
    return *this;
  }

  /**
   * Get width of the rect
   *
   * @returns Width of the rect
   */
  constexpr float GetW() const noexcept { return w; }

  /**
   * Set the width of the rect.
   *
   * @param newW the new width.
   * @returns Reference to self.
   */
  constexpr FRect& SetW(float newW) noexcept {
    w = newW;
    return *this;
  }

  /**
   * Get height of the rect
   *
   * @returns Height of the rect
   */
  constexpr float GetH() const noexcept { return h; }

  /**
   * Set the height of the rect.
   *
   * @param newH the new height.
   * @returns Reference to self.
   */
  constexpr FRect& SetH(float newH) noexcept {
    h = newH;
    return *this;
  }

  /**
   * Calculate a minimal rectangle enclosing a set of points with float
   * precision.
   *
   * If `clip` is not nullptr then only points inside of the clipping rectangle
   * are considered.
   *
   * @param points a span of SDL_Point structures representing points to be
   *               enclosed.
   * @param clip an SDL_Rect used for clipping or std::nullopt to enclose all
   *             points.
   * @returns a FRect structure filled in with the minimal enclosing
   *          rectangle or an empty FRect if all the points were outside of
   *          the clipping rectangle.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  static FRect GetEnclosingPoints(
    SpanRef<const FPointRaw> points,
    OptionalRef<const FRectRaw> clip = std::nullopt);

  /**
   * Construct the rect from given center coordinates, width and height
   *
   * @param[in] cx X coordinate of the rectangle center
   * @param[in] cy Y coordinate of the rectangle center
   * @param[in] w Width of the rectangle
   * @param[in] h Height of the rectangle
   */
  static constexpr FRect FromCenter(float cx, float cy, float w, float h) {
    return FRect(cx - w / 2, cy - h / 2, w, h);
  }

  /**
   * Construct the rect from given center coordinates and size
   *
   * @param[in] center Coordinates of the rectangle center
   * @param[in] size Dimensions of the rectangle
   */
  static constexpr FRect FromCenter(FPoint center, FPoint size) {
    return FRect(center - size / 2, size);
  }

  /**
   * Construct the rect from given corners coordinates
   *
   * @param[in] x1 X coordinate of the top left rectangle corner
   * @param[in] y1 Y coordinate of the top left rectangle corner
   * @param[in] x2 X coordinate of the bottom right rectangle corner
   * @param[in] y2 Y coordinate of the bottom right rectangle corner
   */
  static constexpr FRect FromCorners(float x1, float y1, float x2, float y2) {
    return FRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1);
  }

  /**
   * Construct the rect from given centers coordinates
   *
   * @param[in] p1 Coordinates of the top left rectangle corner
   * @param[in] p2 Coordinates of the bottom right rectangle corner
   */
  static constexpr FRect FromCorners(FPoint p1, FPoint p2) {
    return FRect(p1, p2 - p1 + FPoint(1, 1));
  }

  /**
   * Get X coordinate of the rect second corner
   *
   * @returns X coordinate of the rect second corner
   */
  constexpr float GetX2() const { return x + w - 1; }

  /**
   * Set X coordinate of the rect second corner
   *
   * @param[in] x2 New X coordinate value
   *
   * This modifies rectangle width internally
   *
   * @returns Reference to self
   */
  constexpr FRect& SetX2(float x2) {
    w = x2 - x + 1;
    return *this;
  }

  /**
   * Get Y coordinate of the rect second corner
   *
   * @returns Y coordinate of the rect second corner
   */
  constexpr float GetY2() const { return y + h - 1; }

  /**
   * Set Y coordinate of the rect second corner
   *
   * @param[in] y2 New Y coordinate value
   *
   * This modifies rectangle height internally
   *
   * @returns Reference to self
   */
  constexpr FRect& SetY2(float y2) {
    h = y2 - y + 1;
    return *this;
  }

  /**
   * Get top left corner of the rect
   *
   * @returns Top left corner of the rect
   */
  constexpr FPoint GetTopLeft() const { return FPoint(x, y); }

  /**
   * Get top right corner of the rect
   *
   * @returns Top right corner of the rect
   */
  constexpr FPoint GetTopRight() const { return FPoint(GetX2(), y); }

  /**
   * Get bottom left corner of the rect
   *
   * @returns bottom left corner of the rect
   */
  constexpr FPoint GetBottomLeft() const { return FPoint(x, GetY2()); }

  /**
   * Get bottom right corner of the rect
   *
   * @returns Bottom right corner of the rect
   */
  constexpr FPoint GetBottomRright() const { return FPoint(GetX2(), GetY2()); }

  /**
   * Get size of the rect
   *
   * @returns size of the rect
   */
  constexpr FPoint GetSize() const { return FPoint(w, h); }

  /**
   * Get centroid of the rect
   *
   * @returns Centroid of the rect
   */
  constexpr FPoint GetCentroid() const { return FPoint(x + w / 2, y + h / 2); }

  /**
   * Calculate the intersection of a rectangle and line segment with float
   * precision.
   *
   * This function is used to clip a line segment to a rectangle. A line segment
   * contained entirely within the rectangle or that does not intersect will
   * remain unchanged. A line segment that crosses the rectangle at either or
   * both ends will be clipped to the boundary of the rectangle and the new
   * coordinates saved in `X1`, `Y1`, `X2`, and/or `Y2` as necessary.
   *
   * @param X1 a pointer to the starting X-coordinate of the line.
   * @param Y1 a pointer to the starting Y-coordinate of the line.
   * @param X2 a pointer to the ending X-coordinate of the line.
   * @param Y2 a pointer to the ending Y-coordinate of the line.
   * @returns true if there is an intersection, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool GetLineIntersection(float* X1, float* Y1, float* X2, float* Y2) const;

  /**
   * Determine whether a floating point rectangle takes no space.
   *
   * @param[in,out] p1 Starting coordinates of the line
   * @param[in,out] p2 Ending coordinates of the line
   *
   * @returns True if there is an intersection, false otherwise
   *
   * This function is used to clip a line segment to a
   * rectangle. A line segment contained entirely within the
   * rectangle or that does not intersect will remain unchanged.
   * A line segment that crosses the rectangle at either or both
   * ends will be clipped to the boundary of the rectangle and
   * the new coordinates saved in p1 and/or p2 as necessary.
   */
  bool GetLineIntersection(FPoint* p1, FPoint* p2) const {
    return GetLineIntersection(p1 ? &p1->x : nullptr,
                               p1 ? &p1->y : nullptr,
                               p2 ? &p2->x : nullptr,
                               p2 ? &p2->y : nullptr);
  }

  /**
   * Determine whether a rectangle has no area.
   *
   * A rectangle is considered "Empty" for this function if `r` is NULL, or if
   * `r`'s width and/or height are <= 0.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @returns true if the rectangle is "Empty", false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  bool Empty() const;

  /**
   * Determine whether two floating point rectangles are equal, within some
   * given epsilon.
   *
   * Rectangles are considered equal if both are not nullptr and each of their
   * x, y, width and height are within `epsilon` of each other. If you don't
   * know what value to use for `epsilon`, you should call the FRect.Equal
   * function instead.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @param other the second rectangle to test.
   * @param epsilon the epsilon value for comparison.
   * @returns true if the rectangles are equal, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   *
   * @sa FRect.Equal
   */
  bool EqualEpsilon(const FRectRaw& other, const float epsilon) const;

  /**
   * Determine whether two floating point rectangles are equal, within a default
   * epsilon.
   *
   * Rectangles are considered equal if both are not nullptr and each of their
   * x, y, width and height are within FLT_EPSILON of each other. This is often
   * a reasonable way to compare two floating point rectangles and deal with the
   * slight precision variations in floating point calculations that tend to pop
   * up.
   *
   * Note that this is a forced-inline function in a header, and not a public
   * API function available in the SDL library (which is to say, the code is
   * embedded in the calling program and the linker and dynamic loader will not
   * be able to find this function inside SDL itself).
   *
   * @param other the second rectangle to test.
   * @returns true if the rectangles are equal, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   *
   * @sa FRect.EqualEpsilon
   */
  bool Equal(const FRectRaw& other) const;

  /**
   * Check whether the rect contains given point
   *
   * @param p Point to check
   *
   * @returns True if the point is contained in the rect
   */
  bool Contains(const FPointRaw& p) const {
    return SDL_PointInRectFloat(&p, this);
  }

  /**
   * Check whether the rect contains given point
   *
   * @param other Point to check
   *
   * @returns True if the point is contained in the rect
   */
  bool Contains(const FRectRaw& other) const {
    return GetUnion(other) == *this;
  }

  /**
   * Determine whether two rectangles intersect with float precision.
   *
   * @param other an FRect structure representing the second rectangle.
   * @returns true if there is an intersection, false otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   *
   * @sa Rect.GetIntersection
   */
  bool HasIntersection(const FRectRaw& other) const;

  /**
   * Calculate the intersection of two rectangles with float precision.
   *
   * If `result` is nullptr then this function will return false.
   *
   * @param other an FRect structure representing the second rectangle.
   * @returns an FRect structure filled in with the intersection of
   *          if there is intersection, an empty FRect otherwise.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   *
   * @sa FRect.HasIntersection
   */
  FRect GetIntersection(const FRectRaw& other) const;

  /**
   * Calculate the union of two rectangles with float precision.
   *
   * @param other an FRect structure representing the second rectangle.
   * @returns Rect representing union of two rectangles
   * @throws Error on failure.
   *
   * @threadsafety It is safe to call this function from any thread.
   *
   * @since This function is available since SDL 3.2.0.
   */
  FRect GetUnion(const FRectRaw& other) const;

  /**
   * Get a rect extended by specified amount of pixels
   *
   * @param[in] amount Number of pixels to extend by
   *
   * @returns Extended rect
   */
  constexpr FRect GetExtension(unsigned int amount) const {
    FRect r = *this;
    r.Extend(float(amount));
    return r;
  }

  /**
   * Get a rect extended by specified amount of pixels
   *
   * @param[in] hAmount Number of pixels to extend by
   *                    in horizontal direction
   * @param[in] vAmount Number of pixels to extend by
   *                    in vertical direction
   *
   * @returns Extended rect
   */
  constexpr FRect GetExtension(float hAmount, float vAmount) const {
    FRect r = *this;
    r.Extend(hAmount, vAmount);
    return r;
  }

  /**
   * Extend a rect by specified amount of pixels
   *
   * @param[in] amount Number of pixels to extend by
   *
   * @returns Reference to self
   */
  constexpr FRect& Extend(float amount) { return Extend(amount, amount); }

  /**
   * Extend a rect by specified amount of pixels
   *
   * @param[in] hAmount Number of pixels to extend by
   *                    in horizontal direction
   * @param[in] vAmount Number of pixels to extend by
   *                    in vertical direction
   *
   * @returns Reference to self
   */
  constexpr FRect& Extend(float hAmount, float vAmount) {
    x -= hAmount;
    y -= vAmount;
    w += hAmount * 2;
    h += vAmount * 2;
    return *this;
  }

  /**
   * Extend a rect by box
   * 
   * @param[in] box Reference to floating box
   * 
   * @returns Reference to self
   */
  constexpr FRect& Extend(const FBox & box);

  /**
   * Get rectangle moved by a given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Moved rectangle
   */
  constexpr FRect operator+(const FPoint& offset) const {
    return FRect(x + offset.x, y + offset.y, w, h);
  }

  /**
   * Get rectangle moved by an opposite of given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Moved rectangle
   */
  constexpr FRect operator-(const FPoint& offset) const {
    return FRect(x - offset.x, y - offset.y, w, h);
  }

  /**
   * Move by then given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Reference to self
   */
  constexpr FRect& operator+=(const FPoint& offset) {
    x += offset.x;
    y += offset.y;
    return *this;
  }

  /**
   * Move by an opposite of the given offset
   *
   * @param[in] offset Point specifying an offset
   *
   * @returns Reference to self
   */
  constexpr FRect& operator-=(const FPoint& offset) {
    x -= offset.x;
    y -= offset.y;
    return *this;
  }

  /**
   * Check whether the rect equals given rect
   * 
   * @param other Rect to check
   * @return True if the rect is equal to the rect
   */
  bool operator==(const FRect& other) const { return Equal(other); }

  /**
   * Check whether the rect does not equal given rect
   * 
   * @param other Rect to check
   * @return True if the rect is not equal to the rect
   */
  bool operator!=(const FRect& other) const { return !(*this == other); }
};

/**
 * A box stored using integer values.
 */
struct Box {
  union {
    int x1, left;
  };
  union {
    int y1, top;
  };
  union {
    int x2, right;
  };
  union {
    int y2, bottom;
  };

  /**
   * Constructs a box with all fields set to Zero.
   */
  constexpr Box() noexcept
    : Box(0, 0, 0, 0) {
  }

  /**
   * Constructs from its fields.
   *
   * @param x1 the left x.
   * @param y1 the top y.
   * @param x2 the right x.
   * @param y2 the bottom y.
   */
  constexpr Box(int x1, int y1, int x2, int y2) noexcept
    : x1(x1), y1(y1), x2(x2), y2(y2) {
  }

  /**
   * Constructs from top-left point and bottom-right point.
   * 
   * @param p1 Coordinates of the top left rectangle corner
   * @param p2 Coordinates of the bottom right rectangle corner
   */
  constexpr Box(const PointRaw& p1, const PointRaw& p2)
    : Box(p1.x, p1.y, p2.x, p2.y) {
  }

  /**
   * Constructs from an Rect.
    *
    * @param rect the Rect to construct from.
   */
  constexpr Box(const RectRaw& rect)
    : x1(rect.x), y1(rect.y), x2(rect.x + rect.w - 1), y2(rect.y + rect.h - 1) {
  }

  int GetH() const {
    return left + right;
  }

  int GetV() const {
    return top + bottom;
  }

  /**
   * Check whether the box contains given point
   * 
   * @param p Point to check
   * @return True if the point is contained in the box
   */
  bool Contains(const PointRaw& p) const {
    return p.x >= x1 && p.x <= x2 && p.y >= y1 && p.y <= y2;
  }

  /**
   * Check whether the box contains given rect
   * 
   * @param other Rect to check
   * @return True if the rect is contained in the box
   */
  bool Contains(const RectRaw& other) const {
    return other.x >= x1 && other.x + other.w <= x2 && other.y >= y1 && other.y + other.h <= y2;
  }

  /**
   * Check whether the box equals given box
   * 
   * @param other Box to check
   * @return True if the box is equal to the box
   */
  bool operator==(const Box& other) const {
    return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2;
  }

  /**
   * Check whether the box does not equal given box
   * 
   * @param other Box to check
   * @return True if the box is not equal to the box
   */
  bool operator!=(const Box& other) const {
    return !(*this == other);
  }
};

/**
 * @brief A box stored using float values.
 * * This structure can represent either an Axis-Aligned Bounding Box (AABB) 
 * using coordinates (x1, y1, x2, y2) or a set of offsets/margins 
 * (left, top, right, bottom).
 */
struct FBox {
    union { float x1, left; };   /**< Left-most coordinate or left offset */
    union { float y1, top; };    /**< Top-most coordinate or top offset */
    union { float x2, right; };  /**< Right-most coordinate or right offset */
    union { float y2, bottom; }; /**< Bottom-most coordinate or bottom offset */

    // --- Constructors ---

    /**
     * @brief Constructs a box with all fields set to zero.
     */
    constexpr FBox() noexcept : x1(0.0f), y1(0.0f), x2(0.0f), y2(0.0f) {}

    /**
     * @brief Constructs a box with all sides set to the same width.
     * @param width The uniform value for all sides.
     */
    constexpr explicit FBox(float width) noexcept : x1(width), y1(width), x2(width), y2(width) {}

    /**
     * @brief Constructs a box with symmetric horizontal and vertical values.
     * @param horizontal Value for left and right.
     * @param vertical Value for top and bottom.
     */
    constexpr FBox(float horizontal, float vertical) noexcept : x1(horizontal), y1(vertical), x2(horizontal), y2(vertical) {}

    /**
     * @brief Constructs a box from individual side values.
     * @param left The left side value.
     * @param top The top side value.
     * @param right The right side value.
     * @param bottom The bottom side value.
     */
    constexpr FBox(float left, float top, float right, float bottom) noexcept : x1(left), y1(top), x2(right), y2(bottom) {}

    /**
     * @brief Constructs a box from a top-left and a bottom-right point.
     * @param p1 The top-left point coordinates.
     * @param p2 The bottom-right point coordinates.
     */
    constexpr FBox(const FPointRaw& p1, const FPointRaw& p2) : x1(p1.x), y1(p1.y), x2(p2.x), y2(p2.y) {}

    /**
     * @brief Constructs a box from a rectangle.
     * @param rect The source rectangle.
     */
    constexpr FBox(const FRectRaw& rect) : x1(rect.x), y1(rect.y), x2(rect.x + rect.w - 1), y2(rect.y + rect.h - 1) {}

    // --- Utility Methods ---

    /**
     * @brief Calculates the total horizontal extent (left + right).
     * @return The sum of horizontal values.
     */
    constexpr float GetH() const { return left + right; }

    /**
     * @brief Calculates the total vertical extent (top + bottom).
     * @return The sum of vertical values.
     */
    constexpr float GetV() const { return top + bottom; }

    /**
     * @brief Checks if a point is contained within the box boundaries.
     * @param p The point to test.
     * @return True if the point is inside or on the edge.
     */
    bool Contains(const FPointRaw& p) const { return p.x >= x1 && p.x <= x2 && p.y >= y1 && p.y <= y2; }

    /**
     * @brief Checks if a rectangle is entirely contained within the box boundaries.
     * @param other The rectangle to test.
     * @return True if the rectangle is fully inside.
     */
    bool Contains(const FRectRaw& other) const { return other.x >= x1 && other.x + other.w <= x2 && other.y >= y1 && other.y + other.h <= y2; }

    // --- Comparison Operators ---

    /**
     * @brief Compares two boxes for equality.
     */
    bool operator==(const FBox& other) const { return x1 == other.x1 && y1 == other.y1 && x2 == other.x2 && y2 == other.y2; }

    /**
     * @brief Compares two boxes for inequality.
     */
    bool operator!=(const FBox& other) const { return !(*this == other); }

    // --- Unary Operator ---

    /**
     * @brief Negates all values in the box.
     * @return A new FBox with negated values.
     */
    constexpr FBox operator-() const noexcept {
        return FBox(-x1, -y1, -x2, -y2);
    }

    // --- Compound Assignment Operators (Box & Box) ---

    /** @brief Adds another box's values to this box. */
    constexpr FBox& operator+=(const FBox& other) noexcept {
        x1 += other.x1; y1 += other.y1; x2 += other.x2; y2 += other.y2;
        return *this;
    }
    /** @brief Subtracts another box's values from this box. */
    constexpr FBox& operator-=(const FBox& other) noexcept {
        x1 -= other.x1; y1 -= other.y1; x2 -= other.x2; y2 -= other.y2;
        return *this;
    }
    /** @brief Multiplies this box's values by another box's values. */
    constexpr FBox& operator*=(const FBox& other) noexcept {
        x1 *= other.x1; y1 *= other.y1; x2 *= other.x2; y2 *= other.y2;
        return *this;
    }
    /** @brief Divides this box's values by another box's values. */
    constexpr FBox& operator/=(const FBox& other) noexcept {
        x1 /= other.x1; y1 /= other.y1; x2 /= other.x2; y2 /= other.y2;
        return *this;
    }

    // --- Compound Assignment Operators (Box & Scalar) ---

    /** @brief Adds a scalar value to all sides of the box. */
    constexpr FBox& operator+=(float s) noexcept {
        x1 += s; y1 += s; x2 += s; y2 += s;
        return *this;
    }
    /** @brief Subtracts a scalar value from all sides of the box. */
    constexpr FBox& operator-=(float s) noexcept {
        x1 -= s; y1 -= s; x2 -= s; y2 -= s;
        return *this;
    }
    /** @brief Multiplies all sides of the box by a scalar value. */
    constexpr FBox& operator*=(float s) noexcept {
        x1 *= s; y1 *= s; x2 *= s; y2 *= s;
        return *this;
    }
    /** @brief Divides all sides of the box by a scalar value. */
    constexpr FBox& operator/=(float s) noexcept {
        float invS = 1.0f / s;
        x1 *= invS; y1 *= invS; x2 *= invS; y2 *= invS;
        return *this;
    }

    // --- Binary Operators ---
    
    /** @brief Binary addition of two boxes. */
    friend constexpr FBox operator+(FBox lhs, const FBox& rhs) noexcept { return lhs += rhs; }
    /** @brief Binary addition of a box and a scalar. */
    friend constexpr FBox operator+(FBox lhs, float rhs) noexcept { return lhs += rhs; }
    /** @brief Binary addition of a scalar and a box. */
    friend constexpr FBox operator+(float lhs, FBox rhs) noexcept { return rhs += lhs; }

    /** @brief Binary subtraction of two boxes. */
    friend constexpr FBox operator-(FBox lhs, const FBox& rhs) noexcept { return lhs -= rhs; }
    /** @brief Binary subtraction of a scalar from a box. */
    friend constexpr FBox operator-(FBox lhs, float rhs) noexcept { return lhs -= rhs; }
    /** @brief Binary subtraction of a box from a scalar. */
    friend constexpr FBox operator-(float lhs, const FBox& rhs) noexcept { 
        return FBox(lhs - rhs.x1, lhs - rhs.y1, lhs - rhs.x2, lhs - rhs.y2); 
    }

    /** @brief Binary multiplication of two boxes. */
    friend constexpr FBox operator*(FBox lhs, const FBox& rhs) noexcept { return lhs *= rhs; }
    /** @brief Binary multiplication of a box and a scalar. */
    friend constexpr FBox operator*(FBox lhs, float rhs) noexcept { return lhs *= rhs; }
    /** @brief Binary multiplication of a scalar and a box. */
    friend constexpr FBox operator*(float lhs, FBox rhs) noexcept { return rhs *= lhs; }

    /** @brief Binary division of two boxes. */
    friend constexpr FBox operator/(FBox lhs, const FBox& rhs) noexcept { return lhs /= rhs; }
    /** @brief Binary division of a box by a scalar. */
    friend constexpr FBox operator/(FBox lhs, float rhs) noexcept { return lhs /= rhs; }
    /** @brief Binary division of a scalar by a box. */
    friend constexpr FBox operator/(float lhs, const FBox& rhs) noexcept {
        return FBox(lhs / rhs.x1, lhs / rhs.y1, lhs / rhs.x2, lhs / rhs.y2);
    }
};

/**
 * A corners stored using integer values.
 */
struct Corners {
  union {
    int top_left, tl;
  };
  union {
    int top_right, tr;
  };
  union {
    int bottom_left, bl;
  };
  union {
    int bottom_right, br;
  };

  /**
   * Constructs a corners with all fields set to Zero.
   */
  constexpr Corners() noexcept
    : Corners(0, 0, 0, 0) {
  }

  /**
   * Constructs from its fields.
   *
   * @param tl the top left corner.
   * @param tr the top right corner.
   * @param bl the bottom left corner.
   * @param br the bottom right corner.
   */
  constexpr Corners(int tl, int tr, int bl, int br) noexcept
    : top_left(tl), top_right(tr), bottom_left(bl), bottom_right(br) {
  }

  /**
   * Check whether the corners equals given corners
   * 
   * @param other Corners to check
   * @return True if the corners is equal to the corners
   */
  bool operator==(const Corners& other) const {
    return top_left == other.top_left && top_right == other.top_right &&
           bottom_left == other.bottom_left && bottom_right == other.bottom_right;
  }

  /**
   * Check whether the corners does not equal given corners
   * 
   * @param other Corners to check
   * @return True if the corners is not equal to the corners
   */
  bool operator!=(const Corners& other) const {
    return !(*this == other);
  }

  Corners operator+(const Corners &other) {
    return {this->top_left + other.top_left, this->top_right + other.top_right, this->bottom_left + other.bottom_left, this->bottom_right + other.bottom_right};
  }

  Corners operator-(const Corners &other) {
    return {this->top_left - other.top_left, this->top_right - other.top_right, this->bottom_left - other.bottom_left, this->bottom_right - other.bottom_right};
  }

  Corners operator*(int scalar) {
    return {this->top_left * scalar, this->top_right * scalar, this->bottom_left * scalar, this->bottom_right * scalar};
  }

  Corners operator/(int scalar) {
    return {this->top_left / scalar, this->top_right / scalar, this->bottom_left / scalar, this->bottom_right / scalar};
  }

  Corners &operator+= (const Corners &other) {
    this->top_left += other.top_left;
    this->top_right += other.top_right;
    this->bottom_left += other.bottom_left;
    this->bottom_right += other.bottom_right;
    return *this;
  }

  Corners &operator-= (const Corners &other) {
    this->top_left -= other.top_left;
    this->top_right -= other.top_right;
    this->bottom_left -= other.bottom_left;
    this->bottom_right -= other.bottom_right;
    return *this;
  }

  Corners &operator*=(int scalar) {
    this->top_left *= scalar;
    this->top_right *= scalar;
    this->bottom_left *= scalar;
    this->bottom_right *= scalar;
    return *this;
  }

  Corners &operator/=(int scalar) {
    this->top_left /= scalar;
    this->top_right /= scalar;
    this->bottom_left /= scalar;
    this->bottom_right /= scalar;
    return *this;
  }
};

/**
 * A corners stored using float values.
 */
struct FCorners {
  union {
    float top_left, tl;
  };
  union {
    float top_right, tr;
  };
  union {
    float bottom_left, bl;
  };
  union {
    float bottom_right, br;
  };

  /**
   * Constructs a corners with all fields set to Zero.
   */
  constexpr FCorners() noexcept
    : FCorners(0.0f, 0.0f, 0.0f, 0.0f) {
  }

  /**
   * Constructs all corners with the same radius.
   *
   * @param radius the radius applied to all four corners.
   */
  constexpr explicit FCorners(float radius) noexcept
    : top_left(radius), top_right(radius), bottom_left(radius), bottom_right(radius) {
  }

  /**
   * Constructs from its fields.
   *
   * @param tl the top left corner radius.
   * @param tr the top right corner radius.
   * @param bl the bottom left corner radius.
   * @param br the bottom right corner radius.
   */
  constexpr FCorners(float tl, float tr, float bl, float br) noexcept
    : top_left(tl), top_right(tr), bottom_left(bl), bottom_right(br) {
  }

  /**
   * Check whether the corners equals given corners
   * 
   * @param other Corners to check
   * @return True if the corners is equal to the corners
   */
  bool operator==(const FCorners& other) const {
    return top_left == other.top_left && top_right == other.top_right &&
           bottom_left == other.bottom_left && bottom_right == other.bottom_right;
  }

  /**
   * Check whether the corners does not equal given corners
   * 
   * @param other Corners to check
   * @return True if the corners is not equal to the corners
   */
  bool operator!=(const FCorners& other) const {
    return !(*this == other);
  }

  FCorners operator+(const FCorners &other) {
    return {this->top_left + other.top_left, this->top_right + other.top_right, this->bottom_left + other.bottom_left, this->bottom_right + other.bottom_right};
  }

  FCorners operator-(const FCorners &other) {
    return {this->top_left - other.top_left, this->top_right - other.top_right, this->bottom_left - other.bottom_left, this->bottom_right - other.bottom_right};
  }

  FCorners operator*(float scalar) {
    return {this->top_left * scalar, this->top_right * scalar, this->bottom_left * scalar, this->bottom_right * scalar};
  }

  FCorners operator/(float scalar) {
    return {this->top_left / scalar, this->top_right / scalar, this->bottom_left / scalar, this->bottom_right / scalar};
  }

  FCorners &operator+= (const FCorners &other) {
    this->top_left += other.top_left;
    this->top_right += other.top_right;
    this->bottom_left += other.bottom_left;
    this->bottom_right += other.bottom_right;
    return *this;
  }

  FCorners &operator-= (const FCorners &other) {
    this->top_left -= other.top_left;
    this->top_right -= other.top_right;
    this->bottom_left -= other.bottom_left;
    this->bottom_right -= other.bottom_right;
    return *this;
  }

  FCorners &operator*=(int scalar) {
    this->top_left *= scalar;
    this->top_right *= scalar;
    this->bottom_left *= scalar;
    this->bottom_right *= scalar;
    return *this;
  }

  FCorners &operator/=(int scalar) {
    this->top_left /= scalar;
    this->top_right /= scalar;
    this->bottom_left /= scalar;
    this->bottom_right /= scalar;
    return *this;
  }
};

/**
 * Convert an Rect to FRect
 *
 * @param rect a pointer to an Rect.
 * @returns the floating point representation of `rect`.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline FRect RectToFRect(const RectRaw& rect) {
  FRect frect;
  SDL_RectToFRect(&rect, &frect);
  return frect;
}

/**
 * Convert an FRect to Rect
 *
 * @param rect a pointer to an FRect.
 * @returns the integer point representation of `rect`.
 *
 * @threadsafety It is safe to call this function from any thread.
 */
inline Rect FRectToRect(const FRectRaw& rect) {
  Rect frect = {(int)rect.x, (int)rect.y, (int)SDL::Ceil(rect.w), (int)SDL::Ceil(rect.h)};
  return frect;
}

inline Rect::operator SDL_FRect() const { return SDL::RectToFRect(*this); }

/**
 * Convert an Box to FBox
 * 
 * @param box a pointer to an Box.
 * @returns the floating point representation of `box`.
 * 
 * @threadsafety It is safe to call this function from any thread.
 */
inline FBox BoxToFBox(const Box& box) {
  FBox fbox(
    static_cast<float>(box.x1), static_cast<float>(box.y1),
    static_cast<float>(box.x2), static_cast<float>(box.y2)
  );
  return fbox;
}

/// Min
inline Box Min(const Box& a, int s) {
  return {SDL::Min(a.left, s), SDL::Min(a.right, s), SDL::Min(a.top, s), SDL::Min(a.bottom, s)};
}
inline FBox Min(const FBox& a, float s) {
  return {SDL::Min(a.left, s), SDL::Min(a.right, s), SDL::Min(a.top, s), SDL::Min(a.bottom, s)};
}
inline Box Min(const Box& a, const Box& b) {
  return {SDL::Min(a.left, b.left), SDL::Min(a.right, b.right), SDL::Min(a.top, b.top), SDL::Min(a.bottom, b.bottom)};
}
inline FBox Min(const FBox& a, const FBox& b) {
  return {SDL::Min(a.left, b.left), SDL::Min(a.right, b.right), SDL::Min(a.top, b.top), SDL::Min(a.bottom, b.bottom)};
}

inline Corners Min(const Corners& a, int s) {
  return {SDL::Min(a.bl, s), SDL::Min(a.br, s), SDL::Min(a.tl, s), SDL::Min(a.tr, s)};
}
inline FCorners Min(const FCorners& a, float s) {
  return {SDL::Min(a.bl, s), SDL::Min(a.br, s), SDL::Min(a.tl, s), SDL::Min(a.tr, s)};
}
inline Corners Min(const Corners& a, const Corners& b) {
  return {SDL::Min(a.bl, b.bl), SDL::Min(a.br, b.br), SDL::Min(a.tl, b.tl), SDL::Min(a.tr, b.tr)};
}
inline FCorners Min(const FCorners& a, const FCorners& b) {
  return {SDL::Min(a.bl, b.bl), SDL::Min(a.br, b.br), SDL::Min(a.tl, b.tl), SDL::Min(a.tr, b.tr)};
}

/// Max
inline Box Max(const Box& a, int s) {
  return {SDL::Max(a.left, s), SDL::Max(a.right, s), SDL::Max(a.top, s), SDL::Max(a.bottom, s)};
}
inline FBox Max(const FBox& a, float s) {
  return {SDL::Max(a.left, s), SDL::Max(a.right, s), SDL::Max(a.top, s), SDL::Max(a.bottom, s)};
}
inline Box Max(const Box& a, const Box& b) {
  return {SDL::Max(a.left, b.left), SDL::Max(a.right, b.right), SDL::Max(a.top, b.top), SDL::Max(a.bottom, b.bottom)};
}
inline FBox Max(const FBox& a, const FBox& b) {
  return {SDL::Max(a.left, b.left), SDL::Max(a.right, b.right), SDL::Max(a.top, b.top), SDL::Max(a.bottom, b.bottom)};
}

inline Corners Max(const Corners& a, int s) {
  return {SDL::Max(a.bl, s), SDL::Max(a.br, s), SDL::Max(a.tl, s), SDL::Max(a.tr, s)};
}
inline FCorners Max(const FCorners& a, float s) {
  return {SDL::Max(a.bl, s), SDL::Max(a.br, s), SDL::Max(a.tl, s), SDL::Max(a.tr, s)};
}
inline Corners Max(const Corners& a, const Corners& b) {
  return {SDL::Max(a.bl, b.bl), SDL::Max(a.br, b.br), SDL::Max(a.tl, b.tl), SDL::Max(a.tr, b.tr)};
}
inline FCorners Max(const FCorners& a, const FCorners& b) {
  return {SDL::Max(a.bl, b.bl), SDL::Max(a.br, b.br), SDL::Max(a.tl, b.tl), SDL::Max(a.tr, b.tr)};
}

/// Clamp
inline Box Clamp(const Box& a, int lo, int hi) {
  return {SDL::Clamp(a.left, lo, hi), SDL::Clamp(a.right, lo, hi), SDL::Clamp(a.top, lo, hi), SDL::Clamp(a.bottom, lo, hi)};
}
inline FBox Clamp(const FBox& a, float lo, float hi) {
  return {SDL::Clamp(a.left, lo, hi), SDL::Clamp(a.right, lo, hi), SDL::Clamp(a.top, lo, hi), SDL::Clamp(a.bottom, lo, hi)};
}
inline Box Clamp(const Box& a, const Box& rlo, const Box& rhi) {
  return {SDL::Clamp(a.left, rlo.left, rhi.left), SDL::Clamp(a.right, rlo.right, rhi.right), SDL::Clamp(a.top, rlo.top, rhi.top), SDL::Clamp(a.bottom, rlo.bottom, rhi.bottom)};
}
inline FBox Clamp(const FBox& a, const FBox& rlo, const FBox& rhi) {
  return {SDL::Clamp(a.left, rlo.left, rhi.left), SDL::Clamp(a.right, rlo.right, rhi.right), SDL::Clamp(a.top, rlo.top, rhi.top), SDL::Clamp(a.bottom, rlo.bottom, rhi.bottom)};
}

inline Corners Clamp(const Corners& a, int lo, int hi) {
  return {SDL::Clamp(a.bl, lo, hi), SDL::Clamp(a.br, lo, hi), SDL::Clamp(a.tl, lo, hi), SDL::Clamp(a.tr, lo, hi)};
}
inline FCorners Clamp(const FCorners& a, float lo, float hi) {
  return {SDL::Clamp(a.bl, lo, hi), SDL::Clamp(a.br, lo, hi), SDL::Clamp(a.tl, lo, hi), SDL::Clamp(a.tr, lo, hi)};
}
inline Corners Clamp(const Corners& a, const Corners& rlo, const Corners& rhi) {
  return {SDL::Clamp(a.bl, rlo.bl, rhi.bl), SDL::Clamp(a.br, rlo.br, rhi.br), SDL::Clamp(a.tl, rlo.tl, rhi.tl), SDL::Clamp(a.tr, rlo.tr, rhi.tr)};
}
inline FCorners Clamp(const FCorners& a, const FCorners& rlo, const FCorners& rhi) {
  return {SDL::Clamp(a.bl, rlo.bl, rhi.bl), SDL::Clamp(a.br, rlo.br, rhi.br), SDL::Clamp(a.tl, rlo.tl, rhi.tl), SDL::Clamp(a.tr, rlo.tr, rhi.tr)};
}


/**
 * Convert an Box to Rect
 * 
 * @param box a pointer to an Box.
 * @returns the Rect representation of `box`.
 * 
 * @threadsafety It is safe to call this function from any thread.
 */
inline Rect BoxToRect(const Box& box) {
  Rect rect(
    box.x1, box.y1,
    box.x2 - box.x1 + 1, box.y2 - box.y1 + 1
  );
  return rect;
}

/**
 * Convert an FBox to FRect
 * 
 * @param fbox a pointer to an FBox.
 * @returns the FRect representation of `fbox`.
 * 
 * @threadsafety It is safe to call this function from any thread.
 */
inline FRect FBoxToFRect(const FBox& fbox) {
  FRect frect(
    fbox.x1, fbox.y1,
    fbox.x2 - fbox.x1 + 1, fbox.y2 - fbox.y1 + 1
  );
  return frect;
}

/**
 * Convert an Corners to FCorners
 * 
 * @param corners a pointer to an Corners.
 * @returns the floating point representation of `corners`.
 * 
 * @threadsafety It is safe to call this function from any thread.
 */
inline FCorners CornersToFCorners(const Corners& corners) {
  FCorners fcorners(
    static_cast<float>(corners.top_left), static_cast<float>(corners.top_right),
    static_cast<float>(corners.bottom_left), static_cast<float>(corners.bottom_right)
  );
  return fcorners;
}

/**
 * Determine whether a point resides inside a rectangle.
 *
 * A point is considered part of a rectangle if both `p` and `r` are not
 * nullptr, and `p`'s x and y coordinates are >= to the rectangle's top left
 * corner, and < the rectangle's x+w and y+h. So a 1x1 rectangle considers point
 * (0,0) as "inside" and (0,1) as not.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param p the point to test.
 * @param r the rectangle to test.
 * @returns true if `p` is contained by `r`, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool PointInRect(const PointRaw& p, const RectRaw& r) {
  return SDL_PointInRect(&p, &r);
}

inline bool Point::InRect(const RectRaw& r) const {
  return SDL::PointInRect(*this, r);
}

/**
 * Determine whether a rectangle has no area.
 *
 * A rectangle is considered "Empty" for this function if `r` is nullptr, or if
 * `r`'s width and/or height are <= 0.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param r the rectangle to test.
 * @returns true if the rectangle is "Empty", false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool RectEmpty(const RectRaw& r) { return SDL_RectEmpty(&r); }

inline bool Rect::Empty() const { return SDL::RectEmpty(*this); }

/**
 * Determine whether two rectangles are equal.
 *
 * Rectangles are considered equal if both are not nullptr and each of their x,
 * y, width and height match.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param a the first rectangle to test.
 * @param b the second rectangle to test.
 * @returns true if the rectangles are equal, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool RectsEqual(const RectRaw& a, const RectRaw& b) {
  return SDL_RectsEqual(&a, &b);
}

inline bool Rect::Equal(const RectRaw& other) const {
  return SDL::RectsEqual(*this, other);
}

/**
 * Determine whether two rectangles intersect.
 *
 * If either pointer is nullptr the function will return false.
 *
 * @param A an Rect structure representing the first rectangle.
 * @param B an Rect structure representing the second rectangle.
 * @returns true if there is an intersection, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Rect.GetIntersection
 */
inline bool HasRectIntersection(const RectRaw& A, const RectRaw& B) {
  return SDL_HasRectIntersection(&A, &B);
}

inline bool Rect::HasIntersection(const RectRaw& other) const {
  return SDL::HasRectIntersection(*this, other);
}

/**
 * Calculate the intersection of two rectangles.
 *
 * If `result` is nullptr then this function will return false.
 *
 * @param A an Rect structure representing the first rectangle.
 * @param B an Rect structure representing the second rectangle.
 * @returns a Rect structure filled in with the intersection of if there is
 *          intersection, std::nullopt otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Rect.HasIntersection
 */
inline Rect GetRectIntersection(const RectRaw& A, const RectRaw& B) {
  if (Rect result; SDL_GetRectIntersection(&A, &B, &result)) return result;
  return {};
}

inline Rect Rect::GetIntersection(const RectRaw& other) const {
  return SDL::GetRectIntersection(*this, other);
}

/**
 * Calculate the union of two rectangles.
 *
 * @param A an Rect structure representing the first rectangle.
 * @param B an Rect structure representing the second rectangle.
 * @returns Rect representing union of two rectangles
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline Rect GetRectUnion(const RectRaw& A, const RectRaw& B) {
  Rect r;
  CheckError(SDL_GetRectUnion(&A, &B, &r));
  return r;
}

inline Rect Rect::GetUnion(const RectRaw& other) const {
  return SDL::GetRectUnion(*this, other);
}

/**
 * Calculate a minimal rectangle enclosing a set of points.
 *
 * If `clip` is not nullopt then only points inside of the clipping rectangle
 * are considered.
 *
 * @param points an array of Point structures representing points to be
 *               enclosed.
 * @param clip an Rect used for clipping or nullptr to enclose all points.
 * @returns Result if any points were enclosed or empty rect if all the points
 * were outside of the clipping rectangle.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline Rect GetRectEnclosingPoints(
  SpanRef<const PointRaw> points,
  OptionalRef<const RectRaw> clip = std::nullopt) {
  if (Rect result; SDL_GetRectEnclosingPoints(
        points.data(), NarrowS32(points.size()), clip, &result)) {
    return result;
  }
  return {};
}

inline Rect Rect::GetEnclosingPoints(SpanRef<const PointRaw> points,
                                     OptionalRef<const RectRaw> clip) {
  return SDL::GetRectEnclosingPoints(points, clip);
}

/**
 * Calculate the intersection of a rectangle and line segment.
 *
 * This function is used to clip a line segment to a rectangle. A line segment
 * contained entirely within the rectangle or that does not intersect will
 * remain unchanged. A line segment that crosses the rectangle at either or both
 * ends will be clipped to the boundary of the rectangle and the new coordinates
 * saved in `X1`, `Y1`, `X2`, and/or `Y2` as necessary.
 *
 * @param rect an Rect structure representing the rectangle to intersect.
 * @param X1 a pointer to the starting X-coordinate of the line.
 * @param Y1 a pointer to the starting Y-coordinate of the line.
 * @param X2 a pointer to the ending X-coordinate of the line.
 * @param Y2 a pointer to the ending Y-coordinate of the line.
 * @returns true if there is an intersection, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool GetRectAndLineIntersection(const RectRaw& rect,
                                       int* X1,
                                       int* Y1,
                                       int* X2,
                                       int* Y2) {
  return SDL_GetRectAndLineIntersection(&rect, X1, Y1, X2, Y2);
}

inline bool Rect::GetLineIntersection(int* X1, int* Y1, int* X2, int* Y2) const {
  return SDL::GetRectAndLineIntersection(*this, X1, Y1, X2, Y2);
}

/**
 * Determine whether a point resides inside a floating point rectangle.
 *
 * A point is considered part of a rectangle if both `p` and `r` are not
 * nullptr, and `p`'s x and y coordinates are >= to the rectangle's top left
 * corner, and <= the rectangle's x+w and y+h. So a 1x1 rectangle considers
 * point (0,0) and (0,1) as "inside" and (0,2) as not.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param p the point to test.
 * @param r the rectangle to test.
 * @returns true if `p` is contained by `r`, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool PointInRectFloat(const FPointRaw& p, const FRectRaw& r) {
  return SDL_PointInRectFloat(&p, &r);
}

inline bool FPoint::InRect(const FRectRaw& r) const {
  return SDL::PointInRectFloat(*this, r);
}

/**
 * Determine whether a floating point rectangle takes no space.
 *
 * A rectangle is considered "Empty" for this function if `r` is nullptr, or if
 * `r`'s width and/or height are < 0.0f.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param r the rectangle to test.
 * @returns true if the rectangle is "Empty", false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool RectEmptyFloat(const FRectRaw& r) { return SDL_RectEmptyFloat(&r); }

inline bool FRect::Empty() const { return SDL::RectEmptyFloat(*this); }

inline constexpr FRect &FRect::Extend(const FBox &box) {
  x -= box.left;
  y -= box.top;
  w += box.GetH();
  h += box.GetV();
  return *this;
}

/**
 * Determine whether two floating point rectangles are equal, within some given
 * epsilon.
 *
 * Rectangles are considered equal if both are not nullptr and each of their x,
 * y, width and height are within `epsilon` of each other. If you don't know
 * what value to use for `epsilon`, you should call the FRect.Equal function
 * instead.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param a the first rectangle to test.
 * @param b the second rectangle to test.
 * @param epsilon the epsilon value for comparison.
 * @returns true if the rectangles are equal, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa FRect.Equal
 */
inline bool RectsEqualEpsilon(const FRectRaw& a,
                              const FRectRaw& b,
                              const float epsilon) {
  return SDL_RectsEqualEpsilon(&a, &b, epsilon);
}

inline bool FRect::EqualEpsilon(const FRectRaw& other,
                                const float epsilon) const {
  return SDL::RectsEqualEpsilon(*this, other, epsilon);
}

/**
 * Determine whether two floating point rectangles are equal, within a default
 * epsilon.
 *
 * Rectangles are considered equal if both are not nullptr and each of their x,
 * y, width and height are within FLT_EPSILON of each other. This is often a
 * reasonable way to compare two floating point rectangles and deal with the
 * slight precision variations in floating point calculations that tend to pop
 * up.
 *
 * Note that this is a forced-inline function in a header, and not a public API
 * function available in the SDL library (which is to say, the code is embedded
 * in the calling program and the linker and dynamic loader will not be able to
 * find this function inside SDL itself).
 *
 * @param a the first rectangle to test.
 * @param b the second rectangle to test.
 * @returns true if the rectangles are equal, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa FRect.EqualEpsilon
 */
inline bool RectsEqualFloat(const FRectRaw& a, const FRectRaw& b) {
  return SDL_RectsEqualFloat(&a, &b);
}

inline bool FRect::Equal(const FRectRaw& other) const {
  return SDL::RectsEqualFloat(*this, other);
}

/**
 * Determine whether two rectangles intersect with float precision.
 *
 * If either pointer is nullptr the function will return false.
 *
 * @param A an FRect structure representing the first rectangle.
 * @param B an FRect structure representing the second rectangle.
 * @returns true if there is an intersection, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Rect.GetIntersection
 */
inline bool HasRectIntersectionFloat(const FRectRaw& A, const FRectRaw& B) {
  return SDL_HasRectIntersectionFloat(&A, &B);
}

inline bool FRect::HasIntersection(const FRectRaw& other) const {
  return SDL::HasRectIntersectionFloat(*this, other);
}

/**
 * Calculate the intersection of two rectangles with float precision.
 *
 * If `result` is nullptr then this function will return false.
 *
 * @param A an FRect structure representing the first rectangle.
 * @param B an FRect structure representing the second rectangle.
 * @returns a FRect structure filled in with the intersection of if there is
 *          intersection, std::nullopt otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa FRect.HasIntersection
 */
inline FRect GetRectIntersectionFloat(const FRectRaw& A, const FRectRaw& B) {
  if (FRect r; SDL_GetRectIntersectionFloat(&A, &B, &r)) return r;
  return {};
}

inline FRect FRect::GetIntersection(const FRectRaw& other) const {
  return SDL::GetRectIntersectionFloat(*this, other);
}

/**
 * Calculate the union of two rectangles with float precision.
 *
 * @param A an FRect structure representing the first rectangle.
 * @param B an FRect structure representing the second rectangle.
 * @returns a FRect structure filled in with the union of rectangles `A` and
 *          `B`.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline FRect GetRectUnionFloat(const FRectRaw& A, const FRectRaw& B) {
  FRect r;
  SDL::CheckError(SDL_GetRectUnionFloat(&A, &B, &r));
  return r;
}

inline FRect FRect::GetUnion(const FRectRaw& other) const {
  return SDL::GetRectUnionFloat(*this, other);
}

/**
 * Calculate a minimal rectangle enclosing a set of points with float precision.
 *
 * If `clip` is not std::nullopt then only points inside of the clipping
 * rectangle are considered.
 *
 * @param points an array of FPoint structures representing points to be
 *               enclosed.
 * @param clip an FRect used for clipping or nullptr to enclose all points.
 * @returns a FRect structure filled in with the minimal enclosing rectangle or
 *          false if all the points were outside of the clipping rectangle.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline FRect GetRectEnclosingPointsFloat(
  SpanRef<const FPointRaw> points,
  OptionalRef<const FRectRaw> clip = std::nullopt) {
  if (FRect result; SDL_GetRectEnclosingPointsFloat(
        points.data(), NarrowS32(points.size()), clip, &result)) {
    return result;
  }
  return {};
}

inline FRect FRect::GetEnclosingPoints(SpanRef<const FPointRaw> points,
                                       OptionalRef<const FRectRaw> clip) {
  return SDL::GetRectEnclosingPointsFloat(points, clip);
}

/**
 * Calculate the intersection of a rectangle and line segment with float
 * precision.
 *
 * This function is used to clip a line segment to a rectangle. A line segment
 * contained entirely within the rectangle or that does not intersect will
 * remain unchanged. A line segment that crosses the rectangle at either or both
 * ends will be clipped to the boundary of the rectangle and the new coordinates
 * saved in `X1`, `Y1`, `X2`, and/or `Y2` as necessary.
 *
 * @param rect an FRect structure representing the rectangle to intersect.
 * @param X1 a pointer to the starting X-coordinate of the line.
 * @param Y1 a pointer to the starting Y-coordinate of the line.
 * @param X2 a pointer to the ending X-coordinate of the line.
 * @param Y2 a pointer to the ending Y-coordinate of the line.
 * @returns true if there is an intersection, false otherwise.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline bool GetRectAndLineIntersectionFloat(const FRectRaw& rect,
                                            float* X1,
                                            float* Y1,
                                            float* X2,
                                            float* Y2) {
  return SDL_GetRectAndLineIntersectionFloat(&rect, X1, Y1, X2, Y2);
}

inline bool FRect::GetLineIntersection(float* X1,
                                       float* Y1,
                                       float* X2,
                                       float* Y2) const {
  return SDL::GetRectAndLineIntersectionFloat(*this, X1, Y1, X2, Y2);
}

/// @}

constexpr Point::operator FPoint() const { return {float(x), float(y)}; }

constexpr FPoint Point::operator/(float value) const {
  return FPoint(*this) / value;
}
constexpr FPoint Point::operator*(float value) const {
  return FPoint(*this) * value;
}

constexpr Point Point::GetClamped(const Rect& rect) const {
  Point p = *this;
  p.Clamp(rect);
  return p;
}

constexpr Point& Point::Clamp(const Rect& rect) {
  if (x < rect.x) x = rect.x;
  if (x > rect.GetX2()) x = rect.GetX2();
  if (y < rect.y) y = rect.y;
  if (y > rect.GetY2()) y = rect.GetY2();
  return *this;
}

constexpr Point Point::GetWrapped(const Rect& rect) const {
  Point p = *this;
  p.Wrap(rect);
  return p;
}

constexpr Point& Point::Wrap(const Rect& rect) {
  if (x < rect.x)
    x = rect.x + rect.w - 1 - (rect.x - x + rect.w - 1) % rect.w;
  else if (x >= rect.x + rect.w)
    x = rect.x + (x - rect.x - rect.w) % rect.w;

  if (y < rect.y)
    y = rect.y + rect.h - 1 - (rect.y - y + rect.h - 1) % rect.h;
  else if (y >= rect.y + rect.h)
    y = rect.y + (y - rect.y - rect.h) % rect.h;

  return *this;
}

constexpr FPoint FPoint::GetClamped(const FRect& rect) const {
  FPoint p = *this;
  p.Clamp(rect);
  return p;
}

constexpr FPoint& FPoint::Clamp(const FRect& rect) {
  if (x < rect.x) x = rect.x;
  if (x > rect.GetX2()) x = rect.GetX2();
  if (y < rect.y) y = rect.y;
  if (y > rect.GetY2()) y = rect.GetY2();
  return *this;
}

constexpr FPoint FPoint::GetWrapped(const FRect& rect) const {
  FPoint p = *this;
  p.Wrap(rect);
  return p;
}

constexpr FPoint& FPoint::Wrap(const FRect& rect) {
  if (x < rect.x)
    x = rect.x + rect.w - 1 - SDL::Fmod(rect.x - x + rect.w - 1, rect.w);
  else if (x >= rect.x + rect.w)
    x = rect.x + SDL::Fmod(x - rect.x - rect.w, rect.w);

  if (y < rect.y)
    y = rect.y + rect.h - 1 - SDL::Fmod(rect.y - y + rect.h - 1, rect.h);
  else if (y >= rect.y + rect.h)
    y = rect.y + SDL::Fmod(y - rect.y - rect.h, rect.h);

  return *this;
}

constexpr Rect::operator FRect() const {
  return {float(x), float(y), float(w), float(h)};
}

inline FPoint Lerp(const FPointRaw& a, const FPointRaw& b, float t) {
  return FPoint(
    SDL::Lerp(a.x, b.x, t),
    SDL::Lerp(a.y, b.y, t)
  );
}

} // namespace SDL

#endif /* SDL3PP_RECT_H_ */
