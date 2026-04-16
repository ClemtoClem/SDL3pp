#pragma once
#include "ScriptParser.hpp"
#include "../Logger/Logger.hpp"
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// Collision IDs (normalized: original_value - tileCollisionIdOffset)
// ─────────────────────────────────────────────────────────────────────────────

enum class CollisionId : int {
	None       = 0,
	Block      = 1,   // walls, trees
	Message    = 2,   // signs
	Access     = 3,   // door requiring Enter
	Water      = 4,
	Transition = 5,   // map transition
	Lava       = 6,
	MoverE     = 7,
	MoverS     = 8,
	MoverW     = 9,
	MoverN     = 10,
};

inline bool IsBlocking(CollisionId id) noexcept {
	return id == CollisionId::Block  || id == CollisionId::Water ||
		   id == CollisionId::Lava;
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity spawn / teleport definitions
// ─────────────────────────────────────────────────────────────────────────────

enum class Direction { North = 0, South, West, East };

inline Direction ParseDirection(const std::string& s) noexcept {
	if (s == "NORTH" || s == "north") return Direction::North;
	if (s == "SOUTH" || s == "south") return Direction::South;
	if (s == "WEST"  || s == "west")  return Direction::West;
	if (s == "EAST"  || s == "east")  return Direction::East;
	return Direction::South;
}

struct EntitySpawnDef {
	std::string typeName;
	float x = 0.f, y = 0.f;
	Direction dir = Direction::South;
};

struct TeleportDef {
	bool        changeMap = false;
	std::string mapName;
	float x = 0.f, y = 0.f;
};

// ─────────────────────────────────────────────────────────────────────────────
// MapData — fully loaded tile map
// ─────────────────────────────────────────────────────────────────────────────

struct MapData {
	std::string name;
	int width = 0, height = 0;
	int numLayers = 0;
	int frontLayers = 0;       // layers rendered in front of entities
	int backgroundTile = 0;
	bool zeroIsNotTile = false;
	std::string musicKey;

	// tiles[layer][y * width + x] = tile ID in the tileset image
	std::vector<std::vector<int>> tiles;

	// collision[y * width + x] = normalized CollisionId
	std::vector<int> collision;

	std::vector<EntitySpawnDef> spawns;
	std::unordered_map<std::string, std::vector<std::string>> messages; // "x_y" -> lines
	std::unordered_map<std::string, TeleportDef> teleports; // "x_y" -> def

	[[nodiscard]] CollisionId GetCollision(int x, int y) const noexcept {
		if (x < 0 || y < 0 || x >= width || y >= height) return CollisionId::Block;
		int v = collision[(size_t)(y * width + x)];
		return static_cast<CollisionId>(v);
	}

	[[nodiscard]] int GetTile(int layer, int x, int y) const noexcept {
		if (layer < 0 || layer >= numLayers) return 0;
		if (x < 0 || y < 0 || x >= width || y >= height) return 0;
		return tiles[(size_t)layer][(size_t)(y * width + x)];
	}
};

// ─────────────────────────────────────────────────────────────────────────────
// Loader
// ─────────────────────────────────────────────────────────────────────────────

namespace detail {

inline std::vector<int> ParseCSVRow(const std::string& line) {
	std::vector<int> row;
	std::istringstream ss(line);
	std::string token;
	while (std::getline(ss, token, ',')) {
		if (token.empty()) continue;
		try { row.push_back((int)std::stoul(token)); }
		catch (...) { row.push_back(0); }
	}
	return row;
}

} // namespace detail

// ─────────────────────────────────────────────────────────────────────────────
// Helper — convert a string to UPPERCASE in-place
// ─────────────────────────────────────────────────────────────────────────────
inline std::string ToUpperCase(std::string s) {
	for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
	return s;
}

/// Load a complete map from its directory.
/// Expected files: `res/maps/<name>.script`, `res/maps/<name>.map`, `res/maps/<name>.cmap`
inline MapData LoadMap(const std::string& basePath, const std::string& mapName) {
	MapData map;
	map.name = mapName;

	const std::string confPath = basePath + "/" + mapName + ".script";
	const std::string mapPath  = basePath + "/" + mapName + ".map";
	const std::string cmapPath = basePath + "/" + mapName + ".cmap";

	// ── 1. Parse .script ──────────────────────────────────────────────────────
	ScriptSectionPtr script;
	try {
		script = ParseConfFile(confPath);
	} catch (const std::exception& e) {
		LOG_RESOURCE(core::LogLevel::Error) << "LoadMap script: " << e.what();
		return map;
	}

	map.width          = script->count("width")      ? (*script)["width"].AsInt(1)   : 1;
	map.height         = script->count("height")     ? (*script)["height"].AsInt(1)  : 1;
	map.numLayers      = script->count("tracings")   ? (*script)["tracings"].AsInt(1): 1;
	map.frontLayers    = script->count("nbTracingsInFrontOf")
						 ? (*script)["nbTracingsInFrontOf"].AsInt(0) : 0;
	map.backgroundTile = script->count("backgroundTile")
						 ? (*script)["backgroundTile"].AsInt(0) : 0;
	map.zeroIsNotTile  = script->count("zeroIsNotTile")
						 ? (*script)["zeroIsNotTile"].AsBool(false) : false;
	map.musicKey       = script->count("musicName")
						 ? (*script)["musicName"].AsString("") : "";

	int collisionOffset = script->count("tileCollisionIdOffset")
						  ? (*script)["tileCollisionIdOffset"].AsInt(0) : 0;

	// ── Entities ──────────────────────────────────────────────────────────────
	if (script->count("configEntities")) {
		auto entSec = (*script)["configEntities"].AsSection();
		if (entSec) {
			for (auto& [k, v] : *entSec) {
				auto sec = v.AsSection();
				if (!sec) continue;
				EntitySpawnDef def;
				// Accept both "type" and "name" fields; normalise to UPPERCASE
				std::string rawType = v.Get("type").AsString("");
				if (rawType.empty()) rawType = v.Get("name").AsString("UNKNOWN");
				def.typeName = ToUpperCase(rawType);
				def.x        = v.Get("x").AsFloat(0.f);
				def.y        = v.Get("y").AsFloat(0.f);
				def.dir      = ParseDirection(v.Get("direction").AsString("SOUTH"));
				map.spawns.push_back(std::move(def));
			}
		}
	}

	// ── Messages ─────────────────────────────────────────────────────────────
	if (script->count("messages")) {
		auto msgSec = (*script)["messages"].AsSection();
		if (msgSec) {
			for (auto& [pos, lines] : *msgSec) {
				auto lineSec = lines.AsSection();
				if (!lineSec) continue;
				std::vector<std::string> msgs;
				for (int i = 1; ; ++i) {
					auto it = lineSec->find('%' + std::to_string(i));
					if (it == lineSec->end()) break;
					msgs.push_back(it->second.AsString(""));
				}
				map.messages[pos] = std::move(msgs);
			}
		}
	}

	// ── Transitions — accepts both "transitions" and "teleportations" keys ────
	{
		const char* teleKeys[] = { "transitions", "teleportations" };
		for (const char* key : teleKeys) {
			if (!script->count(key)) continue;
			auto teleSec = (*script)[key].AsSection();
			if (!teleSec) continue;
			for (auto& [pos, v] : *teleSec) {
				TeleportDef def;
				def.changeMap = v.Get("changeMap").AsBool(false);
				// Accept both "name" and "type" as the destination map identifier
				def.mapName   = v.Get("name").AsString("");
				if (def.mapName.empty()) def.mapName = v.Get("type").AsString("");
				def.x         = v.Get("x").AsFloat(0.f);
				def.y         = v.Get("y").AsFloat(0.f);
				map.teleports[pos] = std::move(def);
			}
			break; // use the first key found
		}
	}

	// ── 2. Parse .map ────────────────────────────────────────────────────────
	{
		std::ifstream f(mapPath);
		if (!f.is_open()) {
			LOG_RESOURCE(core::LogLevel::Error) << "Cannot open " << mapPath;
			return map;
		}

		map.tiles.resize((size_t)map.numLayers,
						 std::vector<int>((size_t)(map.width * map.height), 0));

		int currentLayer = 0;
		int currentRow   = 0;
		std::string line;
		while (std::getline(f, line)) {
			// Windows line endings
			if (!line.empty() && line.back() == '\r') line.pop_back();

			if (line.empty()) {
				// Layer separator
				currentLayer++;
				currentRow = 0;
				continue;
			}

			if (currentLayer >= map.numLayers || currentRow >= map.height) continue;

			auto row = detail::ParseCSVRow(line);
			for (int x = 0; x < map.width && x < (int)row.size(); ++x) {
				map.tiles[(size_t)currentLayer]
						  [(size_t)(currentRow * map.width + x)] = row[(size_t)x];
			}
			currentRow++;
		}
	}

	// ── 3. Parse .cmap ────────────────────────────────────────────────────────
	{
		std::ifstream f(cmapPath);
		if (!f.is_open()) {
			LOG_RESOURCE(core::LogLevel::Error) << "Cannot open " << cmapPath;
			// return partial map (no collision)
		} else {
			map.collision.resize((size_t)(map.width * map.height), 0);

			int row = 0;
			std::string line;
			while (std::getline(f, line) && row < map.height) {
				if (!line.empty() && line.back() == '\r') line.pop_back();
				if (line.empty()) continue;

				auto vals = detail::ParseCSVRow(line);
				for (int x = 0; x < map.width && x < (int)vals.size(); ++x) {
					int v = (int)vals[(size_t)x];
					int normalized = (v > 0 && v >= collisionOffset) ? v - collisionOffset : 0;
					map.collision[(size_t)(row * map.width + x)] =
						(int)std::clamp(normalized, 0, 15);
				}
				row++;
			}
		}
	}

	LOG_RESOURCE(core::LogLevel::Success)
		<< "Map '" << mapName << "' loaded: "
		<< map.width << 'x' << map.height
		<< " | " << map.numLayers << " layers"
		<< " | " << map.spawns.size() << " entities";

	return map;
}

} // namespace core
