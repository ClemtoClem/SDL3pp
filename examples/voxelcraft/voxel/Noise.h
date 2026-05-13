#pragma once
#include <cmath>

// Value-noise generator for 2D and 3D terrain height maps.
class Noise {
    float Hash2D(int x, int z) const noexcept {
        unsigned n = (unsigned)(x * 1619 + z * 31337 + 1013904223u);
        n = (n << 13) ^ n;
        return 1.f - (float)((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu)
                     / 1073741824.f;
    }

    float Hash3D(int x, int y, int z) const noexcept {
        unsigned n = (unsigned)(x * 1619 + y * 23497 + z * 31337 + 1013904223u);
        n = (n << 13) ^ n;
        return 1.f - (float)((n * (n * n * 15731u + 789221u) + 1376312589u) & 0x7fffffffu)
                     / 1073741824.f;
    }

    float Smooth(float t) const noexcept { return t * t * (3.f - 2.f * t); }
    float Lerp(float a, float b, float t) const noexcept { return a + (b - a) * t; }

public:
    // 2D value noise sample
    float Sample(float x, float z) const noexcept {
        int   xi = (int)std::floor(x);
        int   zi = (int)std::floor(z);
        float fx = Smooth(x - xi);
        float fz = Smooth(z - zi);
        return Lerp(Lerp(Hash2D(xi, zi),   Hash2D(xi+1, zi),   fx),
                    Lerp(Hash2D(xi, zi+1), Hash2D(xi+1, zi+1), fx), fz);
    }

    // 3D value noise sample (trilinear)
    float Sample3D(float x, float y, float z) const noexcept {
        int xi = (int)std::floor(x), yi = (int)std::floor(y), zi = (int)std::floor(z);
        float fx = Smooth(x-xi), fy = Smooth(y-yi), fz = Smooth(z-zi);
        float c00 = Lerp(Hash3D(xi,yi,zi),   Hash3D(xi+1,yi,zi),   fx);
        float c01 = Lerp(Hash3D(xi,yi,zi+1), Hash3D(xi+1,yi,zi+1), fx);
        float c10 = Lerp(Hash3D(xi,yi+1,zi), Hash3D(xi+1,yi+1,zi), fx);
        float c11 = Lerp(Hash3D(xi,yi+1,zi+1),Hash3D(xi+1,yi+1,zi+1),fx);
        return Lerp(Lerp(c00,c10,fy), Lerp(c01,c11,fy), fz);
    }

    // 2D fractional Brownian motion
    float Fbm(float x, float z, float scale = 0.04f,
              int octaves = 4, float persistence = 0.5f) const noexcept {
        float total = 0, maxV = 0, amp = 1, freq = scale;
        for (int i = 0; i < octaves; ++i) {
            total += Sample(x * freq, z * freq) * amp;
            maxV  += amp;
            amp   *= persistence;
            freq  *= 2.f;
        }
        return total / maxV; // [-1, 1]
    }

    // 3D fractional Brownian motion (for cave carving, ore placement)
    float Fbm3D(float x, float y, float z, float scale = 0.04f,
                int octaves = 3, float persistence = 0.5f) const noexcept {
        float total = 0, maxV = 0, amp = 1, freq = scale;
        for (int i = 0; i < octaves; ++i) {
            total += Sample3D(x * freq, y * freq, z * freq) * amp;
            maxV  += amp;
            amp   *= persistence;
            freq  *= 2.f;
        }
        return total / maxV;
    }
};
