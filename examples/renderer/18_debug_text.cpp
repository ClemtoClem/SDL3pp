/*
 * This example creates an SDL window and renderer, and then draws some text
 * using SDL_RenderDebugText() every frame.
 *
 * This code is public domain. Feel free to use it for any purpose!
 *
 * Originally from debug-text.c on SDL's examples
 */
#include <SDL3pp/SDL3pp.h>

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>

struct Main {
  static constexpr SDL::Point windowSz = {640, 480};

  // ── Initialisation helpers ──────────────────────────────────────────
  static SDL::AppResult Init(Main** m, SDL::AppArgs args) {
    SDL::LogPriority priority = SDL::LOG_PRIORITY_WARN;
    for (auto arg : args) {
      if (arg == "--verbose") priority = SDL::LOG_PRIORITY_VERBOSE;
      else if (arg == "--debug") priority = SDL::LOG_PRIORITY_DEBUG;
      else if (arg == "--info") priority = SDL::LOG_PRIORITY_INFO;
      else if (arg == "--help") {
        SDL::Log("Usage: %s [options]", SDL::GetBasePath());
        SDL::Log("Options:");
        SDL::Log("  --verbose    Set log priority to verbose");
        SDL::Log("  --debug      Set log priority to debug");
        SDL::Log("  --info       Set log priority to info");
        SDL::Log("  --help       Show this help message");
        return SDL::APP_EXIT_SUCCESS;
      }
    }
    SDL::SetLogPriorities(priority);
    
    SDL::SetAppMetadata("Example Debug Text", "1.0", "com.example.debug-text");
    SDL::Init(SDL::INIT_VIDEO);
    *m = new Main();
    return SDL::APP_CONTINUE;
  }

  static void Quit(Main* m, SDL::AppResult) {
    delete m;
  }

  // ── Members ─────────────────────────────────────────────────────────
  SDL::Window window{"examples/renderer/debug-text", windowSz};
  SDL::Renderer renderer{window};

  // ── Per-frame logic ───────────────────────────────────────────────────
  SDL::AppResult Iterate() {
    constexpr int charsize = SDL_DEBUG_TEXT_FONT_CHARACTER_SIZE;

    // as you can see, rendering draws over what was drawn before it.
    renderer.SetDrawColor(SDL::Color{0, 0, 0}); // black
    renderer.RenderClear();                     // start with a blank canvas.

    renderer.SetDrawColor(SDL::Color{255, 255, 255}); // white
    renderer.RenderDebugText({272, 100}, "Hello ECS::World!");
    renderer.RenderDebugText({224, 150}, "This is some debug text.");

    renderer.SetDrawColor(SDL::Color{51, 102, 255}); // light blue
    renderer.RenderDebugText({184, 200}, "You can do it in different colors.");
    renderer.SetDrawColor(SDL::Color{255, 255, 255}); // white

    renderer.SetScale({4, 4});
    renderer.RenderDebugText({14, 65}, "It can be scaled.");
    renderer.SetScale({1, 1});
    renderer.RenderDebugText( {64, 350},
      "This only does ASCII chars. So this laughing emoji won't draw: 🤣");

    renderer.RenderDebugTextFormat(
      SDL::FPoint{(windowSz.x - (charsize * 46)) / 2.f, 400.f},
      "(This program has been running for {} seconds.)",
      Sint64(SDL::ToSeconds(SDL::GetTicks())));

    renderer.Present();       // put it all on the screen!
    return SDL::APP_CONTINUE; // carry on with the program!
  }

  // ── Event handling ────────────────────────────────────────────────────
  SDL::AppResult Event(const SDL::Event& event) {
    if (event.type == SDL::EVENT_QUIT) {
      return SDL::APP_EXIT_SUCCESS;
    }
    return SDL::APP_CONTINUE;
  }
};

SDL3PP_DEFINE_CALLBACKS(Main)
