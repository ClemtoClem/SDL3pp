/*
 * Snake Game ECS & Scene Graph Implementation.
 * Includes Main Menu, Settings, Game Over panels, JSON saving, and Level System.
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_scene.h>
#include <SDL3pp/SDL3pp_ui.h>
#include <SDL3pp/SDL3pp_dataScripts.h>
#include <SDL3pp/SDL3pp_mixer.h>

#include <vector>
#include <string>
#include <list>

using namespace std::literals;

// =============================================================================
// Map Generation (48x48 grids)
// 0: Empty, 1: Wall, 2: Spawn
// =============================================================================
using MapGrid = std::vector<std::string>;

static constexpr int MAP_W = 48;
static constexpr int MAP_H = 48;

static MapGrid MakeEmpty() { return MapGrid(MAP_H, std::string(MAP_W, '0')); }

static void Border(MapGrid& m) {
    for (int x = 0; x < MAP_W; ++x) { m[0][x] = '1'; m[MAP_H-1][x] = '1'; }
    for (int y = 0; y < MAP_H; ++y) { m[y][0] = '1'; m[y][MAP_W-1] = '1'; }
}

static void HLine(MapGrid& m, int y, int x1, int x2) {
    for (int x = x1; x <= x2; ++x) m[y][x] = '1';
}

static void VLine(MapGrid& m, int x, int y1, int y2) {
    for (int y = y1; y <= y2; ++y) m[y][x] = '1';
}

static void Spawn(MapGrid& m, int x, int y) { m[y][x] = '2'; }

static std::vector<MapGrid> GenerateMaps() {
    std::vector<MapGrid> maps;

    // Level 0: Open field
    {
        auto m = MakeEmpty();
        Spawn(m, 23, 23); Spawn(m, 24, 23); Spawn(m, 23, 24); Spawn(m, 24, 24);
        maps.push_back(std::move(m));
    }

    // Level 1: Bordered arena
    {
        auto m = MakeEmpty();
        Border(m);
        Spawn(m, 23, 23); Spawn(m, 24, 23); Spawn(m, 23, 24); Spawn(m, 24, 24);
        maps.push_back(std::move(m));
    }

    // Level 2: Four rooms with passages
    {
        auto m = MakeEmpty();
        Border(m);
        HLine(m, 23, 1, 20);
        HLine(m, 23, 27, 46);
        VLine(m, 23, 1, 20);
        VLine(m, 23, 27, 46);
        Spawn(m, 10, 11); Spawn(m, 35, 11); Spawn(m, 10, 35); Spawn(m, 35, 35);
        maps.push_back(std::move(m));
    }

    // Level 3: Cross walls dividing the arena
    {
        auto m = MakeEmpty();
        Border(m);
        HLine(m, 16, 1, 20); HLine(m, 16, 27, 46);
        HLine(m, 31, 1, 20); HLine(m, 31, 27, 46);
        VLine(m, 16, 1, 15); VLine(m, 16, 32, 46);
        VLine(m, 31, 1, 15); VLine(m, 31, 32, 46);
        Spawn(m, 23, 8); Spawn(m, 23, 39); Spawn(m, 8, 23); Spawn(m, 39, 23);
        maps.push_back(std::move(m));
    }

    // Level 4: Zigzag corridors
    {
        auto m = MakeEmpty();
        Border(m);
        for (int row = 0; row < 5; ++row) {
            int y = 8 + row * 8;
            if (row % 2 == 0) HLine(m, y, 1, 38);
            else               HLine(m, y, 9, 46);
        }
        Spawn(m, 42, 4); Spawn(m, 4, 4);
        maps.push_back(std::move(m));
    }

    // Level 5: Concentric rectangles
    {
        auto m = MakeEmpty();
        Border(m);
        HLine(m, 8, 8, 39); HLine(m, 39, 8, 39);
        VLine(m, 8, 8, 18); VLine(m, 8, 29, 39);
        VLine(m, 39, 8, 18); VLine(m, 39, 29, 39);
        HLine(m, 16, 16, 31); HLine(m, 31, 16, 31);
        VLine(m, 16, 16, 21); VLine(m, 16, 26, 31);
        VLine(m, 31, 16, 21); VLine(m, 31, 26, 31);
        Spawn(m, 23, 23); Spawn(m, 24, 23); Spawn(m, 23, 24); Spawn(m, 24, 24);
        maps.push_back(std::move(m));
    }

    // Level 6: Pillar columns
    {
        auto m = MakeEmpty();
        Border(m);
        for (int px = 8; px <= 40; px += 8) {
            VLine(m, px, 4, 20);
            VLine(m, px, 27, 43);
        }
        Spawn(m, 4, 23); Spawn(m, 44, 23);
        maps.push_back(std::move(m));
    }

    // Level 7: Checkerboard pillar blocks
    {
        auto m = MakeEmpty();
        Border(m);
        for (int py = 6; py <= 42; py += 6)
            for (int px = 6; px <= 42; px += 6)
                if (((px / 6) + (py / 6)) % 2 == 0) {
                    HLine(m, py,     px, px + 2);
                    HLine(m, py + 2, px, px + 2);
                    VLine(m, px,     py, py + 2);
                    VLine(m, px + 2, py, py + 2);
                }
        Spawn(m, 23, 23); Spawn(m, 24, 23);
        maps.push_back(std::move(m));
    }

    // Level 8: Labyrinth of horizontal walls
    {
        auto m = MakeEmpty();
        Border(m);
        for (int row = 0; row < 6; ++row) {
            int y = 7 + row * 6;
            if (row % 2 == 0) { HLine(m, y, 1, 40); HLine(m, y+1, 1, 40); }
            else               { HLine(m, y, 7, 46); HLine(m, y+1, 7, 46); }
        }
        Spawn(m, 44, 4); Spawn(m, 3, 4);
        maps.push_back(std::move(m));
    }

    // Level 9: Dense spiral/maze
    {
        auto m = MakeEmpty();
        Border(m);
        HLine(m,  4,  4, 43); HLine(m,  5,  4, 43);
        HLine(m, 42,  4, 43); HLine(m, 43,  4, 43);
        VLine(m,  4,  4, 43); VLine(m,  5,  4, 43);
        VLine(m, 42,  4, 43); VLine(m, 43,  4, 43);
        HLine(m, 12, 12, 35); HLine(m, 35, 12, 35);
        VLine(m, 12, 12, 23); VLine(m, 35, 24, 35);
        HLine(m, 22, 20, 27); HLine(m, 25, 20, 27);
        VLine(m, 20, 22, 25); VLine(m, 27, 22, 25);
        Spawn(m, 23, 8); Spawn(m, 23, 39); Spawn(m, 8, 23); Spawn(m, 39, 23);
        maps.push_back(std::move(m));
    }

    return maps;
}

const std::vector<MapGrid> MAPS = GenerateMaps();

// =============================================================================
// State & Components
// =============================================================================
enum class GameState { MENU, SETTINGS, PLAYING, GAMEOVER };
enum class Direction { UP, DOWN, LEFT, RIGHT };

struct Config {
    int speed_ms  = 125;
    int difficulty = 50;
    int high_score = 0;
};

struct GridPos { int x, y; };
struct SnakeTag {};
struct WallTag {};
struct FoodTag {};

// =============================================================================
// Main Application
// =============================================================================
struct Main {
	static constexpr SDL::Point kWinSz = {1280, 760};

    static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::SetAppMetadata("Snake ECS", "1.0", "com.example.SnakeECS");
        SDL::Init(SDL::INIT_VIDEO);
        SDL::TTF::Init();
        SDL::MIX::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }

    static void Quit(Main* m, SDL::AppResult) {
        delete m;
        SDL::MIX::Quit();
        SDL::TTF::Quit();
        SDL::Quit();
    }

    SDL::Window window{"SDL3pp - Snake ECS & UI", kWinSz, SDL::WINDOW_RESIZABLE};
    SDL::Renderer renderer{window};
    SDL::ResourceManager rm;
    SDL::ResourcePool* pool = rm.CreatePool("main");
    SDL::ECS::Context ecs_context;
    SDL::UI::System ui{ecs_context, renderer, {}, *pool};
    SDL::ECS::SceneBuilder scene{ecs_context, renderer};
    std::string assetsPath;

    Config config;
    GameState state = GameState::MENU;

    // Audio
    SDL::Mixer mixer{SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK, SDL::AudioSpec{SDL::AUDIO_F32, 2, 44100}};
    SDL::Track musicTrack{mixer};
    SDL::Track sfxTrack{mixer};

    // UI Entity IDs
    SDL::ECS::EntityId uiMenu, uiSettings, uiGameover, uiHud;
    SDL::ECS::EntityId lblScore, lblHudScore, lblHighScore, lblFinalScore, lblSettingsParams;

    // Scene Graph IDs & Game Data
    SDL::ECS::EntityId sceneRoot = SDL::ECS::NullEntity;
    std::list<SDL::ECS::EntityId> snake;
    std::vector<SDL::ECS::EntityId> walls;
    SDL::ECS::EntityId food = SDL::ECS::NullEntity;

    Direction current_dir = Direction::RIGHT;
    Direction next_dir    = Direction::RIGHT;
    Uint64 last_step_time = 0;

    int score      = 0;
    int level      = 0;
    int block_size = 12;

    // -------------------------------------------------------------------------
    // Constructor
    // -------------------------------------------------------------------------
    Main() {
        assetsPath = std::string(SDL::GetBasePath()) + "../../../assets/";

        pool->Add<SDL::Font>("font", SDL::Font(assetsPath + "fonts/DejaVuSans.ttf", 16));
        pool->Add<SDL::Texture>("snake", CreateSolidTexture(50, 200, 50));
        pool->Add<SDL::Texture>("food",  CreateSolidTexture(250, 50, 50));
        pool->Add<SDL::Texture>("wall",  SDL::LoadTexture(renderer, assetsPath + "textures/crate.png"));

        ui.SetDefaultFont("font", 16.f);

        sceneRoot = scene.Node2D("Context").Id();
        scene.SetRoot(sceneRoot);

        LoadAudio();
        LoadConfig();
        BuildUI();
        ChangeState(GameState::MENU);
    }

    // -------------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------------
    void LoadAudio() {
        try {
            pool->Add<SDL::Audio>("music-menu", mixer.LoadAudio(assetsPath + "sounds/game-music-3.mp3", false));
            pool->Add<SDL::Audio>("music-game", mixer.LoadAudio(assetsPath + "sounds/game-music-4.mp3", false));
            pool->Add<SDL::Audio>("sfx-win",    mixer.LoadAudio(assetsPath + "sounds/effect-win.ogg",   true));
            pool->Add<SDL::Audio>("sfx-fail",   mixer.LoadAudio(assetsPath + "sounds/effect-fail.mp3",  true));
        } catch (...) {}
    }

    void PlayMusic(const std::string& key) {
        SDL::ResourceHandle<SDL::Audio> audio = pool->Get<SDL::Audio>(key);
        if (!audio) return;
        musicTrack.SetAudio(*audio);
        SDL::Properties props = SDL::CreateProperties();
        props.SetNumberProperty(SDL::prop::Play::LOOPS_NUMBER, -1);
        musicTrack.Play(props);
    }

    void PlaySFX(const std::string& key) {
        SDL::ResourceHandle<SDL::Audio> audio = pool->Get<SDL::Audio>(key);
        if (!audio) return;
        sfxTrack.SetAudio(*audio);
        sfxTrack.Play();
    }

    // -------------------------------------------------------------------------
    // Config Persistence (JSON)
    // -------------------------------------------------------------------------
    void LoadConfig() {
        SDL::IOStream io;
        try { io = SDL::IOStream::FromFile(std::string(SDL::GetBasePath()) + "../../../data/snake_config.json", "r"); }
        catch (...) { return; }
        auto doc = SDL::ParseDataScript(io, "json");
        if (doc && doc->getRoot()) {
            auto r = doc->getRoot();
            auto getInt = [&](const std::string& k, int& out) {
                if (r->has(k)) {
                    auto node = r->get(k);
                    if (node->getType() == SDL::DataNodeType::S32) out = std::dynamic_pointer_cast<SDL::S32DataNode>(node)->getValue();
                    if (node->getType() == SDL::DataNodeType::S64) out = std::dynamic_pointer_cast<SDL::S64DataNode>(node)->getValue();
                }
            };
            getInt("high_score", config.high_score);
            getInt("speed_ms",   config.speed_ms);
            getInt("difficulty", config.difficulty);
        }
    }

    void SaveConfig() {
        auto doc  = SDL::DataScriptFactory::instance().createByName("json");
        auto root = SDL::ObjectDataNode::Make();
        root->set("high_score", SDL::S32DataNode::Make(config.high_score));
        root->set("speed_ms",   SDL::S32DataNode::Make(config.speed_ms));
        root->set("difficulty", SDL::S32DataNode::Make(config.difficulty));
        doc->setRoot(root);
        auto io = SDL::IOStream::FromFile(std::string(SDL::GetBasePath()) + "../../../data/snake_config.json", "w");
        doc->encode(io);
    }

    // -------------------------------------------------------------------------
    // Utilities
    // -------------------------------------------------------------------------
    SDL::Texture CreateSolidTexture(Uint8 r, Uint8 g, Uint8 b) {
        SDL::Surface surf = SDL::CreateSurface({24, 24}, SDL::PIXELFORMAT_RGBA32);
        surf.FillRect(std::nullopt, surf.MapRGBA({r, g, b, 255}));
        return SDL::Texture(renderer, surf);
    }

    void ChangeState(GameState s) {
        state = s;
        ui.SetVisible(uiMenu,     s == GameState::MENU);
        ui.SetVisible(uiSettings, s == GameState::SETTINGS);
        ui.SetVisible(uiGameover, s == GameState::GAMEOVER);
        ui.SetVisible(uiHud,      s == GameState::PLAYING);

        if (s == GameState::PLAYING) {
            score = 0;
            level = 0;
            LoadLevel();
            ui.SetText(lblHudScore,  "Score : 0");
            ui.SetText(lblHighScore, "Best : " + std::to_string(config.high_score));
            last_step_time = SDL::GetTicksMS();
            PlayMusic("music-game");
        }

        if (s == GameState::MENU) {
            ui.SetText(lblSettingsParams,
                "Speed: " + std::to_string(config.speed_ms) + "ms"
                " | Diff: " + std::to_string(config.difficulty));
            PlayMusic("music-menu");
        }

        if (s == GameState::SETTINGS) {
            ui.SetText(lblSettingsParams,
                "Speed: " + std::to_string(config.speed_ms) + "ms"
                " | Diff: " + std::to_string(config.difficulty));
        }

        if (s == GameState::GAMEOVER) {
            musicTrack.Stop(15);
            PlaySFX("sfx-fail");
        }
    }

    // -------------------------------------------------------------------------
    // UI Construction
    // -------------------------------------------------------------------------
    void BuildUI() {
        SDL::UI::Theme::ApplyDark(ui);

        auto rootUI = ui.Container("UIRoot")
            .W(SDL::UI::Value::Ww(100)).H(SDL::UI::Value::Wh(100)).AsRoot();

        // 1. MAIN MENU
        uiMenu = ui.Column("Menu").AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .Align(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .W(300).Gap(15).AttachTo(rootUI);

        auto mkBtn = [&](const std::string& name, const std::string& label, const SDL::UI::Style& style, std::function<void()> onClick) -> SDL::UI::Builder {
            return ui.Button(name, label).W(200).H(45)
                .Style(style).OnClick(onClick)
                .Font("font", 16.f);
        };

        ui.Label("Title", "SNAKE").FontSize(40).TextColor({50, 200, 100, 255}).MarginBottom(20).AttachTo(uiMenu).AlignH(SDL::UI::Align::Center);
        lblSettingsParams = ui.Label("Params", "").TextColor({150, 150, 150, 255}).AttachTo(uiMenu);
        mkBtn("BtnPlay",     "JOUER",      SDL::UI::Theme::SuccessButton(), [this]{ ChangeState(GameState::PLAYING); }).AttachTo(uiMenu);
        mkBtn("BtnSettings", "PARAMETRES", SDL::UI::Theme::PrimaryButton(), [this]{ ChangeState(GameState::SETTINGS); }).AttachTo(uiMenu);
        mkBtn("BtnQuit",     "QUITTER",    SDL::UI::Theme::DangerButton(),  []{ SDL::Event e{}; e.type = SDL::EVENT_QUIT; SDL::PushEvent(e); }).AttachTo(uiMenu);

        // 2. SETTINGS (speed and difficulty only -- grid is fixed 48x48)
        uiSettings = ui.Column("Settings").AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .Align(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .W(400).Gap(15).Visible(false).AttachTo(rootUI);

        ui.Label("TitleSet", "PARAMETRES").FontSize(32).MarginBottom(20).AttachTo(uiSettings);

        auto rowSpeed = ui.Row("RSpeed").W(SDL::UI::Value::Pw(100)).AttachTo(uiSettings);
        auto lblSpd = ui.Label("LblSpd", std::format("Vitesse : {} ms", config.speed_ms)).W(150).AttachTo(rowSpeed);
        ui.Slider("SldSpd", 50.f, 400.f, (float)config.speed_ms).Grow(100.f)
            .OnChange([this, lblSpd](float v){ config.speed_ms = (int)v; ui.SetText(lblSpd, std::format("Vitesse : {} ms", config.speed_ms)); })
            .AttachTo(rowSpeed);

        auto rowD = ui.Row("RD").W(SDL::UI::Value::Pw(100)).AttachTo(uiSettings);
        auto lblD = ui.Label("LblD", std::format("Niveau tous les : {}", config.difficulty)).W(150).AttachTo(rowD);
        ui.Slider("SldD", 10.f, 200.f, (float)config.difficulty).Grow(100.f)
            .OnChange([this, lblD](float v){ config.difficulty = (int)v; ui.SetText(lblD, std::format("Niveau tous les : {}", config.difficulty)); })
            .AttachTo(rowD);

        ui.Button("BtnBack", "SAUVEGARDER & RETOUR").W(250).H(45).MarginTop(20)
            .OnClick([this]{ SaveConfig(); ChangeState(GameState::MENU); }).AttachTo(uiSettings);

        // 3. GAME OVER
        uiGameover = ui.Column("GameOver").AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .Align(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .W(300).Gap(15).Visible(false).AttachTo(rootUI);

        ui.Label("GOTitle", "GAME OVER").FontSize(48).TextColor({250, 60, 60, 255}).MarginBottom(10).AttachTo(uiGameover);
        lblFinalScore = ui.Label("FinalScore", "Score : 0").FontSize(24).AttachTo(uiGameover);
        ui.Button("BtnRetry", "REJOUER").W(200).H(45).Style(SDL::UI::Theme::SuccessButton())
            .OnClick([this]{ ChangeState(GameState::PLAYING); }).AttachTo(uiGameover);
        ui.Button("BtnMnu", "MENU PRINCIPAL").W(200).H(45).Style(SDL::UI::Theme::PrimaryButton())
            .OnClick([this]{ ChangeState(GameState::MENU); }).AttachTo(uiGameover);

        // 4. HUD IN GAME
        uiHud = ui.Row("HUD").Absolute(10, 10).Gap(20).Visible(false).AttachTo(rootUI);
        lblHudScore  = ui.Label("HudS", "Score : 0").FontSize(20).TextColor({255, 255, 255, 255}).AttachTo(uiHud);
        lblHighScore = ui.Label("HudH", "Best : 0").FontSize(20).TextColor({200, 200, 200, 255}).AttachTo(uiHud);
    }

    // -------------------------------------------------------------------------
    // Level & Game Logic
    // -------------------------------------------------------------------------
    void SpawnFood() {
        if (ecs_context.IsAlive(food)) scene.DestroyNode(food);

        std::vector<GridPos> free_cells;
        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                bool occupied = false;
                for (auto w : walls) {
                    auto gp = ecs_context.Get<GridPos>(w);
                    if (gp && gp->x == x && gp->y == y) { occupied = true; break; }
                }
                for (auto s : snake) {
                    auto gp = ecs_context.Get<GridPos>(s);
                    if (gp && gp->x == x && gp->y == y) { occupied = true; break; }
                }
                if (!occupied) free_cells.push_back({x, y});
            }
        }
        if (free_cells.empty()) return;

        auto pos = free_cells[SDL::Rand((Sint32)free_cells.size())];
        food = scene.Sprite2D("Food", *pool->Get<SDL::Texture>("food"))
                .Position(pos.x * block_size + block_size/2.f, pos.y * block_size + block_size/2.f)
                .AttachTo(sceneRoot).Id();
        ecs_context.Add<GridPos>(food, pos);
        ecs_context.Add<FoodTag>(food);
    }

    void LoadLevel() {
        for (auto e : walls) scene.DestroyNode(e);
        walls.clear();
        for (auto e : snake) scene.DestroyNode(e);
        snake.clear();

        current_dir = Direction::RIGHT;
        next_dir    = Direction::RIGHT;

        SDL::Point size = kWinSz;
        window.GetSize(&size.x, &size.y);
        block_size = std::min(size.x / MAP_W, size.y / MAP_H);
        scene.GetGraph().SetCamera({
            {MAP_W * block_size / 2.f, MAP_H * block_size / 2.f},
            1.f, size.x, size.y
        });

        int map_idx = level % (int)MAPS.size();
        const auto& blueprint = MAPS[map_idx];

        std::vector<GridPos> spawns;

        for (int y = 0; y < MAP_H; ++y) {
            for (int x = 0; x < MAP_W; ++x) {
                char c = blueprint[y][x];
                if (c == '1') {
                    auto w = scene.Sprite2D("Wall", *pool->Get<SDL::Texture>("wall"))
                        .Position(x * block_size + block_size/2.f, y * block_size + block_size/2.f)
                        .Scale((float)block_size / 24.f)
                        .AttachTo(sceneRoot).Id();
                    ecs_context.Add<GridPos>(w, {x, y});
                    ecs_context.Add<WallTag>(w);
                    walls.push_back(w);
                } else if (c == '2') {
                    spawns.push_back({x, y});
                }
            }
        }

        GridPos spawn = {MAP_W / 2, MAP_H / 2};
        if (!spawns.empty())
            spawn = spawns[SDL::Rand((Sint32)spawns.size())];

        for (int i = 0; i < 3; ++i) {
            auto s = scene.Sprite2D("SnakeBody", *pool->Get<SDL::Texture>("snake"))
                .Position((spawn.x - i) * block_size + block_size/2.f, spawn.y * block_size + block_size/2.f)
                .Scale((float)block_size / 24.f)
                .AttachTo(sceneRoot).Id();
            ecs_context.Add<GridPos>(s, {spawn.x - i, spawn.y});
            ecs_context.Add<SnakeTag>(s);
            snake.push_back(s);
        }

        SpawnFood();
    }

    void Step() {
        current_dir = next_dir;
        auto head_pos = *ecs_context.Get<GridPos>(snake.front());
        GridPos next_pos = head_pos;

        switch (current_dir) {
            case Direction::UP:    next_pos.y--; break;
            case Direction::DOWN:  next_pos.y++; break;
            case Direction::LEFT:  next_pos.x--; break;
            case Direction::RIGHT: next_pos.x++; break;
        }

        // Wrap around
        if (next_pos.x < 0)      next_pos.x = MAP_W - 1;
        if (next_pos.x >= MAP_W) next_pos.x = 0;
        if (next_pos.y < 0)      next_pos.y = MAP_H - 1;
        if (next_pos.y >= MAP_H) next_pos.y = 0;

        // Collision detection
        bool collision = false;
        for (auto w : walls) {
            auto gp = ecs_context.Get<GridPos>(w);
            if (gp && gp->x == next_pos.x && gp->y == next_pos.y) collision = true;
        }
        for (auto s : snake) {
            auto gp = ecs_context.Get<GridPos>(s);
            if (gp && gp->x == next_pos.x && gp->y == next_pos.y && s != snake.back()) collision = true;
        }

        if (collision) {
            ui.SetText(lblFinalScore, "Score : " + std::to_string(score) + " (Map " + std::to_string(level) + ")");
            if (score > config.high_score) {
                config.high_score = score;
                SaveConfig();
            }
            ChangeState(GameState::GAMEOVER);
            return;
        }

        auto new_head = scene.Sprite2D("SnakeBody", *pool->Get<SDL::Texture>("snake"))
                .Position(next_pos.x * block_size + block_size/2.f, next_pos.y * block_size + block_size/2.f)
                .Scale((float)block_size / 24.f)
                .AttachTo(sceneRoot).Id();
        ecs_context.Add<GridPos>(new_head, next_pos);
        ecs_context.Add<SnakeTag>(new_head);
        snake.push_front(new_head);

        auto food_gp = ecs_context.Get<GridPos>(food);
        if (food_gp && food_gp->x == next_pos.x && food_gp->y == next_pos.y) {
            score += 10;
            ui.SetText(lblHudScore, "Score : " + std::to_string(score));

            if (score > 0 && score % config.difficulty == 0) {
                level++;
                PlaySFX("sfx-win");
                LoadLevel();
                return;
            }
            SpawnFood();
        } else {
            auto tail = snake.back();
            scene.DestroyNode(tail);
            snake.pop_back();
        }
    }

    // -------------------------------------------------------------------------
    // Application Lifecycle
    // -------------------------------------------------------------------------
    SDL::AppResult Iterate() {
        Uint64 now = SDL::GetTicksMS();
        float dt = 0.016f;

        if (state == GameState::PLAYING) {
            if (now - last_step_time >= (uint64_t)config.speed_ms) {
                Step();
                last_step_time = now;
            }
        }

        scene.Update(dt);

        renderer.SetDrawColor({25, 25, 30, 255});
        renderer.RenderClear();

        if (state == GameState::PLAYING)
            scene.Render();

        ui.Iterate(dt);
        renderer.Present();

        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& e) {
        ui.ProcessEvent(e);
        scene.DispatchInput(e);

        if (state == GameState::PLAYING && e.type == SDL::EVENT_KEY_DOWN) {
            auto key = e.key.scancode;
            if (key == SDL::SCANCODE_UP    && current_dir != Direction::DOWN)  next_dir = Direction::UP;
            if (key == SDL::SCANCODE_DOWN  && current_dir != Direction::UP)    next_dir = Direction::DOWN;
            if (key == SDL::SCANCODE_LEFT  && current_dir != Direction::RIGHT) next_dir = Direction::LEFT;
            if (key == SDL::SCANCODE_RIGHT && current_dir != Direction::LEFT)  next_dir = Direction::RIGHT;
        }

        if (e.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
