#pragma once

#include "imgui.h"
#include "core/renderer/sdl_renderer.h"
#include <Windows.h>
#include <string>

namespace menu
{
    // Current tab index
    inline int currentTab = 0;

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
    inline int targetFPS = 30;           // Target render FPS (30-240)

    // Aimbot Settings
    inline bool aimbotEnabled = false;     // Aimbot enabled
    inline float aimbotFOV = 10.0f;        // Field of view for aimbot (degrees)
    inline float aimbotSmoothing = 5.0f;   // Smoothing factor (1.0 = instant, higher = smoother)
    inline int aimbotBone = 0;             // 0=Head, 1=Neck, 2=Chest
    inline bool aimbotVisibleOnly = true;  // Only aim at visible enemies (not behind walls)
    inline int aimbotKey = VK_SHIFT;       // Aimbot activation key (default: Shift key)
    inline bool aimbotShowFOV = true;      // Show FOV circle on screen
    inline float aimbotFOVColor[4] = { 1.0f, 1.0f, 0.0f, 0.5f };  // FOV circle color (yellow, 50% opacity)

    // Head Offset Settings (for side-facing enemies)
    inline bool headOffsetEnabled = true;      // Enable head offset compensation
    inline float headOffsetAmount = 5.0f;      // Offset amount in game units (0-15)
    inline float headOffsetAngleMin = 45.0f;   // Minimum angle for offset (degrees)
    inline float headOffsetAngleMax = 135.0f;  // Maximum angle for offset (degrees)

    // Smart Aim Settings (auto-lock visible enemies by priority)
    inline bool smartAimEnabled = false;      // Smart aim mode (ignores FOV, auto-selects best target)
    inline int smartAimPriority = 0;          // 0=Distance first, 1=Health first

    // Triggerbot Settings
    inline bool triggerbotEnabled = false; // Triggerbot enabled
    inline int triggerbotDelay = 50;       // Delay before shooting (milliseconds)
    inline int triggerbotKey = 0x46;       // Triggerbot activation key (default: F key, 0x46 = 'F')

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

    // Menu Toggle Key
    inline int menuToggleKey = VK_F4;        // Menu toggle key (default: F4)
    inline int exitKey = VK_F9;              // Exit key (default: F9)

    // Hotkey binding state
    inline bool isBindingKey = false;
    inline int* bindingKeyTarget = nullptr;
    inline const char* bindingKeyName = nullptr;

    // Convert virtual key code to key name
    inline const char* GetKeyName(int vkCode)
    {
        static char keyName[32];

        switch (vkCode)
        {
        // Special keys
        case VK_LBUTTON: return "Mouse1";
        case VK_RBUTTON: return "Mouse2";
        case VK_MBUTTON: return "Mouse3";
        case VK_XBUTTON1: return "Mouse4";
        case VK_XBUTTON2: return "Mouse5";
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_PAUSE: return "Pause";
        case VK_CAPITAL: return "CapsLock";
        case VK_ESCAPE: return "Escape";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left";
        case VK_UP: return "Up";
        case VK_RIGHT: return "Right";
        case VK_DOWN: return "Down";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_LSHIFT: return "LShift";
        case VK_RSHIFT: return "RShift";
        case VK_LCONTROL: return "LCtrl";
        case VK_RCONTROL: return "RCtrl";
        case VK_LMENU: return "LAlt";
        case VK_RMENU: return "RAlt";

        // Function keys
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";

        // Numpad
        case VK_NUMPAD0: return "Num0";
        case VK_NUMPAD1: return "Num1";
        case VK_NUMPAD2: return "Num2";
        case VK_NUMPAD3: return "Num3";
        case VK_NUMPAD4: return "Num4";
        case VK_NUMPAD5: return "Num5";
        case VK_NUMPAD6: return "Num6";
        case VK_NUMPAD7: return "Num7";
        case VK_NUMPAD8: return "Num8";
        case VK_NUMPAD9: return "Num9";
        case VK_MULTIPLY: return "Num*";
        case VK_ADD: return "Num+";
        case VK_SUBTRACT: return "Num-";
        case VK_DECIMAL: return "Num.";
        case VK_DIVIDE: return "Num/";

        // Letters A-Z (0x41 - 0x5A)
        case 0x41: return "A";
        case 0x42: return "B";
        case 0x43: return "C";
        case 0x44: return "D";
        case 0x45: return "E";
        case 0x46: return "F";
        case 0x47: return "G";
        case 0x48: return "H";
        case 0x49: return "I";
        case 0x4A: return "J";
        case 0x4B: return "K";
        case 0x4C: return "L";
        case 0x4D: return "M";
        case 0x4E: return "N";
        case 0x4F: return "O";
        case 0x50: return "P";
        case 0x51: return "Q";
        case 0x52: return "R";
        case 0x53: return "S";
        case 0x54: return "T";
        case 0x55: return "U";
        case 0x56: return "V";
        case 0x57: return "W";
        case 0x58: return "X";
        case 0x59: return "Y";
        case 0x5A: return "Z";

        // Numbers 0-9 (0x30 - 0x39)
        case 0x30: return "0";
        case 0x31: return "1";
        case 0x32: return "2";
        case 0x33: return "3";
        case 0x34: return "4";
        case 0x35: return "5";
        case 0x36: return "6";
        case 0x37: return "7";
        case 0x38: return "8";
        case 0x39: return "9";

        default:
            sprintf_s(keyName, "Key(0x%02X)", vkCode);
            return keyName;
        }
    }

    // Check for key press during binding
    inline int GetPressedKey()
    {
        // Check mouse buttons
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return VK_LBUTTON;
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) return VK_RBUTTON;
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) return VK_MBUTTON;
        if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) return VK_XBUTTON1;
        if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) return VK_XBUTTON2;

        // Check all keyboard keys
        for (int i = 0x08; i <= 0xFE; i++)
        {
            // Skip some keys that shouldn't be used
            if (i == VK_ESCAPE) continue;  // Escape cancels binding

            if (GetAsyncKeyState(i) & 0x8000)
                return i;
        }

        return 0;
    }

    // Render hotkey button
    inline void RenderHotkeyButton(const char* label, int* keyCode, const char* tooltip = nullptr)
    {
        ImGui::Text("%s:", label);
        ImGui::SameLine(150);

        char buttonLabel[64];
        if (isBindingKey && bindingKeyTarget == keyCode)
        {
            sprintf_s(buttonLabel, "[Press Key...]##%s", label);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
        }
        else
        {
            sprintf_s(buttonLabel, "%s##%s", GetKeyName(*keyCode), label);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
        }

        if (ImGui::Button(buttonLabel, ImVec2(100, 0)))
        {
            isBindingKey = true;
            bindingKeyTarget = keyCode;
            bindingKeyName = label;
        }

        ImGui::PopStyleColor();

        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
    }

    // Update key binding (call every frame)
    inline void UpdateKeyBinding()
    {
        if (!isBindingKey || bindingKeyTarget == nullptr)
            return;

        // Check for escape to cancel
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            isBindingKey = false;
            bindingKeyTarget = nullptr;
            bindingKeyName = nullptr;
            return;
        }

        int pressedKey = GetPressedKey();
        if (pressedKey != 0)
        {
            *bindingKeyTarget = pressedKey;
            isBindingKey = false;
            bindingKeyTarget = nullptr;
            bindingKeyName = nullptr;
        }
    }

    // Render Aimbot tab content
    inline void RenderAimbotTab()
    {
        ImGui::Checkbox("Enable Aimbot", &aimbotEnabled);

        if (aimbotEnabled)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Aimbot Settings:");

            // Smart Aim - auto-lock visible enemies
            ImGui::Checkbox("Smart Aim (Auto-Lock)", &smartAimEnabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ignore FOV, auto-aim at best visible target\nPriority: Visible > Distance/Health");

            if (smartAimEnabled) {
                ImGui::Indent();
                const char* priorityItems[] = { "Distance First", "Health First" };
                ImGui::Combo("Priority", &smartAimPriority, priorityItems, IM_ARRAYSIZE(priorityItems));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Distance: Aim at closest enemy\nHealth: Aim at lowest HP enemy");
                ImGui::Unindent();
            }

            if (!smartAimEnabled) {
                ImGui::SliderFloat("FOV", &aimbotFOV, 1.0f, 30.0f, "%.1f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Field of view - only aim at enemies within this angle");
            }

            ImGui::SliderFloat("Aim Smoothing", &aimbotSmoothing, 1.0f, 20.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1.0 = instant lock, higher = smoother/slower");

            const char* boneItems[] = { "Head", "Neck", "Chest" };
            ImGui::Combo("Target Bone", &aimbotBone, boneItems, IM_ARRAYSIZE(boneItems));

            if (!smartAimEnabled) {
                ImGui::Checkbox("Visible Only", &aimbotVisibleOnly);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Only aim at enemies not behind walls");
            }

            ImGui::Checkbox("Show FOV Circle", &aimbotShowFOV);
            if (aimbotShowFOV) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##FOVColor", aimbotFOVColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
            }

            // Head Offset Settings
            ImGui::Separator();
            ImGui::Checkbox("Head Offset (Side-facing)", &headOffsetEnabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Compensate for head position when enemy is facing sideways");

            if (headOffsetEnabled) {
                ImGui::Indent();
                ImGui::SliderFloat("Offset Amount", &headOffsetAmount, 0.0f, 15.0f, "%.1f units");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How much to offset the head position (5-8 recommended)");

                ImGui::SliderFloat("Min Angle", &headOffsetAngleMin, 0.0f, 90.0f, "%.0f deg");
                ImGui::SliderFloat("Max Angle", &headOffsetAngleMax, 90.0f, 180.0f, "%.0f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Angle range for offset (45-135 = side-facing)");

                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "0=facing you, 90=side, 180=back");
                ImGui::Unindent();
            }

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Hold %s to aim", GetKeyName(aimbotKey));
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

        ImGui::Separator();
        ImGui::Checkbox("Enable Triggerbot", &triggerbotEnabled);

        if (triggerbotEnabled)
        {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "Triggerbot Settings:");

            ImGui::SliderInt("Delay (ms)", &triggerbotDelay, 0, 500, "%d ms");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delay before shooting (milliseconds)");

            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Hold %s to activate", GetKeyName(triggerbotKey));
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Auto-aims at head and fires!");
        }
    }

    // Render ESP tab content
    inline void RenderESPTab()
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

    // Render Settings tab content
    inline void RenderSettingsTab()
    {
        // Hotkeys Section
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Hotkeys:");
        ImGui::Separator();

        RenderHotkeyButton("Menu Toggle", &menuToggleKey, "Key to show/hide menu");
        RenderHotkeyButton("Exit Program", &exitKey, "Key to exit the program");
        RenderHotkeyButton("Aimbot Key", &aimbotKey, "Hold to activate aimbot");
        RenderHotkeyButton("Triggerbot Key", &triggerbotKey, "Hold to activate triggerbot");

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click button and press any key to bind");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Press ESC to cancel binding");

        ImGui::Spacing();
        ImGui::Separator();

        // Performance Section
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Performance:");
        ImGui::SliderInt("Target FPS", &targetFPS, 30, 240);
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Lower = less CPU, Higher = smoother)");

        ImGui::Spacing();
        ImGui::Separator();

        // Info Section
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "System Info:");
        ImGui::Text("Resolution: %dx%d", WIDTH, HEIGHT);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::Text("Frame Time: %.3f ms", 1000.0f / ImGui::GetIO().Framerate);
        ImGui::Text("Target FPS: %d", targetFPS);

        ImGui::Spacing();
        ImGui::Separator();

        // About Section
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "CS2 External ESP");
        ImGui::Text("SDL2 + ImGui Overlay");
        ImGui::Text("Build: v2.0");
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "GitHub:");
        ImGui::TextWrapped("github.com/tiansongyu/cs2_cheat");
    }

    // Main render function
    inline void render()
    {
        if (!sdl_renderer::menuVisible) return;

        // Update key binding
        UpdateKeyBinding();

        // Set window transparency
        ImGui::SetNextWindowBgAlpha(0.85f);
        ImGui::SetNextWindowSize(ImVec2(450, 500), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);

        ImGui::Begin("CS2 ESP Menu", nullptr, ImGuiWindowFlags_NoCollapse);

        // Header with controls info
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Controls:");
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s - Hide menu", GetKeyName(menuToggleKey));
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "| %s - Exit", GetKeyName(exitKey));

        ImGui::Separator();

        // Tab Bar
        if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_None))
        {
            if (ImGui::BeginTabItem("Aimbot"))
            {
                ImGui::Spacing();
                RenderAimbotTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("ESP"))
            {
                ImGui::Spacing();
                RenderESPTab();
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Settings"))
            {
                ImGui::Spacing();
                RenderSettingsTab();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();
    }
}
