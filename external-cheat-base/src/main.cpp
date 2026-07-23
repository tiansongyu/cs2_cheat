#include "features/esp.hpp"
#include "features/aimbot.hpp"
#include "core/renderer/sdl_renderer.h"
#include "features/menu.hpp"
#include <algorithm>
#include <thread>
#include <atomic>
#include <iostream>
#include <locale>
#include <codecvt>
#include <chrono>
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
int32_t VIEWPORT_X;
int32_t VIEWPORT_Y;
uint32_t VIEWPORT_W;
uint32_t VIEWPORT_H;

// Render waiting screen with ImGui
void renderWaitingScreen(int dotCount)
{
    const float dpiScale = sdl_renderer::getDpiScale();
    const float margin = std::max(8.0f, 16.0f * dpiScale);
    const float windowWidth = std::min(
        400.0f * dpiScale,
        std::max(1.0f, static_cast<float>(WIDTH) - margin * 2.0f));
    const float windowHeight = std::min(
        200.0f * dpiScale,
        std::max(1.0f, static_cast<float>(HEIGHT) - margin * 2.0f));
    ImGui::SetNextWindowPos(
        ImVec2(
            WIDTH / 2.0f - windowWidth / 2.0f,
            HEIGHT / 2.0f - windowHeight / 2.0f
        ),
        ImGuiCond_Always
    );
    ImGui::SetNextWindowSize(
        ImVec2(windowWidth, windowHeight),
        ImGuiCond_Always
    );

    ImGui::Begin("CS2 ESP Tool", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::Dummy(ImVec2(0.0f, 10.0f * dpiScale));

    // Waiting message with animated dots
    char dots[4] = {0};
    for (int i = 0; i < dotCount; i++) dots[i] = '.';

    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Waiting for CS2 to start%s", dots);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Checking every 3 seconds...");

    ImGui::Dummy(ImVec2(0.0f, 15.0f * dpiScale));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 10.0f * dpiScale));

    // Important notice
    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[!] IMPORTANT:");
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "CS2 must use Fullscreen Windowed mode");
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Settings > Video > Display Mode)");

    ImGui::Dummy(ImVec2(0.0f, 10.0f * dpiScale));
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

    // Initialize SDL2 for waiting screen (without game window)
    if (!sdl_renderer::initWaiting()) {
        return -1;
    }
    if (!sdl_renderer::initImGui()) {
        sdl_renderer::destroy();
        return -1;
    }

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
                aimbot::init();  // Initialize aimbot module
                gameFound = true;
                break;
            }
        }

        if (!sdl_renderer::beginFrame()) {
            break;
        }
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

    if (!sdl_renderer::init(
            L"Counter-Strike 2",
            static_cast<DWORD>(esp::pID))) {
        return -1;
    }
    if (!sdl_renderer::initImGui()) {
        sdl_renderer::destroy();
        return -1;
    }

    // Data thread: consumes a coherent settings snapshot independently of render
    std::atomic<bool> dataRunning{true};
    std::thread dataThread([&dataRunning]() {
        while (dataRunning.load(std::memory_order_relaxed)) {
            const menu::RuntimeConfig config = menu::getRuntimeConfig();
            esp::updateEntities(config);
            aimbot::update(config);
            aimbot::updateTriggerbot(config);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    // Cap the overlay independently of the game. Rendering materially faster
    // than a high-refresh monitor only consumes CPU/GPU and can make CS2 less
    // consistent under load.
    constexpr auto renderInterval = std::chrono::microseconds(6944); // ~144 Hz
    auto nextRenderTime = std::chrono::steady_clock::now();
    while (sdl_renderer::running)
    {
        sdl_renderer::pollEvents();
        sdl_renderer::updateWindowPosition();
        if (!sdl_renderer::isGameVisible()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            nextRenderTime = std::chrono::steady_clock::now();
            continue;
        }

        if (!sdl_renderer::beginFrame()) {
            break;
        }
        sdl_renderer::newFrameImGui();

        if (menu::espEnabled) {
            esp::render();
        }
        esp::renderBombTimer();
        menu::render();

        sdl_renderer::renderImGui();
        sdl_renderer::endFrame();

        nextRenderTime += renderInterval;
        const auto now = std::chrono::steady_clock::now();
        if (nextRenderTime > now) {
            std::this_thread::sleep_until(nextRenderTime);
        } else {
            nextRenderTime = now;
        }
    }

    dataRunning.store(false, std::memory_order_relaxed);
    dataThread.join();

    sdl_renderer::shutdownImGui();
    sdl_renderer::destroy();
    return 0;
}
