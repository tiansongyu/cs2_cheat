// Headless SDL software rendering: exercises the real Skins page without
// attaching to any process or creating a native window.
#include "features/menu.hpp"
#include "imgui_impl_sdlrenderer2.h"

#include <cassert>
#include <filesystem>
#include <string>

std::uint32_t WIDTH = 1920;
std::uint32_t HEIGHT = 1080;
std::uint32_t WINDOW_W = 1920;
std::uint32_t WINDOW_H = 1080;
std::int32_t VIEWPORT_X = 0;
std::int32_t VIEWPORT_Y = 0;
std::uint32_t VIEWPORT_W = 1920;
std::uint32_t VIEWPORT_H = 1080;

namespace
{
    float testDpi = 1;
    bool testWritesAllowed = true;
}

float sdl_renderer::getDpiScale() { return testDpi; }
bool memory::WritesAllowed() { return testWritesAllowed; }

int main(int argc, char** argv)
{
    assert(argc == 2);
    const std::filesystem::path output(argv[1]);
    for (int scenario = 0; scenario < 6; ++scenario) {
        const int width = scenario == 1 ? 430 : scenario == 2 ? 1020 : 660;
        const int height = scenario == 2 ? 990 : 660;
        testDpi = scenario == 2 ? 1.5f : 1.0f;
        testWritesAllowed = scenario != 3;
        auto surface = SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
        assert(surface);
        auto renderer = SDL_CreateSoftwareRenderer(surface);
        assert(renderer);
        ImGui::CreateContext();
        auto& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(static_cast<float>(width), static_cast<float>(height));
        io.DeltaTime = 1.0f / 60.0f;
        ImFontConfig font;
        font.SizePixels = 18.0f * testDpi;
        io.Fonts->AddFontDefaultVector(&font);
        ImGui::StyleColorsDark();
        ImGui::GetStyle().ScaleAllSizes(testDpi);
        assert(ImGui_ImplSDLRenderer2_Init(renderer));
        menu::skinDraft.profiles[0] = {7, 282, 0.2f, 42};
        menu::skinSettings = std::make_shared<const skins::Settings>(menu::skinDraft);
        menu::skinDraftDirty = false;
        menu::skinStatus.lastFailure = {};
        menu::skinStatus.readIssue = game::WeaponReadIssue::none;
        menu::skinsEnabled = testWritesAllowed;
        menu::skinStatus.state = testWritesAllowed ? skins::State::applied : skins::State::writeLocked;
        menu::skinStatus.weapons = {{7, 282, 0.2f, true, true}, {9, 344, 0.12f, false, true}};
        menu::skinStatus.applications = 2;
        menu::skinStatus.trackedWeapons = 2;
        menu::skinStatus.sampleFresh = true;
        menu::skinStatus.inventoryComplete = true;
        for (auto& row : menu::skinStatus.weapons) {
            row.targetPaintKit = row.paintKit;
            row.targetWear = row.wear;
            row.targetSeed = row.seed;
        }
        if (scenario == 4) {
            menu::skinStatus.state = skins::State::restoring;
            menu::skinStatus.lastFailure = {skins::FailureCode::restoreRead, 299};
            menu::skinDraft.profiles[0].seed = 123;
            menu::skinDraftDirty = true;
        }
        if (scenario == 5) {
            menu::skinStatus.state = skins::State::readFailed;
            menu::skinStatus.sampleFresh = false;
            menu::skinStatus.readIssue = game::WeaponReadIssue::inventory;
        }
        menu::skinSettingsMessage = "Selections applied and saved.";
        for (int frame = 0; frame < 3; ++frame) {
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Skins test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("Diagnostics"), scenario == 5 ? 1 : 0);
            ImGui::GetStateStorage()->SetInt(ImGui::GetID("Presets"), scenario == 2 ? 1 : 0);
            menu::RenderSkinsPage();
            ImGui::End();
            ImGui::Render();
            assert(ImGui::GetDrawData()->TotalVtxCount > 0);
            SDL_SetRenderDrawColor(renderer, 12, 16, 24, 255);
            SDL_RenderClear(renderer);
            ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
            SDL_RenderPresent(renderer);
        }
        const auto path = output / ("skins-menu-" + std::to_string(scenario) + ".bmp");
        assert(SDL_SaveBMP(surface, path.string().c_str()) == 0);
        ImGui_ImplSDLRenderer2_Shutdown();
        ImGui::DestroyContext();
        SDL_DestroyRenderer(renderer);
        SDL_FreeSurface(surface);
    }
}
