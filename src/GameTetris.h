#pragma once

#include "Common.h"
#include <algorithm>
#include <array>
#include <deque>
#include <format>
#include <random>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Tetromino data
// ─────────────────────────────────────────────────────────────────────────────

struct Tetromino {
    int id, rotation, col, row;
    std::array<std::pair<int,int>,4> Cells() const;
};

static const std::array<std::pair<int,int>,4> TETRO[7][4] = {
    { std::array<std::pair<int,int>,4>{{{0,1},{1,1},{2,1},{3,1}}},
      std::array<std::pair<int,int>,4>{{{2,0},{2,1},{2,2},{2,3}}},
      std::array<std::pair<int,int>,4>{{{0,2},{1,2},{2,2},{3,2}}},
      std::array<std::pair<int,int>,4>{{{1,0},{1,1},{1,2},{1,3}}} },
    { std::array<std::pair<int,int>,4>{{{1,0},{2,0},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{2,0},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{2,0},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{2,0},{1,1},{2,1}}} },
    { std::array<std::pair<int,int>,4>{{{1,0},{0,1},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{1,1},{2,1},{1,2}}},
      std::array<std::pair<int,int>,4>{{{0,1},{1,1},{2,1},{1,2}}},
      std::array<std::pair<int,int>,4>{{{1,0},{0,1},{1,1},{1,2}}} },
    { std::array<std::pair<int,int>,4>{{{1,0},{2,0},{0,1},{1,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{1,1},{2,1},{2,2}}},
      std::array<std::pair<int,int>,4>{{{1,1},{2,1},{0,2},{1,2}}},
      std::array<std::pair<int,int>,4>{{{0,0},{0,1},{1,1},{1,2}}} },
    { std::array<std::pair<int,int>,4>{{{0,0},{1,0},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{2,0},{1,1},{2,1},{1,2}}},
      std::array<std::pair<int,int>,4>{{{0,1},{1,1},{1,2},{2,2}}},
      std::array<std::pair<int,int>,4>{{{1,0},{0,1},{1,1},{0,2}}} },
    { std::array<std::pair<int,int>,4>{{{0,0},{0,1},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{2,0},{1,1},{1,2}}},
      std::array<std::pair<int,int>,4>{{{0,1},{1,1},{2,1},{2,2}}},
      std::array<std::pair<int,int>,4>{{{1,0},{1,1},{0,2},{1,2}}} },
    { std::array<std::pair<int,int>,4>{{{2,0},{0,1},{1,1},{2,1}}},
      std::array<std::pair<int,int>,4>{{{1,0},{1,1},{1,2},{2,2}}},
      std::array<std::pair<int,int>,4>{{{0,1},{1,1},{2,1},{0,2}}},
      std::array<std::pair<int,int>,4>{{{0,0},{1,0},{1,1},{1,2}}} },
};

inline std::array<std::pair<int,int>,4> Tetromino::Cells() const { return TETRO[id][rotation]; }

// ─────────────────────────────────────────────────────────────────────────────
// GameTetris
// ─────────────────────────────────────────────────────────────────────────────

class GameTetris final : public IGame {
public:
    GameTetris() : m_rng(std::random_device{}()) {}

    const char* Title()       const override { return "Tetris"; }
    const char* Description() const override { return "Classique! Effacez des lignes."; }
    int  Score()               const override { return m_score; }

    void Start() override { m_running = true; Reset(); }
    void Pause() override { m_running = false; }

    void Reset() override {
        m_board.fill(0); m_bag.clear();
        m_level = 1; m_lines = 0; m_score = 0; m_gameOver = false;
        m_fallTimer = 0; m_lockDelay = 0; m_onGround = false;
        m_canHold = true; m_held = -1;
        m_clearRows.clear(); m_clearAnim = 0;
        m_movingL = m_movingR = false; m_dasTimer = m_arrTimer = 0;
        FillBag(); m_next.id = NextId(); m_next.rotation = 0;
        SpawnPiece(); m_running = true;
    }

    void OnEvent(const SDL::Event& ev) override { m_input.Feed(ev); }

    void Update(float dt) override {
        if (!m_running || m_gameOver) { m_input.Flush(); return; }
        if (m_clearAnim > 0) { m_clearAnim -= dt; m_input.Flush(); return; }

        bool wantL = m_input.Down(SDL::KEYCODE_LEFT)  || m_input.Down(SDL::KEYCODE_A);
        bool wantR = m_input.Down(SDL::KEYCODE_RIGHT) || m_input.Down(SDL::KEYCODE_D);

        auto moveH = [&](int dx) {
            Tetromino t = m_cur; t.col += dx;
            if (Valid(t)) m_cur = t;
        };

        if (wantL && !wantR) {
            if (m_input.JP(SDL::KEYCODE_LEFT) || m_input.JP(SDL::KEYCODE_A))
                { moveH(-1); m_dasTimer = 0; m_arrTimer = 0; m_movingL = true; m_movingR = false; }
            else if (m_movingL) {
                m_dasTimer += dt;
                if (m_dasTimer >= 0.13f) { m_arrTimer += dt; if (m_arrTimer >= 0.05f) { moveH(-1); m_arrTimer = 0; } }
            }
        } else m_movingL = false;

        if (wantR && !wantL) {
            if (m_input.JP(SDL::KEYCODE_RIGHT) || m_input.JP(SDL::KEYCODE_D))
                { moveH(1); m_dasTimer = 0; m_arrTimer = 0; m_movingR = true; m_movingL = false; }
            else if (m_movingR) {
                m_dasTimer += dt;
                if (m_dasTimer >= 0.13f) { m_arrTimer += dt; if (m_arrTimer >= 0.05f) { moveH(1); m_arrTimer = 0; } }
            }
        } else m_movingR = false;

        if (m_input.JP(SDL::KEYCODE_UP)    || m_input.JP(SDL::KEYCODE_W))    Rotate(1);
        if (m_input.JP(SDL::KEYCODE_Z)     || m_input.JP(SDL::KEYCODE_LCTRL)) Rotate(-1);
        if (m_input.JP(SDL::KEYCODE_SPACE))                                    HardDrop();
        if (m_input.JP(SDL::KEYCODE_C) && m_canHold)                          Hold();
        if (m_input.JP(SDL::KEYCODE_R))                                        Reset();

        bool soft = m_input.Down(SDL::KEYCODE_DOWN) || m_input.Down(SDL::KEYCODE_S);
        double grav = soft ? 0.05 : GravDelay(m_level);
        m_fallTimer += dt;
        if (m_fallTimer >= grav) {
            m_fallTimer = 0;
            Tetromino below = m_cur; below.row++;
            if (Valid(below)) { m_cur = below; if (soft) m_score += 1; m_onGround = false; }
            else m_onGround = true;
        }
        if (m_onGround) { m_lockDelay += dt; if (m_lockDelay >= 0.5) Lock(); }

        m_input.Flush();
    }

    void Render(SDL::RendererRef r, SDL::FRect rect) override {
        const float CS  = std::min(rect.h / (ROWS + 2.f), (rect.w - 120.f) / COLS);
        const float GW  = CS * COLS, GH = CS * ROWS;
        const float ox  = rect.x + (rect.w - GW - 110.f) * 0.5f;
        const float oy  = rect.y + (rect.h - GH) * 0.5f;

        r.SetDrawColorFloat(SDL::FColor{0.10f, 0.10f, 0.12f}); r.RenderFillRect({{ox, oy, GW, GH}});
        r.SetDrawColorFloat(SDL::FColor{0.25f, 0.25f, 0.30f}); r.RenderRect({{ox, oy, GW, GH}});
        r.SetDrawColorFloat(SDL::FColor{0.18f, 0.18f, 0.20f});
        for (int c = 1; c < COLS; ++c) r.RenderLine({ox + c * CS, oy}, {ox + c * CS, oy + GH});
        for (int rr = 1; rr < ROWS; ++rr) r.RenderLine({ox, oy + rr * CS}, {ox + GW, oy + rr * CS});

        for (int rr = 0; rr < ROWS; ++rr) {
            float alpha = 1.f;
            for (int cr : m_clearRows)
                if (cr == rr) { alpha = std::max(0.f, (float)(m_clearAnim / CLEAR_DUR)); break; }
            for (int c = 0; c < COLS; ++c) {
                int v = Board(c, rr);
                if (v > 0) DrawCell(r, ox + c * CS, oy + rr * CS, CS, v - 1, alpha);
            }
        }

        int ghostRow = GhostRow();
        if (ghostRow != m_cur.row) {
            Tetromino gh = m_cur; gh.row = ghostRow;
            DrawPiece(r, gh, ox, oy, CS, 0.25f);
        }
        DrawPiece(r, m_cur, ox, oy, CS, 1.f);

        const float px = ox + GW + 10.f;
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.6f, 0.6f});
        DrawText(r, px, oy, 10.f, "SUIVANT");
        {
            const float ns = CS * 0.75f;
            Tetromino np = m_next; np.col = 0; np.row = 0;
            for (auto [dc, dr] : np.Cells())
                DrawCell(r, px + dc * ns, oy + 14.f + dr * ns, ns, np.id);
        }
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.6f, 0.6f});
        DrawText(r, px, oy + 80.f, 10.f, "HOLD");
        if (m_held >= 0) {
            const float hs = CS * 0.75f;
            Tetromino hp; hp.id = m_held; hp.rotation = 0; hp.col = 0; hp.row = 0;
            for (auto [dc, dr] : hp.Cells())
                DrawCell(r, px + dc * hs, oy + 94.f + dr * hs, hs, hp.id, m_canHold ? 1.f : 0.4f);
        }
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.6f, 0.6f}); DrawText(r, px, oy + 160.f, 10.f, "SCORE");
        r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});    DrawText(r, px, oy + 173.f, 10.f, std::to_string(m_score));
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.6f, 0.6f}); DrawText(r, px, oy + 195.f, 10.f, "NIVEAU");
        r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});    DrawText(r, px, oy + 208.f, 10.f, std::to_string(m_level));
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.6f, 0.6f}); DrawText(r, px, oy + 230.f, 10.f, "LIGNES");
        r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});    DrawText(r, px, oy + 243.f, 10.f, std::to_string(m_lines));

        if (m_gameOver) {
            r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f, 0.7f}); r.RenderFillRect({{ox, oy, GW, GH}});
            r.SetDrawColorFloat(SDL::FColor{1.f, 0.3f, 0.3f});
            SDL::FPoint gc = SDL::FRect{ox, oy, GW, GH}.GetCentroid();
            DrawTextC(r, {gc.x, gc.y - GH * 0.075f}, 22.f, "GAME OVER");
            r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});
            DrawTextC(r, {gc.x, gc.y + GH * 0.075f}, 12.f, "R pour rejouer");
        }
    }

private:
    static constexpr int    COLS      = 10;
    static constexpr int    ROWS      = 20;
    static constexpr double CLEAR_DUR = 0.25;

    std::array<int, COLS * ROWS> m_board{};
    Tetromino m_cur{}, m_next{};
    int m_held = -1; bool m_canHold = true;
    std::deque<int> m_bag;
    std::mt19937 m_rng;
    int m_level = 1, m_lines = 0, m_score = 0;
    double m_fallTimer = 0, m_lockDelay = 0;
    bool m_onGround = false, m_gameOver = false, m_running = false;
    double m_dasTimer = 0, m_arrTimer = 0;
    bool m_movingL = false, m_movingR = false;
    std::vector<int> m_clearRows;
    double m_clearAnim = 0;
    InputState m_input;

    int& Board(int c, int r)       { return m_board[r * COLS + c]; }
    int  Board(int c, int r) const { return m_board[r * COLS + c]; }

    void FillBag() {
        std::array<int, 7> ids = {0, 1, 2, 3, 4, 5, 6};
        std::shuffle(ids.begin(), ids.end(), m_rng);
        for (int id : ids) m_bag.push_back(id);
    }
    int NextId() {
        if (m_bag.empty()) FillBag();
        int id = m_bag.front(); m_bag.pop_front(); return id;
    }
    void SpawnPiece() {
        m_cur = m_next; m_cur.col = 3; m_cur.row = 0;
        m_next.id = NextId(); m_next.rotation = 0;
        m_canHold = true;
        if (!Valid(m_cur)) { m_gameOver = true; m_running = false; }
    }
    bool Valid(const Tetromino& t) const {
        for (auto [dc, dr] : t.Cells()) {
            int c = t.col + dc, r = t.row + dr;
            if (c < 0 || c >= COLS || r >= ROWS) return false;
            if (r >= 0 && Board(c, r) != 0) return false;
        }
        return true;
    }
    void Lock() {
        for (auto [dc, dr] : m_cur.Cells()) {
            int c = m_cur.col + dc, r = m_cur.row + dr;
            if (r >= 0) Board(c, r) = m_cur.id + 1;
        }
        int lines = ClearLines();
        if (lines > 0) {
            const int base[] = {0, 100, 300, 500, 800};
            m_score += base[std::min(lines, 4)] * m_level;
            m_lines += lines; m_level = m_lines / 10 + 1;
        }
        SpawnPiece(); m_onGround = false; m_lockDelay = 0;
    }
    int ClearLines() {
        m_clearRows.clear();
        for (int r = 0; r < ROWS; ++r) {
            bool full = true;
            for (int c = 0; c < COLS; ++c) if (!Board(c, r)) { full = false; break; }
            if (full) m_clearRows.push_back(r);
        }
        if (m_clearRows.empty()) return 0;
        m_clearAnim = CLEAR_DUR;
        for (int cr : m_clearRows) {
            for (int r = cr; r > 0; --r) for (int c = 0; c < COLS; ++c) Board(c, r) = Board(c, r - 1);
            for (int c = 0; c < COLS; ++c) Board(c, 0) = 0;
        }
        return (int)m_clearRows.size();
    }
    void HardDrop() {
        int gr = GhostRow(), dy = gr - m_cur.row;
        m_cur.row = gr; m_score += dy * 2; Lock();
    }
    void Rotate(int dir) {
        Tetromino t = m_cur; t.rotation = (t.rotation + dir + 4) % 4;
        for (int kick : {0, 1, -1, 2, -2}) {
            t.col = m_cur.col + kick;
            if (Valid(t)) { m_cur = t; return; }
        }
    }
    int GhostRow() const {
        Tetromino t = m_cur;
        while (true) { Tetromino n = t; n.row++; if (!Valid(n)) return t.row; t = n; }
    }
    void Hold() {
        int save = m_held; m_held = m_cur.id;
        if (save == -1) SpawnPiece();
        else { m_cur.id = save; m_cur.rotation = 0; m_cur.col = 3; m_cur.row = 0; }
        m_canHold = false;
    }
    static double GravDelay(int lv) {
        static const double t[] = {
            0.800, 0.717, 0.633, 0.550, 0.467, 0.383, 0.300, 0.217, 0.133, 0.100,
            0.083, 0.067, 0.050, 0.033, 0.017, 0.013, 0.010, 0.008, 0.007, 0.006
        };
        return t[std::clamp(lv - 1, 0, 19)];
    }

    static constexpr SDL::FColor kColors[7] = {
        {0.00f, 0.85f, 0.95f}, {0.95f, 0.90f, 0.00f}, {0.70f, 0.00f, 0.95f},
        {0.00f, 0.85f, 0.20f}, {0.95f, 0.10f, 0.10f}, {0.10f, 0.20f, 0.95f},
        {0.95f, 0.55f, 0.00f}
    };

    void DrawCell(SDL::RendererRef r, float x, float y, float sz, int id, float alpha = 1.f) {
        if (id < 0 || id > 6) return;
        SDL::FColor col = kColors[id];
        const float g = 1.5f;
        r.SetDrawColorFloat({col.r * 0.6f, col.g * 0.6f, col.b * 0.6f, alpha});
        r.RenderFillRect({{x, y, sz, sz}});
        r.SetDrawColorFloat({col.r, col.g, col.b, alpha});
        r.RenderFillRect({{x + g, y + g, sz - g * 2, sz - g * 2}});
        SDL::FColor hi = col.Brighten(0.3f);
        r.SetDrawColorFloat({hi.r, hi.g, hi.b, alpha * 0.5f});
        r.RenderFillRect({{x + g, y + g, sz - g * 2, g * 1.5f}});
    }

    void DrawPiece(SDL::RendererRef r, const Tetromino& t, float ox, float oy, float cs, float alpha = 1.f) {
        for (auto [dc, dr] : t.Cells())
            DrawCell(r, ox + (t.col + dc) * cs, oy + (t.row + dr) * cs, cs, t.id, alpha);
    }
};
