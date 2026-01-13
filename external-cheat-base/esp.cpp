#include "esp.hpp"
#include "renderer/sdl_renderer.h"
#include <iostream>
#include <cmath>

bool esp::init()
{
    pID = memory::GetProcID(L"cs2.exe");
    if (!pID) {
        std::cout << "ERROR: Cannot find cs2.exe process!" << std::endl;
        return false;
    }
    std::cout << "Found cs2.exe, PID: " << pID << std::endl;

    modBase = memory::GetModuleBaseAddress(pID, L"client.dll");
    if (!modBase) {
        std::cout << "ERROR: Cannot find client.dll!" << std::endl;
        return false;
    }
    std::cout << "client.dll base: 0x" << std::hex << modBase << std::dec << std::endl;

    return true;
}

void esp::updateEntities()
{
    uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
    if (!entity_list) return;

    uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) return;

    // Read view matrix
    vm = memory::Read<viewMatrix>(modBase + cs2_dumper::offsets::client_dll::dwViewMatrix);

    // Get local player info
    uint8_t myTeam = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
    player_position = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin) +
        memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);

    std::vector<EnemyInfo> buffer;

    // Iterate entity list
    for (uint32_t i = 1; i < 64; i++)
    {
        uintptr_t listEntry = memory::Read<uintptr_t>(entity_list + 0x10 + 0x8 * (i >> 9));
        if (!listEntry) continue;

        uintptr_t entityController = memory::Read<uintptr_t>(listEntry + 0x70 * (i & 0x1FF));
        if (!entityController) continue;

        // Get pawn handle
        uint32_t pawnHandle = memory::Read<uint32_t>(entityController + cs2_dumper::schemas::client_dll::CBasePlayerController::m_hPawn);
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

        uint32_t pawnIndex = pawnHandle & 0x7FFF;
        uint32_t pawnChunk = pawnIndex >> 9;
        uint32_t pawnInChunk = pawnIndex & 0x1FF;

        uintptr_t pawnListEntry = memory::Read<uintptr_t>(entity_list + 0x10 + 0x8 * pawnChunk);
        if (!pawnListEntry) continue;

        uintptr_t entity = memory::Read<uintptr_t>(pawnListEntry + 0x70 * pawnInChunk);
        if (!entity || entity == localPlayerPawn) continue;

        uint8_t team = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
        int32_t health = memory::Read<int32_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iHealth);

        // Only draw enemy and alive players
        if (team == myTeam || health <= 0) continue;

        vec3 feetPos = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
        vec3 viewOffset = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
        vec3 headPos = feetPos + viewOffset;

        EnemyInfo enemy;
        enemy.position = feetPos;
        enemy.headPosition = headPos;
        enemy.health = health;
        enemy.distance = static_cast<float>(player_distance(player_position, feetPos));

        buffer.push_back(enemy);
    }

    enemies = buffer;
}

void esp::render()
{
    for (const auto& enemy : enemies)
    {
        vec2 screenFeet, screenHead;

        if (!w2s(enemy.position, screenFeet, vm.m)) continue;
        if (!w2s(enemy.headPosition, screenHead, vm.m)) continue;

        float height = screenFeet.y - screenHead.y;
        float width = height / 2.5f;

        int x = static_cast<int>(screenHead.x - width / 2);
        int y = static_cast<int>(screenHead.y);
        int w = static_cast<int>(width);
        int h = static_cast<int>(height);

        // Color based on distance
        // Close (< 10m) = Red
        // Medium (10-30m) = Yellow
        // Far (> 30m) = Green
        uint8_t r, g, b;
        float distanceMeters = enemy.distance / 100.0f;

        if (distanceMeters < 10.0f) {
            r = 255; g = 50; b = 50;
        } else if (distanceMeters < 30.0f) {
            r = 255; g = 255; b = 50;
        } else {
            r = 50; g = 255; b = 50;
        }

        // Draw box
        sdl_renderer::draw::box(x, y, w, h, r, g, b, 255);

        // Draw health bar background
        int healthBarWidth = w;
        int healthBarHeight = 4;
        int healthBarX = x;
        int healthBarY = y - 8;

        sdl_renderer::draw::filledBox(healthBarX, healthBarY, healthBarWidth, healthBarHeight, 50, 50, 50, 200);

        // Draw health bar
        int healthWidth = static_cast<int>((enemy.health / 100.0f) * healthBarWidth);
        uint8_t healthR = static_cast<uint8_t>(255 * (1.0f - enemy.health / 100.0f));
        uint8_t healthG = static_cast<uint8_t>(255 * (enemy.health / 100.0f));
        sdl_renderer::draw::filledBox(healthBarX, healthBarY, healthWidth, healthBarHeight, healthR, healthG, 0, 255);
    }
}

bool esp::w2s(const vec3& world, vec2& screen, float m[16])
{
    vec4 clipCoords;
    clipCoords.x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
    clipCoords.y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
    clipCoords.z = world.x * m[8] + world.y * m[9] + world.z * m[10] + m[11];
    clipCoords.w = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];

    if (clipCoords.w < 0.1f) return false;

    vec3 ndc;
    ndc.x = clipCoords.x / clipCoords.w;
    ndc.y = clipCoords.y / clipCoords.w;

    screen.x = (WINDOW_W / 2.0f * ndc.x) + WINDOW_W / 2.0f;
    screen.y = -(WINDOW_H / 2.0f * ndc.y) + WINDOW_H / 2.0f;

    return true;
}

double esp::player_distance(const vec3& a, const vec3& b)
{
    return std::sqrt(
        std::pow(a.x - b.x, 2) +
        std::pow(a.y - b.y, 2) +
        std::pow(a.z - b.z, 2)
    );
}