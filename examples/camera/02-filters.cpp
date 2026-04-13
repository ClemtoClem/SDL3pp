/**
 * @file camera/02_filters.cpp
 * @brief Live camera feed with real-time image filter cycling (Multi-threaded).
 *
 * Each frame from the webcam is:
 *   1. Acquired as a SDL::Surface (via SDL3's camera API).
 *   2. Passed through the currently selected SDL::SurfaceFilter.
 *   3. Uploaded to a streaming texture and displayed.
 *
 * Controls:
 *   LEFT  arrow  ──  previous filter
 *   RIGHT arrow  ──  next filter
 *   SPACE        ──  toggle filter on/off (show raw frame)
 *   ESC          ──  quit
 *
 * The filter name and index are displayed as a HUD overlay.
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>

#include <format>
#include <memory>
#include <stdexcept>
#include <queue>
#include <mutex>
#include <shared_mutex>
#include <condition_variable>
#include <atomic>
#include <vector>
#include <string>

constexpr size_t QUEUE_SIZE = 20;

// ─────────────────────────────────────────────────────────────────────────────
// Build the filter registry
// ─────────────────────────────────────────────────────────────────────────────

static SDL::SurfaceFilterRegistry MakeRegistry() {
  SDL::SurfaceFilterRegistry reg;

  // ── Pointwise ─────────────────────────────────────────────────────────────
  reg.Add(std::make_unique<SDL::GrayscaleSurfaceFilter>());
  reg.Add(std::make_unique<SDL::InvertSurfaceFilter>());

  reg.Add(std::make_unique<SDL::SepiaSurfaceFilter>(0.5f));
  reg.Add(std::make_unique<SDL::SepiaSurfaceFilter>(1.f));

  reg.Add(std::make_unique<SDL::BrightnessSurfaceFilter>(50.f));
  reg.Add(std::make_unique<SDL::BrightnessSurfaceFilter>(-50.f));

  reg.Add(std::make_unique<SDL::ContrastSurfaceFilter>(2.0f));
  reg.Add(std::make_unique<SDL::ContrastSurfaceFilter>(0.5f));
  reg.Add(std::make_unique<SDL::ContrastSurfaceFilter>(-0.5f));
  reg.Add(std::make_unique<SDL::ContrastSurfaceFilter>(-2.0f));

  reg.Add(std::make_unique<SDL::ThresholdSurfaceFilter>(32.f));
  reg.Add(std::make_unique<SDL::ThresholdSurfaceFilter>(64.f));
  reg.Add(std::make_unique<SDL::ThresholdSurfaceFilter>(128.f));

  // ── Convolution ───────────────────────────────────────────────────────────
  reg.Add(std::make_unique<SDL::BoxBlurSurfaceFilter>(2));
  reg.Add(std::make_unique<SDL::GaussianBlurSurfaceFilter>(2.0f));
  reg.Add(std::make_unique<SDL::SharpenSurfaceFilter>(1.0f));
  reg.Add(std::make_unique<SDL::SobelSurfaceFilter>());
  reg.Add(std::make_unique<SDL::PrewittSurfaceFilter>());
  reg.Add(std::make_unique<SDL::LaplacianSurfaceFilter>());
  reg.Add(std::make_unique<SDL::EmbossSurfaceFilter>());
  
  reg.Add(std::make_unique<SDL::CannySurfaceFilter>(1.4f, 40.f, 100.f));
  reg.Add(std::make_unique<SDL::CannySurfaceFilter>(1.0f, 30.f, 120.f));

  // ── Morphological ─────────────────────────────────────────────────────────
  reg.Add(std::make_unique<SDL::DilationSurfaceFilter>(2));
  reg.Add(std::make_unique<SDL::ErosionSurfaceFilter>(2));
  reg.Add(std::make_unique<SDL::MorphCloseSurfaceFilter>(2));

  // ── Statistical ───────────────────────────────────────────────────────────
  reg.Add(std::make_unique<SDL::MedianSurfaceFilter>(1));
  reg.Add(std::make_unique<SDL::MinSurfaceFilter>(50));
  reg.Add(std::make_unique<SDL::MaxSurfaceFilter>(215));

  // ── Bilateral ─────────────────────────────────────────────────────────────
  reg.Add(std::make_unique<SDL::BilateralSurfaceFilter>(3.f, 30.f));

  // ── Dithering ─────────────────────────────────────────────────────────────
  reg.Add(std::make_unique<SDL::FloydSteinbergDitherSurfaceFilter>(2));   // B&W dither
  reg.Add(std::make_unique<SDL::FloydSteinbergDitherSurfaceFilter>(4));   // 4-level

  // ── LUT ───────────────────────────────────────────────────────────────────
  {
    auto lut = std::make_unique<SDL::LutSurfaceFilter>(SDL::LutSurfaceFilter::MakeWarmth(30));
    lut->Name(); // just to confirm linkage; Name() is virtual
    reg.Add(std::move(lut));
  }
  reg.Add(std::make_unique<SDL::LutSurfaceFilter>(SDL::LutSurfaceFilter::MakeNightVision()));
  reg.Add(std::make_unique<SDL::LutSurfaceFilter>(SDL::LutSurfaceFilter::MakeVintage()));
  reg.Add(std::make_unique<SDL::LutSurfaceFilter>(SDL::LutSurfaceFilter::MakeInvert()));

  return reg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Thread-Safe Queue pour les Surfaces (Pattern Producer-Consumer)
// ─────────────────────────────────────────────────────────────────────────────
class SurfaceQueue {
  std::queue<SDL::Surface> q;
  std::mutex mtx;
  std::condition_variable cv;
  size_t max_size;

public:
  SurfaceQueue(size_t limit) : max_size(limit) {}

  void Push(SDL::Surface&& s) {
    std::lock_guard<std::mutex> lock(mtx);
    if (q.size() >= max_size) {
      q.pop(); // Drop la frame la plus vieille pour éviter la latence
    }
    q.push(std::move(s));
    cv.notify_one();
  }

  bool TryPop(SDL::Surface& s) {
    std::lock_guard<std::mutex> lock(mtx);
    if (q.empty()) return false;
    s = std::move(q.front());
    q.pop();
    return true;
  }

  bool WaitAndPop(SDL::Surface& s, std::atomic<bool>& running) {
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&]{ return !q.empty() || !running; });
    if (!running && q.empty()) return false;
    
    s = std::move(q.front());
    q.pop();
    return true;
  }

  void Stop() {
    std::lock_guard<std::mutex> lock(mtx);
    cv.notify_all();
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mtx);
    cv.notify_all();
    while(!q.empty()) q.pop();
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────

struct Main {
  // ── Init (static) ─────────────────────────────────────────────────────────

  static SDL::AppResult Init(Main** out, SDL::AppArgs /*args*/) {
    SDL::SetLogPriority(SDL::LOG_CATEGORY_APPLICATION, SDL::LOG_PRIORITY_INFO);
    SDL::SetAppMetadata("Camera SurfaceFilters Demo", "1.0", "com.example.camera-filters");
    SDL::Init(SDL::INIT_VIDEO | SDL::INIT_CAMERA);
    *out = new Main();
    return SDL::APP_CONTINUE;
  }

  // ── Window / renderer ─────────────────────────────────────────────────────

  SDL::Window   window  {"examples/camera/filters", {640, 480}};
  SDL::Renderer renderer{window};

  // ── Camera ────────────────────────────────────────────────────────────────

  SDL::OwnArray<SDL::CameraID> devices = SDL::GetCameras();
  SDL::Camera  camera;
  SDL::Texture texture;         ///< Streaming texture for camera frames.
  SDL::Texture filteredTex;     ///< Streaming texture for filtered output.

  // ── Multithreading & State ────────────────────────────────────────────────

  SDL::SurfaceFilterRegistry registry = MakeRegistry();
  std::shared_mutex             registryMutex; // RW Lock

  std::atomic<bool>             filterEnabled{true};
  std::atomic<bool>             cameraReady{false};
  std::atomic<bool>             running{true};

  SurfaceQueue                  inputQueue{QUEUE_SIZE}; 
  SurfaceQueue                  outputQueue{QUEUE_SIZE};
  std::vector<SDL::Thread>      workers; // Changement: Utilisation de SDL::Thread
  
  bool                          lastWasSurfaceFiltered = true;

  // ── Constructor ───────────────────────────────────────────────────────────

  Main() {
    if (devices.empty()) {
      throw std::runtime_error{
        "No camera found! Connect a webcam and try again."};
    }
    camera = SDL::OpenCamera(devices[0]);

    // Initialisation du Pool de Threads SDL
    // Note: hardware_concurrency() vient de <thread> mais on peut l'utiliser pour dimensionner
    unsigned int numThreads = std::max(2u, std::thread::hardware_concurrency() - 1);
    
    for (unsigned int i = 0; i < numThreads; ++i) {
      std::string threadName = "SurfaceFilterWorker-" + std::to_string(i);
      
      // SDL::ThreadCB attend un std::function<int()>
      workers.emplace_back([this]() -> int { 
        return this->WorkerThread(); 
      }, threadName.c_str());
    }
  }

  // ── Destructor (Cleanup des threads) ──────────────────────────────────────
  
  ~Main() {
    running = false;
    inputQueue.Stop(); // Réveille les workers en attente
    
    for (auto& t : workers) {
        if (t) {
          int status;
          // On utilise Release() pour extraire le pointeur brut.
          // C'est nécessaire car SDL_WaitThread détruit le thread en interne,
          // et nous voulons éviter que le destructeur de SDL::Thread ne tente
          // de faire un SDL_DetachThread sur un pointeur devenu invalide.
          SDL::WaitThread(t.Release(), &status);
        }
    }
  }

  // ── Worker Routine ────────────────────────────────────────────────────────
  
  // Doit maintenant retourner un int pour satisfaire SDL::ThreadFunction / SDL::ThreadCB
  int WorkerThread() {
    while (running) {
      SDL::Surface inputSurface;
      if (inputQueue.WaitAndPop(inputSurface, running)) {
        if (!filterEnabled) {
          outputQueue.Push(std::move(inputSurface));
        } else {
          SDL::Surface filtered;
          try {
            std::shared_lock<std::shared_mutex> lock(registryMutex);
            filtered = registry.ApplyCurrent(inputSurface);
          } catch (const std::exception& e) {
            SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION, "SurfaceFilter error: %s", e.what());
            filtered = std::move(inputSurface);
          }
          outputQueue.Push(std::move(filtered));
        }
      }
    }
    return 0; // Succès
  }

  // ── Events ────────────────────────────────────────────────────────────────

  SDL::AppResult Event(const SDL::Event& ev) {
    switch (ev.type) {
      case SDL::EVENT_QUIT:
        return SDL::APP_SUCCESS;

      case SDL::EVENT_CAMERA_DEVICE_APPROVED:
        SDL::LogInfo(SDL::LOG_CATEGORY_APPLICATION, "Camera approved.");
        cameraReady = true;
        break;

      case SDL::EVENT_CAMERA_DEVICE_DENIED:
        SDL::LogError(SDL::LOG_CATEGORY_APPLICATION, "Camera denied by user.");
        return SDL::APP_FAILURE;

      case SDL::EVENT_KEY_DOWN:
        switch (ev.key.key) {
          case SDL::KEYCODE_ESCAPE: return SDL::APP_SUCCESS;
          case SDL::KEYCODE_LEFT: {
            //std::unique_lock<std::shared_mutex> lock(registryMutex);
            inputQueue.Clear();
            outputQueue.Clear();
            registry.Prev();
            break;
          }
          case SDL::KEYCODE_RIGHT: {
            //std::unique_lock<std::shared_mutex> lock(registryMutex);
            inputQueue.Clear();
            outputQueue.Clear();
            registry.Next();
            break;
          }
          case SDL::KEYCODE_SPACE: {
            //std::unique_lock<std::shared_mutex> lock(registryMutex);
            inputQueue.Clear();
            outputQueue.Clear();
            filterEnabled = !filterEnabled;
            break;
          }
          default: break;
        }
        break;

      default: break;
    }
    return SDL::APP_CONTINUE;
  }

  // ── Per-frame ─────────────────────────────────────────────────────────────

  SDL::AppResult Iterate() {
    renderer.SetDrawColor({30, 30, 30, 255});
    renderer.RenderClear();

    // ── Acquire camera frame & Feed the Workers ───────────────────────────
    Uint64 tsNS = 0;
    if (auto frame = camera.AcquireFrame(&tsNS)) {
      if (!texture) {
        const SDL::Point sz = frame.GetSize();
        window.SetSize(sz);
        texture = SDL::CreateTexture(renderer, frame.GetFormat(), SDL::TEXTUREACCESS_STREAMING, sz);
        filteredTex = SDL::CreateTexture(renderer, SDL::PIXELFORMAT_RGBA8888, SDL::TEXTUREACCESS_STREAMING, sz);
      }
      inputQueue.Push(frame.Duplicate());
    }

    // ── Retrieve Processed Frames ─────────────────────────────────────────
    SDL::Surface outSurface;
    bool hasNewFrame = false;

    while (outputQueue.TryPop(outSurface)) {
      hasNewFrame = true;
    }

    if (hasNewFrame) {
      lastWasSurfaceFiltered = filterEnabled;
      auto lock = outSurface.Lock();
      
      if (lastWasSurfaceFiltered) {
        filteredTex.Update(std::nullopt, lock.GetPixels(), lock.GetPitch());
      } else {
        texture.Update(std::nullopt, lock.GetPixels(), lock.GetPitch());
      }
    }
    
    // ── Draw ──────────────────────────────────────────────────────────────
    
    if (!hasNewFrame) {
      renderer.SetDrawColor({255, 100, 50, 255});
      renderer.SetScale({1.5f, 1.5f});
      renderer.RenderDebugText({8.f, 56.0f}, "Please wait, filtering in progress...");
      renderer.SetScale({1.0f, 1.0f});
    }

    if (texture && filteredTex) {
      renderer.RenderTexture(lastWasSurfaceFiltered ? filteredTex : texture, std::nullopt, std::nullopt);
    }

    // ── HUD ───────────────────────────────────────────────────────────────
    DrawHud();

    renderer.Present();
    return SDL::APP_CONTINUE;
  }

  void DrawHud() {
    // Dark translucent background bar.
    const SDL::Point winSz = window.GetSize();
    renderer.SetDrawColor({0, 0, 0, 140});
    renderer.RenderFillRect(SDL::FRect{0.f, 0.f,
                                      static_cast<float>(winSz.x), 56.f});

    renderer.SetDrawColor({230, 230, 255, 255});

    if (!cameraReady) {
      renderer.RenderDebugText({8.f, 8.f},
        "Waiting for camera permission...");
      renderer.RenderDebugText({8.f, 20.f},
        "Check your OS permission dialog.");
      return;
    }

    const std::string filterLabel = 
      std::format("{}[{}/{}] {}",
                  filterEnabled ? "" : "[BYPASS] Raw camera feed, ",
                  registry.CurrentIndex() + 1,
                  registry.Count(),
                  registry.CurrentName());

    renderer.RenderDebugText({8.f, 8.f}, filterLabel);
    renderer.RenderDebugTextFormat({8.f, 24.f},
      "LEFT / RIGHT = cycle filter   "
                  "SPACE = {}   ESC = quit",
                  filterEnabled ? "bypass" : "enable filter");

    // Draw simple left/right arrow indicators.
    renderer.SetDrawColor({100, 200, 255, 255});
    renderer.RenderDebugText({8.f, 40.f}, "< PREV");
    renderer.RenderDebugText({static_cast<float>(winSz.x) - 60.f, 40.f},
                             "NEXT >");
  }

  // ── Cleanup ───────────────────────────────────────────────────────────────

  static void Quit(Main* m, SDL::AppResult) {
    delete m;
  }
};

SDL3PP_DEFINE_CALLBACKS(Main)