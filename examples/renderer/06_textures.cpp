/*
 * This example creates an SDL window and renderer, and then draws some
 * textures to it every frame.
 *
 * This code is public domain. Feel free to use it for any purpose!
 *
 * Originally from textures.c on SDL's examples
 */

#include <SDL3pp/SDL3pp.h>

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>

struct Main {
  // Window size
  static constexpr SDL::Point windowSz = {640, 480};

  // Init library
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
    
    SDL::SetAppMetadata(
      "Example Renderer Textures", "1.0", "com.example.renderer-textures");
    SDL::Init(SDL::INIT_VIDEO);
    *m = new Main();
    return SDL::APP_CONTINUE;
  }

  static void Quit(Main* m, SDL::AppResult) {
    delete m;
  }

  static SDL::Window InitAndCreateWindow() {
    return SDL::CreateWindowAndRenderer(
      "examples/renderer/textures", windowSz, 0, nullptr);
  }

  // We will use this renderer to draw into this window every frame.
  SDL::Window window{InitAndCreateWindow()};
  SDL::RendererRef renderer{window.GetRenderer()};

  /* Textures are pixel data that we upload to the video hardware for fast
     drawing. Lots of 2D engines refer to these as "sprites." We'll do a static
     texture (upload once, draw many times) with data from a bitmap file. */
  SDL::Texture texture;
  Main() {
    texture = SDL::CheckError(SDL::LoadTexture(
      renderer,
      std::format("{}../../../assets/textures/sample.png", SDL::GetBasePath())));
  }

  SDL::AppResult Iterate() {
    SDL::FRect dst_rect;
    const float now = SDL::ToSeconds(SDL::GetTicks());

    // we'll have some textures move around over a few seconds.
    const float direction = SDL::Fmod(now, 2.0f) > 1.f ? 1.0f : -1.0f;
    const float scale = (SDL::Fmod(now, 1.0f) - 0.5f) / 0.5f * direction;

    // as you can see, rendering draws over what was drawn before it.
    renderer.SetDrawColor(SDL::Color{0, 0, 0}); // black
    renderer.RenderClear();                     // start with a blank canvas.

    int texture_width = texture.GetWidth();
    int texture_height = texture.GetHeight();

    /* Just draw the static texture a few times. You can think of it like a
       stamp, there isn't a limit to the number of times you can draw with it.
     */

    // top left
    dst_rect.x = (100.0f * scale);
    dst_rect.y = 0.0f;
    dst_rect.w = (float)texture_width;
    dst_rect.h = (float)texture_height;
    renderer.RenderTexture(texture, std::nullopt, dst_rect);

    //  center this one.
    dst_rect.x = ((float)(windowSz.x - texture_width)) / 2.0f;
    dst_rect.y = ((float)(windowSz.y - texture_height)) / 2.0f;
    dst_rect.w = (float)texture_width;
    dst_rect.h = (float)texture_height;
    renderer.RenderTexture(texture, std::nullopt, dst_rect);

    // bottom right
    dst_rect.x = ((float)(windowSz.x - texture_width)) - (100.0f * scale);
    dst_rect.y = (float)(windowSz.y - texture_height);
    dst_rect.w = (float)texture_width;
    dst_rect.h = (float)texture_height;
    renderer.RenderTexture(texture, std::nullopt, dst_rect);

    renderer.Present();       // put it all on the screen!
    return SDL::APP_CONTINUE; // carry on with the program!
  }
};

SDL3PP_DEFINE_CALLBACKS(Main)
