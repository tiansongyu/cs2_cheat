#pragma once

#include "core/memory/memory.hpp"
#include "utils/math/vector.hpp"
#include "offsets.hpp"
#include "client_dll.hpp"
#include <cstdint>

namespace aimbot
{
    // RCS state
    inline vec3 oldPunchAngle = { 0.0f, 0.0f, 0.0f };
    inline int oldShotsFired = 0;
    inline uintptr_t pID = 0;
    inline uintptr_t modBase = 0;

    // Initialize aimbot module (called after esp::init)
    bool init();

    // Update aimbot - should be called every frame
    void update();

    // Update RCS - should be called every frame
    void updateRCS();

    // Reset RCS state (when player stops shooting)
    void resetRCS();

    // Calculate angle to target
    vec2 calcAngle(const vec3& src, const vec3& dst);

    // Get FOV distance from crosshair to target
    float getFOV(const vec2& viewAngle, const vec2& aimAngle);

    // Normalize angle to -180 to 180
    float normalizeAngle(float angle);
}

