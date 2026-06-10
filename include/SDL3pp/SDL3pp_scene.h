#ifndef SDL3PP_SCENE_TOPLEVEL_H_
#define SDL3PP_SCENE_TOPLEVEL_H_

/**
 * @file SDL3pp_scene.h
 * @brief Legacy top-level include path for the scene/ECS engine.
 *
 * The engine now lives under `SDL3pp_engine/`. This shim forwards to the full
 * engine umbrella so existing `#include <SDL3pp/SDL3pp_scene.h>` keeps working.
 */

#include "SDL3pp_engine/Engine.h"

#endif // SDL3PP_SCENE_TOPLEVEL_H_
