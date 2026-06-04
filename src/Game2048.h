#pragma once

#include "Common.h"
#include <algorithm>
#include <format>
#include <random>
#include <string>
#include <vector>

class Game2048 final : public IGame {
public:
    explicit Game2048(int gs = 4)
        : m_gs(std::clamp(gs, 3, 8)), m_rng(std::random_device{}()) {}

    const char* Title()       const override { return "2048"; }
    const char* Description() const override { return "Combinez les tuiles pour atteindre 2048!"; }
    int  Score()               const override { return m_score; }
    int  GetGridSize()         const          { return m_gs; }

    void SetGridSize(int s) {
        m_gs = std::clamp(s, 3, 8);
        m_grid.assign(m_gs * m_gs, 0);
        m_target = (m_gs >= 6) ? 4096 : 2048;
        m_spawnAnims.clear();
    }

    void Start() override { m_running = true; Reset(); }
    void Pause() override { m_running = false; }

    void Reset() override {
        m_gameOver = false;
        m_score    = 0;
        m_spawnAnims.clear();
        m_grid.assign(m_gs * m_gs, 0);
        Spawn(); Spawn();
        m_running = true;
    }

    void OnEvent(const SDL::Event& ev) override { m_input.Feed(ev); }

    void Update(float dt) override {
        if (!m_running) return;
        auto tryMove = [&](bool moved) {
            if (moved) { Spawn(); if (!HasMoves()) m_gameOver = true; }
        };
        if (m_input.JP(SDL::KEYCODE_UP)    || m_input.JP(SDL::KEYCODE_W)) tryMove(SlideUp());
        if (m_input.JP(SDL::KEYCODE_DOWN)  || m_input.JP(SDL::KEYCODE_S)) tryMove(SlideDown());
        if (m_input.JP(SDL::KEYCODE_LEFT)  || m_input.JP(SDL::KEYCODE_A)) tryMove(SlideLeft());
        if (m_input.JP(SDL::KEYCODE_RIGHT) || m_input.JP(SDL::KEYCODE_D)) tryMove(SlideRight());
        if (m_input.JP(SDL::KEYCODE_R)) Reset();
        m_input.Flush();

        for (auto& sa : m_spawnAnims) { sa.scale += dt / 0.15f; if (sa.scale > 1.f) sa.scale = 1.f; }
        m_spawnAnims.erase(
            std::remove_if(m_spawnAnims.begin(), m_spawnAnims.end(),
                [](auto& a) { return a.scale >= 1.f; }),
            m_spawnAnims.end());
    }

    void Render(SDL::RendererRef r, SDL::FRect rect) override {
        r.SetDrawColorFloat(SDL::FColor{0.16f, 0.15f, 0.14f});
        r.RenderFillRoundedRect(rect, SDL::FCorners(10.f));

        const float pad   = 12.f;
        const float avail = std::min(rect.w, rect.h) - pad * 2.f;
        const float ts    = avail / (float)m_gs;
        const float gw    = ts * m_gs;
        const float gh    = ts * m_gs;
        const float sx    = rect.x + (rect.w - gw) * 0.5f;
        const float sy    = rect.y + (rect.h - gh) * 0.5f;

        r.SetDrawColorFloat(SDL::FColor{0.27f, 0.25f, 0.23f});
        r.RenderFillRoundedRect({sx - 4, sy - 4, gw + 8, gh + 8}, SDL::FCorners(8.f));

        for (int row = 0; row < m_gs; ++row)
            for (int col = 0; col < m_gs; ++col) {
                int val = Cell(col, row);
                float tx = sx + col * ts, ty = sy + row * ts;
                float scale = 1.f;
                for (auto& sa : m_spawnAnims)
                    if (sa.col == col && sa.row == row) { scale = sa.scale; break; }
                DrawTile(r, tx, ty, ts, val, scale);
            }

        if (m_gameOver) {
            r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f, 0.65f});
            r.RenderFillRoundedRect(rect, SDL::FCorners(10.f));
            r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});
            SDL::FPoint c = rect.GetCentroid();
            DrawTextC(r, {c.x, c.y - rect.h * 0.05f}, 20.f, "PARTIE TERMINEE");
            DrawTextC(r, {c.x, c.y + rect.h * 0.05f}, 12.f, "R pour rejouer");
        }
    }

private:
    int  m_gs, m_target = 2048, m_score = 0;
    bool m_running = false, m_gameOver = false;
    std::vector<int> m_grid;
    std::mt19937 m_rng;
    InputState m_input;

    struct SpawnAnim { int col, row; float scale = 0.f; };
    std::vector<SpawnAnim> m_spawnAnims;

    int& Cell(int c, int r)       { return m_grid[r * m_gs + c]; }
    int  Cell(int c, int r) const { return m_grid[r * m_gs + c]; }

    void Spawn() {
        std::vector<int> empty;
        for (int i = 0; i < m_gs * m_gs; ++i)
            if (m_grid[i] == 0) empty.push_back(i);
        if (empty.empty()) return;
        int idx = empty[std::uniform_int_distribution<int>(0, (int)empty.size() - 1)(m_rng)];
        m_grid[idx] = (std::uniform_int_distribution<int>(0, 9)(m_rng) < 9) ? 2 : 4;
        m_spawnAnims.push_back({idx % m_gs, idx / m_gs, 0.f});
    }

    bool SlideLeft() {
        bool moved = false;
        for (int r = 0; r < m_gs; ++r) {
            std::vector<int> row;
            for (int c = 0; c < m_gs; ++c) if (Cell(c, r)) row.push_back(Cell(c, r));
            for (int i = 0; i + 1 < (int)row.size(); ++i)
                if (row[i] == row[i + 1]) { row[i] *= 2; m_score += row[i]; row.erase(row.begin() + i + 1); }
            row.resize(m_gs, 0);
            for (int c = 0; c < m_gs; ++c) { if (Cell(c, r) != row[c]) moved = true; Cell(c, r) = row[c]; }
        }
        return moved;
    }

    bool SlideRight() {
        bool moved = false;
        for (int r = 0; r < m_gs; ++r) {
            std::vector<int> row;
            for (int c = m_gs - 1; c >= 0; --c) if (Cell(c, r)) row.push_back(Cell(c, r));
            for (int i = 0; i + 1 < (int)row.size(); ++i)
                if (row[i] == row[i + 1]) { row[i] *= 2; m_score += row[i]; row.erase(row.begin() + i + 1); }
            row.resize(m_gs, 0);
            for (int c = m_gs - 1; c >= 0; --c) {
                int v = row[m_gs - 1 - c];
                if (Cell(c, r) != v) moved = true;
                Cell(c, r) = v;
            }
        }
        return moved;
    }

    bool SlideUp() {
        bool moved = false;
        for (int c = 0; c < m_gs; ++c) {
            std::vector<int> col;
            for (int r = 0; r < m_gs; ++r) if (Cell(c, r)) col.push_back(Cell(c, r));
            for (int i = 0; i + 1 < (int)col.size(); ++i)
                if (col[i] == col[i + 1]) { col[i] *= 2; m_score += col[i]; col.erase(col.begin() + i + 1); }
            col.resize(m_gs, 0);
            for (int r = 0; r < m_gs; ++r) { if (Cell(c, r) != col[r]) moved = true; Cell(c, r) = col[r]; }
        }
        return moved;
    }

    bool SlideDown() {
        bool moved = false;
        for (int c = 0; c < m_gs; ++c) {
            std::vector<int> col;
            for (int r = m_gs - 1; r >= 0; --r) if (Cell(c, r)) col.push_back(Cell(c, r));
            for (int i = 0; i + 1 < (int)col.size(); ++i)
                if (col[i] == col[i + 1]) { col[i] *= 2; m_score += col[i]; col.erase(col.begin() + i + 1); }
            col.resize(m_gs, 0);
            for (int r = m_gs - 1; r >= 0; --r) {
                int v = col[m_gs - 1 - r];
                if (Cell(c, r) != v) moved = true;
                Cell(c, r) = v;
            }
        }
        return moved;
    }

    bool HasMoves() const {
        for (int r = 0; r < m_gs; ++r)
            for (int c = 0; c < m_gs; ++c) {
                if (!Cell(c, r)) return true;
                if (c + 1 < m_gs && Cell(c, r) == Cell(c + 1, r)) return true;
                if (r + 1 < m_gs && Cell(c, r) == Cell(c, r + 1)) return true;
            }
        return false;
    }

    static constexpr SDL::FColor TileColor(int v) {
        switch (v) {
            case    0: return {0.20f, 0.19f, 0.18f};
            case    2: return {0.93f, 0.89f, 0.85f};
            case    4: return {0.93f, 0.88f, 0.78f};
            case    8: return {0.95f, 0.69f, 0.47f};
            case   16: return {0.96f, 0.58f, 0.39f};
            case   32: return {0.96f, 0.49f, 0.37f};
            case   64: return {0.96f, 0.37f, 0.23f};
            case  128: return {0.93f, 0.81f, 0.45f};
            case  256: return {0.93f, 0.80f, 0.38f};
            case  512: return {0.93f, 0.78f, 0.31f};
            case 1024: return {0.93f, 0.77f, 0.25f};
            case 2048: return {0.93f, 0.76f, 0.18f};
            default:   return {0.20f, 0.65f, 0.35f};
        }
    }

    void DrawTile(SDL::RendererRef r, float x, float y, float sz, int val, float animScale) {
        const float mg    = 4.f;
        const float inner = (sz - mg * 2.f) * animScale;
        const float ox    = x + mg + (sz - mg * 2.f - inner) * 0.5f;
        const float oy    = y + mg + (sz - mg * 2.f - inner) * 0.5f;
        r.SetDrawColorFloat(TileColor(val));
        r.RenderFillRoundedRect({ox, oy, inner, inner}, SDL::FCorners(6.f));
        if (val == 0) return;

        std::string txt = std::to_string(val);
        float fsz = inner * 0.38f;
        if (inner > 50.f && val >= 1000) fsz = inner * 0.28f;
        if (fsz < 8.f) fsz = 8.f;

        if (val <= 4) r.SetDrawColorFloat(SDL::FColor{0.47f, 0.43f, 0.40f});
        else          r.SetDrawColorFloat(SDL::FColor{0.98f, 0.97f, 0.94f});
        DrawTextC(r, {ox + inner * 0.5f, oy + inner * 0.5f}, fsz, txt);
    }
};
