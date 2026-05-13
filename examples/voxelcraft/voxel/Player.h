#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  Player.h – Contrôleur du joueur FPS avec physique et inventaire
// ═══════════════════════════════════════════════════════════════════════════
#include "Blocks.h"
#include "World.h"
#include <array>
#include <cstdint>

class World;

// ── Données AABB du joueur ───────────────────────────────────────────────────
struct PlayerBounds {
    static constexpr float W = 0.6f;  // largeur
    static constexpr float H = 1.8f;  // hauteur
    static constexpr float EYE = 1.62f; // hauteur des yeux depuis les pieds
};

class Player {
public:
    // ── Position & caméra ───────────────────────────────────────────────────
    float x = 0, y = 30, z = 0;  // position des pieds
    float yaw   = 0;              // rotation horizontale (degrés)
    float pitch = 0;              // rotation verticale  (degrés, clampée ±89)

    // Velocity
    float vx = 0, vy = 0, vz = 0;

    // États
    bool  grounded   = false;
    bool  flying     = false;  // mode créatif
    bool  inLiquid   = false;
    bool  sprinting  = false;

    // ── Santé & faim ─────────────────────────────────────────────────────────
    float health    = 20.f;
    float maxHealth = 20.f;
    float hunger    = 18.f;
    float maxHunger = 20.f;

    // ── Inventaire (10 col × 8 lignes = 80 slots, ligne 0 = hotbar) ──────────
    std::array<BlockID, INV_SIZE> inventory = defaultInventory();
    int   hotbarSlot    = 0;   // 0-9
    bool  inventoryOpen = false;

    BlockID selectedBlock() const { return inventory[hotbarSlot]; }

    // ── Initialisation ───────────────────────────────────────────────────────
    void spawn(float sx, float sy, float sz) { x=sx; y=sy; z=sz; }

    // ── Direction du regard ──────────────────────────────────────────────────
    struct LookDir { float dx, dy, dz; };
    LookDir lookDir() const;

    // ── Mise à jour physique ─────────────────────────────────────────────────
    void update(float dt, const World& world,
                bool moveF, bool moveB, bool moveL, bool moveR,
                bool jump, bool sneak);

    // ── Position des yeux (pour la caméra) ──────────────────────────────────
    float eyeX() const { return x; }
    float eyeY() const { return y + PlayerBounds::EYE; }
    float eyeZ() const { return z; }

private:
    // Physique & collisions
    void applyGravity(float dt);
    void applyMovement(float dt, bool f, bool b, bool l, bool r, bool sneak);
    void resolveCollisions(float dx, float dy, float dz, const World& world);
    bool testAABB(float px, float py, float pz, const World& world) const;
};





static constexpr float PI      = 3.14159265f;
static constexpr float DEG2RAD = PI / 180.f;
static constexpr float GRAVITY = -28.f;    // m/s²
static constexpr float JUMP_V  = 9.f;
static constexpr float SPEED   = 4.5f;
static constexpr float SPRINT_MUL = 1.6f;
static constexpr float SWIM_SPEED = 2.5f;
static constexpr float FLY_SPEED  = 10.f;
static constexpr float SNEAK_MUL  = 0.3f;
static constexpr float DRAG_GROUND = 0.92f;
static constexpr float DRAG_AIR    = 0.98f;
static constexpr float DRAG_LIQUID = 0.80f;

inline Player::LookDir Player::lookDir() const {
    float yR = yaw   * DEG2RAD;
    float pR = pitch * DEG2RAD;
    float cosPitch = std::cos(pR);
    return {
        -std::sin(yR) * cosPitch,
         std::sin(pR),
         -std::cos(yR) * cosPitch
    };
}

// Teste si la bounding box (pieds en px,py,pz) est libre de blocs
inline bool Player::testAABB(float px, float py, float pz, const World& world) const {
    float hw = PlayerBounds::W * 0.5f;
    int x0 = (int)std::floor(px - hw);
    int x1 = (int)std::floor(px + hw - 0.001f);
    int y0 = (int)std::floor(py);
    int y1 = (int)std::floor(py + PlayerBounds::H - 0.001f);
    int z0 = (int)std::floor(pz - hw);
    int z1 = (int)std::floor(pz + hw - 0.001f);
    for (int bx = x0; bx <= x1; ++bx)
        for (int by = y0; by <= y1; ++by)
            for (int bz = z0; bz <= z1; ++bz)
                if (blockSolid(world.GetBlock(bx, by, bz))) return false;
    return true;
}

inline void Player::applyGravity(float dt) {
    if (flying) {
        vy *= 0.9f;
        return;
    }
    if (inLiquid) {
        vy += GRAVITY * 0.25f * dt;
        vy = std::max(vy, -3.f);
    } else if (!grounded) {
        vy += GRAVITY * dt;
        vy = std::max(vy, -50.f);
    }
}

inline void Player::applyMovement(float dt, bool f, bool b, bool l, bool r, bool sneak) {
    // Vecteur de direction en 2D (plan horizontal)
    float yR = yaw * DEG2RAD;
    float fw_x = -std::sin(yR), fw_z = -std::cos(yR);
    float ri_x =  std::cos(yR), ri_z = -std::sin(yR);

    float ix = 0, iz = 0;
    if (f) { ix += fw_x; iz += fw_z; }
    if (b) { ix -= fw_x; iz -= fw_z; }
    if (l) { ix -= ri_x; iz -= ri_z; }
    if (r) { ix += ri_x; iz += ri_z; }

    float len = std::sqrt(ix*ix + iz*iz);
    if (len > 0.001f) { ix /= len; iz /= len; }

    float spd = flying ? FLY_SPEED :
                inLiquid ? SWIM_SPEED :
                (sneak ? SPEED * SNEAK_MUL : (sprinting ? SPEED*SPRINT_MUL : SPEED));

    vx = ix * spd;
    vz = iz * spd;

    if (flying) {
        // Vol vertical
    }
}

inline void Player::resolveCollisions(float moveX, float moveY, float moveZ,
                                const World& world) {
    // On déplace axe par axe et teste la collision
    float nx = x + moveX;
    if (testAABB(nx, y, z, world)) {
        x = nx;
    } else {
        vx = 0;
    }

    float ny_pos = y + moveY;
    if (testAABB(x, ny_pos, z, world)) {
        y = ny_pos;
        grounded = (moveY < 0) ? false : grounded;
    } else {
        if (moveY < 0) grounded = true;
        else if (moveY > 0) ; // plafond
        vy = 0;
    }

    float nz = z + moveZ;
    if (testAABB(x, y, nz, world)) {
        z = nz;
    } else {
        vz = 0;
    }
}

inline void Player::update(float dt, const World& world,
                    bool moveF, bool moveB, bool moveL, bool moveR,
                    bool jump, bool sneak) {
    // Détection liquide
    BlockID headBlock = world.GetBlock((int)x, (int)(y + PlayerBounds::EYE), (int)z);
    BlockID feetBlock = world.GetBlock((int)x, (int)(y + 0.1f), (int)z);
    inLiquid = blockLiquid(feetBlock) || blockLiquid(headBlock);

    applyMovement(dt, moveF, moveB, moveL, moveR, sneak);

    // Saut
    if (jump) {
        if (grounded && !inLiquid) {
            vy = JUMP_V;
            grounded = false;
        } else if (inLiquid) {
            vy = SWIM_SPEED * 0.8f;
        } else if (flying) {
            vy = FLY_SPEED * 0.5f;
        }
    }

    applyGravity(dt);

    // Drag
    float drag = inLiquid ? DRAG_LIQUID : (grounded ? DRAG_GROUND : DRAG_AIR);
    vx *= drag;
    vz *= drag;
    if (std::abs(vx) < 0.01f) vx = 0;
    if (std::abs(vz) < 0.01f) vz = 0;

    // Résolution des collisions
    grounded = false; // sera remis à true si on touche le sol
    resolveCollisions(vx * dt, vy * dt, vz * dt, world);

    // Clamp position Y (évite de tomber infiniment)
    if (y < -10) { y = 30; vy = 0; }
}