#define NOMINMAX  // Prevent Windows.h from defining min/max macros

#include "aimbot.hpp"
#include "esp.hpp"
#include "menu.hpp"
#include <Windows.h>
#include <iostream>
#include <cmath>
#include <limits>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Calculate head offset for side-facing enemies
// When enemy is facing sideways (angle 45-135 degrees to player),
// the head extends forward in the direction they are facing
vec3 calculateHeadOffset(const vec3& headPos, float enemyViewYaw, float angleToPlayer)
{
    // Check if head offset is enabled
    if (!menu::headOffsetEnabled) {
        return headPos;
    }

    // Check if enemy is within the angle range for offset
    if (angleToPlayer < menu::headOffsetAngleMin || angleToPlayer > menu::headOffsetAngleMax) {
        return headPos;
    }

    // Calculate offset strength based on angle
    // Maximum offset at 90 degrees (perfectly sideways), less at edges
    float centerAngle = (menu::headOffsetAngleMin + menu::headOffsetAngleMax) / 2.0f;
    float angleRange = (menu::headOffsetAngleMax - menu::headOffsetAngleMin) / 2.0f;
    float angleFactor = 1.0f - std::abs(angleToPlayer - centerAngle) / angleRange;
    angleFactor = std::max(0.0f, std::min(1.0f, angleFactor));  // Clamp to 0-1

    // Calculate offset direction (perpendicular to enemy's facing direction)
    // Enemy viewYaw: 0 = facing +X, 90 = facing +Y, etc.
    float yawRadians = enemyViewYaw * static_cast<float>(M_PI) / 180.0f;

    // Head extends in the direction enemy is facing
    float offsetX = std::cos(yawRadians) * menu::headOffsetAmount * angleFactor;
    float offsetY = std::sin(yawRadians) * menu::headOffsetAmount * angleFactor;

    vec3 adjustedHead = headPos;
    adjustedHead.x += offsetX;
    adjustedHead.y += offsetY;
    // Z offset is typically not needed as head height stays same

    return adjustedHead;
}

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
    // Check if aimbot is enabled
    if (!menu::aimbotEnabled) return;

    // Check if aimbot key is held (Shift)
    if (!(GetAsyncKeyState(menu::aimbotKey) & 0x8000)) return;

    // Use cached local player data (updated once per frame in esp::updateEntities)
    if (!esp::localPlayer.isValid) return;

    const vec2& currentViewAngle = esp::localPlayer.viewAngle;
    const vec3& eyePos = esp::localPlayer.eyePosition;

    vec2 bestAngle = { 0.0f, 0.0f };
    bool foundTarget = false;

    if (menu::smartAimEnabled) {
        // Smart Aim Mode: Ignore FOV, select best visible target by priority
        float bestScore = 999999.0f;  // Lower is better

        for (const auto& enemy : esp::enemies)
        {
            // Smart Aim ONLY targets visible enemies (not behind walls)
            if (!enemy.isSpotted) continue;

            // Calculate priority score based on selected mode
            float score;
            if (menu::smartAimPriority == 0) {
                // Distance first: closer enemies have lower score
                score = enemy.distance;
            } else {
                // Health first: lower HP enemies have lower score
                // Use distance as tiebreaker (add small distance factor)
                score = static_cast<float>(enemy.health) + enemy.distance * 0.001f;
            }

            if (score < bestScore)
            {
                bestScore = score;

                // Get target position based on selected bone
                vec3 targetPos;
                switch (menu::aimbotBone)
                {
                    case 0: // Head
                        targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);
                        break;
                    case 1: // Neck
                        targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);
                        targetPos.z -= 5.0f;
                        break;
                    case 2: // Chest
                        targetPos.x = (enemy.headPosition.x + enemy.position.x) / 2.0f;
                        targetPos.y = (enemy.headPosition.y + enemy.position.y) / 2.0f;
                        targetPos.z = (enemy.headPosition.z + enemy.position.z) / 2.0f;
                        break;
                    default:
                        targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);
                        break;
                }

                bestAngle = calcAngle(eyePos, targetPos);
                foundTarget = true;
            }
        }
    }
    else {
        // Normal Mode: Use FOV to find closest target to crosshair
        float bestFOV = menu::aimbotFOV;

        for (const auto& enemy : esp::enemies)
        {
            // Skip if visible only mode and enemy is behind wall
            if (menu::aimbotVisibleOnly && !enemy.isSpotted) continue;

            // Get target position based on selected bone
            vec3 targetPos;
            switch (menu::aimbotBone)
            {
                case 0: // Head
                    targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);
                    break;
                case 1: // Neck
                    targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);
                    targetPos.z -= 5.0f;
                    break;
                case 2: // Chest
                    targetPos.x = (enemy.headPosition.x + enemy.position.x) / 2.0f;
                    targetPos.y = (enemy.headPosition.y + enemy.position.y) / 2.0f;
                    targetPos.z = (enemy.headPosition.z + enemy.position.z) / 2.0f;
                    break;
                default:
                    targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);
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
    }

    if (!foundTarget) return;

    // Apply smoothing using linear interpolation (Lerp)
    float smoothing = menu::aimbotSmoothing;
    float deltaPitch = (bestAngle.x - currentViewAngle.x) / smoothing;
    float deltaYaw = normalizeAngle(bestAngle.y - currentViewAngle.y) / smoothing;

    // Convert angle delta to mouse movement
    float mouseSensitivityFactor = menu::rcsSensitivity * 0.022f;

    // In CS2: Moving mouse RIGHT decreases Yaw, DOWN increases Pitch
    float moveX = -deltaYaw / mouseSensitivityFactor;
    float moveY = deltaPitch / mouseSensitivityFactor;

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

    // Use cached local player data
    if (!esp::localPlayer.isValid) {
        resetRCS();
        return;
    }

    // Use cached shots fired count
    int shotsFired = esp::localPlayer.shotsFired;

    // If not shooting, reset
    if (shotsFired <= 0) {
        resetRCS();
        return;
    }

    // Use cached punch angle
    const vec3& punchAngle = esp::localPlayer.punchAngle;

    // Only apply RCS when we have previous data and shots > 1
    if (oldShotsFired > 0 && shotsFired > oldShotsFired) {
        // Calculate delta between current and old punch angle
        float deltaPunchX = punchAngle.x - oldPunchAngle.x;
        float deltaPunchY = punchAngle.y - oldPunchAngle.y;

        // Skip if punch angle didn't change significantly
        if (std::abs(deltaPunchX) < 0.001f && std::abs(deltaPunchY) < 0.001f) {
            oldPunchAngle = punchAngle;
            oldShotsFired = shotsFired;
            return;
        }

        // Apply sensitivity and strength multipliers
        // In CS2, the visual recoil is 2x the punch angle
        // Mouse sensitivity formula: pixels = angle / (sensitivity * 0.022)
        float sensitivity = menu::rcsSensitivity;
        float strength = menu::rcsStrength / 100.0f;
        float smoothing = menu::rcsSmoothing;

        // Calculate mouse movement to counteract recoil
        // In CS2:
        //   deltaPunchX (pitch): negative = view kicks UP, so we need mouse DOWN (positive dy)
        //   deltaPunchY (yaw): positive = view kicks LEFT, so we need mouse RIGHT (positive dx)
        // Factor: 2.0 because visual recoil = 2x punch angle
        // We NEGATE the delta to move OPPOSITE to the recoil direction
        float pixelsPerDegree = 1.0f / (sensitivity * 0.022f);
        float moveY = -deltaPunchX * 2.0f * pixelsPerDegree * strength / smoothing;
        float moveX = -deltaPunchY * 2.0f * pixelsPerDegree * strength / smoothing;

        // Move mouse if delta is significant
        if (std::abs(moveX) > 0.5f || std::abs(moveY) > 0.5f) {
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

    // Use cached local player data
    if (!esp::localPlayer.isValid) {
        triggerbotHasTarget = false;
        return;
    }

    const vec2& currentViewAngle = esp::localPlayer.viewAngle;
    const vec3& eyePos = esp::localPlayer.eyePosition;

    // Find any visible enemy that is very close to crosshair (within small FOV)
    const float triggerbotFOV = 1.5f;  // Very small FOV - only trigger when almost on target
    bool foundTarget = false;

    for (const auto& enemy : esp::enemies)
    {
        // Skip enemies behind walls (only target visible enemies)
        if (!enemy.isSpotted) continue;

        // Target head position with offset compensation for side-facing enemies
        vec3 targetPos = calculateHeadOffset(enemy.headPosition, enemy.viewYaw, enemy.angleToPlayer);

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
                float deltaPitch = (aimAngle.x - currentViewAngle.x) / 2.0f;
                float deltaYaw = normalizeAngle(aimAngle.y - currentViewAngle.y) / 2.0f;

                float mouseSensitivityFactor = menu::rcsSensitivity * 0.022f;

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

