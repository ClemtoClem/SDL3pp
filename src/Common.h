#pragma once

#include <SDL3pp/SDL3pp.h>
#include <functional>
#include <string_view>
#include <unordered_set>

// std::hash for SDL::Keycode (wraps SDL_Keycode/uint32)
namespace std {
template<>
struct hash<SDL::Keycode> {
    size_t operator()(SDL::Keycode k) const noexcept {
        return std::hash<SDL::KeycodeRaw>{}(static_cast<SDL::KeycodeRaw>(k));
    }
};
}

// ─────────────────────────────────────────────────────────────────────────────
// Drawing helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float CH = (float)SDL::DEBUG_TEXT_FONT_CHARACTER_SIZE;

inline void DrawText(SDL::RendererRef r, float x, float y, float sz, std::string_view text) {
    if (text.empty() || sz <= 0.f) return;
    const float scale = sz / CH;
    SDL::FPoint s = r.GetScale();
    r.SetScale(s * scale);
    r.RenderDebugText({x / scale, y / scale}, text);
    r.SetScale(s);
}

inline SDL::FPoint MeasText(std::string_view text, float sz) {
    return {(float)text.size() * sz, sz};
}

inline void DrawTextC(SDL::RendererRef r, SDL::FPoint center, float sz, std::string_view text) {
    SDL::FPoint ts = MeasText(text, sz);
    DrawText(r, center.x - ts.x * 0.5f, center.y - ts.y * 0.5f, sz, text);
}

// ─────────────────────────────────────────────────────────────────────────────
// InputState — keyboard + mouse tracking
// ─────────────────────────────────────────────────────────────────────────────

struct InputState {
    std::unordered_set<SDL::Keycode> held, justDown;
    float mx = 0, my = 0;
    Uint32 mouseJust = 0;
    Uint32 mouseHeld = 0;
    bool   wantsEscape = false;
    bool   wantsReturn = false;

    void Feed(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_KEY_DOWN && !ev.key.repeat) {
            held.insert(ev.key.key);
            justDown.insert(ev.key.key);
            if (ev.key.key == SDL::KEYCODE_ESCAPE) wantsEscape = true;
            if (ev.key.key == SDL::KEYCODE_RETURN || ev.key.key == SDL::KEYCODE_SPACE) wantsReturn = true;
        }
        if (ev.type == SDL::EVENT_KEY_UP) held.erase(ev.key.key);
        if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN) {
            mouseJust |= SDL::ButtonMask(ev.button.button);
            mouseHeld |= SDL::ButtonMask(ev.button.button);
        }
        if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP)  mouseHeld &= ~SDL::ButtonMask(ev.button.button);
        if (ev.type == SDL::EVENT_MOUSE_MOTION) { mx = ev.motion.x; my = ev.motion.y; }
    }

    void Flush() {
        justDown.clear(); mouseJust = 0;
        wantsEscape = false; wantsReturn = false;
    }

    bool JP(SDL::Keycode k)    const { return justDown.count(k) > 0; }
    bool Down(SDL::Keycode k)  const { return held.count(k) > 0; }
    bool MouseJP(int btn)      const { return (mouseJust & SDL::ButtonMask(btn)) != 0; }
    bool MouseDown(int btn)    const { return (mouseHeld & SDL::ButtonMask(btn)) != 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// IGame — interface commune à tous les mini-jeux
// ─────────────────────────────────────────────────────────────────────────────

struct IGame {
    virtual ~IGame() = default;

    virtual const char* Title()       const = 0;
    virtual const char* Description() const = 0;
    virtual int         Score()       const = 0;

    virtual void Start()   = 0;
    virtual void Pause()   = 0;
    virtual void Reset()   = 0;

    virtual void OnEvent(const SDL::Event& ev)                    = 0;
    virtual void Update  (float dt)                               = 0;
    virtual void Render  (SDL::RendererRef r, SDL::FRect rect)    = 0;
};
