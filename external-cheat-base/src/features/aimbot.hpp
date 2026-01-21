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
    
    // Update RCS - should be called every frame
    void updateRCS();
    
    // Reset RCS state (when player stops shooting)
    void resetRCS();
}

