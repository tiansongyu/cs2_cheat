#include "esp.hpp"
#include "core/renderer/sdl_renderer.h"
#include "menu.hpp"
#include "utils/weapon_names.hpp"
#include "imgui.h"
#include <iostream>
#include <cmath>
#include <algorithm>  // For std::min

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

    // Get local player view angle for radar rotation
    vec3 localEyeAngles = memory::Read<vec3>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
    player_yaw = localEyeAngles.y;  // Yaw is the Y component

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

        // Read weapon information
        std::string weaponName = "Unknown";
        uintptr_t weaponServices = memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_pWeaponServices);
        if (weaponServices) {
            uint32_t activeWeaponHandle = memory::Read<uint32_t>(weaponServices + cs2_dumper::schemas::client_dll::CPlayer_WeaponServices::m_hActiveWeapon);
            if (activeWeaponHandle && activeWeaponHandle != 0xFFFFFFFF) {
                uint32_t weaponIndex = activeWeaponHandle & 0x7FFF;
                uint32_t weaponChunk = weaponIndex >> 9;
                uint32_t weaponInChunk = weaponIndex & 0x1FF;

                uintptr_t weaponListEntry = memory::Read<uintptr_t>(entity_list + 0x10 + 0x8 * weaponChunk);
                if (weaponListEntry) {
                    uintptr_t weaponEntity = memory::Read<uintptr_t>(weaponListEntry + 0x70 * weaponInChunk);
                    if (weaponEntity) {
                        // Read AttributeManager -> Item -> ItemDefinitionIndex
                        uintptr_t attributeManager = weaponEntity + cs2_dumper::schemas::client_dll::C_EconEntity::m_AttributeManager;
                        uint16_t itemDefIndex = memory::Read<uint16_t>(attributeManager + cs2_dumper::schemas::client_dll::C_AttributeContainer::m_Item + cs2_dumper::schemas::client_dll::C_EconItemView::m_iItemDefinitionIndex);
                        weaponName = weapon_names::getWeaponName(itemDefIndex);
                    }
                }
            }
        }

        // Read enemy view angles
        vec3 eyeAngles = memory::Read<vec3>(entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles);
        float enemyYaw = eyeAngles.y;  // Yaw is the Y component of QAngle

        // Calculate angle to player
        float angleToPlayer = calculateAngleToPlayer(enemyYaw, feetPos, player_position);

        // Read flashbang status with alpha threshold
        float flashDuration = memory::Read<float>(entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration);
        float flashMaxAlpha = memory::Read<float>(entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashMaxAlpha);

        // Only consider flashed if alpha is above threshold (e.g., 0.5 = 50% flash intensity)
        const float FLASH_ALPHA_THRESHOLD = 0.5f;
        bool isFlashed = (flashDuration > 0.0f) && (flashMaxAlpha >= FLASH_ALPHA_THRESHOLD);

        // Calculate distance first (needed for hybrid detection)
        float distance = static_cast<float>(player_distance(player_position, feetPos));

        // Hybrid visibility detection system
        // m_bSpotted is reliable only within a certain distance (default ~2000 units)
        // Beyond that distance, we use fallback logic
        uintptr_t entitySpottedState = entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_entitySpottedState;
        bool rawSpotted = memory::Read<bool>(entitySpottedState + 0x8);  // m_bSpotted

        bool isSpotted;
        if (distance < menu::espWallCheckDistance) {
            // Close range: Use m_bSpotted (accurate)
            isSpotted = rawSpotted;
        } else {
            // Long range: m_bSpotted is unreliable
            // Fallback: Assume visible (red) since wall occlusion matters less at long range
            isSpotted = true;  // Default to "visible" at long range
        }

        EnemyInfo enemy;
        enemy.position = feetPos;
        enemy.headPosition = headPos;
        enemy.health = health;
        enemy.distance = distance;
        enemy.weaponName = weaponName;
        enemy.viewYaw = enemyYaw;
        enemy.angleToPlayer = angleToPlayer;
        enemy.flashDuration = flashDuration;
        enemy.isFlashed = isFlashed;
        enemy.isSpotted = isSpotted;

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

        // Determine box color based on ANGLE (threat level)
        // Red = enemy facing you (danger), Green = enemy facing away (safe)
        uint8_t r, g, b, a;

        if (menu::espViewAngle) {
            // Use angle-based color for box
            if (enemy.angleToPlayer < 45.0f) {
                // Facing player (RED - DANGER!)
                r = 255; g = 0; b = 0; a = 255;
            } else if (enemy.angleToPlayer < 90.0f) {
                // Partially facing (ORANGE)
                r = 255; g = 165; b = 0; a = 255;
            } else if (enemy.angleToPlayer < 135.0f) {
                // Side view (YELLOW)
                r = 255; g = 255; b = 0; a = 255;
            } else {
                // Back to player (GREEN - SAFE)
                r = 0; g = 255; b = 0; a = 255;
            }
        } else {
            // View angle disabled, use default box color
            r = static_cast<uint8_t>(menu::espBoxColor[0] * 255);
            g = static_cast<uint8_t>(menu::espBoxColor[1] * 255);
            b = static_cast<uint8_t>(menu::espBoxColor[2] * 255);
            a = static_cast<uint8_t>(menu::espBoxColor[3] * 255);
        }

        // Draw box (color indicates threat level based on angle)
        if (menu::espBox) {
            sdl_renderer::draw::box(x, y, w, h, r, g, b, a);
        }

        // Draw ellipse "eye" indicator below weapon name
        if (menu::espFlashIndicator) {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();

            // Calculate ellipse position (below weapon name, above the box)
            float ellipseCenterX = static_cast<float>(x + w / 2);

            // If weapon is shown, place ellipse below it; otherwise place above box
            float ellipseCenterY;
            if (menu::espWeapon) {
                // Weapon is at y-18, text height is ~13px, so ellipse at y-18+13+3 = y-2
                ellipseCenterY = static_cast<float>(y - 2);
            } else {
                // No weapon, place ellipse above box
                ellipseCenterY = static_cast<float>(y - 10);
            }

            float ellipseRadiusX = w * 0.25f;  // 25% of box width
            float ellipseRadiusY = 6.0f;       // Fixed height

            // Determine eye color: Red (normal) or Yellow (flashed)
            ImU32 eyeColor;
            if (enemy.isFlashed) {
                // Yellow - enemy is flashed
                uint8_t fr = static_cast<uint8_t>(menu::espFlashColor[0] * 255);
                uint8_t fg = static_cast<uint8_t>(menu::espFlashColor[1] * 255);
                uint8_t fb = static_cast<uint8_t>(menu::espFlashColor[2] * 255);
                uint8_t fa = static_cast<uint8_t>(menu::espFlashColor[3] * 255);
                eyeColor = IM_COL32(fr, fg, fb, fa);
            } else {
                // Red - normal state
                uint8_t nr = static_cast<uint8_t>(menu::espFlashNormalColor[0] * 255);
                uint8_t ng = static_cast<uint8_t>(menu::espFlashNormalColor[1] * 255);
                uint8_t nb = static_cast<uint8_t>(menu::espFlashNormalColor[2] * 255);
                uint8_t na = static_cast<uint8_t>(menu::espFlashNormalColor[3] * 255);
                eyeColor = IM_COL32(nr, ng, nb, na);
            }

            // Draw filled ellipse
            const int segments = 32;
            drawList->AddEllipseFilled(
                ImVec2(ellipseCenterX, ellipseCenterY),  // center
                ImVec2(ellipseRadiusX, ellipseRadiusY),  // radius (x, y)
                eyeColor,                                 // color
                0.0f,                                     // rotation
                segments                                  // num_segments
            );
        }

        // Draw health bar (vertical bar on left side)
        if (menu::espHealth) {
            int healthBarWidth = 4;
            int healthBarHeight = h;
            int healthBarX = x - 8;
            int healthBarY = y;

            // Background bar
            sdl_renderer::draw::filledBox(healthBarX, healthBarY, healthBarWidth, healthBarHeight, 50, 50, 50, 200);

            // Health bar (fills from bottom to top)
            int healthHeight = static_cast<int>((enemy.health / 100.0f) * healthBarHeight);
            int healthY = healthBarY + (healthBarHeight - healthHeight);
            uint8_t healthR = static_cast<uint8_t>(255 * (1.0f - enemy.health / 100.0f));
            uint8_t healthG = static_cast<uint8_t>(255 * (enemy.health / 100.0f));
            sdl_renderer::draw::filledBox(healthBarX, healthY, healthBarWidth, healthHeight, healthR, healthG, 0, 255);
        }

        // Draw weapon name above the box
        if (menu::espWeapon) {
            uint8_t wr = static_cast<uint8_t>(menu::espWeaponColor[0] * 255);
            uint8_t wg = static_cast<uint8_t>(menu::espWeaponColor[1] * 255);
            uint8_t wb = static_cast<uint8_t>(menu::espWeaponColor[2] * 255);
            uint8_t wa = static_cast<uint8_t>(menu::espWeaponColor[3] * 255);

            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            ImVec2 textSize = ImGui::CalcTextSize(enemy.weaponName.c_str());
            drawList->AddText(
                ImVec2(static_cast<float>(x + w / 2 - textSize.x / 2), static_cast<float>(y - 18)),
                IM_COL32(wr, wg, wb, wa),
                enemy.weaponName.c_str()
            );
        }

        // Draw wall occlusion indicator (triangle arrow)
        // Triangle color indicates visibility: RED = visible, GREEN = behind wall
        if (menu::espWallCheck) {
            ImDrawList* drawList = ImGui::GetBackgroundDrawList();

            // Determine triangle color based on WALL OCCLUSION
            uint8_t vr, vg, vb, va;
            if (enemy.isSpotted) {
                // Enemy is visible (no wall) - RED
                vr = static_cast<uint8_t>(menu::espBoxColor[0] * 255);
                vg = static_cast<uint8_t>(menu::espBoxColor[1] * 255);
                vb = static_cast<uint8_t>(menu::espBoxColor[2] * 255);
                va = static_cast<uint8_t>(menu::espBoxColor[3] * 255);
            } else {
                // Enemy is behind wall - GREEN
                vr = static_cast<uint8_t>(menu::espWallColor[0] * 255);
                vg = static_cast<uint8_t>(menu::espWallColor[1] * 255);
                vb = static_cast<uint8_t>(menu::espWallColor[2] * 255);
                va = static_cast<uint8_t>(menu::espWallColor[3] * 255);
            }

            // Draw arrow indicator at top of box
            float centerX = static_cast<float>(x + w / 2);
            float topY = static_cast<float>(y - 35);
            float arrowSize = 8.0f;

            // Draw filled triangle pointing in enemy's view direction relative to player
            // Triangle direction still shows where enemy is facing
            // But COLOR now shows wall occlusion status
            if (enemy.angleToPlayer < 90.0f) {
                // Enemy is facing towards player - arrow points down
                ImVec2 p1(centerX, topY + arrowSize);           // Bottom point
                ImVec2 p2(centerX - arrowSize, topY);           // Top left
                ImVec2 p3(centerX + arrowSize, topY);           // Top right
                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(vr, vg, vb, va));
            } else {
                // Enemy is facing away from player - arrow points up
                ImVec2 p1(centerX, topY);                       // Top point
                ImVec2 p2(centerX - arrowSize, topY + arrowSize); // Bottom left
                ImVec2 p3(centerX + arrowSize, topY + arrowSize); // Bottom right
                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(vr, vg, vb, va));
            }

            // Draw angle text (still shows angle degree)
            if (menu::espViewAngleText) {
                char angleText[16];
                snprintf(angleText, sizeof(angleText), "%.0f°", enemy.angleToPlayer);
                ImVec2 angleTextSize = ImGui::CalcTextSize(angleText);
                drawList->AddText(
                    ImVec2(centerX - angleTextSize.x / 2, topY + arrowSize + 2),
                    IM_COL32(vr, vg, vb, va),
                    angleText
                );
            }
        }

        // Flashbang text indicator removed - now using ellipse eye indicator above

        // Draw distance text using ImGui
        if (menu::espDistance) {
            char distText[32];
            snprintf(distText, sizeof(distText), "%.0fm", enemy.distance / 100.0f);

            uint8_t dr = static_cast<uint8_t>(menu::espDistanceColor[0] * 255);
            uint8_t dg = static_cast<uint8_t>(menu::espDistanceColor[1] * 255);
            uint8_t db = static_cast<uint8_t>(menu::espDistanceColor[2] * 255);
            uint8_t da = static_cast<uint8_t>(menu::espDistanceColor[3] * 255);

            ImDrawList* drawList = ImGui::GetBackgroundDrawList();
            drawList->AddText(
                ImVec2(static_cast<float>(x + w / 2 - 10), static_cast<float>(y + h + 2)),
                IM_COL32(dr, dg, db, da),
                distText
            );
        }

        // Draw snaplines
        if (menu::espSnaplines) {
            int startX, startY;
            switch (menu::snaplinesOrigin) {
                case 0: // Bottom
                    startX = WINDOW_W / 2;
                    startY = WINDOW_H;
                    break;
                case 1: // Center
                    startX = WINDOW_W / 2;
                    startY = WINDOW_H / 2;
                    break;
                case 2: // Top
                    startX = WINDOW_W / 2;
                    startY = 0;
                    break;
                default:
                    startX = WINDOW_W / 2;
                    startY = WINDOW_H;
            }

            uint8_t sr = static_cast<uint8_t>(menu::espSnaplinesColor[0] * 255);
            uint8_t sg = static_cast<uint8_t>(menu::espSnaplinesColor[1] * 255);
            uint8_t sb = static_cast<uint8_t>(menu::espSnaplinesColor[2] * 255);
            uint8_t sa = static_cast<uint8_t>(menu::espSnaplinesColor[3] * 255);

            sdl_renderer::draw::line(
                startX, startY,
                static_cast<int>(screenFeet.x), static_cast<int>(screenFeet.y),
                sr, sg, sb, sa
            );
        }
    }

    // ==================== RADAR OVERLAY ====================
    if (menu::radarEnabled) {
        ImDrawList* drawList = ImGui::GetBackgroundDrawList();

        // Calculate radar center position based on screen size
        float radarCenterXPx = WINDOW_W * menu::radarCenterX;
        float radarCenterYPx = WINDOW_H * menu::radarCenterY;
        float radarRadiusPx = WINDOW_H * menu::radarRadius;

        // Draw radar background (semi-transparent circle)
        uint8_t bgR = static_cast<uint8_t>(menu::radarBgColor[0] * 255);
        uint8_t bgG = static_cast<uint8_t>(menu::radarBgColor[1] * 255);
        uint8_t bgB = static_cast<uint8_t>(menu::radarBgColor[2] * 255);
        uint8_t bgA = static_cast<uint8_t>(menu::radarBgColor[3] * 255);
        drawList->AddCircleFilled(
            ImVec2(radarCenterXPx, radarCenterYPx),
            radarRadiusPx,
            IM_COL32(bgR, bgG, bgB, bgA), 64
        );
        // Draw radar border
        drawList->AddCircle(
            ImVec2(radarCenterXPx, radarCenterYPx),
            radarRadiusPx,
            IM_COL32(100, 100, 100, 200), 64, 2.0f
        );

        // Draw player marker at center (you)
        if (menu::radarShowCenter) {
            uint8_t cr = static_cast<uint8_t>(menu::radarCenterColor[0] * 255);
            uint8_t cg = static_cast<uint8_t>(menu::radarCenterColor[1] * 255);
            uint8_t cb = static_cast<uint8_t>(menu::radarCenterColor[2] * 255);
            uint8_t ca = static_cast<uint8_t>(menu::radarCenterColor[3] * 255);

            // Draw player dot at center (1.5x size: 5 * 1.5 = 7.5)
            float playerDotRadius = 7.5f;
            drawList->AddCircleFilled(
                ImVec2(radarCenterXPx, radarCenterYPx),
                playerDotRadius,
                IM_COL32(cr, cg, cb, ca)
            );

            // Draw player direction arrow (pointing up = forward)
            float arrowLen = 14.0f;
            float arrowOffset = playerDotRadius + 2.0f;  // Start from circle edge
            drawList->AddTriangleFilled(
                ImVec2(radarCenterXPx, radarCenterYPx - arrowOffset - arrowLen),  // Top point
                ImVec2(radarCenterXPx - 6.0f, radarCenterYPx - arrowOffset),      // Bottom left
                ImVec2(radarCenterXPx + 6.0f, radarCenterYPx - arrowOffset),      // Bottom right
                IM_COL32(cr, cg, cb, ca)
            );
        }

        // Draw enemy dots on radar
        uint8_t er = static_cast<uint8_t>(menu::radarEnemyColor[0] * 255);
        uint8_t eg = static_cast<uint8_t>(menu::radarEnemyColor[1] * 255);
        uint8_t eb = static_cast<uint8_t>(menu::radarEnemyColor[2] * 255);
        uint8_t ea = static_cast<uint8_t>(menu::radarEnemyColor[3] * 255);

        // Convert player yaw to radians for rotation
        // CS2 yaw: 0 = East, 90 = North, 180/-180 = West, -90 = South
        // We need to rotate so that "up" on radar = player's forward direction
        float rotationRad = (90.0f - player_yaw) * (3.14159265f / 180.0f);
        float cosRot = std::cos(rotationRad);
        float sinRot = std::sin(rotationRad);

        for (const auto& enemy : enemies) {
            // Calculate relative position from player to enemy (world coords)
            float deltaX = enemy.position.x - player_position.x;
            float deltaY = enemy.position.y - player_position.y;

            // Rotate based on player's view direction
            // After rotation: +Y = forward (up on radar), +X = right
            float rotatedX = deltaX * cosRot - deltaY * sinRot;
            float rotatedY = deltaX * sinRot + deltaY * cosRot;

            // Scale the position to fit radar
            float scaleFactor = radarRadiusPx / (2000.0f * menu::radarScale);

            // Map to screen: +X = right, -rotatedY = up (since screen Y increases downward)
            float radarX = radarCenterXPx + rotatedX * scaleFactor;
            float radarY = radarCenterYPx - rotatedY * scaleFactor;

            // Calculate distance from radar center
            float distFromCenter = std::sqrt(
                (radarX - radarCenterXPx) * (radarX - radarCenterXPx) +
                (radarY - radarCenterYPx) * (radarY - radarCenterYPx)
            );

            // Only draw if within radar radius
            if (distFromCenter <= radarRadiusPx) {
                // Draw enemy dot (red by default) - 1.5x size (6 * 1.5 = 9)
                float dotRadius = 9.0f;
                drawList->AddCircleFilled(
                    ImVec2(radarX, radarY),
                    dotRadius,
                    IM_COL32(er, eg, eb, ea)
                );

                // Get arrow color (white by default, separate from dot)
                uint8_t ar = static_cast<uint8_t>(menu::radarEnemyArrowColor[0] * 255);
                uint8_t ag = static_cast<uint8_t>(menu::radarEnemyArrowColor[1] * 255);
                uint8_t ab = static_cast<uint8_t>(menu::radarEnemyArrowColor[2] * 255);
                uint8_t aa = static_cast<uint8_t>(menu::radarEnemyArrowColor[3] * 255);

                // Draw enemy direction arrow
                // Calculate enemy's view direction relative to player's view
                float enemyDirRad = (enemy.viewYaw - player_yaw) * (3.14159265f / 180.0f);

                // Arrow starts from edge of circle, not center (to avoid overlap)
                float arrowLen = 12.0f;
                float arrowWidth = 6.0f;
                float arrowOffset = dotRadius + 2.0f;  // Start from circle edge + small gap

                // Calculate arrow base center (at edge of circle)
                float baseCenterX = radarX + arrowOffset * std::sin(enemyDirRad);
                float baseCenterY = radarY - arrowOffset * std::cos(enemyDirRad);

                // Calculate arrow tip position (extends from base)
                float tipX = baseCenterX + arrowLen * std::sin(enemyDirRad);
                float tipY = baseCenterY - arrowLen * std::cos(enemyDirRad);

                // Calculate arrow base corners (perpendicular to direction)
                float baseX1 = baseCenterX + arrowWidth * std::cos(enemyDirRad);
                float baseY1 = baseCenterY + arrowWidth * std::sin(enemyDirRad);
                float baseX2 = baseCenterX - arrowWidth * std::cos(enemyDirRad);
                float baseY2 = baseCenterY - arrowWidth * std::sin(enemyDirRad);

                // Draw filled triangle arrow (white by default)
                drawList->AddTriangleFilled(
                    ImVec2(tipX, tipY),
                    ImVec2(baseX1, baseY1),
                    ImVec2(baseX2, baseY2),
                    IM_COL32(ar, ag, ab, aa)
                );
            }
        }
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

// Normalize angle to -180 to 180 range
float esp::normalizeAngle(float angle)
{
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

// Calculate yaw angle from position A to position B
float esp::calculateYawToTarget(const vec3& from, const vec3& to)
{
    float deltaX = to.x - from.x;
    float deltaY = to.y - from.y;
    float yaw = std::atan2(deltaY, deltaX) * (180.0f / 3.14159265f);
    return normalizeAngle(yaw);
}

// Calculate angle difference between enemy view and player direction
float esp::calculateAngleToPlayer(float enemyYaw, const vec3& enemyPos, const vec3& playerPos)
{
    float yawToPlayer = calculateYawToTarget(enemyPos, playerPos);
    float angleDiff = std::abs(normalizeAngle(enemyYaw - yawToPlayer));
    return angleDiff;
}