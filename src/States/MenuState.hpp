#pragma once
#include "IState.hpp"
#include "../Core/SaveManager.hpp"
#include "../Logger/Logger.hpp"
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3/SDL.h>
#include <cmath>

namespace game {

// ─────────────────────────────────────────────────────────────────────────────
// MenuState — main menu
//
// UI hierarchy (SDL3pp Builder DSL):
//   Column "root" {
//     Canvas "background"   h=180  (animated bg + title)
//     Column "buttons"  w=360  (centered)
//       [Nouvelle Partie] [Continuer] [Options] [Quitter]
//   }
//   (* disabled)
// ─────────────────────────────────────────────────────────────────────────────

class MenuState : public IState {
public:
	void Enter(AppContext& ctx) override {
		m_ctx = &ctx; m_time = 0.f;
		LOG_INFO << "MenuState::Enter";

		// Check whether a save file is available.
		m_hasSave = !ctx.savePath.empty() &&
					core::SaveManager(ctx.savePath).Exists();

		m_uiWorld = std::make_unique<SDL::ECS::World>();
		m_ui      = std::make_unique<SDL::UI::System>(
			*m_uiWorld, ctx.renderer, SDL::MixerRef{nullptr}, *ctx.pool);
		_BuildUI();
	}

	void Leave() override {
		m_ui.reset();
		m_uiWorld.reset();
		LOG_INFO << "MenuState::Leave";
	}

	bool HandleEvent(const SDL::Event& ev) override {
		if (ev.type == SDL::EVENT_QUIT) { m_ctx->quit(); return false; }
		if (m_ui) m_ui->ProcessEvent(reinterpret_cast<const SDL::Event&>(ev));
		return true;
	}

	void Update(float dt) override { m_time += dt; }

	void Render(float dt) override {
		if (m_ui) m_ui->Iterate(dt);
	}

private:
	AppContext* m_ctx     = nullptr;
	float       m_time   = 0.f;
	bool        m_hasSave = false;
	std::unique_ptr<SDL::ECS::World>  m_uiWorld;
	std::unique_ptr<SDL::UI::System>  m_ui;

	void _BuildUI() {
		using namespace SDL::UI;

		constexpr SDL::Color kBg    = {12,  14,  22,  255};
		constexpr SDL::Color kAcc   = {60, 140, 220, 255};
		constexpr SDL::Color kAccH  = {85, 165, 245, 255};
		constexpr SDL::Color kAccP  = {40, 110, 190, 255};
		constexpr SDL::Color kTxt   = {235,235,245, 255};
		constexpr SDL::Color kDanger= {145, 35, 35, 255};

		m_ui->LoadFont("DejaVuSans", m_ctx->assetsBasePath + "fonts/DejaVuSans.ttf");
		m_ui->SetDefaultFont("DejaVuSans", 16.f);

		// ── Background canvas ─────────────────────────────────────────────────────
		auto background = m_ui->CanvasWidget("background", nullptr, nullptr,
			[this, kAcc](SDL::RendererRef r, SDL::FRect rect) {
				_DrawBackground(r, rect, kAcc);
			})
			.Attach(AttachLayout::Absolute)
			.W(SDL::UI::Value::Ww(100.f))
			.H(SDL::UI::Value::Wh(100.0f));

		// ── Button factory ────────────────────────────────────────────────────
		auto mkBtn = [&](const char* id, const char* lbl,
						 SDL::Color bg, SDL::Color bgh, SDL::Color bgp,
						 std::function<void()> cb) {
			return m_ui->Button(id, lbl)
				.BgColor(bg).BgHover(bgh).BgPress(bgp)
				.BorderColor(kAcc)
				.TextColor(kTxt)
				.FontSize(16.f)
				.Radius(SDL::FCorners(6.f))
				.H(44.f).MarginV(5.f)
				.OnClick(std::move(cb));
		};

		auto bNew  = mkBtn("bNew",  "Nouvelle Partie",
							kAcc, kAccH, kAccP, [this]{ _StartGame(false); });
		// "Continuer" is enabled only when a save file exists.
		auto bCont = mkBtn("bCont", m_hasSave ? "Continuer" : "Continuer (N/A)",
							m_hasSave ? SDL::Color{35,40,55,255} : SDL::Color{25,28,38,255},
							m_hasSave ? SDL::Color{50,55,75,255} : SDL::Color{25,28,38,255},
							m_hasSave ? SDL::Color{20,25,40,255} : SDL::Color{25,28,38,255},
							[this]{ if (m_hasSave) _StartGame(true); })
						.Enable(m_hasSave);
		auto bOpts = mkBtn("bOpts", "Options",
							{35,40,55,255}, {50,55,75,255}, {20,25,40,255}, []{})
						.Enable(false);
		auto bQuit = mkBtn("bQuit", "Quitter",
							kDanger, {175,50,45,255}, {100,25,25,255},
							[this]{ m_ctx->quit(); });

		// ── Button column (centered, 360px wide) ──────────────────────────────
		auto btnCol = m_ui->Column("buttons", 6.f, 12.f)
			.W(360.f)
			.Align(Align::Center, Align::Center)
			.BgColor({0,0,0,0})
			.MarginV(18.f)
			.Children(bNew, bCont, m_ui->Sep(), bOpts, bQuit);

		// ── Root ──────────────────────────────────────────────────────────────
		m_ui->Column("root", 0.f, 0.f)
			.W(Value::Ww(100.f)).H(Value::Wh(100.f))
			.BgColor(kBg)
			.Children(background, btnCol)
			.AsRoot();
	}

	void _DrawBackground(SDL::RendererRef r, SDL::FRect rect, SDL::Color acc) {
		// Gradient background
		for (int i = 0; i < (int)rect.h; ++i) {
			float t  = (float)i / rect.h;
			r.SetDrawColorFloat(SDL::FColor{t*0.3f, t*0.3f, t*0.6f, 1.f});
			r.RenderFillRect(SDL::FRect{rect.x, rect.y + rect.h * t, rect.w, rect.h / 12.f});
		}

		// Shimmer band
		float lx = rect.x + rect.w * (0.5f + 0.5f * SDL::Sin(m_time * 0.4f));
		r.SetDrawColor({acc.r, acc.g, acc.b, 70});
		r.RenderFillRect(SDL::FRect{lx - 80.f, rect.y, 160.f, rect.h});

		// Bottom border
		r.SetDrawColor({acc.r, acc.g, acc.b, 220});
		r.RenderFillRect(SDL::FRect{rect.x, rect.y + rect.h - 2.f, rect.w, 2.f});

		// Title (4×)
		float pct  = 0.5f + 0.5f * SDL::Sin(m_time * 0.5f);
		r.SetDrawColor({(uint8_t)(180 + 75 * pct),
						(uint8_t)(180 + 75 * SDL::Sin(m_time*0.5f+2.09f)),
						255, 255});
		const char* title = "POLYADVENTURE";
		float tw = SDL::Strlen(title) * 8.f * 4.f;
		r.SetScale({4.f, 4.f});
		r.RenderDebugText({(rect.x + rect.w * 0.5f - tw * 0.5f) / 4.f,
						   (rect.y + rect.h * 0.38f - 16.f) / 4.f}, title);
		r.SetScale({1.f, 1.f});

		// Subtitle (1.5×)
		r.SetDrawColor({160, 165, 200, 200});
		const char* sub = "An SDL3pp C++20 RPG";
		float sw = SDL::Strlen(sub) * 8.f * 1.5f;
		r.SetScale({1.5f, 1.5f});
		r.RenderDebugText({(rect.x + rect.w * 0.5f - sw * 0.5f) / 1.5f,
						   (rect.y + rect.h * 0.72f - 6.f) / 1.5f}, sub);
		r.SetScale({1.f, 1.f});
	}

	// Defined in main.cpp (requires GameState to be complete).
	// loadSave=true: GameState will load the save file on Enter().
	// loadSave=false: GameState starts fresh (save file is NOT deleted here).
	void _StartGame(bool loadSave = false);
};

} // namespace game
