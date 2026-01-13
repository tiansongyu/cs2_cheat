#pragma once

#include "imgui.h"
#include "renderer/sdl_renderer.h"

namespace menu
{
    inline bool espEnabled = true;
    inline bool espBox = true;
    inline bool espHealth = true;
    inline bool espDistance = true;
    inline float espBoxColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };
    
    inline void render()
    {
        if (!sdl_renderer::menuVisible) return;
        
        ImGui::SetNextWindowSize(ImVec2(350, 300), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
        
        ImGui::Begin("CS2 ESP Menu", nullptr, ImGuiWindowFlags_NoCollapse);
        
        ImGui::Text("Press INSERT to toggle menu");
        ImGui::Text("Press F9 to exit");
        ImGui::Separator();
        
        if (ImGui::CollapsingHeader("ESP Settings", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::Checkbox("Enable ESP", &espEnabled);
            
            if (espEnabled)
            {
                ImGui::Indent();
                ImGui::Checkbox("Box ESP", &espBox);
                ImGui::Checkbox("Health Bar", &espHealth);
                ImGui::Checkbox("Distance", &espDistance);
                ImGui::ColorEdit4("Box Color", espBoxColor, ImGuiColorEditFlags_NoInputs);
                ImGui::Unindent();
            }
        }
        
        ImGui::Separator();
        
        if (ImGui::CollapsingHeader("Info"))
        {
            ImGui::Text("Resolution: %dx%d", WIDTH, HEIGHT);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        }
        
        ImGui::End();
    }
}

