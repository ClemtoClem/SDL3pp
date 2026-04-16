/*
 * This example creates an SDL window and renderer, and then draws a scene
 * to it every frame, while sliding around a clipping rectangle.
 *
 * This code is public domain. Feel free to use it for any purpose!
 *
 * Originally from cliprect.c on SDL's examples
 */
#include <SDL3pp/SDL3pp.h>

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>

struct Main {
	static constexpr SDL::Point windowSz = {640, 480};
	static constexpr int cliprect_size = 250;
	static constexpr float cliprect_speed = 200; // pixels per second

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
		
		SDL::SetAppMetadata("Example Renderer Clipping Rectangle",
												"1.0",
												"com.example.renderer-cliprect");
		SDL::Init(SDL::INIT_VIDEO);
		*m = new Main();
		return SDL::APP_CONTINUE;
	}

	static void Quit(Main* m, SDL::AppResult) {
		delete m;
	}

	// ── Members ─────────────────────────────────────────────────────────
	SDL::Window window{"examples/renderer/cliprect", windowSz};
	SDL::Renderer renderer{window};

	/* Textures are pixel data that we upload to the video hardware for fast
		drawing. Lots of 2D engines refer to these as "sprites." We'll do a static
		texture (upload once, draw many times) with data from a bitmap file. */
	SDL::Texture texture;

	SDL::FPoint cliprect_position;
	SDL::FPoint cliprect_direction{1, 1};
	SDL::Nanoseconds last_time = SDL::GetTicks();
	
	// ── Constructor ──────────────────────────────────────────────────────
	Main() {
		texture = SDL::CheckError(SDL::LoadTexture(
			renderer,
			std::format("{}../../../assets/textures/sample.png", SDL::GetBasePath())));
	}

	// ── Per-frame logic ───────────────────────────────────────────────────
	SDL::AppResult Iterate() {
		const SDL::Rect cliprect{SDL::Point(cliprect_position),
														 SDL::Point{cliprect_size, cliprect_size}};
		const SDL::Nanoseconds now = SDL::GetTicks();
		const float elapsed = SDL::ToSeconds(now - last_time);
		const float distance = elapsed * cliprect_speed;

		// Set a new clipping rectangle position
		renderer.SetClipRect(cliprect);
		cliprect_position += cliprect_direction * (float)distance;
		if (cliprect_position.x < 0.0f) {
			cliprect_position.x = 0.0f;
			cliprect_direction.x = 1.0f;
		} else if (cliprect_position.x >= (windowSz.x - cliprect_size)) {
			cliprect_position.x = (windowSz.x - cliprect_size) - 1;
			cliprect_direction.x = -1.0f;
		}
		if (cliprect_position.y < 0.0f) {
			cliprect_position.y = 0.0f;
			cliprect_direction.y = 1.0f;
		} else if (cliprect_position.y >= (windowSz.y - cliprect_size)) {
			cliprect_position.y = (windowSz.y - cliprect_size) - 1;
			cliprect_direction.y = -1.0f;
		}
		last_time = now;

		// okay, now draw!

		// Note that SDL_RenderClear is _not_ affected by the clipping rectangle!
		renderer.SetDrawColor(SDL::Color{33, 33, 33}); // grey, full alpha
		renderer.RenderClear();                        // start with a blank canvas.

		// stretch the texture across the entire window. Only the piece in the
		// clipping rectangle will actually render, though!
		renderer.RenderTexture(texture, std::nullopt, std::nullopt);

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
