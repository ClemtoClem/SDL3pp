/**
 * @file 03_tile_editor.cpp
 * @brief SDL3pp Tile & Map Editor
 *
 * Features:
 *  - Smart tilesets (auto-tile / neighbour-aware placement)
 *  - Multi-layer tile editing (Pencil, Brush, Fill, Erase, Select tools)
 *  - Object layers  (Rect, Ellipse, Polygon, Tile objects)
 *  - Pan + zoom map canvas
 *  - Undo / Redo (Ctrl+Z / Ctrl+Y)
 *  - XML export / import (SDL3pp DataScripts)
 *  - File dialogs (SDL ShowOpenFileDialog / ShowSaveFileDialog)
 *  - Orthogonal / Isometric / Hexagonal map types
 *  - Keyboard shortcuts
 */

#define SDL3PP_MAIN_USE_CALLBACKS 1
#include <SDL3pp/SDL3pp.h>
#include <SDL3pp/SDL3pp_main.h>
#include <SDL3pp/SDL3pp_dataScripts.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <functional>
#include <future>
#include <memory>
#include <queue>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

#ifdef SDL3PP_TILE_EDITOR_LUA
extern "C" {
	#include <lua.h>
	#include <lauxlib.h>
	#include <lualib.h>
}
#endif

#define TILE_EDITOR_VERSION "2.0.0"

// ─────────────────────────────────────────────────────────────────────────────
// Resource / pool keys
// ─────────────────────────────────────────────────────────────────────────────

namespace pool_key {
	constexpr const char* UI    = "ui";
	constexpr const char* TILES = "tiles";
}
namespace res_key {
	constexpr const char* FONT  = "font";
	constexpr const char* CLICK = "click";
	constexpr const char* FAIL  = "fail";
}
namespace icon_key {
	constexpr const char* NEW        = "icon_new";
	constexpr const char* OPEN       = "icon_open";
	constexpr const char* SAVE       = "icon_save";
	constexpr const char* SAVE_AS    = "icon_save_as";
	constexpr const char* IMPORT     = "icon_import";
	constexpr const char* LAYER_ADD  = "icon_layer_add";
	constexpr const char* LAYER_DEL  = "icon_layer_remove";
	constexpr const char* PENCIL     = "icon_pencil";
	constexpr const char* BRUSH      = "icon_brush";
	constexpr const char* FILL       = "icon_fill";
	constexpr const char* ERASE      = "icon_erase";
	constexpr const char* SELECT     = "icon_select";
	constexpr const char* UNDO       = "icon_undo";
	constexpr const char* REDO       = "icon_redo";
	constexpr const char* GRID       = "icon_grid";
	constexpr const char* ZOOM_IN    = "icon_zoom_in";
	constexpr const char* ZOOM_OUT   = "icon_zoom_out";
	constexpr const char* VISIBILITY = "icon_visibility";
	constexpr const char* UP         = "icon_up_arrow";
	constexpr const char* DOWN       = "icon_down_arrow";
	constexpr const char* LEFT       = "icon_left_arrow";
	constexpr const char* RIGHT      = "icon_right_arrow";
	constexpr const char* LOCK       = "icon_collision";
	constexpr const char* STAMP      = "icon_stamp";
	// Object tool icons (re-use existing icons)
	constexpr const char* OBJ_SELECT = "icon_select"; // alias
	constexpr const char* OBJ_RECT   = "icon_stamp";  // alias for box object
	constexpr const char* OBJ_POINT  = "icon_grid";   // alias
	constexpr const char* OBJ_POLY   = "icon_pencil"; // alias for polygon
}

// ─────────────────────────────────────────────────────────────────────────────
// Palette
// ─────────────────────────────────────────────────────────────────────────────

namespace pal {
	// Modern dark-blue editor theme (subtle gradient between surface levels)
	constexpr SDL::Color BG        = { 14,  16,  24, 255}; // canvas / app background
	constexpr SDL::Color HEADER    = { 22,  26,  38, 255}; // top bar / status bar
	constexpr SDL::Color PANEL     = { 19,  22,  32, 255}; // side panels
	constexpr SDL::Color SURFACE   = { 28,  32,  46, 255}; // inputs / list items
	constexpr SDL::Color SURFACE2  = { 36,  41,  58, 255}; // hover layer
	constexpr SDL::Color ACCENT    = { 88, 156, 245, 255}; // primary brand
	constexpr SDL::Color ACCENT2   = {124, 178, 255, 255}; // accent hover
	constexpr SDL::Color ACCENT3   = { 56, 124, 220, 255}; // accent pressed
	constexpr SDL::Color NEUTRAL   = { 36,  40,  54, 255};
	constexpr SDL::Color NEUTRAL2  = { 52,  58,  78, 255};
	constexpr SDL::Color BORDER    = { 50,  56,  78, 255};
	constexpr SDL::Color BORDER2   = { 70,  78, 104, 255};
	constexpr SDL::Color WHITE     = {228, 230, 238, 255};
	constexpr SDL::Color TEXT      = {215, 220, 232, 255};
	constexpr SDL::Color TEXT_DIM  = {148, 156, 178, 255};
	constexpr SDL::Color GREY      = {130, 138, 158, 255};
	constexpr SDL::Color GREEN     = { 72, 200, 130, 255};
	constexpr SDL::Color ORANGE    = {235, 158,  50, 255};
	constexpr SDL::Color RED       = {230,  85,  90, 255};
	constexpr SDL::Color YELLOW    = {255, 210,  80, 255};
	constexpr SDL::Color SELECTED  = {255, 210,  80, 255};
	constexpr SDL::Color GRID      = { 70,  78, 104, 130};
	constexpr SDL::Color OBJ_COL   = {110, 195, 130, 170};
	constexpr SDL::Color OBJ_SEL   = { 80, 200, 255, 200};
	constexpr SDL::Color OBJ_RECT  = {120, 180, 230, 200};
	constexpr SDL::Color OBJ_POLY  = {200, 130, 220, 200};
	constexpr SDL::Color OBJ_ELLIP = {255, 170, 100, 200};
	constexpr SDL::Color OBJ_POINT = {255, 210,  80, 220};
}

// =============================================================================
// Data model
// =============================================================================

using TileID = uint16_t;
static constexpr TileID EMPTY_TILE = 0;

// ── Custom properties ─────────────────────────────────────────────────────────
// Supported value types: int, float, bool, std::string
using PropertyValue = std::variant<int, float, bool, std::string>;
using PropertyMap   = std::unordered_map<std::string, PropertyValue>;

// ── Chunk-based infinite tile storage ─────────────────────────────────────────
static constexpr int CHUNK_SIZE = 16; ///< Tiles per chunk edge (power of two).

struct ChunkPos {
	int x = 0, y = 0;
	bool operator==(const ChunkPos& o) const noexcept { return x == o.x && y == o.y; }
};
struct ChunkHasher {
	std::size_t operator()(const ChunkPos& p) const noexcept {
		// Combine two 32-bit ints into one 64-bit hash (good for negative coords too)
		return std::hash<long long>()((long long)(unsigned)p.x | ((long long)(unsigned)p.y << 32));
	}
};
struct Chunk {
	std::array<TileID, CHUNK_SIZE * CHUNK_SIZE> tiles;
	bool dirty = true;
	Chunk() { tiles.fill(EMPTY_TILE); }
};
using ChunkMap = std::unordered_map<ChunkPos, Chunk, ChunkHasher>;

// Floor-division that handles negative numerators correctly (unlike C++ truncation)
static constexpr int FloorDiv(int a, int b) noexcept {
	return a / b - (a % b != 0 && (a ^ b) < 0);
}

// ── Tile metadata (per local-ID inside a TilesetDef) ─────────────────────────

struct AnimFrame {
	int localId     = 0;   ///< Local tile index within the tileset.
	int durationMs  = 100; ///< Frame duration in milliseconds.
};

/// Per-tile metadata stored in TilesetDef::tileData[localId].
struct TileMetadata {
	std::vector<AnimFrame> anim;      ///< Non-empty → tile is animated.
	uint32_t    wangId     = 0;       ///< Wang-tile edge/corner bitmask (for autotile).
	PropertyMap properties;           ///< Custom key→value properties.
};

// ── Enums ────────────────────────────────────────────────────────────────────
enum class LayerType  { Tile, Object };
enum class ObjectType { Rect, Ellipse, Point, Polygon, Tile, MapLink };
enum class MapOrient  {
	Orthogonal,
	Isometric,
	HexFlat,            ///< Hexagonal, flat-top (hex columns).
	HexPointy,          ///< Hexagonal, pointy-top (hex rows).
	Hexagonal = HexFlat ///< Back-compat alias (saves predating the flat/pointy split).
};

// ── Tileset ───────────────────────────────────────────────────────────────────
struct TilesetDef {
	std::string key;
	std::string name     = "Tileset";
	std::string path;
	int  tileW    = 16,  tileH    = 16;
	int  spacing  = 0,   margin   = 0;
	int  columns  = 8,   rows     = 8;
	int  tileCount= 64;
	int  imageW   = 0,   imageH   = 0;
	bool smart    = false;           ///< Simple 4-bit neighbour autotile.
	TileID firstGid = 1;             ///< Global tile ID of the first tile.

	/// Per-tile metadata (animations, Wang ID, properties).
	std::unordered_map<int, TileMetadata> tileData;

	const TileMetadata* MetaFor(int localId) const {
		auto it = tileData.find(localId);
		return (it != tileData.end()) ? &it->second : nullptr;
	}
};

// ── Object ────────────────────────────────────────────────────────────────────
struct ObjectDef {
	int         id       = 0;
	ObjectType  type     = ObjectType::Rect;
	std::string name;
	float x = 0, y = 0, w = 32, h = 32;
	float rotation = 0.f;
	TileID tileId  = 0;
	std::vector<SDL::FPoint> points;
	bool selected  = false;
	PropertyMap properties; ///< Custom key→value properties.
};

// ── Map layer ─────────────────────────────────────────────────────────────────
struct MapLayer {
	std::string  name    = "Layer";
	LayerType    type    = LayerType::Tile;
	bool         visible = true;
	bool         locked  = false;
	float        opacity = 1.0f;
	ChunkMap     chunks;              ///< Sparse chunk storage (infinite map).
	std::vector<ObjectDef> objects;
	PropertyMap  properties;          ///< Custom key→value properties.
};

// ── TileMap ───────────────────────────────────────────────────────────────────
struct TileMap {
	std::string name     = "Untitled";
	std::string filePath;
	int  tileW   = 32,  tileH   = 32;
	bool infinite = false;            ///< When false, tiles are bounded by width/height.
	int  width   = 20,  height  = 15; ///< Used only when !infinite.
	MapOrient orientation = MapOrient::Orthogonal;
	std::vector<TilesetDef> tilesets;
	std::vector<MapLayer>   layers;
	int  activeLayer = 0;
	bool dirty       = false;
	PropertyMap properties;

	void Init(int w = 20, int h = 15, int tw = 32, int th = 32) {
		width = w; height = h; tileW = tw; tileH = th;
		infinite = false;
		name = "Untitled"; filePath = "";
		tilesets.clear(); layers.clear(); properties.clear();
		MapLayer layer; layer.name = "Layer 1";
		layers.push_back(layer);
		activeLayer = 0; dirty = false;
	}

	// ── Chunk helpers ─────────────────────────────────────────────────────────

	static ChunkPos TileToChunk(int tx, int ty) noexcept {
		return {FloorDiv(tx, CHUNK_SIZE), FloorDiv(ty, CHUNK_SIZE)};
	}

	static int TileLocalIdx(int tx, int ty) noexcept {
		int lx = ((tx % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		int ly = ((ty % CHUNK_SIZE) + CHUNK_SIZE) % CHUNK_SIZE;
		return ly * CHUNK_SIZE + lx;
	}

	// ── Tile access ───────────────────────────────────────────────────────────

	TileID GetTile(int layer, int tx, int ty) const {
		if (layer < 0 || layer >= (int)layers.size()) return EMPTY_TILE;
		if (!infinite && (tx < 0 || ty < 0 || tx >= width || ty >= height)) return EMPTY_TILE;
		const auto& l = layers[layer];
		if (l.type != LayerType::Tile) return EMPTY_TILE;
		auto it = l.chunks.find(TileToChunk(tx, ty));
		if (it == l.chunks.end()) return EMPTY_TILE;
		return it->second.tiles[TileLocalIdx(tx, ty)];
	}

	bool SetTile(int layer, int tx, int ty, TileID id) {
		if (layer < 0 || layer >= (int)layers.size()) return false;
		if (!infinite && (tx < 0 || ty < 0 || tx >= width || ty >= height)) return false;
		auto& l = layers[layer];
		if (l.type != LayerType::Tile || l.locked) return false;
		auto& chunk = l.chunks[TileToChunk(tx, ty)]; // creates on demand
		chunk.tiles[TileLocalIdx(tx, ty)] = id;
		chunk.dirty = true;
		dirty = true;
		return true;
	}

	// ── Tileset helpers ───────────────────────────────────────────────────────

	const TilesetDef* FindTileset(TileID tid) const {
		if (tid == EMPTY_TILE) return nullptr;
		const TilesetDef* best = nullptr;
		for (const auto& ts : tilesets)
			if (ts.firstGid <= tid && (!best || ts.firstGid > best->firstGid))
				best = &ts;
		return best;
	}

	SDL::FRect TileSrcRect(const TilesetDef& ts, TileID tid) const {
		int local = (int)(tid - ts.firstGid);
		int col   = (ts.columns > 0) ? local % ts.columns : 0;
		int row   = (ts.columns > 0) ? local / ts.columns : 0;
		return {
			float(ts.margin + col * (ts.tileW + ts.spacing)),
			float(ts.margin + row * (ts.tileH + ts.spacing)),
			float(ts.tileW), float(ts.tileH)
		};
	}

	// ── Orientation-aware coordinate transforms ───────────────────────────────
	//
	// Returns the world-space pixel position of the top-left corner of tile
	// (tx, ty) under the current orientation. Tile pixel size is (tileW, tileH).
	SDL::FPoint TileToWorld(int tx, int ty) const noexcept {
		const float tw = float(tileW), th = float(tileH);
		switch (orientation) {
			case MapOrient::Orthogonal: return {tx * tw, ty * th};
			case MapOrient::Isometric:
				return {(tx - ty) * tw * 0.5f, (tx + ty) * th * 0.5f};
			case MapOrient::HexFlat: {
				// Flat-top: column offset, odd cols shifted down by half.
				float x = tx * tw * 0.75f;
				float y = ty * th + ((tx & 1) ? th * 0.5f : 0.f);
				return {x, y};
			}
			case MapOrient::HexPointy: {
				// Pointy-top: row offset, odd rows shifted right by half.
				float x = tx * tw + ((ty & 1) ? tw * 0.5f : 0.f);
				float y = ty * th * 0.75f;
				return {x, y};
			}
		}
		return {tx * tw, ty * th};
	}

	// Inverse of TileToWorld: world-space pixel coords → tile coords.
	// Picking is approximate for hex/iso (uses bounding-rect mapping; good
	// enough for editor purposes — game-side picking can refine if needed).
	void WorldToTile(float wx, float wy, int& tx, int& ty) const noexcept {
		const float tw = float(tileW), th = float(tileH);
		switch (orientation) {
			case MapOrient::Orthogonal:
				tx = (int)std::floor(wx / tw);
				ty = (int)std::floor(wy / th);
				return;
			case MapOrient::Isometric: {
				float fx = wx / (tw * 0.5f);
				float fy = wy / (th * 0.5f);
				tx = (int)std::floor((fx + fy) * 0.5f);
				ty = (int)std::floor((fy - fx) * 0.5f);
				return;
			}
			case MapOrient::HexFlat: {
				tx = (int)std::floor(wx / (tw * 0.75f));
				float yShift = (tx & 1) ? th * 0.5f : 0.f;
				ty = (int)std::floor((wy - yShift) / th);
				return;
			}
			case MapOrient::HexPointy: {
				ty = (int)std::floor(wy / (th * 0.75f));
				float xShift = (ty & 1) ? tw * 0.5f : 0.f;
				tx = (int)std::floor((wx - xShift) / tw);
				return;
			}
		}
		tx = (int)std::floor(wx / tw);
		ty = (int)std::floor(wy / th);
	}

	// ── Auto-tile ─────────────────────────────────────────────────────────────

	/// Bit mask of 4-way neighbours with the same tile ID: N=1 E=2 S=4 W=8.
	uint8_t NeighbourMask(int layer, int x, int y) const {
		TileID t = GetTile(layer, x, y);
		uint8_t m = 0;
		if (GetTile(layer, x,   y-1) == t) m |= 1;
		if (GetTile(layer, x+1, y  ) == t) m |= 2;
		if (GetTile(layer, x,   y+1) == t) m |= 4;
		if (GetTile(layer, x-1, y  ) == t) m |= 8;
		return m;
	}

	// ── Map extent (for bounded maps / UI display) ────────────────────────────

	/// Bounding box of all occupied tile positions across all tile layers,
	/// returned as {minX, minY, maxX+1, maxY+1} in tile coordinates.
	/// Returns {0,0,width,height} for bounded maps.
	struct TileBounds { int x0=0,y0=0,x1=0,y1=0; bool empty=true; };
	TileBounds OccupiedBounds() const {
		if (!infinite) return {0, 0, width, height, false};
		TileBounds b;
		for (const auto& layer : layers) {
			if (layer.type != LayerType::Tile) continue;
			for (const auto& [cp, chunk] : layer.chunks) {
				for (int ly = 0; ly < CHUNK_SIZE; ++ly)
				for (int lx = 0; lx < CHUNK_SIZE; ++lx) {
					if (chunk.tiles[ly * CHUNK_SIZE + lx] == EMPTY_TILE) continue;
					int tx = cp.x * CHUNK_SIZE + lx;
					int ty = cp.y * CHUNK_SIZE + ly;
					if (b.empty) { b.x0=tx; b.y0=ty; b.x1=tx+1; b.y1=ty+1; b.empty=false; }
					else {
						b.x0 = SDL::Min(b.x0, tx);  b.y0 = SDL::Min(b.y0, ty);
						b.x1 = SDL::Max(b.x1, tx+1); b.y1 = SDL::Max(b.y1, ty+1);
					}
				}
			}
		}
		return b;
	}
};

// =============================================================================
// Undo / Redo
// =============================================================================

struct TileChange { int layer, x, y; TileID oldId, newId; };
struct Command    { std::vector<TileChange> changes; };

struct UndoRedo {
	static constexpr int MAX = 64;
	std::vector<Command> undo, redo;

	void Push(Command cmd) {
		if (cmd.changes.empty()) return;
		undo.push_back(std::move(cmd));
		if ((int)undo.size() > MAX) undo.erase(undo.begin());
		redo.clear();
	}
	bool CanUndo() const { return !undo.empty(); }
	bool CanRedo() const { return !redo.empty(); }
	Command PopUndo() { auto c = std::move(undo.back()); undo.pop_back(); return c; }
	Command PopRedo() { auto c = std::move(redo.back()); redo.pop_back(); return c; }
	void PushRedo(Command c) { redo.push_back(std::move(c)); }
};

// =============================================================================
// Project: shared state and cross-workspace event bus
//
// A Project bundles every resource that the editor knows how to author —
// the current map (with its tilesets, layers, objects), the Lua scripts,
// the cinematic timelines, and the node-graphs. Workspaces are views over
// this Project; mutations are broadcast via the dirty-flag bus so a tab
// that's currently hidden picks up changes when it becomes active.
// =============================================================================

enum class WorkspaceKind : uint32_t {
	None      = 0,
	Map       = 1 << 0,
	Scripts   = 1 << 1,
	Cinematic = 1 << 2,
	NodeGraph = 1 << 3,
	Test      = 1 << 4,
	All       = 0xFFFFFFFFu,
};
inline WorkspaceKind operator|(WorkspaceKind a, WorkspaceKind b) noexcept {
	return (WorkspaceKind)((uint32_t)a | (uint32_t)b);
}
inline WorkspaceKind operator&(WorkspaceKind a, WorkspaceKind b) noexcept {
	return (WorkspaceKind)((uint32_t)a & (uint32_t)b);
}
inline bool HasFlag(WorkspaceKind a, WorkspaceKind b) noexcept {
	return ((uint32_t)a & (uint32_t)b) != 0;
}

// ── Lua script document ──────────────────────────────────────────────────
struct ScriptDoc {
	std::string name = "main.lua";
	std::string code =
		"-- Lua script. Available globals:\n"
		"--   engine.log(msg), engine.tile(layer, x, y), engine.set_tile(layer, x, y, id)\n"
		"--   engine.player_pos(), engine.set_player(x, y), engine.dialog(text)\n"
		"--   engine.size(), engine.tick(dt) (called once per test frame if defined)\n"
		"engine.log('Hello from Lua!')\n";
};

// ── Cinematic timeline ──────────────────────────────────────────────────
enum class CineTrackKind : uint8_t { Image, Music, Sfx, Dialog };
struct CineClip {
	float start = 0.f;      ///< seconds
	float length = 2.f;     ///< seconds
	std::string asset;      ///< file path or dialogue text
};
struct CineTrack {
	std::string name;
	CineTrackKind kind = CineTrackKind::Image;
	std::vector<CineClip> clips;
};
struct CineDoc {
	std::string name = "intro";
	float duration = 10.f;            ///< seconds
	std::vector<CineTrack> tracks;    ///< default-empty list of tracks
};

// ── Node-graph ──────────────────────────────────────────────────────────
enum class NodeKind : uint8_t {
	Event,     ///< Trigger / entry point (no inputs).
	Script,    ///< Runs a Lua script block.
	Dialog,    ///< Shows a dialogue text (Pokemon-style).
	Cinematic, ///< Plays a CineDoc.
	Wait,      ///< Delays a configurable number of seconds.
	Branch,    ///< Conditional fork (Lua expression → 2 outputs).
};
struct NodePort {
	SDL::FPoint local = {0.f, 0.f}; ///< Offset from node origin (px).
	std::string label;
};
struct NodeDef {
	int id = 0;
	NodeKind kind = NodeKind::Script;
	SDL::FPoint pos = {0.f, 0.f};   ///< Top-left in node-graph world space (px).
	SDL::FPoint size = {180.f, 80.f};
	std::string title;
	std::string body;                ///< Lua snippet, dialogue text, cinematic name, etc.
	std::vector<NodePort> inputs;
	std::vector<NodePort> outputs;
};
struct NodeWire {
	int srcNode = 0, srcPort = 0;
	int dstNode = 0, dstPort = 0;
};
struct NodeGraphDoc {
	std::string name = "main";
	std::vector<NodeDef> nodes;
	std::vector<NodeWire> wires;
	int nextNodeId = 1;
};

// ── Project: the shared model ───────────────────────────────────────────
struct Project {
	// Cross-workspace dirty bus. Setting a bit asks each workspace to refresh
	// itself the next time it becomes active. A workspace clears its own bit
	// after consuming it. Mutated from any workspace; read from Iterate().
	WorkspaceKind dirtyFor = WorkspaceKind::None;
	void Touch(WorkspaceKind w) noexcept { dirtyFor = dirtyFor | w; }

	// Scripts: a small map of named documents (open file → string).
	std::vector<ScriptDoc>    scripts;
	int                       activeScript = 0;

	// Cinematics: one document per cinematic; the user adds more as needed.
	std::vector<CineDoc>      cinematics;
	int                       activeCinematic = 0;

	// Node-graphs: each one is its own runnable mini-flow.
	std::vector<NodeGraphDoc> graphs;
	int                       activeGraph = 0;

	void Init() {
		scripts    = { ScriptDoc{} };
		cinematics = { CineDoc{} };
		graphs     = { NodeGraphDoc{} };
	}
};

// =============================================================================
// Editor state
// =============================================================================

enum class ToolType {
	Pencil, Brush, Fill, Erase, Select,
	// Object tools (active on Object layers)
	ObjSelect, ObjRect, ObjEllipse, ObjPoint, ObjPolygon
};

struct EditorState {
	ToolType tool     = ToolType::Pencil;
	bool     showGrid = true;

	// Viewport
	float viewX = 0.f, viewY = 0.f;
	float zoom  = 1.0f;
	static constexpr float ZOOM_MIN = 0.1f, ZOOM_MAX = 8.0f;

	// Canvas rects (updated each frame from render callbacks)
	SDL::FRect mapRect     = {};
	SDL::FRect tilesetRect = {};

	// Mouse state for map canvas
	bool      mapLDown  = false;
	bool      mapRDown  = false;
	bool      panning   = false;
	SDL::FPoint panStart     = {};
	SDL::FPoint panViewStart = {};
	SDL::FPoint lastTile     = {-1, -1};
	Command     stroke;

	// Selection (Select tool)
	bool hasMapSel  = false;
	int  selX = 0, selY = 0, selW = 0, selH = 0;
	bool selDrag    = false;
	SDL::FPoint selDragStart = {};

	// Tileset panel
	int    activeTileset = 0;
	int    selTileX = 0,  selTileY = 0;
	int    selTileW = 1,  selTileH = 1;
	bool   tsDragging    = false;
	SDL::FPoint tsDragStart = {};
	float  tsScrollX     = 0.f;
	float  tsScrollY     = 0.f;
	float  tsTileZoom    = 0.f;  // 0 = auto-fit; positive = explicit zoom scale
	float  tsScale       = 1.0f;

	// Object layer drag (for shape creation)
	bool      objDrag  = false;
	SDL::FPoint objStart = {};
	int       nextObjId  = 1;

	// Object selection / move state
	int  selectedObjLayer = -1;  ///< Layer index of selected object (-1 = none).
	int  selectedObjId    = -1;  ///< ObjectDef::id of selected object.
	bool objMoving        = false;
	SDL::FPoint objMoveStart  = {};   ///< World coords of mouse at move start.
	SDL::FPoint objMoveOrigin = {};   ///< Original obj.x / obj.y at move start.

	// Polygon-in-progress
	std::vector<SDL::FPoint> polyPoints; ///< World-space points (relative to first point).
	SDL::FPoint               polyOrigin = {};

	// Animation playback timer
	float animTime = 0.f;

	// Brush tool size (1, 3, 5, 7, 9)
	int brushSize = 3;

	// Pending file-dialog results (populated by async SDL dialog callbacks)
	std::string pendingOpenPath;
	std::string pendingSavePath;
	std::string pendingTilesetPath;
	bool        pendingNew = false;
};

// =============================================================================
// XML Save / Load (SDL3pp DataScripts)
// =============================================================================

// ── Save helpers ──────────────────────────────────────────────────────────────

/// Serialise a PropertyMap to a <properties> ObjectDataNode (TMX-style).
static std::shared_ptr<SDL::ObjectDataNode> SaveProperties(const PropertyMap& props) {
	if (props.empty()) return nullptr;
	auto pn = SDL::ObjectDataNode::Make();
	for (const auto& [k, v] : props) {
		auto en = SDL::ObjectDataNode::Make();
		auto ea = SDL::ObjectDataNode::Make();
		ea->set("name", SDL::StringDataNode::Make(k));
		std::visit([&](auto&& val) {
			using T = std::decay_t<decltype(val)>;
			if constexpr (std::is_same_v<T, int>) {
				ea->set("type",  SDL::StringDataNode::Make("int"));
				ea->set("value", SDL::S32DataNode::Make(val));
			} else if constexpr (std::is_same_v<T, float>) {
				ea->set("type",  SDL::StringDataNode::Make("float"));
				ea->set("value", SDL::F32DataNode::Make(val));
			} else if constexpr (std::is_same_v<T, bool>) {
				ea->set("type",  SDL::StringDataNode::Make("bool"));
				ea->set("value", SDL::BoolDataNode::Make(val));
			} else {
				ea->set("type",  SDL::StringDataNode::Make("string"));
				ea->set("value", SDL::StringDataNode::Make(val));
			}
		}, v);
		en->set("@attributes", ea);
		pn->set("property", en);
	}
	return pn;
}

/// Encode a flat tile array (row-major, bounded) as CSV.
static std::string TilesToCsv(const ChunkMap& chunks, int x0, int y0, int w, int h) {
	std::string csv;
	csv.reserve(w * h * 3);
	for (int ty = y0; ty < y0 + h; ++ty) {
		for (int tx = x0; tx < x0 + w; ++tx) {
			if (tx != x0 || ty != y0) csv += ',';
			auto it = chunks.find(TileMap::TileToChunk(tx, ty));
			TileID id = (it != chunks.end())
				? it->second.tiles[TileMap::TileLocalIdx(tx, ty)]
				: EMPTY_TILE;
			csv += std::to_string((int)id);
		}
	}
	return csv;
}

static void SaveMap(const TileMap& map, const std::string& path) {
	auto doc   = std::make_shared<SDL::XMLDataDocument>();
	auto root  = SDL::ObjectDataNode::Make();
	auto mNode = SDL::ObjectDataNode::Make();

	// <map @attributes>
	{
		auto a = SDL::ObjectDataNode::Make();
		a->set("version",     SDL::StringDataNode::Make("1.10"));
		a->set("tiledversion",SDL::StringDataNode::Make("1.10.0"));
		a->set("name",        SDL::StringDataNode::Make(map.name));
		a->set("width",       SDL::S32DataNode::Make(map.width));
		a->set("height",      SDL::S32DataNode::Make(map.height));
		a->set("tilewidth",   SDL::S32DataNode::Make(map.tileW));
		a->set("tileheight",  SDL::S32DataNode::Make(map.tileH));
		a->set("infinite",    SDL::BoolDataNode::Make(map.infinite));
		const char* ori = "orthogonal";
		if (map.orientation == MapOrient::Isometric) ori = "isometric";
		if (map.orientation == MapOrient::HexFlat)   ori = "hexagonal";       // flat-top
		if (map.orientation == MapOrient::HexPointy) ori = "hexagonal-pointy";
		a->set("orientation", SDL::StringDataNode::Make(ori));
		mNode->set("@attributes", a);
	}
	if (auto pn = SaveProperties(map.properties))
		mNode->set("properties", pn);

	// <tileset> entries
	for (const auto& ts : map.tilesets) {
		auto tn = SDL::ObjectDataNode::Make();
		auto a  = SDL::ObjectDataNode::Make();
		a->set("name",       SDL::StringDataNode::Make(ts.name));
		a->set("source",     SDL::StringDataNode::Make(ts.path));
		a->set("firstgid",   SDL::S32DataNode::Make((int)ts.firstGid));
		a->set("tilewidth",  SDL::S32DataNode::Make(ts.tileW));
		a->set("tileheight", SDL::S32DataNode::Make(ts.tileH));
		a->set("spacing",    SDL::S32DataNode::Make(ts.spacing));
		a->set("margin",     SDL::S32DataNode::Make(ts.margin));
		a->set("columns",    SDL::S32DataNode::Make(ts.columns));
		a->set("tilecount",  SDL::S32DataNode::Make(ts.tileCount));
		a->set("smart",      SDL::BoolDataNode::Make(ts.smart));
		tn->set("@attributes", a);
		// Per-tile metadata: <tile id="…"> <animation> / <properties>
		for (const auto& [lid, meta] : ts.tileData) {
			auto tileNode = SDL::ObjectDataNode::Make();
			auto ta = SDL::ObjectDataNode::Make();
			ta->set("id", SDL::S32DataNode::Make(lid));
			tileNode->set("@attributes", ta);
			if (!meta.anim.empty()) {
				auto animNode = SDL::ObjectDataNode::Make();
				for (const auto& fr : meta.anim) {
					auto fn = SDL::ObjectDataNode::Make();
					auto fa = SDL::ObjectDataNode::Make();
					fa->set("tileid",   SDL::S32DataNode::Make(fr.localId));
					fa->set("duration", SDL::S32DataNode::Make(fr.durationMs));
					fn->set("@attributes", fa);
					animNode->set("frame", fn);
				}
				tileNode->set("animation", animNode);
			}
			if (!meta.properties.empty())
				tileNode->set("properties", SaveProperties(meta.properties));
			if (meta.wangId)
				tileNode->set("wangid", SDL::S32DataNode::Make((int)meta.wangId));
			tn->set("tile", tileNode);
		}
		mNode->set("tileset", tn);
	}

	// <layer> / <objectgroup> entries
	for (const auto& layer : map.layers) {
		auto ln = SDL::ObjectDataNode::Make();
		auto a  = SDL::ObjectDataNode::Make();
		a->set("name",    SDL::StringDataNode::Make(layer.name));
		a->set("visible", SDL::BoolDataNode::Make(layer.visible));
		a->set("locked",  SDL::BoolDataNode::Make(layer.locked));
		a->set("opacity", SDL::F32DataNode::Make(layer.opacity));
		ln->set("@attributes", a);
		if (auto pn = SaveProperties(layer.properties))
			ln->set("properties", pn);

		if (layer.type == LayerType::Tile) {
			if (map.infinite) {
				// TMX infinite format: one <chunk> per occupied chunk
				auto chunksNode = SDL::ObjectDataNode::Make();
				for (const auto& [cp, chunk] : layer.chunks) {
					// Skip fully-empty chunks
					bool hasData = false;
					for (auto t : chunk.tiles) if (t != EMPTY_TILE) { hasData = true; break; }
					if (!hasData) continue;
					auto cn = SDL::ObjectDataNode::Make();
					auto ca = SDL::ObjectDataNode::Make();
					int wx = cp.x * CHUNK_SIZE, wy = cp.y * CHUNK_SIZE;
					ca->set("x",      SDL::S32DataNode::Make(wx));
					ca->set("y",      SDL::S32DataNode::Make(wy));
					ca->set("width",  SDL::S32DataNode::Make(CHUNK_SIZE));
					ca->set("height", SDL::S32DataNode::Make(CHUNK_SIZE));
					std::string csv = TilesToCsv(layer.chunks, wx, wy, CHUNK_SIZE, CHUNK_SIZE);
					ca->set("data", SDL::StringDataNode::Make(csv));
					cn->set("@attributes", ca);
					chunksNode->set("chunk", cn);
				}
				ln->set("chunks", chunksNode);
			} else {
				// Bounded format: flat CSV (TMX compatible)
				ln->set("data", SDL::StringDataNode::Make(
					TilesToCsv(layer.chunks, 0, 0, map.width, map.height)));
			}
			mNode->set("layer", ln);
		} else {
			for (const auto& obj : layer.objects) {
				auto on = SDL::ObjectDataNode::Make();
				auto oa = SDL::ObjectDataNode::Make();
				oa->set("id",       SDL::S32DataNode::Make(obj.id));
				oa->set("name",     SDL::StringDataNode::Make(obj.name));
				oa->set("x",        SDL::F32DataNode::Make(obj.x));
				oa->set("y",        SDL::F32DataNode::Make(obj.y));
				oa->set("width",    SDL::F32DataNode::Make(obj.w));
				oa->set("height",   SDL::F32DataNode::Make(obj.h));
				oa->set("rotation", SDL::F32DataNode::Make(obj.rotation));
				const char* tp = "rect";
				if (obj.type == ObjectType::Ellipse) tp = "ellipse";
				if (obj.type == ObjectType::Point)   tp = "point";
				if (obj.type == ObjectType::Polygon) tp = "polygon";
				if (obj.type == ObjectType::Tile)    tp = "tile";
				oa->set("type", SDL::StringDataNode::Make(tp));
				if (obj.type == ObjectType::Tile)
					oa->set("tileid", SDL::S32DataNode::Make((int)obj.tileId));
				on->set("@attributes", oa);
				if (auto pn = SaveProperties(obj.properties))
					on->set("properties", pn);
				ln->set("object", on);
			}
			mNode->set("objectgroup", ln);
		}
	}

	root->set("map", mNode);
	doc->setRoot(root);
	try {
		auto io = SDL::IOStream::FromFile(path.c_str(), "w");
		std::string data = doc->encode();
		io.Write(data);
	} catch (const std::exception& e) {
		SDL::LogError(SDL::LOG_CATEGORY_APPLICATION, "Save failed: %s", e.what());
	}
}

// ── XML helpers ──────────────────────────────────────────────────────────────

static std::string XmlStr(const SDL::ObjectDataNode& n, const char* k,
						   const std::string& def = {}) {
	auto nd = n.get(k);
	if (!nd) return def;
	if (auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(nd)) return s->getValue();
	return def;
}
static int XmlInt(const SDL::ObjectDataNode& n, const char* k, int def = 0) {
	auto nd = n.get(k);
	if (!nd) return def;
	if (auto i = std::dynamic_pointer_cast<SDL::S32DataNode>(nd)) return (int)i->getValue();
	if (auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(nd))
		try { return std::stoi(s->getValue()); } catch (...) {}
	return def;
}
static float XmlFloat(const SDL::ObjectDataNode& n, const char* k, float def = 0.f) {
	auto nd = n.get(k);
	if (!nd) return def;
	if (auto f = std::dynamic_pointer_cast<SDL::F32DataNode>(nd)) return f->getValue();
	if (auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(nd))
		try { return std::stof(s->getValue()); } catch (...) {}
	return def;
}
static bool XmlBool(const SDL::ObjectDataNode& n, const char* k, bool def = false) {
	auto nd = n.get(k);
	if (!nd) return def;
	if (auto b = std::dynamic_pointer_cast<SDL::BoolDataNode>(nd)) return b->getValue();
	if (auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(nd)) return s->getValue() == "true";
	return def;
}
// Call fn(ObjectDataNode&) for each node under key (or all items if it's an array)
static void XmlEach(const SDL::ObjectDataNode& parent, const char* key,
					std::function<void(const SDL::ObjectDataNode&)> fn) {
	auto nd = parent.get(key);
	if (!nd) return;
	if (nd->getType() == SDL::DataNodeType::ARRAY) {
		auto arr = std::dynamic_pointer_cast<SDL::ArrayDataNode>(nd);
		for (size_t i = 0; i < arr->getSize(); ++i)
			if (auto obj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(arr->get(i)))
				fn(*obj);
	} else if (auto obj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(nd)) {
		fn(*obj);
	}
}

// Parse a <properties> element back into a PropertyMap.
static PropertyMap LoadProperties(const SDL::ObjectDataNode& parent) {
	PropertyMap props;
	auto propsNd = parent.get("properties");
	if (!propsNd) return props;
	auto propsObj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(propsNd);
	if (!propsObj) return props;
	XmlEach(*propsObj, "property", [&](const SDL::ObjectDataNode& en) {
		auto ea = std::dynamic_pointer_cast<SDL::ObjectDataNode>(en.get("@attributes"));
		if (!ea) return;
		std::string pname = XmlStr(*ea, "name");
		std::string ptype = XmlStr(*ea, "type", "string");
		if (pname.empty()) return;
		if (ptype == "int")
			props[pname] = XmlInt(*ea, "value");
		else if (ptype == "float")
			props[pname] = XmlFloat(*ea, "value");
		else if (ptype == "bool") {
			auto vnd = ea->get("value");
			if (auto b = std::dynamic_pointer_cast<SDL::BoolDataNode>(vnd))
				props[pname] = b->getValue();
			else
				props[pname] = XmlStr(*ea, "value") == "true";
		} else
			props[pname] = XmlStr(*ea, "value");
	});
	return props;
}

static bool LoadMap(TileMap& map, const std::string& path) {
	auto doc = std::make_shared<SDL::XMLDataDocument>();
	try {
		auto io  = SDL::IOStream::FromFile(path.c_str(), "r");
		auto err = doc->decode(std::move(io));
		if (err) {
			SDL::LogError(SDL::LOG_CATEGORY_APPLICATION,
						  "Load error: %s", err->format().c_str());
			return false;
		}
	} catch (const std::exception& e) {
		SDL::LogError(SDL::LOG_CATEGORY_APPLICATION, "Open failed: %s", e.what());
		return false;
	}

	auto root = doc->getRoot();
	if (!root) return false;
	auto mn = std::dynamic_pointer_cast<SDL::ObjectDataNode>(root->get("map"));
	if (!mn) return false;

	auto ma = std::dynamic_pointer_cast<SDL::ObjectDataNode>(mn->get("@attributes"));
	if (ma) {
		map.name     = XmlStr(*ma, "name",        "Untitled");
		map.width    = XmlInt(*ma, "width",         20);
		map.height   = XmlInt(*ma, "height",        15);
		map.tileW    = XmlInt(*ma, "tilewidth",     32);
		map.tileH    = XmlInt(*ma, "tileheight",    32);
		map.infinite = XmlBool(*ma, "infinite",    false);
		auto ori     = XmlStr(*ma, "orientation",  "orthogonal");
		if      (ori == "isometric")        map.orientation = MapOrient::Isometric;
		else if (ori == "hexagonal")        map.orientation = MapOrient::HexFlat;
		else if (ori == "hexagonal-pointy") map.orientation = MapOrient::HexPointy;
		else                                map.orientation = MapOrient::Orthogonal;
	}
	map.filePath   = path;
	map.properties = LoadProperties(*mn);
	map.tilesets.clear();
	map.layers.clear();

	// ── Tilesets ──────────────────────────────────────────────────────────────
	XmlEach(*mn, "tileset", [&](const SDL::ObjectDataNode& tn) {
		auto ta = std::dynamic_pointer_cast<SDL::ObjectDataNode>(tn.get("@attributes"));
		if (!ta) return;
		TilesetDef ts;
		ts.name      = XmlStr(*ta, "name",       "Tileset");
		ts.path      = XmlStr(*ta, "source");
		ts.firstGid  = (TileID)XmlInt(*ta, "firstgid",   1);
		ts.tileW     = XmlInt(*ta, "tilewidth",   16);
		ts.tileH     = XmlInt(*ta, "tileheight",  16);
		ts.spacing   = XmlInt(*ta, "spacing",      0);
		ts.margin    = XmlInt(*ta, "margin",       0);
		ts.columns   = XmlInt(*ta, "columns",      8);
		ts.tileCount = XmlInt(*ta, "tilecount",   64);
		ts.smart     = XmlBool(*ta, "smart");
		ts.key       = "tileset_" + std::to_string(map.tilesets.size());
		// Per-tile metadata
		XmlEach(tn, "tile", [&](const SDL::ObjectDataNode& tileNd) {
			auto tileA = std::dynamic_pointer_cast<SDL::ObjectDataNode>(tileNd.get("@attributes"));
			if (!tileA) return;
			int lid = XmlInt(*tileA, "id", -1);
			if (lid < 0) return;
			TileMetadata& meta = ts.tileData[lid];
			// <animation>
			auto animNd = tileNd.get("animation");
			if (animNd) {
				auto animObj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(animNd);
				if (animObj) {
					XmlEach(*animObj, "frame", [&](const SDL::ObjectDataNode& fn) {
						auto fa = std::dynamic_pointer_cast<SDL::ObjectDataNode>(fn.get("@attributes"));
						if (!fa) return;
						meta.anim.push_back({XmlInt(*fa, "tileid", 0), XmlInt(*fa, "duration", 100)});
					});
				}
			}
			// wangid
			auto wangNd = tileNd.get("wangid");
			if (wangNd) {
				if (auto wi = std::dynamic_pointer_cast<SDL::S32DataNode>(wangNd))
					meta.wangId = (uint32_t)wi->getValue();
			}
			meta.properties = LoadProperties(tileNd);
		});
		map.tilesets.push_back(std::move(ts));
	});

	// ── Tile layers ───────────────────────────────────────────────────────────
	XmlEach(*mn, "layer", [&](const SDL::ObjectDataNode& ln) {
		auto la = std::dynamic_pointer_cast<SDL::ObjectDataNode>(ln.get("@attributes"));
		MapLayer layer; layer.type = LayerType::Tile;
		if (la) {
			layer.name    = XmlStr(*la, "name",    "Layer");
			layer.visible = XmlBool(*la, "visible", true);
			layer.locked  = XmlBool(*la, "locked",  false);
			layer.opacity = XmlFloat(*la, "opacity", 1.f);
		}
		layer.properties = LoadProperties(ln);
		map.layers.push_back(std::move(layer));
		int layerIdx = (int)map.layers.size() - 1;

		if (map.infinite) {
			// Chunked format: <chunks><chunk x y width height data="csv"/>
			auto chunksNd = ln.get("chunks");
			if (chunksNd) {
				auto chunksObj = std::dynamic_pointer_cast<SDL::ObjectDataNode>(chunksNd);
				if (chunksObj) {
					XmlEach(*chunksObj, "chunk", [&](const SDL::ObjectDataNode& cn) {
						auto ca = std::dynamic_pointer_cast<SDL::ObjectDataNode>(cn.get("@attributes"));
						if (!ca) return;
						int cx = XmlInt(*ca, "x",      0);
						int cy = XmlInt(*ca, "y",      0);
						int cw = XmlInt(*ca, "width",  CHUNK_SIZE);
						int ch = XmlInt(*ca, "height", CHUNK_SIZE);
						std::string csv = XmlStr(*ca, "data");
						std::istringstream ss(csv);
						std::string tok;
						for (int ty = cy; ty < cy + ch; ++ty) {
							for (int tx = cx; tx < cx + cw; ++tx) {
								TileID id = EMPTY_TILE;
								if (std::getline(ss, tok, ','))
									try { id = (TileID)std::stoi(tok); } catch (...) {}
								if (id != EMPTY_TILE)
									map.SetTile(layerIdx, tx, ty, id);
							}
						}
					});
				}
			}
		} else {
			// Bounded format: flat CSV in <data>
			auto dn = ln.get("data");
			if (dn) {
				std::string csv;
				if (auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(dn))
					csv = s->getValue();
				std::istringstream ss(csv);
				std::string tok;
				for (int ty = 0; ty < map.height; ++ty) {
					for (int tx = 0; tx < map.width; ++tx) {
						TileID id = EMPTY_TILE;
						if (std::getline(ss, tok, ','))
							try { id = (TileID)std::stoi(tok); } catch (...) {}
						if (id != EMPTY_TILE)
							map.SetTile(layerIdx, tx, ty, id);
					}
				}
			}
		}
	});

	// ── Object layers ──────────────────────────────────────────────────────────
	XmlEach(*mn, "objectgroup", [&](const SDL::ObjectDataNode& ln) {
		auto la = std::dynamic_pointer_cast<SDL::ObjectDataNode>(ln.get("@attributes"));
		MapLayer layer; layer.type = LayerType::Object;
		if (la) {
			layer.name    = XmlStr(*la, "name",    "Objects");
			layer.visible = XmlBool(*la, "visible", true);
			layer.locked  = XmlBool(*la, "locked",  false);
			layer.opacity = XmlFloat(*la, "opacity", 1.f);
		}
		layer.properties = LoadProperties(ln);
		XmlEach(ln, "object", [&](const SDL::ObjectDataNode& on) {
			auto oa = std::dynamic_pointer_cast<SDL::ObjectDataNode>(on.get("@attributes"));
			if (!oa) return;
			ObjectDef obj;
			obj.id       = XmlInt(*oa,   "id",       0);
			obj.name     = XmlStr(*oa,   "name");
			obj.x        = XmlFloat(*oa, "x",        0.f);
			obj.y        = XmlFloat(*oa, "y",        0.f);
			obj.w        = XmlFloat(*oa, "width",   32.f);
			obj.h        = XmlFloat(*oa, "height",  32.f);
			obj.rotation = XmlFloat(*oa, "rotation", 0.f);
			auto tp      = XmlStr(*oa,   "type",    "rect");
			if      (tp == "ellipse") obj.type = ObjectType::Ellipse;
			else if (tp == "point")   obj.type = ObjectType::Point;
			else if (tp == "polygon") obj.type = ObjectType::Polygon;
			else if (tp == "tile")  {
				obj.type   = ObjectType::Tile;
				obj.tileId = (TileID)XmlInt(*oa, "tileid", 0);
			}
			obj.properties = LoadProperties(on);
			layer.objects.push_back(std::move(obj));
		});
		map.layers.push_back(std::move(layer));
	});

	if (map.layers.empty()) {
		MapLayer l; l.name = "Layer 1";
		map.layers.push_back(std::move(l));
	}
	map.activeLayer = 0;
	map.dirty       = false;
	return true;
}

// =============================================================================
// Flood fill
// =============================================================================

[[maybe_unused]] static Command FloodFill(TileMap& map, int layer, int startX, int startY,
						 TileID newId, int maxTiles = 50000) {
	Command cmd;
	if (layer < 0 || layer >= (int)map.layers.size()) return cmd;
	const auto& l = map.layers[layer];
	if (l.type != LayerType::Tile || l.locked) return cmd;
	if (!map.infinite && (startX < 0 || startY < 0 ||
		startX >= map.width || startY >= map.height)) return cmd;

	TileID target = map.GetTile(layer, startX, startY);
	if (target == newId) return cmd;

	// Encode (x,y) as a 64-bit key: upper 32 bits = y, lower 32 = x
	auto key = [](int x, int y) -> uint64_t {
		return (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32);
	};

	std::unordered_set<uint64_t> visited;
	std::queue<std::pair<int,int>> q;
	q.push({startX, startY});

	while (!q.empty() && (int)cmd.changes.size() < maxTiles) {
		auto [x, y] = q.front(); q.pop();
		if (!map.infinite && (x < 0 || y < 0 || x >= map.width || y >= map.height)) continue;
		if (!visited.insert(key(x, y)).second) continue;
		if (map.GetTile(layer, x, y) != target) continue;
		cmd.changes.push_back({layer, x, y, target, newId});
		map.SetTile(layer, x, y, newId);
		q.push({x+1,y}); q.push({x-1,y});
		q.push({x,y+1}); q.push({x,y-1});
	}
	if (!cmd.changes.empty()) map.dirty = true;
	return cmd;
}

// =============================================================================
// Main application
// =============================================================================

struct Main {
	static constexpr SDL::Point kWinSz  = {1440, 860};
	static constexpr int kMaxLayers = 32;
	static constexpr int kTileToolCount = 5; ///< Pencil / Brush / Fill / Erase / Select
	static constexpr int kObjToolCount  = 5; ///< Select / Rect / Ellipse / Point / Polygon
	static constexpr int kLeftW     = 210;
	static constexpr int kRightW    = 250;
	static constexpr int kBottomH   = 0;     ///< reserved for future use

	// ── SDL objects ───────────────────────────────────────────────────────────
	static SDL::Window MakeWindow() {
		return SDL::CreateWindowAndRenderer(
			"SDL3pp - Tile Editor " TILE_EDITOR_VERSION, kWinSz,
			SDL::WINDOW_RESIZABLE, nullptr);
	}

	SDL::MixerRef    mixer   { SDL::CreateMixerDevice(
		SDL::AUDIO_DEVICE_DEFAULT_PLAYBACK,
		SDL::AudioSpec{SDL::AUDIO_F32, 2, 48000}) };
	SDL::Window      window  { MakeWindow()          };
	SDL::RendererRef renderer{ window.GetRenderer()  };

	SDL::ResourceManager resources;
	SDL::ResourcePool& pool_ui   { *resources.CreatePool(pool_key::UI)    };
	SDL::ResourcePool& pool_tiles{ *resources.CreatePool(pool_key::TILES) };

	SDL::ECS::Context  ecs_context;
	SDL::UI::System  ui{ ecs_context, renderer, mixer, pool_ui };
	SDL::FrameTimer  frameTimer{ 60.f };

	// ── Editor data ───────────────────────────────────────────────────────────
	TileMap     map;
	EditorState state;
	UndoRedo    ur;
	Project     project;  ///< Shared model for non-map workspaces.

	// ── Workspace tab IDs ─────────────────────────────────────────────────────
	enum WsTab { WsMap = 0, WsScripts, WsCine, WsGraph, WsTest, WsCount };
	int  m_activeTab = WsMap;
	bool m_tabRebuiltOnce = false;  ///< Tab content is built lazily on first display.

	SDL::ECS::EntityId eTabView      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTabContent[WsCount] = {}; ///< per-tab content container.

	// ── Scripts workspace UI ─────────────────────────────────────────────────
	SDL::ECS::EntityId eScriptArea   = SDL::ECS::NullEntity; ///< TextArea editor.
	SDL::ECS::EntityId eScriptName   = SDL::ECS::NullEntity; ///< Label / breadcrumb.
	SDL::ECS::EntityId eScriptCons   = SDL::ECS::NullEntity; ///< Console TextArea.
	std::string        m_consoleLog;                         ///< Buffered Lua output.

	// ── Cinematic workspace UI ───────────────────────────────────────────────
	SDL::ECS::EntityId eCineCanvas   = SDL::ECS::NullEntity; ///< Timeline canvas.
	SDL::ECS::EntityId eCinePlayhead = SDL::ECS::NullEntity; ///< Label showing time.
	float              m_cineTime    = 0.f;                  ///< Current playhead (s).
	bool               m_cinePlaying = false;
	int                m_cineDragTrack = -1, m_cineDragClip = -1;
	bool               m_cineResize  = false;

	// ── NodeGraph workspace UI ───────────────────────────────────────────────
	SDL::ECS::EntityId eGraphCanvas  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eGraphStatus  = SDL::ECS::NullEntity;
	float              m_graphViewX  = 0.f, m_graphViewY = 0.f;
	float              m_graphZoom   = 1.f;
	int                m_selectedNode = -1;
	bool               m_graphPanning = false;
	bool               m_graphNodeDrag = false;
	SDL::FPoint        m_graphDragOrigin = {};
	SDL::FPoint        m_graphDragNodeStart = {};
	// Wire-in-progress: from a node:port to the cursor.
	int                m_wireFromNode = -1, m_wireFromPort = -1;

	// ── Test workspace state (Mario-like physics) ────────────────────────────
	struct PlayerPhys {
		SDL::FPoint pos     = {64.f, 64.f};  ///< world px
		SDL::FPoint vel     = {0.f, 0.f};
		SDL::FPoint size    = {28.f, 44.f};
		bool        onGround = false;
		bool        alive    = true;
	};
	PlayerPhys         m_player;
	bool               m_testPlaying = false;
	SDL::ECS::EntityId eTestCanvas   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTestStatus   = SDL::ECS::NullEntity;
	int                m_testCollisionLayer = 0; ///< layer used for tile collision.
	std::unordered_set<int> m_testKeysHeld;     ///< SDL keycodes currently down.

#ifdef SDL3PP_TILE_EDITOR_LUA
	lua_State*         m_lua         = nullptr; ///< Persistent Lua state for the project.
#endif

	// ── UI entity IDs ─────────────────────────────────────────────────────────
	SDL::ECS::EntityId eMapCanvas      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTilesetCanvas  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eStatusLabel    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTilesetName    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTileInfo       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eLayerContent   = SDL::ECS::NullEntity;
	SDL::ECS::EntityId tileToolBtns[kTileToolCount] = {};
	SDL::ECS::EntityId objToolBtns [kObjToolCount]  = {};
	SDL::ECS::EntityId eToolRowTile    = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eToolRowObj     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eGridBtn        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMapTileW       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMapTileH       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTsTileW        = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eTsTileH        = SDL::ECS::NullEntity;
	// Properties panel (right side bottom)
	SDL::ECS::EntityId ePropsHdr       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsBody      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsName      = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsX         = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsY         = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsW         = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsH         = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsRot       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId ePropsTypeLbl   = SDL::ECS::NullEntity;
	// Active layer opacity slider (in layer panel)
	SDL::ECS::EntityId eOpacitySlider  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eOpacityLabel   = SDL::ECS::NullEntity;
	// Map properties popup
	SDL::ECS::EntityId eMapPropsPopup  = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMapPropW       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMapPropH       = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMapPropInf     = SDL::ECS::NullEntity;
	SDL::ECS::EntityId eMapPropOri     = SDL::ECS::NullEntity;
	int                m_syncedTs      = -2;
	int                m_lastSelObjId  = -2;

	struct LayerSlot {
		SDL::ECS::EntityId row     = SDL::ECS::NullEntity;
		SDL::ECS::EntityId btnVis  = SDL::ECS::NullEntity;
		SDL::ECS::EntityId lblName = SDL::ECS::NullEntity;
		SDL::ECS::EntityId btnLock = SDL::ECS::NullEntity;
	};
	std::array<LayerSlot, kMaxLayers> layerSlots;

	// ── App lifecycle ─────────────────────────────────────────────────────────

	static SDL::AppResult Init(Main** out, SDL::AppArgs args) {
		SDL::LogPriority prio = SDL::LOG_PRIORITY_WARN;
		for (auto arg : args) {
			if      (arg == "--verbose") prio = SDL::LOG_PRIORITY_VERBOSE;
			else if (arg == "--debug")   prio = SDL::LOG_PRIORITY_DEBUG;
			else if (arg == "--info")    prio = SDL::LOG_PRIORITY_INFO;
		}
		SDL::SetLogPriorities(prio);
		SDL::SetAppMetadata("SDL3pp Tile Editor", TILE_EDITOR_VERSION, "com.example.tile_editor");
		SDL::Init(SDL::INIT_VIDEO);
		SDL::TTF::Init();
		SDL::MIX::Init();
		*out = new Main();
		return SDL::APP_CONTINUE;
	}
	static void Quit(Main* m, SDL::AppResult) {
		delete m;
		SDL::MIX::Quit();
		SDL::TTF::Quit();
		SDL::Quit();
	}

	Main() {
		window.StartTextInput();
		map.Init();
		project.Init();
		_LoadResources();
#ifdef SDL3PP_TILE_EDITOR_LUA
		_LuaInit();
#endif
		_BuildUI();
	}
	~Main() {
#ifdef SDL3PP_TILE_EDITOR_LUA
		_LuaShutdown();
#endif
		resources.ReleaseAll();
	}

	// ── Event ─────────────────────────────────────────────────────────────────

	SDL::AppResult Event(const SDL::Event& ev) {
		if (ev.type == SDL::EVENT_QUIT) return SDL::APP_SUCCESS;
		if (ev.type == SDL::EVENT_KEY_DOWN) {
			auto key  = ev.key.key;
			auto mod  = ev.key.mod;
			bool ctrl  = (mod & SDL::KMOD_CTRL)  != 0;
			bool shift = (mod & SDL::KMOD_SHIFT) != 0;

			if (ctrl  && key == SDL::KEYCODE_Q)              return SDL::APP_SUCCESS;
			if (ctrl  && key == SDL::KEYCODE_N)            { _NewMap();     return SDL::APP_CONTINUE; }
			if (ctrl  && !shift && key == SDL::KEYCODE_S)  { _SaveMap();    return SDL::APP_CONTINUE; }
			if (ctrl  && shift  && key == SDL::KEYCODE_S)  { _SaveMapAs();  return SDL::APP_CONTINUE; }
			if (ctrl  && key == SDL::KEYCODE_O)            { _OpenMap();    return SDL::APP_CONTINUE; }
			if (ctrl  && key == SDL::KEYCODE_Z)            { _Undo();       return SDL::APP_CONTINUE; }
			if ((ctrl && key == SDL::KEYCODE_Y) ||
				(ctrl && shift && key == SDL::KEYCODE_Z))  { _Redo();       return SDL::APP_CONTINUE; }
			if (!ctrl) {
				bool objLayer = _ActiveLayerIsObject();
				if (!objLayer) {
					if (key == SDL::KEYCODE_P) _SetTool(ToolType::Pencil);
					if (key == SDL::KEYCODE_B) _SetTool(ToolType::Brush);
					if (key == SDL::KEYCODE_F) _SetTool(ToolType::Fill);
					if (key == SDL::KEYCODE_E) _SetTool(ToolType::Erase);
					if (key == SDL::KEYCODE_S) _SetTool(ToolType::Select);
				} else {
					if (key == SDL::KEYCODE_V)      _SetTool(ToolType::ObjSelect);
					if (key == SDL::KEYCODE_R)      _SetTool(ToolType::ObjRect);
					if (key == SDL::KEYCODE_C)      _SetTool(ToolType::ObjEllipse);
					if (key == SDL::KEYCODE_PERIOD) _SetTool(ToolType::ObjPoint);
					if (shift && key == SDL::KEYCODE_P) _SetTool(ToolType::ObjPolygon);
					if (key == SDL::KEYCODE_DELETE) _DeleteSelectedObject();
					if (key == SDL::KEYCODE_RETURN) _FinishPolygon();
					if (key == SDL::KEYCODE_ESCAPE) state.polyPoints.clear();
				}
				if (key == SDL::KEYCODE_G) { state.showGrid = !state.showGrid; _RefreshGridBtn(); }
				if (key == SDL::KEYCODE_EQUALS || key == SDL::KEYCODE_KP_PLUS)
					_ZoomAt(1.25f, {kWinSz.x / 2.f, kWinSz.y / 2.f});
				if (key == SDL::KEYCODE_MINUS || key == SDL::KEYCODE_KP_MINUS)
					_ZoomAt(0.8f,  {kWinSz.x / 2.f, kWinSz.y / 2.f});
				if (key == SDL::KEYCODE_0)
					{ state.zoom = 1.f; state.viewX = 0.f; state.viewY = 0.f; }
				if (key == SDL::KEYCODE_PAGEUP)   _MoveActiveLayer(-1);
				if (key == SDL::KEYCODE_PAGEDOWN) _MoveActiveLayer(+1);
			}
		}
		ui.ProcessEvent(ev);
		return SDL::APP_CONTINUE;
	}

	// ── Iterate ───────────────────────────────────────────────────────────────

	SDL::AppResult Iterate() {
		frameTimer.Begin();
		const float dt = frameTimer.GetDelta();

		// Pump pending file-dialog results (populated by async SDL callbacks)
		if (state.pendingNew) {
			state.pendingNew = false;
			_DoNewMap();
		}
		if (!state.pendingOpenPath.empty()) {
			auto p = state.pendingOpenPath; state.pendingOpenPath.clear();
			_DoOpenMap(p);
		}
		if (!state.pendingSavePath.empty()) {
			auto p = state.pendingSavePath; state.pendingSavePath.clear();
			SaveMap(map, p);
			map.filePath = p;
			map.dirty    = false;
			_UpdateTitle();
		}
		if (!state.pendingTilesetPath.empty()) {
			auto p = state.pendingTilesetPath; state.pendingTilesetPath.clear();
			_DoImportTileset(p);
		}

		resources.UpdateAll();

		// After textures load, compute tileset dimensions from image size
		for (auto& ts : map.tilesets) {
			if (ts.imageW <= 0 && !ts.key.empty()) {
				auto h = pool_ui.Get<SDL::Texture>(ts.key);
				if (h) {
					SDL::Point sz = h->GetSize();
					ts.imageW    = sz.x;
					ts.imageH    = sz.y;
					int sp       = SDL::Max(ts.spacing, 0);
					ts.columns   = SDL::Max(1, (sz.x - 2*ts.margin) / (ts.tileW + (sp ? sp : 1)));
					ts.rows      = SDL::Max(1, (sz.y - 2*ts.margin) / (ts.tileH + (sp ? sp : 1)));
					ts.tileCount = ts.columns * ts.rows;
				}
			}
		}

		// Sync tileset spinboxes and reset zoom/scroll when active tileset changes
		if (m_syncedTs != state.activeTileset) {
			m_syncedTs = state.activeTileset;
			// Auto-fit the new tileset in the palette view
			state.tsTileZoom = 0.f;
			state.tsScrollX  = 0.f;
			state.tsScrollY  = 0.f;
			if (eTsTileW != SDL::ECS::NullEntity &&
				!map.tilesets.empty() && state.activeTileset < (int)map.tilesets.size()) {
				const auto& ts = map.tilesets[state.activeTileset];
				ui.SetValue(eTsTileW, float(ts.tileW));
				ui.SetValue(eTsTileH, float(ts.tileH));
			}
		}

		_UpdateStatus(dt);
		_UpdateLayerSlots();
		_UpdateToolRowVisibility();
		_UpdatePropertiesPanel();
		_SyncOpacitySlider();

		// Per-tab updates
		if (m_activeTab == WsCine) _CineUpdate(dt);

		// Drain main-thread closures queued by graph workers (Lua isn't
		// thread-safe). Always do this — workers might be running while we
		// browse another tab.
		_DrainPendingMain();
		_ConsoleFlushToUI();

		// Animation clock for animated tiles
		state.animTime += dt;

		renderer.SetDrawColor(pal::BG);
		renderer.RenderClear();
		ui.Iterate(dt);
		renderer.Present();
		frameTimer.End();
		return SDL::APP_CONTINUE;
	}

	void _SyncOpacitySlider() {
		if (eOpacitySlider == SDL::ECS::NullEntity) return;
		if (map.activeLayer < 0 || map.activeLayer >= (int)map.layers.size()) return;
		static int lastLayer = -1;
		if (lastLayer != map.activeLayer) {
			lastLayer = map.activeLayer;
			float op = map.layers[map.activeLayer].opacity;
			ui.SetValue(eOpacitySlider, op);
			ui.SetText(eOpacityLabel, std::format("{:.0f}%", op * 100.f));
		}
	}

	// =========================================================================
	// Resources
	// =========================================================================

	void _LoadResources() {
		const std::string base = std::string(SDL::GetBasePath()) + "../../../assets/";
		ui.LoadFont(res_key::FONT,  base + "fonts/DejaVuSans.ttf");
		ui.SetDefaultFont(res_key::FONT, 13.f);
		ui.LoadAudio(res_key::CLICK, base + "sounds/effect-click.mp3");
		ui.LoadAudio(res_key::FAIL,  base + "sounds/effect-fail.mp3");

		const std::string icons = base + "textures/icons/";
		ui.LoadTexture(icon_key::NEW,        icons + "icon_new.png");
		ui.LoadTexture(icon_key::OPEN,       icons + "icon_open.png");
		ui.LoadTexture(icon_key::SAVE,       icons + "icon_save.png");
		ui.LoadTexture(icon_key::SAVE_AS,    icons + "icon_save_as.png");
		ui.LoadTexture(icon_key::IMPORT,     icons + "icon_import.png");
		ui.LoadTexture(icon_key::LAYER_ADD,  icons + "icon_layer_add.png");
		ui.LoadTexture(icon_key::LAYER_DEL,  icons + "icon_layer_remove.png");
		ui.LoadTexture(icon_key::PENCIL,     icons + "icon_pencil.png");
		ui.LoadTexture(icon_key::BRUSH,      icons + "icon_brush.png");
		ui.LoadTexture(icon_key::FILL,       icons + "icon_fill.png");
		ui.LoadTexture(icon_key::ERASE,      icons + "icon_erase.png");
		ui.LoadTexture(icon_key::SELECT,     icons + "icon_select.png");
		ui.LoadTexture(icon_key::UNDO,       icons + "icon_undo.png");
		ui.LoadTexture(icon_key::REDO,       icons + "icon_redo.png");
		ui.LoadTexture(icon_key::GRID,       icons + "icon_grid.png");
		ui.LoadTexture(icon_key::ZOOM_IN,    icons + "icon_zoom_in.png");
		ui.LoadTexture(icon_key::ZOOM_OUT,   icons + "icon_zoom_out.png");
		ui.LoadTexture(icon_key::VISIBILITY, icons + "icon_visibility.png");
		ui.LoadTexture(icon_key::UP,         icons + "icon_up_arrow.png");
		ui.LoadTexture(icon_key::DOWN,       icons + "icon_down_arrow.png");
		ui.LoadTexture(icon_key::LEFT,       icons + "icon_left_arrow.png");
		ui.LoadTexture(icon_key::RIGHT,      icons + "icon_right_arrow.png");
		ui.LoadTexture(icon_key::LOCK,       icons + "icon_collision.png");
		ui.LoadTexture(icon_key::STAMP,      icons + "icon_stamp.png");
	}

	// =========================================================================
	// UI construction
	// =========================================================================

	void _BuildUI() {
		_BuildMapPropsPopup();

		// Workspace tab bar — sits between the toolbar and the per-tab content.
		auto tabBar = _BuildWorkspaceTabBar();

		ui.Column("root", 0.f, 0.f)
			.BgColor(pal::BG)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			})
			.W(SDL::UI::Value::Ww(100.f))
			.H(SDL::UI::Value::Wh(100.f))
			.Padding(0.f)
			.Children(
				_BuildToolbar(),
				tabBar,
				_BuildWorkspaceContainer(),  // hosts the active workspace
				_BuildStatusBar(),
				eMapPropsPopup
			)
			.AsRoot();
	}

	// ── Workspace tab bar (sits below the toolbar) ────────────────────────────
	SDL::ECS::EntityId _BuildWorkspaceTabBar() {
		auto bar = ui.Row("workspace_tabs", 4.f, 0.f)
			.W(SDL::UI::Value::Ww(100.f)).H(30.f)
			.PaddingH(8.f).PaddingV(2.f)
			.BgColor(pal::HEADER)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f, 0.f, 1.f, 0.f);
				s.bdColor = pal::BORDER;
				s.radius  = SDL::FCorners(0.f);
			});

		static constexpr struct { WsTab tab; const char* label; const char* tip; } kTabs[] = {
			{WsMap,     "Map",        "World / map editor"},
			{WsScripts, "Scripts",    "Lua script editor + REPL"},
			{WsCine,    "Cinematic",  "Cinematic timeline"},
			{WsGraph,   "Node Graph", "Async block graph (events, dialog, scripts)"},
			{WsTest,    "Test",       "Play the map with physics (Mario-style)"},
		};
		for (const auto& t : kTabs) {
			WsTab tab = t.tab;
			auto b = ui.Button(std::format("ws_tab_{}", (int)tab), t.label)
				.W(SDL::UI::Value::Auto()).H(26.f).PaddingH(12.f)
				.Font(res_key::FONT, 12.f)
				.Tooltip(t.tip, 0.6f)
				.ClickSound(res_key::CLICK)
				.WithStyle([](auto& s){
					s.borders = SDL::FBox(0.f);
					s.radius  = SDL::FCorners(4.f);
					s.bgColor        = SDL::Color(0,0,0,0);
					s.bgHoveredColor = pal::SURFACE2;
					s.bgPressedColor = pal::ACCENT3;
					s.textColor      = pal::TEXT;
				})
				.OnClick([this, tab]{ _SwitchTab(tab); })
				.Id();
			bar.Child(b);
			eTabContent[tab] = SDL::ECS::NullEntity; // resolved when its panel is built
		}
		return bar;
	}

	// ── Workspace content container (single child = active workspace) ──────
	SDL::ECS::EntityId _BuildWorkspaceContainer() {
		// We pre-build all 5 workspace panels and toggle visibility on tab switch.
		eTabContent[WsMap]     = _BuildMainContent();
		eTabContent[WsScripts] = _BuildScriptsWorkspace();
		eTabContent[WsCine]    = _BuildCinematicWorkspace();
		eTabContent[WsGraph]   = _BuildNodeGraphWorkspace();
		eTabContent[WsTest]    = _BuildTestWorkspace();

		// Hide all but the default active tab.
		for (int i = 0; i < WsCount; ++i)
			if (eTabContent[i] != SDL::ECS::NullEntity)
				ui.SetVisible(eTabContent[i], i == m_activeTab);

		return ui.Column("workspace_host", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); s.radius = SDL::FCorners(0.f); })
			.Children(eTabContent[WsMap], eTabContent[WsScripts],
			          eTabContent[WsCine], eTabContent[WsGraph], eTabContent[WsTest]);
	}

	void _SwitchTab(WsTab tab) {
		if (tab < 0 || tab >= WsCount) return;
		if (m_activeTab == tab) return;
		// Stop the test simulation if we leave the Test tab.
		if (m_activeTab == WsTest) m_testPlaying = false;
		m_activeTab = tab;
		for (int i = 0; i < WsCount; ++i)
			if (eTabContent[i] != SDL::ECS::NullEntity)
				ui.SetVisible(eTabContent[i], i == m_activeTab);
		_OnTabActivated(tab);
	}

	void _OnTabActivated(WsTab tab) {
		// Refresh side workspaces from project state when they become visible.
		switch (tab) {
			case WsScripts:
				if (eScriptArea != SDL::ECS::NullEntity && !project.scripts.empty()) {
					int idx = std::clamp(project.activeScript, 0, (int)project.scripts.size()-1);
					ui.SetText(eScriptArea, project.scripts[idx].code);
					ui.SetText(eScriptName, std::format("Script: {}", project.scripts[idx].name));
				}
				project.dirtyFor = project.dirtyFor & (WorkspaceKind)~(uint32_t)WorkspaceKind::Scripts;
				break;
			case WsCine:
				project.dirtyFor = project.dirtyFor & (WorkspaceKind)~(uint32_t)WorkspaceKind::Cinematic;
				break;
			case WsGraph:
				project.dirtyFor = project.dirtyFor & (WorkspaceKind)~(uint32_t)WorkspaceKind::NodeGraph;
				break;
			case WsTest:
				_TestReset();
				project.dirtyFor = project.dirtyFor & (WorkspaceKind)~(uint32_t)WorkspaceKind::Test;
				break;
			default:
				break;
		}
	}

	// ── Map properties popup ─────────────────────────────────────────────────
	void _BuildMapPropsPopup() {
		using namespace SDL::UI;
		constexpr float popW = 320.f, popH = 240.f;

		Style hdr;
		hdr.bgColor   = pal::SURFACE;
		hdr.textColor = pal::ACCENT;
		hdr.fontKey   = res_key::FONT;
		hdr.fontSize  = 11.f;

		eMapPropW = ui.InputValue<int>("inp_mp_w", 1, 8192, map.width, 1)
			.Grow(100.f).H(22.f)
			.OnChange<int>([this](int v){ map.width  = SDL::Max(1,v); map.dirty=true; })
			.Id();
		eMapPropH = ui.InputValue<int>("inp_mp_h", 1, 8192, map.height, 1)
			.Grow(100.f).H(22.f)
			.OnChange<int>([this](int v){ map.height = SDL::Max(1,v); map.dirty=true; })
			.Id();
		eMapPropInf = ui.ComboBox("cmb_mp_inf",
			{"Bounded (fixed)", "Infinite (chunks)"}, map.infinite ? 1 : 0)
			.Grow(100.f).H(22.f).Font(res_key::FONT, 11.f)
			.OnChange<int>([this](int v){ map.infinite = (v == 1); map.dirty=true; })
			.Id();
		eMapPropOri = ui.ComboBox("cmb_mp_ori",
			{"Orthogonal", "Isometric", "Hex (flat)", "Hex (pointy)"},
			map.orientation == MapOrient::Isometric ? 1 :
			map.orientation == MapOrient::HexFlat   ? 2 :
			map.orientation == MapOrient::HexPointy ? 3 : 0)
			.Grow(100.f).H(22.f).Font(res_key::FONT, 11.f)
			.OnChange<int>([this](int v){
				switch (v) {
					case 1: map.orientation = MapOrient::Isometric; break;
					case 2: map.orientation = MapOrient::HexFlat;   break;
					case 3: map.orientation = MapOrient::HexPointy; break;
					default:map.orientation = MapOrient::Orthogonal;break;
				}
				map.dirty = true;
			})
			.Id();

		auto mkRow = [&](const char* id, const char* lbl, SDL::ECS::EntityId widget) {
			return ui.Row(id, 6.f, 6.f).GrowW(100.f).H(28.f)
				.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
				.AlignChildrenV(Align::Center)
				.Children(
					ui.Label(std::format("{}_l", id), lbl)
						.W(90).TextColor(pal::TEXT).Font(res_key::FONT, 12.f),
					widget
				);
		};

		auto content = ui.Column("mp_col", 4.f, 6.f)
			.GrowW(100.f).BgColor(pal::BG)
			.PaddingH(10.f).PaddingV(8.f)
			.Children(
				mkRow("mp_r_w",   "Width (tiles)",  eMapPropW),
				mkRow("mp_r_h",   "Height (tiles)", eMapPropH),
				mkRow("mp_r_inf", "Storage",        eMapPropInf),
				mkRow("mp_r_ori", "Orientation",    eMapPropOri)
			);

		eMapPropsPopup = ui.Popup("popup_map_props", "Map properties", true, true, false)
			.W(popW).H(popH)
			.Fixed(Value::Ww(50.f) - popW * 0.5f,
			       Value::Wh(50.f) - popH * 0.5f)
			.BgColor(pal::PANEL)
			.Children(content).Id();
		ui.SetPopupOpen(eMapPropsPopup, false);
	}

	void _ShowMapPropsPopup() {
		if (eMapPropW != SDL::ECS::NullEntity) {
			ui.SetValue(eMapPropW, float(map.width));
			ui.SetValue(eMapPropH, float(map.height));
		}
		ui.SetPopupOpen(eMapPropsPopup, true);
	}

	// ── Toolbar ──────────────────────────────────────────────────────────────

	SDL::ECS::EntityId _BuildToolbar() {
		auto bar = ui.Row("toolbar", 2.f, 0.f)
			.W(SDL::UI::Value::Ww(100.f)).H(42.f)
			.PaddingH(6.f).PaddingV(4.f)
			.BgColor(pal::HEADER)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f, 0.f, 1.f, 0.f);
				s.bdColor = pal::BORDER;
				s.radius  = SDL::FCorners(0.f);
			});

		// Applies the flat/ghost style shared by all icon buttons in the toolbar
		auto flat = [](SDL::UI::Builder& b) -> SDL::UI::Builder& {
			return b.BgColor({0,0,0,0})
					.BgHoveredColor({42,54,78,220})
					.BgPressedColor(pal::ACCENT)
					.WithStyle([](auto& s){
						s.borders = SDL::FBox(0.f);
						s.radius  = SDL::FCorners(4.f);
					});
		};

		// Generic flat icon button
		auto mkBtn = [&](const char* id, const char* key, const char* tip,
						 std::function<void()> cb) -> SDL::ECS::EntityId {
			auto b = ui.Button(id).W(32).H(32).Padding(0.f)
				.Icon(key, 5.f).IconOpacity(0.65f, 1.f, 0.9f)
				.ClickSound(res_key::CLICK)
				.Tooltip(tip, 0.6f).OnClick(std::move(cb));
			return flat(b).Id();
		};

		// Thin vertical divider
		auto mkSep = [&](const char* id) -> SDL::ECS::EntityId {
			return ui.Container(id).W(1).H(28)
				.WithStyle([](auto& s){
					s.bgColor = pal::BORDER;
					s.borders = SDL::FBox(0.f);
					s.radius  = SDL::FCorners(0.f);
				});
		};

		// Tile tool buttons — built ahead so we can pass entity IDs to bar.Children()
		static constexpr struct { ToolType type; const char* icon; const char* tip; }
		kTileTools[kTileToolCount] = {
			{ToolType::Pencil, icon_key::PENCIL, "Pencil (P)"},
			{ToolType::Brush,  icon_key::BRUSH,  "Brush  (B)"},
			{ToolType::Fill,   icon_key::FILL,   "Fill   (F)"},
			{ToolType::Erase,  icon_key::ERASE,  "Erase  (E)"},
			{ToolType::Select, icon_key::SELECT, "Select (S)"},
		};
		for (int i = 0; i < kTileToolCount; ++i) {
			ToolType t = kTileTools[i].type;
			auto b = ui.Button(std::format("btn_ttool{}", i)).W(32).H(32).Padding(0.f)
				.Icon(kTileTools[i].icon, 5.f).IconOpacity(1.f)
				.Tooltip(kTileTools[i].tip, 0.6f)
				.ClickSound(res_key::CLICK)
				.OnClick([this, t]{ _SetTool(t); });
			tileToolBtns[i] = flat(b).Id();
		}

		// Object tool buttons (Select / Rect / Ellipse / Point / Polygon)
		static constexpr struct { ToolType type; const char* icon; const char* tip; }
		kObjTools[kObjToolCount] = {
			{ToolType::ObjSelect,  icon_key::SELECT,  "Object Select & Move (V)"},
			{ToolType::ObjRect,    icon_key::STAMP,   "Rectangle Object / Collision Box (R)"},
			{ToolType::ObjEllipse, icon_key::FILL,    "Ellipse Object (C)"},
			{ToolType::ObjPoint,   icon_key::GRID,    "Point Object (.)"},
			{ToolType::ObjPolygon, icon_key::PENCIL,  "Polygon Collision (Shift-P)"},
		};
		for (int i = 0; i < kObjToolCount; ++i) {
			ToolType t = kObjTools[i].type;
			auto b = ui.Button(std::format("btn_otool{}", i)).W(32).H(32).Padding(0.f)
				.Icon(kObjTools[i].icon, 5.f).IconOpacity(1.f)
				.Tooltip(kObjTools[i].tip, 0.6f)
				.ClickSound(res_key::CLICK)
				.OnClick([this, t]{ _SetTool(t); });
			objToolBtns[i] = flat(b).Id();
		}

		// Grid toggle button — tint is updated by _RefreshGridBtn()
		{
			auto b = ui.Button("btn_grid").W(32).H(32).Padding(0.f)
				.Icon(icon_key::GRID, 5.f).IconOpacity(1.f)
				.Tooltip("Grid (G)", 0.6f)
				.ClickSound(res_key::CLICK)
				.OnClick([this]{ state.showGrid = !state.showGrid; _RefreshGridBtn(); });
			eGridBtn = flat(b).Id();
		}

		// Map zoom reset (1:1) button
		SDL::ECS::EntityId btnZoomReset;
		{
			auto b = ui.Button("btn_zoom_reset", "1:1").W(34).H(32).Padding(2.f)
				.Tooltip("Reset map zoom to 100% (0)", 0.6f)
				.ClickSound(res_key::CLICK)
				.OnClick([this]{ state.zoom=1.f; state.viewX=0.f; state.viewY=0.f; });
			btnZoomReset = flat(b).Id();
		}

		// Tile tools row container — visibility toggled by _UpdateToolRowVisibility()
		eToolRowTile = ui.Row("tile_tools_row", 2.f, 0.f)
			.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
			.Children(tileToolBtns[0], tileToolBtns[1], tileToolBtns[2],
			          tileToolBtns[3], tileToolBtns[4]);

		// Object tools row container — visible only when active layer is an Object layer
		eToolRowObj = ui.Row("obj_tools_row", 2.f, 0.f)
			.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
			.Children(objToolBtns[0], objToolBtns[1], objToolBtns[2],
			          objToolBtns[3], objToolBtns[4]);

		bar.Children(
			// File ops
			mkBtn("btn_new",     icon_key::NEW,     "New Map  (Ctrl+N)",       [this]{ _NewMap();    }),
			mkBtn("btn_open",    icon_key::OPEN,    "Open Map (Ctrl+O)",       [this]{ _OpenMap();   }),
			mkBtn("btn_save",    icon_key::SAVE,    "Save     (Ctrl+S)",       [this]{ _SaveMap();   }),
			mkBtn("btn_save_as", icon_key::SAVE_AS, "Save As  (Ctrl+Shift+S)", [this]{ _SaveMapAs(); }),
			mkSep("sep1"),
			// Map / layer / tileset ops
			mkBtn("btn_map_prop", icon_key::GRID,      "Map Properties...", [this]{ _ShowMapPropsPopup(); }),
			mkBtn("btn_import",   icon_key::IMPORT,    "Import Tileset",    [this]{ _ImportTileset();     }),
			mkBtn("btn_add_lyr",  icon_key::LAYER_ADD, "Add Tile Layer",    [this]{ _AddTileLayer();      }),
			mkBtn("btn_add_obj",  icon_key::STAMP,     "Add Object Layer",  [this]{ _AddObjectLayer();    }),
			mkSep("sep2"),
			// Active tool row (one of the two is shown depending on layer type)
			eToolRowTile, eToolRowObj,
			mkSep("sep3"),
			// Undo / Redo
			mkBtn("btn_undo",    icon_key::UNDO, "Undo (Ctrl+Z)", [this]{ _Undo(); }),
			mkBtn("btn_redo",    icon_key::REDO, "Redo (Ctrl+Y)", [this]{ _Redo(); }),
			mkSep("sep4"),
			// View
			eGridBtn,
			mkBtn("btn_zoom_in",  icon_key::ZOOM_IN,  "Zoom In  (+)",
				  [this]{ _ZoomAt(1.25f, {kWinSz.x / 2.f, kWinSz.y / 2.f}); }),
			mkBtn("btn_zoom_out", icon_key::ZOOM_OUT, "Zoom Out (-)",
				  [this]{ _ZoomAt(0.8f,  {kWinSz.x / 2.f, kWinSz.y / 2.f}); }),
			btnZoomReset
		);

		// Spacer + title
		bar.Child(ui.Container("spacer_bar").Grow(100.f)
			.WithStyle([](auto& s){
				s.bgColor = SDL::Color(0,0,0,0);
				s.borders = SDL::FBox(0.f);
			}));
		bar.Child(ui.Label("lbl_app_title", "Tile Editor " TILE_EDITOR_VERSION)
			.TextColor(pal::ACCENT));

		// Apply initial active states
		_SetTool(state.tool);
		_RefreshGridBtn();

		return bar;
	}

	// ── Main content (3 columns) ──────────────────────────────────────────────

	SDL::ECS::EntityId _BuildMainContent() {
		auto content = ui.Row("main_content", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			});
		content.Children(
			_BuildLayerPanel(),
			_BuildMapCanvas(),
			_BuildTilesetPanel()
		);
		return content;
	}

	// ── Layer panel (left column) ─────────────────────────────────────────────

	SDL::ECS::EntityId _BuildLayerPanel() {
		auto panel = ui.Column("layer_panel", 0.f, 0.f)
			.W(float(kLeftW))
			.BgColor(pal::PANEL)
			.WithStyle([](auto& s){
				s.borders     = SDL::FBox(0.f, 0.f, 0.f, 1.f);
				s.bdColor = pal::BORDER;
				s.radius      = SDL::FCorners(0.f);
			});

		panel.Child(ui.Label("lbl_layers_hdr", "Layers")
			.TextColor(pal::ACCENT)
			.PaddingH(8).PaddingV(4)
			.BgColor(pal::HEADER)
			.W(SDL::UI::Value::Pw(100.f)));

		// Scrollable list of layer rows
		auto sv = ui.ScrollView("layer_sv", 0.f)
			.Grow(100.f).Padding(0.f).ScrollableY();
		eLayerContent = ui.Column("layer_list", 0.f, 0.f)
			.WithStyle([](auto& s){
				s.bgColor = SDL::Color(0,0,0,0);
				s.borders = SDL::FBox(0.f);
				s.radius  = SDL::FCorners(0.f);
			})
			.WithLayout([](auto& l){
				l.padding = SDL::FBox(0.f);
				l.margin  = SDL::FBox(0.f);
			});

		// Pre-build fixed layer slots (max kMaxLayers)
		for (int i = 0; i < kMaxLayers; ++i) {
			auto& slot = layerSlots[i];

			auto row = ui.Row(std::format("ls_row{}", i), 2.f, 0.f)
				.W(SDL::UI::Value::Pw(100.f)).H(26.f)
				.PaddingH(4.f).PaddingV(2.f)
				.WithStyle([](auto& s){
					s.bgColor        = SDL::Color(0,0,0,0);
					s.bgHoveredColor = SDL::Color(32, 48, 76, 140);
					s.borders        = SDL::FBox(0.f, 0.f, 1.f, 0.f);
					s.bdColor        = pal::BORDER;
					s.radius         = SDL::FCorners(0.f);
				})
				.Hoverable().Selectable()
				.OnClick([this, i]{ _SelectLayer(i); })
				.Hide();
			slot.row = row;

			// Visibility icon button — tint/opacity updated by _UpdateLayerSlots()
			slot.btnVis = ui.Button(std::format("ls_vis{}", i))
				.W(20).H(20).Padding(0.f)
				.Icon(icon_key::VISIBILITY, 3.f)
				.ClickSound(res_key::CLICK)
				.Tooltip("Toggle Layer Visibility", 0.6f)
				.BgColor({0,0,0,0}).BgHoveredColor({42,54,78,180}).BgPressedColor({42,54,78,220})
				.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); s.radius = SDL::FCorners(3.f); })
				.OnClick([this, i]{ _ToggleLayerVisible(i); })
				.Id();

			slot.lblName = ui.Label(std::format("ls_name{}", i), "Layer")
				.Grow(100.f).TextColor(pal::WHITE)
				.PaddingH(4).PaddingV(0)
				.Hoverable().Selectable()
				.OnClick([this, i]{ _SelectLayer(i); });

			// Lock icon button — tint/bg updated by _UpdateLayerSlots()
			slot.btnLock = ui.Button(std::format("ls_lock{}", i))
				.W(20).H(20).Padding(0.f)
				.Icon(icon_key::LOCK, 3.f)
				.BgColor({0,0,0,0}).BgHoveredColor({42,54,78,180}).BgPressedColor({42,54,78,220})
				.ClickSound(res_key::CLICK)
				.Tooltip("Lock Layer", 0.6f)
				.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); s.radius = SDL::FCorners(3.f); })
				.OnClick([this, i]{ _ToggleLayerLock(i); })
				.Id();

			row.Children(slot.btnVis, slot.lblName, slot.btnLock);
			ui.AppendChild(eLayerContent, slot.row);
		}
		sv.Child(eLayerContent);
		panel.Child(sv);

		// Layer operation buttons (move up / move down / add / delete)
		auto mkLayerOpBtn = [&](const char* id, const char* key, const char* tip,
								SDL::Color tint, std::function<void()> cb) -> SDL::ECS::EntityId {
			return ui.Button(id).W(30).H(24).Padding(0.f)
				.Icon(key, 4.f)
				.IconTint({255,255,255,255}, tint, tint)
				.BgColor({0,0,0,0}).BgHoveredColor({42,54,78,200}).BgPressedColor(pal::ACCENT)
				.ClickSound(res_key::CLICK)
				.WithStyle([](auto& s){ s.borders = SDL::FBox(0.f); s.radius = SDL::FCorners(3.f); })
				.Tooltip(tip, 0.6f)
				.OnClick(std::move(cb)).Id();
		};

		panel.Child(
			ui.Row("layer_op_row", 4.f, 4.f)
				.W(SDL::UI::Value::Pw(100.f)).H(30.f)
				.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
				.Children(
					mkLayerOpBtn("btn_lyr_up",  icon_key::UP,        "Move Layer Up",   pal::ACCENT, [this]{ _MoveActiveLayer(-1);    }),
					mkLayerOpBtn("btn_lyr_dn",  icon_key::DOWN,      "Move Layer Down", pal::ACCENT, [this]{ _MoveActiveLayer(+1);    }),
					mkLayerOpBtn("btn_lyr_add", icon_key::LAYER_ADD, "Add Tile Layer",  pal::GREEN,  [this]{ _AddTileLayer();         }),
					mkLayerOpBtn("btn_lyr_del", icon_key::LAYER_DEL, "Delete Layer",    pal::RED,    [this]{ _DeleteActiveLayer();    })
				)
		);

		// Active layer opacity row
		eOpacityLabel = ui.Label("lbl_op_val", "100%")
			.W(34).TextColor(pal::TEXT_DIM).Font(res_key::FONT, 11.f)
			.AlignH(SDL::UI::Align::End);
		eOpacitySlider = ui.Slider<float>("sld_layer_opacity", 0.f, 1.f, 1.f, 0.05f)
			.Grow(100.f).H(18.f)
			.FillColor(pal::ACCENT)
			.OnChange<float>([this](float v){
				if (map.activeLayer < 0 || map.activeLayer >= (int)map.layers.size()) return;
				map.layers[map.activeLayer].opacity = SDL::Clamp(v, 0.f, 1.f);
				map.dirty = true;
				ui.SetText(eOpacityLabel, std::format("{:.0f}%", v * 100.f));
			}).Id();
		panel.Child(
			ui.Column("opacity_sect", 2.f, 0.f)
				.W(SDL::UI::Value::Pw(100.f))
				.WithStyle([](auto& s){
					s.bgColor = SDL::Color(0,0,0,0);
					s.borders = SDL::FBox(1.f,0.f,0.f,0.f);
					s.bdColor = pal::BORDER;
					s.radius  = SDL::FCorners(0.f);
				})
				.Children(
					ui.Label("lbl_op_hdr", "Layer opacity")
						.W(SDL::UI::Value::Pw(100.f))
						.TextColor(pal::TEXT_DIM).Font(res_key::FONT, 11.f)
						.PaddingH(8).PaddingV(2),
					ui.Row("opacity_row", 6.f, 4.f)
						.W(SDL::UI::Value::Pw(100.f)).H(24.f)
						.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
						.Children(eOpacitySlider, eOpacityLabel)
				)
		);

		// Map tile size controls
		eMapTileW = ui.InputValue<int>("input_map_tw", 1, 512, map.tileW, 1)
			.Grow(100.f).H(22.f)
			.OnChange<int>([this](int v){ map.tileW = SDL::Max(1,v); map.dirty=true; })
			.Id();
		eMapTileH = ui.InputValue<int>("input_map_th", 1, 512, map.tileH, 1)
			.Grow(100.f).H(22.f)
			.OnChange<int>([this](int v){ map.tileH = SDL::Max(1,v); map.dirty=true; })
			.Id();
		panel.Child(
			ui.Column("map_tsize_sect", 2.f, 0.f)
				.W(SDL::UI::Value::Pw(100.f))
				.WithStyle([](auto& s){
					s.bgColor = SDL::Color(0,0,0,0);
					s.borders = SDL::FBox(1.f,0.f,0.f,0.f);
					s.bdColor = pal::BORDER;
					s.radius  = SDL::FCorners(0.f);
				})
				.Children(
					ui.Label("lbl_map_tsize_hdr", "Map tile size")
						.W(SDL::UI::Value::Pw(100.f))
						.TextColor(pal::GREY).Font(res_key::FONT, 11.f)
						.PaddingH(6).PaddingV(2),
					ui.Row("map_tsize_row", 4.f, 4.f)
						.W(SDL::UI::Value::Pw(100.f)).H(26.f)
						.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
						.Children(
							ui.Label("lbl_mtW","W").W(14).TextColor(pal::GREY),
							eMapTileW,
							ui.Label("lbl_mtH","H").W(14).TextColor(pal::GREY),
							eMapTileH
						)
				)
		);

		return panel;
	}

	// ── Map canvas (center, grows) ────────────────────────────────────────────

	SDL::ECS::EntityId _BuildMapCanvas() {
		eMapCanvas = ui.Canvas("map_canvas",
			[this](SDL::Event& ev){ _OnMapEvent(ev); },
			nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect){ _RenderMap(r, rect); }
		).Grow(100.f).Padding(0.f).Id();
		return eMapCanvas;
	}

	// ── Tileset panel (right column) ──────────────────────────────────────────

	SDL::ECS::EntityId _BuildTilesetPanel() {
		auto panel = ui.Column("ts_panel", 4.f, 0.f)
			.W(float(kRightW))
			.BgColor(pal::PANEL)
			.WithStyle([](auto& s){
				s.borders   = SDL::FBox(1.f, 0.f, 0.f, 0.f);
				s.bdColor   = pal::BORDER;
				s.radius    = SDL::FCorners(0.f);
			});

		eTilesetName = ui.Label("lbl_ts_name", "No Tileset")
			.TextColor(pal::ACCENT).PaddingH(8).PaddingV(4)
			.BgColor(pal::HEADER)
			.W(SDL::UI::Value::Pw(100.f));
		panel.Child(eTilesetName);

		// Tileset canvas (palette view)
		eTilesetCanvas = ui.Canvas("ts_canvas",
			[this](SDL::Event& ev){ _OnTilesetEvent(ev); },
			nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect){ _RenderTileset(r, rect); }
		).Grow(100.f).Padding(0.f).Id();
		panel.Child(eTilesetCanvas);

		// Tile info
		eTileInfo = ui.Label("lbl_tile_info", "Tile: —")
			.TextColor(pal::GREY).PaddingH(8).PaddingV(2);
		panel.Child(eTileInfo);

		// Tileset tile size controls
		{
			int initW = map.tilesets.empty() ? 16.f : map.tilesets[0].tileW;
			int initH = map.tilesets.empty() ? 16.f : map.tilesets[0].tileH;
			eTsTileW = ui.InputValue<int>("input_ts_tw", 1, 512, initW, 1)
				.Grow(100.f).H(22.f)
				.OnChange<int>([this](int v){
					if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
					auto& ts = map.tilesets[state.activeTileset];
					ts.tileW = SDL::Max(1,v);
					if (ts.imageW > 0) {
						int sp = SDL::Max(ts.spacing, 0);
						ts.columns   = SDL::Max(1, (ts.imageW - 2*ts.margin) / (ts.tileW + (sp ? sp : 1)));
						ts.tileCount = ts.columns * ts.rows;
					}
					map.dirty = true;
				})
				.Id();
			
			eTsTileH = ui.InputValue<int>("input_ts_th", 1, 512, initH, 1)
				.Grow(100.f).H(22.f)
				.OnChange<int>([this](int v){
					if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
					auto& ts = map.tilesets[state.activeTileset];
					ts.tileH = SDL::Max(1, v);
					if (ts.imageH > 0) {
						int sp = SDL::Max(ts.spacing, 0);
						ts.rows      = SDL::Max(1, (ts.imageH - 2*ts.margin) / (ts.tileH + (sp ? sp : 1)));
						ts.tileCount = ts.columns * ts.rows;
					}
					map.dirty = true;
				})
				.Id();
			
			panel.Child(
				ui.Row("ts_tsize_row", 4.f, 4.f)
					.W(SDL::UI::Value::Pw(100.f)).H(26.f)
					.WithStyle([](auto& s){
						s.bgColor = SDL::Color(0,0,0,0);
						s.borders = SDL::FBox(0.f);
					})
					.Children(
						ui.Label("lbl_tstW","Tile W").W(40).TextColor(pal::GREY).Font(res_key::FONT, 11.f),
						eTsTileW,
						ui.Label("lbl_tstH","H").W(10).TextColor(pal::GREY),
						eTsTileH
					)
			);
		}

		// Tileset navigation (prev / next) + zoom controls
		auto mkTsBtn = [&](const char* id, const char* icon, const char* tip, std::function<void()> cb) {
			return ui.Button(id).W(26).H(26)
				.Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
				.Icon(icon, 5.f).ClickSound(res_key::CLICK)
				.Tooltip(tip, 0.6f)
				.WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
				.OnClick(std::move(cb));
		};
		panel.Child(
			ui.Row("ts_nav", 4.f, 4.f)
				.W(SDL::UI::Value::Pw(100.f)).H(30.f)
				.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
				.Children(
					mkTsBtn("btn_ts_prev", icon_key::LEFT,  "Previous Tileset", [this]{
						if (state.activeTileset > 0) --state.activeTileset;
					}),
					mkTsBtn("btn_ts_next", icon_key::RIGHT, "Next Tileset", [this]{
						if (state.activeTileset < (int)map.tilesets.size()-1) ++state.activeTileset;
					}),
					ui.Button("btn_ts_smart", "Smart").W(38).H(26)
						.Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
						.Font(res_key::FONT, 11.f).ClickSound(res_key::CLICK)
						.Tooltip("Toggle Smart Tileset", 0.6f)
						.WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
						.OnClick([this]{
							if (state.activeTileset < (int)map.tilesets.size())
								map.tilesets[state.activeTileset].smart =
									!map.tilesets[state.activeTileset].smart;
						}),
					mkTsBtn("btn_ts_zi", icon_key::ZOOM_IN, "Zoom In (Ctrl+Wheel)", [this]{
						float cx = state.tilesetRect.x + state.tilesetRect.w * 0.5f;
						float cy = state.tilesetRect.y + state.tilesetRect.h * 0.5f;
						_ZoomTileset(1.25f, cx, cy);
					}),
					mkTsBtn("btn_ts_zo", icon_key::ZOOM_OUT, "Zoom Out (Ctrl+Wheel)", [this]{
						float cx = state.tilesetRect.x + state.tilesetRect.w * 0.5f;
						float cy = state.tilesetRect.y + state.tilesetRect.h * 0.5f;
						_ZoomTileset(1.f / 1.25f, cx, cy);
					}),
					ui.Button("btn_ts_fit", "Fit").W(28).H(26)
						.Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
						.Font(res_key::FONT, 11.f).ClickSound(res_key::CLICK)
						.Tooltip("Reset zoom to fit panel", 0.6f)
						.WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
						.OnClick([this]{
							state.tsTileZoom = 0.f;
							state.tsScrollX  = 0.f;
							state.tsScrollY  = 0.f;
						})
				)
		);

		// Import button
		panel.Child(ui.Button("btn_import2", "Import Tileset")
			.W(SDL::UI::Value::Pw(100.f)).H(26)
			.Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
			.WithStyle([](auto& s){ s.radius=SDL::FCorners(0.f); })
			.Font(res_key::FONT, 11.f)
			.ClickSound(res_key::CLICK)
			.OnClick([this]{ _ImportTileset(); }));

		// Brush size row
		panel.Child(
			ui.Row("brush_row", 6.f, 4.f)
				.W(SDL::UI::Value::Pw(100.f)).H(26.f)
				.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
				.Children(
					ui.Label("lbl_brush_sz", "Brush:").W(44).TextColor(pal::TEXT_DIM),
					ui.Slider<int>("sld_brush", 1, 9, state.brushSize, 1)
						.Grow(100.f)
						.FillColor(pal::ACCENT)
						.OnChange<int>([this](int v){
							state.brushSize = SDL::Clamp(v, 1, 9);
						})
				)
		);

		// ── Object properties section (shown when an object is selected) ───
		panel.Child(_BuildPropertiesSection());

		return panel;
	}

	// ── Object properties section (right panel bottom) ─────────────────────────
	SDL::ECS::EntityId _BuildPropertiesSection() {
		SDL::UI::Style sectionHdrStyle;
		sectionHdrStyle.bgColor   = pal::SURFACE;
		sectionHdrStyle.textColor = pal::ACCENT;
		sectionHdrStyle.fontKey   = res_key::FONT;
		sectionHdrStyle.fontSize  = 11.f;

		auto smallLbl = [&](const char* id, const char* text, float w) {
			return ui.Label(id, text)
				.W(w).TextColor(pal::TEXT_DIM).Font(res_key::FONT, 11.f);
		};
		auto numInput = [&](const char* id, float init) {
			return ui.InputValue<float>(id, -100000.f, 100000.f, init, 1.f)
				.Grow(100.f).H(20.f).Font(res_key::FONT, 11.f);
		};

		ePropsHdr = ui.Label("lbl_props_hdr", "PROPERTIES")
			.Style(sectionHdrStyle).PaddingH(8).PaddingV(3)
			.W(SDL::UI::Value::Pw(100.f));

		ePropsTypeLbl = ui.Label("lbl_props_type", "—")
			.TextColor(pal::ORANGE).Font(res_key::FONT, 10.f)
			.W(SDL::UI::Value::Pw(100.f)).PaddingH(8).PaddingV(0);

		ePropsName = ui.InputValue<float>("inp_props_name_dummy", -1000.f, 1000.f, 0.f, 1.f)
			.Grow(100.f).H(20.f); // placeholder; name shown via type label for now

		ePropsX = numInput("inp_obj_x", 0)
			.OnChange<float>([this](float v){ if (auto* o = _SelObj()) { o->x = v; map.dirty=true; } })
			.Id();
		ePropsY = numInput("inp_obj_y", 0)
			.OnChange<float>([this](float v){ if (auto* o = _SelObj()) { o->y = v; map.dirty=true; } })
			.Id();
		ePropsW = numInput("inp_obj_w", 32)
			.OnChange<float>([this](float v){ if (auto* o = _SelObj()) { o->w = SDL::Max(1.f,v); map.dirty=true; } })
			.Id();
		ePropsH = numInput("inp_obj_h", 32)
			.OnChange<float>([this](float v){ if (auto* o = _SelObj()) { o->h = SDL::Max(1.f,v); map.dirty=true; } })
			.Id();
		ePropsRot = ui.InputValue<float>("inp_obj_rot", -360.f, 360.f, 0.f, 1.f)
			.Grow(100.f).H(20.f).Font(res_key::FONT, 11.f)
			.OnChange<float>([this](float v){ if (auto* o = _SelObj()) { o->rotation = v; map.dirty=true; } })
			.Id();

		ePropsBody = ui.Column("props_body", 4.f, 0.f)
			.W(SDL::UI::Value::Pw(100.f))
			.WithStyle([](auto& s){
				s.bgColor = SDL::Color(0,0,0,0);
				s.borders = SDL::FBox(0.f);
			})
			.Children(
				ePropsTypeLbl,
				ui.Row("rprop_xy", 4.f, 4.f).W(SDL::UI::Value::Pw(100.f)).H(22.f)
					.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
					.Children(smallLbl("lblX","X",10), ePropsX, smallLbl("lblY","Y",10), ePropsY),
				ui.Row("rprop_wh", 4.f, 4.f).W(SDL::UI::Value::Pw(100.f)).H(22.f)
					.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
					.Children(smallLbl("lblW","W",10), ePropsW, smallLbl("lblH","H",10), ePropsH),
				ui.Row("rprop_rot", 4.f, 4.f).W(SDL::UI::Value::Pw(100.f)).H(22.f)
					.WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
					.Children(smallLbl("lblR","Rot",24), ePropsRot),
				ui.Button("btn_obj_del", "Delete object").W(SDL::UI::Value::Pw(100.f)).H(22.f)
					.BgColor(SDL::Color{120,40,46,255}).BgHoveredColor(SDL::Color{160,52,60,255})
					.TextColor(pal::WHITE).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.radius=SDL::FCorners(4.f); s.borders=SDL::FBox(0.f); })
					.ClickSound(res_key::CLICK)
					.OnClick([this]{ _DeleteSelectedObject(); })
			);

		return ui.Column("props_section", 0.f, 0.f)
			.W(SDL::UI::Value::Pw(100.f))
			.WithStyle([](auto& s){
				s.bgColor = SDL::Color(0,0,0,0);
				s.borders = SDL::FBox(1.f,0.f,0.f,0.f);
				s.bdColor = pal::BORDER;
				s.radius  = SDL::FCorners(0.f);
			})
			.Children(ePropsHdr, ePropsBody);
	}

	ObjectDef* _SelObj() {
		if (state.selectedObjLayer < 0 ||
		    state.selectedObjLayer >= (int)map.layers.size()) return nullptr;
		auto& L = map.layers[state.selectedObjLayer];
		if (L.type != LayerType::Object) return nullptr;
		for (auto& o : L.objects)
			if (o.id == state.selectedObjId) return &o;
		return nullptr;
	}

	void _UpdatePropertiesPanel() {
		ObjectDef* o = _SelObj();
		// Avoid pushing values back into widgets we're not changing
		if (o) {
			if (m_lastSelObjId != o->id) {
				m_lastSelObjId = o->id;
				ui.SetValue(ePropsX,   o->x);
				ui.SetValue(ePropsY,   o->y);
				ui.SetValue(ePropsW,   o->w);
				ui.SetValue(ePropsH,   o->h);
				ui.SetValue(ePropsRot, o->rotation);
			}
			const char* tname = "Rect";
			switch (o->type) {
				case ObjectType::Rect:    tname = "Rectangle";  break;
				case ObjectType::Ellipse: tname = "Ellipse";    break;
				case ObjectType::Point:   tname = "Point";      break;
				case ObjectType::Polygon: tname = "Polygon";    break;
				case ObjectType::Tile:    tname = "Tile";       break;
				case ObjectType::MapLink: tname = "Map Link";   break;
			}
			ui.SetText(ePropsTypeLbl,
				std::format("{} #{}  '{}'", tname, o->id, o->name));
			ui.SetVisible(ePropsBody, true);
		} else {
			if (m_lastSelObjId != -1) {
				m_lastSelObjId = -1;
				ui.SetText(ePropsTypeLbl, "No object selected");
			}
			ui.SetVisible(ePropsBody, false);
		}
	}

	void _DeleteSelectedObject() {
		ObjectDef* o = _SelObj();
		if (!o) return;
		auto& L = map.layers[state.selectedObjLayer];
		L.objects.erase(std::remove_if(L.objects.begin(), L.objects.end(),
			[id=o->id](const ObjectDef& x){ return x.id == id; }), L.objects.end());
		state.selectedObjId    = -1;
		state.selectedObjLayer = -1;
		map.dirty = true;
	}

	void _FinishPolygon() {
		if (state.tool != ToolType::ObjPolygon || state.polyPoints.size() < 2) return;
		if (!_ActiveLayerIsObject()) { state.polyPoints.clear(); return; }
		auto& L = map.layers[map.activeLayer];
		if (L.locked) { state.polyPoints.clear(); return; }
		ObjectDef obj;
		obj.id     = state.nextObjId++;
		obj.name   = std::format("Polygon{}", obj.id);
		obj.type   = ObjectType::Polygon;
		obj.x      = state.polyOrigin.x;
		obj.y      = state.polyOrigin.y;
		obj.points = state.polyPoints;
		// Bounding box
		float minX=obj.points[0].x, maxX=minX, minY=obj.points[0].y, maxY=minY;
		for (auto& p : obj.points) {
			minX = SDL::Min(minX, p.x); maxX = SDL::Max(maxX, p.x);
			minY = SDL::Min(minY, p.y); maxY = SDL::Max(maxY, p.y);
		}
		obj.w = maxX - minX;
		obj.h = maxY - minY;
		L.objects.push_back(std::move(obj));
		state.polyPoints.clear();
		map.dirty = true;
	}

	// =========================================================================
	// Workspace builders
	// =========================================================================

	// ── Scripts workspace ────────────────────────────────────────────────────
	//
	// Two columns: on the left a panel with the list of script docs + new/run
	// buttons; on the right the text area editor + console.
	SDL::ECS::EntityId _BuildScriptsWorkspace() {
		using namespace SDL::UI;

		// Left side panel: scripts list (simple buttons for now)
		auto leftSide = ui.Column("ws_scripts_side", 0.f, 0.f)
			.W(220.f).BgColor(pal::PANEL)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f, 0.f, 0.f, 1.f);
				s.bdColor = pal::BORDER;
				s.radius  = SDL::FCorners(0.f);
			});
		leftSide.Child(ui.Label("ws_scr_hdr", "Scripts")
			.TextColor(pal::ACCENT).PaddingH(8).PaddingV(4)
			.BgColor(pal::HEADER).W(Value::Pw(100.f)));
		leftSide.Child(ui.Button("ws_scr_new", "+ New script")
			.W(Value::Pw(100.f)).H(26.f).Font(res_key::FONT, 11.f)
			.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
			.TextColor(pal::TEXT)
			.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(0.f); })
			.ClickSound(res_key::CLICK)
			.OnClick([this]{
				ScriptDoc d;
				d.name = std::format("script_{}.lua", project.scripts.size()+1);
				d.code = "-- New script\n";
				project.scripts.push_back(std::move(d));
				project.activeScript = (int)project.scripts.size()-1;
				_OnTabActivated(WsScripts);
			}));
		leftSide.Child(ui.Button("ws_scr_run", "▶ Run")
			.W(Value::Pw(100.f)).H(28.f).Font(res_key::FONT, 12.f)
			.BgColor(pal::ACCENT).BgHoveredColor(pal::ACCENT2)
			.BgPressedColor(pal::ACCENT3).TextColor(pal::WHITE)
			.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
			.ClickSound(res_key::CLICK)
			.OnClick([this]{ _LuaRunActiveScript(); }));
		leftSide.Child(ui.Button("ws_scr_clear", "Clear console")
			.W(Value::Pw(100.f)).H(22.f).Font(res_key::FONT, 10.f)
			.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
			.TextColor(pal::TEXT_DIM)
			.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(0.f); })
			.OnClick([this]{
				m_consoleLog.clear();
				if (eScriptCons != SDL::ECS::NullEntity)
					ui.SetText(eScriptCons, "");
			}));

		// Editor column: name + textarea + console
		eScriptName = ui.Label("ws_scr_name",
			std::format("Script: {}",
				project.scripts.empty() ? "—" : project.scripts[0].name))
			.TextColor(pal::ACCENT).PaddingH(8).PaddingV(4)
			.BgColor(pal::HEADER).W(Value::Pw(100.f));

		eScriptArea = ui.TextArea("ws_scr_area",
			project.scripts.empty() ? "" : project.scripts[0].code)
			.W(Value::Pw(100.f)).Grow(100.f)
			.BgColor(pal::SURFACE).TextColor(pal::TEXT)
			.Font(res_key::FONT, 12.f).PaddingH(8).PaddingV(6)
			.OnTextChange([this](const std::string& txt){
				if (project.scripts.empty()) return;
				int i = std::clamp(project.activeScript, 0, (int)project.scripts.size()-1);
				project.scripts[i].code = txt;
			}).Id();

		eScriptCons = ui.TextArea("ws_scr_console", "")
			.W(Value::Pw(100.f)).H(140.f)
			.BgColor({10, 12, 18, 255}).TextColor(pal::GREEN)
			.Font(res_key::FONT, 11.f).PaddingH(8).PaddingV(6)
			.ReadOnly(true).Id();

		auto editorCol = ui.Column("ws_scr_editor", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){ s.bgColor = pal::BG; s.borders=SDL::FBox(0.f); })
			.Children(eScriptName, eScriptArea,
				ui.Label("ws_scr_clbl", "Console")
					.TextColor(pal::TEXT_DIM).PaddingH(8).PaddingV(2)
					.BgColor(pal::HEADER).W(Value::Pw(100.f)),
				eScriptCons);

		return ui.Row("ws_scripts", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){ s.bgColor = pal::BG; s.borders=SDL::FBox(0.f); })
			.Children(leftSide, editorCol);
	}

	// ── Cinematic workspace ─────────────────────────────────────────────────
	//
	// Top bar with playhead controls + the timeline canvas drawn manually
	// (track headers on the left, clip rectangles on the right).
	SDL::ECS::EntityId _BuildCinematicWorkspace() {
		using namespace SDL::UI;

		eCinePlayhead = ui.Label("ws_cine_time", "0.00s / 10.00s")
			.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
			.W(Value::Auto()).PaddingH(8).PaddingV(2);

		auto controls = ui.Row("ws_cine_ctrl", 6.f, 4.f)
			.W(Value::Pw(100.f)).H(34.f).PaddingH(8.f).PaddingV(4.f)
			.BgColor(pal::HEADER)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f, 0.f, 1.f, 0.f);
				s.bdColor = pal::BORDER;
			})
			.Children(
				ui.Button("ws_cine_play", "▶ Play")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::ACCENT).BgHoveredColor(pal::ACCENT2)
					.TextColor(pal::WHITE).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ m_cinePlaying = !m_cinePlaying; }),
				ui.Button("ws_cine_stop", "⏹ Stop")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
					.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ m_cinePlaying = false; m_cineTime = 0.f; }),
				ui.Button("ws_cine_add_img", "+ Image")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
					.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ _CineAddTrack(CineTrackKind::Image); }),
				ui.Button("ws_cine_add_mus", "+ Music")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
					.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ _CineAddTrack(CineTrackKind::Music); }),
				ui.Button("ws_cine_add_sfx", "+ Sfx")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
					.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ _CineAddTrack(CineTrackKind::Sfx); }),
				ui.Button("ws_cine_add_dlg", "+ Dialog")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
					.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ _CineAddTrack(CineTrackKind::Dialog); }),
				ui.Container("ws_cine_sp").Grow(100.f).BgColor({0,0,0,0})
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); }),
				eCinePlayhead
			);

		eCineCanvas = ui.Canvas("ws_cine_canvas",
			[this](SDL::Event& ev){ _OnCineEvent(ev); },
			nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect){ _RenderCine(r, rect); }
		).Grow(100.f).Padding(0.f).Id();

		return ui.Column("ws_cine", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){ s.bgColor = pal::BG; s.borders=SDL::FBox(0.f); })
			.Children(controls, eCineCanvas);
	}

	// ── NodeGraph workspace ─────────────────────────────────────────────────
	//
	// Toolbar with "Add node" buttons + a canvas. The canvas pans (middle
	// mouse), zooms (wheel), and supports drag-to-move-node and drag-wire
	// from an output port to an input port.
	SDL::ECS::EntityId _BuildNodeGraphWorkspace() {
		using namespace SDL::UI;

		auto addBtn = [&](const char* id, const char* lbl, NodeKind k) {
			return ui.Button(id, lbl)
				.W(Value::Auto()).H(26.f).PaddingH(10.f)
				.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
				.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
				.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
				.OnClick([this, k]{ _GraphAddNode(k); });
		};

		eGraphStatus = ui.Label("ws_graph_status", "Drag from output → input to wire")
			.TextColor(pal::TEXT_DIM).Font(res_key::FONT, 11.f)
			.W(Value::Auto()).PaddingH(8);

		auto controls = ui.Row("ws_graph_ctrl", 6.f, 4.f)
			.W(Value::Pw(100.f)).H(34.f).PaddingH(8.f).PaddingV(4.f)
			.BgColor(pal::HEADER)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f, 0.f, 1.f, 0.f);
				s.bdColor = pal::BORDER;
			})
			.Children(
				addBtn("ws_graph_add_evt", "+ Event",     NodeKind::Event),
				addBtn("ws_graph_add_scr", "+ Script",    NodeKind::Script),
				addBtn("ws_graph_add_dlg", "+ Dialog",    NodeKind::Dialog),
				addBtn("ws_graph_add_cin", "+ Cinematic", NodeKind::Cinematic),
				addBtn("ws_graph_add_wt",  "+ Wait",      NodeKind::Wait),
				addBtn("ws_graph_add_br",  "+ Branch",    NodeKind::Branch),
				ui.Button("ws_graph_run", "▶ Run async")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::ACCENT).BgHoveredColor(pal::ACCENT2)
					.BgPressedColor(pal::ACCENT3).TextColor(pal::WHITE)
					.Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ _GraphRunAsync(); }),
				ui.Container("ws_graph_sp").Grow(100.f).BgColor({0,0,0,0})
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); }),
				eGraphStatus
			);

		eGraphCanvas = ui.Canvas("ws_graph_canvas",
			[this](SDL::Event& ev){ _OnGraphEvent(ev); },
			nullptr,
			[this](SDL::RendererRef r, SDL::FRect rect){ _RenderGraph(r, rect); }
		).Grow(100.f).Padding(0.f).Id();

		return ui.Column("ws_graph", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){ s.bgColor = pal::BG; s.borders=SDL::FBox(0.f); })
			.Children(controls, eGraphCanvas);
	}

	// ── Test workspace ──────────────────────────────────────────────────────
	//
	// Plays the active map with simple AABB physics + gravity + jump. The
	// "collision layer" property of object-layer rectangles marks them as
	// solid walls; tile layers also act as solid ground per non-empty tile.
	SDL::ECS::EntityId _BuildTestWorkspace() {
		using namespace SDL::UI;

		eTestStatus = ui.Label("ws_test_status",
			"Click in the canvas, then use WASD/arrows + Space to jump")
			.TextColor(pal::TEXT_DIM).Font(res_key::FONT, 11.f)
			.W(Value::Auto()).PaddingH(8);

		auto controls = ui.Row("ws_test_ctrl", 6.f, 4.f)
			.W(Value::Pw(100.f)).H(34.f).PaddingH(8.f).PaddingV(4.f)
			.BgColor(pal::HEADER)
			.WithStyle([](auto& s){
				s.borders = SDL::FBox(0.f, 0.f, 1.f, 0.f);
				s.bdColor = pal::BORDER;
			})
			.Children(
				ui.Button("ws_test_play", "▶ Play")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::ACCENT).BgHoveredColor(pal::ACCENT2)
					.TextColor(pal::WHITE).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ m_testPlaying = !m_testPlaying; }),
				ui.Button("ws_test_reset", "↺ Reset")
					.W(Value::Auto()).H(26.f).PaddingH(10.f)
					.BgColor(pal::NEUTRAL).BgHoveredColor(pal::NEUTRAL2)
					.TextColor(pal::TEXT).Font(res_key::FONT, 11.f)
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); s.radius=SDL::FCorners(4.f); })
					.OnClick([this]{ _TestReset(); }),
				ui.Container("ws_test_sp").Grow(100.f).BgColor({0,0,0,0})
					.WithStyle([](auto& s){ s.borders=SDL::FBox(0.f); }),
				eTestStatus
			);

		eTestCanvas = ui.Canvas("ws_test_canvas",
			[this](SDL::Event& ev){ _OnTestEvent(ev); },
			[this](float dt){ _TestUpdate(dt); },
			[this](SDL::RendererRef r, SDL::FRect rect){ _RenderTest(r, rect); }
		).Grow(100.f).Padding(0.f).Id();

		return ui.Column("ws_test", 0.f, 0.f)
			.Grow(100.f)
			.WithStyle([](auto& s){ s.bgColor = pal::BG; s.borders=SDL::FBox(0.f); })
			.Children(controls, eTestCanvas);
	}

	// ── Status bar ────────────────────────────────────────────────────────────

	SDL::ECS::EntityId _BuildStatusBar() {
		auto bar = ui.Row("status_bar", 16.f, 0.f)
			.W(SDL::UI::Value::Ww(100.f)).H(22.f)
			.PaddingH(10.f).PaddingV(2.f)
			.BgColor(pal::HEADER)
			.WithStyle([](auto& s){
				s.borders     = SDL::FBox(1.f, 0.f, 0.f, 0.f);
				s.bdColor = pal::BORDER;
				s.radius      = SDL::FCorners(0.f);
			});
		eStatusLabel = ui.Label("lbl_status", "Ready").TextColor(pal::GREY);
		bar.Child(eStatusLabel);
		return bar;
	}

	// =========================================================================
	// Coordinate transforms
	// =========================================================================

	SDL::FPoint WorldToScreen(float wx, float wy) const {
		return { state.mapRect.x + (wx - state.viewX) * state.zoom,
				 state.mapRect.y + (wy - state.viewY) * state.zoom };
	}
	SDL::FPoint ScreenToWorld(float sx, float sy) const {
		return { state.viewX + (sx - state.mapRect.x) / state.zoom,
				 state.viewY + (sy - state.mapRect.y) / state.zoom };
	}
	void ScreenToTile(float sx, float sy, int& tx, int& ty) const {
		auto [wx, wy] = ScreenToWorld(sx, sy);
		map.WorldToTile(wx, wy, tx, ty);
	}
	// Inverse — useful for highlight/preview rendering under any orientation.
	SDL::FPoint TileToScreen(int tx, int ty) const {
		auto wp = map.TileToWorld(tx, ty);
		return WorldToScreen(wp.x, wp.y);
	}

	// =========================================================================
	// Map canvas rendering
	// =========================================================================

	void _RenderMap(SDL::RendererRef r, SDL::FRect rect) {
		state.mapRect = rect;

		// Background
		r.SetDrawColor({22, 22, 34, 255});
		r.RenderFillRect(rect);

		const float tw = map.tileW * state.zoom;
		const float th = map.tileH * state.zoom;
		const float ox = rect.x + std::fmod(-(state.viewX * state.zoom), tw);
		const float oy = rect.y + std::fmod(-(state.viewY * state.zoom), th);

		// Checkerboard for transparent area
		for (int row = -1; row <= (int)(rect.h / th) + 1; ++row)
		for (int col = -1; col <= (int)(rect.w / tw) + 1; ++col) {
			if ((row + col) % 2 != 0) continue;
			r.SetDrawColor({28, 30, 42, 255});
			r.RenderFillRect(SDL::FRect{ox + col * tw, oy + row * th, tw, th});
		}

		// Map boundary (bounded maps only)
		if (!map.infinite) {
			auto tl = WorldToScreen(0.f, 0.f);
			auto br = WorldToScreen(float(map.width  * map.tileW),
									float(map.height * map.tileH));
			r.SetDrawColor({55, 60, 95, 200});
			r.RenderRect(SDL::FRect{tl.x, tl.y, br.x - tl.x, br.y - tl.y});
		}

		// Render each visible layer
		for (int li = 0; li < (int)map.layers.size(); ++li) {
			const auto& layer = map.layers[li];
			if (!layer.visible) continue;

			if (layer.type == LayerType::Tile) {
				// Only iterate the visible tile range (works for both bounded and infinite)
				int vx0 = (int)std::floor(state.viewX / map.tileW) - 1;
				int vy0 = (int)std::floor(state.viewY / map.tileH) - 1;
				int vx1 = (int)std::ceil((state.viewX + rect.w / state.zoom) / map.tileW) + 1;
				int vy1 = (int)std::ceil((state.viewY + rect.h / state.zoom) / map.tileH) + 1;
				if (!map.infinite) {
					vx0 = SDL::Max(vx0, 0); vy0 = SDL::Max(vy0, 0);
					vx1 = SDL::Min(vx1, map.width); vy1 = SDL::Min(vy1, map.height);
				}
				for (int ty = vy0; ty < vy1; ++ty)
				for (int tx = vx0; tx < vx1; ++tx) {
					TileID tid = map.GetTile(li, tx, ty);
					if (tid == EMPTY_TILE) continue;
					const TilesetDef* ts = map.FindTileset(tid);
					if (!ts || ts->key.empty()) continue;
					auto texH = pool_ui.Get<SDL::Texture>(ts->key);
					if (!texH) continue;

					// Tile animation playback: if this tile has frames, swap to
					// the active frame's local ID based on the editor anim clock.
					TileID renderId = tid;
					{
						int local = (int)(tid - ts->firstGid);
						if (const TileMetadata* m = ts->MetaFor(local)) {
							if (!m->anim.empty()) {
								int totalMs = 0;
								for (const auto& f : m->anim)
									totalMs += SDL::Max(1, f.durationMs);
								if (totalMs > 0) {
									int t = (int)(state.animTime * 1000.f) % totalMs;
									int acc = 0;
									for (const auto& f : m->anim) {
										acc += SDL::Max(1, f.durationMs);
										if (t < acc) {
											renderId = (TileID)(ts->firstGid + f.localId);
											break;
										}
									}
								}
							}
						}
					}

					auto src = map.TileSrcRect(*ts, renderId);
					auto wp  = map.TileToWorld(tx, ty);
					auto p   = WorldToScreen(wp.x, wp.y);
					SDL::FRect dst{p.x, p.y, tw, th};

					SDL::TextureRef tex{*texH};
					if (layer.opacity < 1.f) tex.SetAlphaModFloat(layer.opacity);
					r.RenderTexture(tex, src, dst);
					if (layer.opacity < 1.f) tex.SetAlphaModFloat(1.f);
				}
			} else {
				// Object layer — per-type color, filled fill + outline, selection halo
				for (const auto& obj : layer.objects) {
					auto p  = WorldToScreen(obj.x, obj.y);
					float dw = obj.w * state.zoom;
					float dh = obj.h * state.zoom;
					SDL::FRect dr{p.x, p.y, dw, dh};

					SDL::Color typeC;
					switch (obj.type) {
						case ObjectType::Rect:    typeC = pal::OBJ_RECT;  break;
						case ObjectType::Ellipse: typeC = pal::OBJ_ELLIP; break;
						case ObjectType::Polygon: typeC = pal::OBJ_POLY;  break;
						case ObjectType::Point:   typeC = pal::OBJ_POINT; break;
						case ObjectType::Tile:    typeC = pal::OBJ_COL;   break;
						case ObjectType::MapLink: typeC = {200, 120, 240, 220}; break;
					}
					SDL::Color fill   = {typeC.r, typeC.g, typeC.b, (Uint8)(80 * layer.opacity)};
					SDL::Color stroke = obj.selected ? pal::OBJ_SEL : typeC;

					if (obj.type == ObjectType::Rect || obj.type == ObjectType::Tile) {
						r.SetDrawColor(fill);   r.RenderFillRect(dr);
						r.SetDrawColor(stroke); r.RenderRect(dr);
						if (obj.selected) {
							SDL::FRect halo{dr.x-2, dr.y-2, dr.w+4, dr.h+4};
							r.SetDrawColor(pal::OBJ_SEL); r.RenderRect(halo);
						}
					} else if (obj.type == ObjectType::Ellipse) {
						float cx = dr.x + dr.w * .5f, cy = dr.y + dr.h * .5f;
						float rx = dr.w * .5f,         ry = dr.h * .5f;
						constexpr int SEG = 48;
						std::vector<SDL::FPoint> pts(SEG + 1);
						for (int i = 0; i <= SEG; ++i) {
							float a = float(i) / SEG * 2.f * SDL::PI_F;
							pts[i] = {cx + std::cos(a) * rx, cy + std::sin(a) * ry};
						}
						r.SetDrawColor(stroke); r.RenderLines(pts);
						if (obj.selected) {
							r.SetDrawColor(pal::OBJ_SEL);
							r.RenderRect(dr);
						}
					} else if (obj.type == ObjectType::Point) {
						float rad = 6.f;
						r.SetDrawColor(fill);
						r.RenderFillCircle(p, rad);
						r.SetDrawColor(stroke);
						r.RenderCircle(p, rad);
						if (obj.selected) {
							r.SetDrawColor(pal::OBJ_SEL);
							r.RenderCircle(p, rad + 3);
						}
					} else if (obj.type == ObjectType::Polygon && obj.points.size() >= 2) {
						std::vector<SDL::FPoint> pts;
						for (auto& pt : obj.points)
							pts.push_back(WorldToScreen(obj.x + pt.x, obj.y + pt.y));
						pts.push_back(pts.front());
						r.SetDrawColor(stroke); r.RenderLines(pts);
						// vertex markers
						for (size_t i = 0; i + 1 < pts.size(); ++i) {
							r.SetDrawColor(stroke);
							r.RenderFillRect(SDL::FRect{pts[i].x-2, pts[i].y-2, 4, 4});
						}
						if (obj.selected) {
							SDL::FRect bb{p.x, p.y, dw, dh};
							r.SetDrawColor(pal::OBJ_SEL);
							r.RenderRect(bb);
						}
					}
				}
			}
		}

		// Grid
		if (state.showGrid && state.zoom > 0.15f) {
			r.SetDrawColor(pal::GRID);
			float sx = ox; while (sx > rect.x) sx -= tw;
			float sy = oy; while (sy > rect.y) sy -= th;
			for (float x = sx; x <= rect.x + rect.w; x += tw)
				r.RenderLine({x, rect.y}, {x, rect.y + rect.h});
			for (float y = sy; y <= rect.y + rect.h; y += th)
				r.RenderLine({rect.x, y}, {rect.x + rect.w, y});
		}

		// Cursor tile highlight + tool-specific preview
		float mx, my;
		SDL::GetMouseState(mx, my);
		if (mx >= rect.x && mx < rect.x + rect.w &&
			my >= rect.y && my < rect.y + rect.h) {
			int ecs_context, cty;
			ScreenToTile(mx, my, ecs_context, cty);

			if (state.tool == ToolType::Brush) {
				int half = state.brushSize / 2;
				for (int dy = -half; dy <= half; ++dy)
				for (int dx = -half; dx <= half; ++dx) {
					int bx = ecs_context + dx, by = cty + dy;
					if (!map.infinite && (bx < 0 || by < 0 || bx >= map.width || by >= map.height)) continue;
					auto bp = TileToScreen(bx, by);
					r.SetDrawColor({100, 180, 255, 55});
					r.RenderFillRect(SDL::FRect{bp.x, bp.y, tw, th});
				}
			}

			// Pencil / Fill: preview the NxM stamp footprint (multi-tile)
			if ((state.tool == ToolType::Pencil || state.tool == ToolType::Fill) &&
			    (state.selTileW > 1 || state.selTileH > 1)) {
				for (int dy = 0; dy < state.selTileH; ++dy)
				for (int dx = 0; dx < state.selTileW; ++dx) {
					int bx = ecs_context + dx, by = cty + dy;
					if (!map.infinite && (bx < 0 || by < 0 || bx >= map.width || by >= map.height)) continue;
					auto bp = TileToScreen(bx, by);
					r.SetDrawColor({120, 200, 255, 65});
					r.RenderFillRect(SDL::FRect{bp.x, bp.y, tw, th});
				}
				auto bp = TileToScreen(ecs_context, cty);
				r.SetDrawColor({120, 200, 255, 220});
				r.RenderRect(SDL::FRect{bp.x, bp.y,
				                        state.selTileW * tw, state.selTileH * th});
			}

			if (map.infinite || (ecs_context >= 0 && cty >= 0 && ecs_context < map.width && cty < map.height)) {
				auto cp = TileToScreen(ecs_context, cty);
				r.SetDrawColor({255, 255, 100, 75});
				r.RenderFillRect(SDL::FRect{cp.x, cp.y, tw, th});
				r.SetDrawColor({255, 255, 100, 190});
				r.RenderRect(SDL::FRect{cp.x, cp.y, tw, th});
			}
		}

		// Map selection rectangle
		if (state.hasMapSel && state.selW > 0 && state.selH > 0) {
			auto tl = TileToScreen(state.selX, state.selY);
			float sw = float(state.selW) * tw, sh = float(state.selH) * th;
			r.SetDrawColor({255, 200, 50, 45}); r.RenderFillRect(SDL::FRect{tl.x, tl.y, sw, sh});
			r.SetDrawColor({255, 200, 50, 220}); r.RenderRect(SDL::FRect{tl.x, tl.y, sw, sh});
		}

		// Object drag preview (rectangle / ellipse creation)
		if (state.objDrag) {
			SDL::GetMouseState(mx, my);
			float ox2 = SDL::Min(state.objStart.x, mx);
			float oy2 = SDL::Min(state.objStart.y, my);
			float ow  = SDL::Abs(mx - state.objStart.x);
			float oh  = SDL::Abs(my - state.objStart.y);
			SDL::Color preview = state.tool == ToolType::ObjEllipse
				? pal::OBJ_ELLIP : pal::OBJ_RECT;
			r.SetDrawColor({preview.r, preview.g, preview.b, 60});
			r.RenderFillRect(SDL::FRect{ox2, oy2, ow, oh});
			r.SetDrawColor(preview);
			r.RenderRect(SDL::FRect{ox2, oy2, ow, oh});
		}

		// Polygon-in-progress preview
		if (state.tool == ToolType::ObjPolygon && !state.polyPoints.empty()) {
			SDL::GetMouseState(mx, my);
			auto [wx, wy] = ScreenToWorld(mx, my);
			std::vector<SDL::FPoint> pts;
			for (auto& p : state.polyPoints)
				pts.push_back(WorldToScreen(state.polyOrigin.x + p.x,
				                            state.polyOrigin.y + p.y));
			pts.push_back({mx, my}); // live tail at cursor
			if (pts.size() >= 2) {
				r.SetDrawColor(pal::OBJ_POLY);
				r.RenderLines(pts);
			}
			for (auto& sp : pts) {
				r.SetDrawColor(pal::OBJ_POLY);
				r.RenderFillRect(SDL::FRect{sp.x-3, sp.y-3, 6, 6});
			}
			(void)wx; (void)wy;
		}
	}

	// =========================================================================
	// Tileset palette rendering
	// =========================================================================

	void _RenderTileset(SDL::RendererRef r, SDL::FRect rect) {
		state.tilesetRect = rect;
		r.SetDrawColor({20, 22, 32, 255});
		r.RenderFillRect(rect);

		if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size())
			return;
		const auto& ts = map.tilesets[state.activeTileset];
		if (ts.key.empty()) return;
		auto texH = pool_ui.Get<SDL::Texture>(ts.key);
		if (!texH) return;
		if (ts.imageW <= 0) return;

		// Auto-fit on first render of this tileset (tsTileZoom == 0)
		if (state.tsTileZoom <= 0.f)
			state.tsTileZoom = SDL::Min(1.f, (rect.w - 4.f) / float(ts.imageW));
		state.tsScale = state.tsTileZoom;

		float dispW = ts.imageW * state.tsScale;
		float dispH = ts.imageH * state.tsScale;
		float imgX  = rect.x + 2.f - state.tsScrollX;
		float imgY  = rect.y + 2.f - state.tsScrollY;

		float tw  = ts.tileW   * state.tsScale;
		float th  = ts.tileH   * state.tsScale;
		float spH = ts.spacing * state.tsScale;
		float spV = spH;

		// Clip all rendering to the panel rectangle
		r.SetClipRect(SDL::Rect{(int)rect.x, (int)rect.y, (int)rect.w, (int)rect.h});

		// Draw image
		r.RenderTexture(SDL::TextureRef{*texH}, {},
						SDL::FRect{imgX, imgY, dispW, dispH});

		// Grid overlay
		if (tw > 3.f) {
			r.SetDrawColor(pal::GRID);
			for (int c = 0; c <= ts.columns; ++c)
				r.RenderLine({imgX + c*(tw+spH), imgY},
							 {imgX + c*(tw+spH), imgY + dispH});
			for (int rr = 0; rr <= ts.rows; ++rr)
				r.RenderLine({imgX,         imgY + rr*(th+spV)},
							 {imgX + dispW, imgY + rr*(th+spV)});
		}

		// Hover highlight
		float mx, my;
		SDL::GetMouseState(mx, my);
		if (ts.columns > 0 && ts.rows > 0) {
			int hx = (int)std::floor((mx - imgX) / (tw + spH));
			int hy = (int)std::floor((my - imgY) / (th + spV));
			if (hx >= 0 && hy >= 0 && hx < ts.columns && hy < ts.rows &&
				mx >= rect.x && mx < rect.x + rect.w &&
				my >= rect.y && my < rect.y + rect.h) {
				float px = imgX + hx*(tw+spH), py = imgY + hy*(th+spV);
				r.SetDrawColor({255, 255, 100, 75});
				r.RenderFillRect(SDL::FRect{px, py, tw, th});
			}
		}

		// Selection highlight
		{
			float px = imgX + state.selTileX * (tw + spH);
			float py = imgY + state.selTileY * (th + spV);
			float sw = float(state.selTileW) * (tw + spH) - spH;
			float sh = float(state.selTileH) * (th + spV) - spV;
			r.SetDrawColor({255, 200, 50, 55});
			r.RenderFillRect(SDL::FRect{px, py, sw, sh});
			r.SetDrawColor(pal::SELECTED);
			r.RenderRect(SDL::FRect{px, py, sw, sh});
		}

		r.ResetClipRect();

		// Clamp scroll to content bounds
		float maxScrollX = SDL::Max(0.f, dispW + 4.f - rect.w);
		float maxScrollY = SDL::Max(0.f, dispH + 4.f - rect.h);
		state.tsScrollX = SDL::Clamp(state.tsScrollX, 0.f, maxScrollX);
		state.tsScrollY = SDL::Clamp(state.tsScrollY, 0.f, maxScrollY);
	}

	// =========================================================================
	// Map canvas events
	// =========================================================================

	void _OnMapEvent(SDL::Event& ev) {
		if (ev.type == SDL::EVENT_MOUSE_WHEEL) {
			float mx = ev.wheel.mouse_x, my = ev.wheel.mouse_y;
			if (mx < state.mapRect.x || mx > state.mapRect.x + state.mapRect.w) return;
			if (my < state.mapRect.y || my > state.mapRect.y + state.mapRect.h) return;
			_ZoomAt((ev.wheel.y > 0) ? 1.2f : (1.f / 1.2f), {mx, my});
			return;
		}

		if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN) {
			float mx = ev.button.x, my = ev.button.y;
			if (mx < state.mapRect.x || mx > state.mapRect.x + state.mapRect.w) return;
			if (my < state.mapRect.y || my > state.mapRect.y + state.mapRect.h) return;

			if (ev.button.button == SDL::BUTTON_MIDDLE) {
				state.panning      = true;
				state.panStart     = {mx, my};
				state.panViewStart = {state.viewX, state.viewY};
				return;
			}
			if (ev.button.button == SDL::BUTTON_LEFT) {
				state.mapLDown = true;
				state.lastTile = {-1.f, -1.f};
				state.stroke   = {};
				if (_ActiveLayerIsObject()) {
					_OnObjectLeftDown(mx, my, ev.button.clicks);
					return;
				}
				_ApplyToolAt(mx, my);
			}
			if (ev.button.button == SDL::BUTTON_RIGHT) {
				state.mapRDown = true;
				state.lastTile = {-1.f, -1.f};
				state.stroke   = {};
				int tx, ty; ScreenToTile(mx, my, tx, ty);
				_PaintTile(tx, ty, EMPTY_TILE);
			}
		}

		if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP) {
			if (ev.button.button == SDL::BUTTON_MIDDLE)
				state.panning = false;
			if (ev.button.button == SDL::BUTTON_LEFT) {
				state.mapLDown = false;
				if (state.objDrag) {
					state.objDrag = false;
					_FinishObjectDrag(ev.button.x, ev.button.y);
				}
				if (state.objMoving) {
					state.objMoving = false;
				}
				if (!state.stroke.changes.empty()) {
					ur.Push(std::move(state.stroke));
					state.stroke = {};
				}
			}
			if (ev.button.button == SDL::BUTTON_RIGHT) {
				state.mapRDown = false;
				if (!state.stroke.changes.empty()) {
					ur.Push(std::move(state.stroke));
					state.stroke = {};
				}
			}
		}

		if (ev.type == SDL::EVENT_MOUSE_MOTION) {
			float mx = ev.motion.x, my = ev.motion.y;
			if (state.panning) {
				state.viewX = state.panViewStart.x - (mx - state.panStart.x) / state.zoom;
				state.viewY = state.panViewStart.y - (my - state.panStart.y) / state.zoom;
				return;
			}
			if (state.objMoving) {
				if (auto* o = _SelObj()) {
					auto [wx, wy] = ScreenToWorld(mx, my);
					o->x = state.objMoveOrigin.x + (wx - state.objMoveStart.x);
					o->y = state.objMoveOrigin.y + (wy - state.objMoveStart.y);
					map.dirty = true;
				}
				return;
			}
			if (state.mapLDown && !state.objDrag && !_ActiveLayerIsObject())
				_ApplyToolAt(mx, my);
			if (state.mapRDown && !_ActiveLayerIsObject()) {
				int tx, ty; ScreenToTile(mx, my, tx, ty);
				_PaintTile(tx, ty, EMPTY_TILE);
			}
		}
	}

	// ── Object editing event handlers ─────────────────────────────────────────
	void _OnObjectLeftDown(float mx, float my, int clicks) {
		auto& L = map.layers[map.activeLayer];
		if (L.locked) return;
		auto [wx, wy] = ScreenToWorld(mx, my);

		switch (state.tool) {
			case ToolType::ObjSelect: {
				// Pick topmost object containing the world point.
				state.selectedObjLayer = -1;
				state.selectedObjId    = -1;
				for (auto& obj : L.objects) obj.selected = false;
				for (auto it = L.objects.rbegin(); it != L.objects.rend(); ++it) {
					if (_HitObject(*it, wx, wy)) {
						it->selected = true;
						state.selectedObjLayer = map.activeLayer;
						state.selectedObjId    = it->id;
						state.objMoving        = true;
						state.objMoveStart     = {wx, wy};
						state.objMoveOrigin    = {it->x, it->y};
						break;
					}
				}
				return;
			}
			case ToolType::ObjRect:
			case ToolType::ObjEllipse: {
				state.objDrag  = true;
				state.objStart = {mx, my};
				return;
			}
			case ToolType::ObjPoint: {
				ObjectDef obj;
				obj.id   = state.nextObjId++;
				obj.name = std::format("Point{}", obj.id);
				obj.type = ObjectType::Point;
				obj.x = wx; obj.y = wy; obj.w = 4; obj.h = 4;
				L.objects.push_back(std::move(obj));
				map.dirty = true;
				return;
			}
			case ToolType::ObjPolygon: {
				if (state.polyPoints.empty()) {
					state.polyOrigin = {wx, wy};
					state.polyPoints.push_back({0.f, 0.f});
				} else {
					state.polyPoints.push_back({wx - state.polyOrigin.x,
					                            wy - state.polyOrigin.y});
				}
				// Double click closes the polygon
				if (clicks >= 2) _FinishPolygon();
				return;
			}
			default: break;
		}
	}

	// Hit-test an object against a world-space point.
	bool _HitObject(const ObjectDef& o, float wx, float wy) const {
		switch (o.type) {
			case ObjectType::Rect:
			case ObjectType::Tile:
			case ObjectType::MapLink:
				return wx >= o.x && wy >= o.y &&
				       wx <= o.x + o.w && wy <= o.y + o.h;
			case ObjectType::Ellipse: {
				float cx = o.x + o.w * .5f, cy = o.y + o.h * .5f;
				float rx = SDL::Max(o.w * .5f, 1.f), ry = SDL::Max(o.h * .5f, 1.f);
				float dx = (wx - cx) / rx, dy = (wy - cy) / ry;
				return dx*dx + dy*dy <= 1.f;
			}
			case ObjectType::Point: {
				float dx = wx - o.x, dy = wy - o.y;
				return dx*dx + dy*dy <= 100.f; // 10 world-pixel pick radius
			}
			case ObjectType::Polygon: {
				// AABB pre-test
				if (o.points.size() < 3) return false;
				float lx = wx - o.x, ly = wy - o.y;
				// Ray-cast even-odd test
				bool inside = false;
				size_t n = o.points.size();
				for (size_t i = 0, j = n-1; i < n; j = i++) {
					const auto& pi = o.points[i];
					const auto& pj = o.points[j];
					bool cross = ((pi.y > ly) != (pj.y > ly)) &&
					             (lx < (pj.x - pi.x) * (ly - pi.y) /
					                   (pj.y - pi.y + 1e-9f) + pi.x);
					if (cross) inside = !inside;
				}
				return inside;
			}
		}
		return false;
	}

	// ── Tool application ──────────────────────────────────────────────────────

	void _ApplyToolAt(float sx, float sy) {
		int tx, ty;
		ScreenToTile(sx, sy, tx, ty);

		if (state.tool == ToolType::Select) {
			if (!state.selDrag) {
				state.selDrag      = true;
				state.selDragStart = {float(tx), float(ty)};
			}
			int x0 = int(SDL::Min(float(tx), state.selDragStart.x));
			int y0 = int(SDL::Min(float(ty), state.selDragStart.y));
			int x1 = int(SDL::Max(float(tx), state.selDragStart.x));
			int y1 = int(SDL::Max(float(ty), state.selDragStart.y));
			state.hasMapSel = true;
			state.selX = x0; state.selY = y0;
			state.selW = x1 - x0 + 1; state.selH = y1 - y0 + 1;
			return;
		}
		state.selDrag = false;

		if (state.tool == ToolType::Fill) {
			// Pattern flood-fill: fill the connected region with the selected
			// NxM tileset stamp, repeating it.
			_PatternFloodFill(tx, ty);
			return;
		}

		if (state.tool == ToolType::Erase) {
			// Erase ignores the multi-tile stamp.
			_PaintTile(tx, ty, EMPTY_TILE);
			return;
		}

		if (state.tool == ToolType::Brush) {
			TileID paint = _SelectedTileID();
			int half = state.brushSize / 2;
			for (int dy = -half; dy <= half; ++dy)
			for (int dx = -half; dx <= half; ++dx)
				_PaintTile(tx + dx, ty + dy, paint);
			return;
		}

		// Pencil — stamp the selected NxM region from the tileset (or a single
		// tile when the selection is 1x1).
		_StampSelectionAt(tx, ty);
	}

	// Paint the currently-selected NxM tileset region anchored at (tx0, ty0).
	void _StampSelectionAt(int tx0, int ty0) {
		if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
		const auto& ts = map.tilesets[state.activeTileset];
		int sw = SDL::Max(1, state.selTileW);
		int sh = SDL::Max(1, state.selTileH);
		// Anchor the stamp using the tile under the cursor (avoids repaint
		// loops in lastTile by walking deterministically per cell).
		for (int dy = 0; dy < sh; ++dy)
		for (int dx = 0; dx < sw; ++dx) {
			int gx = state.selTileX + dx;
			int gy = state.selTileY + dy;
			if (gx >= ts.columns || gy >= ts.rows) continue;
			TileID id = ts.firstGid + (TileID)(gy * ts.columns + gx);
			_PaintTileNoDup(tx0 + dx, ty0 + dy, id);
		}
	}

	// Variant of _PaintTile that doesn't filter on lastTile (so stamps work).
	void _PaintTileNoDup(int tx, int ty, TileID id) {
		if (!map.infinite && (tx < 0 || ty < 0 || tx >= map.width || ty >= map.height)) return;
		if (map.activeLayer < 0 || map.activeLayer >= (int)map.layers.size()) return;
		TileID old = map.GetTile(map.activeLayer, tx, ty);
		if (old == id) return;
		map.SetTile(map.activeLayer, tx, ty, id);
		state.stroke.changes.push_back({map.activeLayer, tx, ty, old, id});
		if (id != EMPTY_TILE && !map.tilesets.empty()) {
			const TilesetDef* ts = map.FindTileset(id);
			if (ts && ts->smart) _SmartUpdate(map.activeLayer, tx, ty);
		}
	}

	// Pattern flood-fill: select all tiles in the connected region of the same
	// id, then paint the selected tileset region over it, tiling the stamp.
	void _PatternFloodFill(int startX, int startY) {
		if (map.activeLayer < 0 || map.activeLayer >= (int)map.layers.size()) return;
		const auto& L = map.layers[map.activeLayer];
		if (L.type != LayerType::Tile || L.locked) return;
		if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
		const auto& ts = map.tilesets[state.activeTileset];
		int sw = SDL::Max(1, state.selTileW);
		int sh = SDL::Max(1, state.selTileH);

		// Pre-compute the tile IDs in the stamp for fast lookup.
		auto tileFor = [&](int dx, int dy) -> TileID {
			int gx = state.selTileX + dx;
			int gy = state.selTileY + dy;
			if (gx >= ts.columns || gy >= ts.rows) return EMPTY_TILE;
			return ts.firstGid + (TileID)(gy * ts.columns + gx);
		};

		TileID target = map.GetTile(map.activeLayer, startX, startY);
		// Walk the connected region (4-neighbour) and paint per-cell using stamp tiling.
		auto key = [](int x, int y) -> uint64_t {
			return (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32);
		};
		std::unordered_set<uint64_t> visited;
		std::queue<std::pair<int,int>> q;
		q.push({startX, startY});
		Command cmd;
		while (!q.empty() && (int)cmd.changes.size() < 200000) {
			auto [x, y] = q.front(); q.pop();
			if (!map.infinite && (x < 0 || y < 0 || x >= map.width || y >= map.height)) continue;
			if (!visited.insert(key(x, y)).second) continue;
			if (map.GetTile(map.activeLayer, x, y) != target) continue;
			// Tile index in the stamp pattern (origin = start cell)
			int rx = ((x - startX) % sw + sw) % sw;
			int ry = ((y - startY) % sh + sh) % sh;
			TileID newId = tileFor(rx, ry);
			TileID old   = target;
			if (newId != old) {
				map.SetTile(map.activeLayer, x, y, newId);
				cmd.changes.push_back({map.activeLayer, x, y, old, newId});
			}
			q.push({x+1,y}); q.push({x-1,y});
			q.push({x,y+1}); q.push({x,y-1});
		}
		if (!cmd.changes.empty()) {
			ur.Push(std::move(cmd));
			map.dirty = true;
		}
	}

	void _PaintTile(int tx, int ty, TileID id) {
		if (!map.infinite && (tx < 0 || ty < 0 || tx >= map.width || ty >= map.height)) return;
		if (map.activeLayer < 0 || map.activeLayer >= (int)map.layers.size()) return;
		if (state.lastTile.x == tx && state.lastTile.y == ty) return;
		state.lastTile = {float(tx), float(ty)};

		TileID old = map.GetTile(map.activeLayer, tx, ty);
		if (old == id) return;
		map.SetTile(map.activeLayer, tx, ty, id);
		state.stroke.changes.push_back({map.activeLayer, tx, ty, old, id});

		// Smart auto-tile update in 3x3 neighbourhood
		if (id != EMPTY_TILE && !map.tilesets.empty()) {
			const TilesetDef* ts = map.FindTileset(id);
			if (ts && ts->smart) _SmartUpdate(map.activeLayer, tx, ty);
		}
	}

	// Auto-tile: remap tile to a variant in the same row based on
	// the 4-way neighbour mask (16 possible combinations → column 0..15).
	void _SmartUpdate(int layer, int cx, int cy) {
		for (int dy = -1; dy <= 1; ++dy)
		for (int dx = -1; dx <= 1; ++dx) {
			int x = cx + dx, y = cy + dy;
			if (!map.infinite && (x < 0 || y < 0 || x >= map.width || y >= map.height)) continue;
			TileID t = map.GetTile(layer, x, y);
			if (t == EMPTY_TILE) continue;
			const TilesetDef* ts = map.FindTileset(t);
			if (!ts || !ts->smart) continue;
			int  local   = (int)(t - ts->firstGid);
			int  rowBase = (local / ts->columns) * ts->columns;
			uint8_t mask = map.NeighbourMask(layer, x, y);
			int variant  = (int)mask % SDL::Min(16, ts->tileCount);
			TileID newId = ts->firstGid + (TileID)(rowBase + variant);
			if (newId != t && (int)(newId - ts->firstGid) < ts->tileCount) {
				TileID old = t;
				map.SetTile(layer, x, y, newId);
				state.stroke.changes.push_back({layer, x, y, old, newId});
			}
		}
	}

	TileID _SelectedTileID() const {
		if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size())
			return EMPTY_TILE;
		const auto& ts = map.tilesets[state.activeTileset];
		int local = state.selTileY * ts.columns + state.selTileX;
		return ts.firstGid + (TileID)local;
	}

	void _FinishObjectDrag(float ex, float ey) {
		if (map.activeLayer < 0 || map.activeLayer >= (int)map.layers.size()) return;
		auto& layer = map.layers[map.activeLayer];
		if (layer.type != LayerType::Object || layer.locked) return;
		float dx = SDL::Abs(ex - state.objStart.x);
		float dy = SDL::Abs(ey - state.objStart.y);
		if (dx < 2.f && dy < 2.f) return;
		auto [wx, wy] = ScreenToWorld(SDL::Min(state.objStart.x, ex),
									  SDL::Min(state.objStart.y, ey));
		ObjectDef obj;
		obj.id   = state.nextObjId++;
		obj.x = wx; obj.y = wy;
		obj.w = dx / state.zoom; obj.h = dy / state.zoom;
		if (state.tool == ToolType::ObjEllipse) {
			obj.type = ObjectType::Ellipse;
			obj.name = std::format("Ellipse{}", obj.id);
		} else {
			obj.type = ObjectType::Rect;
			obj.name = std::format("Rect{}", obj.id);
		}
		layer.objects.push_back(std::move(obj));
		map.dirty = true;
	}

	// =========================================================================
	// Tileset panel events
	// =========================================================================

	// Zoom the tileset palette at a given screen pivot point.
	void _ZoomTileset(float factor, float pivotX, float pivotY) {
		if (state.tsTileZoom <= 0.f) {
			if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
			const auto& ts = map.tilesets[state.activeTileset];
			if (ts.imageW <= 0 || state.tilesetRect.w <= 4.f) return;
			state.tsTileZoom = SDL::Min(1.f, (state.tilesetRect.w - 4.f) / float(ts.imageW));
		}
		float newZoom = SDL::Clamp(state.tsTileZoom * factor, 0.1f, 8.f);
		if (newZoom == state.tsTileZoom) return;
		float imgX = state.tilesetRect.x + 2.f - state.tsScrollX;
		float imgY = state.tilesetRect.y + 2.f - state.tsScrollY;
		float ratio = newZoom / state.tsTileZoom;
		state.tsScrollX  = state.tilesetRect.x + 2.f - pivotX + (pivotX - imgX) * ratio;
		state.tsScrollY  = state.tilesetRect.y + 2.f - pivotY + (pivotY - imgY) * ratio;
		state.tsTileZoom = newZoom;
	}

	void _OnTilesetEvent(SDL::Event& ev) {
		SDL::Keymod keymod = SDL::GetModState();

		if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
		const auto& ts = map.tilesets[state.activeTileset];
		if (ts.imageW <= 0 || ts.columns <= 0 || ts.rows <= 0) return;

		float tw  = ts.tileW   * state.tsScale;
		float th  = ts.tileH   * state.tsScale;
		float spH = ts.spacing * state.tsScale;
		float spV = spH;
		float imgX = state.tilesetRect.x + 2.f - state.tsScrollX;
		float imgY = state.tilesetRect.y + 2.f - state.tsScrollY;

		auto screenToCell = [&](float sx, float sy, int& cx, int& cy) {
			cx = SDL::Clamp((int)std::floor((sx - imgX) / (tw + spH)), 0, ts.columns - 1);
			cy = SDL::Clamp((int)std::floor((sy - imgY) / (th + spV)), 0, ts.rows    - 1);
		};

		if (ev.type == SDL::EVENT_MOUSE_WHEEL) {
			float mx = ev.wheel.mouse_x, my = ev.wheel.mouse_y;
			if (mx < state.tilesetRect.x || mx > state.tilesetRect.x + state.tilesetRect.w) return;
			if (my < state.tilesetRect.y || my > state.tilesetRect.y + state.tilesetRect.h) return;
			bool ctrl = (keymod & SDL::KMOD_CTRL) != 0;
			if (ctrl)
				_ZoomTileset((ev.wheel.y > 0) ? 1.2f : (1.f / 1.2f), mx, my);
			else
				state.tsScrollY -= ev.wheel.y * 28.f;
			return;
		}
		if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN &&
			ev.button.button == SDL::BUTTON_LEFT) {
			float mx = ev.button.x, my = ev.button.y;
			if (mx < state.tilesetRect.x || mx > state.tilesetRect.x + state.tilesetRect.w) return;
			if (my < state.tilesetRect.y || my > state.tilesetRect.y + state.tilesetRect.h) return;
			screenToCell(mx, my, state.selTileX, state.selTileY);
			state.selTileW   = 1; state.selTileH = 1;
			state.tsDragging = true;
			state.tsDragStart= {mx, my};
		}
		if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP &&
			ev.button.button == SDL::BUTTON_LEFT)
			state.tsDragging = false;

		if (ev.type == SDL::EVENT_MOUSE_MOTION && state.tsDragging) {
			int x0, y0, x1, y1;
			screenToCell(state.tsDragStart.x, state.tsDragStart.y, x0, y0);
			screenToCell(ev.motion.x,         ev.motion.y,         x1, y1);
			state.selTileX = SDL::Min(x0, x1); state.selTileY = SDL::Min(y0, y1);
			state.selTileW = SDL::Abs(x1 - x0) + 1;
			state.selTileH = SDL::Abs(y1 - y0) + 1;
		}

		int gid = (int)ts.firstGid + state.selTileY * ts.columns + state.selTileX;
		ui.SetText(eTileInfo, std::format("GID:{} [{},{}]", gid, state.selTileX, state.selTileY));
	}

	// =========================================================================
	// Zoom
	// =========================================================================

	void _ZoomAt(float factor, SDL::FPoint pivot) {
		float nz = SDL::Clamp(state.zoom * factor,
							  EditorState::ZOOM_MIN, EditorState::ZOOM_MAX);
		if (nz == state.zoom) return;
		auto [wx, wy] = ScreenToWorld(pivot.x, pivot.y);
		state.zoom  = nz;
		state.viewX = wx - (pivot.x - state.mapRect.x) / nz;
		state.viewY = wy - (pivot.y - state.mapRect.y) / nz;
	}

	// =========================================================================
	// Layer management
	// =========================================================================

	void _SelectLayer(int idx) {
		if (idx < 0 || idx >= (int)map.layers.size()) return;
		map.activeLayer = idx;
	}
	void _ToggleLayerVisible(int idx) {
		if (idx >= 0 && idx < (int)map.layers.size())
			map.layers[idx].visible = !map.layers[idx].visible;
	}
	void _ToggleLayerLock(int idx) {
		if (idx >= 0 && idx < (int)map.layers.size())
			map.layers[idx].locked = !map.layers[idx].locked;
	}
	void _AddTileLayer() {
		if ((int)map.layers.size() >= kMaxLayers) return;
		MapLayer l; l.name = std::format("Layer {}", map.layers.size() + 1);
		map.layers.push_back(std::move(l));
		map.activeLayer = (int)map.layers.size() - 1;
	}
	void _AddObjectLayer() {
		if ((int)map.layers.size() >= kMaxLayers) return;
		MapLayer l; l.name = std::format("Objects {}", map.layers.size() + 1);
		l.type = LayerType::Object;
		map.layers.push_back(std::move(l));
		map.activeLayer = (int)map.layers.size() - 1;
	}
	void _DeleteActiveLayer() {
		if (map.layers.size() <= 1) return;
		map.layers.erase(map.layers.begin() + map.activeLayer);
		map.activeLayer = SDL::Clamp(map.activeLayer, 0, (int)map.layers.size() - 1);
	}
	void _MoveActiveLayer(int dir) {
		int n    = (int)map.layers.size();
		int dest = map.activeLayer + dir;
		if (dest < 0 || dest >= n) return;
		std::swap(map.layers[map.activeLayer], map.layers[dest]);
		map.activeLayer = dest;
	}

	// Refresh pre-built layer slot widgets every frame
	void _UpdateLayerSlots() {
		int n = (int)map.layers.size();
		for (int i = 0; i < kMaxLayers; ++i) {
			auto& slot = layerSlots[i];
			if (i >= n) { ui.SetVisible(slot.row, false); continue; }
			ui.SetVisible(slot.row, true);
			const auto& layer = map.layers[i];
			ui.SetText(slot.lblName, layer.name);
			ui.GetStyle(slot.lblName).textColor =
				(i == map.activeLayer) ? pal::SELECTED : pal::WHITE;
			ui.GetStyle(slot.row).bgColor =
				(i == map.activeLayer)
				? SDL::Color{ pal::ACCENT3.r, pal::ACCENT3.g, pal::ACCENT3.b, 200 }
				: SDL::Color{0, 0, 0, 0};
			// Visibility button: green tint + full opacity when visible, dimmed when hidden
			auto& icVis = ui.GetOrAddIconData(slot.btnVis);
			icVis.tintNormalColor  = layer.visible ? pal::GREEN  : SDL::Color{255,255,255,255};
			icVis.tintHoveredColor = icVis.tintNormalColor;
			icVis.opacityNormal  = layer.visible ? 1.f : 0.3f;
			icVis.opacityHovered = 0.9f;
			// Lock button: orange tint + dark bg when locked
			auto& icLock = ui.GetOrAddIconData(slot.btnLock);
			icLock.tintNormalColor  = layer.locked ? pal::ORANGE : SDL::Color{255,255,255,255};
			icLock.tintHoveredColor = icLock.tintNormalColor;
			ui.GetStyle(slot.btnLock).bgColor =
				layer.locked ? SDL::Color{70,40,10,200} : SDL::Color{0,0,0,0};
		}
	}

	// =========================================================================
	// Tool selection
	// =========================================================================

	void _SetTool(ToolType t) {
		state.tool = t;

		// Cancel any in-progress polygon when switching tools
		if (t != ToolType::ObjPolygon) state.polyPoints.clear();

		auto applyStyle = [this](SDL::ECS::EntityId id, bool active) {
			if (id == SDL::ECS::NullEntity) return;
			auto& s = ui.GetStyle(id);
			if (active) {
				s.bgColor        = pal::ACCENT;
				s.bgHoveredColor = pal::ACCENT2;
				s.bgPressedColor = pal::ACCENT3;
				s.borders        = SDL::FBox(1.f);
				s.bdColor        = {pal::ACCENT.r, pal::ACCENT.g, pal::ACCENT.b, 160};
			} else {
				s.bgColor        = {0, 0, 0, 0};
				s.bgHoveredColor = pal::SURFACE2;
				s.bgPressedColor = pal::ACCENT3;
				s.borders        = SDL::FBox(0.f);
				s.bdColor        = pal::BORDER;
			}
		};
		static constexpr ToolType kTile[kTileToolCount] = {
			ToolType::Pencil, ToolType::Brush, ToolType::Fill,
			ToolType::Erase,  ToolType::Select
		};
		static constexpr ToolType kObj[kObjToolCount] = {
			ToolType::ObjSelect, ToolType::ObjRect, ToolType::ObjEllipse,
			ToolType::ObjPoint,  ToolType::ObjPolygon
		};
		for (int i = 0; i < kTileToolCount; ++i)
			applyStyle(tileToolBtns[i], kTile[i] == t);
		for (int i = 0; i < kObjToolCount; ++i)
			applyStyle(objToolBtns[i],  kObj[i]  == t);
	}

	bool _ActiveLayerIsObject() const {
		return map.activeLayer >= 0
		    && map.activeLayer < (int)map.layers.size()
		    && map.layers[map.activeLayer].type == LayerType::Object;
	}

	void _UpdateToolRowVisibility() {
		bool obj = _ActiveLayerIsObject();
		if (eToolRowTile != SDL::ECS::NullEntity) ui.SetVisible(eToolRowTile, !obj);
		if (eToolRowObj  != SDL::ECS::NullEntity) ui.SetVisible(eToolRowObj,  obj);
		// When switching to an object layer, default to ObjSelect if the
		// current tool isn't an object tool (and vice versa).
		bool curIsObj =
			state.tool == ToolType::ObjSelect ||
			state.tool == ToolType::ObjRect    ||
			state.tool == ToolType::ObjEllipse ||
			state.tool == ToolType::ObjPoint   ||
			state.tool == ToolType::ObjPolygon;
		if (obj && !curIsObj)  _SetTool(ToolType::ObjSelect);
		if (!obj && curIsObj)  _SetTool(ToolType::Pencil);
	}

	void _RefreshGridBtn() {
		if (eGridBtn == SDL::ECS::NullEntity) return;
		auto& ic = ui.GetOrAddIconData(eGridBtn);
		SDL::Color tint = state.showGrid ? pal::GREEN : SDL::Color{255,255,255,255};
		ic.tintNormalColor  = tint;
		ic.tintHoveredColor = tint;
		auto& s = ui.GetStyle(eGridBtn);
		s.bgColor = state.showGrid ? SDL::Color{26,58,38,220} : SDL::Color{0,0,0,0};
	}

	// =========================================================================
	// Lua scripting (engine API)
	// =========================================================================

	void _ConsolePush(const std::string& line) {
		// _ConsolePush can run from worker threads (graph runner) — append
		// under a mutex and let Iterate() push the visible text into the UI.
		std::lock_guard<std::mutex> lk(m_consoleMutex);
		m_consoleLog += line;
		if (!line.empty() && line.back() != '\n') m_consoleLog += '\n';
		// Keep the console bounded — drop oldest lines when > 200 lines.
		size_t lines = std::count(m_consoleLog.begin(), m_consoleLog.end(), '\n');
		while (lines > 200) {
			auto p = m_consoleLog.find('\n');
			if (p == std::string::npos) break;
			m_consoleLog.erase(0, p + 1);
			--lines;
		}
		m_consoleDirty = true;
	}
	bool m_consoleDirty = false;
	void _ConsoleFlushToUI() {
		if (!m_consoleDirty) return;
		std::string copy;
		{
			std::lock_guard<std::mutex> lk(m_consoleMutex);
			if (!m_consoleDirty) return;
			copy = m_consoleLog;
			m_consoleDirty = false;
		}
		if (eScriptCons != SDL::ECS::NullEntity)
			ui.SetText(eScriptCons, copy);
	}

#ifdef SDL3PP_TILE_EDITOR_LUA
	// ── Lua interop: per-binding C functions read `this` from the registry ───
	static Main* _LuaGetMain(lua_State* L) {
		lua_getfield(L, LUA_REGISTRYINDEX, "__main_this");
		Main* m = (Main*)lua_touserdata(L, -1);
		lua_pop(L, 1);
		return m;
	}
	static int _Lua_log(lua_State* L) {
		Main* m = _LuaGetMain(L);
		int n = lua_gettop(L);
		std::string out;
		for (int i = 1; i <= n; ++i) {
			const char* s = luaL_tolstring(L, i, nullptr);
			if (s) out += s;
			lua_pop(L, 1);
			if (i < n) out += "\t";
		}
		if (m) m->_ConsolePush(out);
		return 0;
	}
	static int _Lua_size(lua_State* L) {
		Main* m = _LuaGetMain(L);
		if (!m) return 0;
		lua_pushinteger(L, m->map.width);
		lua_pushinteger(L, m->map.height);
		return 2;
	}
	static int _Lua_tile(lua_State* L) {
		Main* m = _LuaGetMain(L);
		if (!m) { lua_pushinteger(L, 0); return 1; }
		int layer = (int)luaL_checkinteger(L, 1);
		int x     = (int)luaL_checkinteger(L, 2);
		int y     = (int)luaL_checkinteger(L, 3);
		lua_pushinteger(L, (lua_Integer)m->map.GetTile(layer, x, y));
		return 1;
	}
	static int _Lua_set_tile(lua_State* L) {
		Main* m = _LuaGetMain(L);
		if (!m) return 0;
		int layer = (int)luaL_checkinteger(L, 1);
		int x     = (int)luaL_checkinteger(L, 2);
		int y     = (int)luaL_checkinteger(L, 3);
		int id    = (int)luaL_checkinteger(L, 4);
		m->map.SetTile(layer, x, y, (TileID)id);
		m->project.Touch(WorkspaceKind::Map | WorkspaceKind::Test);
		lua_pushboolean(L, 1);
		return 1;
	}
	static int _Lua_player_pos(lua_State* L) {
		Main* m = _LuaGetMain(L);
		if (!m) return 0;
		lua_pushnumber(L, m->m_player.pos.x);
		lua_pushnumber(L, m->m_player.pos.y);
		return 2;
	}
	static int _Lua_set_player(lua_State* L) {
		Main* m = _LuaGetMain(L);
		if (!m) return 0;
		m->m_player.pos.x = (float)luaL_checknumber(L, 1);
		m->m_player.pos.y = (float)luaL_checknumber(L, 2);
		m->m_player.vel = {0.f, 0.f};
		return 0;
	}
	static int _Lua_dialog(lua_State* L) {
		Main* m = _LuaGetMain(L);
		const char* msg = luaL_checkstring(L, 1);
		if (m) m->_ConsolePush(std::string("[dialog] ") + msg);
		return 0;
	}

	void _LuaInit() {
		m_lua = luaL_newstate();
		if (!m_lua) return;
		luaL_openlibs(m_lua);
		// Register `this` so static thunks can find the Main instance.
		lua_pushlightuserdata(m_lua, this);
		lua_setfield(m_lua, LUA_REGISTRYINDEX, "__main_this");
		// engine = { log=..., size=..., tile=..., set_tile=..., player_pos=..., set_player=..., dialog=... }
		lua_newtable(m_lua);
		struct { const char* name; lua_CFunction fn; } kFns[] = {
			{"log",         _Lua_log},
			{"size",        _Lua_size},
			{"tile",        _Lua_tile},
			{"set_tile",    _Lua_set_tile},
			{"player_pos",  _Lua_player_pos},
			{"set_player",  _Lua_set_player},
			{"dialog",      _Lua_dialog},
		};
		for (const auto& f : kFns) {
			lua_pushcfunction(m_lua, f.fn);
			lua_setfield(m_lua, -2, f.name);
		}
		lua_setglobal(m_lua, "engine");
	}

	void _LuaShutdown() {
		if (m_lua) { lua_close(m_lua); m_lua = nullptr; }
	}

	bool _LuaRun(const std::string& code, const std::string& chunkname) {
		if (!m_lua) return false;
		if (luaL_loadbuffer(m_lua, code.c_str(), code.size(), chunkname.c_str()) != LUA_OK) {
			_ConsolePush(std::string("[error] ") + lua_tostring(m_lua, -1));
			lua_pop(m_lua, 1);
			return false;
		}
		if (lua_pcall(m_lua, 0, 0, 0) != LUA_OK) {
			_ConsolePush(std::string("[runtime] ") + lua_tostring(m_lua, -1));
			lua_pop(m_lua, 1);
			return false;
		}
		return true;
	}

	void _LuaCallGlobal(const char* name, float dt) {
		if (!m_lua) return;
		lua_getglobal(m_lua, name);
		if (!lua_isfunction(m_lua, -1)) { lua_pop(m_lua, 1); return; }
		lua_pushnumber(m_lua, dt);
		if (lua_pcall(m_lua, 1, 0, 0) != LUA_OK) {
			_ConsolePush(std::string("[runtime] ") + lua_tostring(m_lua, -1));
			lua_pop(m_lua, 1);
		}
	}
#else
	void _LuaInit()        {}
	void _LuaShutdown()    {}
	bool _LuaRun(const std::string&, const std::string&) {
		_ConsolePush("[lua] runtime disabled — rebuild with -DSDL3PP_TILE_EDITOR_LUA");
		return false;
	}
	void _LuaCallGlobal(const char*, float) {}
#endif

	void _LuaRunActiveScript() {
		if (project.scripts.empty()) return;
		int i = std::clamp(project.activeScript, 0, (int)project.scripts.size()-1);
		const auto& s = project.scripts[i];
		_ConsolePush(std::string("[run] ") + s.name);
		_LuaRun(s.code, s.name);
	}

	// =========================================================================
	// Cinematic workspace impl
	// =========================================================================

	void _CineAddTrack(CineTrackKind kind) {
		if (project.cinematics.empty()) project.cinematics.push_back({});
		auto& doc = project.cinematics[project.activeCinematic];
		CineTrack t;
		t.kind = kind;
		switch (kind) {
			case CineTrackKind::Image:  t.name = std::format("Image {}", doc.tracks.size()+1); break;
			case CineTrackKind::Music:  t.name = std::format("Music {}", doc.tracks.size()+1); break;
			case CineTrackKind::Sfx:    t.name = std::format("Sfx {}",   doc.tracks.size()+1); break;
			case CineTrackKind::Dialog: t.name = std::format("Dialog {}",doc.tracks.size()+1); break;
		}
		// Add one starter clip at the current playhead.
		CineClip c;
		c.start = m_cineTime;
		c.length = 2.f;
		c.asset = (kind == CineTrackKind::Dialog) ? "Hello world!" : "";
		t.clips.push_back(std::move(c));
		doc.tracks.push_back(std::move(t));
		project.Touch(WorkspaceKind::Cinematic);
	}

	void _RenderCine(SDL::RendererRef r, SDL::FRect rect) {
		// Background
		r.SetDrawColor({18, 20, 28, 255});
		r.RenderFillRect(rect);
		if (project.cinematics.empty()) return;
		auto& doc = project.cinematics[project.activeCinematic];

		constexpr float headerW = 110.f;
		constexpr float trackH  = 38.f;
		const float secW = (rect.w - headerW) / SDL::Max(0.5f, doc.duration);

		// Time ruler (every second)
		for (int s = 0; s <= (int)std::ceil(doc.duration); ++s) {
			float x = rect.x + headerW + s * secW;
			r.SetDrawColor((s % 5 == 0) ? SDL::Color{90, 96, 130, 200}
			                            : SDL::Color{60, 65, 90, 160});
			r.RenderLine({x, rect.y}, {x, rect.y + rect.h});
		}
		r.SetDrawColor({90, 96, 130, 230});
		r.RenderLine({rect.x + headerW, rect.y}, {rect.x + headerW, rect.y + rect.h});

		// Tracks
		for (int ti = 0; ti < (int)doc.tracks.size(); ++ti) {
			float y = rect.y + 4.f + ti * trackH;
			// Header
			SDL::FRect hdr{rect.x, y, headerW, trackH - 4.f};
			SDL::Color trackBg;
			switch (doc.tracks[ti].kind) {
				case CineTrackKind::Image:  trackBg = {70, 100, 160, 255}; break;
				case CineTrackKind::Music:  trackBg = {120, 70, 160, 255}; break;
				case CineTrackKind::Sfx:    trackBg = {160, 110, 60, 255}; break;
				case CineTrackKind::Dialog: trackBg = {80, 140, 90, 255};  break;
			}
			r.SetDrawColor(trackBg); r.RenderFillRect(hdr);
			r.SetDrawColor({0, 0, 0, 90}); r.RenderRect(hdr);

			// Clips
			for (int ci = 0; ci < (int)doc.tracks[ti].clips.size(); ++ci) {
				const auto& c = doc.tracks[ti].clips[ci];
				SDL::FRect clip{rect.x + headerW + c.start * secW, y,
				                c.length * secW, trackH - 4.f};
				SDL::Color cc = trackBg; cc.a = 220;
				r.SetDrawColor(cc); r.RenderFillRect(clip);
				r.SetDrawColor({255, 255, 255, 180}); r.RenderRect(clip);
			}
		}

		// Playhead
		float phX = rect.x + headerW + m_cineTime * secW;
		r.SetDrawColor({255, 100, 100, 240});
		r.RenderLine({phX, rect.y}, {phX, rect.y + rect.h});
	}

	void _OnCineEvent(SDL::Event& ev) {
		if (project.cinematics.empty()) return;
		auto& doc = project.cinematics[project.activeCinematic];
		constexpr float headerW = 110.f;
		constexpr float trackH  = 38.f;
		const float rectx = 0.f; // canvas-relative; we use mouse coords directly via state-less hit.
		(void)rectx;

		if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN && ev.button.button == SDL::BUTTON_LEFT) {
			float mx = ev.button.x, my = ev.button.y;
			// Find which track / clip was hit (approximate: relies on UI placing
			// the canvas at the rect we used last frame; conservative for now).
			// Locate track by Y delta from canvas top — for simplicity we walk
			// every track / clip and test world-space rectangles.
			float canvasTop = 0.f, canvasLeft = 0.f;
			if (auto* cr = ecs_context.Get<SDL::UI::ComputedRect>(eCineCanvas)) {
				canvasTop  = cr->screen.y;
				canvasLeft = cr->screen.x;
			}
			float canvasW = 0.f;
			if (auto* cr = ecs_context.Get<SDL::UI::ComputedRect>(eCineCanvas))
				canvasW = cr->screen.w;
			float secW = (canvasW - headerW) / SDL::Max(0.5f, doc.duration);
			for (int ti = 0; ti < (int)doc.tracks.size(); ++ti) {
				float y = canvasTop + 4.f + ti * trackH;
				for (int ci = 0; ci < (int)doc.tracks[ti].clips.size(); ++ci) {
					const auto& c = doc.tracks[ti].clips[ci];
					float x = canvasLeft + headerW + c.start * secW;
					float w = c.length * secW;
					if (mx >= x && mx <= x + w && my >= y && my <= y + trackH - 4.f) {
						m_cineDragTrack = ti;
						m_cineDragClip = ci;
						// Hit right-edge → resize; otherwise move.
						m_cineResize = (mx > x + w - 6.f);
						return;
					}
				}
			}
		} else if (ev.type == SDL::EVENT_MOUSE_MOTION
		           && m_cineDragTrack >= 0 && m_cineDragClip >= 0) {
			float canvasLeft = 0.f, canvasW = 0.f;
			if (auto* cr = ecs_context.Get<SDL::UI::ComputedRect>(eCineCanvas)) {
				canvasLeft = cr->screen.x;
				canvasW = cr->screen.w;
			}
			float secW = (canvasW - headerW) / SDL::Max(0.5f, doc.duration);
			auto& clip = doc.tracks[m_cineDragTrack].clips[m_cineDragClip];
			if (m_cineResize) {
				clip.length = SDL::Max(0.1f, (ev.motion.x - canvasLeft - headerW)/secW - clip.start);
			} else {
				clip.start += ev.motion.xrel / secW;
				if (clip.start < 0.f) clip.start = 0.f;
			}
		} else if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP) {
			m_cineDragTrack = -1; m_cineDragClip = -1; m_cineResize = false;
		}
	}

	void _CineUpdate(float dt) {
		if (!m_cinePlaying) return;
		auto& doc = project.cinematics[project.activeCinematic];
		m_cineTime += dt;
		if (m_cineTime > doc.duration) { m_cineTime = doc.duration; m_cinePlaying = false; }
		ui.SetText(eCinePlayhead,
			std::format("{:.2f}s / {:.2f}s", m_cineTime, doc.duration));
	}

	// =========================================================================
	// NodeGraph workspace impl
	// =========================================================================

	void _GraphAddNode(NodeKind k) {
		if (project.graphs.empty()) project.graphs.push_back({});
		auto& g = project.graphs[project.activeGraph];
		NodeDef n;
		n.id   = g.nextNodeId++;
		n.kind = k;
		n.pos  = {(float)(60 + (g.nodes.size()%6) * 200),
		          (float)(60 + (g.nodes.size()/6) * 130)};
		switch (k) {
			case NodeKind::Event:     n.title = "Event";     n.outputs = {{{(float)n.size.x, n.size.y*0.5f},"out"}}; break;
			case NodeKind::Script:    n.title = "Script";    n.body = "engine.log('hi')";
				n.inputs = {{{0,n.size.y*0.5f},"in"}}; n.outputs = {{{(float)n.size.x,n.size.y*0.5f},"out"}}; break;
			case NodeKind::Dialog:    n.title = "Dialog";    n.body = "Hello!";
				n.inputs = {{{0,n.size.y*0.5f},"in"}}; n.outputs = {{{(float)n.size.x,n.size.y*0.5f},"next"}}; break;
			case NodeKind::Cinematic: n.title = "Cinematic"; n.body = "intro";
				n.inputs = {{{0,n.size.y*0.5f},"play"}}; n.outputs = {{{(float)n.size.x,n.size.y*0.5f},"done"}}; break;
			case NodeKind::Wait:      n.title = "Wait";      n.body = "1.0";
				n.inputs = {{{0,n.size.y*0.5f},"in"}}; n.outputs = {{{(float)n.size.x,n.size.y*0.5f},"after"}}; break;
			case NodeKind::Branch:    n.title = "Branch";    n.body = "engine.player_pos() > 100";
				n.inputs = {{{0,n.size.y*0.5f},"cond"}};
				n.outputs = {{{(float)n.size.x,n.size.y*0.33f},"true"},
				             {{(float)n.size.x,n.size.y*0.67f},"false"}}; break;
		}
		g.nodes.push_back(std::move(n));
		project.Touch(WorkspaceKind::NodeGraph);
	}

	SDL::FPoint _GraphWorldToScreen(SDL::FRect canvas, float wx, float wy) const {
		return {canvas.x + (wx - m_graphViewX) * m_graphZoom,
		        canvas.y + (wy - m_graphViewY) * m_graphZoom};
	}
	SDL::FPoint _GraphScreenToWorld(SDL::FRect canvas, float sx, float sy) const {
		return {(sx - canvas.x) / m_graphZoom + m_graphViewX,
		        (sy - canvas.y) / m_graphZoom + m_graphViewY};
	}

	void _RenderGraph(SDL::RendererRef r, SDL::FRect rect) {
		r.SetDrawColor({16, 18, 26, 255});
		r.RenderFillRect(rect);
		if (project.graphs.empty()) return;

		// Grid
		float step = 32.f * m_graphZoom;
		if (step > 6.f) {
			r.SetDrawColor({30, 34, 50, 255});
			float ox = std::fmod(-m_graphViewX * m_graphZoom, step);
			float oy = std::fmod(-m_graphViewY * m_graphZoom, step);
			for (float x = rect.x + ox; x < rect.x + rect.w; x += step)
				r.RenderLine({x, rect.y}, {x, rect.y + rect.h});
			for (float y = rect.y + oy; y < rect.y + rect.h; y += step)
				r.RenderLine({rect.x, y}, {rect.x + rect.w, y});
		}

		const auto& g = project.graphs[project.activeGraph];
		auto findNode = [&](int id) -> const NodeDef* {
			for (const auto& n : g.nodes) if (n.id == id) return &n;
			return nullptr;
		};

		// Wires
		for (const auto& w : g.wires) {
			const NodeDef* a = findNode(w.srcNode);
			const NodeDef* b = findNode(w.dstNode);
			if (!a || !b) continue;
			if (w.srcPort >= (int)a->outputs.size() || w.dstPort >= (int)b->inputs.size()) continue;
			auto p1 = _GraphWorldToScreen(rect,
				a->pos.x + a->outputs[w.srcPort].local.x,
				a->pos.y + a->outputs[w.srcPort].local.y);
			auto p2 = _GraphWorldToScreen(rect,
				b->pos.x + b->inputs[w.dstPort].local.x,
				b->pos.y + b->inputs[w.dstPort].local.y);
			r.SetDrawColor({210, 185, 60, 220});
			r.RenderLine(p1, p2);
		}

		// Wire-in-progress
		if (m_wireFromNode >= 0) {
			const NodeDef* a = findNode(m_wireFromNode);
			if (a && m_wireFromPort < (int)a->outputs.size()) {
				auto p1 = _GraphWorldToScreen(rect,
					a->pos.x + a->outputs[m_wireFromPort].local.x,
					a->pos.y + a->outputs[m_wireFromPort].local.y);
				float mx, my; SDL::GetMouseState(mx, my);
				r.SetDrawColor({255, 220, 60, 200});
				r.RenderLine(p1, {mx, my});
			}
		}

		// Nodes
		for (const auto& n : g.nodes) {
			auto tl = _GraphWorldToScreen(rect, n.pos.x, n.pos.y);
			SDL::FRect nr{tl.x, tl.y, n.size.x * m_graphZoom, n.size.y * m_graphZoom};
			SDL::Color body;
			switch (n.kind) {
				case NodeKind::Event:     body = {50, 110, 160, 240}; break;
				case NodeKind::Script:    body = {110, 80, 160, 240}; break;
				case NodeKind::Dialog:    body = {80, 140, 90, 240};  break;
				case NodeKind::Cinematic: body = {160, 90, 140, 240}; break;
				case NodeKind::Wait:      body = {140, 130, 60, 240}; break;
				case NodeKind::Branch:    body = {160, 70, 70, 240};  break;
			}
			r.SetDrawColor(body); r.RenderFillRect(nr);
			SDL::Color border = (n.id == m_selectedNode) ? SDL::Color{255, 220, 60, 255}
			                                              : SDL::Color{0, 0, 0, 200};
			r.SetDrawColor(border); r.RenderRect(nr);
			// Title bar
			SDL::FRect tb{nr.x, nr.y, nr.w, 16.f * m_graphZoom};
			SDL::Color tbc = body; tbc.r = (Uint8)SDL::Min(255, tbc.r + 30);
			tbc.g = (Uint8)SDL::Min(255, tbc.g + 30); tbc.b = (Uint8)SDL::Min(255, tbc.b + 30);
			r.SetDrawColor(tbc); r.RenderFillRect(tb);
			// Ports
			float pr = 5.f * m_graphZoom;
			for (const auto& p : n.inputs) {
				auto sp = _GraphWorldToScreen(rect, n.pos.x + p.local.x, n.pos.y + p.local.y);
				r.SetDrawColor({60, 140, 220, 255});
				r.RenderFillRect(SDL::FRect{sp.x - pr, sp.y - pr, pr*2, pr*2});
			}
			for (const auto& p : n.outputs) {
				auto sp = _GraphWorldToScreen(rect, n.pos.x + p.local.x, n.pos.y + p.local.y);
				r.SetDrawColor({45, 195, 110, 255});
				r.RenderFillRect(SDL::FRect{sp.x - pr, sp.y - pr, pr*2, pr*2});
			}
		}
	}

	void _OnGraphEvent(SDL::Event& ev) {
		SDL::FRect rect{};
		if (auto* cr = ecs_context.Get<SDL::UI::ComputedRect>(eGraphCanvas))
			rect = cr->screen;
		if (rect.w <= 0.f) return;
		if (project.graphs.empty()) return;
		auto& g = project.graphs[project.activeGraph];

		auto hitPort = [&](float mx, float my, bool wantOutput, int& outNode, int& outPort) -> bool {
			float pr = 6.f * m_graphZoom;
			for (const auto& n : g.nodes) {
				const auto& ports = wantOutput ? n.outputs : n.inputs;
				for (int i = 0; i < (int)ports.size(); ++i) {
					auto sp = _GraphWorldToScreen(rect,
						n.pos.x + ports[i].local.x, n.pos.y + ports[i].local.y);
					if (std::abs(mx - sp.x) <= pr && std::abs(my - sp.y) <= pr) {
						outNode = n.id; outPort = i;
						return true;
					}
				}
			}
			return false;
		};
		auto hitNode = [&](float mx, float my, int& outIdx) -> bool {
			for (int i = (int)g.nodes.size() - 1; i >= 0; --i) {
				const auto& n = g.nodes[i];
				auto tl = _GraphWorldToScreen(rect, n.pos.x, n.pos.y);
				SDL::FRect nr{tl.x, tl.y, n.size.x * m_graphZoom, n.size.y * m_graphZoom};
				if (nr.Contains(SDL::FPoint{mx, my})) { outIdx = i; return true; }
			}
			return false;
		};

		if (ev.type == SDL::EVENT_MOUSE_WHEEL) {
			float mx = ev.wheel.mouse_x, my = ev.wheel.mouse_y;
			if (!rect.Contains(SDL::FPoint{mx, my})) return;
			float old = m_graphZoom;
			m_graphZoom = std::clamp(m_graphZoom * (ev.wheel.y > 0 ? 1.2f : 1.f/1.2f), 0.2f, 4.f);
			// Keep mouse anchor stable.
			float k = m_graphZoom / old;
			m_graphViewX += (mx - rect.x) / old * (1.f - 1.f/k);
			m_graphViewY += (my - rect.y) / old * (1.f - 1.f/k);
			return;
		}
		if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN) {
			float mx = ev.button.x, my = ev.button.y;
			if (!rect.Contains(SDL::FPoint{mx, my})) return;
			if (ev.button.button == SDL::BUTTON_MIDDLE) {
				m_graphPanning = true;
				m_graphDragOrigin = {mx, my};
				m_graphDragNodeStart = {m_graphViewX, m_graphViewY};
				return;
			}
			if (ev.button.button == SDL::BUTTON_LEFT) {
				int sn, sp;
				if (hitPort(mx, my, /*wantOutput=*/true, sn, sp)) {
					m_wireFromNode = sn; m_wireFromPort = sp;
					return;
				}
				int idx;
				if (hitNode(mx, my, idx)) {
					m_selectedNode = g.nodes[idx].id;
					m_graphNodeDrag = true;
					m_graphDragOrigin = {mx, my};
					m_graphDragNodeStart = g.nodes[idx].pos;
					ui.SetText(eGraphStatus,
						std::format("Selected: {} #{}", g.nodes[idx].title, g.nodes[idx].id));
					return;
				}
				m_selectedNode = -1;
			}
		} else if (ev.type == SDL::EVENT_MOUSE_BUTTON_UP) {
			float mx = ev.button.x, my = ev.button.y;
			if (ev.button.button == SDL::BUTTON_MIDDLE) m_graphPanning = false;
			if (ev.button.button == SDL::BUTTON_LEFT) {
				if (m_wireFromNode >= 0) {
					int dn, dp;
					if (hitPort(mx, my, /*wantOutput=*/false, dn, dp)) {
						NodeWire w; w.srcNode=m_wireFromNode; w.srcPort=m_wireFromPort;
						w.dstNode=dn; w.dstPort=dp;
						g.wires.push_back(w);
					}
					m_wireFromNode = -1; m_wireFromPort = -1;
				}
				m_graphNodeDrag = false;
			}
		} else if (ev.type == SDL::EVENT_MOUSE_MOTION) {
			float mx = ev.motion.x, my = ev.motion.y;
			if (m_graphPanning) {
				m_graphViewX = m_graphDragNodeStart.x - (mx - m_graphDragOrigin.x)/m_graphZoom;
				m_graphViewY = m_graphDragNodeStart.y - (my - m_graphDragOrigin.y)/m_graphZoom;
			} else if (m_graphNodeDrag && m_selectedNode >= 0) {
				for (auto& n : g.nodes) {
					if (n.id != m_selectedNode) continue;
					n.pos.x = m_graphDragNodeStart.x + (mx - m_graphDragOrigin.x)/m_graphZoom;
					n.pos.y = m_graphDragNodeStart.y + (my - m_graphDragOrigin.y)/m_graphZoom;
					break;
				}
			}
		} else if (ev.type == SDL::EVENT_KEY_DOWN) {
			if (ev.key.key == SDL::KEYCODE_DELETE && m_selectedNode >= 0) {
				int id = m_selectedNode;
				g.nodes.erase(std::remove_if(g.nodes.begin(), g.nodes.end(),
					[id](const NodeDef& n){ return n.id == id; }), g.nodes.end());
				g.wires.erase(std::remove_if(g.wires.begin(), g.wires.end(),
					[id](const NodeWire& w){ return w.srcNode==id || w.dstNode==id; }), g.wires.end());
				m_selectedNode = -1;
			}
		}
	}

	// Asynchronous run: walks the graph starting from every Event node and
	// schedules block execution on a worker thread. Dialog/Wait nodes yield
	// for real time; Script nodes run their Lua snippet on the main thread
	// (because Lua state isn't thread-safe).
	void _GraphRunAsync() {
		if (project.graphs.empty()) return;
		// Snapshot the graph; the worker walks the snapshot — concurrent edits
		// in the UI keep working without locks.
		auto snap = std::make_shared<NodeGraphDoc>(project.graphs[project.activeGraph]);
		auto self = this;
		std::thread([self, snap]{
			try {
				self->_GraphWalk(*snap);
			} catch (...) {
				self->_ConsolePush("[graph] exception during async run");
			}
		}).detach();
		_ConsolePush("[graph] running async");
	}

	void _GraphWalk(const NodeGraphDoc& g) {
		auto findNode = [&](int id) -> const NodeDef* {
			for (const auto& n : g.nodes) if (n.id == id) return &n;
			return nullptr;
		};
		auto runNode = [&](const NodeDef& n) {
			switch (n.kind) {
				case NodeKind::Event:
					_ConsolePush(std::format("[event] {}", n.title));
					break;
				case NodeKind::Script: {
					// Lua state is not thread-safe — hop to main via a flag.
					std::string code = n.body;
					auto done = std::make_shared<std::atomic<bool>>(false);
					{
						std::lock_guard<std::mutex> lk(m_pendingMutex);
						m_pendingMain.push_back([this, code, done]{
							_LuaRun(code, "graph-script");
							done->store(true);
						});
					}
					while (!done->load())
						std::this_thread::sleep_for(std::chrono::milliseconds(5));
					break;
				}
				case NodeKind::Dialog:
					_ConsolePush(std::string("[dialog] ") + n.body);
					std::this_thread::sleep_for(std::chrono::milliseconds(800));
					break;
				case NodeKind::Cinematic:
					_ConsolePush(std::string("[cine] play ") + n.body);
					std::this_thread::sleep_for(std::chrono::milliseconds(500));
					break;
				case NodeKind::Wait: {
					float secs = 1.f;
					try { secs = std::stof(n.body); } catch (...) {}
					std::this_thread::sleep_for(std::chrono::milliseconds((int)(secs * 1000)));
					break;
				}
				case NodeKind::Branch:
					_ConsolePush(std::string("[branch] ") + n.body);
					break;
			}
		};

		// BFS from each Event node, exec each node, then fan out through wires.
		std::queue<int> q;
		std::unordered_set<int> visited;
		for (const auto& n : g.nodes) if (n.kind == NodeKind::Event) q.push(n.id);

		while (!q.empty()) {
			int id = q.front(); q.pop();
			if (!visited.insert(id).second) continue;
			const NodeDef* n = findNode(id);
			if (!n) continue;
			runNode(*n);
			for (const auto& w : g.wires)
				if (w.srcNode == id) q.push(w.dstNode);
		}
		_ConsolePush("[graph] done");
	}

	// =========================================================================
	// Test workspace impl (Mario-like physics)
	// =========================================================================

	void _TestReset() {
		m_player.pos  = {64.f, 64.f};
		m_player.vel  = {0.f, 0.f};
		m_player.onGround = false;
		m_player.alive = true;
		m_testCollisionLayer = -1;
		// Default to layer 0 unless an "objects" layer is named Collision.
		for (int i = 0; i < (int)map.layers.size(); ++i) {
			if (map.layers[i].name == "Collision") { m_testCollisionLayer = i; break; }
		}
		if (m_testCollisionLayer < 0) m_testCollisionLayer = 0;
	}

	bool _TestSolidAt(float wx, float wy) const {
		// A world-point is solid if any tile layer (≥ collision layer) has a
		// non-empty tile under it, OR a Rect/Polygon object on an object layer
		// contains it.
		int tx, ty;
		map.WorldToTile(wx, wy, tx, ty);
		for (int li = 0; li < (int)map.layers.size(); ++li) {
			const auto& L = map.layers[li];
			if (!L.visible) continue;
			if (L.type == LayerType::Tile) {
				if (map.GetTile(li, tx, ty) != EMPTY_TILE) return true;
			} else {
				for (const auto& obj : L.objects) {
					if (obj.type == ObjectType::Rect &&
					    wx >= obj.x && wx <= obj.x + obj.w &&
					    wy >= obj.y && wy <= obj.y + obj.h)
						return true;
				}
			}
		}
		return false;
	}

	void _TestUpdate(float dt) {
		if (m_activeTab != WsTest) return;

		// Drain any pending main-thread closures queued by graph workers.
		_DrainPendingMain();

		// Optional Lua tick hook.
		_LuaCallGlobal("on_tick", dt);

		if (!m_testPlaying) return;

		// Read held keys for input.
		bool left  = m_testKeysHeld.count(SDL::KEYCODE_LEFT)  || m_testKeysHeld.count(SDL::KEYCODE_A);
		bool right = m_testKeysHeld.count(SDL::KEYCODE_RIGHT) || m_testKeysHeld.count(SDL::KEYCODE_D);
		bool jump  = m_testKeysHeld.count(SDL::KEYCODE_SPACE) || m_testKeysHeld.count(SDL::KEYCODE_W);

		// Horizontal accel + drag.
		const float ACC = 1400.f, MAX_V = 220.f, GRAV = 1300.f, JUMP_V = 460.f;
		float ax = 0.f;
		if (left)  ax -= ACC;
		if (right) ax += ACC;
		m_player.vel.x += ax * dt;
		// Drag when no input
		if (!left && !right) m_player.vel.x *= std::max(0.f, 1.f - 8.f * dt);
		m_player.vel.x = std::clamp(m_player.vel.x, -MAX_V, MAX_V);

		// Gravity + jump
		m_player.vel.y += GRAV * dt;
		if (jump && m_player.onGround) {
			m_player.vel.y = -JUMP_V;
			m_player.onGround = false;
		}

		// Integrate + collide axis-by-axis.
		auto sweepAxis = [&](float dx, float dy, bool xAxis) {
			SDL::FPoint half = {m_player.size.x * 0.5f, m_player.size.y * 0.5f};
			float steps = std::ceil(std::max(std::abs(dx), std::abs(dy)) / 4.f);
			if (steps < 1.f) steps = 1.f;
			float sx = dx / steps, sy = dy / steps;
			for (int s = 0; s < (int)steps; ++s) {
				SDL::FPoint next = {m_player.pos.x + sx, m_player.pos.y + sy};
				// 4 corners + midpoints.
				bool blocked = false;
				for (int cy = -1; cy <= 1; ++cy)
				for (int cx = -1; cx <= 1; ++cx) {
					float wx = next.x + cx * half.x * 0.95f;
					float wy = next.y + cy * half.y * 0.95f;
					if (_TestSolidAt(wx, wy)) { blocked = true; break; }
				}
				if (blocked) {
					if (xAxis) m_player.vel.x = 0.f;
					else {
						if (sy > 0.f) m_player.onGround = true;
						m_player.vel.y = 0.f;
					}
					return;
				}
				m_player.pos = next;
			}
			if (!xAxis) m_player.onGround = false; // override only when no collision
		};
		// First X, then Y (classic platformer order).
		sweepAxis(m_player.vel.x * dt, 0.f, true);
		// Pre-check Y to set onGround correctly even with 0 vel.y when standing
		m_player.onGround = false;
		sweepAxis(0.f, m_player.vel.y * dt, false);
	}

	void _RenderTest(SDL::RendererRef r, SDL::FRect rect) {
		// Reuse the map renderer with a fixed pinned camera following the
		// player. We update state.mapRect/viewX/viewY temporarily.
		auto saveRect  = state.mapRect;
		auto saveViewX = state.viewX, saveViewY = state.viewY;
		auto saveZoom  = state.zoom;
		state.mapRect  = rect;
		state.zoom     = 1.0f;
		state.viewX    = m_player.pos.x - rect.w * 0.5f;
		state.viewY    = m_player.pos.y - rect.h * 0.5f;
		_RenderMap(r, rect);

		// Draw the player
		auto pp = WorldToScreen(m_player.pos.x - m_player.size.x*0.5f,
		                        m_player.pos.y - m_player.size.y*0.5f);
		r.SetDrawColor({230, 90, 70, 255});
		r.RenderFillRect(SDL::FRect{pp.x, pp.y, m_player.size.x, m_player.size.y});
		r.SetDrawColor({0, 0, 0, 200});
		r.RenderRect(SDL::FRect{pp.x, pp.y, m_player.size.x, m_player.size.y});

		state.mapRect = saveRect;
		state.viewX = saveViewX; state.viewY = saveViewY;
		state.zoom = saveZoom;

		ui.SetText(eTestStatus,
			std::format("{}  pos ({:.0f}, {:.0f})  vel ({:.0f}, {:.0f})  ground={}",
				m_testPlaying ? "PLAY" : "PAUSE",
				m_player.pos.x, m_player.pos.y,
				m_player.vel.x, m_player.vel.y,
				m_player.onGround));
	}

	void _OnTestEvent(SDL::Event& ev) {
		if (ev.type == SDL::EVENT_KEY_DOWN) m_testKeysHeld.insert(ev.key.key);
		if (ev.type == SDL::EVENT_KEY_UP)   m_testKeysHeld.erase (ev.key.key);
		if (ev.type == SDL::EVENT_MOUSE_BUTTON_DOWN
		    && ev.button.button == SDL::BUTTON_LEFT) {
			// Place the player at the click position (handy for quick testing).
			SDL::FRect rect{};
			if (auto* cr = ecs_context.Get<SDL::UI::ComputedRect>(eTestCanvas))
				rect = cr->screen;
			float wx = ev.button.x - rect.x + (m_player.pos.x - rect.w * 0.5f);
			float wy = ev.button.y - rect.y + (m_player.pos.y - rect.h * 0.5f);
			m_player.pos = {wx, wy};
			m_player.vel = {0.f, 0.f};
		}
	}

	// =========================================================================
	// Cross-workspace plumbing
	// =========================================================================

	// Queue closures that must run on the main thread (Lua calls from graph
	// worker threads, primarily). Protected by m_pendingMutex.
	std::vector<std::function<void()>> m_pendingMain;
	std::mutex                         m_pendingMutex;
	std::mutex                         m_consoleMutex;

	void _DrainPendingMain() {
		std::vector<std::function<void()>> drained;
		{
			std::lock_guard<std::mutex> lk(m_pendingMutex);
			drained.swap(m_pendingMain);
		}
		for (auto& fn : drained) fn();
	}

	// =========================================================================
	// File operations
	// =========================================================================

	void _NewMap()    { state.pendingNew = true; }
	void _DoNewMap() {
		map.Init(); state = EditorState{}; ur = UndoRedo{};
		m_syncedTs = -2;
		if (eMapTileW != SDL::ECS::NullEntity) {
			ui.SetValue(eMapTileW, float(map.tileW));
			ui.SetValue(eMapTileH, float(map.tileH));
		}
		_UpdateTitle();
	}

	void _OpenMap() {
		static const SDL::DialogFileFilter kF[] = {
			{"Tile Map (*.xml)", "xml"}, {"All Files", "*"}
		};
		SDL::ShowOpenFileDialog(
			[](void* ud, const char* const* lst, int) {
				if (lst && lst[0])
					static_cast<Main*>(ud)->state.pendingOpenPath = lst[0];
			}, this, window, kF);
	}
	void _DoOpenMap(const std::string& path) {
		TileMap nm;
		if (!LoadMap(nm, path)) return;
		map = std::move(nm);
		state = EditorState{}; ur = UndoRedo{};
		m_syncedTs = -2;
		if (eMapTileW != SDL::ECS::NullEntity) {
			ui.SetValue(eMapTileW, float(map.tileW));
			ui.SetValue(eMapTileH, float(map.tileH));
		}
		for (size_t i = 0; i < map.tilesets.size(); ++i) {
			auto& ts = map.tilesets[i];
			ts.key = "tileset_" + std::to_string(i);
			if (!ts.path.empty()) ui.LoadTexture(ts.key, ts.path);
		}
		_UpdateTitle();
		if (!map.tilesets.empty())
			ui.SetText(eTilesetName, map.tilesets[0].name);
	}

	void _SaveMap() {
		if (map.filePath.empty()) { _SaveMapAs(); return; }
		SaveMap(map, map.filePath);
		map.dirty = false; _UpdateTitle();
	}
	void _SaveMapAs() {
		static const SDL::DialogFileFilter kF[] = {
			{"Tile Map (*.xml)", "xml"}, {"All Files", "*"}
		};
		SDL::ShowSaveFileDialog(
			[](void* ud, const char* const* lst, int) {
				if (lst && lst[0])
					static_cast<Main*>(ud)->state.pendingSavePath = lst[0];
			}, this, window, kF);
	}

	void _ImportTileset() {
		static const SDL::DialogFileFilter kF[] = {
			{"Images (*.png;*.jpg)", "png;jpg;bmp"}, {"All Files", "*"}
		};
		SDL::ShowOpenFileDialog(
			[](void* ud, const char* const* lst, int) {
				if (lst && lst[0])
					static_cast<Main*>(ud)->state.pendingTilesetPath = lst[0];
			}, this, window, kF);
	}
	void _DoImportTileset(const std::string& path) {
		TilesetDef ts;
		// Derive name from filename
		std::string fname = path.substr(path.rfind('/') + 1);
		auto dot = fname.rfind('.');
		ts.name = (dot != std::string::npos) ? fname.substr(0, dot) : fname;
		ts.path = path;
		ts.tileW = map.tileW; ts.tileH = map.tileH;
		ts.key   = "tileset_" + std::to_string(map.tilesets.size());
		// firstGid = highest used + tileCount
		TileID nextGid = 1;
		for (const auto& e : map.tilesets)
			nextGid = SDL::Max(nextGid, (TileID)(e.firstGid + e.tileCount));
		ts.firstGid = nextGid;
		ts.imageW   = 0;   // will be resolved in Iterate() once texture loads
		ui.LoadTexture(ts.key, path);
		map.tilesets.push_back(std::move(ts));
		state.activeTileset = (int)map.tilesets.size() - 1;
		ui.SetText(eTilesetName, map.tilesets.back().name);
	}

	// =========================================================================
	// Undo / Redo
	// =========================================================================

	void _Undo() {
		if (!ur.CanUndo()) return;
		Command cmd = ur.PopUndo();
		Command rev;
		for (auto it = cmd.changes.rbegin(); it != cmd.changes.rend(); ++it) {
			map.SetTile(it->layer, it->x, it->y, it->oldId);
			rev.changes.push_back({it->layer, it->x, it->y, it->newId, it->oldId});
		}
		ur.PushRedo(std::move(rev));
	}
	void _Redo() {
		if (!ur.CanRedo()) return;
		Command cmd = ur.PopRedo();
		Command rev;
		for (auto it = cmd.changes.rbegin(); it != cmd.changes.rend(); ++it) {
			map.SetTile(it->layer, it->x, it->y, it->newId);
			rev.changes.push_back({it->layer, it->x, it->y, it->oldId, it->newId});
		}
		ur.Push(std::move(rev));
	}

	// =========================================================================
	// Title / status
	// =========================================================================

	void _UpdateTitle() {
		std::string t = std::format("SDL3pp - Tile Editor {} - {}{}",
			TILE_EDITOR_VERSION, map.name, map.dirty ? " *" : "");
		window.SetTitle(t.c_str());
	}

	void _UpdateStatus(float /*dt*/) {
		float mx, my;
		SDL::GetMouseState(mx, my);
		int tx = -1, ty = -1;
		if (mx >= state.mapRect.x && mx < state.mapRect.x + state.mapRect.w &&
			my >= state.mapRect.y && my < state.mapRect.y + state.mapRect.h)
			ScreenToTile(mx, my, tx, ty);

		static const char* const kToolNames[] = {
			"Pencil","Brush","Fill","Erase","Select",
			"ObjSelect","ObjRect","ObjEllipse","ObjPoint","ObjPolygon"
		};

		std::string tilePos = (tx >= 0) ? std::format("[{},{}]", tx, ty) : "---";

		std::string mapSize;
		if (map.infinite) {
			int chunkCount = 0;
			for (const auto& l : map.layers)
				if (l.type == LayerType::Tile) chunkCount += (int)l.chunks.size();
			mapSize = std::format("inf ({} chunks)", chunkCount);
		} else {
			mapSize = std::format("{}x{}", map.width, map.height);
		}

		const char* layerKind = _ActiveLayerIsObject() ? "Obj" : "Tile";

		std::string extra;
		if (state.tool == ToolType::ObjPolygon && !state.polyPoints.empty()) {
			extra = std::format(" | poly: {} pts (Enter=finish, Esc=cancel)",
								(int)state.polyPoints.size());
		} else if (ObjectDef* o = const_cast<Main*>(this)->_SelObj()) {
			extra = std::format(" | sel: '{}' #{}", o->name, o->id);
		}

		std::string s = std::format(
			"{} | Map {} ({}x{}px) | L {}/{} [{}] | Zoom {:.0f}% | {}{}{}",
			kToolNames[(int)state.tool],
			mapSize, map.tileW, map.tileH,
			map.activeLayer + 1, (int)map.layers.size(), layerKind,
			state.zoom * 100.f,
			tilePos,
			extra,
			map.dirty ? " [unsaved]" : ""
		);
		ui.SetText(eStatusLabel, s);

		// Update tileset name label
		if (!map.tilesets.empty() && state.activeTileset < (int)map.tilesets.size())
			ui.SetText(eTilesetName, std::format("{} [{}/{}]",
				map.tilesets[state.activeTileset].name,
				state.activeTileset + 1,
				(int)map.tilesets.size()));
	}
};

SDL3PP_DEFINE_CALLBACKS(Main)
