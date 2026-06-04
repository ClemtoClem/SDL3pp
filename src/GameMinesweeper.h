#pragma once

#include "Common.h"
#include <algorithm>
#include <cmath>
#include <format>
#include <queue>
#include <random>
#include <string>
#include <vector>

class GameMinesweeper final : public IGame {
public:
    GameMinesweeper() : m_rng(std::random_device{}()) { SetDiff(Diff::Easy); }

    const char* Title()       const override { return "Demineur"; }
    const char* Description() const override { return "Decouvrez toutes les cases sans toucher une mine!"; }
    int  Score()               const override { return m_score; }

    void Start() override { m_running = true; Reset(); }
    void Pause() override { m_running = false; }

    void Reset() override {
        m_cells.assign(m_cols * m_rows, Cell{});
        m_minesPlaced = false; m_flagCount = 0; m_revealCount = 0;
        m_time = 0; m_won = false; m_gameOver = false; m_score = 0;
        m_explodeAnim = 0.f; m_explodeC = m_explodeR = -1;
        m_hoverC = m_hoverR = -1; m_running = true;
    }

    void OnEvent(const SDL::Event& ev) override { m_input.Feed(ev); }

    void Update(float dt) override {
        if (!m_running) { m_input.Flush(); return; }

        if (m_gameOver) {
            m_explodeAnim += (float)(dt * 2.0);
            if (m_input.JP(SDL::KEYCODE_R)) Reset();
            m_input.Flush(); return;
        }
        if (m_won) { if (m_input.JP(SDL::KEYCODE_R)) Reset(); m_input.Flush(); return; }

        m_time += dt;
        if (m_input.JP(SDL::KEYCODE_R)) { Reset(); m_input.Flush(); return; }
        if (m_input.JP(SDL::KEYCODE_E)) { SetDiff(Diff::Easy);   Reset(); m_input.Flush(); return; }
        if (m_input.JP(SDL::KEYCODE_M)) { SetDiff(Diff::Medium); Reset(); m_input.Flush(); return; }
        if (m_input.JP(SDL::KEYCODE_H)) { SetDiff(Diff::Hard);   Reset(); m_input.Flush(); return; }

        if (m_cellSize > 0.f) {
            m_hoverC = (int)((m_input.mx - m_gridOx) / m_cellSize);
            m_hoverR = (int)((m_input.my - m_gridOy) / m_cellSize);
        }

        bool inGrid = m_hoverC >= 0 && m_hoverC < m_cols && m_hoverR >= 0 && m_hoverR < m_rows;

        if (m_input.MouseJP(SDL::BUTTON_LEFT)   && inGrid) Reveal(m_hoverC, m_hoverR);
        if (m_input.MouseJP(SDL::BUTTON_RIGHT)  && inGrid) {
            Cell& c = GetCell(m_hoverC, m_hoverR);
            if (!c.revealed) { c.flagged = !c.flagged; m_flagCount += c.flagged ? 1 : -1; }
        }
        if (m_input.MouseJP(SDL::BUTTON_MIDDLE) && inGrid) {
            Cell& c = GetCell(m_hoverC, m_hoverR);
            if (c.revealed && c.adjacent > 0) {
                int adjFlags = 0;
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                        if ((dr || dc) && InBounds(m_hoverC + dc, m_hoverR + dr) &&
                            GetCell(m_hoverC + dc, m_hoverR + dr).flagged)
                            adjFlags++;
                if (adjFlags == c.adjacent)
                    for (int dr = -1; dr <= 1; ++dr)
                        for (int dc = -1; dc <= 1; ++dc)
                            if (dr || dc) Reveal(m_hoverC + dc, m_hoverR + dr);
            }
        }

        if (!m_gameOver && CheckWin()) {
            m_won = true; m_score = std::max(0, 1000 - (int)m_time * 2);
        }
        m_input.Flush();
    }

    void Render(SDL::RendererRef r, SDL::FRect rect) override {
        r.SetDrawColorFloat(SDL::FColor{0.18f, 0.18f, 0.20f}); r.RenderFillRect(rect);

        const float hudH = 36.f;
        m_cellSize = std::min({(rect.w - 20.f) / m_cols, (rect.h - hudH - 16.f) / m_rows, 40.f});
        float gw = m_cellSize * m_cols, gh = m_cellSize * m_rows;
        m_gridOx = rect.x + (rect.w - gw) * 0.5f;
        m_gridOy = rect.y + hudH + (rect.h - hudH - gh) * 0.5f;

        for (int rr = 0; rr < m_rows; ++rr)
            for (int c = 0; c < m_cols; ++c) {
                bool hov = (c == m_hoverC && rr == m_hoverR && !m_gameOver && !m_won);
                DrawCell(r, m_gridOx + c * m_cellSize, m_gridOy + rr * m_cellSize,
                         m_cellSize, GetCell(c, rr), hov);
            }

        if (m_explodeAnim > 0.f && m_explodeAnim < 1.5f && m_explodeC >= 0) {
            float rad   = m_explodeAnim * m_cellSize * 3.f;
            float alpha = std::max(0.f, 1.f - m_explodeAnim / 1.5f);
            r.SetDrawColorFloat(SDL::FColor{1.f, 0.5f, 0.1f, alpha * 0.6f});
            r.RenderFillCircle({m_gridOx + (m_explodeC + 0.5f) * m_cellSize,
                                m_gridOy + (m_explodeR + 0.5f) * m_cellSize}, rad);
        }

        const char* dstr = (m_diff == Diff::Easy)   ? "Facile (E)"
                         : (m_diff == Diff::Medium) ? "Moyen (M)" : "Expert (H)";
        r.SetDrawColorFloat(SDL::FColor{0.8f, 0.8f, 0.8f});
        DrawText(r, rect.x + 8.f, rect.y + 6.f, 10.f,
            std::format("{} | Mines:{} | Drapeaux:{} | Temps:{:.0f}s | R=reset",
                dstr, m_mineCount, m_flagCount, m_time));
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.6f, 0.6f});
        DrawText(r, rect.x + 8.f, rect.y + 18.f, 10.f,
            "Clic gauche=reveler  Clic droit=drapeau  Molette=chord");

        if (m_won) {
            r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f, 0.65f});
            r.RenderFillRect(rect);
            r.SetDrawColorFloat(SDL::FColor{0.3f, 1.f, 0.4f});
            SDL::FPoint c = rect.GetCentroid();
            DrawTextC(r, {c.x, c.y - rect.h * 0.10f}, 26.f, "GAGNE!");
            r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});
            DrawTextC(r, {c.x, c.y + rect.h * 0.02f}, 13.f,
                std::format("Temps:{:.1f}s  Score:{}", m_time, m_score));
            r.SetDrawColorFloat(SDL::FColor{0.7f, 0.7f, 0.7f});
            DrawTextC(r, {c.x, c.y + rect.h * 0.12f}, 11.f, "R pour rejouer");
        }
        if (m_gameOver) {
            r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f, 0.65f}); r.RenderFillRect(rect);
            r.SetDrawColorFloat(SDL::FColor{1.f, 0.3f, 0.3f});
            SDL::FPoint c = rect.GetCentroid();
            DrawTextC(r, {c.x, c.y - rect.h * 0.10f}, 26.f, "BOOM!");
            r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});
            DrawTextC(r, {c.x, c.y + rect.h * 0.02f}, 13.f, "R pour rejouer");
        }
    }

private:
    enum class Diff { Easy, Medium, Hard };
    struct Cell { bool mine = false, revealed = false, flagged = false; int adjacent = 0; };

    std::vector<Cell> m_cells;
    int m_cols = 9, m_rows = 9, m_mineCount = 10;
    Diff m_diff = Diff::Easy;
    bool m_minesPlaced = false, m_won = false, m_gameOver = false, m_running = false;
    int m_flagCount = 0, m_revealCount = 0, m_score = 0;
    double m_time = 0;
    float m_cellSize = 0.f, m_gridOx = 0.f, m_gridOy = 0.f;
    int m_hoverC = -1, m_hoverR = -1, m_explodeC = -1, m_explodeR = -1;
    float m_explodeAnim = 0.f;
    std::mt19937 m_rng;
    InputState m_input;

    Cell& GetCell(int c, int r)             { return m_cells[r * m_cols + c]; }
    const Cell& GetCell(int c, int r) const { return m_cells[r * m_cols + c]; }
    bool InBounds(int c, int r)       const { return c >= 0 && c < m_cols && r >= 0 && r < m_rows; }

    void SetDiff(Diff d) {
        m_diff = d;
        switch (d) {
            case Diff::Easy:   m_cols =  9; m_rows =  9; m_mineCount =  10; break;
            case Diff::Medium: m_cols = 16; m_rows = 16; m_mineCount =  40; break;
            case Diff::Hard:   m_cols = 30; m_rows = 16; m_mineCount =  99; break;
        }
    }

    void PlaceMines(int safeC, int safeR) {
        std::vector<int> cands;
        for (int r = 0; r < m_rows; ++r)
            for (int c = 0; c < m_cols; ++c)
                if (std::abs(c - safeC) > 1 || std::abs(r - safeR) > 1)
                    cands.push_back(r * m_cols + c);
        std::shuffle(cands.begin(), cands.end(), m_rng);
        int cnt = std::min(m_mineCount, (int)cands.size());
        for (int i = 0; i < cnt; ++i) m_cells[cands[i]].mine = true;
        for (int r = 0; r < m_rows; ++r)
            for (int c = 0; c < m_cols; ++c) {
                int adj = 0;
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                        if ((dr || dc) && InBounds(c + dc, r + dr) && GetCell(c + dc, r + dr).mine) adj++;
                GetCell(c, r).adjacent = adj;
            }
        m_minesPlaced = true;
    }

    void FloodReveal(int col, int row) {
        std::queue<std::pair<int,int>> q; q.push({col, row});
        while (!q.empty()) {
            auto [c, r] = q.front(); q.pop();
            if (!InBounds(c, r)) continue;
            Cell& cell = GetCell(c, r);
            if (cell.revealed || cell.flagged || cell.mine) continue;
            cell.revealed = true; m_revealCount++;
            if (cell.adjacent == 0)
                for (int dr = -1; dr <= 1; ++dr)
                    for (int dc = -1; dc <= 1; ++dc)
                        if (dr || dc) q.push({c + dc, r + dr});
        }
    }

    void Reveal(int c, int r) {
        if (!InBounds(c, r)) return;
        Cell& cell = GetCell(c, r);
        if (cell.revealed || cell.flagged) return;
        if (!m_minesPlaced) PlaceMines(c, r);
        if (cell.mine) {
            cell.revealed = true; m_gameOver = true;
            m_explodeC = c; m_explodeR = r; m_explodeAnim = 0.f;
            for (auto& cc : m_cells) if (cc.mine) cc.revealed = true;
            return;
        }
        FloodReveal(c, r);
    }

    bool CheckWin() const { return m_revealCount >= m_cols * m_rows - m_mineCount; }

    void DrawCell(SDL::RendererRef r, float x, float y, float sz, const Cell& cell, bool hov) {
        const float g = 1.f;
        if (!cell.revealed) {
            r.SetDrawColorFloat(SDL::FColor{hov ? 0.55f : 0.48f, hov ? 0.57f : 0.50f, hov ? 0.60f : 0.53f});
            r.RenderFillRect({{x + g, y + g, sz - g*2, sz - g*2}});
            r.SetDrawColorFloat(SDL::FColor{0.70f, 0.72f, 0.75f});
            r.RenderLine({x+g, y+g}, {x+g+sz-g*2, y+g});
            r.RenderLine({x+g, y+g}, {x+g, y+g+sz-g*2});
            r.SetDrawColorFloat(SDL::FColor{0.30f, 0.32f, 0.35f});
            r.RenderLine({x+g, y+sz-g*2}, {x+g+sz-g*2, y+sz-g*2});
            r.RenderLine({x+sz-g*2, y+g}, {x+sz-g*2, y+g+sz-g*2});
            if (cell.flagged) {
                float cx = x + sz*0.5f, cy = y + sz*0.5f;
                r.SetDrawColorFloat(SDL::FColor{0.95f, 0.15f, 0.15f});
                SDL::FPoint pts[3] = {
                    {cx - sz*0.2f, cy - sz*0.3f},
                    {cx + sz*0.25f, cy - sz*0.05f},
                    {cx - sz*0.2f, cy + sz*0.1f}
                };
                r.RenderFillPolygon(pts);
                r.SetDrawColorFloat(SDL::FColor{0.1f, 0.1f, 0.1f});
                r.RenderLine({cx - sz*0.2f, cy - sz*0.3f}, {cx - sz*0.2f, cy - sz*0.3f + sz*0.45f});
            }
        } else if (cell.mine) {
            r.SetDrawColorFloat(SDL::FColor{m_explodeC >= 0 ? 0.9f : 0.4f, 0.15f, 0.15f});
            r.RenderFillRect({{x + g, y + g, sz - g*2, sz - g*2}});
            float cx = x + sz*0.5f, cy = y + sz*0.5f;
            r.SetDrawColorFloat(SDL::FColor{0.05f, 0.05f, 0.05f}); r.RenderFillCircle({cx, cy}, sz*0.30f);
            r.SetDrawColorFloat(SDL::FColor{0.9f, 0.9f, 0.9f});    r.RenderFillCircle({cx - sz*0.08f, cy - sz*0.08f}, sz*0.07f);
            r.SetDrawColorFloat(SDL::FColor{0.05f, 0.05f, 0.05f});
            r.RenderLine({cx, cy - sz*0.38f}, {cx, cy - sz*0.22f});
            r.RenderLine({cx, cy + sz*0.22f}, {cx, cy + sz*0.38f});
            r.RenderLine({cx - sz*0.38f, cy}, {cx - sz*0.22f, cy});
            r.RenderLine({cx + sz*0.22f, cy}, {cx + sz*0.38f, cy});
        } else {
            r.SetDrawColorFloat(SDL::FColor{0.78f, 0.80f, 0.83f}); r.RenderFillRect({{x+g, y+g, sz-g*2, sz-g*2}});
            r.SetDrawColorFloat(SDL::FColor{0.60f, 0.62f, 0.65f, 1.f}); r.RenderRect({{x+g, y+g, sz-g*2, sz-g*2}});
            if (cell.adjacent > 0) {
                static constexpr SDL::FColor NC[9] = {
                    {0, 0, 0}, {0.15f, 0.15f, 0.95f}, {0.05f, 0.55f, 0.05f},
                    {0.90f, 0.10f, 0.10f}, {0.10f, 0.10f, 0.60f}, {0.70f, 0.10f, 0.10f},
                    {0.10f, 0.60f, 0.60f}, {0.50f, 0.10f, 0.50f}, {0.30f, 0.30f, 0.30f}
                };
                int idx = std::min(cell.adjacent, 8);
                r.SetDrawColorFloat(NC[idx]);
                float fsz = sz * 0.55f;
                std::string s = std::to_string(cell.adjacent);
                auto ts = MeasText(s, fsz);
                DrawText(r, x + (sz - ts.x) * 0.5f, y + (sz - ts.y) * 0.5f, fsz, s);
            }
        }
    }
};
