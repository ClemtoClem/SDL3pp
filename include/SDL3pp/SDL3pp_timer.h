#ifndef SDL3PP_TIMER_H_
#define SDL3PP_TIMER_H_

#include <SDL3/SDL_timer.h>
#include "SDL3pp_stdinc.h"

namespace SDL {

/**
 * @defgroup CategoryTimer Timer Support
 *
 * SDL provides time management functionality. It is useful for dealing with
 * (usually) small durations of time.
 *
 * This is not to be confused with _calendar time_ management, which is provided
 * by [CategoryTime](#CategoryTime).
 *
 * This category covers measuring time elapsed (GetTicks(),
 * GetPerformanceCounter()), putting a thread to sleep for a certain amount of
 * time (Delay(), DelayNS(), DelayPrecise()), and firing a callback function
 * after a certain amount of time has elapsed (AddTimer(), etc).
 *
 * @{
 */

constexpr Time Time::FromPosix(Sint64 time) {
  return Time::FromNS(SDL_SECONDS_TO_NS(time));
}

constexpr Sint64 Time::ToPosix() const {
  return SDL_NS_TO_SECONDS(m_time.count());
}

/**
 * Get the time elapsed since SDL library initialization.
 *
 * @returns a std::chrono::nanoseconds value representing the number of
 *          nanoseconds since the SDL library initialized.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetTicksMS
 * @sa GetTicksNS
 */
inline std::chrono::nanoseconds GetTicks() {
  return std::chrono::nanoseconds(SDL_GetTicksNS());
}

/**
 * Get the number of milliseconds that have elapsed since the SDL library
 * initialization.
 *
 * @returns an unsigned 64‑bit integer that represents the number of
 *          milliseconds that have elapsed since the SDL library was initialized
 *          (typically via a call to Init).
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetTicksNS
 */
inline Uint64 GetTicksMS() { return SDL_GetTicks(); }

/**
 * Get the number of nanoseconds since SDL library initialization.
 *
 * @returns an unsigned 64-bit value representing the number of nanoseconds
 *          since the SDL library initialized.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 */
inline Uint64 GetTicksNS() { return SDL_GetTicksNS(); }

/**
 * Get the current value of the high resolution counter.
 *
 * This function is typically used for profiling.
 *
 * The counter values are only meaningful relative to each other. Differences
 * between values can be converted to times by using GetPerformanceFrequency().
 *
 * @returns the current counter value.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetPerformanceFrequency
 */
inline Uint64 GetPerformanceCounter() { return SDL_GetPerformanceCounter(); }

/**
 * Get the count per second of the high resolution counter.
 *
 * @returns a platform-specific count per second.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa GetPerformanceCounter
 */
inline Uint64 GetPerformanceFrequency() {
  return SDL_GetPerformanceFrequency();
}

/**
 * Wait a specified number of milliseconds before returning.
 *
 * This function waits a specified number of milliseconds before returning. It
 * waits at least the specified time, but possibly longer due to OS scheduling.
 *
 * @param ms the number of milliseconds to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay(std::chrono::nanoseconds)
 * @sa DelayNS
 * @sa DelayPrecise
 */
inline void Delay(Uint32 ms) { SDL_Delay(ms); }

/**
 * Wait a specified duration before returning.
 *
 * This function waits a specified duration before returning. It
 * waits at least the specified time, but possibly longer due to OS scheduling.
 *
 * @param duration the duration to delay, with Max precision in ns.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa DelayNS
 * @sa DelayPrecise(std::chrono::nanoseconds)
 */
inline void Delay(std::chrono::nanoseconds duration) {
  SDL_DelayNS(duration.count());
}

/**
 * Wait a specified number of nanoseconds before returning.
 *
 * This function waits a specified number of nanoseconds before returning. It
 * waits at least the specified time, but possibly longer due to OS scheduling.
 *
 * @param ns the number of nanoseconds to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay
 * @sa DelayPrecise(std::chrono::nanoseconds)
 */
inline void DelayNS(Uint64 ns) { SDL_DelayNS(ns); }

/**
 * Wait a specified number of nanoseconds before returning.
 *
 * This function waits a specified number of nanoseconds before returning. It
 * will attempt to wait as close to the requested time as possible, busy waiting
 * if necessary, but could return later due to OS scheduling.
 *
 * @param ns the number of nanoseconds to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay
 * @sa DelayNS
 * @sa DelayPrecise(std::chrono::nanoseconds)
 */
inline void DelayPrecise(Uint64 ns) { SDL_DelayPrecise(ns); }

/**
 * Wait a specified duration before returning.
 *
 * This function waits a specified duration before returning. It
 * will attempt to wait as close to the requested time as possible, busy waiting
 * if necessary, but could return later due to OS scheduling.
 *
 * @param duration the duration to delay.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa Delay(Uint32)
 * @sa Delay(std::chrono::nanoseconds)
 * @sa DelayNS
 * @sa DelayPrecise(Uint64)
 */
inline void DelayPrecise(std::chrono::nanoseconds duration) {
  SDL_DelayPrecise(duration.count());
}

/**
 * Definition of the timer ID type.
 *
 * @since This datatype is available since SDL 3.2.0.
 */
using TimerID = SDL_TimerID;

/**
 * Function prototype for the millisecond timer callback function.
 *
 * The callback function is passed the current timer interval and returns the
 * next timer interval, in milliseconds. If the returned value is the same as
 * the one passed in, the periodic alarm continues, otherwise a new alarm is
 * scheduled. If the callback returns 0, the periodic alarm is canceled and will
 * be removed.
 *
 * @param userdata an arbitrary pointer provided by the app through AddTimer,
 *                 for its own use.
 * @param timerID the current timer being processed.
 * @param interval the current callback time interval.
 * @returns the new callback time interval, or 0 to disable further runs of the
 *          callback.
 *
 * @threadsafety SDL may call this callback at any time from a background
 *               thread; the application is responsible for locking resources
 *               the callback touches that need to be protected.
 *
 * @since This datatype is available since SDL 3.2.0.
 *
 * @sa AddTimer
 */
using MSTimerCallback = Uint32(SDLCALL*)(void* userdata,
                                         TimerID timerID,
                                         Uint32 interval);

/**
 * Function prototype for the nanosecond timer callback function.
 *
 * The callback function is passed the current timer interval and returns the
 * next timer interval, in nanoseconds. If the returned value is the same as the
 * one passed in, the periodic alarm continues, otherwise a new alarm is
 * scheduled. If the callback returns 0, the periodic alarm is canceled and will
 * be removed.
 *
 * @param userdata an arbitrary pointer provided by the app through AddTimer,
 *                 for its own use.
 * @param timerID the current timer being processed.
 * @param interval the current callback time interval.
 * @returns the new callback time interval, or 0 to disable further runs of the
 *          callback.
 *
 * @threadsafety SDL may call this callback at any time from a background
 *               thread; the application is responsible for locking resources
 *               the callback touches that need to be protected.
 *
 * @since This datatype is available since SDL 3.2.0.
 *
 * @sa AddTimer
 */
using NSTimerCallback = Uint64(SDLCALL*)(void* userdata,
                                         TimerID timerID,
                                         Uint64 interval);

/**
 * Function prototype for the nanosecond timer callback function.
 *
 * The callback function is passed the current timer interval and returns the
 * next timer interval, in nanoseconds. If the returned value is the same as the
 * one passed in, the periodic alarm continues, otherwise a new alarm is
 * scheduled. If the callback returns 0, the periodic alarm is canceled and will
 * be removed.
 *
 * @param timerID the current timer being processed.
 * @param interval the current callback time interval.
 * @returns the new callback time interval, or 0 to disable further runs of the
 *          callback.
 *
 * @threadsafety SDL may call this callback at any time from a background
 *               thread; the application is responsible for locking resources
 *               the callback touches that need to be protected.
 *
 * @since This datatype is available since SDL 3.2.0.
 *
 * @cat listener-callback
 *
 * @sa AddTimer
 *
 * @sa NSTimerCallback
 */
struct TimerCB : LightweightCallbackT<TimerCB, Uint64, TimerID, Uint64> {
  /// ctor
  template<std::invocable<TimerID, std::chrono::nanoseconds> F>
  TimerCB(const F& func)
    : LightweightCallbackT<TimerCB, Uint64, TimerID, Uint64>(func) {
  }

  /// @private
  template<std::invocable<TimerID, std::chrono::nanoseconds> F>
  static Uint64 DoCall(F& func, TimerID timerID, Uint64 interval) {
    return func(timerID, std::chrono::nanoseconds(interval)).count();
  }
};

/**
 * Call a callback function at a future time.
 *
 * The callback function is passed the current timer interval and the user
 * supplied parameter from the AddTimer() call and should return the next timer
 * interval. If the value returned from the callback is 0, the timer is canceled
 * and will be removed.
 *
 * The callback is run on a separate thread, and for short timeouts can
 * potentially be called before this function returns.
 *
 * Timers take into account the amount of time it took to execute the callback.
 * For example, if the callback took 250 ms to execute and returned 1000 (ms),
 * the timer would only wait another 750 ms before its next iteration.
 *
 * Timing may be inexact due to OS scheduling. Be sure to note the current time
 * with GetTicksNS() or GetPerformanceCounter() in case your callback needs to
 * adjust for variances.
 *
 * @param interval the timer delay, in std::chrono::nanoseconds, passed to
 *                 `callback`.
 * @param callback the NSTimerCallback function to call when the specified
 *                 `interval` elapses.
 * @param userdata a pointer that is passed to `callback`.
 * @returns a timer ID or 0 on failure; call GetError() for more information.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa AddTimer(std::chrono::nanoseconds, NSTimerCallback, void*)
 * @sa AddTimer(std::chrono::nanoseconds, TimerCB)
 * @sa RemoveTimer
 */
inline TimerID AddTimer(std::chrono::milliseconds interval,
                        MSTimerCallback callback,
                        void* userdata) {
  return SDL_AddTimer(Uint32(interval.count()), callback, userdata);
}

/**
 * Call a callback function at a future time.
 *
 * The callback function is passed the current timer interval and the user
 * supplied parameter from the AddTimer() call and should return the next timer
 * interval. If the value returned from the callback is 0, the timer is canceled
 * and will be removed.
 *
 * The callback is run on a separate thread, and for short timeouts can
 * potentially be called before this function returns.
 *
 * Timers take into account the amount of time it took to execute the callback.
 * For example, if the callback took 250 ns to execute and returned 1000 (ns),
 * the timer would only wait another 750 ns before its next iteration.
 *
 * Timing may be inexact due to OS scheduling. Be sure to note the current time
 * with GetTicksNS() or GetPerformanceCounter() in case your callback needs to
 * adjust for variances.
 *
 * @param interval the timer delay, in std::chrono::nanoseconds, passed to
 *                 `callback`.
 * @param callback the NSTimerCallback function to call when the specified
 *                 `interval` elapses.
 * @param userdata a pointer that is passed to `callback`.
 * @returns a timer ID.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa AddTimer(std::chrono::milliseconds, MSTimerCallback, void*)
 * @sa AddTimer(std::chrono::nanoseconds, TimerCB)
 * @sa RemoveTimer
 */
inline TimerID AddTimer(std::chrono::nanoseconds interval,
                        NSTimerCallback callback,
                        void* userdata) {
  return CheckError(SDL_AddTimerNS(interval.count(), callback, userdata));
}

/**
 * Call a callback function at a future time.
 *
 * The callback function is passed the current timer interval and the user
 * supplied parameter from the AddTimer() call and should return the next timer
 * interval. If the value returned from the callback is 0, the timer is canceled
 * and will be removed.
 *
 * The callback is run on a separate thread, and for short timeouts can
 * potentially be called before this function returns.
 *
 * Timers take into account the amount of time it took to execute the callback.
 * For example, if the callback took 250 ns to execute and returned 1000 (ns),
 * the timer would only wait another 750 ns before its next iteration.
 *
 * Timing may be inexact due to OS scheduling. Be sure to note the current time
 * with GetTicksNS() or GetPerformanceCounter() in case your callback needs to
 * adjust for variances.
 *
 * @param interval the timer delay, in std::chrono::nanoseconds, passed to
 * `callback`.
 * @param callback the TimerCB function to call when the specified
 *                 `interval` elapses.
 * @returns a timer ID.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @cat listener-callback
 *
 * @sa listener-callback
 * @sa AddTimer(std::chrono::milliseconds, MSTimerCallback, void*)
 * @sa AddTimer(std::chrono::nanoseconds, NSTimerCallback, void*)
 * @sa RemoveTimer()
 */
inline TimerID AddTimer(std::chrono::nanoseconds interval, TimerCB callback) {
  return SDL_AddTimerNS(interval.count(), callback.wrapper, callback.data);
}

/**
 * Remove a timer created with AddTimer().
 *
 * @param id the ID of the timer to remove.
 * @throws Error on failure.
 *
 * @threadsafety It is safe to call this function from any thread.
 *
 * @since This function is available since SDL 3.2.0.
 *
 * @sa AddTimer
 */
inline void RemoveTimer(TimerID id) { CheckError(SDL_RemoveTimer(id)); }

/**
 * @brief Manages per-frame timing and measures real FPS over a 1-second window.
 *
 * Typical usage:
 * @code
 * SDL::FrameTimer timer;               // default 60 FPS target
 * while (running) {
 *   timer.Begin();
 *   // update / render ...
 *   timer.End();                       // sleeps if frame was too fast
 *   float delta = timer.GetDelta();    // use for physics / animation
 *   float fps   = timer.GetFPS();      // measured FPS (updated every second)
 * }
 * @endcode
 *
 * @sa SetTargetFPS
 * @sa GetDelta
 * @sa GetFPS
 */
class FrameTimer {
  Uint64 m_frameStart     = 0;    ///< ns – timestamp of the current Begin()
  Uint64 m_prevBeginTime  = 0;    ///< ns – timestamp of the previous Begin()
  Uint64 m_computeTime    = 0;    ///< ns – computation time (Begin→End)
  float  m_delta          = 0.f;  ///< seconds between consecutive Begin() calls
  float  m_targetFPS      = 60.f; ///< target frames per second
  float  m_time           = 0.f;  ///< counted time in second
  float  m_fps            = 0.f;  ///< measured FPS (1-second window)
  Uint32 m_fpsFrameCount  = 0;    ///< frames counted in the current window
  Uint64 m_fpsWindowStart = 0;    ///< ns – start of the current measurement window

public:
  /**
   * @brief Construct a FrameTimer with an optional target FPS.
   *
   * @param targetFPS desired frames per second (default 60).
   */
  explicit FrameTimer(float targetFPS = 60.f)
    : m_targetFPS(targetFPS) {
  }

  /**
   * @brief Mark the beginning of a frame.
   *
   * Call once at the top of your main loop. On the very first call GetDelta()
   * returns `1/targetFPS` as a sensible default; the FPS measurement window is
   * also initialised here.
   */
  void Begin() {
    Uint64 now = GetTicksNS();

    if (m_prevBeginTime > 0) {
      m_delta = float(now - m_prevBeginTime) * 1e-9f;

      // Accumulate frames; refresh the measured FPS every second.
      ++m_fpsFrameCount;
      if (now - m_fpsWindowStart >= 1'000'000'000ULL) {
        m_fps =
          float(m_fpsFrameCount) / (float(now - m_fpsWindowStart) * 1e-9f);
        m_fpsWindowStart = now;
        m_fpsFrameCount  = 0;
      }
    } else {
      // First call: initialise the measurement window.
      m_fpsWindowStart = now;
      m_delta          = 1.f / m_targetFPS;
    }

    m_time         += m_delta;
    m_prevBeginTime = now;
    m_frameStart    = now;
  }

  /**
   * @brief Mark the end of a frame.
   *
   * Computes the computation time (Begin→End). If the frame finished faster
   * than the target rate, the thread is suspended with nanosecond precision
   * (via DelayPrecise()) for the remaining budget.
   */
  void End() {
    Uint64 now    = GetTicksNS();
    m_computeTime = now - m_frameStart;

    Uint64 targetNS = Uint64(1'000'000'000.0 / double(m_targetFPS));
    if (m_computeTime < targetNS) {
      DelayPrecise(targetNS - m_computeTime);
    }
  }

  /**
   * @brief Return the computation time of the last frame (Begin→End), in seconds.
   *
   * This excludes any sleep added by End(). Useful for profiling how long the
   * actual work took.
   *
   * @returns elapsed computation time in seconds.
   */
  float GetTimer() const { return float(m_computeTime) * 1e-9f; }

  /**
   * @brief Return the current time elapsed from the first frame
   * 
   * @returns elapsed time in seconds.
   */
  float GetTime() const { return m_time; }

  /**
   * @brief Return the total duration of the last frame (including sleep), in seconds.
   *
   * This is the time between two consecutive Begin() calls and is what you
   * should use to advance physics or animations.
   *
   * @returns delta time in seconds.
   */
  float GetDelta() const { return m_delta; }

  /**
   * @brief Set the target frames per second.
   *
   * @param tfps desired frame rate (default 60).
   */
  void SetTargetFPS(float tfps = 60.f) { m_targetFPS = tfps; }

  /**
   * @brief Return the target frames per second.
   *
   * @returns target FPS as set by SetTargetFPS() or the constructor.
   */
  float GetTargetFPS() const { return m_targetFPS; }

  /**
   * @brief Return the measured FPS averaged over the last second.
   *
   * The value is updated once per second inside Begin(). Returns 0 until the
   * first full measurement window has elapsed.
   *
   * @returns measured FPS.
   */
  float GetFPS() const { return m_fps; }

  /**
   * @brief Reset all timer state.
   *
   * Clears all counters, timestamps, and the measured FPS. The target FPS set
   * by SetTargetFPS() (or the constructor) is preserved.
   */
  void Reset() {
    m_frameStart     = 0;
    m_prevBeginTime  = 0;
    m_computeTime    = 0;
    m_delta          = 0.f;
    m_fps            = 0.f;
    m_time           = 0.f;
    m_fpsFrameCount  = 0;
    m_fpsWindowStart = 0;
  }
};

/// @}

} // namespace SDL

#endif /* SDL3PP_TIMER_H_ */
