#ifndef SDL3PP_ENGINE_SIGNAL_SYSTEM_H_
#define SDL3PP_ENGINE_SIGNAL_SYSTEM_H_

/**
 * @file Systems/SignalSystem.h
 * @brief Decoupled signal/slot bus for scene events.
 *
 * Signals are addressed by a `(node, signal)` name pair, letting gameplay code
 * react to events ("Player::died", "Door::opened") without holding direct
 * references. Multiple handlers may connect to the same signal and are invoked
 * in connection order.
 *
 * The `ProcessSystem` builds on this to *launch processes* in response to
 * signals (see `Systems/ProcessSystem.h`).
 *
 * ```cpp
 * SDL::ECS::SignalBus bus;
 * bus.Connect("Player", "died", []{ SDL::Log("game over"); });
 * bus.Emit("Player", "died");
 * ```
 */

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace SDL {
namespace ECS {

/**
 * @addtogroup CategoryEngineSystems
 * @{
 */

/// A lightweight signal/slot bus keyed on `(node, signal)` names.
class SignalBus {
public:
	using Handler = std::function<void()>;

	/// Connect a handler to `(node, signal)`. Returns an id usable for removal.
	size_t Connect(const std::string& node, const std::string& signal, Handler h) {
		auto& slot = m_slots[Key(node, signal)];
		slot.push_back({m_nextId, std::move(h), false});
		return m_nextId++;
	}

	/// Connect a handler that disconnects itself after firing once.
	size_t ConnectOnce(const std::string& node, const std::string& signal, Handler h) {
		auto& slot = m_slots[Key(node, signal)];
		slot.push_back({m_nextId, std::move(h), true});
		return m_nextId++;
	}

	/// Emit `(node, signal)`: call every connected handler, then drop one-shots.
	void Emit(const std::string& node, const std::string& signal) {
		auto it = m_slots.find(Key(node, signal));
		if (it == m_slots.end()) return;
		auto handlers = it->second; // copy: a handler may mutate the bus
		for (const auto& c : handlers) if (c.fn) c.fn();
		std::erase_if(it->second, [](const Connection& c) { return c.once; });
	}

	/// Remove every handler for `(node, signal)`.
	void Disconnect(const std::string& node, const std::string& signal) {
		m_slots.erase(Key(node, signal));
	}

	/// Remove a single handler by the id returned from `Connect`.
	void DisconnectId(size_t id) {
		for (auto& [key, slot] : m_slots)
			std::erase_if(slot, [id](const Connection& c) { return c.id == id; });
	}

	void Clear() { m_slots.clear(); }

private:
	struct Connection { size_t id; Handler fn; bool once; };

	static std::string Key(const std::string& node, const std::string& signal) {
		return node + "::" + signal;
	}

	std::unordered_map<std::string, std::vector<Connection>> m_slots;
	size_t m_nextId = 1;
};

/** @} */ // CategoryEngineSystems

} // namespace ECS
} // namespace SDL

#endif // SDL3PP_ENGINE_SIGNAL_SYSTEM_H_
