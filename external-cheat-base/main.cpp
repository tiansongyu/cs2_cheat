#include "esp.hpp"
#include "renderer/sdl_renderer.h"
#include "menu.hpp"
#include <thread>
#include <iostream>
#include <locale>
#include <codecvt>
#include <Windows.h>

uint32_t WIDTH;
uint32_t HEIGHT;
uint32_t WINDOW_W;
uint32_t WINDOW_H;

int main(int argc, char* argv[])
{
    SetConsoleOutputCP(CP_UTF8);
    std::locale::global(std::locale(""));

    std::cout << "======================================" << std::endl;
    std::cout << "    CS2 ESP Tool (SDL2 + ImGui)" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << std::endl;

    if (!esp::init()) {
        std::cout << "Make sure CS2 is running." << std::endl;
        system("pause");
        return -1;
    }

    WIDTH = GetSystemMetrics(SM_CXSCREEN);
    HEIGHT = GetSystemMetrics(SM_CYSCREEN);
    WINDOW_W = WIDTH;
    WINDOW_H = HEIGHT;

    std::cout << "Initializing SDL2 overlay..." << std::endl;
    if (!sdl_renderer::init(L"Counter-Strike 2")) {
        std::cout << "ERROR: Failed to initialize SDL2 renderer!" << std::endl;
        std::cout << "Make sure CS2 window is visible." << std::endl;
        system("pause");
        return -1;
    }

    std::cout << "Initializing ImGui..." << std::endl;
    sdl_renderer::initImGui();

    std::cout << "SDL2 overlay initialized successfully!" << std::endl;
    std::cout << "Window size: " << WIDTH << "x" << HEIGHT << std::endl;
    std::cout << std::endl;
    std::cout << "========== Controls ==========" << std::endl;
    std::cout << "  INSERT - Toggle menu" << std::endl;
    std::cout << "  F9     - Exit program" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << std::endl;
    std::cout << "ESP is now active!" << std::endl;

    while (sdl_renderer::running)
    {
        sdl_renderer::pollEvents();
        sdl_renderer::updateWindowPosition();
        esp::updateEntities();

        sdl_renderer::beginFrame();
        sdl_renderer::newFrameImGui();

        if (menu::espEnabled) {
            esp::render();
        }

        menu::render();

        sdl_renderer::renderImGui();
        sdl_renderer::endFrame();

        Sleep(1);
    }

    std::cout << "Cleaning up..." << std::endl;
    sdl_renderer::shutdownImGui();
    sdl_renderer::destroy();

    std::cout << "Exiting..." << std::endl;
    return 0;
}
