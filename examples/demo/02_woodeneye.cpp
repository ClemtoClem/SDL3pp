/*
 * demo/02_woodeneye.cpp
 *
 * Extended SDL3pp port of woodeneye-008 – GPU Edition.
 *
 * New features vs the original:
 *  - GPU Vulkan/hardware-accelerated renderer  (SDL::GPU_RENDERER)
 *  - Textured floor          (assets/dirt.png  – tiled, wrap mode)
 *  - Textured terrain crates (assets/crate.jpg – per-face, back-face culled)
 *  - Physical bullet animation: tracers travel at 120 m/s, collide with
 *    terrain and capsules before applying damage
 *  - Near-plane polygon clipping (Sutherland-Hodgman) for clean geo edges
 *  - Painter's-algorithm depth sort for opaque faces
 *  - SDL3pp_ui: Menu, Game Over, and in-game HUD use the retained-mode UI system
 *
 * Controls (in-game):
 *   WASD + Space – move / jump
 *   Mouse        – look
 *   LMB          – fire bullet  (25 damage per hit)
 *   ESC          – back to menu
 */

// ── Enable optional SDL3pp subsystems ────────────────────────────────────────
#ifndef SDL3PP_ENABLE_TTF
#define SDL3PP_ENABLE_TTF
#endif
#ifndef SDL3PP_ENABLE_MIXER
#define SDL3PP_ENABLE_MIXER
#endif

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_ecs.h>
#include <SDL3pp/SDL3pp_scene.h>
#include <SDL3pp/SDL3pp_math3D.h>
#include <SDL3pp/SDL3pp_image.h>
#include <SDL3pp/SDL3pp_ttf.h>
#include <SDL3pp/SDL3pp_mixer.h>
#include <SDL3pp/SDL3pp_ui.h>

#include <algorithm>
#include <cmath>
#include <format>
#include <numbers>
#include <numeric>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Game constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int MAX_PLAYERS = 4;
static constexpr float kCapsuleRadius = 0.5f;
static constexpr float kCapsuleHeight = 1.5f;
static constexpr float kMaxHP = 100.f;
static constexpr float kPlayerDamage = 25.f;
static constexpr float kEnemyDamage = 20.f;
static constexpr float kEnemyShootSec = 2.f;
static constexpr float kMouseSensitivity = 0.0015f;
static constexpr float kNearPlane = 0.15f;
static constexpr float kBulletSpeed = 120.f;
static constexpr float kBulletLife = 2.f;
static constexpr float kFloorTile = 2.f; ///< world units per dirt tile

// ─────────────────────────────────────────────────────────────────────────────
// ECS Components
// ─────────────────────────────────────────────────────────────────────────────

using Pos3 = SDL::FVector3;
using Vel3 = SDL::FVector3;
struct Look
{
    float yaw = 0.f, pitch = 0.f;
};
struct Capsule
{
    float radius = kCapsuleRadius, height = kCapsuleHeight;
};
struct Health
{
    float hp = kMaxHP, maxHp = kMaxHP;
};

enum class Team : Uint8
{
    Player,
    Enemy
};
struct EntityTeam
{
    Team team = Team::Player;
};
struct Score
{
    int points = 0;
};

struct PlayerInput
{
    SDL::MouseID mouse = 0;
    SDL::KeyboardID keyboard = 0;
    Uint8 wasd = 0;
};

struct EnemyAI
{
    float shootTimer = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Physical bullet
// ─────────────────────────────────────────────────────────────────────────────

struct BulletData
{
    SDL::FVector3 pos;
    SDL::FVector3 vel;
    SDL::ECS::EntityId ownerId;
    Team ownerTeam;
    float damage;
    float life; ///< seconds remaining
};

// ─────────────────────────────────────────────────────────────────────────────
// Polygon clipping vertex
// ─────────────────────────────────────────────────────────────────────────────

struct ClipVert
{
    SDL::FVector3 view; ///< view-space position
    SDL::FVector2 uv;   ///< texture coordinates (may tile > 1.0)
    float shade;        ///< luminance multiplier [0,1]
};

/// Sutherland-Hodgman clip against the near plane (keep z < -near).
static std::vector<ClipVert> ClipNear(const std::vector<ClipVert> &poly, float near)
{
    if (poly.empty())
        return {};
    std::vector<ClipVert> out;
    out.reserve(poly.size() + 1);
    const int n = (int)poly.size();
    for (int i = 0; i < n; ++i)
    {
        const ClipVert &a = poly[i];
        const ClipVert &b = poly[(i + 1) % n];
        bool aIn = a.view.z < -near;
        bool bIn = b.view.z < -near;
        if (aIn)
            out.push_back(a);
        if (aIn != bIn)
        {
            float t = (-near - a.view.z) / (b.view.z - a.view.z);
            ClipVert c;
            c.view = {a.view.x + t * (b.view.x - a.view.x),
                      a.view.y + t * (b.view.y - a.view.y),
                      -near};
            c.uv = {a.uv.x + t * (b.uv.x - a.uv.x),
                    a.uv.y + t * (b.uv.y - a.uv.y)};
            c.shade = a.shade + t * (b.shade - a.shade);
            out.push_back(c);
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Geometry builders
// ─────────────────────────────────────────────────────────────────────────────

using EdgeList = std::vector<std::pair<SDL::FVector3, SDL::FVector3>>;

static EdgeList CubeEdges(const SDL::FAABB &b)
{
    auto &mn = b.Min;
    auto &mx = b.Max;
    return {
        {{mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z}},
        {{mx.x, mn.y, mn.z}, {mx.x, mn.y, mx.z}},
        {{mx.x, mn.y, mx.z}, {mn.x, mn.y, mx.z}},
        {{mn.x, mn.y, mx.z}, {mn.x, mn.y, mn.z}},
        {{mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z}},
        {{mx.x, mx.y, mn.z}, {mx.x, mx.y, mx.z}},
        {{mx.x, mx.y, mx.z}, {mn.x, mx.y, mx.z}},
        {{mn.x, mx.y, mx.z}, {mn.x, mx.y, mn.z}},
        {{mn.x, mn.y, mn.z}, {mn.x, mx.y, mn.z}},
        {{mx.x, mn.y, mn.z}, {mx.x, mx.y, mn.z}},
        {{mx.x, mn.y, mx.z}, {mx.x, mx.y, mx.z}},
        {{mn.x, mn.y, mx.z}, {mn.x, mx.y, mx.z}},
    };
}

static EdgeList ArenaEdges(float s)
{
    EdgeList edges;
    edges.reserve(12 + (int)(2 * s + 2) * 2);
    const int map[24] = {0, 1, 1, 3, 3, 2, 2, 0, 7, 6, 6, 4, 4, 5, 5, 7, 6, 2, 3, 7, 0, 4, 5, 1};
    for (int i = 0; i < 12; ++i)
    {
        SDL::FVector3 a, b;
        for (int j = 0; j < 3; ++j)
        {
            (&a.x)[j] = (map[i * 2 + 0] & (1 << j)) ? s : -s;
            (&b.x)[j] = (map[i * 2 + 1] & (1 << j)) ? s : -s;
        }
        edges.push_back({a, b});
    }
    for (int i = 0; i <= (int)(2 * s); i += 2)
    {
        float d = (float)i - s;
        edges.push_back({{-s, -s, d}, {s, -s, d}});
        edges.push_back({{d, -s, -s}, {d, -s, s}});
    }
    return edges;
}

// ─────────────────────────────────────────────────────────────────────────────
// Game config
// ─────────────────────────────────────────────────────────────────────────────

struct GameConfig
{
    int mapScale = 16;
    int playerCount = 1;
    int enemyCount = 3;
};

// ─────────────────────────────────────────────────────────────────────────────
// Application
// ─────────────────────────────────────────────────────────────────────────────

enum class AppState
{
    Menu,
    Playing,
    GameOver
};

struct Main
{
    // ── SDL callbacks ──────────────────────────────────────────────────────────

    static SDL::AppResult Init(Main **out, SDL::AppArgs)
    {
        SDL::SetAppMetadata("Woodeneye-008 GPU", "3.0", "com.example.woodeneye");
        SDL::Init(SDL::INIT_VIDEO);
        SDL::TTF::Init();
        *out = new Main();
        return SDL::APP_CONTINUE;
    }
    static void Quit(Main *m, SDL::AppResult)
    {
        delete m;
        SDL::TTF::Quit();
    }

    // ── Window + GPU-backed renderer ──────────────────────────────────────────

    SDL::Window window{"Woodeneye-008 - GPU Edition", {800, 600}, SDL::WINDOW_RESIZABLE};
    SDL::Renderer renderer{window, SDL::GPU_RENDERER};

    // ── Textures (loaded after renderer is ready) ─────────────────────────────

    SDL::Texture texDirt;  ///< assets/dirt.png   – floor tile
    SDL::Texture texCrate; ///< assets/crate.jpg  – obstacle faces

    // ── Timing ────────────────────────────────────────────────────────────────

    SDL::FrameTimer frameTimer{120.f};
    Uint64 frameCount = 0;

    // ── App state ─────────────────────────────────────────────────────────────

    AppState appState = AppState::Menu;
    GameConfig config = {};
    int menuSel = 0; ///< 0=map 1=players 2=enemies (keyboard nav)

    // ── ECS world (recreated each game) ───────────────────────────────────────

    SDL::ECS::World world;

    int numPlayers = 1;
    SDL::ECS::EntityId playerIds[MAX_PLAYERS] = {};
    std::vector<SDL::ECS::EntityId> enemyIds;

    // ── Terrain ───────────────────────────────────────────────────────────────

    float mapScale = 16.f;
    EdgeList arenaEdges;

    struct TerrainCube
    {
        SDL::FAABB box;
        EdgeList edges;
        float distToCam = 0.f;
    };
    std::vector<TerrainCube> terrain;

    // ── Bullets ───────────────────────────────────────────────────────────────

    std::vector<BulletData> bullets;

    // ── UI subsystem ──────────────────────────────────────────────────────────

    SDL::ResourceManager rm;
    SDL::ResourcePool &uiPool{*rm.CreatePool("ui")};
    SDL::ECS::World uiWorld;
    SDL::UI::System ui{uiWorld, renderer, SDL::MixerRef{}, uiPool};

    // UI entity IDs for dynamic updates
    SDL::ECS::EntityId eid_mapVal = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_plVal = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_enVal = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_menuPage = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_goPage = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eid_hudPage = SDL::ECS::NullEntity;
    std::array<SDL::ECS::EntityId, MAX_PLAYERS> eid_goScore{};

    // ─────────────────────────────────────────────────────────────────────────
    // Constructor
    // ─────────────────────────────────────────────────────────────────────────

    Main()
    {
        renderer.SetVSync(0);
        SDL::SetHintWithPriority(SDL_HINT_WINDOWS_RAW_KEYBOARD, "1",
                                 SDL::HINT_OVERRIDE);
        // Gracefully attempt to load textures
        try
        {
            texDirt = SDL::LoadTexture(renderer, "assets/dirt.png");
        }
        catch (...)
        {
            SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
                         "Cannot load assets/dirt.png");
        }
        try
        {
            texCrate = SDL::LoadTexture(renderer, "assets/crate.jpg");
        }
        catch (...)
        {
            SDL::LogWarn(SDL::LOG_CATEGORY_APPLICATION,
                         "Cannot load assets/crate.jpg");
        }
        _BuildUI();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // View-space transform + projection  (identical conventions to DrawSeg)
    // ─────────────────────────────────────────────────────────────────────────

    static SDL::FVector3 ToView(const SDL::FVector3 &wld,
                                const SDL::FVector3 &cam,
                                const Look &look) noexcept
    {
        const float cy = SDL::Cos(look.yaw), sy = SDL::Sin(look.yaw);
        const float cp = SDL::Cos(look.pitch), sp = SDL::Sin(look.pitch);
        const SDL::FVector3 r = wld - cam;
        return {
            cy * r.x - sy * r.z,
            sy * sp * r.x + cp * r.y + cy * sp * r.z,
            sy * cp * r.x - sp * r.y + cy * cp * r.z};
    }

    static bool Project(const SDL::FVector3 &v, float ox, float oy, float fov,
                        float &sx, float &sy) noexcept
    {
        if (v.z >= -kNearPlane)
            return false;
        sx = ox - fov * v.x / v.z;
        sy = oy + fov * v.y / v.z;
        return true;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Render a near-clipped view-space polygon as textured triangles
    // ─────────────────────────────────────────────────────────────────────────

    void RenderPoly(const std::vector<ClipVert> &poly,
                    float cx, float cy, float fov,
                    SDL::TextureRef tex, float alpha = 1.f)
    {
        if ((int)poly.size() < 3)
            return;
        // Fan triangulation from vertex 0
        for (int i = 1; i + 1 < (int)poly.size(); ++i)
        {
            SDL::Vertex verts[3];
            for (int k = 0; k < 3; ++k)
            {
                const ClipVert &cv = (k == 0) ? poly[0] : poly[i + k - 1];
                float s = cv.shade;
                float sx = cx - fov * cv.view.x / cv.view.z;
                float sy_ = cy + fov * cv.view.y / cv.view.z;
                verts[k] = {{sx, sy_}, {s, s, s, alpha}, {cv.uv.x, cv.uv.y}};
            }
            renderer.RenderGeometry(tex, std::span{verts, 3});
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Draw a single world-space quad face (4 CCW corners viewed from outside).
    // Performs back-face culling, transforms to view space, clips and renders.
    // ─────────────────────────────────────────────────────────────────────────

    void DrawFace(const SDL::FVector3 wp[4], const SDL::FVector2 uvs[4],
                  float shade, const SDL::FVector3 &camPos, const Look &look,
                  float cx, float cy, float fov, SDL::TextureRef tex)
    {
        // Back-face culling: outward normal vs vector to camera
        SDL::FVector3 fn = (wp[1] - wp[0]).Cross(wp[3] - wp[0]);
        if (fn.Dot(camPos - wp[0]) <= 0.f)
            return;

        std::vector<ClipVert> poly(4);
        for (int i = 0; i < 4; ++i)
            poly[i] = {ToView(wp[i], camPos, look), uvs[i], shade};

        auto clipped = ClipNear(poly, kNearPlane);
        RenderPoly(clipped, cx, cy, fov, tex);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Textured floor – single large quad with WRAP addressing so dirt tiles
    // ─────────────────────────────────────────────────────────────────────────

    void DrawFloor(const SDL::FVector3 &cam, const Look &look,
                   float cx, float cy, float fov)
    {
        if (!texDirt.Get())
        {
            // Fallback: solid dark-grey fill using 2 triangles
            SDL::FVector3 fp[4] = {{-mapScale, -mapScale, mapScale}, {mapScale, -mapScale, mapScale}, {mapScale, -mapScale, -mapScale}, {-mapScale, -mapScale, -mapScale}};
            static constexpr SDL::FVector2 noUV[4] = {};
            DrawFace(fp, noUV, 0.3f, cam, look, cx, cy, fov, nullptr);
            return;
        }

        const float s = mapScale;
        const float y = -s;
        const float tiles = s / kFloorTile; // UV scale so dirt tiles naturally

        SDL::FVector3 wp[4] = {
            {-s, y, s}, // winding CCW viewed from above (+Y) { s, y,  s}, { s, y, -s}, {-s, y, -s},
        };
        SDL::FVector2 uvs[4] = {{0, 0}, {tiles, 0}, {tiles, tiles}, {0, tiles}};

        renderer.SetRenderTextureAddressMode(SDL::TEXTURE_ADDRESS_WRAP,
                                             SDL::TEXTURE_ADDRESS_WRAP);
        DrawFace(wp, uvs, 0.82f, cam, look, cx, cy, fov, texDirt);
        renderer.SetRenderTextureAddressMode(SDL::TEXTURE_ADDRESS_CLAMP,
                                             SDL::TEXTURE_ADDRESS_CLAMP);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Textured crate box (6 faces, back-face culled, axis-shaded)
    // ─────────────────────────────────────────────────────────────────────────

    void DrawCrateBox(const SDL::FAABB &box, const SDL::FVector3 &cam,
                      const Look &look, float cx, float cy, float fov)
    {
        const SDL::FVector3 &mn = box.Min;
        const SDL::FVector3 &mx = box.Max;

        // 8 corners indexed as binary XYZ: index = x<<0 | y<<1 | z<<2
        const SDL::FVector3 c[8] = {
            {mn.x, mn.y, mn.z}, // 0: 000
            {mx.x, mn.y, mn.z}, // 1: 100
            {mn.x, mx.y, mn.z}, // 2: 010
            {mx.x, mx.y, mn.z}, // 3: 110
            {mn.x, mn.y, mx.z}, // 4: 001
            {mx.x, mn.y, mx.z}, // 5: 101
            {mn.x, mx.y, mx.z}, // 6: 011
            {mx.x, mx.y, mx.z}  // 7: 111
        };

        // face{ corner indices (CCW from outside), shade }
        struct FD
        {
            int v[4];
            float shade;
        };
        static constexpr FD kFaces[6] = {
            {{0, 2, 3, 1}, 0.65f}, // -Z front
            {{5, 7, 6, 4}, 0.65f}, // +Z back
            {{4, 6, 2, 0}, 0.50f}, // -X left
            {{1, 3, 7, 5}, 0.50f}, // +X right
            {{2, 6, 7, 3}, 1.00f}, // +Y top   (brightest)
            {{0, 1, 5, 4}, 0.18f}, // -Y bottom (darkest)
        };

        static constexpr SDL::FVector2 kUV[4] = {{0, 0}, {0, 1}, {1, 1}, {1, 0}};

        // Only draw if texture is available, otherwise draw as coloured solid
        SDL::TextureRef tex = texCrate.Get()
                                  ? SDL::TextureRef{texCrate}
                                  : SDL::TextureRef{nullptr};
        for (const auto &f : kFaces)
        {
            SDL::FVector3 wp[4] = {c[f.v[0]], c[f.v[1]], c[f.v[2]], c[f.v[3]]};
            SDL::FVector2 uvs[4];
            for (int i = 0; i < 4; ++i)
                uvs[i] = kUV[i];
            DrawFace(wp, uvs, f.shade, cam, look, cx, cy, fov, tex);
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Terrain collision (sphere-FAABB push-out)
    // ─────────────────────────────────────────────────────────────────────────

    void ResolveCollision(SDL::FVector3 &pos, SDL::FVector3 &vel, const Capsule &cap) const
    {
        SDL::FVector3 sc = {pos.x, pos.y - cap.height * 0.5f, pos.z};
        for (const auto &t : terrain)
        {
            float cx_ = std::max(t.box.Min.x, std::min(sc.x, t.box.Max.x));
            float cy_ = std::max(t.box.Min.y, std::min(sc.y, t.box.Max.y));
            float cz_ = std::max(t.box.Min.z, std::min(sc.z, t.box.Max.z));
            SDL::FVector3 diff = sc - SDL::FVector3{cx_, cy_, cz_};
            float d = diff.Length();
            if (d < cap.radius && d > 1e-5f)
            {
                SDL::FVector3 push = diff.Normalize() * (cap.radius - d);
                pos.x += push.x;
                pos.z += push.z;
                if (push.y > 0.f)
                {
                    pos.y += push.y;
                    vel.y = 0.f;
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Bullet management
    // ─────────────────────────────────────────────────────────────────────────

    void FireBullet(SDL::ECS::EntityId shooterId, float damage)
    {
        const auto *pPos = world.Get<Pos3>(shooterId);
        const auto *pLook = world.Get<Look>(shooterId);
        const auto *pCap = world.Get<Capsule>(shooterId);
        const auto *pTeam = world.Get<EntityTeam>(shooterId);
        if (!pPos || !pLook || !pCap || !pTeam)
            return;

        SDL::FVector3 eye = {pPos->x, pPos->y - pCap->height * 0.3f, pPos->z};
        SDL::FVector3 dir = {
            -SDL::Sin(pLook->yaw) * SDL::Cos(pLook->pitch),
            SDL::Sin(pLook->pitch),
            -SDL::Cos(pLook->yaw) * SDL::Cos(pLook->pitch)};
        bullets.push_back({eye, dir * kBulletSpeed,
                           shooterId, pTeam->team, damage, kBulletLife});
    }

    void UpdateBullets(float dt)
    {
        for (auto &b : bullets)
        {
            if (b.life <= 0.f)
                continue;
            b.life -= dt;

            SDL::FVector3 newPos = b.pos + b.vel * dt;
            SDL::FRay ray{b.pos, b.vel.Normalize()};
            float moveDist = (b.vel * dt).Length();

            // Terrain collision
            for (const auto &t : terrain)
            {
                float tMin = 0.f, tMax = 0.f;
                if (ray.Intersects(t.box, tMin, tMax) && tMin >= 0.f && tMin <= moveDist)
                {
                    b.life = 0.f;
                    break;
                }
            }
            if (b.life <= 0.f)
                continue;

            // Arena boundary
            if (std::abs(newPos.x) >= mapScale || std::abs(newPos.z) >= mapScale || newPos.y < -mapScale || newPos.y > mapScale)
            {
                b.life = 0.f;
                continue;
            }

            // Entity capsule hit (sphere test at mid-capsule)
            auto TryHit = [&](SDL::ECS::EntityId eid)
            {
                if (eid == b.ownerId || !world.IsAlive(eid))
                    return;
                const auto *hp = world.Get<Health>(eid);
                if (!hp || hp->hp <= 0.f)
                    return;
                const auto &tp = *world.Get<Pos3>(eid);
                const auto &tc = *world.Get<Capsule>(eid);
                SDL::FVector3 mid = {tp.x, tp.y - tc.height * 0.5f, tp.z};
                float r = tc.radius * 1.3f;
                if ((newPos - mid).LengthSq() < r * r)
                {
                    ApplyDamage(b.ownerId, eid, b.damage);
                    b.life = 0.f;
                }
            };
            for (int i = 0; i < numPlayers; ++i)
                if (playerIds[i] != SDL::ECS::NullEntity)
                    TryHit(playerIds[i]);
            for (auto eid : enemyIds)
                TryHit(eid);

            if (b.life > 0.f)
                b.pos = newPos;
        }

        bullets.erase(
            std::remove_if(bullets.begin(), bullets.end(),
                           [](const BulletData &bd)
                           { return bd.life <= 0.f; }),
            bullets.end());
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Damage / death
    // ─────────────────────────────────────────────────────────────────────────

    void ApplyDamage(SDL::ECS::EntityId attacker, SDL::ECS::EntityId target, float dmg)
    {
        auto *hp = world.Get<Health>(target);
        if (!hp || hp->hp <= 0.f)
            return;
        hp->hp -= dmg;
        if (hp->hp > 0.f)
            return;
        hp->hp = 0.f;

        if (auto *sc = world.Get<Score>(attacker))
            sc->points++;

        const auto *team = world.Get<EntityTeam>(target);
        if (team && team->team == Team::Enemy)
        {
            // Respawn enemy
            *world.Get<Pos3>(target) = RandSpawnPos();
            *world.Get<Vel3>(target) = {0, 0, 0};
            hp->hp = kMaxHP;
        }
        else
        {
            bool anyAlive = false;
            for (int i = 0; i < numPlayers; ++i)
            {
                if (playerIds[i] == SDL::ECS::NullEntity)
                    continue;
                const auto *ph = world.Get<Health>(playerIds[i]);
                if (ph && ph->hp > 0.f)
                {
                    anyAlive = true;
                    break;
                }
            }
            if (!anyAlive)
            {
                appState = AppState::GameOver;
                window.SetRelativeMouseMode(false);
                _UpdateGameOverLabels();
                _ShowPage(AppState::GameOver);
            }
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Game startup helpers
    // ─────────────────────────────────────────────────────────────────────────

    SDL::FVector3 RandSpawnPos() const
    {
        float b = mapScale * 0.75f;
        float x = ((float)(SDL::Rand(200) - 100) / 100.f) * b;
        float z = ((float)(SDL::Rand(200) - 100) / 100.f) * b;
        return {x, kCapsuleHeight - mapScale + 0.01f, z};
    }

    void SpawnPlayer(int slot)
    {
        SDL::ECS::EntityId id = world.CreateEntity();
        playerIds[slot] = id;
        float ang = (float)slot * (float)(std::numbers::pi * 0.5);
        float dist = mapScale * 0.35f;
        world.Add<Pos3>(id, {SDL::Cos(ang) * dist,
                             kCapsuleHeight - mapScale + 0.01f,
                             SDL::Sin(ang) * dist});
        world.Add<Vel3>(id, {0, 0, 0});
        world.Add<Look>(id, {ang + (float)std::numbers::pi, 0.f});
        world.Add<Capsule>(id, {kCapsuleRadius, kCapsuleHeight});
        world.Add<Health>(id, {kMaxHP, kMaxHP});
        world.Add<EntityTeam>(id, {Team::Player});
        world.Add<Score>(id, {0});
        world.Add<PlayerInput>(id, {0, 0, 0});
    }

    void SpawnEnemy()
    {
        SDL::ECS::EntityId id = world.CreateEntity();
        enemyIds.push_back(id);
        world.Add<Pos3>(id, RandSpawnPos());
        world.Add<Vel3>(id, {0, 0, 0});
        world.Add<Look>(id, {0.f, 0.f});
        world.Add<Capsule>(id, {kCapsuleRadius, kCapsuleHeight});
        world.Add<Health>(id, {kMaxHP, kMaxHP});
        world.Add<EntityTeam>(id, {Team::Enemy});
        world.Add<EnemyAI>(id, {0.f});
    }

    void GenerateTerrain()
    {
        int count = 15 + config.mapScale;
        for (int i = 0; i < count; ++i)
        {
            float hs = 1.f + (float)(SDL::Rand(3));
            float bnd = mapScale - hs - 1.f;
            if (bnd <= 0)
                continue;
            float cx_ = ((float)(SDL::Rand(200) - 100) / 100.f) * bnd;
            float cz_ = ((float)(SDL::Rand(200) - 100) / 100.f) * bnd;
            if (std::abs(cx_) < 4.f && std::abs(cz_) < 4.f)
                continue;
            float cy_ = -mapScale;
            SDL::FAABB box{{cx_ - hs, cy_, cz_ - hs}, {cx_ + hs, cy_ + hs * 2.f, cz_ + hs}};
            terrain.push_back({box, CubeEdges(box), 0.f});
        }
    }

    void StartGame()
    {
        world = SDL::ECS::World{};
        enemyIds.clear();
        terrain.clear();
        bullets.clear();
        for (auto &id : playerIds)
            id = SDL::ECS::NullEntity;

        numPlayers = config.playerCount;
        mapScale = (float)config.mapScale;
        arenaEdges = ArenaEdges(mapScale);
        GenerateTerrain();

        for (int i = 0; i < numPlayers; ++i)
            SpawnPlayer(i);
        for (int i = 0; i < config.enemyCount; ++i)
            SpawnEnemy();

        frameTimer.Reset();
        appState = AppState::Playing;
        window.SetRelativeMouseMode(true);
        _ShowPage(AppState::Playing);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Physics update
    // ─────────────────────────────────────────────────────────────────────────

    void UpdatePhysics(float dt)
    {
        const float bound = mapScale;
        world.Each<Pos3, Vel3, Look, Capsule, PlayerInput>(
            [dt, bound, this](SDL::ECS::EntityId eid,
                              Pos3 &pos, Vel3 &vel, Look &look,
                              Capsule &cap, PlayerInput &inp)
            {
                auto *hp = world.Get<Health>(eid);
                if (hp && hp->hp <= 0.f)
                    return;
                const float rate = 6.f, drag = std::exp(-dt * rate), diff = 1.f - drag;
                const float mult = 60.f, grav = 25.f;
                const float cosY = SDL::Cos(look.yaw), sinY = SDL::Sin(look.yaw);
                const Uint8 wasd = inp.wasd;
                float dirX = (wasd & 8 ? 1.f : 0.f) - (wasd & 2 ? 1.f : 0.f);
                float dirZ = (wasd & 4 ? 1.f : 0.f) - (wasd & 1 ? 1.f : 0.f);
                float norm = dirX * dirX + dirZ * dirZ;
                float accX = mult * (norm == 0.f ? 0.f : (cosY * dirX + sinY * dirZ) / std::sqrt(norm));
                float accZ = mult * (norm == 0.f ? 0.f : (-sinY * dirX + cosY * dirZ) / std::sqrt(norm));
                float vx = vel.x, vy = vel.y, vz = vel.z;
                vel.x = vel.x * drag + diff * accX / rate;
                vel.y -= grav * dt;
                vel.z = vel.z * drag + diff * accZ / rate;
                pos.x += (dt - diff / rate) * accX / rate + diff * vx / rate;
                pos.y += -0.5f * grav * dt * dt + vy * dt;
                pos.z += (dt - diff / rate) * accZ / rate + diff * vz / rate;
                float bxz = bound - cap.radius, byMin = cap.height - bound;
                float nx = std::max(std::min(bxz, pos.x), -bxz);
                float ny = std::max(std::min(bound, pos.y), byMin);
                float nz = std::max(std::min(bxz, pos.z), -bxz);
                if (pos.x != nx)
                    vel.x = 0.f;
                if (pos.y != ny)
                    vel.y = (wasd & 16) ? 8.4375f : 0.f;
                if (pos.z != nz)
                    vel.z = 0.f;
                pos = {nx, ny, nz};
                ResolveCollision(pos, vel, cap);
            });
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Enemy AI
    // ─────────────────────────────────────────────────────────────────────────

    void UpdateEnemies(float dt)
    {
        auto NearestPlayer = [&](const SDL::FVector3 &from) -> SDL::ECS::EntityId
        {
            SDL::ECS::EntityId best = SDL::ECS::NullEntity;
            float bestD = 1e30f;
            for (int i = 0; i < numPlayers; ++i)
            {
                if (playerIds[i] == SDL::ECS::NullEntity)
                    continue;
                const auto *hp = world.Get<Health>(playerIds[i]);
                if (!hp || hp->hp <= 0.f)
                    continue;
                const auto *p = world.Get<Pos3>(playerIds[i]);
                if (!p)
                    continue;
                float d = (*p - from).Length();
                if (d < bestD)
                {
                    bestD = d;
                    best = playerIds[i];
                }
            }
            return best;
        };

        world.Each<Pos3, Vel3, Look, Capsule, EnemyAI>(
            [&, dt](SDL::ECS::EntityId eid, Pos3 &pos, Vel3 &vel, Look &look,
                    Capsule &cap, EnemyAI &ai)
            {
                SDL::ECS::EntityId tid = NearestPlayer(pos);
                if (tid == SDL::ECS::NullEntity)
                    return;
                const SDL::FVector3 &tgt = *world.Get<Pos3>(tid);
                SDL::FVector3 toTgt = tgt - pos;
                float dist = toTgt.Length();
                look.yaw = std::atan2(-toTgt.x, -toTgt.z);
                look.pitch = std::atan2(toTgt.y,
                                        std::sqrt(toTgt.x * toTgt.x + toTgt.z * toTgt.z));
                if (dist > cap.radius * 3.f)
                {
                    SDL::FVector3 dir2D = SDL::FVector3{toTgt.x, 0.f, toTgt.z}.Normalize();
                    vel.x = dir2D.x * 5.f;
                    vel.z = dir2D.z * 5.f;
                }
                else
                {
                    vel.x *= 0.8f;
                    vel.z *= 0.8f;
                }
                vel.y -= 25.f * dt;
                pos.x += vel.x * dt;
                pos.y += vel.y * dt;
                pos.z += vel.z * dt;
                float bxz = mapScale - cap.radius, byMin = cap.height - mapScale;
                pos.x = std::max(std::min(bxz, pos.x), -bxz);
                if (pos.y < byMin)
                {
                    pos.y = byMin;
                    vel.y = 0.f;
                }
                pos.z = std::max(std::min(bxz, pos.z), -bxz);
                ResolveCollision(pos, vel, cap);
                ai.shootTimer += dt;
                if (ai.shootTimer >= kEnemyShootSec && dist < mapScale * 1.8f)
                {
                    ai.shootTimer = 0.f;
                    FireBullet(eid, kEnemyDamage);
                }
            });
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Wireframe helper (near-plane clipped line segment)
    // ─────────────────────────────────────────────────────────────────────────

    void DrawSeg(const SDL::FVector3 &va, const SDL::FVector3 &vb,
                 float ox, float oy, float fov)
    {
        float ax = va.x, ay = va.y, az = va.z;
        float bx = vb.x, by = vb.y, bz = vb.z;
        if (az >= -kNearPlane && bz >= -kNearPlane)
            return;
        if (az > -kNearPlane)
        {
            float t = (-kNearPlane - bz) / (az - bz);
            ax = bx + (ax - bx) * t;
            ay = by + (ay - by) * t;
            az = -kNearPlane;
        }
        else if (bz > -kNearPlane)
        {
            float t = (-kNearPlane - az) / (bz - az);
            bx = ax - (ax - bx) * t;
            by = ay - (ay - by) * t;
        }
        renderer.RenderLine({ox - fov * ax / az, oy + fov * ay / az}, {ox - fov * bx / bz, oy + fov * by / bz});
    }

    // ─────────────────────────────────────────────────────────────────────────
    // HP bar projected above an entity head
    // ─────────────────────────────────────────────────────────────────────────

    void DrawHPBar(const SDL::FVector3 &top, const SDL::FVector3 &cam, const Look &look,
                   float ox, float oy, float fov,
                   float hp, float maxHp, bool isPlayer)
    {
        SDL::FVector3 v = ToView(top, cam, look);
        float sx, sy;
        if (!Project(v, ox, oy, fov, sx, sy))
            return;
        float scl = std::max(0.4f, std::min(4.f, fov * 0.25f / (-v.z)));
        float barW = 42.f * scl, barH = 5.f * scl;
        float bx = sx - barW * 0.5f, by = sy - 18.f * scl;
        float fill = std::max(0.f, hp / maxHp);
        SDL::FRect bg{bx, by, barW, barH};
        renderer.SetDrawColor({20, 20, 20, 200});
        renderer.RenderFillRect(bg);
        renderer.SetDrawColor(isPlayer
                                  ? SDL::Color{30, 120, 255, 255}
                                  : SDL::Color{220, 30, 30, 255});
        renderer.RenderFillRect(SDL::FRect{bx, by, barW * fill, barH});
        renderer.SetDrawColor({200, 200, 200, 200});
        renderer.RenderRect(bg);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Per-viewport 3D scene render (no 2D HUD — handled by SDL3pp_ui Canvas)
    // ─────────────────────────────────────────────────────────────────────────

    void DrawViewport(int pi, const SDL::Rect &clip)
    {
        renderer.SetClipRect(clip);
        const float cx = (float)clip.x + clip.w * 0.5f;
        const float cy = (float)clip.y + clip.h * 0.5f;
        const float fov = 0.5f * SDL::Sqrt((float)(clip.w * clip.w + clip.h * clip.h));

        const SDL::FVector3 &cam = *world.Get<Pos3>(playerIds[pi]);
        const Look &look = *world.Get<Look>(playerIds[pi]);

        // ── 1. Textured floor ─────────────────────────────────────────────────
        DrawFloor(cam, look, cx, cy, fov);

        // ── 2. Crate cubes (textured, painter's order: far → near) ───────────
        for (auto &tc : terrain)
            tc.distToCam = (tc.box.Center() - cam).LengthSq();
        std::vector<int> order((int)terrain.size());
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(), [&](int a, int b_)
                  { return terrain[a].distToCam > terrain[b_].distToCam; });
        for (int idx : order)
            DrawCrateBox(terrain[idx].box, cam, look, cx, cy, fov);

        // ── 3. Wireframe edge outlines on top of faces ────────────────────────
        renderer.SetDrawColor({20, 12, 5, 160});
        for (int idx : order)
            for (const auto &[a, b] : terrain[idx].edges)
                DrawSeg(ToView(a, cam, look), ToView(b, cam, look), cx, cy, fov);

        // Arena boundary (soft blue-grey)
        renderer.SetDrawColor({45, 50, 70, 180});
        for (const auto &[a, b] : arenaEdges)
            DrawSeg(ToView(a, cam, look), ToView(b, cam, look), cx, cy, fov);

        // ── 4. Bullet tracers ─────────────────────────────────────────────────
        for (const auto &bd : bullets)
        {
            SDL::FVector3 v = ToView(bd.pos, cam, look);
            float sx, sy;
            if (!Project(v, cx, cy, fov, sx, sy))
                continue;
            float dist = -v.z;
            float r = std::max(1.5f, 5.f / dist);

            // Tracer dot
            renderer.SetDrawColor(bd.ownerTeam == Team::Player
                                      ? SDL::Color{255, 240, 40, 255}
                                      : SDL::Color{255, 80, 20, 255});
            renderer.RenderLine({sx - r, sy}, {sx + r, sy});
            renderer.RenderLine({sx, sy - r}, {sx, sy + r});

            // Faint trail behind bullet
            SDL::FVector3 tail = bd.pos - bd.vel.Normalize() * std::min(1.2f, dist * 0.1f);
            SDL::FVector3 vt = ToView(tail, cam, look);
            float stx, sty;
            if (Project(vt, cx, cy, fov, stx, sty))
            {
                Uint8 ta = (Uint8)(100.f * bd.life / kBulletLife);
                renderer.SetDrawColor({255, 200, 0, ta});
                renderer.RenderLine({sx, sy}, {stx, sty});
            }
        }

        // ── 5. Entities (wireframe capsule circles + world-space HP bars) ─────
        auto DrawEntity = [&](SDL::ECS::EntityId eid, bool isPlayer_)
        {
            if (eid == playerIds[pi] || !world.IsAlive(eid))
                return;
            const SDL::FVector3 &ep = *world.Get<Pos3>(eid);
            const Capsule &ec = *world.Get<Capsule>(eid);
            const Health *eh = world.Get<Health>(eid);
            renderer.SetDrawColor(isPlayer_
                                      ? SDL::Color{50, 100, 255, 255}
                                      : SDL::Color{230, 40, 40, 255});
            for (int k = 0; k < 2; ++k)
            {
                SDL::FVector3 pt = {ep.x, ep.y + (k == 0 ? 0.f : ec.radius - ec.height), ep.z};
                SDL::FVector3 v = ToView(pt, cam, look);
                if (!(v.z < 0.f))
                    continue;
                renderer.RenderCircle({cx - fov * v.x / v.z, cy + fov * v.y / v.z},
                                      ec.radius * fov / (-v.z));
            }
            if (eh)
            {
                SDL::FVector3 head = {ep.x, ep.y + 0.5f, ep.z};
                DrawHPBar(head, cam, look, cx, cy, fov, eh->hp, eh->maxHp, isPlayer_);
            }
        };
        for (int i = 0; i < numPlayers; ++i)
            if (playerIds[i] != SDL::ECS::NullEntity)
                DrawEntity(playerIds[i], true);
        for (auto eid : enemyIds)
            DrawEntity(eid, false);
    }

    // ─────────────────────────────────────────────────────────────────────────
    // HUD overlay drawn inside the SDL3pp_ui Canvas widget
    // Renders crosshair, player HP bar, score and FPS for every viewport.
    // ─────────────────────────────────────────────────────────────────────────

    void _DrawHUD(SDL::FRect outerRect)
    {
        if (appState != AppState::Playing)
            return;
        const float W = outerRect.w, H = outerRect.h;
        const float ox = outerRect.x, oy = outerRect.y;
        int partH = numPlayers > 2 ? 2 : 1;
        int partV = numPlayers > 1 ? 2 : 1;
        float sizeH = W / partH;
        float sizeV = H / partV;

        for (int pi = 0; pi < numPlayers; ++pi)
        {
            if (playerIds[pi] == SDL::ECS::NullEntity || !world.IsAlive(playerIds[pi]))
                continue;
            float clipX = ox + (float)(pi % partH) * sizeH;
            float clipY = oy + (float)(pi / partH) * sizeV;
            float cx = clipX + sizeH * 0.5f;
            float cy = clipY + sizeV * 0.5f;

            // ── Crosshair ───────────────────────────────────────────────────────
            renderer.SetDrawColor({0, 0, 0, 160});
            renderer.RenderLine({cx, cy - 11.f}, {cx, cy + 11.f});
            renderer.RenderLine({cx - 11.f, cy}, {cx + 11.f, cy});
            renderer.SetDrawColor({255, 255, 255, 230});
            renderer.RenderLine({cx, cy - 9.f}, {cx, cy + 9.f});
            renderer.RenderLine({cx - 9.f, cy}, {cx + 9.f, cy});

            // ── Player HP bar (bottom-center of viewport) ───────────────────────
            const Health *myHp = world.Get<Health>(playerIds[pi]);
            if (myHp)
            {
                float bw = sizeH * 0.38f, bh = 13.f;
                float bx0 = clipX + (sizeH - bw) * 0.5f;
                float by0 = clipY + sizeV - bh - 8.f;
                float ratio = std::max(0.f, myHp->hp / myHp->maxHp);
                SDL::FRect bg{bx0, by0, bw, bh};
                renderer.SetDrawColor({10, 10, 10, 210});
                renderer.RenderFillRect(bg);
                Uint8 gc = (Uint8)(255 * (1.f - ratio));
                Uint8 gr = (Uint8)(255 * ratio);
                renderer.SetDrawColor({gc, gr, 0, 255});
                renderer.RenderFillRect(SDL::FRect{bx0, by0, bw * ratio, bh});
                renderer.SetDrawColor({180, 180, 180, 180});
                renderer.RenderRect(bg);
                renderer.SetDrawColor({200, 200, 200, 200});
                renderer.RenderDebugTextFormat({bx0 + 4.f, by0 - 14.f}, "HP {:.0f}", myHp->hp);
            }

            // ── Score + FPS (top-left of viewport) ──────────────────────────────
            renderer.SetDrawColor({255, 255, 255, 255});
            const Score *sc = world.Get<Score>(playerIds[pi]);
            renderer.RenderDebugTextFormat({clipX + 4.f, clipY + 4.f},
                                           "P{} Score:{}", pi + 1, sc ? sc->points : 0);
            if (pi == 0)
                renderer.RenderDebugTextFormat({clipX + 4.f, clipY + 16.f},
                                               "{:.0f} fps  bullets:{}", frameTimer.GetFPS(), (int)bullets.size());
        }
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Full-frame game draw
    // ─────────────────────────────────────────────────────────────────────────

    void DrawGame()
    {
        const SDL::Point sz = renderer.GetOutputSize();
        renderer.SetDrawColor({8, 10, 22, 255});
        renderer.RenderClear();

        int partH = numPlayers > 2 ? 2 : 1;
        int partV = numPlayers > 1 ? 2 : 1;
        float sizeH = (float)sz.x / partH;
        float sizeV = (float)sz.y / partV;

        for (int i = 0; i < numPlayers; ++i)
        {
            if (playerIds[i] == SDL::ECS::NullEntity || !world.IsAlive(playerIds[i]))
                continue;
            SDL::Rect clip{(int)((i % partH) * sizeH), (int)((i / partH) * sizeV),
                           (int)sizeH, (int)sizeV};
            DrawViewport(i, clip);
        }
        renderer.ResetClipRect();

        // Split line for multiplayer
        if (numPlayers > 1)
        {
            renderer.SetDrawColor({50, 50, 60, 255});
            if (partH > 1)
                renderer.RenderLine({(float)sz.x * 0.5f, 0.f}, {(float)sz.x * 0.5f, (float)sz.y});
            if (partV > 1)
                renderer.RenderLine({0.f, (float)sz.y * 0.5f}, {(float)sz.x, (float)sz.y * 0.5f});
        }

        // HUD overlay: crosshair, HP bar, score, FPS via SDL3pp_ui Canvas
        ui.Frame(frameTimer.GetDelta());
        renderer.Present();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Menu  (SDL3pp_ui retained-mode widgets)
    // ─────────────────────────────────────────────────────────────────────────

    void DrawMenu()
    {
        const SDL::Point sz = renderer.GetOutputSize();
        renderer.SetDrawColor({5, 7, 16, 255});
        renderer.RenderClear();
        // Subtle scanlines rendered before the UI
        renderer.SetDrawColor({12, 14, 28, 255});
        for (int y = 0; y < sz.y; y += 4)
            renderer.RenderLine({0.f, (float)y}, {(float)sz.x, (float)y});

        ui.Frame(frameTimer.GetDelta());
        renderer.Present();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Game-over screen (SDL3pp_ui)
    // ─────────────────────────────────────────────────────────────────────────

    void DrawGameOver()
    {
        renderer.SetDrawColor({5, 0, 0, 255});
        renderer.RenderClear();
        ui.Frame(frameTimer.GetDelta());
        renderer.Present();
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Events
    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Event(const SDL::Event &ev)
    {
        if (ev.type == SDL::EVENT_QUIT)
            return SDL::APP_SUCCESS;
        switch (appState)
        {
        case AppState::Menu:
            return EventMenu(ev);
        case AppState::GameOver:
            return EventGameOver(ev);
        default:
            return EventPlaying(ev);
        }
    }

    SDL::AppResult EventMenu(const SDL::Event &ev)
    {
        ui.ProcessEvent(ev);
        if (ev.type != SDL::EVENT_KEY_DOWN)
            return SDL::APP_CONTINUE;
        static constexpr int kMapScales[] = {16, 32, 48};
        int mapIdx = (config.mapScale == 32) ? 1 : (config.mapScale == 48) ? 2
                                                                           : 0;
        switch (ev.key.key)
        {
        case SDL::KEYCODE_ESCAPE:
            return SDL::APP_SUCCESS;
        case SDL::KEYCODE_UP:
            menuSel = (menuSel + 2) % 3;
            break;
        case SDL::KEYCODE_DOWN:
            menuSel = (menuSel + 1) % 3;
            break;
        case SDL::KEYCODE_LEFT:
            if (menuSel == 0)
                config.mapScale = kMapScales[(mapIdx + 2) % 3];
            if (menuSel == 1)
                config.playerCount = std::max(1, config.playerCount - 1);
            if (menuSel == 2)
                config.enemyCount = std::max(0, config.enemyCount - 1);
            _UpdateMenuLabels();
            break;
        case SDL::KEYCODE_RIGHT:
            if (menuSel == 0)
                config.mapScale = kMapScales[(mapIdx + 1) % 3];
            if (menuSel == 1)
                config.playerCount = std::min(MAX_PLAYERS, config.playerCount + 1);
            if (menuSel == 2)
                config.enemyCount = std::min(20, config.enemyCount + 1);
            _UpdateMenuLabels();
            break;
        case SDL::KEYCODE_RETURN:
        case SDL::KEYCODE_KP_ENTER:
            StartGame();
            break;
        default:
            break;
        }
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult EventGameOver(const SDL::Event &ev)
    {
        ui.ProcessEvent(ev);
        if (ev.type == SDL::EVENT_KEY_DOWN &&
            (ev.key.key == SDL::KEYCODE_RETURN || ev.key.key == SDL::KEYCODE_KP_ENTER))
        {
            appState = AppState::Menu;
            window.SetRelativeMouseMode(false);
            _ShowPage(AppState::Menu);
        }
        return SDL::APP_CONTINUE;
    }

    SDL::AppResult EventPlaying(const SDL::Event &ev)
    {
        if (ev.type == SDL::EVENT_MOUSE_REMOVED)
        {
            for (int i = 0; i < numPlayers; ++i)
            {
                if (auto *inp = world.Get<PlayerInput>(playerIds[i]))
                    if (inp->mouse == ev.mdevice.which)
                        inp->mouse = 0;
            }
        }
        else if (ev.type == SDL::EVENT_KEYBOARD_REMOVED)
        {
            for (int i = 0; i < numPlayers; ++i)
            {
                if (auto *inp = world.Get<PlayerInput>(playerIds[i]))
                    if (inp->keyboard == ev.kdevice.which)
                        inp->keyboard = 0;
            }
        }
        else if (ev.type == SDL::EVENT_MOUSE_MOTION)
        {
            SDL::MouseID id = ev.motion.which;
            int idx = WhoseMouse(id);
            if (idx >= 0)
            {
                Look &look = *world.Get<Look>(playerIds[idx]);
                look.yaw -= ev.motion.xrel * kMouseSensitivity;
                look.pitch = SDL::Clamp(look.pitch - ev.motion.yrel * kMouseSensitivity,
                                        -(float)(std::numbers::pi * 0.45),
                                        (float)(std::numbers::pi * 0.45));
            }
            else if (id)
            {
                for (int i = 0; i < numPlayers; ++i)
                {
                    if (auto *inp = world.Get<PlayerInput>(playerIds[i]))
                        if (inp->mouse == 0)
                        {
                            inp->mouse = id;
                            break;
                        }
                }
            }
        }
        else if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN)
        {
            int idx = WhoseMouse(ev.button.which);
            if (idx >= 0)
                FireBullet(playerIds[idx], kPlayerDamage);
        }
        else if (ev.type == SDL::EVENT_KEY_DOWN)
        {
            SDL::Keycode sym = ev.key.key;
            SDL::KeyboardID id = ev.key.which;
            if (sym == SDL::KEYCODE_ESCAPE)
            {
                appState = AppState::Menu;
                window.SetRelativeMouseMode(false);
                _ShowPage(AppState::Menu);
                return SDL::APP_CONTINUE;
            }
            int idx = WhoseKeyboard(id);
            if (idx >= 0)
            {
                auto &inp = *world.Get<PlayerInput>(playerIds[idx]);
                if (sym == SDL::KEYCODE_W)
                    inp.wasd |= 1;
                if (sym == SDL::KEYCODE_A)
                    inp.wasd |= 2;
                if (sym == SDL::KEYCODE_S)
                    inp.wasd |= 4;
                if (sym == SDL::KEYCODE_D)
                    inp.wasd |= 8;
                if (sym == SDL::KEYCODE_SPACE)
                    inp.wasd |= 16;
            }
            else if (id)
            {
                for (int i = 0; i < numPlayers; ++i)
                {
                    if (auto *inp = world.Get<PlayerInput>(playerIds[i]))
                        if (inp->keyboard == 0)
                        {
                            inp->keyboard = id;
                            break;
                        }
                }
            }
        }
        else if (ev.type == SDL::EVENT_KEY_UP)
        {
            int idx = WhoseKeyboard(ev.key.which);
            if (idx >= 0)
            {
                auto &inp = *world.Get<PlayerInput>(playerIds[idx]);
                SDL::Keycode sym = ev.key.key;
                if (sym == SDL::KEYCODE_W)
                    inp.wasd &= ~1;
                if (sym == SDL::KEYCODE_A)
                    inp.wasd &= ~2;
                if (sym == SDL::KEYCODE_S)
                    inp.wasd &= ~4;
                if (sym == SDL::KEYCODE_D)
                    inp.wasd &= ~8;
                if (sym == SDL::KEYCODE_SPACE)
                    inp.wasd &= ~16;
            }
        }
        return SDL::APP_CONTINUE;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // Main loop
    // ─────────────────────────────────────────────────────────────────────────

    SDL::AppResult Iterate()
    {
        frameTimer.Begin();
        ++frameCount;
        switch (appState)
        {
        case AppState::Menu:
            DrawMenu();
            break;
        case AppState::GameOver:
            DrawGameOver();
            break;
        case AppState::Playing:
        {
            float dt = std::min(frameTimer.GetDelta(), 0.05f);
            UpdatePhysics(dt);
            UpdateEnemies(dt);
            UpdateBullets(dt);
            DrawGame();
            break;
        }
        }
        frameTimer.End();
        return SDL::APP_CONTINUE;
    }

    // ─────────────────────────────────────────────────────────────────────────
    // SDL3pp_ui — build the retained-mode widget tree
    // ─────────────────────────────────────────────────────────────────────────

    // Helpers for config value strings
    std::string MapSizeName() const
    {
        if (config.mapScale == 32)
            return "Medium (32)";
        if (config.mapScale == 48)
            return "Large  (48)";
        return "Small  (16)";
    }
    std::string PlayersStr() const
    {
        return std::format("{} ({})", config.playerCount,
                           config.playerCount == 1 ? "Solo" : "Multi");
    }

    // Update the dynamic labels in the menu (called after any config change)
    void _UpdateMenuLabels()
    {
        if (eid_mapVal != SDL::ECS::NullEntity)
            ui.SetText(eid_mapVal, MapSizeName());
        if (eid_plVal != SDL::ECS::NullEntity)
            ui.SetText(eid_plVal, PlayersStr());
        if (eid_enVal != SDL::ECS::NullEntity)
            ui.SetText(eid_enVal, std::to_string(config.enemyCount));
    }

    // Refresh the Game-Over score labels from the last game's data
    void _UpdateGameOverLabels()
    {
        for (int i = 0; i < MAX_PLAYERS; ++i)
        {
            if (eid_goScore[i] == SDL::ECS::NullEntity)
                continue;
            if (i >= numPlayers || playerIds[i] == SDL::ECS::NullEntity)
            {
                ui.SetVisible(eid_goScore[i], false);
            }
            else
            {
                const Score *sc = world.Get<Score>(playerIds[i]);
                ui.SetText(eid_goScore[i],
                           std::format("Player {}: {} kill(s)", i + 1, sc ? sc->points : 0));
                ui.SetVisible(eid_goScore[i], true);
            }
        }
    }

    // Show the correct UI page for the given app state
    void _ShowPage(AppState state)
    {
        ui.SetVisible(eid_menuPage, state == AppState::Menu);
        ui.SetVisible(eid_goPage, state == AppState::GameOver);
        ui.SetVisible(eid_hudPage, state == AppState::Playing);
    }

    // ── Menu page ─────────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildMenuPage()
    {
        constexpr SDL::Color kTitle = {235, 200, 45, 255};
        constexpr SDL::Color kSub = {80, 90, 130, 255};
        constexpr SDL::Color kDim = {160, 165, 185, 255};
        constexpr SDL::Color kHint = {60, 65, 95, 255};
        constexpr SDL::Color kBtnBg = {40, 55, 100, 255};
        constexpr SDL::Color kStartC = {40, 130, 60, 255};

        // Panel container
        auto panel = ui.Column("menu_panel", 10.f, 0.f)
                         .W(400.f)
                         .Padding(20, 14)
                         .BgColor({9, 11, 26, 230})
                         .BorderColor({55, 75, 135, 255})
                         .Borders(SDL::FBox(1.f))
                         .Radius(SDL::FCorners(4.f));

        panel.Children(
            ui.Label("menu_title", "WOODENEYE  008  GPU").TextColor(kTitle),
            ui.Label("menu_sub", "hardware-accelerated").TextColor(kSub),
            ui.Sep("menu_sep1"));

        // Helper: build a config row with < label > navigation buttons
        auto mkRow = [&](const char *rowId, const char *label,
                         const char *valId, const std::string &initVal,
                         auto onLeft, auto onRight) -> SDL::ECS::EntityId
        {
            auto valLbl = ui.Label(valId, initVal)
                              .W(140)
                              .TextColor(kDim)
                              .AlignSelf(SDL::UI::Align::Center);
            panel.Child(
                ui.Row(rowId, 8.f, 0.f)
                    .Style(SDL::UI::Theme::Transparent())
                    .H(30)
                    .Children(
                        ui.Label(std::string(rowId) + "_lbl", label).W(110).TextColor(kDim).AlignSelf(SDL::UI::Align::Center),
                        ui.Button(std::string(rowId) + "_l", " < ").W(28).H(26).Style(SDL::UI::Theme::PrimaryButton(kBtnBg)).AlignSelf(SDL::UI::Align::Center).OnClick(onLeft),
                        valLbl,
                        ui.Button(std::string(rowId) + "_r", " > ").W(28).H(26).Style(SDL::UI::Theme::PrimaryButton(kBtnBg)).AlignSelf(SDL::UI::Align::Center).OnClick(onRight)));
            return valLbl.Id();
        };

        static constexpr int kMapScales[] = {16, 32, 48};

        eid_mapVal = mkRow("menu_map", "Map size:", "menu_map_val", MapSizeName(), [this]
                           { int i=(config.mapScale==32)?1:(config.mapScale==48)?2:0;
                config.mapScale=kMapScales[(i+2)%3]; _UpdateMenuLabels(); }, [this]
                           { int i=(config.mapScale==32)?1:(config.mapScale==48)?2:0;
                config.mapScale=kMapScales[(i+1)%3]; _UpdateMenuLabels(); });

        eid_plVal = mkRow("menu_pl", "Players:", "menu_pl_val", PlayersStr(), [this]
                          { config.playerCount=std::max(1,config.playerCount-1); _UpdateMenuLabels(); }, [this]
                          { config.playerCount=std::min(MAX_PLAYERS,config.playerCount+1); _UpdateMenuLabels(); });

        eid_enVal = mkRow("menu_en", "Enemies:", "menu_en_val", std::to_string(config.enemyCount), [this]
                          { config.enemyCount=std::max(0,config.enemyCount-1); _UpdateMenuLabels(); }, [this]
                          { config.enemyCount=std::min(20,config.enemyCount+1); _UpdateMenuLabels(); });

        panel.Children(
            ui.Sep("menu_sep2"),
            ui.Button("menu_start", "[ START GAME ]")
                .H(36)
                .W(SDL::UI::Value::Pw(100))
                .Style(SDL::UI::Theme::PrimaryButton(kStartC))
                .OnClick([this]
                         { StartGame(); }));

        // Hint row below the panel
        auto hints = ui.Column("menu_hints", 4.f, 0.f)
                         .Style(SDL::UI::Theme::Transparent())
                         .PaddingV(4.f);
        hints.Children(
            ui.Label("hint1", "UP/DOWN: select    LEFT/RIGHT: change    ENTER: start")
                .TextColor(kHint),
            ui.Label("hint2", "WASD+Space: move   Mouse: look   LMB: fire   ESC: quit")
                .TextColor(kHint));

        // Page: transparent (scanlines drawn raw before ui.Frame()), panel centered
        auto page = ui.Column("menu_page", 16.f, 0.f)
                        .W(SDL::UI::Value::Ww(100))
                        .H(SDL::UI::Value::Wh(100))
                        .Padding(0)
                        .PaddingTop(80.f)
                        .Align(SDL::UI::Align::Center)
                        .WithStyle([](auto &s)
                                   {
            s.borders = SDL::FBox(0.f);
            s.bgColor = {0, 0, 0, 0}; })
                        .Children(panel, hints);

        return page;
    }

    // ── Game-over page ────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildGameOverPage()
    {
        auto card = ui.Column("go_card", 12.f, 0.f)
                        .W(340.f)
                        .Padding(24, 16)
                        .BgColor({22, 4, 4, 240})
                        .BorderColor({90, 20, 20, 255})
                        .Borders(SDL::FBox(1.f))
                        .Radius(SDL::FCorners(4.f));

        card.Children(
            ui.Label("go_title", "GAME  OVER").TextColor({255, 40, 40, 255}),
            ui.Sep("go_sep"));

        for (int i = 0; i < MAX_PLAYERS; ++i)
        {
            auto lbl = ui.Label(std::format("go_p{}", i + 1),
                                std::format("Player {}: -", i + 1))
                           .TextColor({190, 190, 200, 255});
            eid_goScore[i] = lbl.Id();
            card.Child(lbl);
        }

        card.Children(
            ui.Sep("go_sep2"),
            ui.Button("go_return", "Return to Menu")
                .H(32)
                .W(200)
                .AlignSelf(SDL::UI::Align::Center)
                .Style(SDL::UI::Theme::PrimaryButton({70, 75, 100, 255}))
                .OnClick([this]
                         {
              appState = AppState::Menu;
              window.SetRelativeMouseMode(false);
              _ShowPage(AppState::Menu); }));

        auto page = ui.Column("go_page", 0.f, 0.f)
                        .W(SDL::UI::Value::Ww(100))
                        .H(SDL::UI::Value::Wh(100))
                        .Padding(0)
                        .Align(SDL::UI::Align::Center)
                        .WithStyle([](auto &s)
                                   {
                                       s.borders = SDL::FBox(0.f);
                                       s.bgColor = {0, 0, 0, 0}; // background drawn raw
                                   })
                        .Child(card);

        return page;
    }

    // ── In-game HUD page ──────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildHudPage()
    {
        // Full-window transparent Canvas that draws the 2D HUD on top of the 3D scene
        auto canvas = ui.CanvasWidget("hud_canvas", nullptr, nullptr,
                                      [this](SDL::RendererRef /*r*/, SDL::FRect rect)
                                      {
                                          _DrawHUD(rect);
                                      })
                          .W(SDL::UI::Value::Ww(100))
                          .H(SDL::UI::Value::Wh(100))
                          .Padding(0);

        auto page = ui.Column("hud_page", 0.f, 0.f)
                        .W(SDL::UI::Value::Ww(100))
                        .H(SDL::UI::Value::Wh(100))
                        .Padding(0)
                        .WithStyle([](auto &s)
                                   {
                                       s.borders = SDL::FBox(0.f);
                                       s.bgColor = {0, 0, 0, 0}; // fully transparent – 3D scene shows through
                                   })
                        .Child(canvas);

        return page;
    }

    // ── Root tree ─────────────────────────────────────────────────────────────

    void _BuildUI()
    {
        ui.LoadFont("font", "assets/fonts/DejaVuSans.ttf");
        ui.SetDefaultFont("font", 14.f);

        eid_menuPage = _BuildMenuPage();
        eid_goPage = _BuildGameOverPage();
        eid_hudPage = _BuildHudPage();

        ui.Column("ui_root", 0.f, 0.f)
            .W(SDL::UI::Value::Ww(100))
            .H(SDL::UI::Value::Wh(100))
            .Padding(0)
            .WithStyle([](auto &s)
                       {
            s.borders = SDL::FBox(0.f);
            s.bgColor = {0, 0, 0, 0}; })
            .Children(eid_menuPage, eid_goPage, eid_hudPage)
            .AsRoot();

        // Start in Menu state: show only the menu page
        _ShowPage(AppState::Menu);
    }

private:
    int WhoseMouse(SDL::MouseID id) const
    {
        for (int i = 0; i < numPlayers; ++i)
        {
            if (playerIds[i] == SDL::ECS::NullEntity)
                continue;
            if (const auto *inp = world.Get<PlayerInput>(playerIds[i]))
                if (inp->mouse == id)
                    return i;
        }
        return -1;
    }
    int WhoseKeyboard(SDL::KeyboardID id) const
    {
        for (int i = 0; i < numPlayers; ++i)
        {
            if (playerIds[i] == SDL::ECS::NullEntity)
                continue;
            if (const auto *inp = world.Get<PlayerInput>(playerIds[i]))
                if (inp->keyboard == id)
                    return i;
        }
        return -1;
    }
};

SDL3PP_DEFINE_CALLBACKS(Main)
