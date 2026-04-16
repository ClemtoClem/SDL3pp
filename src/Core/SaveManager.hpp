#pragma once

/**
 * @file SaveManager.hpp
 * @brief Binary save/load for Polyadventure player progression.
 *
 * Save format (little-endian, version-prefixed):
 *
 *   [4 bytes] magic   = "PAVE"
 *   [1 byte]  version = 1
 *   [4 bytes] polypoints  (int32)
 *   [4 bytes] hp           (float)
 *   [4 bytes] maxHp        (float)
 *   [N bytes] mapName      (uint16 length + UTF-8 bytes, no NUL)
 *   [4 bytes] playerX      (float)
 *   [4 bytes] playerY      (float)
 *   [1 byte]  playerDir    (uint8, Direction enum)
 */

#include "MapLoader.hpp"   // Direction
#include "../Logger/Logger.hpp"
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// SaveData — plain aggregate, mirrors game progression state
// ─────────────────────────────────────────────────────────────────────────────

struct SaveData {
	int         polypoints = 0;
	float       hp         = 100.f;
	float       maxHp      = 100.f;
	std::string mapName    = "world";
	float       playerX    = 28.5f;
	float       playerY    = 88.5f;
	Direction   playerDir  = Direction::South;
};

// ─────────────────────────────────────────────────────────────────────────────
// SaveManager
// ─────────────────────────────────────────────────────────────────────────────

class SaveManager {
public:
	// ── Construction ──────────────────────────────────────────────────────────

	/// @param path  Full path to the save file (e.g. "…/res/save.dat").
	explicit SaveManager(std::string path) : m_path(std::move(path)) {}

	// ── Public API ────────────────────────────────────────────────────────────

	/// Returns true if a valid save file already exists on disk.
	[[nodiscard]] bool Exists() const noexcept {
		return std::filesystem::exists(m_path);
	}

	/// Delete the save file from disk (no-op if absent).
	void Delete() const {
		std::error_code ec;
		std::filesystem::remove(m_path, ec);
		if (!ec)
			LOG_RESOURCE(core::LogLevel::Info) << "Save deleted: " << m_path;
	}

	/**
	 * Write `data` to disk.
	 * @returns true on success.
	 */
	bool Save(const SaveData& data) const {
		// Ensure parent directory exists.
		std::error_code ec;
		std::filesystem::create_directories(
			std::filesystem::path(m_path).parent_path(), ec);

		std::ofstream f(m_path, std::ios::binary | std::ios::trunc);
		if (!f) {
			LOG_RESOURCE(core::LogLevel::Error)
				<< "SaveManager: cannot open for write: " << m_path;
			return false;
		}

		// Magic + version
		f.write("PAVE", 4);
		_WriteU8(f, 1u);

		// Fields
		_WriteI32(f, data.polypoints);
		_WriteF32(f, data.hp);
		_WriteF32(f, data.maxHp);
		_WriteStr(f, data.mapName);
		_WriteF32(f, data.playerX);
		_WriteF32(f, data.playerY);
		_WriteU8 (f, static_cast<uint8_t>(data.playerDir));

		LOG_RESOURCE(core::LogLevel::Success)
			<< "Saved to: " << m_path;
		return f.good();
	}

	/**
	 * Load save data from disk into `out`.
	 * @returns true on success; `out` is unchanged on failure.
	 */
	bool Load(SaveData& out) const {
		std::ifstream f(m_path, std::ios::binary);
		if (!f) {
			LOG_RESOURCE(core::LogLevel::Warning)
				<< "SaveManager: no save at: " << m_path;
			return false;
		}

		// Magic
		char magic[4];
		f.read(magic, 4);
		if (std::strncmp(magic, "PAVE", 4) != 0) {
			LOG_RESOURCE(core::LogLevel::Error)
				<< "SaveManager: invalid magic in: " << m_path;
			return false;
		}

		uint8_t version = _ReadU8(f);
		if (version != 1) {
			LOG_RESOURCE(core::LogLevel::Error)
				<< "SaveManager: unsupported save version " << (int)version;
			return false;
		}

		SaveData tmp;
		tmp.polypoints = _ReadI32(f);
		tmp.hp         = _ReadF32(f);
		tmp.maxHp      = _ReadF32(f);
		tmp.mapName    = _ReadStr(f);
		tmp.playerX    = _ReadF32(f);
		tmp.playerY    = _ReadF32(f);
		tmp.playerDir  = static_cast<Direction>(_ReadU8(f));

		if (!f) {
			LOG_RESOURCE(core::LogLevel::Error)
				<< "SaveManager: truncated save file: " << m_path;
			return false;
		}

		out = std::move(tmp);
		LOG_RESOURCE(core::LogLevel::Success)
			<< "Loaded save: pts=" << out.polypoints
			<< " map=" << out.mapName
			<< " pos=(" << out.playerX << "," << out.playerY << ")";
		return true;
	}

private:
	std::string m_path;

	// ── Serialisation helpers (little-endian) ─────────────────────────────────

	static void _WriteU8 (std::ofstream& f, uint8_t  v) { f.put(static_cast<char>(v)); }
	static void _WriteI32(std::ofstream& f, int32_t  v) {
		uint8_t b[4] = {
			static_cast<uint8_t>(v & 0xFF),
			static_cast<uint8_t>((v >> 8) & 0xFF),
			static_cast<uint8_t>((v >> 16) & 0xFF),
			static_cast<uint8_t>((v >> 24) & 0xFF)
		};
		f.write(reinterpret_cast<const char*>(b), 4);
	}
	static void _WriteF32(std::ofstream& f, float v) {
		uint32_t u;
		std::memcpy(&u, &v, 4);
		_WriteU32(f, u);
	}
	static void _WriteU32(std::ofstream& f, uint32_t v) {
		uint8_t b[4] = {
			static_cast<uint8_t>(v & 0xFF),
			static_cast<uint8_t>((v >> 8) & 0xFF),
			static_cast<uint8_t>((v >> 16) & 0xFF),
			static_cast<uint8_t>((v >> 24) & 0xFF)
		};
		f.write(reinterpret_cast<const char*>(b), 4);
	}
	static void _WriteStr(std::ofstream& f, const std::string& s) {
		uint16_t len = static_cast<uint16_t>(s.size());
		uint8_t  lb[2] = { static_cast<uint8_t>(len & 0xFF),
							static_cast<uint8_t>((len >> 8) & 0xFF) };
		f.write(reinterpret_cast<const char*>(lb), 2);
		f.write(s.data(), len);
	}

	static uint8_t  _ReadU8 (std::ifstream& f) {
		return static_cast<uint8_t>(f.get());
	}
	static int32_t  _ReadI32(std::ifstream& f) {
		uint8_t b[4]; f.read(reinterpret_cast<char*>(b), 4);
		return static_cast<int32_t>(
			(uint32_t)b[0] | ((uint32_t)b[1] << 8) |
			((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24));
	}
	static float    _ReadF32(std::ifstream& f) {
		uint32_t u = _ReadU32(f);
		float v; std::memcpy(&v, &u, 4);
		return v;
	}
	static uint32_t _ReadU32(std::ifstream& f) {
		uint8_t b[4]; f.read(reinterpret_cast<char*>(b), 4);
		return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
			   ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
	}
	static std::string _ReadStr(std::ifstream& f) {
		uint8_t lb[2]; f.read(reinterpret_cast<char*>(lb), 2);
		uint16_t len = static_cast<uint16_t>(lb[0] | ((uint16_t)lb[1] << 8));
		std::string s(len, '\0');
		f.read(s.data(), len);
		return s;
	}
};

} // namespace core
