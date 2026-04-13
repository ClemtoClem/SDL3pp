#pragma once
#include <algorithm>
#include <cmath>

namespace core {

// ─────────────────────────────────────────────────────────────────────────────
// Camera — 2D viewport tracking
//
// World coordinates: (0,0) = top-left of map, 1 unit = 1 tile
// Entity positions  : center of entity in world units
// Screen coordinates: pixels, (0,0) = top-left of the canvas widget
// ─────────────────────────────────────────────────────────────────────────────

struct Camera {
    // World position of the viewport centre
    float worldX = 0.f, worldY = 0.f;

    // Canvas rect on screen (set by UI Canvas widget callback)
    float pixelX = 0.f, pixelY = 0.f;   // top-left
    float viewW  = 800.f, viewH = 600.f; // size

    // Current display tile size (pixels per tile)
    int dispTileSize = 64;

    // ── Transform ────────────────────────────────────────────────────────────

    /// Convert a world-space point to screen pixels (absolute, in window space).
    [[nodiscard]] float ScreenX(float wx) const noexcept {
        return (wx - worldX) * (float)dispTileSize + viewW * 0.5f + pixelX;
    }
    [[nodiscard]] float ScreenY(float wy) const noexcept {
        return (wy - worldY) * (float)dispTileSize + viewH * 0.5f + pixelY;
    }

    /// Convert screen pixels to world units.
    [[nodiscard]] float WorldX(float sx) const noexcept {
        return (sx - pixelX - viewW * 0.5f) / (float)dispTileSize + worldX;
    }
    [[nodiscard]] float WorldY(float sy) const noexcept {
        return (sy - pixelY - viewH * 0.5f) / (float)dispTileSize + worldY;
    }

    // ── Follow ───────────────────────────────────────────────────────────────

    /// Smoothly follow a target, clamped so the viewport never shows beyond map edges.
    void Follow(float targetX, float targetY, int mapW, int mapH, float lerp = 1.f) noexcept {
        float halfTilesX = viewW * 0.5f / (float)dispTileSize;
        float halfTilesY = viewH * 0.5f / (float)dispTileSize;

        float desiredX = std::clamp(targetX, halfTilesX, (float)mapW - halfTilesX);
        float desiredY = std::clamp(targetY, halfTilesY, (float)mapH - halfTilesY);

        if (lerp >= 1.f) {
            worldX = desiredX;
            worldY = desiredY;
        } else {
            worldX += (desiredX - worldX) * lerp;
            worldY += (desiredY - worldY) * lerp;
        }
    }

    // ── Visibility queries ───────────────────────────────────────────────────

    /// Returns true if a tile at (tx, ty) is inside (or near) the viewport.
    [[nodiscard]] bool IsTileVisible(int tx, int ty, int margin = 1) const noexcept {
        float sx = ScreenX((float)tx);
        float sy = ScreenY((float)ty);
        float ts = (float)dispTileSize;
        float m  = (float)(margin * dispTileSize);
        return sx + ts + m > pixelX         && sx - m < pixelX + viewW &&
               sy + ts + m > pixelY         && sy - m < pixelY + viewH;
    }

    /// Visible tile range [startX..endX) × [startY..endY) clamped to map size.
    void VisibleTileRange(int mapW, int mapH, int& startX, int& startY,
                          int& endX, int& endY, int margin = 1) const noexcept {
        float invTs = 1.f / (float)dispTileSize;
        startX = std::max(0, (int)((worldX - viewW * 0.5f * invTs)) - margin);
        startY = std::max(0, (int)((worldY - viewH * 0.5f * invTs)) - margin);
        endX   = std::min(mapW, (int)((worldX + viewW * 0.5f * invTs)) + margin + 2);
        endY   = std::min(mapH, (int)((worldY + viewH * 0.5f * invTs)) + margin + 2);
    }

    // ── Viewport update ──────────────────────────────────────────────────────

    void SetViewport(float x, float y, float w, float h) noexcept {
        pixelX = x; pixelY = y; viewW = w; viewH = h;
    }

    void SetDispTileSize(int sz) noexcept { dispTileSize = sz; }
};

} // namespace core
