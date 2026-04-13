#ifndef SDL3PP_REF_H_
#define SDL3PP_REF_H_

#include <string>

/**
 * Reference for Renderer.
 *
 * This does not take ownership!
 */
template <class Base, typename BaseRaw>
struct Ref : Base {
  using Base::Base;

  /**
   * Constructs from raw Base.
   *
   * @param resource a Raw.
   *
   * This does not takes ownership!
   */
  constexpr Ref(BaseRaw resource) noexcept
    : Base(resource) {
  }

  /**
   * Constructs from Base.
   *
   * @param resource a Base.
   *
   * This does not takes ownership!
   */
  constexpr Ref(const Base& resource) noexcept
    : Base(resource.Get()) {
  }

  /**
   * Constructs from Base.
   *
   * @param resource a Base.
   *
   * This will Release the ownership from resource!
   */
  constexpr Ref(Base&& resource) noexcept
    : Base(std::move(resource).Release()) {
  }

  /// Copy constructor.
  constexpr Ref(const Ref& other) noexcept
    : Renderer(other.Get()) {
  }

  /// Move constructor.
  constexpr Ref(Ref&& other) noexcept
    : Base(other.Get()) {
  }

  /// Destructor
  ~Ref() { Release(); }

  /// Assignment operator.
  Ref& operator=(const Ref& other) noexcept {
    Release();
    Base::operator=(Base(other.Get()));
    return *this;
  }

  /// Converts to BaseRaw
  constexpr operator BaseRaw() const noexcept { return Get(); }
};

#endif /* SDL3PP_REF_H_ */