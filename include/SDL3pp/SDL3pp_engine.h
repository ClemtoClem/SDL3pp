#ifndef SDL3PP_ENGINE_UMBRELLA_H_
#define SDL3PP_ENGINE_UMBRELLA_H_

/**
 * @file SDL3pp_engine.h
 * @brief Top-level umbrella header for the SDL3pp game engine module.
 *
 * Including this single header pulls in the whole engine: the ECS core, 3-D
 * math, mesh/model loading, every built-in component, the spatial structures
 * and all systems, plus the fluent scene builder.
 *
 * ## Module layout
 *
 * | Area        | Header(s) |
 * |-------------|-----------|
 * | ECS core    | `ECS.h` |
 * | Math        | `Math3D.h` |
 * | Geometry    | `Mesh.h`, `ObjLoader.h` |
 * | Components  | `Components.h` (all 2-D + 3-D components) |
 * | Collisions  | `Collisions.h`, `Quadtree.h`, `Octree.h` |
 * | Systems     | `Systems/TransformSystem.h`, `RenderSystem2D.h`, `RenderSystem3D.h`, `AnimationSystem.h`, `ScriptSystem.h`, `CollisionSystem.h`, `RaycastSystem.h`, `SignalSystem.h`, `ProcessSystem.h` |
 * | Authoring   | `SceneBuilder.h` |
 *
 * Components and systems live in `SDL::ECS`; math, meshes and the spatial trees
 * live in `SDL` / `SDL::Physics`.
 *
 * ## A frame, end to end
 *
 * ```cpp
 * SDL::ECS::Context      ctx;
 * SDL::ECS::SceneBuilder scene(ctx, renderer);
 * // …build the scene…
 *
 * // gameplay
 * scene.Update(dt);                              // scripts/anim/transforms/processes
 * auto hits = SDL::ECS::CollisionSystem2D::Detect(ctx);
 *
 * // 2-D draw
 * scene.Render();
 *
 * // 3-D draw (your pipeline + render pass)
 * FMatrix4 vp;
 * SDL::ECS::RenderSystem3D::ComputeViewProj(ctx, aspect, vp);
 * SDL::ECS::RenderSystem3D::Render(ctx, cmd, pass, vp);
 * ```
 */

// ── Core ──────────────────────────────────────────────────────────────────────
#include "SDL3pp_engine/ECS.h"
#include "SDL3pp_engine/Math3D.h"

// ── Geometry & assets ───────────────────────────────────────────────────────────
#include "SDL3pp_engine/Mesh.h"
#include "SDL3pp_engine/ObjLoader.h"

// ── Components ──────────────────────────────────────────────────────────────────
#include "SDL3pp_engine/Components.h"

// ── Spatial / collision primitives ──────────────────────────────────────────────
#include "SDL3pp_engine/Collisions.h"
#include "SDL3pp_engine/Octree.h"
#include "SDL3pp_engine/Quadtree.h"

// ── Systems (one per file) ──────────────────────────────────────────────────────
#include "SDL3pp_engine/Systems/AnimationSystem.h"
#include "SDL3pp_engine/Systems/CollisionSystem.h"
#include "SDL3pp_engine/Systems/ProcessSystem.h"
#include "SDL3pp_engine/Systems/RaycastSystem.h"
#include "SDL3pp_engine/Systems/RenderSystem2D.h"
#include "SDL3pp_engine/Systems/RenderSystem3D.h"
#include "SDL3pp_engine/Systems/ScriptSystem.h"
#include "SDL3pp_engine/Systems/SignalSystem.h"
#include "SDL3pp_engine/Systems/TransformSystem.h"

// ── Authoring ───────────────────────────────────────────────────────────────────
#include "SDL3pp_engine/SceneBuilder.h"

#endif // SDL3PP_ENGINE_H_
