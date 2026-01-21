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
    inline bool espSnaplines = false;

    // Colors
    inline float espBoxColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    inline float espDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline float espWeaponColor[4] = { 0.0f, 1.0f, 1.0f, 1.0f };  // Cyan color for weapon
    inline float espSnaplinesColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };

    // Visual Settings
    inline int snaplinesOrigin = 0; // 0=Bottom, 1=Center, 2=Top

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

        // ESP Tab
        if (ImGui::CollapsingHeader("ESP Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Enable ESP", &espEnabled);

            if (espEnabled)
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Features:");

                // Box ESP
                ImGui::Checkbox("Box ESP", &espBox);
                if (espBox) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##BoxColor", espBoxColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                }

                // Health Bar
                ImGui::Checkbox("Health Bar", &espHealth);

                // Weapon Display
                ImGui::Checkbox("Weapon", &espWeapon);
                if (espWeapon) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##WeaponColor", espWeaponColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
                }

                // Distance
                ImGui::Checkbox("Distance", &espDistance);
                if (espDistance) {
                    ImGui::SameLine();
                    ImGui::ColorEdit4("##DistanceColor", espDistanceColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
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
            }
        }

        ImGui::Separator();

        // Info section
        if (ImGui::CollapsingHeader("Info"))
        {
            ImGui::Text("Resolution: %dx%d", WIDTH, HEIGHT);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
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
