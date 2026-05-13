#pragma once

#include "Blocks.h"
#include <SDL3pp/SDL3pp_gpu.h>
#include <cstdint>
#include <vector>
#include <functional>

// ── Dimensions ────────────────────────────────────────────────────────────────
static constexpr int CHUNK_W  = 8;
static constexpr int CHUNK_H  = 8;
static constexpr int CHUNK_D  = 8;

static constexpr int WORLD_CX = 20;   // chunks in X
static constexpr int WORLD_CY = 8;    // chunks in Y (vertical stacks)
static constexpr int WORLD_CZ = 20;   // chunks in Z

static constexpr int WORLD_TOTAL_H = WORLD_CY * CHUNK_H; // 64 total world height
static constexpr int SEA_LEVEL     = 20;
static constexpr int TREE_MIN_H    = 4;
static constexpr int TREE_MAX_H    = 7;

// ── Vertex GPU ────────────────────────────────────────────────────────────────
struct VoxelVertex {
    float x, y, z;   // local chunk position
    float u, v;       // UV [0,1]
    float layer;      // texture array layer index
    float light;      // AO × face brightness [0,1]
};

// ── Face geometry tables ──────────────────────────────────────────────────────
// Winding CCW Vulkan. TOP=0 BOTTOM=1 NORTH(-Z)=2 SOUTH(+Z)=3 EAST(+X)=4 WEST(-X)=5
static constexpr float kFaceVerts[6][4][3] = {
    {{0,1,0},{1,1,0},{1,1,1},{0,1,1}},
    {{0,0,1},{1,0,1},{1,0,0},{0,0,0}},
    {{1,0,0},{0,0,0},{0,1,0},{1,1,0}},
    {{0,0,1},{1,0,1},{1,1,1},{0,1,1}},
    {{1,0,1},{1,0,0},{1,1,0},{1,1,1}},
    {{0,0,0},{0,0,1},{0,1,1},{0,1,0}},
};
static constexpr float kFaceUVs[6][4][2] = {
    {{0,0},{1,0},{1,1},{0,1}},
    {{0,0},{1,0},{1,1},{0,1}},
    {{0,1},{1,1},{1,0},{0,0}},
    {{0,1},{1,1},{1,0},{0,0}},
    {{0,1},{1,1},{1,0},{0,0}},
    {{0,1},{1,1},{1,0},{0,0}},
};
static constexpr int kNeighborOff[6][3] = {
    { 0,+1, 0}, { 0,-1, 0}, { 0, 0,-1}, { 0, 0,+1}, {+1, 0, 0}, {-1, 0, 0},
};
static constexpr float kFaceLight[6] = { 1.00f, 0.50f, 0.75f, 0.75f, 0.65f, 0.65f };

// ── ECS Components ────────────────────────────────────────────────────────────

struct ChunkPos { int cx = 0, cy = 0, cz = 0; };

inline bool operator==(const ChunkPos& a, const ChunkPos& b) noexcept {
    return a.cx == b.cx && a.cy == b.cy && a.cz == b.cz;
}

struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& p) const noexcept {
        std::size_t h = std::hash<int>{}(p.cx);
        h ^= std::hash<int>{}(p.cy) + 0x9e3779b9u + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(p.cz) + 0x9e3779b9u + (h << 6) + (h >> 2);
        return h;
    }
};

// Block storage 8×8×8
struct ChunkBlocks {
    BlockID data[CHUNK_W][CHUNK_H][CHUNK_D] = {};

    BlockID GetLocal(int x, int y, int z) const noexcept {
        if ((unsigned)x >= CHUNK_W || (unsigned)y >= CHUNK_H || (unsigned)z >= CHUNK_D)
            return BlockID::AIR;
        return data[x][y][z];
    }
    void SetLocal(int x, int y, int z, BlockID b) noexcept {
        if ((unsigned)x < CHUNK_W && (unsigned)y < CHUNK_H && (unsigned)z < CHUNK_D)
            data[x][y][z] = b;
    }
};

// CPU mesh + GPU buffers
struct ChunkMesh {
    std::vector<VoxelVertex> vertices;
    std::vector<uint32_t>    indices;
    SDL::GPUBuffer           vbuf;
    SDL::GPUBuffer           ibuf;
    uint32_t                 indexCount = 0;
    bool                     dirty    = true;
    bool                     gpuDirty = true;
};
