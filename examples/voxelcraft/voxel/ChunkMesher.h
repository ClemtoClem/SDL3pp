#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  ChunkMesher.h – Construction du mesh d'un chunk (face-culling + AO)
// ══════════════════════════════════════════════════════════════════════════
#include "Chunk.h"
#include "World.h"
#include <cmath>

// Construit le mesh CPU d'un chunk.
// Signature attendue par Renderer::UploadDirtyChunks().
void BuildChunkMesh(const ChunkPos& pos, const ChunkBlocks& blocks,
                    const World& world, ChunkMesh& mesh);


// ── Helpers ───────────────────────────────────────────────────────────────────

static inline float ComputeAO(const World& world,
                        int wx, int wy, int wz,
                        int d1x, int d1y, int d1z,
                        int d2x, int d2y, int d2z)
{
    bool s1     = blockSolid(world.GetBlock(wx+d1x, wy+d1y, wz+d1z));
    bool s2     = blockSolid(world.GetBlock(wx+d2x, wy+d2y, wz+d2z));
    bool corner = blockSolid(world.GetBlock(wx+d1x+d2x, wy+d1y+d2y, wz+d1z+d2z));
    if (s1 && s2) return 0.1f;
    return 1.f - ((int)s1 + (int)s2 + (int)corner) * 0.2f;
}

// Détermine si la face entre self et neighbor doit être émise
static inline bool ShouldEmitFace(BlockID self, BlockID neighbor)
{
    if (neighbor == BlockID::AIR)                        return true;
    if (blockTransparent(neighbor) && !blockTransparent(self)) return true;
    if (blockLiquid(self) && !blockLiquid(neighbor))     return false;
    if (blockTransparent(self) && blockLiquid(neighbor)) return false;
    if (blockTransparent(self))                          return true;
    return false; // opaque → opaque
}

// Emet 4 sommets + 2 triangles pour une face
static inline void EmitFace(ChunkMesh& mesh,
                      float lx, float ly, float lz,
                      int face, float layer,
                      float ao0, float ao1, float ao2, float ao3)
{
    float faceL = kFaceLight[face];
    uint32_t base = (uint32_t)mesh.vertices.size();

    for (int v = 0; v < 4; ++v) {
        float ao = (v == 0) ? ao0 : (v == 1) ? ao1 : (v == 2) ? ao2 : ao3;
        mesh.vertices.push_back({
            lx + kFaceVerts[face][v][0],
            ly + kFaceVerts[face][v][1],
            lz + kFaceVerts[face][v][2],
            kFaceUVs[face][v][0],
            kFaceUVs[face][v][1],
            layer,
            ao * faceL
        });
    }

    // Diagonale optimisee pour l'AO (evite les artefacts de bande)
    if (ao0 + ao2 < ao1 + ao3) {
        mesh.indices.insert(mesh.indices.end(),
            {base, base+1, base+2, base, base+2, base+3});
    } else {
        mesh.indices.insert(mesh.indices.end(),
            {base+1, base+2, base+3, base+1, base+3, base});
    }
}

// ── Fonction principale ───────────────────────────────────────────────────────

inline void BuildChunkMesh(const ChunkPos& pos, const ChunkBlocks& blocks,
                    const World& world, ChunkMesh& mesh)
{
    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.indexCount = 0;
    mesh.dirty    = false;
    mesh.gpuDirty = true;

    for (int ly = 0; ly < CHUNK_H; ++ly) {
        for (int lx = 0; lx < CHUNK_W; ++lx) {
            for (int lz = 0; lz < CHUNK_D; ++lz) {
                BlockID self = blocks.GetLocal(lx, ly, lz);
                if (self == BlockID::AIR) continue;

                const BlockDef& def = blockDef(self);
                int wx = pos.cx * CHUNK_W + lx;
                int wy = pos.cy * CHUNK_H + ly;
                int wz = pos.cz * CHUNK_D + lz;

                for (int f = 0; f < 6; ++f) {
                    // Bloc voisin (local si possible, sinon via World)
                    int nlx = lx + kNeighborOff[f][0];
                    int nly = ly + kNeighborOff[f][1];
                    int nlz = lz + kNeighborOff[f][2];

                    BlockID neighbor;
                    if (nlx >= 0 && nlx < CHUNK_W &&
                        nly >= 0 && nly < CHUNK_H &&
                        nlz >= 0 && nlz < CHUNK_D)
                    {
                        neighbor = blocks.GetLocal(nlx, nly, nlz);
                    } else {
                        neighbor = world.GetBlock(
                            wx + kNeighborOff[f][0],
                            wy + kNeighborOff[f][1],
                            wz + kNeighborOff[f][2]);
                    }

                    if (!ShouldEmitFace(self, neighbor)) continue;

                    // Texture : choisir top/bottom/side
                    AtlasSlot slot;
                    switch (f) {
                        case 0:  slot = def.texTop;    break;
                        case 1:  slot = def.texBottom; break;
                        default: slot = def.texSide;   break;
                    }
                    float layer = (float)atlasLayer(slot);

                    // AO des 4 coins (calcule seulement pour top et bottom)
                    float ao[4] = {1.f, 1.f, 1.f, 1.f};
                    if (f == 0) { // TOP
                        ao[0] = ComputeAO(world, wx,wy+1,wz, -1,0,0, 0,0,-1);
                        ao[1] = ComputeAO(world, wx,wy+1,wz, +1,0,0, 0,0,-1);
                        ao[2] = ComputeAO(world, wx,wy+1,wz, +1,0,0, 0,0,+1);
                        ao[3] = ComputeAO(world, wx,wy+1,wz, -1,0,0, 0,0,+1);
                    } else if (f == 1) { // BOTTOM
                        ao[0] = ComputeAO(world, wx,wy,wz, -1,0,0, 0,0,+1);
                        ao[1] = ComputeAO(world, wx,wy,wz, +1,0,0, 0,0,+1);
                        ao[2] = ComputeAO(world, wx,wy,wz, +1,0,0, 0,0,-1);
                        ao[3] = ComputeAO(world, wx,wy,wz, -1,0,0, 0,0,-1);
                    }
                    // AO lateral simplifie (faces N/S/E/W)
                    else {
                        // Occlusion basee sur les blocs lateraux adjacents a la face
                        int nx = kNeighborOff[f][0], nz = kNeighborOff[f][2];
                        // Pour NORTH/SOUTH, variation le long de X et Y
                        if (f == 2 || f == 3) {
                            bool sl = blockSolid(world.GetBlock(wx-1, wy,   wz+nz));
                            bool sr = blockSolid(world.GetBlock(wx+1, wy,   wz+nz));
                            bool su = blockSolid(world.GetBlock(wx,   wy+1, wz+nz));
                            bool sd = blockSolid(world.GetBlock(wx,   wy-1, wz+nz));
                            ao[0] = 1.f - ((int)(sl||sd)+(int)(sl&&sd))*0.2f;
                            ao[1] = 1.f - ((int)(sl||su)+(int)(sl&&su))*0.2f;
                            ao[2] = 1.f - ((int)(sr||su)+(int)(sr&&su))*0.2f;
                            ao[3] = 1.f - ((int)(sr||sd)+(int)(sr&&sd))*0.2f;
                        }
                        // Pour EAST/WEST, variation le long de Z et Y
                        else {
                            bool sf = blockSolid(world.GetBlock(wx+nx, wy,   wz-1));
                            bool sb = blockSolid(world.GetBlock(wx+nx, wy,   wz+1));
                            bool su = blockSolid(world.GetBlock(wx+nx, wy+1, wz));
                            bool sd = blockSolid(world.GetBlock(wx+nx, wy-1, wz));
                            ao[0] = 1.f - ((int)(sb||su)+(int)(sb&&su))*0.2f;
                            ao[1] = 1.f - ((int)(sf||su)+(int)(sf&&su))*0.2f;
                            ao[2] = 1.f - ((int)(sf||sd)+(int)(sf&&sd))*0.2f;
                            ao[3] = 1.f - ((int)(sb||sd)+(int)(sb&&sd))*0.2f;
                        }
                    }

                    EmitFace(mesh, (float)lx, (float)ly, (float)lz,
                             f, layer, ao[0], ao[1], ao[2], ao[3]);
                }
            }
        }
    }

    mesh.indexCount = (uint32_t)mesh.indices.size();
}
