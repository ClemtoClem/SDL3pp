/**
 * @file 01_clear.cpp
 *
 * GPU example 01 – Screen clear
 *
 * Demonstrates the minimal SDL3 GPU API setup:
 *   - GPU device creation
 *   - Claiming a window for GPU rendering
 *   - Acquiring a command buffer each frame
 *   - Acquiring the swapchain texture
 *   - Opening a render pass that clears to a colour
 *   - Submitting the command buffer
 *
 * No shaders, no vertex buffers – just a clear.
 *
 * The clear colour cycles smoothly through hues using offset sine waves.
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <cmath>

struct Main {
  static constexpr SDL::Point windowSz = {800, 600};

  // ── Initialisation helpers ────────────────────────────────────────────
  static SDL::Window CreateWindow() {
    SDL::SetAppMetadata("GPU Clear", "1.0", "com.example.gpu.clear");
    SDL::Init(SDL::INIT_VIDEO);
    return SDL::CreateWindow("examples/gpu/01_clear", windowSz, 0);
  }

  // ── Members (initialised in declaration order) ────────────────────────
  SDL::Window    window{CreateWindow()};
  SDL::GPUDevice device{SDL::GPU_SHADERFORMAT_SPIRV, false, nullptr};
  float          t = 0.0f;

  // ── Constructor / destructor ──────────────────────────────────────────
  Main() { device.ClaimWindow(window); }
  ~Main() { device.ReleaseWindow(window); }

  // ── Per-frame logic ───────────────────────────────────────────────────
  SDL::AppResult Iterate() {
    t += 0.01f;

    // Three sine waves with 120° phase offsets → smooth RGB cycling
    SDL::FColor color{
      (std::sin(t * 1.0f) + 1.0f) * 0.5f,
      (std::sin(t * 1.0f + 2.094f) + 1.0f) * 0.5f,
      (std::sin(t * 1.0f + 4.189f) + 1.0f) * 0.5f,
      1.0f
    };

    auto cmdBuf  = device.AcquireCommandBuffer();
    auto swapTex = cmdBuf.WaitAndAcquireSwapchainTexture(window);

    // WaitAndAcquireSwapchainTexture can return null when the window is
    // minimised or not yet ready – skip rendering in that case.
    if (!static_cast<SDL::GPUTextureRaw>(swapTex)) {
      cmdBuf.Submit();
      return SDL::APP_CONTINUE;
    }

    SDL::GPUColorTargetInfo ct{};
    ct.texture     = swapTex;
    ct.clear_color = color;
    ct.load_op     = SDL::GPU_LOADOP_CLEAR;
    ct.store_op    = SDL::GPU_STOREOP_STORE;

    auto pass = cmdBuf.BeginRenderPass(std::span{&ct, 1}, std::nullopt);
    pass.End();

    cmdBuf.Submit();
    return SDL::APP_CONTINUE;
  }

  // ── Event handling ────────────────────────────────────────────────────
  SDL::AppResult Event(const SDL::Event& event) {
    if (event.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
    if (event.type == SDL::EVENT_KEY_DOWN &&
        event.key.scancode == SDL::SCANCODE_ESCAPE)
      return SDL::APP_SUCCESS;
    return SDL::APP_CONTINUE;
  }
};

SDL3PP_DEFINE_CALLBACKS(Main)
