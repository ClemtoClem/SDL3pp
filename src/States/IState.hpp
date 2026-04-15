#pragma once
#include <SDL3pp/SDL3pp.h>
#include "../Core/ScriptParser.hpp"
#include "../Core/SaveManager.hpp"
#include <functional>
#include <memory>
#include <string>

namespace game {

struct IState;

// ─────────────────────────────────────────────────────────────────────────────
// AppContext — shared dependencies injected into every state via Enter()
// ─────────────────────────────────────────────────────────────────────────────

struct AppContext {
    SDL::RendererRef     renderer;
    SDL::ResourcePool*   pool        = nullptr;   // pointer – never null in practice
    core::ScriptSectionPtr config;
    std::string          assetsBasePath;
    std::string          savePath;                // full path to the save file

    // Deferred state switch (applied at the start of the next frame)
    std::function<void(std::unique_ptr<IState>)> switchState;
    // Request clean shutdown
    std::function<void()> quit;
};

// ─────────────────────────────────────────────────────────────────────────────
// IState — abstract game state
// ─────────────────────────────────────────────────────────────────────────────

struct IState {
    virtual ~IState() = default;

    /// Called once when the state becomes active.  `ctx` outlives the state.
    virtual void Enter(AppContext& ctx) = 0;

    /// Called once when this state is replaced by another.
    virtual void Leave() = 0;

    /// Process one SDL event.  Return false to request immediate quit.
    virtual bool HandleEvent(const SDL::Event& ev) = 0;

    /// Game-logic update (called every frame, before Render).
    virtual void Update(float dt) = 0;

    /// Rendering pass — calls ui.Iterate(dt) internally.
    virtual void Render(float dt) = 0;
};

} // namespace game
