#include "aimbot.hpp"
#include "esp.hpp"
#include "menu.hpp"
#include <Windows.h>
#include <iostream>
#include <cmath>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

float aimbot::normalizeAngle(float angle)
{
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

vec2 aimbot::calcAngle(const vec3& src, const vec3& dst)
{
    // Delta = TargetPos - LocalEyePos
    vec3 delta = { dst.x - src.x, dst.y - src.y, dst.z - src.z };

    float hyp = std::sqrt(delta.x * delta.x + delta.y * delta.y);

    vec2 angle;
    // Correct CS2 formula:
    // Yaw = atan2(Δy, Δx) * 180/π
    angle.y = std::atan2(delta.y, delta.x) * (180.0f / static_cast<float>(M_PI));
    // Pitch = -atan2(Δz, sqrt(Δx² + Δy²)) * 180/π
    angle.x = -std::atan2(delta.z, hyp) * (180.0f / static_cast<float>(M_PI));

    return angle;
}

// Debug function to print angles
void debugPrintAngles(const char* label, float pitch, float yaw)
{
    static int frameCount = 0;
    if (++frameCount % 60 == 0) {  // Print every 60 frames
        std::cout << label << " Pitch: " << pitch << " Yaw: " << yaw << std::endl;
    }
}

float aimbot::getFOV(const vec2& viewAngle, const vec2& aimAngle)
{
    float pitchDiff = normalizeAngle(aimAngle.x - viewAngle.x);
    float yawDiff = normalizeAngle(aimAngle.y - viewAngle.y);

    return std::sqrt(pitchDiff * pitchDiff + yawDiff * yawDiff);
}

void aimbot::resetRCS()
{
    oldPunchAngle = { 0.0f, 0.0f, 0.0f };
    oldShotsFired = 0;
}

void aimbot::update()
{
    // Debug: check if function is called
    static int callCount = 0;

    // Check if aimbot is enabled
    if (!menu::aimbotEnabled) return;

    // Check if aimbot key is held (Shift)
    if (!(GetAsyncKeyState(menu::aimbotKey) & 0x8000)) return;

    // Debug: key is pressed
    if (++callCount % 30 == 0) {
        std::cout << "[Aimbot] Key pressed, enemies count: " << esp::enemies.size() << std::endl;
    }

    // Get local player pawn
    uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        if (callCount % 30 == 0) std::cout << "[Aimbot] No local player pawn!" << std::endl;
        return;
    }

    // Get local player view angles
    vec3 viewAngles = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
    vec2 currentViewAngle = { viewAngles.x, viewAngles.y };

    // Get local player eye position
    vec3 localPos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
    vec3 viewOffset = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
    vec3 eyePos = { localPos.x + viewOffset.x, localPos.y + viewOffset.y, localPos.z + viewOffset.z };

    // Find best target
    float bestFOV = menu::aimbotFOV;
    vec2 bestAngle = { 0.0f, 0.0f };
    bool foundTarget = false;

    for (const auto& enemy : esp::enemies)
    {
        // Skip if visible only mode and enemy is behind wall
        if (menu::aimbotVisibleOnly && !enemy.isSpotted) continue;

        // Get target position based on selected bone
        vec3 targetPos;
        switch (menu::aimbotBone)
        {
            case 0: // Head
                targetPos = enemy.headPosition;
                break;
            case 1: // Neck (slightly below head)
                targetPos = enemy.headPosition;
                targetPos.z -= 5.0f;
                break;
            case 2: // Chest (between head and feet)
                targetPos.x = (enemy.headPosition.x + enemy.position.x) / 2.0f;
                targetPos.y = (enemy.headPosition.y + enemy.position.y) / 2.0f;
                targetPos.z = (enemy.headPosition.z + enemy.position.z) / 2.0f;
                break;
            default:
                targetPos = enemy.headPosition;
                break;
        }

        // Calculate angle to target
        vec2 aimAngle = calcAngle(eyePos, targetPos);

        // Get FOV distance
        float fov = getFOV(currentViewAngle, aimAngle);

        // Check if this target is closer to crosshair
        if (fov < bestFOV)
        {
            bestFOV = fov;
            bestAngle = aimAngle;
            foundTarget = true;
        }
    }

    if (!foundTarget) {
        if (callCount % 30 == 0) std::cout << "[Aimbot] No target in FOV" << std::endl;
        return;
    }

    // Debug: print angles
    static int debugFrame = 0;
    if (++debugFrame % 30 == 0) {
        std::cout << "[Aimbot] ViewAngle: (" << currentViewAngle.x << ", " << currentViewAngle.y
                  << ") TargetAngle: (" << bestAngle.x << ", " << bestAngle.y << ")" << std::endl;
    }

    // Apply smoothing using linear interpolation (Lerp)
    // WriteAngle = CurrentAngle + (FinalAngle - CurrentAngle) / SmoothFactor
    float smoothing = menu::aimbotSmoothing;
    float smoothPitch = currentViewAngle.x + (bestAngle.x - currentViewAngle.x) / smoothing;
    float smoothYaw = currentViewAngle.y + normalizeAngle(bestAngle.y - currentViewAngle.y) / smoothing;

    // Calculate delta from current view to smoothed target
    float deltaPitch = smoothPitch - currentViewAngle.x;
    float deltaYaw = normalizeAngle(smoothYaw - currentViewAngle.y);

    // Convert angle delta to mouse movement
    // Formula: mouse_pixels = angle_degrees / (sensitivity * 0.022)
    // 0.022 is CS2's mouse sensitivity coefficient
    float sensitivity = menu::rcsSensitivity;
    float mouseSensitivityFactor = sensitivity * 0.022f;

    // In CS2/Source engine:
    // - Moving mouse RIGHT decreases Yaw (turns right)
    // - Moving mouse DOWN increases Pitch (looks down)
    // So we need to NEGATE deltaYaw for correct horizontal movement
    float moveX = -deltaYaw / mouseSensitivityFactor;  // Negated!
    float moveY = deltaPitch / mouseSensitivityFactor;

    // Debug
    if (debugFrame % 30 == 0) {
        std::cout << "[Aimbot] Delta: (" << deltaPitch << ", " << deltaYaw
                  << ") Move: (" << moveX << ", " << moveY << ")" << std::endl;
    }

    // Move mouse if delta is significant
    if (std::abs(moveX) > 0.1f || std::abs(moveY) > 0.1f)
    {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dx = static_cast<LONG>(moveX);
        input.mi.dy = static_cast<LONG>(moveY);
        SendInput(1, &input, sizeof(INPUT));
    }
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

void aimbot::updateTriggerbot()
{
    // Check if triggerbot is enabled
    if (!menu::triggerbotEnabled) {
        triggerbotHasTarget = false;
        return;
    }

    // Check if triggerbot key is held (Alt by default)
    if (!(GetAsyncKeyState(menu::triggerbotKey) & 0x8000)) {
        triggerbotHasTarget = false;
        return;
    }

    // Get local player pawn
    uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        triggerbotHasTarget = false;
        return;
    }

    // Get local player view angles
    vec3 viewAngles = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
    vec2 currentViewAngle = { viewAngles.x, viewAngles.y };

    // Get local player eye position
    vec3 localPos = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin);
    vec3 viewOffset = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset);
    vec3 eyePos = { localPos.x + viewOffset.x, localPos.y + viewOffset.y, localPos.z + viewOffset.z };

    // Find any visible enemy that is very close to crosshair (within small FOV)
    const float triggerbotFOV = 1.5f;  // Very small FOV - only trigger when almost on target
    bool foundTarget = false;

    for (const auto& enemy : esp::enemies)
    {
        // Skip enemies behind walls (only target visible enemies)
        if (!enemy.isSpotted) continue;

        // Target head position
        vec3 targetPos = enemy.headPosition;

        // Calculate angle to target
        vec2 aimAngle = calcAngle(eyePos, targetPos);

        // Get FOV distance
        float fov = getFOV(currentViewAngle, aimAngle);

        // Check if crosshair is on enemy head
        if (fov < triggerbotFOV)
        {
            foundTarget = true;

            // If this is a new target, record the time
            if (!triggerbotHasTarget) {
                triggerbotTargetTime = GetTickCount();
                triggerbotHasTarget = true;
            }

            // Check if delay has passed
            DWORD currentTime = GetTickCount();
            if (currentTime - triggerbotTargetTime >= static_cast<DWORD>(menu::triggerbotDelay))
            {
                // Aim at head first (snap to target)
                float smoothPitch = currentViewAngle.x + (aimAngle.x - currentViewAngle.x) / 2.0f;
                float smoothYaw = currentViewAngle.y + normalizeAngle(aimAngle.y - currentViewAngle.y) / 2.0f;

                float deltaPitch = smoothPitch - currentViewAngle.x;
                float deltaYaw = normalizeAngle(smoothYaw - currentViewAngle.y);

                float sensitivity = menu::rcsSensitivity;
                float mouseSensitivityFactor = sensitivity * 0.022f;

                float moveX = -deltaYaw / mouseSensitivityFactor;
                float moveY = deltaPitch / mouseSensitivityFactor;

                // Move mouse to aim at head
                if (std::abs(moveX) > 0.1f || std::abs(moveY) > 0.1f)
                {
                    INPUT moveInput = {};
                    moveInput.type = INPUT_MOUSE;
                    moveInput.mi.dwFlags = MOUSEEVENTF_MOVE;
                    moveInput.mi.dx = static_cast<LONG>(moveX);
                    moveInput.mi.dy = static_cast<LONG>(moveY);
                    SendInput(1, &moveInput, sizeof(INPUT));
                }

                // Fire! (left mouse click)
                INPUT clickInput = {};
                clickInput.type = INPUT_MOUSE;
                clickInput.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                SendInput(1, &clickInput, sizeof(INPUT));

                // Release click
                clickInput.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &clickInput, sizeof(INPUT));

                // Reset to allow next shot with delay
                triggerbotTargetTime = currentTime;
            }
            break;  // Only process first found target
        }
    }

    if (!foundTarget) {
        triggerbotHasTarget = false;
    }
}

