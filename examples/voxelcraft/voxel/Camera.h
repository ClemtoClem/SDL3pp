#pragma once

#include "Chunk.h"
#include <SDL3pp/SDL3pp_math3D.h>
#include <cmath>

// Camera FPS (Y-up, regarde par défaut vers -Z).
// yaw=0 → -Z ; yaw tourne CCW autour de +Y.
// pitch > 0 → regard vers le bas (convention Vulkan/Y-down NDC).
class Camera {
public:
    SDL::FVector3 position{
        (float)(WORLD_CX * CHUNK_W) * 0.5f,
        26.f,
        (float)(WORLD_CZ * CHUNK_D) * 0.5f
    };
    float yaw   = 0.f;  // radians
    float pitch = 0.f;  // radians

    static constexpr float kPitchLimit = 1.55f; // ~89°

    SDL::FVector3 Forward() const noexcept {
        float cp = std::cos(pitch), sp = std::sin(pitch);
        float cy = std::cos(yaw),   sy = std::sin(yaw);
        return {cp * sy, -sp, cp * (-cy)};
    }

    SDL::FVector3 Right() const noexcept {
        return {std::cos(yaw), 0.f, std::sin(yaw)};
    }

    SDL::FMatrix4 GetView() const noexcept {
        SDL::FVector3 fwd = Forward();
        return SDL::FMatrix4::LookAt(position, position + fwd, {0.f, 1.f, 0.f});
    }

    void Rotate(float dYaw, float dPitch) noexcept {
        yaw   += dYaw;
        pitch += dPitch;
        if (pitch >  kPitchLimit) pitch =  kPitchLimit;
        if (pitch < -kPitchLimit) pitch = -kPitchLimit;
    }

    void Move(float right, float up, float forward) noexcept {
        SDL::FVector3 fwd = Forward();
        SDL::FVector3 rgt = Right();
        position += rgt * right;
        position.y += up;
        position += fwd * forward;
    }
};
