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
    for (int scenario = 0; scenario < 4; ++scenario) {
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
        menu::skinsEnabled = testWritesAllowed;
        menu::skinStatus.state = testWritesAllowed ? skins::State::applied : skins::State::writeLocked;
        menu::skinStatus.weapons = {{7, 282, 0.2f, true, true}, {9, 344, 0.12f, false, true}};
        menu::skinStatus.applications = 2;
        menu::skinStatus.trackedWeapons = 2;
        menu::skinSettingsMessage = "Selections applied and saved.";
        for (int frame = 0; frame < 3; ++frame) {
            ImGui_ImplSDLRenderer2_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(io.DisplaySize);
            ImGui::Begin("Skins test", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize);
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
