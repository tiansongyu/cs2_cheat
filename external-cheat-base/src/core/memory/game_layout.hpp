#pragma once

#include "client_dll.hpp"
#include <cstddef>
#include <cstdint>

// cs2-dumper does not expose every container/layout detail used by an external
// reader. Keep the unavoidable values in one audited location and combine them
// with generated schema offsets wherever possible.
namespace game_layout
{
    constexpr uintptr_t ENTITY_LIST_CHUNK_ARRAY = 0x10;
    constexpr uintptr_t ENTITY_IDENTITY_STRIDE = 0x70;
    constexpr uint32_t ENTITY_CHUNK_SHIFT = 9;
    constexpr uint32_t ENTITY_CHUNK_MASK = 0x1FF;
    constexpr uint32_t ENTITY_HANDLE_MASK = 0x7FFF;
    constexpr uint32_t FIRST_PLAYER_CONTROLLER = 1;
    constexpr uint32_t LAST_PLAYER_CONTROLLER = 64;
    constexpr uint32_t FIRST_WORLD_ENTITY =
        LAST_PLAYER_CONTROLLER + 1;
    constexpr uint32_t MAX_WORLD_ENTITIES = ENTITY_HANDLE_MASK;

    constexpr uintptr_t BONE_ARRAY_IN_MODEL_STATE = 0x80;
    constexpr size_t BONE_STRIDE = 0x20;
    constexpr uintptr_t GLOBAL_VARS_CURRENT_TIME = 0x2C;
    // CGlobalVarsBase::mapname is not emitted by cs2-dumper. Keep this
    // pointer offset isolated and validate the pointed-to string before use.
    constexpr uintptr_t GLOBAL_VARS_MAP_NAME = 0x188;

    constexpr uintptr_t spottedFlagOffset()
    {
        return static_cast<uintptr_t>(
            cs2_dumper::schemas::client_dll::
                EntitySpottedState_t::m_bSpotted);
    }

    constexpr uintptr_t boneArrayPointerOffset()
    {
        return static_cast<uintptr_t>(
            cs2_dumper::schemas::client_dll::
                CSkeletonInstance::m_modelState) +
            BONE_ARRAY_IN_MODEL_STATE;
    }

}
