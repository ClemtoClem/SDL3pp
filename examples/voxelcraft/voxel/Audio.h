#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  Audio.h – Gestionnaire audio : ambiance, sons de blocs
// ══════════════════════════════════════════════════════════════════════════
#include "Blocks.h"

#include <SDL3pp/SDL3pp_resources.h>
#include <SDL3pp/SDL3pp_mixer.h>
#include <SDL3/SDL_timer.h>

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <random>

class AudioManager {
private:
    SDL::Mixer mixer;
    std::string base;

    std::unique_ptr<SDL::ResourcePool> ambiencePool;
    std::unique_ptr<SDL::ResourcePool> soundsPool;

    std::map<std::string, std::vector<std::string>> ambienceThemes;
    std::vector<std::string> allAmbienceKeys;

    std::map<std::string, std::map<std::string, std::vector<std::string>>> groups;

    Uint64 nextAmbience = 0;
    std::mt19937 rng{std::random_device{}()};

public:
    AudioManager() {
        SDL::AudioSpec spec{};
        spec.freq     = 44100;
        spec.format   = SDL_AUDIO_F32;
        spec.channels = 2;

        try {
            mixer = SDL::CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec);
        } catch (const std::exception& e) {
            SDL::LogError(SDL::LOG_CATEGORY_APPLICATION, "AudioManager: %s", e.what());
            return;
        }

        ambiencePool = std::make_unique<SDL::ResourcePool>("ambience");
        soundsPool   = std::make_unique<SDL::ResourcePool>("sounds");

        base = std::string(SDL::GetBasePath()) + "../../../assets/sounds/game/";

        // Ambience themes
        LoadAmbienceTheme("cave", "cave", 9);
        LoadAmbienceTheme("cave", "creepy_cave", 1);
        LoadAmbienceTheme("cave", "rocks_falling_cave", 1);
        LoadAmbienceTheme("cave", "water_drips_cave", 2);
        LoadAmbienceTheme("cave", "wind_cave", 5);
        LoadAmbienceTheme("cave", "monster_sigh_cave", 1);
        LoadAmbienceTheme("ghost", "ghost", 6);
        LoadAmbienceTheme("graveyard", "graveyard", 10);
        LoadAmbienceTheme("night", "night_crickets", 5);
        LoadAmbienceTheme("night", "owl", 1);
        LoadAmbienceTheme("volcanic", "hot_stone", 2);
        LoadAmbienceTheme("volcanic", "sulfur", 4);
        LoadAmbienceTheme("ice", "ice_cracking", 3);
        LoadAmbienceTheme("snow", "snow_storm", 4);
        LoadAmbienceTheme("swamp", "swamp", 4);

        auto setup = [&](std::string_view act, std::string_view grp, std::string_view pref, int count) {
            LoadGroup(soundsPool.get(), act, grp, pref, count);
        };

        setup("hit", "dirt",   "default_dirt_hit",   3);
        setup("hit", "grass",  "default_grass_hit",  3);
        setup("hit", "stone",  "default_stone_hit",  3);
        setup("hit", "wood",   "default_wood_hit",   6);
        setup("dug", "dirt",   "default_dirt_hit",   3);
        setup("dug", "stone",  "default_stone_dug",  3);
        setup("dug", "gravel", "default_gravel_dug", 4);
        setup("place", "dirt",  "default_dirt_footstep", 4);
        setup("place", "stone", "default_stone_footstep", 5);
        setup("footstep", "dirt",  "default_grass_footstep", 3);
        setup("footstep", "grass", "default_dirt_footstep", 4);
        setup("footstep", "stone", "default_stone_footstep", 5);

        ScheduleAmbience();
    }

    void Update() {
        if (allAmbienceKeys.empty() || !ambiencePool) return;
        Uint64 now = SDL_GetTicks();
        if (now >= nextAmbience) {
            PlayRandomKey(ambiencePool.get(), allAmbienceKeys);
            ScheduleAmbience();
        }
    }

    void PlayBlockBreak(BlockID id) { PlayGroupAction(std::string(blockDef(id).soundGroup), "dug"); }
    void PlayBlockPlace(BlockID id) { PlayGroupAction(std::string(blockDef(id).soundGroup), "place"); }
    void PlayBlockHit(BlockID id)   { PlayGroupAction(std::string(blockDef(id).soundGroup), "hit"); }
    void PlayFootstep(BlockID surface) { PlayGroupAction(std::string(blockDef(surface).soundGroup), "footstep"); }

private:
    void PlayGroupAction(const std::string& group, const std::string& action) {
        auto itGroup = groups.find(group);
        if (itGroup == groups.end()) return;
        auto itAction = itGroup->second.find(action);
        if (itAction != itGroup->second.end())
            PlayRandomKey(soundsPool.get(), itAction->second);
    }

    void TryLoad(SDL::ResourcePool* pool, const std::string& path) {
        if (!mixer) return;
        try {
            pool->Add<SDL::Audio>(path, mixer.LoadAudio(path, true));
        } catch (...) {}
    }

    void LoadAmbienceTheme(std::string_view theme, std::string_view subName, int count) {
        std::string sTheme(theme);
        for (int i = 1; i <= count; ++i) {
            std::string fileName = (count == 1)
                ? std::format("default_ambience_{}.ogg", subName)
                : std::format("default_ambience_{}_{}.ogg", subName, i);
            std::string path = base + fileName;
            TryLoad(ambiencePool.get(), path);
            ambienceThemes[sTheme].push_back(path);
            allAmbienceKeys.push_back(path);
        }
    }

    void LoadGroup(SDL::ResourcePool* pool,
                   std::string_view action, std::string_view group,
                   std::string_view prefix, int count) {
        std::string sGroup(group), sAction(action);
        for (int i = 1; i <= count; ++i) {
            std::string path = std::format("{}{}.{}.ogg", base, prefix, i);
            TryLoad(pool, path);
            groups[sGroup][sAction].push_back(path);
        }
    }

    void PlayRandomKey(SDL::ResourcePool* pool, const std::vector<std::string>& keys) {
        if (keys.empty() || !pool || !mixer) return;
        std::uniform_int_distribution<size_t> pick(0, keys.size() - 1);
        auto handle = pool->Get<SDL::Audio>(keys[pick(rng)]);
        if (handle) {
            try { mixer.PlayAudio(*handle); } catch (...) {}
        }
    }

    void ScheduleAmbience() {
        std::uniform_int_distribution<Uint64> d(20000, 50000);
        nextAmbience = SDL_GetTicks() + d(rng);
    }
};
