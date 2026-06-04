#pragma once

#include "Common.h"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <stack>
#include <string>
#include <vector>

class GameSokoban final : public IGame {
public:
    const char* Title()       const override { return "Sokoban"; }
    const char* Description() const override { return "Poussez les caisses sur les cibles!"; }
    int  Score()               const override { return m_score; }

    void Start() override {
        m_running = true; m_state = State::PackSelect;
        LoadPackList(); m_packSel = 0;
    }
    void Pause() override { m_running = false; }

    void Reset() override {
        if (m_state == State::Playing) {
            m_won = false; m_winAnim = 0.f;
            LoadLevel(m_curLevel); m_running = true;
        }
    }

    void OnEvent(const SDL::Event& ev) override { m_input.Feed(ev); }

    void Update(float dt) override {
        if (!m_running) { m_input.Flush(); return; }

        if (m_state == State::PackSelect) {
            if (!m_packs.empty()) {
                if (m_input.JP(SDL::KEYCODE_UP) || m_input.JP(SDL::KEYCODE_W))
                    m_packSel = std::max(0, m_packSel - 1);
                if (m_input.JP(SDL::KEYCODE_DOWN) || m_input.JP(SDL::KEYCODE_S))
                    m_packSel = std::min((int)m_packs.size() - 1, m_packSel + 1);
                if (m_input.wantsReturn) {
                    LoadPack(m_packSel);
                    if (!m_levels.empty()) { m_state = State::Playing; LoadLevel(0); }
                }
            }
            m_input.Flush(); return;
        }

        if (m_won) {
            m_winAnim += (float)dt;
            if (m_input.wantsReturn) {
                int next = m_curLevel + 1;
                if (next < (int)m_levels.size()) LoadLevel(next);
            }
            if (m_input.JP(SDL::KEYCODE_TAB)) { m_state = State::PackSelect; LoadPackList(); }
            m_input.Flush(); return;
        }

        bool moved = false;
        if (m_input.JP(SDL::KEYCODE_UP)    || m_input.JP(SDL::KEYCODE_W)) moved = TryMove(0, -1);
        if (m_input.JP(SDL::KEYCODE_DOWN)  || m_input.JP(SDL::KEYCODE_S)) moved = TryMove(0,  1);
        if (m_input.JP(SDL::KEYCODE_LEFT)  || m_input.JP(SDL::KEYCODE_A)) moved = TryMove(-1, 0);
        if (m_input.JP(SDL::KEYCODE_RIGHT) || m_input.JP(SDL::KEYCODE_D)) moved = TryMove( 1, 0);
        if (m_input.JP(SDL::KEYCODE_Z) || (m_input.Down(SDL::KEYCODE_LCTRL) && m_input.JP(SDL::KEYCODE_Z))) Undo();
        if (m_input.JP(SDL::KEYCODE_R)) Reset();
        if (m_input.JP(SDL::KEYCODE_N)) { int n = m_curLevel + 1; if (n < (int)m_levels.size()) LoadLevel(n); }
        if (m_input.JP(SDL::KEYCODE_P)) { int p = m_curLevel - 1; if (p >= 0) LoadLevel(p); }
        if (m_input.JP(SDL::KEYCODE_TAB)) { m_state = State::PackSelect; LoadPackList(); }
        m_input.Flush();

        if (moved && IsWon()) { m_won = true; m_winAnim = 0.f; m_score += 1000 / std::max(1, m_moves); }
    }

    void Render(SDL::RendererRef r, SDL::FRect rect) override {
        if (m_state == State::PackSelect) { RenderPackSelect(r, rect); return; }

        r.SetDrawColorFloat(SDL::FColor{0.12f, 0.12f, 0.15f}); r.RenderFillRect(rect);
        const float hudTop = 32.f, pad = 20.f;
        const float vw = rect.w - pad * 2.f, vh = rect.h - hudTop - pad;
        float maxCS = std::min(vw / m_cols, vh / m_rows);
        float CS = std::min(maxCS, 48.f);
        float GW = CS * m_cols, GH = CS * m_rows;

        float ox, oy;
        if (GW <= vw) ox = rect.x + (rect.w - GW) * 0.5f;
        else {
            float ideal = rect.x + rect.w * 0.5f - (m_px + 0.5f) * CS;
            ox = std::clamp(ideal, rect.x + pad + vw - GW, rect.x + pad);
        }
        if (GH <= vh) oy = rect.y + hudTop + (vh - GH) * 0.5f;
        else {
            float ideal = rect.y + hudTop + vh * 0.5f - (m_py + 0.5f) * CS;
            oy = std::clamp(ideal, rect.y + hudTop + vh - GH, rect.y + hudTop);
        }

        const float cx1 = rect.x + pad, cx2 = rect.x + rect.w - pad;
        const float cy1 = rect.y + hudTop, cy2 = rect.y + rect.h - pad;

        for (int y = 0; y < m_rows; ++y)
            for (int x = 0; x < m_cols; ++x) {
                float px = ox + x * CS, py = oy + y * CS;
                if (px + CS < cx1 || px > cx2 || py + CS < cy1 || py > cy2) continue;
                char c = Cell(x, y);
                if (c == '@' || c == '+') {
                    DrawTile(r, px, py, CS, (c == '+') ? '.' : ' ');
                    DrawPlayer(r, px, py, CS);
                } else {
                    DrawTile(r, px, py, CS, c);
                }
            }

        r.SetDrawColorFloat(SDL::FColor{0.8f, 0.8f, 0.8f});
        if (!m_levels.empty())
            DrawText(r, rect.x + 8.f, rect.y + 4.f, 10.f,
                std::format("Niv {}/{}:  {}   Mvts:{}  Pouss:{}  Z=ann  N/P=niv  Tab=packs",
                    m_curLevel + 1, (int)m_levels.size(), m_levels[m_curLevel].name, m_moves, m_pushes));
        r.SetDrawColorFloat(SDL::FColor{0.6f, 0.8f, 0.6f});
        DrawText(r, rect.x + 8.f, rect.y + 18.f, 10.f,
            std::format("Caisses: {}/{}", m_boxOnTarget, m_boxCount));

        if (m_won) {
            float alpha = std::min(m_winAnim * 2.f, 0.75f);
            r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f, alpha}); r.RenderFillRect(rect);
            float scale = std::min(m_winAnim * 3.f, 1.f);
            r.SetDrawColorFloat(SDL::FColor{0.3f, 1.f, 0.4f});
            SDL::FPoint c = rect.GetCentroid();
            DrawTextC(r, {c.x, c.y - rect.h * 0.12f}, 28.f * scale, "NIVEAU REUSSI!");
            r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});
            const char* msg = (m_curLevel + 1 < (int)m_levels.size())
                ? "Entree = niveau suivant" : "Bravo! Tous les niveaux termines!";
            DrawTextC(r, {c.x, c.y + rect.h * 0.02f}, 14.f, msg);
            r.SetDrawColorFloat(SDL::FColor{0.7f, 0.7f, 0.7f});
            DrawTextC(r, {c.x, c.y + rect.h * 0.12f}, 11.f, "Tab = choisir un autre pack");
        }
    }

private:
    enum class State { PackSelect, Playing };
    struct Level { std::string name; std::vector<std::string> grid; };
    struct SokoMove { int dx, dy; bool pushedBox; int bfx, bfy; };

    std::vector<std::string> m_grid;
    int m_px = 0, m_py = 0, m_cols = 0, m_rows = 0;
    int m_boxCount = 0, m_boxOnTarget = 0;
    int m_curLevel = 0, m_moves = 0, m_pushes = 0, m_score = 0;
    std::stack<SokoMove> m_undo;
    std::vector<Level> m_levels;
    std::vector<std::string> m_packs;
    int m_packSel = 0;
    State m_state = State::PackSelect;
    float m_winAnim = 0.f;
    bool m_won = false, m_running = false;
    InputState m_input;

    char& Cell(int x, int y)       { return m_grid[y][x]; }
    char  Cell(int x, int y) const { return m_grid[y][x]; }

    void LoadPackList() {
        m_packs.clear();
        std::string path = std::string(SDL::GetBasePath()) + "assets/sokobans_levels";
        std::error_code ec;
        namespace fs = std::filesystem;
        for (auto& e : fs::directory_iterator(path, ec))
            if (e.is_regular_file() && e.path().extension() == ".txt")
                m_packs.push_back(e.path().filename().string());
        std::sort(m_packs.begin(), m_packs.end());
        m_packSel = 0;
    }

    void LoadPack(int idx) {
        m_levels.clear();
        if (idx < 0 || idx >= (int)m_packs.size()) return;
        std::string path = std::string(SDL::GetBasePath()) + "assets/sokobans_levels/" + m_packs[idx];
        std::ifstream f(path);
        if (!f.is_open()) return;
        Level cur; bool inLevel = false;
        auto flush = [&]() {
            if (inLevel && !cur.grid.empty()) {
                while (!cur.grid.empty() && cur.grid.back().find_first_not_of(' ') == std::string::npos)
                    cur.grid.pop_back();
                if (!cur.grid.empty()) m_levels.push_back(cur);
            }
        };
        std::string line;
        while (std::getline(f, line)) {
            if (line.size() >= 6 && line.substr(0, 6) == "Level ") {
                flush(); cur = Level{};
                auto dp = line.find(" - ");
                cur.name = (dp != std::string::npos) ? line.substr(dp + 3) : line;
                inLevel = true;
            } else if (inLevel) {
                if (line.empty() && cur.grid.empty()) continue;
                cur.grid.push_back(line);
            }
        }
        flush();
    }

    void LoadLevel(int idx) {
        if (idx < 0 || idx >= (int)m_levels.size()) return;
        m_curLevel = idx; m_grid = m_levels[idx].grid;
        m_rows = (int)m_grid.size(); m_cols = 0;
        for (auto& r : m_grid) m_cols = std::max(m_cols, (int)r.size());
        for (auto& r : m_grid) r.resize(m_cols, ' ');
        m_px = m_py = 0; m_boxCount = m_boxOnTarget = 0;
        for (int y = 0; y < m_rows; ++y)
            for (int x = 0; x < m_cols; ++x) {
                char c = Cell(x, y);
                if (c == '@' || c == '+') { m_px = x; m_py = y; }
                if (c == '$' || c == '*') m_boxCount++;
                if (c == '*') m_boxOnTarget++;
            }
        m_moves = m_pushes = 0;
        while (!m_undo.empty()) m_undo.pop();
        m_won = false; m_winAnim = 0.f; m_score = 0;
    }

    bool TryMove(int dx, int dy) {
        int nx = m_px + dx, ny = m_py + dy;
        if (nx < 0 || nx >= m_cols || ny < 0 || ny >= m_rows) return false;
        char dest = Cell(nx, ny);
        if (dest == '#') return false;
        SokoMove mv{dx, dy, false, 0, 0};
        if (dest == '$' || dest == '*') {
            int bx = nx + dx, by = ny + dy;
            if (bx < 0 || bx >= m_cols || by < 0 || by >= m_rows) return false;
            char bd = Cell(bx, by);
            if (bd == '#' || bd == '$' || bd == '*') return false;
            mv.pushedBox = true; mv.bfx = nx; mv.bfy = ny;
            Cell(nx, ny) = (dest == '*') ? '.' : ' ';
            Cell(bx, by) = (bd == '.') ? '*' : '$';
            m_pushes++;
            if (dest == '$' && Cell(bx, by) == '*') m_boxOnTarget++;
            if (dest == '*' && Cell(bx, by) == '$') m_boxOnTarget--;
        }
        char prev = Cell(m_px, m_py);
        Cell(m_px, m_py) = (prev == '+') ? '.' : ' ';
        bool dWasTarget = mv.pushedBox ? (dest == '*') : (dest == '.');
        Cell(nx, ny) = dWasTarget ? '+' : '@';
        m_px = nx; m_py = ny; m_moves++;
        m_undo.push(mv); return true;
    }

    void Undo() {
        if (m_undo.empty()) return;
        SokoMove mv = m_undo.top(); m_undo.pop();
        int nx = m_px, ny = m_py;
        int px = nx - mv.dx, py = ny - mv.dy;
        char cur = Cell(nx, ny);
        Cell(nx, ny) = (cur == '+') ? '.' : ' ';
        const auto& orig = m_levels[m_curLevel].grid;
        char op = (py < (int)orig.size() && px < (int)orig[py].size()) ? orig[py][px] : ' ';
        bool pWasTarget = (op == '.' || op == '+' || op == '*');
        Cell(px, py) = pWasTarget ? '+' : '@';
        m_px = px; m_py = py;
        if (mv.pushedBox) {
            int bx = mv.bfx + mv.dx, by = mv.bfy + mv.dy;
            char bc = Cell(bx, by);
            Cell(bx, by) = (bc == '*') ? '.' : ' ';
            char ob = (mv.bfy < (int)orig.size() && mv.bfx < (int)orig[mv.bfy].size())
                      ? orig[mv.bfy][mv.bfx] : ' ';
            Cell(mv.bfx, mv.bfy) = (ob == '.' || ob == '*') ? '*' : '$';
            m_pushes--;
        }
        m_boxOnTarget = 0;
        for (int y = 0; y < m_rows; ++y)
            for (int x = 0; x < m_cols; ++x)
                if (Cell(x, y) == '*') m_boxOnTarget++;
        m_moves = std::max(0, m_moves - 1); m_won = false;
    }

    bool IsWon() const { return m_boxOnTarget == m_boxCount && m_boxCount > 0; }

    void DrawTile(SDL::RendererRef r, float x, float y, float s, char tile) {
        switch (tile) {
        case '#':
            r.SetDrawColorFloat({0.38f, 0.30f, 0.22f}); r.RenderFillRect({{x, y, s, s}});
            r.SetDrawColorFloat({0.52f, 0.42f, 0.32f}); r.RenderFillRect({{x+1, y+1, s-4, s-4}});
            r.SetDrawColorFloat({0.25f, 0.20f, 0.15f}); r.RenderRect({{x, y, s, s}}); break;
        case ' ':
            r.SetDrawColorFloat({0.20f, 0.22f, 0.20f}); r.RenderFillRect({{x, y, s, s}}); break;
        case '.':
            r.SetDrawColorFloat({0.20f, 0.22f, 0.20f}); r.RenderFillRect({{x, y, s, s}});
            r.SetDrawColorFloat({0.90f, 0.55f, 0.10f}); r.RenderFillCircle({x+s*0.5f, y+s*0.5f}, s*0.25f);
            r.SetDrawColorFloat({0.70f, 0.40f, 0.05f}); r.RenderCircle({x+s*0.5f, y+s*0.5f}, s*0.35f); break;
        case '$':
            r.SetDrawColorFloat({0.60f, 0.40f, 0.18f}); r.RenderFillRect({{x+2, y+2, s-4, s-4}});
            r.SetDrawColorFloat({0.45f, 0.30f, 0.12f}); r.RenderRect({{x+2, y+2, s-4, s-4}});
            r.SetDrawColorFloat({0.35f, 0.22f, 0.08f});
            r.RenderLine({x+4, y+4}, {x+s-4, y+s-4}); r.RenderLine({x+s-4, y+4}, {x+4, y+s-4}); break;
        case '*':
            r.SetDrawColorFloat({0.20f, 0.70f, 0.20f}); r.RenderFillRect({{x+2, y+2, s-4, s-4}});
            r.SetDrawColorFloat({0.12f, 0.50f, 0.12f}); r.RenderRect({{x+2, y+2, s-4, s-4}});
            r.SetDrawColorFloat({0.08f, 0.35f, 0.08f});
            r.RenderLine({x+4, y+4}, {x+s-4, y+s-4}); r.RenderLine({x+s-4, y+4}, {x+4, y+s-4}); break;
        default:
            r.SetDrawColorFloat({0.20f, 0.22f, 0.20f}); r.RenderFillRect({{x, y, s, s}}); break;
        }
    }

    void DrawPlayer(SDL::RendererRef r, float x, float y, float s) {
        r.SetDrawColorFloat(SDL::FColor{0.25f, 0.45f, 0.85f});
        r.RenderFillRoundedRect({x + s*0.2f, y + s*0.35f, s*0.6f, s*0.55f}, SDL::FCorners(s*0.1f));
        r.SetDrawColorFloat(SDL::FColor{0.95f, 0.78f, 0.60f});
        r.RenderFillCircle({x + s*0.5f, y + s*0.22f}, s*0.18f);
    }

    void RenderPackSelect(SDL::RendererRef r, SDL::FRect rect) {
        r.SetDrawColorFloat(SDL::FColor{0.12f, 0.12f, 0.15f}); r.RenderFillRect(rect);
        r.SetDrawColorFloat(SDL::FColor{0.90f, 0.55f, 0.10f});
        DrawTextC(r, {rect.x + rect.w*0.5f, rect.y + 36.f}, 22.f, "Sokoban - Choisir un pack");
        r.SetDrawColorFloat(SDL::FColor{0.55f, 0.55f, 0.60f});
        DrawTextC(r, {rect.x + rect.w*0.5f, rect.y + 66.f}, 10.f, "Haut/Bas   Entree pour jouer");

        if (m_packs.empty()) {
            r.SetDrawColorFloat(SDL::FColor{0.80f, 0.30f, 0.30f});
            DrawTextC(r, {rect.x + rect.w*0.5f, rect.y + rect.h*0.5f}, 12.f,
                "Aucun pack trouve dans assets/sokobans_levels");
            return;
        }

        const float itemH  = 36.f;
        const float listH  = (float)m_packs.size() * itemH;
        const float startY = rect.y + (rect.h - listH) * 0.5f;
        const float listW  = rect.w * 0.55f;
        const float listX  = rect.x + (rect.w - listW) * 0.5f;

        for (int i = 0; i < (int)m_packs.size(); ++i) {
            float iy = startY + i * itemH;
            bool sel = (i == m_packSel);
            if (sel) {
                r.SetDrawColorFloat({0.20f, 0.48f, 0.20f, 0.45f});
                r.RenderFillRoundedRect({listX, iy + 2.f, listW, itemH - 4.f}, SDL::FCorners(7.f));
                r.SetDrawColorFloat({0.35f, 0.90f, 0.45f});
            } else {
                r.SetDrawColorFloat({0.70f, 0.70f, 0.75f});
            }
            std::string name = m_packs[i];
            if (name.size() > 4 && name.substr(name.size() - 4) == ".txt")
                name = name.substr(0, name.size() - 4);
            DrawTextC(r, {rect.x + rect.w * 0.5f, iy + itemH * 0.5f}, 13.f, name);
        }
    }
};
