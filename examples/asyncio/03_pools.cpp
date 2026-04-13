/**
 * @file asyncio/02_pools.cpp
 * @brief Resource pool demo with async loading and a progress bar.
 *
 * Demonstrates:
 *  - Creating two ResourcePools via a ResourceManager.
 *  - Loading BMP textures asynchronously with SDL::LoadFileAsync.
 *  - Displaying a progress bar while assets are streaming in.
 *  - Getting resources by name, adding a resource synchronously.
 *  - Cancelling / releasing pools on exit.
 *
 * Controls:
 *  - ESC / window-close  →  exit
 *  - C                   →  cancel the active async load mid-way
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_resources.h>

#include <format>
#include <string>

using namespace std::literals;

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr SDL::Point kWindowSz   = {640, 480};
static constexpr float      kBarX       = 80.f;
static constexpr float      kBarY       = 420.f;
static constexpr float      kBarW       = 480.f;
static constexpr float      kBarH       = 24.f;
static constexpr float      kBarPad     = 3.f;   // outline padding

// BMP files expected under  <base>/../../../assets/
static constexpr std::string_view kBmps[] = {
  "sample.bmp",
  "gamepad_front.bmp",
  "speaker.bmp",
  "icon2x.bmp",
  "sample.png",
  "gamepad_front.png",
  "speaker.png",
  "icon2x.png",
  /*"crate.jpg",
  "dirt.png"*/
};
static constexpr size_t kNumBmps = std::size(kBmps);

// Layout for the four preview thumbnails
static constexpr SDL::FRect kThumbnails[kNumBmps] = { {116.f, 60.f,  408.f, 167.f}, { 20.f, 260.f,  96.f,  60.f}, {525.f, 240.f,  96.f,  96.f}, {288.f, 350.f,  64.f,  64.f}, {116.f, 60.f,  408.f, 167.f}, { 20.f, 260.f,  96.f,  60.f}, {525.f, 240.f,  96.f,  96.f}, {288.f, 350.f,  64.f,  64.f},
};

// ─────────────────────────────────────────────────────────────────────────────
// Application state
// ─────────────────────────────────────────────────────────────────────────────

struct Main {
  // ── Window / renderer ─────────────────────────────────────────────────────

  static SDL::AppResult Init(Main** out, SDL::AppArgs /*args*/) {
    SDL::SetAppMetadata("ResourcePool async demo", "1.0",
                        "com.example.asyncio-pools");
    SDL::Init(SDL::INIT_VIDEO);
    *out = new Main();
    return SDL::APP_CONTINUE;
  }

  SDL::Window   window  {"examples/asyncio/02_pools", kWindowSz};
  SDL::Renderer renderer{window};

  // ── Resource manager ──────────────────────────────────────────────────────

  SDL::ResourceManager rm;

  // pool1 holds the async-loaded BMP textures + one sync-added colour swatch.
  // pool2 is a second pool (shown to demonstrate multi-pool management).
  SDL::ResourcePool* pool1 = rm.CreatePool("pool_1");
  SDL::ResourcePool* pool2 = rm.CreatePool("pool_2");

  // ── Application state ─────────────────────────────────────────────────────

  bool loadCancelled = false;

  // ── Constructor: kick off async loads ─────────────────────────────────────

  void LoadImage(SDL::ResourcePool* pool, const std::string key, const std::string &path) {
    pool->LoadAsync<SDL::Texture>(
      key, path,
      // Factory: called on the loading thread once bytes are available.
      // NOTE: SDL::Renderer is NOT thread-safe; we build the texture here
      //       because SDL::CreateTextureFromSurface is safe to call from
      //       any thread that owns a valid Surface (the renderer context is
      //       not touched until the texture is fully constructed).
      [this](const char* /*key*/, void* buffer, size_t bytes) -> SDL::Texture {
        SDL::Surface surface{
          SDL::IOFromConstMem({buffer, bytes})};
        return SDL::CreateTextureFromSurface(renderer, surface);
      }
    );
  }

  Main() {
    renderer.SetDrawBlendMode(SDL::BLENDMODE_BLEND);

    // ── pool1: async-load the four BMP files ──────────────────────────────
    for (auto bmp : kBmps) {
      std::string name(bmp);
      std::string path = std::format("{}../../../assets/textures/{}",
                                     SDL::GetBasePath(), bmp);

      LoadImage(pool1, name, path);
    }

    // ── pool1: add a synchronously-created solid-colour texture ───────────
		SDL::Point size = {32, 32}; {
      SDL::Texture swatch = SDL::CreateTexture(
        renderer, SDL::PIXELFORMAT_RGBA8888,
        SDL::TEXTUREACCESS_STATIC, size);
      // Fill with a pleasant teal colour via surface blit (simple approach).
      SDL::Surface surf = SDL::CreateSurface(size, SDL::PIXELFORMAT_RGBA8888);
      surf.FillRect({}, SDL::MapSurfaceRGBA(surf, {0x00, 0xBC, 0xD4, 0xFF}));
      swatch = SDL::CreateTextureFromSurface(renderer, surf);
      pool2->Add<SDL::Texture>("colour_swatch", std::move(swatch));
    }

    // ── pool2: a couple of placeholder entries (sync) ─────────────────────
		size = {8, 8}; {
      SDL::Texture placeholder = SDL::CreateTexture(
        renderer, SDL::PIXELFORMAT_RGBA8888,
        SDL::TEXTUREACCESS_STATIC, size);
      pool2->Add<SDL::Texture>("placeholder", std::move(placeholder));
    }
  }

  // ── Event handling ────────────────────────────────────────────────────────

  SDL::AppResult Event(SDL::Event& event) {
    if (event.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;

    if (event.type == SDL::EVENT_KEY_DOWN) {
      switch (event.key.scancode) {
        case SDL::SCANCODE_ESCAPE:
          return SDL::APP_SUCCESS;

        case SDL::SCANCODE_C:
          // Cancel the ongoing async load.
          if (pool1->IsLoading()) {
            pool1->CancelLoading();
            loadCancelled = true;
          }
          break;

        default: break;
      }
    }
    return SDL::APP_CONTINUE;
  }

  // ── Per-frame update ──────────────────────────────────────────────────────

  SDL::AppResult Iterate() {
    // Pump any completed async results on the main thread as well
    // (belt-and-suspenders: the worker thread handles them too).
    pool1->Update();

    const float progress = rm.TotalLoadingProgress();

    // ── Draw background
    renderer.SetDrawColor({20, 20, 30, 255});
    renderer.RenderClear();

    // ── Draw loaded textures (falsy handles are skipped) ──────────────────
    for (size_t i = 0; i < kNumBmps; ++i) {
      auto tex = pool1->Get<SDL::Texture>(std::string(kBmps[i]));
      if (tex) renderer.RenderTexture(*tex, std::nullopt, kThumbnails[i]);
    }

    // ── Draw the colour swatch (sync resource) ────────────────────────────
    if (auto swatch = pool2->Get<SDL::Texture>("colour_swatch")) {
      renderer.RenderTexture(*swatch, std::nullopt, SDL::FRect{8.f, 8.f, 48.f, 48.f});
    }

    // ── Progress bar ──────────────────────────────────────────────────────

    // Outline
    renderer.SetDrawColor({180, 180, 180, 255});
    renderer.RenderRect(SDL::FRect{
      kBarX - kBarPad, kBarY - kBarPad,
      kBarW + 2*kBarPad, kBarH + 2*kBarPad});

    // Background track
    renderer.SetDrawColor({40, 40, 50, 255});
    renderer.RenderFillRect(SDL::FRect{kBarX, kBarY, kBarW, kBarH});

    // Filled portion
    if (!loadCancelled) {
      renderer.SetDrawColor({0, 188, 212, 255}); // teal
    } else {
      renderer.SetDrawColor({220, 60, 60, 255});  // red = cancelled
    }
    renderer.RenderFillRect(SDL::FRect{
      kBarX, kBarY, kBarW * progress, kBarH});

    // Label
    renderer.SetDrawColor({240, 240, 240, 255});
    if (loadCancelled) {
      renderer.RenderDebugText({kBarX + 4, kBarY + 5}, "Load cancelled (C)");
    } else if (progress >= 1.0f) {
      renderer.RenderDebugText({kBarX + 4, kBarY + 5}, "All resources loaded!");
    } else {
      auto label = std::format("Loading... {:.0f}%", progress * 100.f);
      renderer.RenderDebugText({kBarX + 4, kBarY + 5}, label);
    }

    // Pool info
    renderer.SetDrawColor({160, 160, 160, 255});
    auto info = std::format(
      "pool1: {} resources   pool2: {} resources   [C] cancel",
      pool1->Size(), pool2->Size());
    renderer.RenderDebugText({kBarX, kBarY + kBarH + 8.f}, info);

    renderer.Present();
    return SDL::APP_CONTINUE;
  }

  // ── Cleanup ───────────────────────────────────────────────────────────────

  static void Quit(Main* m, SDL::AppResult /*result*/) {
    // Release pools explicitly (also called by ~ResourceManager).
    if (m) {
      m->pool1->Release();
      m->pool2->Release();
      delete m;
    }
  }
};

SDL3PP_DEFINE_CALLBACKS(Main)