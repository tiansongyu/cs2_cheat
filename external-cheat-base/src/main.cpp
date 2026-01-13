#include "features/esp.hpp"
#include "core/renderer/sdl_renderer.h"
#include "features/menu.hpp"
#include <thread>
#include <iostream>
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <TlHelp32.h>
#include "imgui.h"

// #define SHOW_CONSOLE

constexpr const wchar_t* PROCESS_NAME = L"external-cheat-base.exe";

void KillOtherInstances()
{
    DWORD currentPid = GetCurrentProcessId();
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return;

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(pe32);

    if (Process32FirstW(snapshot, &pe32)) {
        do {
            if (_wcsicmp(pe32.szExeFile, PROCESS_NAME) == 0 && pe32.th32ProcessID != currentPid) {
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pe32.th32ProcessID);
                if (hProcess) {
                    TerminateProcess(hProcess, 0);
                    CloseHandle(hProcess);
                }
            }
        } while (Process32NextW(snapshot, &pe32));
    }
    CloseHandle(snapshot);
}

uint32_t WIDTH;
uint32_t HEIGHT;
uint32_t WINDOW_W;
uint32_t WINDOW_H;

// Render waiting screen with ImGui
void renderWaitingScreen(int dotCount)
{
    ImGui::SetNextWindowPos(ImVec2(WIDTH / 2.0f - 200, HEIGHT / 2.0f - 100), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(400, 200), ImGuiCond_Always);

    ImGui::Begin("CS2 ESP Tool", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Dummy(ImVec2(0, 10));

    // Waiting message with animated dots
    char dots[4] = {0};
    for (int i = 0; i < dotCount; i++) dots[i] = '.';

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Waiting for CS2 to start%s", dots);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Checking every 3 seconds...");

    ImGui::Dummy(ImVec2(0, 15));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    // Important notice
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[!] IMPORTANT:");
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "CS2 must use Fullscreen Windowed mode");
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Settings > Video > Display Mode)");

    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Press F9 to exit");

    ImGui::End();
}

int main(int argc, char* argv[])
{
    KillOtherInstances();
    Sleep(100);

#ifndef SHOW_CONSOLE
    FreeConsole();
#endif

    WIDTH = GetSystemMetrics(SM_CXSCREEN);
    HEIGHT = GetSystemMetrics(SM_CYSCREEN);
    WINDOW_W = WIDTH;
    WINDOW_H = HEIGHT;

    // Initialize SDL2 for waiting screen (without game window)
    if (!sdl_renderer::initWaiting()) {
        return -1;
    }
    sdl_renderer::initImGui();

    // Wait for CS2 to start
    int dotCount = 0;
    DWORD lastCheckTime = 0;
    bool gameFound = false;

    while (sdl_renderer::running && !gameFound)
    {
        sdl_renderer::pollEvents();

        // Check for CS2 every 3 seconds
        DWORD currentTime = GetTickCount();
        if (currentTime - lastCheckTime >= 3000 || lastCheckTime == 0) {
            lastCheckTime = currentTime;
            dotCount = (dotCount % 3) + 1;

            if (esp::init()) {
                gameFound = true;
                break;
            }
        }

        sdl_renderer::beginFrame();
        sdl_renderer::newFrameImGui();
        renderWaitingScreen(dotCount);
        sdl_renderer::renderImGui();
        sdl_renderer::endFrame();

        Sleep(16);
    }

    if (!sdl_renderer::running) {
        sdl_renderer::shutdownImGui();
        sdl_renderer::destroy();
        return 0;
    }

    // Game found, reinitialize renderer to attach to game window
    sdl_renderer::shutdownImGui();
    sdl_renderer::destroy();

    if (!sdl_renderer::init(L"Counter-Strike 2")) {
        return -1;
    }
    sdl_renderer::initImGui();

    // Main loop
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

    sdl_renderer::shutdownImGui();
    sdl_renderer::destroy();
    return 0;
}
