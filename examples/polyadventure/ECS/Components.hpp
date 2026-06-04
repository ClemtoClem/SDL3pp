#pragma once
#include "../Core/MapLoader.hpp"
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_render.h>
#include <array>
#include <string>
#include <vector>

namespace game {

using namespace core;

// ─────────────────────────────────────────────────────────────────────────────
// Entity types (mirrors original C enum)
// ─────────────────────────────────────────────────────────────────────────────

enum class EntityType {
	Default, Player, Pnj, Blob, Dragon, Goblin, Skeleton
};

// ─────────────────────────────────────────────────────────────────────────────
// Transform — position in world-tile units (center of entity)
// ─────────────────────────────────────────────────────────────────────────────

struct Transform {
	float x = 0.f, y = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Velocity — per-frame displacement in world units
// ─────────────────────────────────────────────────────────────────────────────

struct Velocity {
	float vx = 0.f, vy = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// DirectionComp
// ─────────────────────────────────────────────────────────────────────────────

struct DirectionComp {
	Direction dir = Direction::South;
};

// ─────────────────────────────────────────────────────────────────────────────
// SpeedComp
// ─────────────────────────────────────────────────────────────────────────────

struct SpeedComp {
	float walkSpeed = 0.05f;
	float runSpeed  = 0.1f;
	bool  running   = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// EntityTag — type and display name
// ─────────────────────────────────────────────────────────────────────────────

struct EntityTag {
	EntityType  type = EntityType::Default;
	std::string name;
};

// ─────────────────────────────────────────────────────────────────────────────
// PlayerTag — marks the player entity (used in queries)
// ─────────────────────────────────────────────────────────────────────────────

struct PlayerTag { int polypoints = 0; };

// ─────────────────────────────────────────────────────────────────────────────
// SpriteAnim — sprite sheet animation
//
// Sheet layout:
//   Row 0 = North, Row 1 = South, Row 2 = West, Row 3 = East
//   Col 0 = standing (if firstIsStand=true), Col 1..N = walk frames
// ─────────────────────────────────────────────────────────────────────────────

struct SpriteAnim {
	SDL::TextureRef texture;
	int   tileSize     = 64;  // source frame size (px)
	int   nbCols       = 3;   // animation frames per row
	float scale         = 1.5f;// display scale relative to dispTileSize
	int   currentCol   = 0;   // current animation column
	float timer        = 0.f;
	int   framesPerCol = 5;   // game ticks before advancing frame
	int   tickCounter  = 0;
	bool  firstIsStand = true;
	bool  moving       = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// CollisionBoxes — body and step boxes in source-frame pixel coords
//
// The box is stored as [x, y, w, h] in the 64×64 tile frame.
// 4 entries = one per direction (N, S, W, E).
// ─────────────────────────────────────────────────────────────────────────────

struct CollisionBoxes {
	SDL::FRect body[4] = {};
	SDL::FRect step[4] = {};
	int        tileSize = 64;
};

// ─────────────────────────────────────────────────────────────────────────────
// HealthComp
// ─────────────────────────────────────────────────────────────────────────────

struct HealthComp {
	float hp         = 100.f;
	float maxHp      = 100.f;
	float regenPerSec = 0.f;
	float damagePerSec = 0.f;
	bool  sick       = false;
	bool  enabled    = true;
	float invincibleTimer = 0.f; // invincibility frames after being hit
};

// ─────────────────────────────────────────────────────────────────────────────
// MobAI — behavior state machine
// ─────────────────────────────────────────────────────────────────────────────

enum class AIState { Idle, Patrol, Chase, Attack, Dead };

struct MobAI {
	AIState state        = AIState::Idle;
	float   patrolRadius = 3.f;
	float   chaseRadius  = 8.f;
	float   attackRange  = 0.8f;
	float   stateTimer   = 0.f;
	float   spawnX       = 0.f;
	float   spawnY       = 0.f;
	float   patrolTargX  = 0.f;
	float   patrolTargY  = 0.f;
	float   pushForce    = 8.f;
	float   attackCooldown = 0.f;
	SDL::ECS::EntityId targetEntity = SDL::ECS::NullEntity;
};

// ─────────────────────────────────────────────────────────────────────────────
// AutoWalk — autonomous movement towards a target
// ─────────────────────────────────────────────────────────────────────────────

struct AutoWalk {
	float targetX   = 0.f;
	float targetY   = 0.f;
	float remaining = 0.f;
	bool  active    = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// Dialogue — NPC dialogue lines
// ─────────────────────────────────────────────────────────────────────────────

struct Dialogue {
	std::vector<std::string> lines;
	bool  active  = false;
	int   current = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// CollisionResult — set by CollisionSystem each frame
// ─────────────────────────────────────────────────────────────────────────────

struct CollisionResult {
	CollisionId facing       = CollisionId::None; // tile in front of entity
	bool        fromOther    = false; // collided with another entity
	bool        stepFromOther= false;
};

} // namespace game
