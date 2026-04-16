/*
 * This example creates an SDL window and renderer, and then draws some lines,
 * rectangles and points to it every frame.
 *
 * This code is public domain. Feel free to use it for any purpose!
 *
 * Originally from primitives.c on SDL's examples
 */

#include <SDL3pp/SDL3pp.h>

#define SDL3PP_MAIN_USE_CALLBACKS
#include <SDL3pp/SDL3pp_main.h>

struct Main {
	// Window size
	static constexpr SDL::Point windowSz = {640, 480};

	// Init library
	static SDL::Window InitAndCreateWindow() {
		SDL::SetAppMetadata(
			"Example Renderer Primitives", "1.0", "com.example.renderer-primitives");
		SDL::Init(SDL::INIT_VIDEO);
		return SDL::CreateWindowAndRenderer(
			"examples/renderer/primitives", windowSz, 0, nullptr);
	}

	// We will use this renderer to draw into this window every frame.
	SDL::Window window{InitAndCreateWindow()};
	SDL::RendererRef renderer{window.GetRenderer()};
	std::array<SDL::FPoint, 500> points;

	// This function runs once at startup.
	Main() {
		// set up some random points
		for (auto& point : points) {
			point.x = (SDL_randf() * 440.0f) + 100.0f;
			point.y = (SDL_randf() * 280.0f) + 100.0f;
		}
	}

	SDL::AppResult Iterate() {
		SDL::FPoint center = SDL::FPoint(windowSz)/2.f;
		SDL::FRect rect;

		// as you can see, rendering draws over what was drawn before it.
		renderer.SetDrawColor(SDL::Color{33, 33, 33}); // Dark grey
		renderer.RenderClear();

		// draw a rounded filled rectangle with border with points in the middle of the canvas.
		renderer.SetDrawColor(SDL::Color{0, 0, 255}); // Blue
		rect.x = rect.y = 100;
		rect.w = 440;
		rect.h = 280;
		renderer.RenderFillRoundedRect(rect, SDL::FCorners(8.f));

		renderer.SetDrawColor(SDL::Color{0, 0, 125}); // Dark Blue
		renderer.RenderRoundedBorderedRect(rect, SDL::FBox(12.f), SDL::FCorners(8.f));

		// draw some points across the canvas. */
		renderer.SetDrawColor(SDL::Color{255, 0, 0}); // red
		renderer.RenderPoints(points);

		// draw a unfilled rectangle in-set a little bit. */
		renderer.SetDrawColor(SDL::Color{0, 255, 0}); // green
		rect.x += 30;
		rect.y += 30;
		rect.w -= 60;
		rect.h -= 60;
		renderer.RenderRect(rect);

		// draw two lines in an X across the whole canvas. */
		renderer.SetDrawColor(SDL::Color{255, 255, 0}); // yellow
		renderer.RenderLine({0, 0}, SDL::FPoint(windowSz));
		renderer.RenderLine({0, float(windowSz.y)}, {float(windowSz.x), 0});

		renderer.SetDrawColor(SDL::Color{142, 53, 102});
		renderer.RenderCircle(center, 15.f);
		renderer.RenderEllipse(center, 25.f, 40.f, 0.f);
		renderer.RenderEllipse(center, 25.f, 40.f, 90.f);

		renderer.Present();       // put it all on the screen!
		return SDL::APP_CONTINUE; // carry on with the program!
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
