// ─────────────────────────────────────────────────────────────────────────────
// Arcade SDL3pp — 5 jeux rétro : 2048, Tetris, Snake, Sokoban, Démineur
// Architecture : ECS (SDL3pp_ecs) + UI ECS (SDL3pp_ui) + interface IGame
// ─────────────────────────────────────────────────────────────────────────────
#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include "Common.h"
#include "Game2048.h"
#include "GameTetris.h"
#include "GameSnake.h"
#include "GameSokoban.h"
#include "GameMinesweeper.h"

#include <array>
#include <format>
#include <memory>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Palette UI
// ─────────────────────────────────────────────────────────────────────────────

static constexpr SDL::Color BG       = {20,  20,  25,  255};
static constexpr SDL::Color PANEL_BG = {30,  30,  38,  255};
static constexpr SDL::Color PANEL_BD = {56,  56,  71,  255};
static constexpr SDL::Color ACCENT   = {71,  140, 242, 255};
static constexpr SDL::Color ACCENT_H = {97,  165, 255, 255};
static constexpr SDL::Color WHITE    = {255, 255, 255, 255};
static constexpr SDL::Color GREY     = {140, 140, 153, 255};

static constexpr SDL::Color GAME_COL[5] = {
    {242, 193,  45, 255},   // 2048   — or
    {  0, 216, 242, 255},   // Tetris — cyan
    { 51, 204,  51, 255},   // Snake  — vert
    {229, 140,  25, 255},   // Sokoban — orange
    {204,  51,  51, 255},   // Démineur — rouge
};

// ─────────────────────────────────────────────────────────────────────────────
// Main
// ─────────────────────────────────────────────────────────────────────────────

struct Main {
    static constexpr SDL::Point kWinSize = {1280, 800};

    SDL::Window          window    { SDL::CreateWindowAndRenderer("SDL3pp - Arcade Mini Jeux", kWinSize, SDL::WINDOW_RESIZABLE, nullptr) };
    SDL::RendererRef     renderer  { window.GetRenderer() };
    SDL::FrameTimer      timer     { 60.f };
    SDL::Mixer           mixer     { SDL::CreateMixer(SDL::AudioSpec{SDL::AUDIO_S32, 2, 44100}) };
    SDL::ResourceManager resources;
    SDL::ResourcePool&   uiPool    { *resources.CreatePool("ui") };
    SDL::ECS::Context    ecs;
    SDL::UI::System      ui        { ecs, renderer, mixer, uiPool };

    // Instances des jeux (propriété exclusive)
    std::array<std::unique_ptr<IGame>, 5> m_games = {
        std::make_unique<Game2048>(4),
        std::make_unique<GameTetris>(),
        std::make_unique<GameSnake>(),
        std::make_unique<GameSokoban>(),
        std::make_unique<GameMinesweeper>(),
    };
    IGame* m_current = nullptr;

    // IDs ECS des widgets UI
    SDL::ECS::EntityId rootId    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId menuId    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId gameId    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId scoreId   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId canvasId  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId sizeLblId = SDL::ECS::NullEntity;
    SDL::ECS::EntityId gtitleId  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId ghdrId    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId sizeRowId = SDL::ECS::NullEntity;

    // ── Init ─────────────────────────────────────────────────────────────────

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::LogPriority prio = SDL::LOG_PRIORITY_WARN;
        for (auto arg : args) {
            if (arg == "--verbose") prio = SDL::LOG_PRIORITY_INFO;
            else if (arg == "--debug") prio = SDL::LOG_PRIORITY_DEBUG;
        }
        SDL::SetLogPriorities(prio);
        SDL::SetAppMetadata("SDL3pp Arcade", "1.0.0", "Arcade de mini-jeux rétro SDL3pp");
        SDL::Init(SDL::INIT_VIDEO | SDL::INIT_AUDIO | SDL::INIT_GAMEPAD);
        SDL::TTF::Init();
        SDL::MIX::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }

    static void Quit(Main* self, SDL::AppResult) {
        delete self;
        SDL::MIX::Quit();
        SDL::TTF::Quit();
        SDL::Quit();
    }

    Main() {
        window.StartTextInput();
        BuildUI();
    }
    ~Main() { uiPool.Release(); }

    // ── Construction de l'UI ─────────────────────────────────────────────────

    void BuildUI() {
        using V = SDL::UI::Value;

        // ── Racine ────────────────────────────────────────────────────────────
        auto root = ui.Container("root")
            .W(V::Ww(100.f)).H(V::Wh(100.f))
            .BgColor(BG)
            .Layout(SDL::UI::Layout::Stack)
            .AsRoot();
        rootId = root;

        // ── Header fixe ───────────────────────────────────────────────────────
        auto header = ui.Container("header")
            .W(V::Ww(100.f)).H(44.f)
            .BgColor({25, 25, 33, 255})
            .BorderBottom(1.f).BdColor(PANEL_BD)
            .Attach(SDL::UI::AttachLayout::Absolute)
            .Y(0.f).X(0.f)
            .Layout(SDL::UI::Layout::InLine)
            .Padding(8.f, 0.f);

        ui.Label("title",    "Arcade")
            .TextColor(ACCENT).FontSize(22.f)
            .AlignV(SDL::UI::Align::Center)
            .AttachTo(header);
        ui.Label("subtitle", "Mini Jeux SDL3pp")
            .TextColor(GREY).FontSize(12.f)
            .AlignV(SDL::UI::Align::Center).MarginLeft(8.f)
            .AttachTo(header);
        header.AttachTo(root);

        // ── Écran menu ────────────────────────────────────────────────────────
        auto menuScreen = ui.Container("menu")
            .W(V::Ww(100.f)).H(V::Wh(100.f)-44.f)
            .Y(44.f).X(0.f)
            .Attach(SDL::UI::AttachLayout::Absolute)
            .Layout(SDL::UI::Layout::InColumn)
            .AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .BgColor(BG);

        ui.Label("menu-title", "Arcade - Choisissez un jeu")
            .TextColor(WHITE).FontSize(28.f)
            .Margin(SDL::FBox(0.f, 28.f, 0.f, 10.f))
            .AttachTo(menuScreen);

        ui.Label("menu-sub", "Fleches/WASD jouer  R=reset  Echap=menu")
            .TextColor(GREY).FontSize(12.f)
            .Margin(SDL::FBox(0.f, 0.f, 0.f, 24.f))
            .AttachTo(menuScreen);

        // Grille de cartes
        auto grid = ui.Container("grid")
            .W(V::Ww(100.f)).H(V::Auto())
            .Layout(SDL::UI::Layout::InLine)
            .AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .Padding(8.f)
            .AttachTo(menuScreen);

        for (int i = 0; i < 5; ++i) {
            SDL::Color col = GAME_COL[i];
            auto card = ui.Container("card-" + std::to_string(i))
                .W(220.f).H(100.f)
                .BgColor(PANEL_BG).BgHoveredColor({46, 46, 56, 255})
                .Radius(SDL::FCorners(8.f))
                .Borders({1.f, 1.f, 1.f, 1.f}).BdColor(PANEL_BD).BdHoveredColor(col)
                .Margin(10.f).Padding(SDL::FBox(16.f, 8.f, 8.f, 8.f))
                .Layout(SDL::UI::Layout::InColumn);

            // Barre couleur gauche
            ui.Container("bar-" + std::to_string(i))
                .W(8.f).H(V::Ph(100.f))
                .BgColor(col)
                .Radius(SDL::FCorners{8.f, 0.f, 8.f, 0.f})
                .Attach(SDL::UI::AttachLayout::Absolute)
                .X(0.f).Y(0.f)
                .AttachTo(card);

            ui.Label("cname-" + std::to_string(i), m_games[i]->Title())
                .TextColor(WHITE).FontSize(16.f)
                .AttachTo(card);
            ui.Label("cdesc-" + std::to_string(i), m_games[i]->Description())
                .TextColor(GREY).FontSize(10.f)
                .Padding(SDL::FBox(8.f, 8.f, 0.f, 8.f))
                .AttachTo(card);

            card.OnClick([this, i] { LaunchGame(i); }).AttachTo(grid);
        }

        menuId = menuScreen;
        menuScreen.AttachTo(root);

        // ── Écran jeu (caché au départ) ───────────────────────────────────────
        auto gameScreen = ui.Container("game")
            .W(V::Ww(100.f)).H(V::Wh(100.f)-44.f)
            .Y(44.f).X(0.f)
            .Attach(SDL::UI::AttachLayout::Absolute)
            .Layout(SDL::UI::Layout::InColumn)
            .BgColor(BG)
            .SetVisible(false);

        // Barre de jeu
        auto gHeader = ui.Container("ghdr")
            .W(V::Ww(100.f)).H(44.f)
            .BgColor({25, 25, 33, 255})
            .BorderBottom(1.f)
            .Layout(SDL::UI::Layout::InLine);
        ghdrId = gHeader;

        ui.Button("back", "<  Menu")
            .BgColor({56, 56, 71, 255}).BgHoveredColor({76, 76, 96, 255})
            .TextColor(WHITE).Radius(SDL::FCorners(6.f))
            .Padding(SDL::FBox(6.f, 4.f, 6.f, 4.f)).Margin(8.f)
            .AlignV(SDL::UI::Align::Center)
            .OnClick([this] { ReturnToMenu(); })
            .AttachTo(gHeader);

        gtitleId = ui.Label("gtitle", "")
            .TextColor(WHITE).FontSize(16.f)
            .AlignH(SDL::UI::Align::Center).AlignV(SDL::UI::Align::Center)
            .GrowW(1.f)
            .Id();
        ui.GetBuilder(gtitleId).AttachTo(gHeader);

        scoreId = ui.Label("gscore", "Score: 0")
            .TextColor(WHITE).FontSize(12.f)
            .AlignV(SDL::UI::Align::Center).MarginRight(16.f)
            .Id();
        ui.GetBuilder(scoreId).AttachTo(gHeader);
        gHeader.AttachTo(gameScreen);

        // Canvas de rendu du jeu
        canvasId = ui.Canvas("gcanvas",
            [this](SDL::Event& ev) {
                if (m_current) m_current->OnEvent(ev);
            },
            [this](float dt) {
                if (!m_current) return;
                m_current->Update(dt);
                ui.SetText(scoreId, std::format("Score: {}", m_current->Score()));
            },
            [this](SDL::RendererRef r, SDL::FRect rect) {
                if (m_current) m_current->Render(r, rect);
            }
        )
        .W(V::Ww(100.f)).GrowH(1.f)
        .BgColor(BG)
        .Hoverable().Selectable().Focusable()
        .Id();
        ui.GetBuilder(canvasId).AttachTo(gameScreen);

        // Contrôles 2048 — taille de grille (cachée sauf pour le jeu 2048)
        auto sizeRow = ui.Container("size-row")
            .W(V::Ww()).H(28.f)
            .BgColor({25, 25, 33, 255})
            .Layout(SDL::UI::Layout::InLine)
            .AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .SetVisible(false);

        sizeLblId = ui.Label("size-lbl", "4x4")
            .TextColor(WHITE).FontSize(12.f)
            .Margin(SDL::FBox(4.f, 0.f, 4.f, 0.f))
            .AlignV(SDL::UI::Align::Center)
            .Id();
        ui.GetBuilder(sizeLblId).AttachTo(sizeRow);

        for (int s = 3; s <= 8; ++s) {
            std::string str = std::format("{}x{}", s, s);
            ui.Button("sz-" + str, str)
                .BgColor(s == 4 ? ACCENT : PANEL_BG).BgHoveredColor(ACCENT_H)
                .TextColor(WHITE).Radius(SDL::FCorners(5.f))
                .Padding(SDL::FBox(5.f, 3.f, 5.f, 3.f)).Margin(3.f).FontSize(11.f)
                .OnClick([this, s] {
                    auto* g = static_cast<Game2048*>(m_games[0].get());
                    g->SetGridSize(s);
                    g->Reset();
                    ui.SetText(sizeLblId, std::format("{}x{}", s, s));
                })
                .AttachTo(sizeRow);
        }

        sizeRowId = sizeRow;
        sizeRow.AttachTo(gameScreen);
        gameId = gameScreen;
        gameScreen.AttachTo(root);
    }

    // ── Gestion des jeux ──────────────────────────────────────────────────────

    void LaunchGame(int idx) {
        m_current = m_games[idx].get();
        SDL::Color col = GAME_COL[idx];

        if (ecs.IsAlive(gtitleId))
            ui.SetText(gtitleId, m_current->Title());
        if (ecs.IsAlive(gtitleId))
            ui.GetStyle(gtitleId).textColor = col;
        if (ecs.IsAlive(ghdrId))
            ui.GetStyle(ghdrId).bdColor = col;
        if (ecs.IsAlive(sizeRowId))
            ui.SetVisible(sizeRowId, idx == 0);

        m_current->Start();

        ui.SetVisible(menuId, false);
        ui.SetVisible(gameId, true);
    }

    void ReturnToMenu() {
        if (m_current) {
            m_current->Pause();
            m_current = nullptr;
        }
        ui.SetVisible(menuId, true);
        ui.SetVisible(gameId, false);
    }

    // ── Boucle principale ─────────────────────────────────────────────────────

    SDL::AppResult Iterate() {
        timer.Begin();
        const float dt = timer.GetDelta();
        uiPool.Update();
        renderer.SetDrawColor({0, 0, 0, 255});
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        timer.End();
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        if (ev.type == SDL::EVENT_KEY_DOWN) {
            if ((ev.key.mod & SDL::KMOD_CTRL) && ev.key.key == SDL::KEYCODE_Q)
                return SDL::APP_SUCCESS;
            if (ev.key.key == SDL::KEYCODE_ESCAPE && m_current)
                { ReturnToMenu(); return SDL::APP_CONTINUE; }
        }
        ui.ProcessEvent(ev);
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
