#pragma once
#include "Components.hpp"
#include "../Core/Camera.hpp"
#include <SDL3pp/SDL3pp_stdinc.h>
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_keyboard.h>
#include <SDL3pp/SDL3pp_events.h>
#include <cmath>
#include <algorithm>

namespace game {

namespace Systems {

using World = SDL::ECS::World;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

inline float dist2(float ax, float ay, float bx, float by) noexcept {
    float dx = ax - bx, dy = ay - by;
    return dx*dx + dy*dy;
}

// Convert a box in source-frame pixels to world-unit bounds centred on (cx, cy).
// boxTileSize = source frame size (e.g. 64 px per tile in the sprite sheet)
// worldScale  = 1 world unit = worldScale source pixels
inline SDL::FBox BoxToWorld(const SDL::FRect& b, float cx, float cy, int boxTileSize) noexcept {
    float half  = boxTileSize * 0.5f;
    float left  = cx + (b.x - half) / boxTileSize;
    float top   = cy + (b.y - half) / boxTileSize;
    float right = left + b.w / boxTileSize;
    float bot   = top  + b.h / boxTileSize;
    return { left, top, right, bot };
}

// ─────────────────────────────────────────────────────────────────────────────
// PlayerInput — reads keyboard and sets velocity for the player entity
// ─────────────────────────────────────────────────────────────────────────────

inline void PlayerInput(World& world, std::span<const bool> keys) {
    world.Each<PlayerTag, Transform, Velocity, DirectionComp, SpeedComp, SpriteAnim>(
        [&](SDL::ECS::EntityId, PlayerTag&, Transform&,
            Velocity& vel, DirectionComp& dir, SpeedComp& spd, SpriteAnim& anim) {

        float speed = spd.running ? spd.runSpeed : spd.walkSpeed;
        vel.vx = 0.f; vel.vy = 0.f;

        if (keys[SDL::SCANCODE_LEFT]  || keys[SDL::SCANCODE_A]) { vel.vx = -speed; dir.dir = Direction::West; }
        if (keys[SDL::SCANCODE_RIGHT] || keys[SDL::SCANCODE_D]) { vel.vx =  speed; dir.dir = Direction::East; }
        if (keys[SDL::SCANCODE_UP]    || keys[SDL::SCANCODE_W]) { vel.vy = -speed; dir.dir = Direction::North; }
        if (keys[SDL::SCANCODE_DOWN]  || keys[SDL::SCANCODE_S]) { vel.vy =  speed; dir.dir = Direction::South; }

        anim.moving = (vel.vx != 0.f || vel.vy != 0.f);

        // Diagonal normalization
        if (vel.vx != 0.f && vel.vy != 0.f) {
            float inv = speed / SDL::Sqrt(2.f);
            vel.vx = (vel.vx > 0) ? inv : -inv;
            vel.vy = (vel.vy > 0) ? inv : -inv;
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision — resolve tile collisions; update CollisionResult facing
// ─────────────────────────────────────────────────────────────────────────────

inline void Collision(World& world, const core::MapData& map) {
    world.Each<Transform, Velocity, DirectionComp, CollisionBoxes>(
        [&](SDL::ECS::EntityId, Transform& t, Velocity& vel,
            DirectionComp& dir, CollisionBoxes& boxes) {

        auto& step = boxes.step[(int)dir.dir];
        int ts = boxes.tileSize;

        auto isTileBlocking = [&](float cx, float cy) {
            auto box = BoxToWorld(step, cx, cy, ts);
            for (int ty = (int)SDL::Floor(box.top); ty <= (int)SDL::Floor(box.bottom); ++ty) {
                for (int tx = (int)SDL::Floor(box.left); tx <= (int)SDL::Floor(box.right); ++tx) {
                    if (core::IsBlocking(map.GetCollision(tx, ty))) return true;
                }
            }
            return false;
        };

        // Try X movement
        if (vel.vx != 0.f && !isTileBlocking(t.x + vel.vx, t.y))
            t.x += vel.vx;

        // Try Y movement
        if (vel.vy != 0.f && !isTileBlocking(t.x, t.y + vel.vy))
            t.y += vel.vy;

        // Clamp to map bounds
        t.x = std::clamp(t.x, 0.5f, (float)map.width  - 0.5f);
        t.y = std::clamp(t.y, 0.5f, (float)map.height - 0.5f);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// FacingCollision — resolve which tile is directly in front of each entity
// ─────────────────────────────────────────────────────────────────────────────

inline void FacingCollision(World& world, const core::MapData& map) {
    world.Each<Transform, DirectionComp, CollisionResult>(
        [&](SDL::ECS::EntityId, Transform& t, DirectionComp& dir, CollisionResult& res) {
        float fx = t.x, fy = t.y;
        switch (dir.dir) {
            case Direction::North: fy -= 1.f; break;
            case Direction::South: fy += 1.f; break;
            case Direction::West:  fx -= 1.f; break;
            case Direction::East:  fx += 1.f; break;
        }
        res.facing = map.GetCollision((int)std::round(fx), (int)std::round(fy));
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Animation — advance sprite frame
// ─────────────────────────────────────────────────────────────────────────────

inline void Animation(World& world) {
    world.Each<SpriteAnim, DirectionComp>(
        [&](SDL::ECS::EntityId, SpriteAnim& anim, DirectionComp& dir) {
        if (!anim.moving) {
            anim.currentCol = anim.firstIsStand ? 0 : 1;
            anim.tickCounter = 0;
            return;
        }
        anim.tickCounter++;
        if (anim.tickCounter >= anim.framesPerCol) {
            anim.tickCounter = 0;
            int startCol = anim.firstIsStand ? 1 : 0;
            anim.currentCol++;
            if (anim.currentCol >= anim.nbCols) anim.currentCol = startCol;
        }
        (void)dir;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// MobAI — state machine for non-player entities
// ─────────────────────────────────────────────────────────────────────────────

inline void MobAISystem(World& world, float dt) {
    // Find player position
    float playerX = -1.f, playerY = -1.f;
    SDL::ECS::EntityId playerId = SDL::ECS::NullEntity;
    world.Each<PlayerTag, Transform>([&](SDL::ECS::EntityId eid, PlayerTag&, Transform& pt) {
        playerX = pt.x; playerY = pt.y; playerId = eid;
    });

    world.Each<MobAI, Transform, Velocity, DirectionComp, SpriteAnim>(
        [&](SDL::ECS::EntityId, MobAI& ai, Transform& t,
            Velocity& vel, DirectionComp& dir, SpriteAnim& anim) {

        ai.stateTimer    = std::max(0.f, ai.stateTimer - dt);
        ai.attackCooldown = std::max(0.f, ai.attackCooldown - dt);
        vel.vx = vel.vy  = 0.f;
        anim.moving      = false;

        if (ai.state == AIState::Dead) return;

        float d2 = (playerX >= 0) ? dist2(t.x, t.y, playerX, playerY) : 1e9f;

        switch (ai.state) {
            case AIState::Idle: {
                if (ai.stateTimer <= 0.f) {
                    ai.state = AIState::Patrol;
                    ai.stateTimer = 2.f + (float)(rand() % 300) / 100.f;
                    float angle = (float)(rand() % 628) / 100.f;
                    ai.patrolTargX = ai.spawnX + std::cos(angle) * ai.patrolRadius;
                    ai.patrolTargY = ai.spawnY + std::sin(angle) * ai.patrolRadius;
                }
                // Fall through to chase check
                if (playerX >= 0 && d2 < ai.chaseRadius * ai.chaseRadius)
                    ai.state = AIState::Chase;
                break;
            }
            case AIState::Patrol: {
                if (playerX >= 0 && d2 < ai.chaseRadius * ai.chaseRadius) {
                    ai.state = AIState::Chase; break;
                }
                float dx = ai.patrolTargX - t.x, dy = ai.patrolTargY - t.y;
                float d  = std::sqrt(dx*dx + dy*dy);
                if (d < 0.2f || ai.stateTimer <= 0.f) {
                    ai.state = AIState::Idle;
                    ai.stateTimer = 1.f;
                } else {
                    float inv = 0.025f / d;
                    vel.vx = dx * inv; vel.vy = dy * inv;
                    anim.moving = true;
                    if (std::abs(dx) > std::abs(dy))
                        dir.dir = (dx > 0) ? Direction::East : Direction::West;
                    else
                        dir.dir = (dy > 0) ? Direction::South : Direction::North;
                }
                break;
            }
            case AIState::Chase: {
                if (playerX < 0 || d2 > (ai.chaseRadius * 1.5f) * (ai.chaseRadius * 1.5f)) {
                    ai.state = AIState::Idle; ai.stateTimer = 1.f; break;
                }
                if (d2 < ai.attackRange * ai.attackRange) {
                    ai.state = AIState::Attack; break;
                }
                float dx = playerX - t.x, dy = playerY - t.y;
                float d  = std::sqrt(dx*dx + dy*dy);
                float inv = 0.025f / d;
                vel.vx = dx * inv; vel.vy = dy * inv;
                anim.moving = true;
                if (std::abs(dx) > std::abs(dy))
                    dir.dir = (dx > 0) ? Direction::East : Direction::West;
                else
                    dir.dir = (dy > 0) ? Direction::South : Direction::North;
                ai.targetEntity = playerId;
                break;
            }
            case AIState::Attack: {
                if (playerX < 0 || d2 > ai.attackRange * ai.attackRange * 4.f) {
                    ai.state = AIState::Chase; break;
                }
                if (ai.attackCooldown <= 0.f) {
                    // Deal damage to player
                    if (world.IsAlive(playerId)) {
                        if (auto* ph = world.Get<HealthComp>(playerId)) {
                            if (!ph->sick && ph->invincibleTimer <= 0.f) {
                                ph->hp = std::max(0.f, ph->hp - 5.f);
                                ph->invincibleTimer = 0.5f;
                            }
                        }
                    }
                    ai.attackCooldown = 1.5f;
                }
                break;
            }
            default: break;
        }
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// Health — regen / damage over time, invincibility timer
// ─────────────────────────────────────────────────────────────────────────────

inline void Health(World& world, float dt) {
    world.Each<HealthComp>([&](SDL::ECS::EntityId, HealthComp& h) {
        if (!h.enabled) return;
        h.invincibleTimer = std::max(0.f, h.invincibleTimer - dt);
        if (h.sick)
            h.hp = std::max(0.f, h.hp - h.damagePerSec * dt);
        else if (h.regenPerSec > 0.f)
            h.hp = std::min(h.maxHp, h.hp + h.regenPerSec * dt);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// AutoWalkSystem — move entities on autonomous paths (NPCs)
// ─────────────────────────────────────────────────────────────────────────────

inline void AutoWalkSystem(World& world) {
    world.Each<AutoWalk, Transform, Velocity, DirectionComp, SpriteAnim>(
        [&](SDL::ECS::EntityId, AutoWalk& aw, Transform& t,
            Velocity& vel, DirectionComp& dir, SpriteAnim& anim) {
        if (!aw.active) { vel.vx = vel.vy = 0.f; return; }
        float dx = aw.targetX - t.x, dy = aw.targetY - t.y;
        float d = std::sqrt(dx*dx + dy*dy);
        if (d < 0.05f) { aw.active = false; vel.vx = vel.vy = 0.f; return; }
        float spd = 0.025f;
        vel.vx = dx / d * spd;
        vel.vy = dy / d * spd;
        anim.moving = true;
        if (std::abs(dx) > std::abs(dy))
            dir.dir = (dx > 0) ? Direction::East : Direction::West;
        else
            dir.dir = (dy > 0) ? Direction::South : Direction::North;
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// CameraFollow — move camera to track player
// ─────────────────────────────────────────────────────────────────────────────

inline void CameraFollow(World& world, core::Camera& cam,
                          int mapW, int mapH, float lerp = 0.12f) {
    world.Each<PlayerTag, Transform>([&](SDL::ECS::EntityId, PlayerTag&, Transform& t) {
        cam.Follow(t.x, t.y, mapW, mapH, lerp);
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// RunAll — convenience: run every system in order for one game tick
// ─────────────────────────────────────────────────────────────────────────────

inline void RunAll(World& world, const core::MapData& map,
                   core::Camera& cam, float dt) {
    std::span<const bool> keys = SDL::GetKeyboardState();
    PlayerInput(world, keys);
    AutoWalkSystem(world);
    Collision(world, map);
    FacingCollision(world, map);
    Animation(world);
    MobAISystem(world, dt);
    Health(world, dt);
    CameraFollow(world, cam, map.width, map.height);
}

} // namespace Systems
} // namespace game
