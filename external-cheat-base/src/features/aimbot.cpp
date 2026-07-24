#ifndef NOMINMAX
#define NOMINMAX  // Prevent Windows.h from defining min/max macros
#endif

#include "aimbot.hpp"
#include "esp.hpp"
#include "menu.hpp"
#include <Windows.h>
#include <iostream>
#include <cmath>
#include <limits>
#include <algorithm>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace
{
    using SteadyClock = std::chrono::steady_clock;

    SteadyClock::time_point lastAimUpdate{};
    SteadyClock::time_point lastTriggerUpdate{};
    float aimResidualX = 0.0f;
    float aimResidualY = 0.0f;
    float triggerResidualX = 0.0f;
    float triggerResidualY = 0.0f;

    float timeBasedAlpha(
        SteadyClock::time_point& previous,
        float smoothing)
    {
        const auto now = SteadyClock::now();
        float deltaSeconds = 1.0f / 144.0f;
        if (previous.time_since_epoch().count() != 0) {
            deltaSeconds = std::chrono::duration<float>(
                now - previous).count();
        }
        previous = now;
        deltaSeconds = std::clamp(
            deltaSeconds,
            1.0f / 1000.0f,
            0.05f);
        const float safeSmoothing =
            std::max(1.0f, smoothing);
        const float responsePerSecond =
            60.0f / safeSmoothing;
        return 1.0f -
            std::exp(-responsePerSecond * deltaSeconds);
    }

    LONG consumeMouseDelta(float value, float& residual)
    {
        const float accumulated = value + residual;
        const LONG whole = static_cast<LONG>(
            std::trunc(accumulated));
        residual = accumulated - static_cast<float>(whole);
        return whole;
    }

    void resetAimState()
    {
        lastAimUpdate = {};
        aimResidualX = 0.0f;
        aimResidualY = 0.0f;
    }

    void resetTriggerState()
    {
        lastTriggerUpdate = {};
        triggerResidualX = 0.0f;
        triggerResidualY = 0.0f;
    }
}

// Calculate head offset for side-facing enemies
// When enemy is facing sideways (angle 45-135 degrees to player),
// the head extends forward in the direction they are facing
vec3 calculateHeadOffset(
    const vec3& headPos,
    float enemyViewYaw,
    float angleToPlayer,
    const menu::RuntimeConfig& config)
{
    // Check if head offset is enabled
    if (!config.headOffsetEnabled) {
        return headPos;
    }

    // Check if enemy is within the angle range for offset
    if (angleToPlayer < config.headOffsetAngleMin ||
        angleToPlayer > config.headOffsetAngleMax) {
        return headPos;
    }

    // Calculate offset strength based on angle
    // Maximum offset at 90 degrees (perfectly sideways), less at edges
    float centerAngle =
        (config.headOffsetAngleMin + config.headOffsetAngleMax) / 2.0f;
    float angleRange =
        (config.headOffsetAngleMax - config.headOffsetAngleMin) / 2.0f;
    float angleFactor = angleRange > 0.0001f
        ? 1.0f - std::abs(angleToPlayer - centerAngle) / angleRange
        : 1.0f;
    angleFactor = std::max(0.0f, std::min(1.0f, angleFactor));  // Clamp to 0-1

    // Calculate offset direction (perpendicular to enemy's facing direction)
    // Enemy viewYaw: 0 = facing +X, 90 = facing +Y, etc.
    float yawRadians = enemyViewYaw * static_cast<float>(M_PI) / 180.0f;

    // Head extends in the direction enemy is facing
    float offsetX = std::cos(yawRadians) * config.headOffsetAmount * angleFactor;
    float offsetY = std::sin(yawRadians) * config.headOffsetAmount * angleFactor;

    vec3 adjustedHead = headPos;
    adjustedHead.x += offsetX;
    adjustedHead.y += offsetY;
    // Z offset is typically not needed as head height stays same

    return adjustedHead;
}

bool aimbot::init()
{
    // Reuse the already validated read-only process handle and module base.
    // Reopening here could invalidate a working ESP handle on a transient
    // OpenProcess failure.
    pID = esp::pID;
    modBase = esp::modBase;
    return pID != 0 && modBase != 0;
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


void aimbot::update(const menu::RuntimeConfig& config)
{
    if (!config.aimbotEnabled ||
        config.inputSuppressed ||
        !sdl_renderer::isInputAllowed()) {
        resetAimState();
        return;
    }

    // Check if aimbot key is held (Shift)
    if (!(GetAsyncKeyState(config.aimbotKey) & 0x8000)) {
        resetAimState();
        return;
    }

    // Use cached local player data (updated once per frame in esp::updateEntities)
    if (!esp::localPlayer.isValid) {
        resetAimState();
        return;
    }

    const vec2& currentViewAngle = esp::localPlayer.viewAngle;
    const vec3& eyePos = esp::localPlayer.eyePosition;

    vec2 bestAngle = { 0.0f, 0.0f };
    bool foundTarget = false;
    const esp::EnemySnapshot enemies =
        esp::getEnemySnapshot();
    if (!enemies) {
        resetAimState();
        return;
    }

    if (config.smartAimEnabled) {
        // Smart Aim Mode: Ignore FOV, select the best spotted target.
        float bestScore = 999999.0f;  // Lower is better

        for (const auto& enemy : *enemies)
        {
            // Unknown state never passes the conservative spotted filter.
            if (!enemy.visibilityKnown || !enemy.isSpotted) continue;

            // Calculate priority score based on selected mode
            float score;
            if (config.smartAimPriority == 0) {
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
                switch (config.aimbotBone)
                {
                    case 0: // Head
                        targetPos = calculateHeadOffset(
                            enemy.headPosition,
                            enemy.viewYaw,
                            enemy.angleToPlayer,
                            config);
                        break;
                    case 1: // Neck
                        targetPos = calculateHeadOffset(
                            enemy.headPosition,
                            enemy.viewYaw,
                            enemy.angleToPlayer,
                            config);
                        targetPos.z -= 5.0f;
                        break;
                    case 2: // Chest
                        targetPos.x = (enemy.headPosition.x + enemy.position.x) / 2.0f;
                        targetPos.y = (enemy.headPosition.y + enemy.position.y) / 2.0f;
                        targetPos.z = (enemy.headPosition.z + enemy.position.z) / 2.0f;
                        break;
                    default:
                        targetPos = calculateHeadOffset(
                            enemy.headPosition,
                            enemy.viewYaw,
                            enemy.angleToPlayer,
                            config);
                        break;
                }

                bestAngle = calcAngle(eyePos, targetPos);
                foundTarget = true;
            }
        }
    }
    else {
        // Normal Mode: Use FOV to find closest target to crosshair
        float bestFOV = config.aimbotFOV;

        for (const auto& enemy : *enemies)
        {
            // The option is a spotted-state filter, not a ray-cast.
            if (config.aimbotVisibleOnly &&
                (!enemy.visibilityKnown || !enemy.isSpotted)) {
                continue;
            }

            // Get target position based on selected bone
            vec3 targetPos;
            switch (config.aimbotBone)
            {
                case 0: // Head
                    targetPos = calculateHeadOffset(
                        enemy.headPosition,
                        enemy.viewYaw,
                        enemy.angleToPlayer,
                        config);
                    break;
                case 1: // Neck
                    targetPos = calculateHeadOffset(
                        enemy.headPosition,
                        enemy.viewYaw,
                        enemy.angleToPlayer,
                        config);
                    targetPos.z -= 5.0f;
                    break;
                case 2: // Chest
                    targetPos.x = (enemy.headPosition.x + enemy.position.x) / 2.0f;
                    targetPos.y = (enemy.headPosition.y + enemy.position.y) / 2.0f;
                    targetPos.z = (enemy.headPosition.z + enemy.position.z) / 2.0f;
                    break;
                default:
                    targetPos = calculateHeadOffset(
                        enemy.headPosition,
                        enemy.viewYaw,
                        enemy.angleToPlayer,
                        config);
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

    if (!foundTarget) {
        resetAimState();
        return;
    }

    // Exponential time-based smoothing is stable across worker rates.
    const float alpha =
        timeBasedAlpha(lastAimUpdate, config.aimbotSmoothing);
    float deltaPitch =
        normalizeAngle(bestAngle.x - currentViewAngle.x) * alpha;
    float deltaYaw =
        normalizeAngle(bestAngle.y - currentViewAngle.y) * alpha;

    // Convert angle delta to mouse movement
    const float mouseSensitivityFactor =
        std::max(0.01f, config.mouseSensitivity) * 0.022f;

    // In CS2: Moving mouse RIGHT decreases Yaw, DOWN increases Pitch
    float moveX = -deltaYaw / mouseSensitivityFactor;
    float moveY = deltaPitch / mouseSensitivityFactor;

    // Move mouse if delta is significant
    const LONG moveWholeX =
        consumeMouseDelta(moveX, aimResidualX);
    const LONG moveWholeY =
        consumeMouseDelta(moveY, aimResidualY);
    if (moveWholeX != 0 || moveWholeY != 0)
    {
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        input.mi.dx = moveWholeX;
        input.mi.dy = moveWholeY;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void aimbot::updateTriggerbot(const menu::RuntimeConfig& config)
{
    // Check if triggerbot is enabled
    if (!config.triggerbotEnabled ||
        config.inputSuppressed ||
        !sdl_renderer::isInputAllowed()) {
        triggerbotHasTarget = false;
        resetTriggerState();
        return;
    }

    // Check if triggerbot key is held (Alt by default)
    if (!(GetAsyncKeyState(config.triggerbotKey) & 0x8000)) {
        triggerbotHasTarget = false;
        resetTriggerState();
        return;
    }

    // Use cached local player data
    if (!esp::localPlayer.isValid) {
        triggerbotHasTarget = false;
        resetTriggerState();
        return;
    }

    const vec2& currentViewAngle = esp::localPlayer.viewAngle;
    const vec3& eyePos = esp::localPlayer.eyePosition;

    // Find a spotted enemy that is very close to the crosshair.
    const float triggerbotFOV = 1.5f;  // Very small FOV - only trigger when almost on target
    bool foundTarget = false;
    const esp::EnemySnapshot enemies =
        esp::getEnemySnapshot();
    if (!enemies) {
        triggerbotHasTarget = false;
        resetTriggerState();
        return;
    }

    for (const auto& enemy : *enemies)
    {
        // Unknown state never passes the conservative spotted filter.
        if (!enemy.visibilityKnown || !enemy.isSpotted) continue;

        // Target head position with offset compensation for side-facing enemies
        vec3 targetPos = calculateHeadOffset(
            enemy.headPosition,
            enemy.viewYaw,
            enemy.angleToPlayer,
            config);

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
            if (currentTime - triggerbotTargetTime >=
                static_cast<DWORD>(config.triggerbotDelay))
            {
                const float alpha =
                    timeBasedAlpha(lastTriggerUpdate, 2.0f);
                float deltaPitch = normalizeAngle(
                    aimAngle.x - currentViewAngle.x) * alpha;
                float deltaYaw = normalizeAngle(
                    aimAngle.y - currentViewAngle.y) * alpha;

                const float mouseSensitivityFactor =
                    std::max(0.01f, config.mouseSensitivity) * 0.022f;

                float moveX = -deltaYaw / mouseSensitivityFactor;
                float moveY = deltaPitch / mouseSensitivityFactor;

                // Move mouse to aim at head
                const LONG moveWholeX =
                    consumeMouseDelta(
                        moveX,
                        triggerResidualX);
                const LONG moveWholeY =
                    consumeMouseDelta(
                        moveY,
                        triggerResidualY);
                if (moveWholeX != 0 || moveWholeY != 0)
                {
                    INPUT moveInput = {};
                    moveInput.type = INPUT_MOUSE;
                    moveInput.mi.dwFlags = MOUSEEVENTF_MOVE;
                    moveInput.mi.dx = moveWholeX;
                    moveInput.mi.dy = moveWholeY;
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
        resetTriggerState();
    }
}
