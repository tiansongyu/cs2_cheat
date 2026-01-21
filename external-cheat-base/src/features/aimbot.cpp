#include "aimbot.hpp"
#include "menu.hpp"
#include <Windows.h>
#include <iostream>
#include <cmath>

bool aimbot::init()
{
    // Get process info (shared with ESP)
    pID = memory::GetProcID(L"cs2.exe");
    if (!pID) {
        std::cout << "Aimbot: Cannot find cs2.exe process!" << std::endl;
        return false;
    }

    modBase = memory::GetModuleBaseAddress(pID, L"client.dll");
    if (!modBase) {
        std::cout << "Aimbot: Cannot find client.dll!" << std::endl;
        return false;
    }

    std::cout << "Aimbot module initialized successfully" << std::endl;
    return true;
}

void aimbot::resetRCS()
{
    oldPunchAngle = { 0.0f, 0.0f, 0.0f };
    oldShotsFired = 0;
}

void aimbot::updateRCS()
{
    // Check if RCS is enabled
    if (!menu::rcsEnabled) {
        resetRCS();
        return;
    }

    // Get local player pawn
    uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        resetRCS();
        return;
    }

    // Read shots fired count
    int shotsFired = memory::Read<int>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_iShotsFired);
    
    // If not shooting or just started, reset
    if (shotsFired <= 0) {
        resetRCS();
        return;
    }

    // Read current punch angle (recoil)
    vec3 punchAngle = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_aimPunchAngle);

    // Only apply RCS after first shot (shotsFired > 1 means we're in a spray)
    if (shotsFired > 1) {
        // Calculate delta between current and old punch angle
        vec3 deltaPunch;
        deltaPunch.x = punchAngle.x - oldPunchAngle.x;
        deltaPunch.y = punchAngle.y - oldPunchAngle.y;
        deltaPunch.z = 0.0f;

        // Apply sensitivity and strength multipliers
        // The punch angle is multiplied by 2 in-game, so we need to account for that
        float sensitivity = menu::rcsSensitivity;
        float strength = menu::rcsStrength / 100.0f;
        
        // Calculate mouse movement needed to compensate
        // Negative because we need to move opposite to recoil
        // 0.022f is roughly the CS2 mouse sensitivity factor
        float moveX = -deltaPunch.y * (sensitivity / 0.022f) * 2.0f * strength;
        float moveY = -deltaPunch.x * (sensitivity / 0.022f) * 2.0f * strength;

        // Apply smoothing
        float smoothing = menu::rcsSmoothing;
        moveX /= smoothing;
        moveY /= smoothing;

        // Move mouse if delta is significant
        if (std::abs(moveX) > 0.1f || std::abs(moveY) > 0.1f) {
            INPUT input = {};
            input.type = INPUT_MOUSE;
            input.mi.dwFlags = MOUSEEVENTF_MOVE;
            input.mi.dx = static_cast<LONG>(moveX);
            input.mi.dy = static_cast<LONG>(moveY);
            SendInput(1, &input, sizeof(INPUT));
        }
    }

    // Store current values for next frame
    oldPunchAngle = punchAngle;
    oldShotsFired = shotsFired;
}

