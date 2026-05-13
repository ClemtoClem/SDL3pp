#pragma once
// ═══════════════════════════════════════════════════════════════════════════
//  Blocks.h – Types de blocs, propriétés et mapping vers l'atlas de textures
// ═══════════════════════════════════════════════════════════════════════════
#include <cstdint>
#include <string_view>
#include <array>

// ── Identifiants de blocs ────────────────────────────────────────────────────
enum class BlockID : uint8_t {
    AIR          = 0,
    GRASS        = 1,
    DIRT         = 2,
    STONE        = 3,
    SAND         = 4,
    GRAVEL       = 5,
    WOOD         = 6,   // tronc de séquoia
    COBBLESTONE  = 7,
    COAL_ORE     = 8,
    WATER        = 9,
    LAVA         = 10,
    MUD          = 11,
    MOSS         = 12,
    SNOW         = 13,
    PALM_WOOD    = 14,
    PALM_LEAVES  = 15,
    BAOBAB_WOOD  = 16,
    BAOBAB_LEAVES= 17,
    MAGMA        = 18,
    CRYSTAL_STONE= 19,
    SULFUR_STONE = 20,
    LEAVES       = 21,  // feuilles de séquoia
    BLOCK_COUNT,
};

// Nombre de types dans l'atlas de textures (8×8 grille)
static constexpr int ATLAS_COLS = 8;
static constexpr int ATLAS_ROWS = 8;
static constexpr int TILE_SIZE  = 16;  // pixels par tuile dans l'atlas

// ── Face index ───────────────────────────────────────────────────────────────
enum class BlockFace : int { TOP=0, BOTTOM=1, NORTH=2, SOUTH=3, EAST=4, WEST=5 };

// ── Slot dans l'atlas (colonne, ligne) ──────────────────────────────────────
struct AtlasSlot { uint8_t col, row; };

// Convertit un AtlasSlot en index de couche (texture array)
inline int atlasLayer(AtlasSlot slot) {
    return (int)slot.row * ATLAS_COLS + (int)slot.col;
}

// ── Propriétés d'un type de bloc ─────────────────────────────────────────────
struct BlockDef {
    std::string_view name;
    bool  solid;
    bool  transparent;
    bool  liquid;
    float hardness;     // temps de minage (secondes)
    AtlasSlot texTop;
    AtlasSlot texBottom;
    AtlasSlot texSide;
    std::string_view soundGroup;
};

// ── Définitions (index = valeur uint8_t de BlockID) ─────────────────────────
inline constexpr std::array<BlockDef, (size_t)BlockID::BLOCK_COUNT> BLOCK_DEFS = {{
    // AIR
    { "air",          false, true,  false, 0.f,   {0,0},{0,0},{0,0},  "none"   },
    // GRASS (1)
    { "grass",        true,  false, false, 0.6f,  {0,0},{1,0},{2,0},  "grass"  },
    // DIRT (2)
    { "dirt",         true,  false, false, 0.5f,  {1,0},{1,0},{1,0},  "dirt"   },
    // STONE (3)
    { "stone",        true,  false, false, 1.5f,  {3,0},{3,0},{3,0},  "stone"  },
    // SAND (4)
    { "sand",         true,  false, false, 0.5f,  {4,0},{4,0},{4,0},  "sand"   },
    // GRAVEL (5)
    { "gravel",       true,  false, false, 0.6f,  {5,0},{5,0},{5,0},  "gravel" },
    // WOOD/séquoia trunk (6)
    { "wood",         true,  false, false, 2.0f,  {6,0},{6,0},{7,0},  "wood"   },
    // COBBLESTONE (7)
    { "cobblestone",  true,  false, false, 2.0f,  {0,1},{0,1},{0,1},  "stone"  },
    // COAL_ORE (8)
    { "coal_ore",     true,  false, false, 3.0f,  {1,1},{1,1},{1,1},  "stone"  },
    // WATER (9)
    { "water",        false, true,  true,  100.f, {2,1},{2,1},{2,1},  "liquid" },
    // LAVA (10)
    { "lava",         false, true,  true,  100.f, {3,1},{3,1},{3,1},  "liquid" },
    // MUD (11)
    { "mud",          true,  false, false, 0.6f,  {4,1},{4,1},{4,1},  "dirt"   },
    // MOSS (12)
    { "moss",         true,  false, false, 0.5f,  {5,1},{5,1},{5,1},  "grass"  },
    // SNOW (13)
    { "snow",         true,  false, false, 0.2f,  {6,1},{1,0},{6,1},  "snow"   },
    // PALM_WOOD (14)
    { "palm_wood",    true,  false, false, 2.0f,  {7,1},{7,1},{0,2},  "wood"   },
    // PALM_LEAVES (15)
    { "palm_leaves",  true,  true,  false, 0.2f,  {1,2},{1,2},{1,2},  "grass"  },
    // BAOBAB_WOOD (16)
    { "baobab_wood",  true,  false, false, 2.0f,  {2,2},{2,2},{3,2},  "wood"   },
    // BAOBAB_LEAVES (17)
    { "baobab_leaves",true,  true,  false, 0.2f,  {4,2},{4,2},{4,2},  "grass"  },
    // MAGMA (18)
    { "magma",        true,  false, false, 1.5f,  {5,2},{5,2},{5,2},  "stone"  },
    // CRYSTAL_STONE (19)
    { "crystal_stone",true,  false, false, 2.0f,  {6,2},{6,2},{6,2},  "stone"  },
    // SULFUR_STONE (20)
    { "sulfur_stone", true,  false, false, 1.5f,  {7,2},{7,2},{7,2},  "stone"  },
    // LEAVES/séquoia leaves (21)
    { "leaves",       true,  true,  false, 0.2f,  {7,3},{7,3},{7,3},  "grass"  },
}};

// Acces rapide
inline const BlockDef& blockDef(BlockID id) {
    return BLOCK_DEFS[(uint8_t)id];
}
inline bool blockSolid(BlockID id)       { return blockDef(id).solid; }
inline bool blockTransparent(BlockID id) { return blockDef(id).transparent; }
inline bool blockLiquid(BlockID id)      { return blockDef(id).liquid; }
inline bool blockAir(BlockID id)         { return id == BlockID::AIR; }

// ── Fichiers de textures pour l'atlas (8×8 = 64 slots) ──────────────────────
// index = row * ATLAS_COLS + col
inline constexpr std::array<const char*, ATLAS_COLS * ATLAS_ROWS> ATLAS_TEXTURE_FILES = {
// row 0
"default_grass_top.png",               // (0,0) grass top
"default_dirt.png",                    // (1,0) dirt
"default_grass_side.png",              // (2,0) grass side
"default_stone.png",                   // (3,0) stone
"default_mineral_sand.png",            // (4,0) sand
"default_gravel.png",                  // (5,0) gravel
"default_sequoia_tree_top.png",        // (6,0) wood top
"default_sequoia_tree_side.png",       // (7,0) wood side
// row 1
"default_mineral_cobblestone.png",     // (0,1) cobble
"default_mineral_coal.png",            // (1,1) coal ore
"default_mineral_water_source_animated.png", // (2,1) water
"default_lava_source_animated.png",    // (3,1) lava
"default_mud.png",                     // (4,1) mud
"default_moss_block.png",              // (5,1) moss
"default_snowcobble.png",              // (6,1) snow
"default_palm_tree_top.png",           // (7,1) palm top
// row 2
"default_palm_tree_side.png",          // (0,2) palm side
"default_palm_tree_leaves.png",        // (1,2) palm leaves
"default_baobab_tree_top.png",         // (2,2) baobab top
"default_baobab_tree_side.png",        // (3,2) baobab side
"default_baobab_tree_leaves.png",      // (4,2) baobab leaves
"default_magmacobble.png",             // (5,2) magma
"default_crystal_stone.png",           // (6,2) crystal stone
"default_sulfur_stone.png",            // (7,2) sulfur
// row 3
"default_mineral_stone_brick.png",     // (0,3)
"default_mineral_sandstone.png",       // (1,3)
"default_coral_sand.png",              // (2,3)
"default_coral_bones_block.png",       // (3,3)
"default_mese_wood.png",               // (4,3)
"default_mese_tree_top.png",           // (5,3)
"default_stone.png",                   // (6,3) placeholder
"default_sequoia_tree_leaves.png",     // (7,3) séquoia leaves
// rows 4-7 (réservés – utilise stone comme placeholder)
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
"default_stone.png","default_stone.png","default_stone.png","default_stone.png",
};

// Nombre total de couches texture
static constexpr int NUM_BLOCK_TEXTURES = ATLAS_COLS * ATLAS_ROWS;
// Alias pour compatibilité
static constexpr auto& kTexturePaths = ATLAS_TEXTURE_FILES;

// UV atlas normalisées (approche alternative sans texture array)
inline void atlasUV(AtlasSlot slot, float& u0, float& v0, float& u1, float& v1) {
    constexpr float W = 1.f / ATLAS_COLS;
    constexpr float H = 1.f / ATLAS_ROWS;
    u0 = slot.col * W;
    v0 = slot.row * H;
    u1 = u0 + W;
    v1 = v0 + H;
}

// ── Noms des blocs (pour HUD et tooltips) ───────────────────────────────────
inline const char* blockName(BlockID b) {
    switch (b) {
    case BlockID::AIR:           return "Air";
    case BlockID::GRASS:         return "Herbe";
    case BlockID::DIRT:          return "Terre";
    case BlockID::STONE:         return "Pierre";
    case BlockID::SAND:          return "Sable";
    case BlockID::GRAVEL:        return "Gravier";
    case BlockID::WOOD:          return "Sequoia";
    case BlockID::COBBLESTONE:   return "Pave";
    case BlockID::COAL_ORE:      return "Charbon";
    case BlockID::WATER:         return "Eau";
    case BlockID::MUD:           return "Boue";
    case BlockID::SNOW:          return "Neige";
    case BlockID::PALM_WOOD:     return "Palmier";
    case BlockID::PALM_LEAVES:   return "Feuilles Palmier";
    case BlockID::BAOBAB_WOOD:   return "Baobab";
    case BlockID::BAOBAB_LEAVES: return "Feuilles Baobab";
    case BlockID::CRYSTAL_STONE: return "Cristal";
    case BlockID::SULFUR_STONE:  return "Soufre";
    case BlockID::LEAVES:        return "Feuilles";
    default:                     return "Inconnu";
    }
}

// ── Inventaire par défaut (10 colonnes × 8 lignes = 80 emplacements) ────────
inline constexpr int INV_COLS = 10;
inline constexpr int INV_ROWS = 8;
inline constexpr int INV_SIZE = INV_COLS * INV_ROWS;

inline std::array<BlockID, INV_SIZE> defaultInventory() {
    std::array<BlockID, INV_SIZE> inv{};
    inv.fill(BlockID::AIR);
    // Hotbar (ligne 0)
    inv[0]  = BlockID::GRASS;
    inv[1]  = BlockID::DIRT;
    inv[2]  = BlockID::STONE;
    inv[3]  = BlockID::SAND;
    inv[4]  = BlockID::GRAVEL;
    inv[5]  = BlockID::WOOD;
    inv[6]  = BlockID::LEAVES;
    inv[7]  = BlockID::COBBLESTONE;
    inv[8]  = BlockID::COAL_ORE;
    inv[9]  = BlockID::SNOW;
    // Ligne 1 – blocs naturels
    inv[10] = BlockID::WATER;
    inv[11] = BlockID::MUD;
    inv[12] = BlockID::PALM_WOOD;
    inv[13] = BlockID::PALM_LEAVES;
    inv[14] = BlockID::BAOBAB_WOOD;
    inv[15] = BlockID::BAOBAB_LEAVES;
    inv[16] = BlockID::CRYSTAL_STONE;
    inv[17] = BlockID::SULFUR_STONE;
    return inv;
}
