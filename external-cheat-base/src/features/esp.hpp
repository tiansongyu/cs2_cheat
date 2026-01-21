#pragma once

#include <vector>

#include "core/memory/memory.h"
#include "client_dll.hpp"
#include "offsets.hpp"
#include "utils/math/vector.h"
#include <string>

extern uint32_t WIDTH;
extern uint32_t HEIGHT;
extern uint32_t WINDOW_W;
extern uint32_t WINDOW_H;

struct viewMatrix
{
    float m[16];
};

// Enemy info structure
struct EnemyInfo
{
    vec3 position;
    vec3 headPosition;
    int32_t health;
    float distance;
    std::string weaponName;  // Current weapon name
    float viewYaw;           // Enemy's view yaw angle
    float angleToPlayer;     // Angle difference between enemy view and player direction
    float flashDuration;     // Flashbang blind duration (> 0 means flashed)
    bool isFlashed;          // Is enemy currently flashed
};

namespace esp
{
    inline std::vector<EnemyInfo> enemies;
    inline viewMatrix vm = {};
    inline vec3 player_position{};
    inline uintptr_t pID;
    inline uintptr_t modBase;

    bool init();
    void updateEntities();
    void render();
    bool w2s(const vec3& world, vec2& screen, float m[16]);
    double player_distance(const vec3& a, const vec3& b);
    float normalizeAngle(float angle);
    float calculateYawToTarget(const vec3& from, const vec3& to);
    float calculateAngleToPlayer(float enemyYaw, const vec3& enemyPos, const vec3& playerPos);
}