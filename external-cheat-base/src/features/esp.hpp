#pragma once

#include <vector>

#include "core/memory/memory.hpp"
#include "client_dll.hpp"
#include "offsets.hpp"
#include "utils/math/vector.hpp"
#include <string>

extern uint32_t WIDTH;
extern uint32_t HEIGHT;
extern uint32_t WINDOW_W;
extern uint32_t WINDOW_H;

struct viewMatrix
{
    float m[16];
};

// CS2 bone indices
namespace BoneIndex
{
    constexpr int PELVIS = 0;
    constexpr int SPINE_2 = 2;
    constexpr int NECK = 5;
    constexpr int HEAD = 6;
    constexpr int LEFT_SHOULDER = 8;
    constexpr int LEFT_ELBOW = 9;
    constexpr int LEFT_HAND = 11;
    constexpr int RIGHT_SHOULDER = 13;
    constexpr int RIGHT_ELBOW = 14;
    constexpr int RIGHT_HAND = 15;
    constexpr int LEFT_HIP = 17;
    constexpr int LEFT_KNEE = 18;
    constexpr int LEFT_FOOT = 19;
    constexpr int RIGHT_HIP = 20;
    constexpr int RIGHT_KNEE = 21;
    constexpr int RIGHT_FOOT = 22;
    constexpr int BONE_COUNT = 28;
}

// Bone connection pair
struct BoneConnection
{
    int from;
    int to;
};

// Enemy info structure
struct EnemyInfo
{
    vec3 position;
    vec3 headPosition;
    int32_t health;
    float distance;
    std::string weaponName;
    float viewYaw;
    float angleToPlayer;
    float flashDuration;
    bool isFlashed;
    bool isSpotted;
    vec3 bonePositions[BoneIndex::BONE_COUNT];
    bool hasBones;
};

// Cached local player info (updated once per frame, shared by aimbot/triggerbot)
struct LocalPlayerCache
{
    uintptr_t pawn = 0;           // Local player pawn address
    vec3 position{};              // Local player position
    vec3 eyePosition{};           // Local player eye position
    vec2 viewAngle{};             // Current view angles (pitch, yaw)
    bool isValid = false;         // Is cache valid this frame
};

// Bomb info structure
struct BombInfo
{
    bool isPlanted = false;
    bool isDefusing = false;
    bool hasExploded = false;
    bool isDefused = false;
    float blowTime = 0.0f;
    float defuseCountDown = 0.0f;
    float curtime = 0.0f;
    int bombSite = 0; // 0=A, 1=B
};

// World entity info (grenades, dropped weapons)
struct WorldEntityInfo
{
    vec3 position;
    int type; // 0=smoke, 1=flash, 2=HE, 3=molotov, 4=decoy, 5=weapon
    std::string name;
    float distance;
};

// Cached entity pawn address for fast-path updates
struct CachedPawn
{
    uintptr_t pawnAddress = 0;
    uint8_t team = 0;
    std::string weaponName;
    float flashDuration = 0.0f;
    bool isFlashed = false;
};

#include <mutex>

namespace esp
{
    inline std::vector<EnemyInfo> enemies;
    inline std::vector<WorldEntityInfo> worldEntities;
    inline viewMatrix vm = {};
    inline vec3 player_position{};
    inline float player_yaw = 0.0f;
    inline uintptr_t pID;
    inline uintptr_t modBase;

    // Mutex for thread-safe data access between data thread and render thread
    inline std::mutex dataMutex;

    // Cached pawn addresses (refreshed periodically)
    inline std::vector<CachedPawn> cachedPawns;
    inline int slowUpdateFrame = 0;

    // Cached local player data - updated once per frame
    inline LocalPlayerCache localPlayer;

    // Bomb state
    inline BombInfo bombInfo;

    bool init();
    void updateEntities();
    void refreshEntityCache();
    void render();
    void renderBombTimer();
    bool w2s(const vec3& world, vec2& screen, float m[16]);
    double player_distance(const vec3& a, const vec3& b);
    float normalizeAngle(float angle);
    float calculateYawToTarget(const vec3& from, const vec3& to);
    float calculateAngleToPlayer(float enemyYaw, const vec3& enemyPos, const vec3& playerPos);
}