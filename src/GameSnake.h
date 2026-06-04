#pragma once

#include "Common.h"
#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <random>
#include <utility>
#include <vector>

class GameSnake final : public IGame {
public:
    GameSnake() : m_rng(std::random_device{}()) {}

    const char* Title()       const override { return "Snake"; }
    const char* Description() const override { return "Mangez la nourriture sans vous mordre!"; }
    int  Score()               const override { return m_score; }

    void Start() override { m_running = true; Reset(); }
    void Pause() override { m_running = false; }

    void Reset() override {
        m_gameOver = false; m_score = 0;
        m_moveTimer = 0; m_interval = 0.14f;
        m_deathFlash = 0.f; m_foodAnim = 0.f;
        m_snake.clear();
        m_snake.push_back({COLS / 2,     ROWS / 2});
        m_snake.push_back({COLS / 2 - 1, ROWS / 2});
        m_snake.push_back({COLS / 2 - 2, ROWS / 2});
        m_dir = m_nextDir = Dir::Right;
        SpawnFood(); m_running = true;
    }

    void OnEvent(const SDL::Event& ev) override { m_input.Feed(ev); }

    void Update(float dt) override {
        if (!m_running) { m_input.Flush(); return; }
        m_foodAnim += (float)(dt * 3.0);

        if (m_gameOver) {
            m_deathFlash += (float)(dt * 4.0);
            if (m_input.JP(SDL::KEYCODE_R)) Reset();
            m_input.Flush(); return;
        }

        auto tryDir = [&](Dir d) {
            if (m_dir == Dir::Up    && d == Dir::Down)  return;
            if (m_dir == Dir::Down  && d == Dir::Up)    return;
            if (m_dir == Dir::Left  && d == Dir::Right) return;
            if (m_dir == Dir::Right && d == Dir::Left)  return;
            m_nextDir = d;
        };
        if (m_input.JP(SDL::KEYCODE_UP)    || m_input.JP(SDL::KEYCODE_W)) tryDir(Dir::Up);
        if (m_input.JP(SDL::KEYCODE_DOWN)  || m_input.JP(SDL::KEYCODE_S)) tryDir(Dir::Down);
        if (m_input.JP(SDL::KEYCODE_LEFT)  || m_input.JP(SDL::KEYCODE_A)) tryDir(Dir::Left);
        if (m_input.JP(SDL::KEYCODE_RIGHT) || m_input.JP(SDL::KEYCODE_D)) tryDir(Dir::Right);
        m_input.Flush();

        m_moveTimer += dt;
        if (m_moveTimer < m_interval) return;
        m_moveTimer = 0;
        m_dir = m_nextDir;

        auto [hc, hr] = m_snake.front();
        int nc = hc, nr = hr;
        if      (m_dir == Dir::Up)    nr--;
        else if (m_dir == Dir::Down)  nr++;
        else if (m_dir == Dir::Left)  nc--;
        else                          nc++;

        if (nc < 0 || nc >= COLS || nr < 0 || nr >= ROWS)
            { m_gameOver = true; m_deathFlash = 0.f; return; }
        for (auto [c, r] : m_snake)
            if (c == nc && r == nr) { m_gameOver = true; m_deathFlash = 0.f; return; }

        m_snake.push_front({nc, nr});
        if (nc == m_foodC && nr == m_foodR) {
            m_score += 10;
            m_interval = std::max(0.06f, m_interval - 0.002f);
            SpawnFood();
        } else m_snake.pop_back();
    }

    void Render(SDL::RendererRef r, SDL::FRect rect) override {
        r.SetDrawColorFloat(SDL::FColor{0.10f, 0.12f, 0.10f}); r.RenderFillRect(rect);

        const float CS = std::min(rect.w / COLS, rect.h / ROWS);
        const float GW = CS * COLS, GH = CS * ROWS;
        const float ox = rect.x + (rect.w - GW) * 0.5f;
        const float oy = rect.y + (rect.h - GH) * 0.5f;

        for (int rr = 0; rr < ROWS; ++rr)
            for (int c = 0; c < COLS; ++c) {
                bool even = (c + rr) % 2 == 0;
                r.SetDrawColorFloat({even ? 0.11f : 0.09f, even ? 0.13f : 0.11f, even ? 0.11f : 0.09f, 1.f});
                r.RenderFillRect({{ox + c * CS, oy + rr * CS, CS, CS}});
            }

        r.SetDrawColorFloat(SDL::FColor{0.25f, 0.50f, 0.25f});
        r.RenderRect({{ox - 1.f, oy - 1.f, GW + 2.f, GH + 2.f}});

        {
            float pulse = 0.85f + 0.15f * std::sin(m_foodAnim);
            float fw = CS * pulse, fh = CS * pulse;
            float fx = ox + m_foodC * CS + (CS - fw) * 0.5f;
            float fy = oy + m_foodR * CS + (CS - fh) * 0.5f;
            r.SetDrawColorFloat(SDL::FColor{0.90f, 0.15f, 0.15f});
            r.RenderFillCircle({fx + fw * 0.5f, fy + fh * 0.5f}, fw * 0.4f);
            r.SetDrawColorFloat(SDL::FColor{1.f, 0.6f, 0.6f, 0.6f});
            r.RenderFillCircle({fx + fw * 0.32f, fy + fh * 0.28f}, fw * 0.12f);
        }

        if (m_gameOver && m_deathFlash < 10.f) {
            float alpha = 0.5f + 0.5f * std::sin(m_deathFlash * 3.14f);
            for (auto [c, rr] : m_snake) {
                r.SetDrawColorFloat(SDL::FColor{0.9f, 0.3f, 0.3f, alpha});
                r.RenderFillRect({{ox + c * CS + 1.f, oy + rr * CS + 1.f, CS - 2.f, CS - 2.f}});
            }
        } else {
            for (size_t i = 0; i < m_snake.size(); ++i) {
                auto [c, rr] = m_snake[i];
                float t = 1.f - (float)i / (float)m_snake.size();
                if (i == 0) {
                    r.SetDrawColorFloat(SDL::FColor{0.20f, 0.80f, 0.20f});
                    r.RenderFillRect({{ox + c * CS + 1.f, oy + rr * CS + 1.f, CS - 2.f, CS - 2.f}});
                    r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f});
                    float ew = CS * 0.14f;
                    float ex1 = ox + c * CS + CS * 0.25f, ex2 = ox + c * CS + CS * 0.62f;
                    float ey = oy + rr * CS + CS * 0.25f;
                    if (m_dir == Dir::Down) ey = oy + rr * CS + CS * 0.55f;
                    r.RenderFillCircle({ex1 + ew / 2, ey + ew / 2}, ew / 2);
                    r.RenderFillCircle({ex2 + ew / 2, ey + ew / 2}, ew / 2);
                } else {
                    float g = 0.25f + t * 0.55f;
                    r.SetDrawColorFloat(SDL::FColor{0.05f, g, 0.05f});
                    float gap = CS * 0.12f;
                    r.RenderFillRoundedRect(
                        {ox + c * CS + gap, oy + rr * CS + gap, CS - gap * 2, CS - gap * 2},
                        SDL::FCorners(CS * 0.2f));
                }
            }
        }

        r.SetDrawColorFloat(SDL::FColor{0.8f, 0.9f, 0.8f});
        DrawText(r, rect.x + 8.f, rect.y + 6.f, 12.f,
            std::format("Score: {}  Longueur: {}", m_score, (int)m_snake.size()));

        if (m_gameOver) {
            r.SetDrawColorFloat(SDL::FColor{0.f, 0.f, 0.f, 0.65f}); r.RenderFillRect(rect);
            r.SetDrawColorFloat(SDL::FColor{1.f, 0.3f, 0.3f});
            SDL::FPoint c = rect.GetCentroid();
            DrawTextC(r, {c.x, c.y - rect.h * 0.08f}, 26.f, "GAME OVER");
            r.SetDrawColorFloat(SDL::FColor{1.f, 1.f, 1.f});
            DrawTextC(r, {c.x, c.y + rect.h * 0.04f}, 13.f, std::format("Score final: {}", m_score));
            r.SetDrawColorFloat(SDL::FColor{0.7f, 0.7f, 0.7f});
            DrawTextC(r, {c.x, c.y + rect.h * 0.13f}, 11.f, "R pour rejouer");
        }
    }

private:
    static constexpr int COLS = 20, ROWS = 20;
    enum class Dir { Up, Down, Left, Right };

    std::deque<std::pair<int,int>> m_snake;
    Dir m_dir = Dir::Right, m_nextDir = Dir::Right;
    int m_foodC = 0, m_foodR = 0, m_score = 0;
    float m_moveTimer = 0, m_interval = 0.14f, m_deathFlash = 0.f, m_foodAnim = 0.f;
    bool m_gameOver = false, m_running = false;
    std::mt19937 m_rng;
    InputState m_input;

    void SpawnFood() {
        std::vector<std::pair<int,int>> free;
        for (int rr = 0; rr < ROWS; ++rr)
            for (int c = 0; c < COLS; ++c) {
                bool occ = false;
                for (auto [sc, sr] : m_snake) if (sc == c && sr == rr) { occ = true; break; }
                if (!occ) free.push_back({c, rr});
            }
        if (free.empty()) { m_gameOver = true; return; }
        auto [fc, fr] = free[std::uniform_int_distribution<int>(0, (int)free.size() - 1)(m_rng)];
        m_foodC = fc; m_foodR = fr;
    }
};
