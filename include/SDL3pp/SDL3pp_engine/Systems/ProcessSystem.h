#ifndef SDL3PP_ENGINE_PROCESS_SYSTEM_H_
#define SDL3PP_ENGINE_PROCESS_SYSTEM_H_

/**
 * @file Systems/ProcessSystem.h
 * @brief Cooperative, multi-frame processes (coroutine-like tasks) and a
 *        manager that can launch them in response to signals.
 *
 * A `Process` runs across many frames: it is initialised once, updated every
 * frame until it succeeds/fails/aborts, then its success-chained child (if any)
 * is promoted to run next. This is ideal for sequenced game logic — "fade out,
 * then load level, then fade in", timers, tweened cutscenes, AI steps, etc.
 *
 * The `ProcessManager` integrates with `SignalBus` so emitting a signal can
 * *launch a process*:
 *
 * ```cpp
 * SDL::ECS::ProcessManager pm;
 * SDL::ECS::SignalBus      bus;
 *
 * // Spawn a 2-second shake whenever the player is hit.
 * pm.LaunchOnSignal(bus, "Player", "hit", [] {
 *     return SDL::ECS::Wait(2.f);
 * });
 *
 * // Per frame:
 * pm.Update(dt);
 * ```
 */

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "SignalSystem.h"

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/**
 * Base class for a cooperative, multi-frame process.
 *
 * Override `OnUpdate` (required) and optionally the lifecycle hooks. Drive the
 * outcome with `Succeed()` / `Fail()`; pause with `Pause()` / `Resume()`.
 */
class Process : public std::enable_shared_from_this<Process> {
public:
	enum class State { Uninitialized, Running, Paused, Succeeded, Failed, Aborted };

	virtual ~Process() = default;

	// ── Lifecycle hooks (override as needed) ─────────────────────────────────
	virtual void OnInit()            { m_state = State::Running; }
	virtual void OnUpdate(float dt)  = 0;
	virtual void OnSuccess()         {}
	virtual void OnFail()            {}
	virtual void OnAbort()           {}

	// ── Outcome control ──────────────────────────────────────────────────────
	void Succeed() { m_state = State::Succeeded; }
	void Fail()    { m_state = State::Failed; }
	void Pause()   { if (m_state == State::Running) m_state = State::Paused; }
	void Resume()  { if (m_state == State::Paused)  m_state = State::Running; }

	[[nodiscard]] State GetState() const noexcept { return m_state; }
	[[nodiscard]] bool  IsAlive()  const noexcept { return m_state == State::Running || m_state == State::Paused; }
	[[nodiscard]] bool  IsDead()   const noexcept {
		return m_state == State::Succeeded || m_state == State::Failed || m_state == State::Aborted;
	}

	/// Run `child` once this process succeeds. Returns `child` for chaining.
	std::shared_ptr<Process> Then(std::shared_ptr<Process> child) {
		m_child = std::move(child);
		return m_child;
	}

	/// Detach and return the success-chained child (used by the manager).
	std::shared_ptr<Process> ReleaseChild() noexcept { return std::move(m_child); }
	[[nodiscard]] bool HasChild() const noexcept { return static_cast<bool>(m_child); }

private:
	friend class ProcessManager;
	State                    m_state = State::Uninitialized;
	std::shared_ptr<Process> m_child;
};

/**
 * Lambda-driven process: supply an update callback (and optional hooks) instead
 * of subclassing. The callback receives the process so it can `Succeed()`/`Fail()`.
 */
class DelegateProcess : public Process {
public:
	using UpdateFn = std::function<void(DelegateProcess&, float)>;
	using VoidFn   = std::function<void()>;

	explicit DelegateProcess(UpdateFn update, VoidFn onInit = {},
													 VoidFn onSuccess = {}, VoidFn onFail = {})
		: m_update(std::move(update)), m_onInit(std::move(onInit)),
			m_onSuccess(std::move(onSuccess)), m_onFail(std::move(onFail)) {}

	void OnInit()           override { Process::OnInit(); if (m_onInit) m_onInit(); }
	void OnUpdate(float dt) override { if (m_update) m_update(*this, dt); }
	void OnSuccess()        override { if (m_onSuccess) m_onSuccess(); }
	void OnFail()           override { if (m_onFail) m_onFail(); }

private:
	UpdateFn m_update;
	VoidFn   m_onInit, m_onSuccess, m_onFail;
};

/// Process that succeeds after `seconds` have elapsed.
class WaitProcess : public Process {
public:
	explicit WaitProcess(float seconds) : m_remaining(seconds) {}
	void OnUpdate(float dt) override {
		m_remaining -= dt;
		if (m_remaining <= 0.f) Succeed();
	}
private:
	float m_remaining;
};

/// Build a delay process (factory helper).
[[nodiscard]] inline std::shared_ptr<Process> Wait(float seconds) {
	return std::make_shared<WaitProcess>(seconds);
}

/// Build a process from an update lambda `void(DelegateProcess&, float)`.
[[nodiscard]] inline std::shared_ptr<Process> MakeProcess(DelegateProcess::UpdateFn update) {
	return std::make_shared<DelegateProcess>(std::move(update));
}

/// Build a process that runs `fn` once and immediately succeeds.
[[nodiscard]] inline std::shared_ptr<Process> Call(std::function<void()> fn) {
	return std::make_shared<DelegateProcess>(
		[f = std::move(fn)](DelegateProcess& p, float) { if (f) f(); p.Succeed(); });
}

/**
 * Owns and ticks a set of live processes.
 *
 * On each `Update`, dead processes are reaped: a *succeeded* process promotes
 * its chained child (`Then`) to keep running; failed/aborted processes drop
 * their chain.
 */
class ProcessManager {
public:
	/// Add a process; it is initialised on the next `Update`. Returns a weak handle.
	std::weak_ptr<Process> Attach(std::shared_ptr<Process> p) {
		m_processes.push_back(std::move(p));
		return m_processes.back();
	}

	/// Tick all live processes and reap finished ones.
	void Update(float dt) {
		for (size_t i = 0; i < m_processes.size();) {
			std::shared_ptr<Process> p = m_processes[i];

			if (p->GetState() == Process::State::Uninitialized) p->OnInit();
			if (p->GetState() == Process::State::Running)       p->OnUpdate(dt);

			if (p->IsDead()) {
				switch (p->GetState()) {
				case Process::State::Succeeded:
					p->OnSuccess();
					if (auto child = p->ReleaseChild()) m_processes.push_back(std::move(child));
					break;
				case Process::State::Failed:  p->OnFail();  break;
				case Process::State::Aborted: p->OnAbort(); break;
				default: break;
				}
				m_processes.erase(m_processes.begin() + static_cast<long>(i));
			} else {
				++i;
			}
		}
	}

	/// Abort and drop every process.
	void AbortAll() {
		for (auto& p : m_processes) { p->m_state = Process::State::Aborted; p->OnAbort(); }
		m_processes.clear();
	}

	[[nodiscard]] size_t Count() const noexcept { return m_processes.size(); }

	/**
	 * Launch a freshly built process every time `(node, signal)` is emitted.
	 *
	 * @param bus     the signal bus to listen on.
	 * @param node    signal node name.
	 * @param signal  signal name.
	 * @param factory builds the process to attach when the signal fires.
	 * @returns the bus connection id (for `SignalBus::DisconnectId`).
	 */
	size_t LaunchOnSignal(SignalBus& bus, const std::string& node, const std::string& signal,
												std::function<std::shared_ptr<Process>()> factory) {
		return bus.Connect(node, signal, [this, factory = std::move(factory)] {
			if (factory) if (auto p = factory()) Attach(std::move(p));
		});
	}

private:
	std::vector<std::shared_ptr<Process>> m_processes;
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_PROCESS_SYSTEM_H_
