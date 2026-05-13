#pragma once

#include "Audio.h"
#include "Camera.h"
#include "Player.h"
#include "Renderer.h"
#include "World.h"
#include "WorldSave.h"

#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_render.h>
#include <SDL3pp/SDL3pp_timer.h>

#include <cmath>
#include <memory>
#include <string>

// ── DDA Raycast ───────────────────────────────────────────────────────────────

inline bool Raycast(const World& world,
                    SDL::FVector3 origin, SDL::FVector3 dir, float maxDist,
                    int& hitX, int& hitY, int& hitZ,
                    int& prevX, int& prevY, int& prevZ)
{
    auto fsign = [](float v) { return v >= 0.f ? 1 : -1; };
    float sdx = (dir.x != 0.f) ? std::abs(1.f / dir.x) : 1e30f;
    float sdy = (dir.y != 0.f) ? std::abs(1.f / dir.y) : 1e30f;
    float sdz = (dir.z != 0.f) ? std::abs(1.f / dir.z) : 1e30f;
    int sx = fsign(dir.x), sy = fsign(dir.y), sz = fsign(dir.z);
    int x = (int)std::floor(origin.x), y = (int)std::floor(origin.y), z = (int)std::floor(origin.z);
    auto tInit = [](int cell, float org, int step, float sd) {
        return (step > 0 ? (cell + 1 - org) : (org - cell)) * sd;
    };
    float tmx = tInit(x, origin.x, sx, sdx);
    float tmy = tInit(y, origin.y, sy, sdy);
    float tmz = tInit(z, origin.z, sz, sdz);
    prevX = x; prevY = y; prevZ = z;
    float t = 0.f;
    while (t < maxDist) {
        if (world.GetBlock(x, y, z) != BlockID::AIR) {
            hitX = x; hitY = y; hitZ = z;
            return true;
        }
        prevX = x; prevY = y; prevZ = z;
        if (tmx < tmy && tmx < tmz) { t = tmx; tmx += sdx; x += sx; }
        else if (tmy < tmz)          { t = tmy; tmy += sdy; y += sy; }
        else                         { t = tmz; tmz += sdz; z += sz; }
    }
    return false;
}

// ── Game ──────────────────────────────────────────────────────────────────────

class Game {
public:
    static constexpr SDL::Point kWindowSz     { 1280, 720 };
    static constexpr float      kMouseSensDeg = 0.12f;
    static constexpr float      kReachDist    = 5.f;
    static constexpr float      kStepInterval = 0.45f;

    SDL::Window   window    { "Voxelcraft", kWindowSz };
    SDL::Renderer renderer2d = SDL::CreateRenderer(window, SDL::GPU_RENDERER);
    AudioManager  audio;

    SDL::FrameTimer frameTimer { 60 };

    // ── State machine ─────────────────────────────────────────────────────────
    enum class AppState { MainMenu, Loading, Playing };
    AppState state = AppState::MainMenu;

    // ── UI (SDL3pp_ui) ────────────────────────────────────────────────────────
    SDL::ECS::Context  uiCtx;
    SDL::ResourcePool  uiPool { "ui" };
    SDL::UI::System    ui { uiCtx, renderer2d.Get(), SDL::MixerRef{}, uiPool };

    // Root entities for each screen
    SDL::ECS::EntityId wMenuRoot    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wLoadRoot    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wDialogRoot  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wInvRoot     = SDL::ECS::NullEntity;  // inventaire
    SDL::ECS::EntityId wInvCanvas   = SDL::ECS::NullEntity;

    // Main menu specific widgets
    SDL::ECS::EntityId wWorldList   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wBtnPlay     = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wBtnDelete   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wInputName   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wInputSeed   = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wProgressBar = SDL::ECS::NullEntity;
    SDL::ECS::EntityId wLoadLabel   = SDL::ECS::NullEntity;
    bool               showDialog   = false;

    // HUD constants
    static constexpr float kSlotSz  = 44.f;
    static constexpr float kSlotGap = 4.f;
    static constexpr float kBarH    = 12.f;  // hauteur barre santé/faim

    // World data (valid in Playing state)
    std::unique_ptr<World>    world;
    std::unique_ptr<Renderer> renderer3d;
    Player                    player;

    bool  mouseCaptured = false;
    bool  keys[SDL_SCANCODE_COUNT] = {};
    float stepAccum = 0.f;

    // World list
    std::vector<WorldInfo> worldList;
    int                    selectedWorld = -1;
    std::string            activeWorldName;

    // Incremental generation state
    int  genCX = 0, genCY = 0, genCZ = 0;
    bool genPass1Done = false;
    int  treeCX = 1, treeCZ = 1;
    bool genPass2Done = false;
    bool genUploadDone = false;
    static constexpr int kChunksPerFrame = 8;

    // ── Init ──────────────────────────────────────────────────────────────────
    Game() {
        window.StartTextInput();
        BuildMenuUI();
        BuildLoadingUI();
        BuildInventoryUI();
        RefreshWorldList();
        ui.SetRoot(wMenuRoot);

        std::string fontPath = std::string(SDL::GetBasePath()) + "../../../assets/fonts/DejaVuSans.ttf";
        ui.SetDefaultFont(fontPath, 14.f);
        PrintHelp();
    }

    ~Game() {
        if (world && renderer3d) {
            world->ecs.Each<ChunkMesh>([&](SDL::ECS::EntityId, ChunkMesh& mesh) {
                if ((SDL::GPUBufferRaw)mesh.vbuf) renderer3d->device.ReleaseBuffer(mesh.vbuf);
                if ((SDL::GPUBufferRaw)mesh.ibuf) renderer3d->device.ReleaseBuffer(mesh.ibuf);
            });
        }
    }

    // ── Per-frame ─────────────────────────────────────────────────────────────
    SDL::AppResult Iterate() {
        frameTimer.Begin();
        float dt = frameTimer.GetDelta();

        switch (state) {
        case AppState::MainMenu:  IterateMenu(dt); break;
        case AppState::Loading:   IterateLoading(dt); break;
        case AppState::Playing:   IteratePlaying(dt); break;
        }

        renderer2d.Present();
        audio.Update();
        frameTimer.End();
        return SDL::APP_CONTINUE;
    }

    // ── Events ────────────────────────────────────────────────────────────────
    SDL::AppResult Event(const SDL::Event& ev) {
        if (ev.type == SDL::EVENT_QUIT)
            return SDL::APP_SUCCESS;

        if (state == AppState::MainMenu || state == AppState::Loading) {
            ui.ProcessEvent(ev);
        }

        if (state == AppState::Playing) {
            // Inventaire ouvert : déléguer les events à SDL3pp_ui
            if (player.inventoryOpen) {
                ui.ProcessEvent(ev);
                if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN)
                    HandleInventoryClick(ev.button);
            }

            if (ev.type == SDL::EVENT_KEY_DOWN) {
                keys[ev.key.scancode] = true;
                HandleKeyDown(ev.key);
            }
            if (ev.type == SDL::EVENT_KEY_UP)
                keys[ev.key.scancode] = false;

            if (ev.type == SDL::EVENT_MOUSE_MOTION && mouseCaptured) {
                player.yaw   -= ev.motion.xrel * kMouseSensDeg;
                player.pitch -= ev.motion.yrel * kMouseSensDeg;
                if (player.pitch >  89.f) player.pitch =  89.f;
                if (player.pitch < -89.f) player.pitch = -89.f;
            }

            if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN && !player.inventoryOpen)
                HandleMouseButton(ev.button);

            if (ev.type == SDL::EVENT_MOUSE_WHEEL && mouseCaptured) {
                int dir = (ev.wheel.y > 0) ? -1 : 1;
                player.hotbarSlot = (player.hotbarSlot + dir + 10) % 10;
            }
        }

        return SDL::APP_CONTINUE;
    }

private:
    Camera camera;

    // ── Menu UI construction ──────────────────────────────────────────────────

    void BuildMenuUI() {
        float W = (float)kWindowSz.x, H = (float)kWindowSz.y;
        using namespace SDL::UI;

        // Main root container (Stack so dialog can overlay absolutely)
        wMenuRoot = ui.Container("menu_root")
            .Layout(Layout::Stack)
            .W(Value::Px(W)).H(Value::Px(H))
            .BgColor({18, 20, 32, 255});

        // Inner column for actual menu content
        auto menuCol = ui.Container("menu_col")
            .Layout(Layout::InColumn)
            .W(Value::Px(W)).H(Value::Px(H))
            .BgColor({0,0,0,0})
            .Padding(40.f)
            .Gap(10.f)
            .AlignChildren(Align::Start, Align::Start)
            .AttachTo(wMenuRoot);

        // Title
        ui.Label("title", "VOXELCRAFT")
            .W(Value::Px(W - 80.f)).H(Value::Px(60.f))
            .TextColor({80, 200, 120, 255})
            .BgColor({0,0,0,0})
            .FontSize(36.f)
            .AlignH(Align::Center)
            .AttachTo(menuCol);

        // Separator
        ui.Separator("sep1").W(Value::Px(W - 80.f)).AttachTo(menuCol);

        // World list
        wWorldList = ui.ListBoxWidget("world_list", {})
            .W(Value::Px(W - 80.f)).H(Value::Px(H * 0.42f))
            .BgColor({24, 26, 40, 255})
            .TextColor({200, 210, 220, 255})
            .AttachTo(menuCol);
        ui.OnClick(wWorldList, [this](){
            if (auto* lb = uiCtx.Get<SDL::UI::ListBoxData>(wWorldList))
                selectedWorld = lb->selectedIndex;
        });

        ui.Separator("sep2").W(Value::Px(W - 80.f)).AttachTo(menuCol);

        // Button row
        auto btnRow = ui.Container("btn_row")
            .Layout(Layout::InLine)
            .W(Value::Px(W - 80.f)).H(Value::Px(48.f))
            .BgColor({0,0,0,0})
            .Gap(12.f)
            .AttachTo(menuCol);

        auto mkBtn = [&](const char* n, const char* lbl) -> SDL::ECS::EntityId {
            return ui.Button(n, lbl)
                .W(Value::Px(150.f)).H(Value::Px(40.f))
                .BgColor({50, 120, 200, 255})
                .BgHoveredColor({70, 150, 230, 255})
                .TextColor({255,255,255,255})
                .Radius({6.f,6.f,6.f,6.f})
                .AttachTo(btnRow);
        };

        auto wBtnNew    = mkBtn("btn_new",    "New World");
        wBtnPlay        = mkBtn("btn_play",   "Play");
        wBtnDelete      = mkBtn("btn_delete", "Delete");
        auto wBtnQuit   = mkBtn("btn_quit",   "Quit");

        ui.OnClick(wBtnNew, [this](){ OpenNewWorldDialog(); });
        ui.OnClick(wBtnPlay, [this](){
            if (selectedWorld >= 0 && selectedWorld < (int)worldList.size())
                StartLoading(worldList[selectedWorld].name);
        });
        ui.OnClick(wBtnDelete, [this](){
            if (selectedWorld >= 0 && selectedWorld < (int)worldList.size()) {
                WorldSave::Delete(worldList[selectedWorld].name);
                RefreshWorldList();
            }
        });
        ui.OnClick(wBtnQuit, [](){ SDL_Quit(); });
        (void)wBtnNew; (void)wBtnQuit;

        // ── New-world dialog (Fixed overlay, child of Stack root) ─────────────
        wDialogRoot = ui.Container("dialog")
            .Layout(Layout::InColumn)
            .W(Value::Px(380.f)).H(Value::Px(240.f))
            .Fixed(Value::Px(W*0.5f - 190.f), Value::Px(H*0.5f - 120.f))
            .BgColor({30, 32, 50, 255})
            .BdColor({80, 120, 200, 255})
            .Radius({8.f,8.f,8.f,8.f})
            .Padding(20.f)
            .Gap(8.f)
            .AttachTo(wMenuRoot);
        ui.SetVisible(wDialogRoot, false);

        ui.Label("dlg_title", "New World")
            .TextColor({200, 220, 255, 255})
            .BgColor({0,0,0,0})
            .H(Value::Px(26.f))
            .W(Value::Px(340.f))
            .AttachTo(wDialogRoot);
        ui.Separator("dlg_sep").W(Value::Px(340.f)).AttachTo(wDialogRoot);

        wInputName = ui.Input("input_name", "World name...")
            .W(Value::Px(340.f)).H(Value::Px(32.f))
            .AttachTo(wDialogRoot);
        wInputSeed = ui.Input("input_seed", "Seed (number, optional)")
            .W(Value::Px(340.f)).H(Value::Px(32.f))
            .AttachTo(wDialogRoot);

        auto dlgBtns = ui.Container("dlg_btns")
            .Layout(Layout::InLine)
            .W(Value::Px(340.f)).H(Value::Px(42.f))
            .BgColor({0,0,0,0}).Gap(12.f)
            .AttachTo(wDialogRoot);

        ui.Button("btn_create", "Create")
            .W(Value::Px(120.f)).H(Value::Px(36.f))
            .BgColor({50,180,80,255}).BgHoveredColor({70,210,100,255})
            .TextColor({255,255,255,255}).Radius({5.f,5.f,5.f,5.f})
            .OnClick([this](){ OnCreateWorld(); })
            .AttachTo(dlgBtns);

        ui.Button("btn_cancel", "Cancel")
            .W(Value::Px(100.f)).H(Value::Px(36.f))
            .BgColor({80,40,40,255}).BgHoveredColor({120,60,60,255})
            .TextColor({255,255,255,255}).Radius({5.f,5.f,5.f,5.f})
            .OnClick([this](){ showDialog = false; ui.SetVisible(wDialogRoot, false); })
            .AttachTo(dlgBtns);
    }

    void BuildLoadingUI() {
        float W = (float)kWindowSz.x, H = (float)kWindowSz.y;
        using namespace SDL::UI;

        wLoadRoot = ui.Container("load_root")
            .Layout(Layout::InColumn)
            .W(Value::Px(W)).H(Value::Px(H))
            .BgColor({12, 14, 22, 255})
            .AlignChildren(Align::Center, Align::Center)
            .Padding(200.f, 160.f);

        ui.Label("load_game_title", "Voxelcraft")
            .H(Value::Px(50.f)).W(Value::Px(W - 400.f))
            .TextColor({80, 200, 120, 255})
            .BgColor({0,0,0,0})
            .FontSize(28.f)
            .AlignH(Align::Center)
            .AttachTo(wLoadRoot);

        wLoadLabel = ui.Label("load_status", "Generating world...")
            .H(Value::Px(30.f)).W(Value::Px(W - 400.f))
            .TextColor({180,190,210,255})
            .BgColor({0,0,0,0})
            .AlignH(Align::Center)
            .AttachTo(wLoadRoot);

        wProgressBar = ui.Progress("load_progress", 0.f, 100.f)
            .W(Value::Px(W - 400.f)).H(Value::Px(20.f))
            .FillColor({60,180,100,255})
            .TrackColor({30,40,60,255})
            .AttachTo(wLoadRoot);
    }

    // ── Refresh world list ────────────────────────────────────────────────────

    void RefreshWorldList() {
        worldList = WorldSave::ListWorlds();
        selectedWorld = -1;
        std::vector<std::string> items;
        for (auto& wi : worldList)
            items.push_back(wi.name + "   [seed:" + std::to_string(wi.seed) + "]   " + wi.created_at);
        if (auto* lb = uiCtx.Get<SDL::UI::ListBoxData>(wWorldList)) {
            lb->items = std::move(items);
            lb->selectedIndex = -1;
        }
    }

    // ── Dialog helpers ────────────────────────────────────────────────────────

    void OpenNewWorldDialog() {
        showDialog = true;
        ui.SetVisible(wDialogRoot, true);
        if (auto* ec = uiCtx.Get<SDL::UI::EditableContent>(wInputName)) ec->text.clear();
        if (auto* ec = uiCtx.Get<SDL::UI::EditableContent>(wInputSeed)) ec->text.clear();
    }

    void OnCreateWorld() {
        auto* ecName = uiCtx.Get<SDL::UI::EditableContent>(wInputName);
        if (!ecName || ecName->text.empty()) return;
        std::string name = ecName->text;
        uint32_t    seed = 42337;
        if (auto* ecSeed = uiCtx.Get<SDL::UI::EditableContent>(wInputSeed))
            if (!ecSeed->text.empty())
                try { seed = (uint32_t)std::stoul(ecSeed->text); } catch (...) {}
        WorldSave::Create(name, seed);
        showDialog = false;
        ui.SetVisible(wDialogRoot, false);
        StartLoading(name);
    }

    // ── Loading ───────────────────────────────────────────────────────────────

    void StartLoading(const std::string& name) {
        activeWorldName = name;
        uint32_t seed = WorldSave::ReadSeed(name);
        world = std::make_unique<World>(seed);
        renderer3d.reset();
        genCX = 0; genCY = 0; genCZ = 0;
        genPass1Done = false;
        treeCX = 1; treeCZ = 1;
        genPass2Done = false;
        genUploadDone = false;
        SetLoadStatus("Generating terrain...");
        SetLoadProgress(0, TotalChunks() * 2);
        state = AppState::Loading;
        ui.SetRoot(wLoadRoot);
    }

    static int TotalChunks() { return WORLD_CX * WORLD_CY * WORLD_CZ; }

    void SetLoadStatus(const char* msg) {
        if (auto* ec = uiCtx.Get<SDL::UI::EditableContent>(wLoadLabel))
            ec->text = msg;
    }

    void SetLoadProgress(int done, int total) {
        if (auto* nv = uiCtx.Get<SDL::UI::NumericValue>(wProgressBar)) {
            nv->max = (double)std::max(total, 1);
            nv->val = (double)done;
        }
    }

    void IterateLoading(float dt) {
        if (!genPass1Done) {
            // Pass 1: create & fill chunks
            for (int i = 0; i < kChunksPerFrame && !genPass1Done; ++i) {
                world->CreateChunkAt(genCX, genCY, genCZ);
                int idx = genCX * WORLD_CY * WORLD_CZ + genCY * WORLD_CZ + genCZ;
                SetLoadProgress(idx + 1, TotalChunks() * 2);
                ++genCZ;
                if (genCZ >= WORLD_CZ) { genCZ = 0; ++genCY; }
                if (genCY >= WORLD_CY) { genCY = 0; ++genCX; }
                if (genCX >= WORLD_CX) { genPass1Done = true; SetLoadStatus("Placing trees..."); }
            }
        } else if (!genPass2Done) {
            // Pass 2: place trees
            for (int i = 0; i < 4 && !genPass2Done; ++i) {
                world->RunTreesAt(treeCX, treeCZ);
                int treesDone = (treeCX - 1) * (WORLD_CZ - 2) + (treeCZ - 1);
                SetLoadProgress(TotalChunks() + treesDone, TotalChunks() * 2);
                ++treeCZ;
                if (treeCZ >= WORLD_CZ - 1) { treeCZ = 1; ++treeCX; }
                if (treeCX >= WORLD_CX - 1) { genPass2Done = true; SetLoadStatus("Uploading to GPU..."); }
            }
        } else if (!genUploadDone) {
            // Pass 3: GPU upload + player spawn
            renderer3d = std::make_unique<Renderer>(renderer2d);
            renderer3d->UploadDirtyChunks(*world);

            float spawnX = (float)(WORLD_CX * CHUNK_W) * 0.5f;
            float spawnZ = (float)(WORLD_CZ * CHUNK_D) * 0.5f;
            float spawnY = (float)world->GetTopBlock((int)spawnX, (int)spawnZ) + 1.f;
            player = Player{};
            player.spawn(spawnX, spawnY, spawnZ);
            SyncCameraFromPlayer();

            genUploadDone = true;
            SetLoadProgress(TotalChunks() * 2, TotalChunks() * 2);
            SetLoadStatus("Ready!");

            // Transition to playing next frame
            state = AppState::Playing;
            window.SetRelativeMouseMode(true);
            mouseCaptured = true;
        }

        // Render loading UI
        renderer2d.SetDrawColor({12, 14, 22, 255});
        renderer2d.RenderClear();
        ui.Iterate(dt);
    }

    // ── Menu iteration ────────────────────────────────────────────────────────

    void IterateMenu(float dt) {
        renderer2d.SetDrawColor({18, 20, 32, 255});
        renderer2d.RenderClear();
        ui.SetRoot(wMenuRoot);
        ui.Iterate(dt);
    }

    // ── Playing iteration ─────────────────────────────────────────────────────

    void IteratePlaying(float dt) {
        if (mouseCaptured) {
            UpdatePlayer(dt);
            SyncCameraFromPlayer();
        }

        renderer3d->UploadDirtyChunks(*world);
        renderer3d->DrawFrame(*world, camera);

        SDL::TextureRef colorTex = renderer3d->GetColorSdlTex();
        if (colorTex)
            renderer2d.RenderTextureRotated(colorTex, std::nullopt, std::nullopt,
                                            0.0, std::nullopt, SDL::FLIP_VERTICAL);

        // HUD par-dessus la 3D
        DrawHUD();

        // Inventaire (overlay SDL3pp_ui)
        if (player.inventoryOpen) {
            ui.SetRoot(wInvRoot);
            ui.Iterate(dt);
        }

        UpdateTitle();
    }

    // ── Player & camera ───────────────────────────────────────────────────────

    void SyncCameraFromPlayer() {
        static constexpr float DEG2RAD = 3.14159265f / 180.f;
        camera.position = { player.eyeX(), player.eyeY(), player.eyeZ() };
        camera.yaw      = -(player.yaw   * DEG2RAD);
        camera.pitch    = -(player.pitch * DEG2RAD);
    }

    void UpdatePlayer(float dt) {
        bool fwd   = keys[SDL_SCANCODE_W];
        bool back  = keys[SDL_SCANCODE_S];
        bool left  = keys[SDL_SCANCODE_A];
        bool right = keys[SDL_SCANCODE_D];
        bool jump  = keys[SDL_SCANCODE_SPACE];
        bool sneak = keys[SDL_SCANCODE_LSHIFT];

        if (player.flying && jump)  player.vy =  10.f;
        if (player.flying && sneak) player.vy = -10.f;

        player.update(dt, *world, fwd, back, left, right, jump, sneak);

        if (player.grounded && (fwd || back || left || right)) {
            stepAccum += dt;
            if (stepAccum >= kStepInterval) {
                stepAccum = 0.f;
                BlockID surface = world->GetBlock((int)player.x,
                                                   (int)(player.y - 0.1f),
                                                   (int)player.z);
                if (surface != BlockID::AIR)
                    audio.PlayFootstep(surface);
            }
        } else {
            stepAccum = 0.f;
        }
    }

    void HandleKeyDown(const SDL::KeyboardEvent& key) {
        switch (key.key) {
        case SDL::KEYCODE_ESCAPE:
            if (player.inventoryOpen) {
                CloseInventory();
            } else if (mouseCaptured) {
                window.SetRelativeMouseMode(false);
                mouseCaptured = false;
            } else {
                state = AppState::MainMenu;
                ui.SetRoot(wMenuRoot);
                RefreshWorldList();
            }
            break;
        case SDL::KEYCODE_E:
            if (player.inventoryOpen) CloseInventory();
            else                       OpenInventory();
            break;
        case SDL::KEYCODE_F:
            player.flying = !player.flying;
            player.vy = 0.f;
            break;
        case SDL::KEYCODE_0: player.hotbarSlot = 9; break;
        case SDL::KEYCODE_1: player.hotbarSlot = 0; break;
        case SDL::KEYCODE_2: player.hotbarSlot = 1; break;
        case SDL::KEYCODE_3: player.hotbarSlot = 2; break;
        case SDL::KEYCODE_4: player.hotbarSlot = 3; break;
        case SDL::KEYCODE_5: player.hotbarSlot = 4; break;
        case SDL::KEYCODE_6: player.hotbarSlot = 5; break;
        case SDL::KEYCODE_7: player.hotbarSlot = 6; break;
        case SDL::KEYCODE_8: player.hotbarSlot = 7; break;
        case SDL::KEYCODE_9: player.hotbarSlot = 8; break;
        default: break;
        }
    }

    void HandleMouseButton(const SDL::MouseButtonEvent& btn) {
        if (!mouseCaptured) {
            window.SetRelativeMouseMode(true);
            mouseCaptured = true;
            return;
        }
        int hx, hy, hz, px, py, pz;
        SDL::FVector3 eye { player.eyeX(), player.eyeY(), player.eyeZ() };
        if (!Raycast(*world, eye, camera.Forward(), kReachDist, hx, hy, hz, px, py, pz))
            return;
        if (btn.button == SDL_BUTTON_LEFT) {
            BlockID broken = world->GetBlock(hx, hy, hz);
            world->SetBlock(hx, hy, hz, BlockID::AIR);
            audio.PlayBlockBreak(broken);
            renderer3d->UploadDirtyChunks(*world);
        } else if (btn.button == SDL_BUTTON_RIGHT) {
            BlockID toPlace = player.selectedBlock();
            if (toPlace != BlockID::AIR) {
                world->SetBlock(px, py, pz, toPlace);
                audio.PlayBlockPlace(toPlace);
                renderer3d->UploadDirtyChunks(*world);
            }
        }
    }

    // ── Couleurs des blocs (HUD) ──────────────────────────────────────────────
    static SDL::Color BlockColorForHUD(BlockID b) {
        switch (b) {
        case BlockID::GRASS:         return {86,  153,  62, 255};
        case BlockID::DIRT:          return {134,  95,  66, 255};
        case BlockID::STONE:         return {130, 130, 130, 255};
        case BlockID::SAND:          return {219, 206, 143, 255};
        case BlockID::GRAVEL:        return {158, 142, 127, 255};
        case BlockID::WOOD:          return {101,  79,  49, 255};
        case BlockID::COBBLESTONE:   return {112, 112, 112, 255};
        case BlockID::COAL_ORE:      return { 45,  45,  45, 255};
        case BlockID::WATER:         return { 48,  97, 196, 200};
        case BlockID::MUD:           return { 85,  67,  47, 255};
        case BlockID::SNOW:          return {235, 242, 255, 255};
        case BlockID::PALM_WOOD:     return {120,  95,  60, 255};
        case BlockID::PALM_LEAVES:   return {105, 175,  60, 255};
        case BlockID::BAOBAB_WOOD:   return { 90,  80,  65, 255};
        case BlockID::BAOBAB_LEAVES: return { 65, 120,  45, 255};
        case BlockID::CRYSTAL_STONE: return {100, 220, 220, 255};
        case BlockID::SULFUR_STONE:  return {200, 190,  50, 255};
        case BlockID::LEAVES:        return { 75, 140,  50, 255};
        default:                     return { 60,  60,  60, 255};
        }
    }

    // ── HUD : dessin direct SDL ────────────────────────────────────────────────

    // Dessine un slot inventaire (carré coloré + bordure)
    void DrawSlot(float x, float y, float sz, BlockID b, bool selected) {
        SDL::FRect outer{x, y, sz, sz};
        // Fond du slot
        renderer2d.SetDrawColor(selected ? SDL::Color{80, 120, 220, 220}
                                         : SDL::Color{20,  22,  35, 180});
        renderer2d.RenderFillRect(outer);
        // Couleur du bloc
        if (b != BlockID::AIR) {
            SDL::Color c = BlockColorForHUD(b);
            renderer2d.SetDrawColor(c);
            SDL::FRect inner{x+5, y+5, sz-10, sz-10};
            renderer2d.RenderFillRect(inner);
            // Reflet léger en haut-gauche
            renderer2d.SetDrawColor({255,255,255,50});
            SDL::FRect hi{x+5, y+5, sz-10, (sz-10)*0.4f};
            renderer2d.RenderFillRect(hi);
        }
        // Bordure
        renderer2d.SetDrawColor(selected ? SDL::Color{180, 210, 255, 255}
                                         : SDL::Color{ 70,  80, 100, 200});
        renderer2d.RenderRect(outer);
    }

    // Dessine N segments de barre (santé / faim) avec couleur personnalisée
    void DrawSegmentBar(float x, float y, float totalW, float h,
                        float value, float maxVal,
                        SDL::Color fill, SDL::Color empty) {
        constexpr int kSegs = 10;
        float segW = (totalW - (kSegs - 1) * 2.f) / kSegs;
        float ratio = (maxVal > 0.f) ? (value / maxVal) : 0.f;
        int   filled = (int)(ratio * kSegs + 0.5f);
        for (int i = 0; i < kSegs; ++i) {
            float sx = x + i * (segW + 2.f);
            SDL::FRect r{sx, y, segW, h};
            renderer2d.SetDrawColor(i < filled ? fill : empty);
            renderer2d.RenderFillRect(r);
            renderer2d.SetDrawColor({0, 0, 0, 180});
            renderer2d.RenderRect(r);
        }
    }

    void DrawHUD() {
        float W = (float)kWindowSz.x;
        float H = (float)kWindowSz.y;

        // ── Hotbar (10 slots, centré en bas) ──────────────────────────────────
        float hotbarW = INV_COLS * (kSlotSz + kSlotGap) - kSlotGap;
        float hx = (W - hotbarW) * 0.5f;
        float hy = H - kSlotSz - 12.f;

        for (int i = 0; i < INV_COLS; ++i) {
            DrawSlot(hx + i * (kSlotSz + kSlotGap), hy,
                     kSlotSz, player.inventory[i], i == player.hotbarSlot);
        }

        // Numéro sous chaque slot (petit indicateur)
        // (pas de texte SDL simple sans TTF — on utilise juste les couleurs)

        // ── Barre de santé (bas-gauche) ───────────────────────────────────────
        float barW = 160.f;
        float bx   = 16.f;
        float byH  = H - kSlotSz - 12.f - kBarH - 8.f;

        // Icône cœur (carré rouge arrondi stylisé)
        renderer2d.SetDrawColor({220, 40, 40, 255});
        renderer2d.RenderFillRect(SDL::FRect{bx, byH, kBarH, kBarH});
        renderer2d.SetDrawColor({0,0,0,180});
        renderer2d.RenderRect(SDL::FRect{bx, byH, kBarH, kBarH});

        DrawSegmentBar(bx + kBarH + 4.f, byH, barW, kBarH,
                       player.health, player.maxHealth,
                       {200, 40,  40, 230}, {60, 20, 20, 200});

        // ── Barre de faim (bas-droite, symétrique) ────────────────────────────
        float fx = W - 16.f - kBarH - 4.f - barW;
        float fyH = byH;

        DrawSegmentBar(fx, fyH, barW, kBarH,
                       player.hunger, player.maxHunger,
                       {210, 140, 30, 230}, {60, 42, 12, 200});

        renderer2d.SetDrawColor({210, 140, 30, 255});
        renderer2d.RenderFillRect(SDL::FRect{fx + barW + 4.f, fyH, kBarH, kBarH});
        renderer2d.SetDrawColor({0,0,0,180});
        renderer2d.RenderRect(SDL::FRect{fx + barW + 4.f, fyH, kBarH, kBarH});

        // ── Réticule (croix centrale) ─────────────────────────────────────────
        if (mouseCaptured) {
            float cx = W * 0.5f, cy = H * 0.5f;
            renderer2d.SetDrawColor({255, 255, 255, 200});
            renderer2d.RenderLine(SDL::FPoint{cx-8, cy}, SDL::FPoint{cx+8, cy});
            renderer2d.RenderLine(SDL::FPoint{cx, cy-8}, SDL::FPoint{cx, cy+8});
        }
    }

    // ── Inventaire UI ─────────────────────────────────────────────────────────

    void BuildInventoryUI() {
        float W = (float)kWindowSz.x, H = (float)kWindowSz.y;
        using namespace SDL::UI;

        float gridW = INV_COLS * (kSlotSz + kSlotGap) - kSlotGap;
        float gridH = INV_ROWS * (kSlotSz + kSlotGap) - kSlotGap;
        float panelW = gridW + 48.f;
        float panelH = gridH + 80.f;  // titre + bouton fermer

        wInvRoot = ui.Container("inv_root")
            .Layout(Layout::Stack)
            .W(Value::Px(W)).H(Value::Px(H))
            .BgColor({0, 0, 0, 140});

        auto panel = ui.Container("inv_panel")
            .Layout(Layout::InColumn)
            .W(Value::Px(panelW)).H(Value::Px(panelH))
            .Fixed(Value::Px((W - panelW) * 0.5f), Value::Px((H - panelH) * 0.5f))
            .BgColor({22, 24, 38, 230})
            .BdColor({70, 90, 140, 255})
            .Radius({8.f, 8.f, 8.f, 8.f})
            .Padding(16.f)
            .Gap(10.f)
            .AttachTo(wInvRoot);

        // Titre + bouton fermer (ligne)
        auto titleRow = ui.Container("inv_title_row")
            .Layout(Layout::InLine)
            .W(Value::Px(gridW)).H(Value::Px(28.f))
            .BgColor({0,0,0,0})
            .AttachTo(panel);

        ui.Label("inv_title", "Inventaire")
            .W(Value::Px(gridW - 80.f)).H(Value::Px(28.f))
            .TextColor({200, 220, 255, 255})
            .BgColor({0,0,0,0})
            .FontSize(16.f)
            .AttachTo(titleRow);

        ui.Button("inv_close", "Fermer [E]")
            .W(Value::Px(74.f)).H(Value::Px(26.f))
            .BgColor({80, 30, 30, 255}).BgHoveredColor({120, 50, 50, 255})
            .TextColor({255,255,255,255}).Radius({4.f,4.f,4.f,4.f})
            .OnClick([this](){ CloseInventory(); })
            .AttachTo(titleRow);

        // Canvas grille
        wInvCanvas = ui.CanvasWidget("inv_canvas",
            nullptr,  // event
            nullptr,  // update
            [this](SDL::RendererRef r, SDL::FRect rect) {
                DrawInventoryCanvas(r, rect);
            })
            .W(Value::Px(gridW))
            .H(Value::Px(gridH))
            .AttachTo(panel);
    }

    void DrawInventoryCanvas(SDL::RendererRef r, SDL::FRect rect) {
        for (int row = 0; row < INV_ROWS; ++row) {
            for (int col = 0; col < INV_COLS; ++col) {
                int idx = row * INV_COLS + col;
                float sx = rect.x + col * (kSlotSz + kSlotGap);
                float sy = rect.y + row * (kSlotSz + kSlotGap);

                bool selected = (row == 0 && col == player.hotbarSlot);
                bool isHotbar = (row == 0);

                // Fond
                SDL::Color bg = selected      ? SDL::Color{80, 120, 220, 220}
                              : isHotbar      ? SDL::Color{35,  40,  70, 200}
                                             : SDL::Color{22,  26,  45, 180};
                r.SetDrawColor(bg);
                r.RenderFillRect(SDL::FRect{sx, sy, kSlotSz, kSlotSz});

                BlockID b = player.inventory[idx];
                if (b != BlockID::AIR) {
                    SDL::Color c = BlockColorForHUD(b);
                    r.SetDrawColor(c);
                    r.RenderFillRect(SDL::FRect{sx+5, sy+5, kSlotSz-10, kSlotSz-10});
                    // Reflet
                    r.SetDrawColor({255,255,255,40});
                    r.RenderFillRect(SDL::FRect{sx+5, sy+5, kSlotSz-10, (kSlotSz-10)*0.35f});
                }

                // Bordure
                r.SetDrawColor(selected ? SDL::Color{180, 210, 255, 255}
                             : isHotbar ? SDL::Color{100, 120, 180, 200}
                                        : SDL::Color{ 55,  65,  90, 200});
                r.RenderRect(SDL::FRect{sx, sy, kSlotSz, kSlotSz});
            }
        }
    }

    void OpenInventory() {
        player.inventoryOpen = true;
        window.SetRelativeMouseMode(false);
        mouseCaptured = false;
    }

    void CloseInventory() {
        player.inventoryOpen = false;
        window.SetRelativeMouseMode(true);
        mouseCaptured = true;
    }

    // Clic dans la grille inventaire : échange l'item cliqué vers le slot hotbar actif
    void HandleInventoryClick(const SDL::MouseButtonEvent& btn) {
        if (btn.button != SDL_BUTTON_LEFT) return;
        auto* cr = uiCtx.Get<SDL::UI::ComputedRect>(wInvCanvas);
        if (!cr) return;
        float mx = btn.x - cr->screen.x;
        float my = btn.y - cr->screen.y;
        if (mx < 0 || my < 0) return;
        int col = (int)(mx / (kSlotSz + kSlotGap));
        int row = (int)(my / (kSlotSz + kSlotGap));
        if (col < 0 || col >= INV_COLS || row < 0 || row >= INV_ROWS) return;

        int clicked = row * INV_COLS + col;
        // Si clic sur hotbar : sélectionne le slot
        if (row == 0) {
            player.hotbarSlot = col;
            return;
        }
        // Sinon : échange le bloc cliqué avec le slot hotbar actif
        std::swap(player.inventory[clicked], player.inventory[player.hotbarSlot]);
    }

    void UpdateTitle() {
        BlockID sel = player.selectedBlock();
        char buf[256];
        SDL_snprintf(buf, sizeof(buf),
            "Voxelcraft | FPS:%d | [%d] %s | %s | E=inv F=fly 0-9 Wheel ESC",
            (int)frameTimer.GetFPS(),
            player.hotbarSlot + 1, blockName(sel),
            player.flying ? "FLY" : (player.grounded ? "GND" : "AIR"));
        SDL::SetWindowTitle(window, buf);
    }

    static void PrintHelp() {
        SDL::Log("=== Voxelcraft ===");
        SDL::Log("WASD          : deplacement");
        SDL::Log("Space/Shift   : saut / sneaker (vol : haut/bas)");
        SDL::Log("F             : vol mode");
        SDL::Log("LMB / RMB     : casser / poser bloc");
        SDL::Log("0-9 / Molette : slot hotbar (0=slot 10)");
        SDL::Log("E             : ouvrir/fermer inventaire");
        SDL::Log("ESC           : relacher souris / menu principal");
    }
};
