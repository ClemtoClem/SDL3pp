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
#include <cmath>
#include <format>
#include <queue>
#include <sstream>
#include <string>
#include <vector>

#define TILE_EDITOR_VERSION "1.0.0"

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

// ─────────────────────────────────────────────────────────────────────────────
// Palette
// ─────────────────────────────────────────────────────────────────────────────

namespace pal {
    constexpr SDL::Color BG       = { 14,  14,  20, 255};
    constexpr SDL::Color HEADER   = { 20,  20,  30, 255};
    constexpr SDL::Color PANEL    = { 18,  18,  26, 255};
    constexpr SDL::Color ACCENT   = { 70, 130, 210, 255};
    constexpr SDL::Color NEUTRAL  = { 30,  30,  40, 255};
    constexpr SDL::Color BORDER   = { 50,  52,  72, 255};
    constexpr SDL::Color WHITE    = {220, 220, 225, 255};
    constexpr SDL::Color GREY     = {130, 132, 145, 255};
    constexpr SDL::Color GREEN    = { 50, 195, 100, 255};
    constexpr SDL::Color ORANGE   = {230, 145,  30, 255};
    constexpr SDL::Color RED      = {200,  60,  50, 255};
    constexpr SDL::Color SELECTED = {255, 200,  50, 255};
    constexpr SDL::Color GRID     = { 60,  62,  80, 110};
    constexpr SDL::Color OBJ_COL  = {100, 160, 100, 180};
    constexpr SDL::Color OBJ_SEL  = {  0, 200, 255, 180};
}

// =============================================================================
// Data model
// =============================================================================

using TileID = uint16_t;
static constexpr TileID EMPTY_TILE = 0;

struct TilesetDef {
    std::string key;
    std::string name    = "Tileset";
    std::string path;
    int  tileW    = 16,   tileH   = 16;
    int  spacing  = 0,    margin  = 0;
    int  columns  = 8,    rows    = 8;
    int  tileCount= 64;
    int  imageW   = 0,    imageH  = 0;
    bool smart    = false;     ///< Auto-tile: pick variant based on neighbours
    TileID firstGid = 1;       ///< Global tile ID of the first tile
};

enum class LayerType  { Tile, Object };
enum class ObjectType { Rect, Ellipse, Point, Polygon, Tile };
enum class MapOrient  { Orthogonal, Isometric, Hexagonal };

struct ObjectDef {
    int        id       = 0;
    ObjectType type     = ObjectType::Rect;
    std::string name;
    float x = 0, y = 0, w = 32, h = 32;
    float rotation = 0.f;
    TileID tileId  = 0;
    std::vector<SDL::FPoint> points;
    bool selected  = false;
};

struct MapLayer {
    std::string          name    = "Layer";
    LayerType            type    = LayerType::Tile;
    bool                 visible = true;
    bool                 locked  = false;
    float                opacity = 1.0f;
    std::vector<TileID>  tiles;            ///< row-major, size = mapW * mapH
    std::vector<ObjectDef> objects;
};

struct TileMap {
    std::string name     = "Untitled";
    std::string filePath;
    int  width  = 20,  height = 15;
    int  tileW  = 32,  tileH  = 32;
    MapOrient orientation = MapOrient::Orthogonal;
    std::vector<TilesetDef> tilesets;
    std::vector<MapLayer>   layers;
    int  activeLayer = 0;
    bool dirty       = false;

    void Init(int w = 20, int h = 15, int tw = 32, int th = 32) {
        width = w; height = h; tileW = tw; tileH = th;
        name = "Untitled"; filePath = "";
        tilesets.clear(); layers.clear();
        MapLayer l; l.name = "Layer 1";
        l.tiles.assign(w * h, EMPTY_TILE);
        layers.push_back(std::move(l));
        activeLayer = 0; dirty = false;
    }

    TileID GetTile(int layer, int x, int y) const {
        if (layer < 0 || layer >= (int)layers.size()) return EMPTY_TILE;
        if (x < 0 || y < 0 || x >= width || y >= height) return EMPTY_TILE;
        const auto& l = layers[layer];
        if (l.type != LayerType::Tile || l.tiles.empty()) return EMPTY_TILE;
        return l.tiles[y * width + x];
    }

    bool SetTile(int layer, int x, int y, TileID id) {
        if (layer < 0 || layer >= (int)layers.size()) return false;
        if (x < 0 || y < 0 || x >= width || y >= height) return false;
        auto& l = layers[layer];
        if (l.type != LayerType::Tile || l.locked) return false;
        if ((int)l.tiles.size() != width * height)
            l.tiles.assign(width * height, EMPTY_TILE);
        l.tiles[y * width + x] = id;
        dirty = true;
        return true;
    }

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

    // Bit mask of 4-way neighbours that have the same tile ID (N=1 E=2 S=4 W=8)
    uint8_t NeighbourMask(int layer, int x, int y) const {
        TileID t = GetTile(layer, x, y);
        uint8_t m = 0;
        if (GetTile(layer, x,   y-1) == t) m |= 1;
        if (GetTile(layer, x+1, y  ) == t) m |= 2;
        if (GetTile(layer, x,   y+1) == t) m |= 4;
        if (GetTile(layer, x-1, y  ) == t) m |= 8;
        return m;
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
// Editor state
// =============================================================================

enum class ToolType { Pencil, Brush, Fill, Erase, Select };

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
    float  tsScrollY     = 0.f;
    float  tsScale       = 1.0f;

    // Object layer drag
    bool      objDrag  = false;
    SDL::FPoint objStart = {};
    int       nextObjId  = 1;

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

static void SaveMap(const TileMap& map, const std::string& path) {
    auto doc   = std::make_shared<SDL::XMLDataDocument>();
    auto root  = SDL::ObjectDataNode::Make();
    auto mNode = SDL::ObjectDataNode::Make();

    // <map attributes>
    {
        auto a = SDL::ObjectDataNode::Make();
        a->set("version",     SDL::StringDataNode::Build("1.0"));
        a->set("name",        SDL::StringDataNode::Build(map.name));
        a->set("width",       SDL::S32DataNode::Build(map.width));
        a->set("height",      SDL::S32DataNode::Build(map.height));
        a->set("tilewidth",   SDL::S32DataNode::Build(map.tileW));
        a->set("tileheight",  SDL::S32DataNode::Build(map.tileH));
        const char* ori = "orthogonal";
        if (map.orientation == MapOrient::Isometric) ori = "isometric";
        if (map.orientation == MapOrient::Hexagonal) ori = "hexagonal";
        a->set("orientation", SDL::StringDataNode::Build(ori));
        mNode->set("@attributes", a);
    }

    // <tileset> entries
    for (const auto& ts : map.tilesets) {
        auto tn = SDL::ObjectDataNode::Make();
        auto a  = SDL::ObjectDataNode::Make();
        a->set("name",       SDL::StringDataNode::Build(ts.name));
        a->set("source",     SDL::StringDataNode::Build(ts.path));
        a->set("firstgid",   SDL::S32DataNode::Build((int)ts.firstGid));
        a->set("tilewidth",  SDL::S32DataNode::Build(ts.tileW));
        a->set("tileheight", SDL::S32DataNode::Build(ts.tileH));
        a->set("spacing",    SDL::S32DataNode::Build(ts.spacing));
        a->set("margin",     SDL::S32DataNode::Build(ts.margin));
        a->set("columns",    SDL::S32DataNode::Build(ts.columns));
        a->set("tilecount",  SDL::S32DataNode::Build(ts.tileCount));
        a->set("smart",      SDL::BoolDataNode::Build(ts.smart));
        tn->set("@attributes", a);
        mNode->set("tileset", tn);   // merges to array for multiple tilesets
    }

    // <layer> / <objectgroup> entries
    for (const auto& layer : map.layers) {
        auto ln = SDL::ObjectDataNode::Make();
        auto a  = SDL::ObjectDataNode::Make();
        a->set("name",    SDL::StringDataNode::Build(layer.name));
        a->set("visible", SDL::BoolDataNode::Build(layer.visible));
        a->set("locked",  SDL::BoolDataNode::Build(layer.locked));
        a->set("opacity", SDL::F32DataNode::Build(layer.opacity));
        ln->set("@attributes", a);

        if (layer.type == LayerType::Tile) {
            // Encode tiles as comma-separated values
            std::string csv;
            csv.reserve(layer.tiles.size() * 3);
            for (size_t i = 0; i < layer.tiles.size(); ++i) {
                if (i) csv += ',';
                csv += std::to_string((int)layer.tiles[i]);
            }
            ln->set("data", SDL::StringDataNode::Build(csv));
            mNode->set("layer", ln);
        } else {
            for (const auto& obj : layer.objects) {
                auto on = SDL::ObjectDataNode::Make();
                auto oa = SDL::ObjectDataNode::Make();
                oa->set("id",       SDL::S32DataNode::Build(obj.id));
                oa->set("name",     SDL::StringDataNode::Build(obj.name));
                oa->set("x",        SDL::F32DataNode::Build(obj.x));
                oa->set("y",        SDL::F32DataNode::Build(obj.y));
                oa->set("width",    SDL::F32DataNode::Build(obj.w));
                oa->set("height",   SDL::F32DataNode::Build(obj.h));
                oa->set("rotation", SDL::F32DataNode::Build(obj.rotation));
                const char* tp = "rect";
                if (obj.type == ObjectType::Ellipse) tp = "ellipse";
                if (obj.type == ObjectType::Point)   tp = "point";
                if (obj.type == ObjectType::Polygon) tp = "polygon";
                if (obj.type == ObjectType::Tile)    tp = "tile";
                oa->set("type", SDL::StringDataNode::Build(tp));
                if (obj.type == ObjectType::Tile)
                    oa->set("tileid", SDL::S32DataNode::Build((int)obj.tileId));
                on->set("@attributes", oa);
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
        map.name   = XmlStr(*ma, "name",        "Untitled");
        map.width  = XmlInt(*ma, "width",        20);
        map.height = XmlInt(*ma, "height",       15);
        map.tileW  = XmlInt(*ma, "tilewidth",    32);
        map.tileH  = XmlInt(*ma, "tileheight",   32);
        auto ori   = XmlStr(*ma, "orientation",  "orthogonal");
        if      (ori == "isometric")  map.orientation = MapOrient::Isometric;
        else if (ori == "hexagonal")  map.orientation = MapOrient::Hexagonal;
        else                          map.orientation = MapOrient::Orthogonal;
    }
    map.filePath = path;
    map.tilesets.clear();
    map.layers.clear();

    XmlEach(*mn, "tileset", [&](const SDL::ObjectDataNode& tn) {
        auto ta = std::dynamic_pointer_cast<SDL::ObjectDataNode>(tn.get("@attributes"));
        if (!ta) return;
        TilesetDef ts;
        ts.name      = XmlStr(*ta, "name", "Tileset");
        ts.path      = XmlStr(*ta, "source");
        ts.firstGid  = (TileID)XmlInt(*ta, "firstgid",  1);
        ts.tileW     = XmlInt(*ta, "tilewidth",  16);
        ts.tileH     = XmlInt(*ta, "tileheight", 16);
        ts.spacing   = XmlInt(*ta, "spacing",     0);
        ts.margin    = XmlInt(*ta, "margin",      0);
        ts.columns   = XmlInt(*ta, "columns",     8);
        ts.tileCount = XmlInt(*ta, "tilecount",  64);
        ts.smart     = XmlBool(*ta, "smart");
        ts.key = "tileset_" + std::to_string(map.tilesets.size());
        map.tilesets.push_back(std::move(ts));
    });

    XmlEach(*mn, "layer", [&](const SDL::ObjectDataNode& ln) {
        auto la = std::dynamic_pointer_cast<SDL::ObjectDataNode>(ln.get("@attributes"));
        MapLayer layer; layer.type = LayerType::Tile;
        if (la) {
            layer.name    = XmlStr(*la, "name",    "Layer");
            layer.visible = XmlBool(*la, "visible", true);
            layer.locked  = XmlBool(*la, "locked",  false);
            layer.opacity = XmlFloat(*la, "opacity", 1.f);
        }
        layer.tiles.assign(map.width * map.height, EMPTY_TILE);
        auto dn = ln.get("data");
        if (dn) {
            std::string csv;
            if (auto s = std::dynamic_pointer_cast<SDL::StringDataNode>(dn))
                csv = s->getValue();
            std::istringstream ss(csv);
            std::string tok;
            int i = 0;
            while (std::getline(ss, tok, ',') && i < (int)layer.tiles.size())
                try { layer.tiles[i++] = (TileID)std::stoi(tok); } catch (...) { ++i; }
        }
        map.layers.push_back(std::move(layer));
    });

    XmlEach(*mn, "objectgroup", [&](const SDL::ObjectDataNode& ln) {
        auto la = std::dynamic_pointer_cast<SDL::ObjectDataNode>(ln.get("@attributes"));
        MapLayer layer; layer.type = LayerType::Object;
        if (la) {
            layer.name    = XmlStr(*la, "name",    "Objects");
            layer.visible = XmlBool(*la, "visible", true);
            layer.locked  = XmlBool(*la, "locked",  false);
            layer.opacity = XmlFloat(*la, "opacity", 1.f);
        }
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
            layer.objects.push_back(std::move(obj));
        });
        map.layers.push_back(std::move(layer));
    });

    if (map.layers.empty()) {
        MapLayer l; l.name = "Layer 1";
        l.tiles.assign(map.width * map.height, EMPTY_TILE);
        map.layers.push_back(std::move(l));
    }
    map.activeLayer = 0;
    map.dirty       = false;
    return true;
}

// =============================================================================
// Flood fill
// =============================================================================

static Command FloodFill(TileMap& map, int layer, int startX, int startY,
                         TileID newId, int maxTiles = 50000) {
    Command cmd;
    if (layer < 0 || layer >= (int)map.layers.size()) return cmd;
    auto& l = map.layers[layer];
    if (l.type != LayerType::Tile || l.locked) return cmd;
    if ((int)l.tiles.size() != map.width * map.height)
        l.tiles.assign(map.width * map.height, EMPTY_TILE);

    TileID target = map.GetTile(layer, startX, startY);
    if (target == newId) return cmd;

    std::vector<bool> visited(map.width * map.height, false);
    std::queue<std::pair<int,int>> q;
    q.push({startX, startY});

    while (!q.empty() && (int)cmd.changes.size() < maxTiles) {
        auto [x, y] = q.front(); q.pop();
        if (x < 0 || y < 0 || x >= map.width || y >= map.height) continue;
        int idx = y * map.width + x;
        if (visited[idx] || l.tiles[idx] != target) continue;
        visited[idx] = true;
        cmd.changes.push_back({layer, x, y, target, newId});
        l.tiles[idx] = newId;
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
    static constexpr SDL::Point kWinSz  = {1400, 820};
    static constexpr int kMaxLayers = 32;
    static constexpr int kLeftW     = 172;
    static constexpr int kRightW    = 212;
    static constexpr int kToolCount = 5;

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

    SDL::ECS::World  world;
    SDL::UI::System  ui{ world, renderer, mixer, pool_ui };
    SDL::FrameTimer  frameTimer{ 60.f };

    // ── Editor data ───────────────────────────────────────────────────────────
    TileMap     map;
    EditorState state;
    UndoRedo    ur;

    // ── UI entity IDs ─────────────────────────────────────────────────────────
    SDL::ECS::EntityId eMapCanvas      = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eTilesetCanvas  = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eStatusLabel    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eProjectBtns    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eTilesetName    = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eTileInfo       = SDL::ECS::NullEntity;
    SDL::ECS::EntityId eLayerContent   = SDL::ECS::NullEntity;

    SDL::ECS::EntityId toolBtns[kToolCount] = {};

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
        SDL::SetAppMetadata("SDL3pp Tile Editor", "1.0", "com.example.tile_editor");
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
        _LoadResources();
        _BuildUI();
    }
    ~Main() { resources.ReleaseAll(); }

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
                if (key == SDL::KEYCODE_P) _SetTool(ToolType::Pencil);
                if (key == SDL::KEYCODE_B) _SetTool(ToolType::Brush);
                if (key == SDL::KEYCODE_F) _SetTool(ToolType::Fill);
                if (key == SDL::KEYCODE_E) _SetTool(ToolType::Erase);
                if (key == SDL::KEYCODE_S) _SetTool(ToolType::Select);
                if (key == SDL::KEYCODE_G) state.showGrid = !state.showGrid;
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

        _UpdateStatus(dt);
        _UpdateLayerSlots();

        renderer.SetDrawColor(pal::BG);
        renderer.RenderClear();
        ui.Iterate(dt);
        renderer.Present();
        frameTimer.End();
        return SDL::APP_CONTINUE;
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
    }

    // =========================================================================
    // UI construction
    // =========================================================================

    void _BuildUI() {
        ui.Column("root", 0.f, 0.f)
            .BgColor(pal::BG)
            .WithStyle([](auto& s){
                s.borders = SDL::FBox(0.f);
                s.radius  = SDL::FCorners(0.f);
            })
            .W(SDL::UI::Value::Ww(100.f))
            .H(SDL::UI::Value::Wh(100.f))
            .Padding(0.f)
            .Children(_BuildToolbar(), _BuildMainContent(), _BuildStatusBar())
            .AsRoot();
    }

    // ── Toolbar ──────────────────────────────────────────────────────────────

    SDL::ECS::EntityId _BuildToolbar() {
        auto bar = ui.Row("toolbar", 6.f, 0.f)
            .W(SDL::UI::Value::Ww(100.f)).H(42.f)
            .PaddingH(10.f).PaddingV(5.f)
            .BgColor(pal::HEADER)
            .WithStyle([](auto& s){
                s.borders     = SDL::FBox(0.f, 0.f, 1.f, 0.f);
                s.bdColor = pal::BORDER;
                s.radius      = SDL::FCorners(0.f);
            });

        // Helper: creates a toolbar button
        auto mkBtn = [&](const char* id, const char* lbl, SDL::Color c,
                         std::function<void()> cb) -> SDL::ECS::EntityId {
            return ui.Button(id, lbl)
                .W(SDL::UI::Value::Auto()).H(30).PaddingH(8).PaddingV(4)
                .Style(SDL::UI::Theme::PrimaryButton(c))
                .FontKey(res_key::FONT).FontSize(13.f)
                .WithStyle([](auto& s){ s.radius = SDL::FCorners(4.f); })
                .ClickSoundKey(res_key::CLICK)
                .OnClick(std::move(cb));
        };
        // Vertical divider
        auto mkSep = [&](const char* id) -> SDL::ECS::EntityId {
            return ui.Container(id).W(1).H(28)
                .WithStyle([](auto& s){
                    s.bgColor = pal::BORDER;
                    s.borders = SDL::FBox(0.f);
                    s.radius  = SDL::FCorners(0.f);
                });
        };

        // ── File ops
        bar.Children(
            mkBtn("btn_new",     "New",     pal::NEUTRAL, [this]{ _NewMap();   }),
            mkBtn("btn_open",    "Open",    pal::NEUTRAL, [this]{ _OpenMap();  }),
            mkBtn("btn_save",    "Save",    pal::NEUTRAL, [this]{ _SaveMap();  }),
            mkBtn("btn_save_as", "Save As", pal::NEUTRAL, [this]{ _SaveMapAs();}),
            mkSep("sep1")
        );

        // ── Project actions (Import, Add layers …)
        eProjectBtns = ui.Row("proj_btns", 6.f, 0.f)
            .WithStyle([](auto& s){
                s.bgColor = SDL::Color(0,0,0,0);
                s.borders = SDL::FBox(0.f);
                s.radius  = SDL::FCorners(0.f);
            })
            .WithLayout([](auto& l){
                l.padding = SDL::FBox(0.f);
                l.margin  = SDL::FBox(0.f);
            })
            .Children(
                mkBtn("btn_import",    "Import Tileset", pal::NEUTRAL, [this]{ _ImportTileset(); }),
                mkBtn("btn_add_layer", "+Tile Layer",    pal::NEUTRAL, [this]{ _AddTileLayer();  }),
                mkBtn("btn_add_obj",   "+Objects",       pal::NEUTRAL, [this]{ _AddObjectLayer();}),
                mkSep("sep2")
            );
        bar.Child(eProjectBtns);

        // ── Tools
        struct ToolSpec { const char* id; const char* lbl; ToolType t; };
        static constexpr ToolSpec kTools[kToolCount] = {
            {"btn_pencil","Pencil",ToolType::Pencil},
            {"btn_brush", "Brush", ToolType::Brush},
            {"btn_fill",  "Fill",  ToolType::Fill},
            {"btn_erase", "Erase", ToolType::Erase},
            {"btn_select","Select",ToolType::Select},
        };
        for (int i = 0; i < kToolCount; ++i) {
            SDL::Color c = (state.tool == kTools[i].t) ? pal::ACCENT : pal::NEUTRAL;
            toolBtns[i]  = mkBtn(kTools[i].id, kTools[i].lbl, c,
                                 [this, t = kTools[i].t]{ _SetTool(t); });
            bar.Child(toolBtns[i]);
        }

        // ── Grid toggle
        bar.Child(mkSep("sep3"));
        bar.Child(ui.Toggle("tog_grid", "Grid")
            .Check(state.showGrid)
            .OnToggle([this](bool v){ state.showGrid = v; }));

        // ── Spacer + title
        bar.Child(ui.Container("spacer_bar").Grow(1)
            .WithStyle([](auto& s){
                s.bgColor = SDL::Color(0,0,0,0);
                s.borders = SDL::FBox(0.f);
            }));
        bar.Child(ui.Label("lbl_app_title", "Tile Editor " TILE_EDITOR_VERSION)
            .TextColor(pal::ACCENT));

        return bar;
    }

    // ── Main content (3 columns) ──────────────────────────────────────────────

    SDL::ECS::EntityId _BuildMainContent() {
        auto content = ui.Row("main_content", 0.f, 0.f)
            .Grow(1)
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
            .Grow(1).Padding(0.f).ScrollableY();
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
                    s.bgColor     = SDL::Color(0,0,0,0);
                    s.borders     = SDL::FBox(0.f, 0.f, 1.f, 0.f);
                    s.bdColor = pal::BORDER;
                    s.radius      = SDL::FCorners(0.f);
                })
                .Visible(false);
            slot.row = row;

            slot.btnVis = ui.Button(std::format("ls_vis{}", i), "V")
                .W(20).H(20).PaddingH(2).PaddingV(2)
                .Style(SDL::UI::Theme::PrimaryButton(pal::GREEN))
                .WithStyle([](auto& s){ s.radius = SDL::FCorners(3.f); })
                .OnClick([this, i]{ _ToggleLayerVisible(i); });

            slot.lblName = ui.Label(std::format("ls_name{}", i), "Layer")
                .Grow(1).TextColor(pal::WHITE)
                .PaddingH(4).PaddingV(0)
                .OnClick([this, i]{ _SelectLayer(i); });

            slot.btnLock = ui.Button(std::format("ls_lock{}", i), "L")
                .W(20).H(20).PaddingH(2).PaddingV(2)
                .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
                .WithStyle([](auto& s){ s.radius = SDL::FCorners(3.f); })
                .OnClick([this, i]{ _ToggleLayerLock(i); });

            row.Children(slot.btnVis, slot.lblName, slot.btnLock);
            ui.AppendChild(eLayerContent, slot.row);
        }
        sv.Child(eLayerContent);
        panel.Child(sv);

        // Layer operation buttons (move up / move down / delete)
        panel.Child(
            ui.Row("layer_op_row", 4.f, 4.f)
                .W(SDL::UI::Value::Pw(100.f)).H(28.f)
                .WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
                .Children(
                    ui.Button("btn_lyr_up",  "↑").W(28).H(22)
                        .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
                        .WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
                        .ClickSoundKey(res_key::CLICK)
                        .OnClick([this]{ _MoveActiveLayer(-1); }),
                    ui.Button("btn_lyr_dn",  "↓").W(28).H(22)
                        .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
                        .WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
                        .ClickSoundKey(res_key::CLICK)
                        .OnClick([this]{ _MoveActiveLayer(+1); }),
                    ui.Button("btn_lyr_del", "Del").Grow(1).H(22)
                        .Style(SDL::UI::Theme::PrimaryButton(pal::RED))
                        .WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
                        .ClickSoundKey(res_key::CLICK)
                        .OnClick([this]{ _DeleteActiveLayer(); })
                )
        );

        return panel;
    }

    // ── Map canvas (center, grows) ────────────────────────────────────────────

    SDL::ECS::EntityId _BuildMapCanvas() {
        eMapCanvas = ui.CanvasWidget("map_canvas",
            [this](SDL::Event& ev){ _OnMapEvent(ev); },
            nullptr,
            [this](SDL::RendererRef r, SDL::FRect rect){ _RenderMap(r, rect); }
        ).Grow(1).Padding(0.f).Id();
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
        eTilesetCanvas = ui.CanvasWidget("ts_canvas",
            [this](SDL::Event& ev){ _OnTilesetEvent(ev); },
            nullptr,
            [this](SDL::RendererRef r, SDL::FRect rect){ _RenderTileset(r, rect); }
        ).Grow(1).Padding(0.f).Id();
        panel.Child(eTilesetCanvas);

        // Tile info
        eTileInfo = ui.Label("lbl_tile_info", "Tile: —")
            .TextColor(pal::GREY).PaddingH(8).PaddingV(2);
        panel.Child(eTileInfo);

        // Tileset navigation (prev / next when multiple tilesets)
        panel.Child(
            ui.Row("ts_nav", 4.f, 4.f)
                .W(SDL::UI::Value::Pw(100.f)).H(26.f)
                .WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
                .Children(
                    ui.Button("btn_ts_prev", "◀").W(26).H(22)
                        .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
                        .WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
                        .OnClick([this]{
                            if (state.activeTileset > 0) --state.activeTileset;
                        }),
                    ui.Button("btn_ts_next", "▶").W(26).H(22)
                        .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
                        .WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
                        .OnClick([this]{
                            if (state.activeTileset < (int)map.tilesets.size()-1)
                                ++state.activeTileset;
                        }),
                    ui.Button("btn_ts_smart", "Smart").Grow(1).H(22)
                        .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
                        .WithStyle([](auto& s){ s.radius=SDL::FCorners(3.f); })
                        .OnClick([this]{
                            if (state.activeTileset < (int)map.tilesets.size())
                                map.tilesets[state.activeTileset].smart =
                                    !map.tilesets[state.activeTileset].smart;
                        })
                )
        );

        // Import button
        panel.Child(ui.Button("btn_import2", "Import Tileset")
            .W(SDL::UI::Value::Pw(100.f)).H(26)
            .Style(SDL::UI::Theme::PrimaryButton(pal::NEUTRAL))
            .WithStyle([](auto& s){ s.radius=SDL::FCorners(0.f); })
            .ClickSoundKey(res_key::CLICK)
            .OnClick([this]{ _ImportTileset(); }));

        // Brush size row
        panel.Child(
            ui.Row("brush_row", 6.f, 4.f)
                .W(SDL::UI::Value::Pw(100.f)).H(26.f)
                .WithStyle([](auto& s){ s.bgColor=SDL::Color(0,0,0,0); s.borders=SDL::FBox(0.f); })
                .Children(
                    ui.Label("lbl_brush_sz", "Brush:").W(44).TextColor(pal::GREY),
                    ui.Slider("sld_brush", 1.f, 9.f, float(state.brushSize)).Grow(1)
                        .FillColor(pal::ACCENT)
                        .OnChange([this](float v){
                            int b = 1 + 2 * (int)((v - 1.f) / 2.f + 0.5f);
                            state.brushSize = SDL::Clamp(b, 1, 9);
                        })
                )
        );

        return panel;
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
        tx = (int)std::floor(wx / map.tileW);
        ty = (int)std::floor(wy / map.tileH);
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

        // Map boundary
        {
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
                for (int ty = 0; ty < map.height; ++ty)
                for (int tx = 0; tx < map.width;  ++tx) {
                    TileID tid = layer.tiles.empty()
                                 ? EMPTY_TILE : layer.tiles[ty * map.width + tx];
                    if (tid == EMPTY_TILE) continue;
                    const TilesetDef* ts = map.FindTileset(tid);
                    if (!ts || ts->key.empty()) continue;
                    auto texH = pool_ui.Get<SDL::Texture>(ts->key);
                    if (!texH) continue;

                    auto src = map.TileSrcRect(*ts, tid);
                    auto p   = WorldToScreen(float(tx * map.tileW),
                                             float(ty * map.tileH));
                    SDL::FRect dst{p.x, p.y, tw, th};
                    // Frustum cull
                    if (dst.x + dst.w < rect.x || dst.x > rect.x + rect.w) continue;
                    if (dst.y + dst.h < rect.y || dst.y > rect.y + rect.h) continue;

                    SDL::TextureRef tex{*texH};
                    if (layer.opacity < 1.f) tex.SetAlphaModFloat(layer.opacity);
                    r.RenderTexture(tex, src, dst);
                    if (layer.opacity < 1.f) tex.SetAlphaModFloat(1.f);
                }
            } else {
                // Object layer
                for (const auto& obj : layer.objects) {
                    auto p  = WorldToScreen(obj.x, obj.y);
                    float dw = obj.w * state.zoom;
                    float dh = obj.h * state.zoom;
                    SDL::FRect dr{p.x, p.y, dw, dh};
                    SDL::Color fc = obj.selected ? pal::OBJ_SEL : pal::OBJ_COL;
                    SDL::Color bc = obj.selected
                        ? SDL::Color{0, 220, 255, 255}
                        : SDL::Color{140, 210, 140, 255};

                    if (obj.type == ObjectType::Rect) {
                        r.SetDrawColor(fc); r.RenderFillRect(dr);
                        r.SetDrawColor(bc); r.RenderRect(dr);
                    } else if (obj.type == ObjectType::Ellipse) {
                        float cx = dr.x + dr.w * .5f, cy = dr.y + dr.h * .5f;
                        float rx = dr.w * .5f,         ry = dr.h * .5f;
                        constexpr int SEG = 32;
                        std::vector<SDL::FPoint> pts(SEG + 1);
                        for (int i = 0; i <= SEG; ++i) {
                            float a = float(i) / SEG * 2.f * SDL::PI_F;
                            pts[i] = {cx + std::cos(a) * rx, cy + std::sin(a) * ry};
                        }
                        r.SetDrawColor(bc); r.RenderLines(pts);
                    } else if (obj.type == ObjectType::Point) {
                        r.SetDrawColor({255, 200, 50, 255});
                        r.RenderFillCircle(p, 4.f * state.zoom);
                    } else if (obj.type == ObjectType::Polygon && obj.points.size() >= 2) {
                        std::vector<SDL::FPoint> pts;
                        for (auto& pt : obj.points)
                            pts.push_back(WorldToScreen(obj.x + pt.x, obj.y + pt.y));
                        pts.push_back(pts.front());
                        r.SetDrawColor(bc); r.RenderLines(pts);
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

        // Cursor tile highlight + brush preview
        float mx, my;
        SDL::GetMouseState(mx, my);
        if (mx >= rect.x && mx < rect.x + rect.w &&
            my >= rect.y && my < rect.y + rect.h) {
            int ctx, cty;
            ScreenToTile(mx, my, ctx, cty);
            if (state.tool == ToolType::Brush) {
                int half = state.brushSize / 2;
                for (int dy = -half; dy <= half; ++dy)
                for (int dx = -half; dx <= half; ++dx) {
                    int bx = ctx + dx, by = cty + dy;
                    if (bx < 0 || by < 0 || bx >= map.width || by >= map.height) continue;
                    auto bp = WorldToScreen(float(bx*map.tileW), float(by*map.tileH));
                    r.SetDrawColor({100, 180, 255, 55});
                    r.RenderFillRect(SDL::FRect{bp.x, bp.y, tw, th});
                }
            }
            if (ctx >= 0 && cty >= 0 && ctx < map.width && cty < map.height) {
                auto cp = WorldToScreen(float(ctx*map.tileW), float(cty*map.tileH));
                r.SetDrawColor({255, 255, 100, 75});
                r.RenderFillRect(SDL::FRect{cp.x, cp.y, tw, th});
                r.SetDrawColor({255, 255, 100, 190});
                r.RenderRect(SDL::FRect{cp.x, cp.y, tw, th});
            }
        }

        // Map selection rectangle
        if (state.hasMapSel && state.selW > 0 && state.selH > 0) {
            auto tl = WorldToScreen(float(state.selX * map.tileW),
                                    float(state.selY * map.tileH));
            float sw = float(state.selW) * tw, sh = float(state.selH) * th;
            r.SetDrawColor({255, 200, 50, 45}); r.RenderFillRect(SDL::FRect{tl.x, tl.y, sw, sh});
            r.SetDrawColor({255, 200, 50, 220}); r.RenderRect(SDL::FRect{tl.x, tl.y, sw, sh});
        }

        // Object drag preview
        if (state.objDrag) {
            SDL::GetMouseState(mx, my);
            float ox2 = std::min(state.objStart.x, mx);
            float oy2 = std::min(state.objStart.y, my);
            float ow  = std::abs(mx - state.objStart.x);
            float oh  = std::abs(my - state.objStart.y);
            r.SetDrawColor(pal::OBJ_SEL);
            r.RenderRect(SDL::FRect{ox2, oy2, ow, oh});
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

        // Scale to fit width of panel
        state.tsScale = SDL::Min(1.f, (rect.w - 4.f) / float(ts.imageW));
        float dispW = ts.imageW * state.tsScale;
        float dispH = ts.imageH * state.tsScale;

        float imgX = rect.x + 2.f;
        float imgY = rect.y + 2.f - state.tsScrollY;

        // Draw image
        r.RenderTexture(SDL::TextureRef{*texH}, {},
                        SDL::FRect{imgX, imgY, dispW, dispH});

        float tw  = ts.tileW    * state.tsScale;
        float th  = ts.tileH    * state.tsScale;
        float spH = ts.spacing  * state.tsScale;
        float spV = spH;

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
            r.SetDrawColor({255, 200, 50, 55}); r.RenderFillRect(SDL::FRect{px, py, sw, sh});
            r.SetDrawColor(pal::SELECTED);       r.RenderRect(SDL::FRect{px, py, sw, sh});
        }

        // Clamp scroll
        float maxScroll = SDL::Max(0.f, dispH + 4.f - rect.h);
        state.tsScrollY = SDL::Clamp(state.tsScrollY, 0.f, maxScroll);
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
                // Object layer?
                bool activeIsObj =
                    map.activeLayer >= 0 &&
                    map.activeLayer < (int)map.layers.size() &&
                    map.layers[map.activeLayer].type == LayerType::Object;
                if (activeIsObj) {
                    state.objDrag  = true;
                    state.objStart = {mx, my};
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
            if (state.mapLDown && !state.objDrag) _ApplyToolAt(mx, my);
            if (state.mapRDown) {
                int tx, ty; ScreenToTile(mx, my, tx, ty);
                _PaintTile(tx, ty, EMPTY_TILE);
            }
        }
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
            int x0 = int(std::min(float(tx), state.selDragStart.x));
            int y0 = int(std::min(float(ty), state.selDragStart.y));
            int x1 = int(std::max(float(tx), state.selDragStart.x));
            int y1 = int(std::max(float(ty), state.selDragStart.y));
            state.hasMapSel = true;
            state.selX = x0; state.selY = y0;
            state.selW = x1 - x0 + 1; state.selH = y1 - y0 + 1;
            return;
        }
        state.selDrag = false;

        if (state.tool == ToolType::Fill) {
            TileID paint = _SelectedTileID();
            auto cmd = FloodFill(map, map.activeLayer, tx, ty, paint);
            ur.Push(std::move(cmd));
            return;
        }

        TileID paint = (state.tool == ToolType::Erase) ? EMPTY_TILE : _SelectedTileID();

        if (state.tool == ToolType::Brush) {
            int half = state.brushSize / 2;
            for (int dy = -half; dy <= half; ++dy)
            for (int dx = -half; dx <= half; ++dx)
                _PaintTile(tx + dx, ty + dy, paint);
        } else {  // Pencil or Erase
            _PaintTile(tx, ty, paint);
        }
    }

    void _PaintTile(int tx, int ty, TileID id) {
        if (tx < 0 || ty < 0 || tx >= map.width || ty >= map.height) return;
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
            if (x < 0 || y < 0 || x >= map.width || y >= map.height) continue;
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
        float dx = std::abs(ex - state.objStart.x);
        float dy = std::abs(ey - state.objStart.y);
        if (dx < 2.f && dy < 2.f) return;
        auto [wx, wy] = ScreenToWorld(std::min(state.objStart.x, ex),
                                      std::min(state.objStart.y, ey));
        ObjectDef obj;
        obj.id   = state.nextObjId++;
        obj.name = std::format("Object{}", obj.id);
        obj.x = wx; obj.y = wy;
        obj.w = dx / state.zoom; obj.h = dy / state.zoom;
        obj.type = ObjectType::Rect;
        layer.objects.push_back(std::move(obj));
        map.dirty = true;
    }

    // =========================================================================
    // Tileset panel events
    // =========================================================================

    void _OnTilesetEvent(SDL::Event& ev) {
        if (map.tilesets.empty() || state.activeTileset >= (int)map.tilesets.size()) return;
        const auto& ts = map.tilesets[state.activeTileset];
        if (ts.imageW <= 0 || ts.columns <= 0 || ts.rows <= 0) return;

        float tw  = ts.tileW   * state.tsScale;
        float th  = ts.tileH   * state.tsScale;
        float spH = ts.spacing * state.tsScale;
        float spV = spH;
        float imgX = state.tilesetRect.x + 2.f;
        float imgY = state.tilesetRect.y + 2.f - state.tsScrollY;

        auto screenToCell = [&](float sx, float sy, int& cx, int& cy) {
            cx = SDL::Clamp((int)std::floor((sx - imgX) / (tw + spH)), 0, ts.columns - 1);
            cy = SDL::Clamp((int)std::floor((sy - imgY) / (th + spV)), 0, ts.rows    - 1);
        };

        if (ev.type == SDL::EVENT_MOUSE_WHEEL) {
            float mx = ev.wheel.mouse_x, my = ev.wheel.mouse_y;
            if (mx < state.tilesetRect.x || mx > state.tilesetRect.x + state.tilesetRect.w) return;
            if (my < state.tilesetRect.y || my > state.tilesetRect.y + state.tilesetRect.h) return;
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
            state.selTileX = std::min(x0, x1); state.selTileY = std::min(y0, y1);
            state.selTileW = std::abs(x1 - x0) + 1;
            state.selTileH = std::abs(y1 - y0) + 1;
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
        l.tiles.assign(map.width * map.height, EMPTY_TILE);
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
                ? SDL::Color{32, 48, 76, 255}
                : SDL::Color{0, 0, 0, 0};
            ui.GetStyle(slot.btnVis).bgColor  = layer.visible ? pal::GREEN  : pal::NEUTRAL;
            ui.GetStyle(slot.btnLock).bgColor = layer.locked  ? pal::ORANGE : pal::NEUTRAL;
        }
    }

    // =========================================================================
    // Tool selection
    // =========================================================================

    void _SetTool(ToolType t) {
        state.tool = t;
        static constexpr ToolType kOrder[kToolCount] = {
            ToolType::Pencil, ToolType::Brush, ToolType::Fill,
            ToolType::Erase,  ToolType::Select
        };
        for (int i = 0; i < kToolCount; ++i)
            if (toolBtns[i] != SDL::ECS::NullEntity)
                ui.GetStyle(toolBtns[i]).bgColor =
                    (kOrder[i] == t) ? pal::ACCENT : pal::NEUTRAL;
    }

    // =========================================================================
    // File operations
    // =========================================================================

    void _NewMap()    { state.pendingNew = true; }
    void _DoNewMap() {
        map.Init(); state = EditorState{}; ur = UndoRedo{};
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

        static constexpr const char* kToolNames[kToolCount] =
            {"Pencil","Brush","Fill","Erase","Select"};

        std::string s = std::format(
            "{} | Map {}x{} ({}x{}px) | Layer {}/{} | Zoom {:.0f}% | {}{}",
            kToolNames[(int)state.tool],
            map.width, map.height, map.tileW, map.tileH,
            map.activeLayer + 1, (int)map.layers.size(),
            state.zoom * 100.f,
            (tx >= 0) ? std::format("[{},{}]", tx, ty) : "---",
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
