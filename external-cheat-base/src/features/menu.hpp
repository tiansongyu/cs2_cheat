#pragma once

#include "imgui.h"
#include "core/renderer/sdl_renderer.h"

namespace menu
{
    // ESP Settings
    inline bool espEnabled = true;
    inline bool espBox = true;
    inline bool espHealth = true;
    inline bool espDistance = true;  // Default ON
    inline bool espWeapon = true;    // Weapon display - Default ON
    inline bool espViewAngle = true; // View angle indicator - Default ON
    inline bool espViewAngleText = false; // Show angle degree text
    inline bool espFlashIndicator = false; // Flashbang eye indicator - Default OFF
    inline bool espWallCheck = true; // Wall occlusion check - Default ON
    inline float espWallCheckDistance = 2000.0f; // Max distance for reliable wall check (game units)
    inline bool espSnaplines = false;

    // Colors
    inline float espBoxColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };          // Red - visible enemies (in direct line of sight)
    inline float espWallColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };         // Green - enemies behind wall
    inline float espDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline float espWeaponColor[4] = { 0.0f, 1.0f, 1.0f, 1.0f };       // Cyan color for weapon
    inline float espFlashNormalColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };  // Red - normal eye state
    inline float espFlashColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };        // Yellow - flashed eye state
    inline float espSnaplinesColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };

    // Visual Settings
    inline int snaplinesOrigin = 0; // 0=Bottom, 1=Center, 2=Top

    // System Settings
    inline int targetFPS = 60;           // Target render FPS (30-240)

    // Aimbot Settings
    inline bool aimbotEnabled = false;     // Aimbot enabled
    inline float aimbotFOV = 10.0f;        // Field of view for aimbot (degrees)
    inline float aimbotSmoothing = 5.0f;   // Smoothing factor (1.0 = instant, higher = smoother)
    inline int aimbotBone = 0;             // 0=Head, 1=Neck, 2=Chest
    inline bool aimbotVisibleOnly = true;  // Only aim at visible enemies (not behind walls)
    inline int aimbotKey = VK_SHIFT;       // Aimbot activation key (default: Shift key)
    inline bool aimbotShowFOV = true;      // Show FOV circle on screen
    inline float aimbotFOVColor[4] = { 1.0f, 1.0f, 0.0f, 0.5f };  // FOV circle color (yellow, 50% opacity)

    // RCS Settings
    inline bool rcsEnabled = false;        // RCS (Recoil Control System) enabled
    inline float rcsStrength = 100.0f;     // RCS strength (0-100%)
    inline float rcsSensitivity = 1.0f;    // In-game mouse sensitivity (for accurate compensation)
    inline float rcsSmoothing = 1.0f;      // Smoothing factor (1.0 = instant, higher = smoother)

    // Radar Settings
    inline bool radarEnabled = true;         // Enable radar overlay
    inline bool radarShowCenter = true;      // Show radar center marker (for debugging)
    inline float radarCenterX = 0.227f;      // Radar center X - right side of game radar
    inline float radarCenterY = 0.142f;      // Radar center Y as percentage of screen height (top area)
    inline float radarRadius = 0.117f;       // Radar radius as percentage of screen height
    inline float radarScale = 1.0f;          // Scale for enemy positions (adjust based on map size)
    inline float radarBgColor[4] = { 1.0f, 1.0f, 1.0f, 0.6f };           // #FFFFFF99 - white with 60% opacity
    inline float radarEnemyColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };        // Red - enemy dots on radar
    inline float radarEnemyArrowColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };   // White - enemy direction arrow
    inline float radarCenterColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };       // Green - radar center marker (you)

    inline void render()
    {
        if (!sdl_renderer::menuVisible) return;

        // Set window transparency
        ImGui::SetNextWindowBgAlpha(0.45f);
        ImGui::SetNextWindowSize(ImVec2(350, 400), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

        ImGui::Begin("CS2 ESP Menu", nullptr, ImGuiWindowFlags_NoCollapse);

        // Big red warning text
        ImGui::PushFont(nullptr);
        ImGui::SetWindowFontScale(1.8f);
        ImGui::TextColored(ImVec4(1.0f, 0.2f, 0.2f, 1.0f), ">> Press F4 to START! <<");
        ImGui::SetWindowFontScale(1.0f);
        ImGui::PopFont();

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Controls:");
        ImGui::BulletText("F4 - Hide menu & Play");
        ImGui::BulletText("F9 - Exit program");
        ImGui::Separator();

        // Aimbot Tab
        if (ImGui::CollapsingHeader("Aimbot", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Enable Aimbot", &aimbotEnabled);

            if (aimbotEnabled)
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Aimbot Settings:");

                ImGui::SliderFloat("FOV", &aimbotFOV, 1.0f, 30.0f, "%.1f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Field of view - only aim at enemies within this angle");

                ImGui::SliderFloat("Aim Smoothing", &aimbotSmoothing, 1.0f, 20.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("1.0 = instant lock, higher = smoother/slower");

                const char* boneItems[] = { "Head", "Neck", "Chest" };
                ImGui::Combo("Target Bone", &aimbotBone, boneItems, IM_ARRAYSIZE(boneItems));

                ImGui::Checkbox("Visible Only", &aimbotVisibleOnly);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Only aim at enemies not behind walls");

                ImGui::Checkbox("Show FOV Circle", &aimbotShowFOV);
                if (aimbotShowFOV) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##FOVColor", aimbotFOVColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                }

                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Hold Shift to aim");
            }

            ImGui::Separator();
            ImGui::Checkbox("Enable RCS (Recoil Control)", &rcsEnabled);

            if (rcsEnabled)
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "RCS Settings:");

                ImGui::SliderFloat("Strength", &rcsStrength, 0.0f, 100.0f, "%.0f%%");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How much recoil to compensate (100%% = full)");

                ImGui::SliderFloat("Sensitivity", &rcsSensitivity, 0.1f, 10.0f, "%.2f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Match your in-game mouse sensitivity");

                ImGui::SliderFloat("RCS Smoothing", &rcsSmoothing, 1.0f, 5.0f, "%.1f");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Higher = smoother but slower compensation");

                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Tip: Set sensitivity to match");
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "your CS2 mouse sensitivity.");
            }
        }

        ImGui::Separator();

        // ESP Tab
        if (ImGui::CollapsingHeader("ESP Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Enable ESP", &espEnabled);

            if (espEnabled)
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Features:");

                // Box ESP (color = angle/threat level)
                ImGui::Checkbox("Box ESP", &espBox);
                if (espBox) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##BoxColor", espBoxColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Default color when View Direction disabled)");
                }

                // Health Bar
                ImGui::Checkbox("Health Bar", &espHealth);

                // Weapon Display
                ImGui::Checkbox("Weapon", &espWeapon);
                if (espWeapon) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##WeaponColor", espWeaponColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                }

                // View Direction (controls BOX color based on angle)
                ImGui::Checkbox("View Direction (Box Color)", &espViewAngle);
                if (espViewAngle) {
                    ImGui::Indent();
                    ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Facing You: RED");
                    ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Partial: ORANGE");
                    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Side: YELLOW");
                    ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Back: GREEN");
                    ImGui::Checkbox("Show Angle Degrees", &espViewAngleText);
                    ImGui::Unindent();
                }

                // Wall Occlusion Check (controls TRIANGLE color)
                ImGui::Checkbox("Wall Check (Triangle)", &espWallCheck);
                if (espWallCheck) {
                    ImGui::Indent();
                    ImGui::Text("Visible Color:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##BoxColor2", espBoxColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    ImGui::Text("Behind Wall Color:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##WallColor", espWallColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Triangle: RED=Visible, GREEN=Behind Wall");

                    // Distance threshold slider
                    ImGui::Text("Max Detection Distance:");
                    ImGui::SliderFloat("##WallCheckDistance", &espWallCheckDistance, 500.0f, 5000.0f, "%.0f units");
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Beyond this: assume visible)");
                    ImGui::Unindent();
                }

                // Distance
                ImGui::Checkbox("Distance", &espDistance);
                if (espDistance) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##DistanceColor", espDistanceColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                }

                // Flashbang Eye Indicator (moved below Distance)
                ImGui::Checkbox("Flashbang Eye Indicator", &espFlashIndicator);
                if (espFlashIndicator) {
                    ImGui::Indent();
                    ImGui::Text("Normal Eye Color:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##FlashNormalColor", espFlashNormalColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    ImGui::Text("Flashed Eye Color:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##FlashColor", espFlashColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    ImGui::Unindent();
                }

                // Snaplines
                ImGui::Checkbox("Snaplines", &espSnaplines);
                if (espSnaplines) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##SnaplinesColor", espSnaplinesColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                    ImGui::Indent();
                    const char* origins[] = { "Bottom", "Center", "Top" };
                    ImGui::Combo("Origin", &snaplinesOrigin, origins, IM_ARRAYSIZE(origins));
                    ImGui::Unindent();
                }

                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Radar:");

                // Radar
                ImGui::Checkbox("Radar Overlay", &radarEnabled);
                if (radarEnabled) {
                    ImGui::Indent();

                    // Debug: Show center marker
                    ImGui::Checkbox("Show Center Marker", &radarShowCenter);
                    if (radarShowCenter) {
                        ImGui::SameLine();
                        ImGui::ColorEdit4("##RadarCenterColor", radarCenterColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Green dot = you, arrow = your direction)");
                    }

                    // Colors
                    ImGui::Text("Background:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##RadarBgColor", radarBgColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                    ImGui::Text("Enemy Dot:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##RadarEnemyColor", radarEnemyColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                    ImGui::Text("Enemy Arrow:");
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##RadarEnemyArrowColor", radarEnemyArrowColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);

                    // Position adjustments
                    ImGui::Separator();
                    ImGui::Text("Position & Size:");
                    ImGui::SliderFloat("Center X", &radarCenterX, 0.1f, 0.9f, "%.3f");
                    ImGui::SliderFloat("Center Y", &radarCenterY, 0.1f, 0.9f, "%.3f");
                    ImGui::SliderFloat("Radius", &radarRadius, 0.05f, 0.25f, "%.3f");
                    ImGui::SliderFloat("Scale", &radarScale, 0.1f, 5.0f, "%.1f");
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Scale: smaller = see farther)");

                    ImGui::Unindent();
                }
            }
        }

        ImGui::Separator();

        // Settings section
        if (ImGui::CollapsingHeader("Settings"))
        {
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Performance:");
            ImGui::SliderInt("Target FPS", &targetFPS, 30, 240);
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Lower = less CPU, Higher = smoother)");
        }

        // Info section
        if (ImGui::CollapsingHeader("Info"))
        {
            ImGui::Text("Resolution: %dx%d", WIDTH, HEIGHT);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
            ImGui::Text("Target FPS: %d", targetFPS);
        }

        // About section
        if (ImGui::CollapsingHeader("About"))
        {
            ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "CS2 External ESP");
            ImGui::Text("SDL2 + ImGui Overlay");
            ImGui::Text("Build: v2.0");
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "GitHub:");
            ImGui::TextWrapped("github.com/tiansongyu/cs2_cheat");
        }

        ImGui::End();
    }
}
