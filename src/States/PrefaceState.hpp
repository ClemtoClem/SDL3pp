#pragma once
#include "IState.hpp"
#include "../Logger/Logger.hpp"
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3/SDL.h>
#include <cmath>

namespace game {

// ─────────────────────────────────────────────────────────────────────────────
// PrefaceState — animated opening screen
//
//  • Starfield + color-cycling title drawn via a full-screen UI Canvas
//  • Transitions to MenuState on SPACE / Enter / click / 8-second timeout
//  • _GoToMenu() is defined in main.cpp once all state types are complete
// ─────────────────────────────────────────────────────────────────────────────

class PrefaceState : public IState {
public:
	void Enter(AppContext& ctx) override {
		m_ctx = &ctx;
		m_time = 0.f;
		LOG_INFO << "PrefaceState::Enter";

		m_ecs = std::make_unique<SDL::ECS::Context>();
		m_ui      = std::make_unique<SDL::UI::System>(
			*m_ecs, ctx.renderer, SDL::MixerRef{nullptr}, *ctx.pool);

		m_ui->LoadFont("DejaVuSans", m_ctx->assetsBasePath + "fonts/DejaVuSans.ttf");
		m_ui->SetDefaultFont("DejaVuSans", 16.f);

		// Full-screen canvas — drives all drawing
		m_ui->CanvasWidget("canvas", nullptr, nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect){ _Draw(r, rect); })
			.W(SDL::UI::Value::Ww(100.f))
			.H(SDL::UI::Value::Wh(100.f))
			.AsRoot();
	}

	void Leave() override {
		m_ui.reset();
		m_ecs.reset();
		LOG_INFO << "PrefaceState::Leave";
	}

	bool HandleEvent(const SDL::Event& ev) override {
		if (m_ui) m_ui->ProcessEvent(reinterpret_cast<const SDL::Event&>(ev));

		if (ev.type == SDL::EVENT_QUIT) {
			m_ctx->quit(); 
			return false;
		} else if (ev.type == SDL::EVENT_KEY_DOWN) {
			switch (ev.key.key) {
				case SDL::KEYCODE_SPACE:
				case SDL::KEYCODE_RETURN:
				case SDL::KEYCODE_KP_ENTER:
					_GoToMenu(); return true;
				default: break;
			}
		} else if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN) {
			_GoToMenu();
		}
		return true;
	}

	void Update(float dt) override {
		m_time += dt;
		if (m_time > 8.f) _GoToMenu();
	}

	void Render(float dt) override {
		if (m_ui) m_ui->Iterate(dt);
	}

private:
	AppContext*  m_ctx  = nullptr;
	float        m_time = 0.f;
	std::unique_ptr<SDL::ECS::Context>  m_ecs;
	std::unique_ptr<SDL::UI::System>  m_ui;

	// ── Animated canvas drawing ───────────────────────────────────────────────
	void _Draw(SDL::RendererRef r, SDL::FRect rect) {
		// Gradient background
		for (int i = 0; i < (int)rect.h; ++i) {
			float t  = (float)i / rect.h;
			r.SetDrawColorFloat(SDL::FColor{t*0.3f, t*0.3f, t*0.6f, 1.f});
			r.RenderFillRect(SDL::FRect{rect.x, rect.y + rect.h * t, rect.w, rect.h / 12.f});
		}

		// Stars
		for (int i = 0; i < 80; ++i) {
			float blink = 0.5f + 0.5f * SDL::Sin(m_time * 1.3f + i * 0.7f);
			r.SetDrawColor({220, 220, 255, SDL::Clamp8(blink * 200)});
			r.RenderFillRect(SDL::FRect{
				rect.x + rect.w * ((i * 137 + 17) % 100 / 100.f),
				rect.y + rect.h * ((i * 241 +  7) % 100 / 100.f),
				2.f, 2.f});
		}

		// Color-cycling title
		float th = m_time * 0.6f;
		r.SetDrawColor({
			SDL::Clamp8(128 + 127 * SDL::Sin(th)),
			SDL::Clamp8(128 + 127 * SDL::Sin(th + 2.09f)),
			SDL::Clamp8(128 + 127 * SDL::Sin(th + 4.19f)), 255 });
		_Text(r, "POLYADVENTURE", rect.x + rect.w*0.5f, rect.y + rect.h*0.38f, 4.f);

		r.SetDrawColor({170, 170, 200, 180});
		_Text(r, "An SDL3pp RPG",  rect.x + rect.w*0.5f, rect.y + rect.h*0.52f, 2.f);

		if ((int)(m_time * 1.6f) % 2 == 0) {
			r.SetDrawColor({200, 200, 200, 230});
			_Text(r, "Press SPACE to start",
				  rect.x + rect.w*0.5f, rect.y + rect.h*0.68f, 1.5f);
		}

		// Fade-in vignette
		float fade = SDL::Clamp(1.f - m_time / 0.8f, 0.f, 1.f);
		if (fade > 0.02f) {
			r.SetDrawColor({0, 0, 0, (uint8_t)(fade * 255)});
			r.RenderFillRect(rect);
		}
	}

	void _Text(SDL::RendererRef r, const char* s, float cx, float cy, float sc) {
		float w = SDL::Strlen(s) * 8.f * sc;
		r.SetScale({sc, sc});
		r.RenderDebugText({(cx - w * 0.5f) / sc, (cy - 4.f * sc) / sc}, s);
		r.SetScale({1.f, 1.f});
	}

	// Defined in main.cpp (requires MenuState to be complete)
	void _GoToMenu();
};

} // namespace game
