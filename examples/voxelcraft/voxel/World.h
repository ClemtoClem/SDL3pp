#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  World.h – Voxel world: ECS-based 3D chunk grid with biomes
// ══════════════════════════════════════════════════════════════════════════
#include "Chunk.h"
#include "Noise.h"
#include <SDL3pp/SDL3pp_ecs.h>
#include <unordered_map>
#include <cstdint>
#include <cmath>
#include <functional>
#include <algorithm>
#include <random>

enum class Biome : uint8_t {
    Plains,
    Desert,
    River,
    Mountain,
    Ocean,
};

class World {
public:
    SDL::ECS::Context ecs;
    uint32_t seed = 42337;

    explicit World(uint32_t s = 42337) : seed(s), noise_() {}

    // Generate all chunks (initial fill)
    void GenerateAll(std::function<void(int, int)> onProgress = nullptr);

    // Incremental generation (call once per chunk per frame for loading screen)
    void CreateChunkAt(int cx, int cy, int cz);
    void RunTreesAt(int cx, int cz) { PlaceTrees(cx, cz); }

    // World-space block access
    BlockID GetBlock(int wx, int wy, int wz) const;
    void    SetBlock(int wx, int wy, int wz, BlockID b);
    int     GetTopBlock(int wx, int wz) const;
    bool    IsSolid(int wx, int wy, int wz) const { return blockSolid(GetBlock(wx,wy,wz)); }

    // Get biome at world column (wx, wz)
    Biome GetBiome(int wx, int wz) const;

    static ChunkPos WorldToChunk(int wx, int wy, int wz) {
        int cx = (int)std::floor((float)wx / CHUNK_W);
        int cy = (int)std::floor((float)wy / CHUNK_H);
        int cz = (int)std::floor((float)wz / CHUNK_D);
        return {cx, cy, cz};
    }

private:
    Noise noise_;
    std::unordered_map<ChunkPos, SDL::ECS::EntityId, ChunkPosHash> chunkMap_;

    SDL::ECS::EntityId FindEntity(int cx, int cy, int cz) const;
    ChunkBlocks*       GetBlocks(int cx, int cy, int cz);
    const ChunkBlocks* GetBlocks(int cx, int cy, int cz) const;

    void CreateChunk(int cx, int cy, int cz);
    void FillBlocks(int cx, int cy, int cz, ChunkBlocks& out);
    void PlaceTrees(int cx, int cz);

    // Terrain sampling
    int   TerrainHeight(int wx, int wz) const;
    Biome BiomeAt(int wx, int wz) const;
    float CaveNoise(int wx, int wy, int wz) const;
};


// ── Terrain functions ─────────────────────────────────────────────────────────

inline int World::TerrainHeight(int wx, int wz) const {
    // Continent/ocean mask
    float continent = noise_.Fbm((float)wx, (float)wz, 0.004f, 4, 0.5f) * 0.5f + 0.5f;
    // Base height from low-frequency noise
    float base = noise_.Fbm((float)(wx + (int)seed), (float)(wz + (int)seed), 0.007f, 5, 0.5f) * 0.5f + 0.5f;
    // Fine detail
    float detail = noise_.Fbm((float)(wx - (int)seed), (float)(wz - (int)seed), 0.03f, 2) * 0.5f + 0.5f;

    float h = base * 0.70f + detail * 0.10f + continent * 0.20f;
    return (int)(4 + h * 54);  // [4, 58]
}

inline Biome World::BiomeAt(int wx, int wz) const {
    float temp = noise_.Fbm((float)(wx + 50000), (float)(wz + 50000), 0.003f, 2) * 0.5f + 0.5f;
    float humi = noise_.Fbm((float)(wx - 50000), (float)(wz - 50000), 0.003f, 2) * 0.5f + 0.5f;
    int groundY = TerrainHeight(wx, wz);

    if (groundY < SEA_LEVEL - 4)                return Biome::Ocean;
    if (groundY < SEA_LEVEL + 1)               return Biome::River;
    if (groundY > 38)                           return Biome::Mountain;
    if (temp > 0.60f && humi < 0.40f)          return Biome::Desert;
    return Biome::Plains;
}

inline Biome World::GetBiome(int wx, int wz) const { return BiomeAt(wx, wz); }

inline float World::CaveNoise(int wx, int wy, int wz) const {
    return noise_.Fbm3D((float)wx * 0.07f, (float)wy * 0.11f, (float)wz * 0.07f, 1.f, 3, 0.5f);
}

// ── World setup ───────────────────────────────────────────────────────────────

inline void World::GenerateAll(std::function<void(int, int)> onProgress) {
    int total = WORLD_CX * WORLD_CY * WORLD_CZ;
    int done  = 0;

    // First pass: create all chunks and fill blocks
    for (int cx = 0; cx < WORLD_CX; ++cx)
        for (int cy = 0; cy < WORLD_CY; ++cy)
            for (int cz = 0; cz < WORLD_CZ; ++cz) {
                CreateChunk(cx, cy, cz);
                ++done;
                if (onProgress) onProgress(done, total * 2);
            }

    // Second pass: place trees (needs neighbors to exist)
    for (int cx = 1; cx < WORLD_CX - 1; ++cx)
        for (int cz = 1; cz < WORLD_CZ - 1; ++cz) {
            PlaceTrees(cx, cz);
            ++done;
            if (onProgress) onProgress(done, total * 2);
        }
}

// ── Chunk lookup ──────────────────────────────────────────────────────────────

inline SDL::ECS::EntityId World::FindEntity(int cx, int cy, int cz) const {
    auto it = chunkMap_.find({cx, cy, cz});
    return (it != chunkMap_.end()) ? it->second : SDL::ECS::NullEntity;
}

inline ChunkBlocks* World::GetBlocks(int cx, int cy, int cz) {
    auto e = FindEntity(cx, cy, cz);
    if (e == SDL::ECS::NullEntity) return nullptr;
    return ecs.Get<ChunkBlocks>(e);
}

inline const ChunkBlocks* World::GetBlocks(int cx, int cy, int cz) const {
    auto e = FindEntity(cx, cy, cz);
    if (e == SDL::ECS::NullEntity) return nullptr;
    return ecs.Get<ChunkBlocks>(e);
}

// ── Public block access ───────────────────────────────────────────────────────

inline BlockID World::GetBlock(int wx, int wy, int wz) const {
    if (wy < 0 || wy >= WORLD_TOTAL_H) return BlockID::AIR;
    ChunkPos cp = WorldToChunk(wx, wy, wz);
    const ChunkBlocks* cb = GetBlocks(cp.cx, cp.cy, cp.cz);
    if (!cb) return BlockID::AIR;
    int lx = wx - cp.cx * CHUNK_W;
    int ly = wy - cp.cy * CHUNK_H;
    int lz = wz - cp.cz * CHUNK_D;
    return cb->GetLocal(lx, ly, lz);
}

inline void World::SetBlock(int wx, int wy, int wz, BlockID b) {
    if (wy < 0 || wy >= WORLD_TOTAL_H) return;
    ChunkPos cp = WorldToChunk(wx, wy, wz);
    auto e = FindEntity(cp.cx, cp.cy, cp.cz);
    if (e == SDL::ECS::NullEntity) return;
    ChunkBlocks* cb = ecs.Get<ChunkBlocks>(e);
    if (!cb) return;
    int lx = wx - cp.cx * CHUNK_W;
    int ly = wy - cp.cy * CHUNK_H;
    int lz = wz - cp.cz * CHUNK_D;
    cb->SetLocal(lx, ly, lz, b);

    auto markDirty = [&](int dcx, int dcy, int dcz) {
        auto ne = FindEntity(cp.cx + dcx, cp.cy + dcy, cp.cz + dcz);
        if (ne != SDL::ECS::NullEntity)
            if (ChunkMesh* nm = ecs.Get<ChunkMesh>(ne)) { nm->dirty = true; nm->gpuDirty = true; }
    };
    if (ChunkMesh* m = ecs.Get<ChunkMesh>(e)) { m->dirty = true; m->gpuDirty = true; }
    if (lx == 0)          markDirty(-1,  0,  0);
    if (lx == CHUNK_W-1)  markDirty(+1,  0,  0);
    if (ly == 0)          markDirty( 0, -1,  0);
    if (ly == CHUNK_H-1)  markDirty( 0, +1,  0);
    if (lz == 0)          markDirty( 0,  0, -1);
    if (lz == CHUNK_D-1)  markDirty( 0,  0, +1);
}

inline int World::GetTopBlock(int wx, int wz) const {
    for (int y = WORLD_TOTAL_H - 1; y >= 0; --y)
        if (blockSolid(GetBlock(wx, y, wz))) return y;
    return 0;
}

// ── Chunk creation ────────────────────────────────────────────────────────────

inline void World::CreateChunk(int cx, int cy, int cz) {
    auto e = ecs.CreateEntity();
    ecs.Add<ChunkPos>(e, {cx, cy, cz});
    auto& cb = ecs.Add<ChunkBlocks>(e);
    ecs.Add<ChunkMesh>(e);
    FillBlocks(cx, cy, cz, cb);
    chunkMap_[{cx, cy, cz}] = e;
}

inline void World::CreateChunkAt(int cx, int cy, int cz) {
    CreateChunk(cx, cy, cz);
}

// ── Terrain generation ────────────────────────────────────────────────────────

inline void World::FillBlocks(int cx, int cy, int cz, ChunkBlocks& out) {
    int baseWY = cy * CHUNK_H;

    for (int lx = 0; lx < CHUNK_W; ++lx) {
        for (int lz = 0; lz < CHUNK_D; ++lz) {
            int wx = cx * CHUNK_W + lx;
            int wz = cz * CHUNK_D + lz;

            int    groundY = TerrainHeight(wx, wz);
            groundY = std::clamp(groundY, 2, WORLD_TOTAL_H - 4);
            Biome  biome   = BiomeAt(wx, wz);

            float temp = noise_.Fbm((float)(wx + 50000), (float)(wz + 50000), 0.003f, 2) * 0.5f + 0.5f;

            for (int ly = 0; ly < CHUNK_H; ++ly) {
                int wy = baseWY + ly;
                BlockID b = BlockID::AIR;

                if (wy == 0) {
                    b = BlockID::STONE;  // bedrock
                    out.SetLocal(lx, ly, lz, b);
                    continue;
                }

                switch (biome) {
                case Biome::Ocean:
                    if (wy <= groundY)             b = (wy == groundY) ? BlockID::GRAVEL : BlockID::STONE;
                    else if (wy <= SEA_LEVEL)      b = BlockID::WATER;
                    break;

                case Biome::River:
                    if (wy <= groundY - 2)         b = BlockID::STONE;
                    else if (wy <= groundY)        b = BlockID::SAND;
                    else if (wy <= SEA_LEVEL)      b = BlockID::WATER;
                    break;

                case Biome::Desert:
                    if (wy < groundY - 3)          b = BlockID::STONE;
                    else if (wy <= groundY)        b = BlockID::SAND;
                    break;

                case Biome::Mountain:
                    if (wy <= groundY) {
                        bool isSnow = (temp < 0.35f) && (wy >= groundY - 1);
                        bool isSurf = (wy == groundY);
                        if (isSurf && isSnow)      b = BlockID::SNOW;
                        else if (wy > groundY - 2) b = BlockID::STONE;
                        else                       b = BlockID::STONE;
                    }
                    break;

                case Biome::Plains:
                default:
                    if (wy < groundY - 4) {
                        // Deep underground: check caves
                        float cave = CaveNoise(wx, wy, wz);
                        b = (cave > 0.68f) ? BlockID::AIR : BlockID::STONE;
                    } else if (wy < groundY)  b = BlockID::DIRT;
                    else if (wy == groundY) {
                        if      (temp < 0.28f && wy > SEA_LEVEL + 3) b = BlockID::SNOW;
                        else                                           b = BlockID::GRASS;
                    } else if (wy <= SEA_LEVEL && groundY < SEA_LEVEL) b = BlockID::WATER;
                    if (wy == groundY && groundY < SEA_LEVEL)          b = BlockID::GRAVEL;
                    break;
                }

                // Ore veins in stone
                if (b == BlockID::STONE) {
                    if (wy > 4 && wy < 48) {
                        float ore = noise_.Fbm3D((float)wx * 0.25f, (float)wy * 0.25f,
                                                 (float)wz * 0.25f, 1.f, 2, 0.5f);
                        if      (ore > 0.86f) b = BlockID::COAL_ORE;
                        else if (wy < 20) {
                            float cr = noise_.Fbm3D((float)(wx+7777)*0.3f, (float)wy*0.3f,
                                                    (float)(wz+7777)*0.3f, 1.f, 2, 0.5f);
                            if (cr > 0.91f) b = BlockID::CRYSTAL_STONE;
                        }
                    }
                }

                out.SetLocal(lx, ly, lz, b);
            }
        }
    }
}

// ── Tree placement ────────────────────────────────────────────────────────────

inline void World::PlaceTrees(int cx, int cz) {
    std::mt19937 rng(seed ^ ((uint32_t)cx * 73856093u) ^ ((uint32_t)cz * 19349663u));
    std::uniform_real_distribution<float> roll(0.f, 1.f);

    for (int lx = 2; lx < CHUNK_W - 2; ++lx) {
        for (int lz = 2; lz < CHUNK_D - 2; ++lz) {
            if (roll(rng) > 0.06f) continue;

            int wx = cx * CHUNK_W + lx;
            int wz = cz * CHUNK_D + lz;
            Biome biome = BiomeAt(wx, wz);

            // Trees only in Plains
            if (biome != Biome::Plains) continue;

            // Find grass surface
            int surfY = GetTopBlock(wx, wz);
            if (GetBlock(wx, surfY, wz) != BlockID::GRASS) continue;
            if (surfY + TREE_MAX_H + 3 >= WORLD_TOTAL_H)  continue;

            float temp = noise_.Fbm((float)(wx + 50000), (float)(wz + 50000), 0.003f, 2) * 0.5f + 0.5f;
            float humi = noise_.Fbm((float)(wx - 50000), (float)(wz - 50000), 0.003f, 2) * 0.5f + 0.5f;

            BlockID wood   = BlockID::WOOD;
            BlockID leaves = BlockID::LEAVES;
            if (temp > 0.65f && humi < 0.38f) {
                wood = BlockID::PALM_WOOD;   leaves = BlockID::PALM_LEAVES;
            } else if (temp > 0.68f && humi > 0.60f) {
                wood = BlockID::BAOBAB_WOOD; leaves = BlockID::BAOBAB_LEAVES;
            }

            int h = TREE_MIN_H + (int)(roll(rng) * (TREE_MAX_H - TREE_MIN_H));

            for (int i = 1; i <= h; ++i)
                SetBlock(wx, surfY + i, wz, wood);

            int top = surfY + h;
            for (int dx = -2; dx <= 2; ++dx)
                for (int dz = -2; dz <= 2; ++dz)
                    for (int dy = -1; dy <= 1; ++dy) {
                        if (std::abs(dx) + std::abs(dz) + std::abs(dy) * 2 > 3) continue;
                        if (GetBlock(wx+dx, top+dy, wz+dz) == BlockID::AIR)
                            SetBlock(wx+dx, top+dy, wz+dz, leaves);
                    }
        }
    }
}