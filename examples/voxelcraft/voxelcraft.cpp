#define SDL3PP_MAIN_USE_CALLBACKS 1
#define SDL3PP_ENABLE_IMAGE 1
#define SDL3PP_ENABLE_TTF 1
#define SDL3PP_ENABLE_MIXER 1

#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_mixer.h>
#include <SDL3pp/SDL3pp_ttf.h>
#include "voxel/Game.h"

struct App {
    static SDL::AppResult Init(App** self, SDL::AppArgs args) {
        SDL::SetLogPriorities(SDL::LOG_PRIORITY_WARN);
        for (auto arg : args) {
            if (arg == "--trace")   SDL::SetLogPriorities(SDL::LOG_PRIORITY_TRACE);
            if (arg == "--debug")   SDL::SetLogPriorities(SDL::LOG_PRIORITY_DEBUG);
            if (arg == "--info")    SDL::SetLogPriorities(SDL::LOG_PRIORITY_INFO);
            if (arg == "--verbose") SDL::SetLogPriorities(SDL::LOG_PRIORITY_VERBOSE);
        }
        SDL::SetAppMetadata("Voxelcraft", "1.0", "com.example.voxelcraft");
        SDL::Init(SDL::INIT_VIDEO | SDL::INIT_AUDIO);
        SDL::TTF::Init();
        SDL::MIX::Init();
        *self = new App();
        return SDL::APP_CONTINUE;
    }

    static void Quit(App* self, SDL::AppResult) {
        SDL::MIX::Quit();
        SDL::TTF::Quit();
        delete self;
    }

    Game game;

    // Instance methods required by HasIterateFunction / HasEventFunction concepts
    SDL::AppResult Iterate() { return game.Iterate(); }
    SDL::AppResult Event(const SDL::Event& ev) { return game.Event(ev); }
};

SDL3PP_DEFINE_CALLBACKS(App)
