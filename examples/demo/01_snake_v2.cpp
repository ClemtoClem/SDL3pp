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

#include <vector>
#include <string>
#include <list>

using namespace std::literals;

// =============================================================================
// Map Blueprints
// 0: Vide, 1: Mur, 2: Spawn
// =============================================================================
const std::vector<std::vector<std::string>> RAW_MAPS = {
    { // Level 0: Empty (Just a spawn in the middle)
        "0000000000",
        "0000000000",
        "0000000000",
        "0000220000",
        "0000220000",
        "0000000000",
        "0000000000",
        "0000000000"
    },
    { // Level 1: Borders
        "1111111111",
        "1000000001",
        "1000000001",
        "1000220001",
        "1000220001",
        "1000000001",
        "1000000001",
        "1111111111"
    },
    { // Level 2: The Tunnel (multiple spawns)
        "1111111111",
        "1000000001",
        "1000000001",
        "1011111101",
        "1000000001",
        "1020000201",
        "1000000001",
        "1111111111"
    },
    { // Level 3: Cross
        "1111001111",
        "1000000001",
        "1000000001",
        "1001111001",
        "1020110201",
        "1000000001",
        "1000000001",
        "1111001111"
    }
};

// =============================================================================
// State & Components
// =============================================================================
enum class GameState { MENU, SETTINGS, PLAYING, GAMEOVER };
enum class Direction { UP, DOWN, LEFT, RIGHT };

struct Config {
    int speed_ms = 125;
    int grid_w = 24;
    int grid_h = 18;
    int difficulty = 50; // Points needed to advance to next map
    int high_score = 0;
};

// ECS Components for Game Logic
struct GridPos { int x, y; };
struct SnakeTag {};
struct WallTag {};
struct FoodTag {};

// =============================================================================
// Main Application
// =============================================================================
struct Main {
	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
        SDL::SetAppMetadata("Snake ECS", "1.0", "com.example.SnakeECS");
        SDL::Init(SDL::INIT_VIDEO);
		SDL::TTF::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
		SDL::TTF::Quit();
		SDL::Quit();
	}

    SDL::Window window{"SDL3pp - Snake ECS & UI", {800, 600}};
    SDL::Renderer renderer{window};
    SDL::ResourceManager rm;
    SDL::ResourcePool* pool = rm.CreatePool("main");
    SDL::ECS::Context ecs_context;
    SDL::UI::System ui{ecs_context, renderer, {}, *pool};
    SDL::ECS::SceneBuilder scene{ecs_context, renderer};
	std::string assetsPath;

    Config config;
    GameState state = GameState::MENU;

    // UI Entity IDs
    SDL::ECS::EntityId uiMenu, uiSettings, uiGameover, uiHud;
    SDL::ECS::EntityId lblScore, lblHudScore, lblHighScore, lblFinalScore, lblSettingsParams;

    // Scene Graph IDs & Game Data
    SDL::ECS::EntityId sceneRoot = SDL::ECS::NullEntity;
    std::list<SDL::ECS::EntityId> snake;
    std::vector<SDL::ECS::EntityId> walls;
    SDL::ECS::EntityId food = SDL::ECS::NullEntity;

    Direction current_dir = Direction::RIGHT;
    Direction next_dir = Direction::RIGHT;
    Uint64 last_step_time = 0;

    int score = 0;
    int level = 0;
    int block_size = 24;

	// -------------------------------------------------------------------------
	// Constuctor & Destructor
	// -------------------------------------------------------------------------
	Main() {
		assetsPath = std::string(SDL::GetBasePath()) + "../../../assets/";
		
		pool->Add<SDL::Font>("default_font", SDL::Font(assetsPath + "fonts/DejaVuSans.ttf", 16));
        pool->Add<SDL::Texture>("snake", CreateSolidTexture(50, 200, 50));
        pool->Add<SDL::Texture>("food",  CreateSolidTexture(250, 50, 50));
        pool->Add<SDL::Texture>("wall",  SDL::LoadTexture(renderer, assetsPath + "textures/crate.png"));

		ui.SetDefaultFont("default_font", 16.f);

        sceneRoot = scene.Node2D("Context").Id();
        scene.SetRoot(sceneRoot);

        LoadConfig();
        BuildUI();
        ChangeState(GameState::MENU);
	}

    // -------------------------------------------------------------------------
    // Config Persistence (JSON)
    // -------------------------------------------------------------------------
    void LoadConfig() {
        SDL::IOStream io;
        try { io = SDL::IOStream::FromFile(assetsPath + "saves/snake_config.json", "r"); }
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
            getInt("speed_ms", config.speed_ms);
            getInt("grid_w", config.grid_w);
            getInt("grid_h", config.grid_h);
            getInt("difficulty", config.difficulty);
        }
    }

    void SaveConfig() {
        auto doc = SDL::DataScriptFactory::instance().createByName("json");
        auto root = SDL::ObjectDataNode::Make();
        root->set("high_score", SDL::S32DataNode::Make(config.high_score));
        root->set("speed_ms", SDL::S32DataNode::Make(config.speed_ms));
        root->set("grid_w", SDL::S32DataNode::Make(config.grid_w));
        root->set("grid_h", SDL::S32DataNode::Make(config.grid_h));
        root->set("difficulty", SDL::S32DataNode::Make(config.difficulty));
        doc->setRoot(root);
        auto io = SDL::IOStream::FromFile(assetsPath + "saves/snake_config.json", "w");
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
        ui.SetVisible(uiMenu, s == GameState::MENU);
        ui.SetVisible(uiSettings, s == GameState::SETTINGS);
        ui.SetVisible(uiGameover, s == GameState::GAMEOVER);
        ui.SetVisible(uiHud, s == GameState::PLAYING);

        if (s == GameState::PLAYING) {
            level = 0;
            LoadLevel();
            ui.SetText(lblHudScore, "Score : 0");
            ui.SetText(lblHighScore, "Best : " + std::to_string(config.high_score));
            last_step_time = SDL::GetTicksMS();
        }

        if (s == GameState::MENU || s == GameState::SETTINGS) {
            ui.SetText(lblSettingsParams, 
                "Speed: " + std::to_string(config.speed_ms) + "ms | Size: " + 
                std::to_string(config.grid_w) + "x" + std::to_string(config.grid_h) +
                " | Diff: " + std::to_string(config.difficulty)
            );
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
			.AlignChildrenH(SDL::UI::Align::Center)
            .W(300).Gap(15).AttachTo(rootUI);

		auto mkBtn = [&](const std::string& name, const std::string& label, const SDL::UI::Style& style, std::function<void()> onClick) -> SDL::UI::Builder {
			return ui.Button(name, label).W(200).H(45)
				.Style(style).OnClick(onClick)
				.FontKey("default_font").FontSize(16.f);
		};
        
        ui.Label("Title", "SNAKE ECS").FontSize(40).TextColor({50, 200, 100, 255}).MarginBottom(20).AttachTo(uiMenu);
        lblSettingsParams = ui.Label("Params", "").TextColor({150, 150, 150, 255}).AttachTo(uiMenu);
        mkBtn("BtnPlay", "JOUER", SDL::UI::Theme::SuccessButton(), [this]{ ChangeState(GameState::PLAYING); }).AttachTo(uiMenu);
        mkBtn("BtnSettings", "PARAMÈTRES", SDL::UI::Theme::PrimaryButton(), [this]{ ChangeState(GameState::SETTINGS); }).AttachTo(uiMenu);
        mkBtn("BtnQuit", "QUITTER", SDL::UI::Theme::DangerButton(), []{ SDL::Event e{}; e.type = SDL::EVENT_QUIT; SDL::PushEvent(e); }).AttachTo(uiMenu);

        // 2. SETTINGS
        uiSettings = ui.Column("Settings").AlignChildren(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .Align(SDL::UI::Align::Center, SDL::UI::Align::Center)
            .W(400).Gap(15).Visible(false).AttachTo(rootUI);
            
        ui.Label("TitleSet", "PARAMÈTRES").FontSize(32).MarginBottom(20).AttachTo(uiSettings);

        auto rowSpeed = ui.Row("RSpeed").W(SDL::UI::Value::Pw(100)).AttachTo(uiSettings);
        auto lblSpd = ui.Label("LblSpd", std::format("Vitesse : {} ms", config.speed_ms)).W(150).AttachTo(rowSpeed);
        ui.Slider("SldSpd", 50.f, 400.f, (float)config.speed_ms).Grow(1)
            .OnChange([this, lblSpd](float v){ config.speed_ms = (int)v; ui.SetText(lblSpd, std::format("Vitesse : {} ms", config.speed_ms)); })
            .AttachTo(rowSpeed);

        auto rowW = ui.Row("RW").W(SDL::UI::Value::Pw(100)).AttachTo(uiSettings);
        auto lblW = ui.Label("LblW", std::format("Largeur : {}", config.grid_w)).W(150).AttachTo(rowW);
        ui.Slider("SldW", 10.f, 60.f, (float)config.grid_w).Grow(1)
            .OnChange([this, lblW](float v){ config.grid_w = (int)v; ui.SetText(lblW, std::format("Largeur : {}", config.grid_w)); })
            .AttachTo(rowW);

        auto rowH = ui.Row("RH").W(SDL::UI::Value::Pw(100)).AttachTo(uiSettings);
        auto lblH = ui.Label("LblH", std::format("Hauteur : {}", config.grid_h)).W(150).AttachTo(rowH);
        ui.Slider("SldH", 10.f, 60.f, (float)config.grid_h).Grow(1)
            .OnChange([this, lblH](float v){ config.grid_h = (int)v; ui.SetText(lblH, std::format("Hauteur : {}", config.grid_h)); })
            .AttachTo(rowH);

        auto rowD = ui.Row("RD").W(SDL::UI::Value::Pw(100)).AttachTo(uiSettings);
        auto lblD = ui.Label("LblD", std::format("Niveau tous les : {}", config.difficulty)).W(150).AttachTo(rowD);
        ui.Slider("SldD", 10.f, 200.f, (float)config.difficulty).Grow(1)
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
        lblHudScore = ui.Label("HudS", "Score : 0").FontSize(20).TextColor({255, 255, 255, 255}).AttachTo(uiHud);
        lblHighScore = ui.Label("HudH", "Best : 0").FontSize(20).TextColor({200, 200, 200, 255}).AttachTo(uiHud);
    }

    // -------------------------------------------------------------------------
    // Level & Game Logic
    // -------------------------------------------------------------------------
    void SpawnFood() {
        if (ecs_context.IsAlive(food)) scene.DestroyNode(food);
        
        std::vector<GridPos> free_cells;
        for (int y = 0; y < config.grid_h; ++y) {
            for (int x = 0; x < config.grid_w; ++x) {
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
        // Nettoyage de l'ancienne carte
        for (auto e : walls) scene.DestroyNode(e);
        walls.clear();
        for (auto e : snake) scene.DestroyNode(e);
        snake.clear();

        current_dir = Direction::RIGHT;
        next_dir = Direction::RIGHT;

        // Configuration du rendu pour s'adapter à la taille de la grille
        block_size = std::min(800 / config.grid_w, 600 / config.grid_h);
        scene.GetGraph().SetCamera({
            {config.grid_w * block_size / 2.f, config.grid_h * block_size / 2.f},
            1.f, 800, 600
        });

        int map_idx = level % RAW_MAPS.size();
        const auto& blueprint = RAW_MAPS[map_idx];
        int bp_h = (int)blueprint.size();
        int bp_w = (int)blueprint[0].size();

        std::vector<GridPos> spawns;

        // Génération procédurale adaptée à la taille choisie par l'utilisateur
        for (int y = 0; y < config.grid_h; ++y) {
            for (int x = 0; x < config.grid_w; ++x) {
                int u = x * bp_w / config.grid_w;
                int v = y * bp_h / config.grid_h;
                char c = blueprint[v][u];
                
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

        GridPos spawn = {config.grid_w / 2, config.grid_h / 2};
        if (!spawns.empty()) {
            spawn = spawns[SDL::Rand((Sint32)spawns.size())];
        }

        // Taille du serpent revient à 3
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
        
        switch(current_dir) {
            case Direction::UP: next_pos.y--; break;
            case Direction::DOWN: next_pos.y++; break;
            case Direction::LEFT: next_pos.x--; break;
            case Direction::RIGHT: next_pos.x++; break;
        }
        
        // Wrap around transparent
        if (next_pos.x < 0) next_pos.x = config.grid_w - 1;
        if (next_pos.x >= config.grid_w) next_pos.x = 0;
        if (next_pos.y < 0) next_pos.y = config.grid_h - 1;
        if (next_pos.y >= config.grid_h) next_pos.y = 0;

        // Gestion des Collisions
        bool collision = false;
        for (auto w : walls) {
            auto gp = ecs_context.Get<GridPos>(w);
            if (gp && gp->x == next_pos.x && gp->y == next_pos.y) collision = true;
        }
        for (auto s : snake) {
            auto gp = ecs_context.Get<GridPos>(s);
            // Ignore collision with tail if we're going to move forward
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

        // Déplacement effectif
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
            
            // Passage au niveau suivant ?
            if (score > 0 && score % config.difficulty == 0) {
                level++;
                LoadLevel();
                return;
            }
            SpawnFood();
        } else {
            // Efface la queue si pas de nourriture mangée
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
        
        // Rendu
        renderer.SetDrawColor({25, 25, 30, 255});
        renderer.RenderClear();

        if (state == GameState::PLAYING) {
            scene.Render();
        }

        ui.Iterate(dt); // L'UI fait son propre layout, input et render
        renderer.Present();

        return SDL::APP_CONTINUE;
    }

    SDL::AppResult Event(const SDL::Event& e) {
        ui.ProcessEvent(e);
        scene.DispatchInput(e);

        if (state == GameState::PLAYING && e.type == SDL::EVENT_KEY_DOWN) {
            auto key = e.key.scancode;
            if (key == SDL::SCANCODE_UP && current_dir != Direction::DOWN) next_dir = Direction::UP;
            if (key == SDL::SCANCODE_DOWN && current_dir != Direction::UP) next_dir = Direction::DOWN;
            if (key == SDL::SCANCODE_LEFT && current_dir != Direction::RIGHT) next_dir = Direction::LEFT;
            if (key == SDL::SCANCODE_RIGHT && current_dir != Direction::LEFT) next_dir = Direction::RIGHT;
        }

        if (e.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
        return SDL::APP_CONTINUE;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)