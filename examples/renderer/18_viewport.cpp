/*
 * This example creates an SDL window and renderer, and then draws some
 * textures to it every frame, adjusting the viewport.
 *
 * This code is public domain. Feel free to use it for any purpose!
 *
 * Originally from viewport.c on SDL's examples
 */
#include <SDL3pp/SDL3pp.h>

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>

struct Main {
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
			"Example Renderer Viewport", "1.0", "com.example.renderer-viewport");
		SDL::Init(SDL::INIT_VIDEO);
		*m = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
	}

	SDL::Window window{"examples/renderer/viewport", windowSz};
	SDL::Renderer renderer{window};

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
		/* Setting a viewport has the effect of limiting the area that rendering
			 can happen, and making coordinate (0, 0) live somewhere else in the
			 window. It does _not_ scale rendering to fit the viewport. */

		// as you can see, rendering draws over what was drawn before it.
		renderer.SetDrawColor(SDL::Color{0, 0, 0}); // black
		renderer.RenderClear();                     // start with a blank canvas.

		SDL::FRect dst_rect = {{0, 0}, texture.GetSize()};

		// Draw once with the whole window as the viewport.
		renderer.ResetViewport();
		renderer.RenderTexture(texture, std::nullopt, dst_rect);

		// top right quarter of the window.
		renderer.SetViewport(SDL::Rect{windowSz / 2, windowSz / 2});
		renderer.RenderTexture(texture, std::nullopt, dst_rect);

		// bottom 20% of the window. Note it clips the width!
		renderer.SetViewport(
			SDL::Rect{{0, windowSz.y - (windowSz.y / 5)}, windowSz / 5});
		renderer.RenderTexture(texture, std::nullopt, dst_rect);

		// what happens if you try to draw above the viewport? It should clip!
		renderer.SetViewport(SDL::Rect{{100, 200}, windowSz});
		dst_rect.y -= 50;
		renderer.RenderTexture(texture, std::nullopt, dst_rect);

		renderer.Present();       // put it all on the screen!
		return SDL::APP_CONTINUE; // carry on with the program!
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
