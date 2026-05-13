#pragma once
// ══════════════════════════════════════════════════════════════════════════
//  WorldSave.h – SQLite-backed world persistence
// ══════════════════════════════════════════════════════════════════════════
#include "Chunk.h"
#include <sqlite3.h>
#include <SDL3pp/SDL3pp.h>

#include <string>
#include <vector>
#include <filesystem>
#include <chrono>
#include <cstring>

struct WorldInfo {
    std::string name;
    uint32_t    seed        = 0;
    std::string created_at;
};

// Manage one world .db file in saves/<name>.db
class WorldSave {
public:
    static constexpr const char* kSavesDir = "saves";

    static std::string DbPath(const std::string& name) {
        return std::string(kSavesDir) + "/" + name + ".db";
    }

    // List all worlds (by scanning saves/ directory)
    static std::vector<WorldInfo> ListWorlds() {
        std::vector<WorldInfo> out;
        std::error_code ec;
        std::filesystem::create_directories(kSavesDir, ec);
        for (auto& entry : std::filesystem::directory_iterator(kSavesDir, ec)) {
            if (entry.path().extension() != ".db") continue;
            std::string name = entry.path().stem().string();
            sqlite3* db = nullptr;
            if (sqlite3_open(entry.path().string().c_str(), &db) != SQLITE_OK) continue;
            WorldInfo info;
            info.name = name;
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT seed, created_at FROM metadata LIMIT 1",
                                   -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    info.seed       = (uint32_t)sqlite3_column_int(stmt, 0);
                    info.created_at = (const char*)sqlite3_column_text(stmt, 1);
                }
                sqlite3_finalize(stmt);
            }
            sqlite3_close(db);
            out.push_back(std::move(info));
        }
        return out;
    }

    // Create a new empty world database
    static bool Create(const std::string& name, uint32_t seed) {
        std::error_code ec;
        std::filesystem::create_directories(kSavesDir, ec);
        sqlite3* db = nullptr;
        if (sqlite3_open(DbPath(name).c_str(), &db) != SQLITE_OK) return false;
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS metadata("
            "  id INTEGER PRIMARY KEY,"
            "  name TEXT NOT NULL,"
            "  seed INTEGER NOT NULL,"
            "  created_at TEXT NOT NULL"
            ");"
            "CREATE TABLE IF NOT EXISTS chunks("
            "  cx INTEGER NOT NULL,"
            "  cy INTEGER NOT NULL,"
            "  cz INTEGER NOT NULL,"
            "  data BLOB NOT NULL,"
            "  PRIMARY KEY(cx,cy,cz)"
            ");",
            nullptr, nullptr, nullptr);
        // Insert metadata (ISO 8601 timestamp)
        auto now = std::chrono::system_clock::now();
        auto tt  = std::chrono::system_clock::to_time_t(now);
        char ts[32];
        std::strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", std::gmtime(&tt));
        sqlite3_stmt* ins = nullptr;
        sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO metadata(name,seed,created_at) VALUES(?,?,?)",
            -1, &ins, nullptr);
        sqlite3_bind_text(ins, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(ins, 2, (int)seed);
        sqlite3_bind_text(ins, 3, ts, -1, SQLITE_TRANSIENT);
        sqlite3_step(ins);
        sqlite3_finalize(ins);
        sqlite3_close(db);
        return true;
    }

    // Delete a world database
    static bool Delete(const std::string& name) {
        std::error_code ec;
        return std::filesystem::remove(DbPath(name), ec);
    }

    // Read the seed stored for a world
    static uint32_t ReadSeed(const std::string& name) {
        sqlite3* db = nullptr;
        if (sqlite3_open(DbPath(name).c_str(), &db) != SQLITE_OK) return 0;
        uint32_t seed = 0;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT seed FROM metadata LIMIT 1", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW)
                seed = (uint32_t)sqlite3_column_int(stmt, 0);
            sqlite3_finalize(stmt);
        }
        sqlite3_close(db);
        return seed;
    }

    // ── Per-instance (opened DB) ──────────────────────────────────────────────

    explicit WorldSave(const std::string& name) : name_(name) {
        sqlite3_open(DbPath(name).c_str(), &db_);
        if (db_) sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    }
    ~WorldSave() { if (db_) sqlite3_close(db_); }

    WorldSave(const WorldSave&) = delete;
    WorldSave& operator=(const WorldSave&) = delete;

    bool IsOpen() const { return db_ != nullptr; }

    // Save one chunk (BLOB = raw ChunkBlocks::data)
    void SaveChunk(int cx, int cy, int cz, const ChunkBlocks& cb) {
        if (!db_) return;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT OR REPLACE INTO chunks(cx,cy,cz,data) VALUES(?,?,?,?)",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, cx);
        sqlite3_bind_int(stmt, 2, cy);
        sqlite3_bind_int(stmt, 3, cz);
        sqlite3_bind_blob(stmt, 4, cb.data, (int)sizeof(cb.data), SQLITE_STATIC);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }

    // Load one chunk; returns false if not found
    bool LoadChunk(int cx, int cy, int cz, ChunkBlocks& cb) const {
        if (!db_) return false;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT data FROM chunks WHERE cx=? AND cy=? AND cz=?",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, cx);
        sqlite3_bind_int(stmt, 2, cy);
        sqlite3_bind_int(stmt, 3, cz);
        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int bytes = sqlite3_column_bytes(stmt, 0);
            if (blob && bytes == (int)sizeof(cb.data)) {
                std::memcpy(cb.data, blob, sizeof(cb.data));
                found = true;
            }
        }
        sqlite3_finalize(stmt);
        return found;
    }

    // Check how many chunks are stored
    int ChunkCount() const {
        if (!db_) return 0;
        sqlite3_stmt* stmt = nullptr;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM chunks", -1, &stmt, nullptr);
        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return count;
    }

    // Batch save (wrapped in transaction for speed)
    void BeginTransaction() { if (db_) sqlite3_exec(db_, "BEGIN", nullptr, nullptr, nullptr); }
    void EndTransaction()   { if (db_) sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr); }

private:
    std::string name_;
    sqlite3*    db_ = nullptr;
};
