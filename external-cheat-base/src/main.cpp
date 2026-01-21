#include "features/esp.hpp"
#include "features/aimbot.hpp"
#include "core/renderer/sdl_renderer.h"
#include "features/menu.hpp"
#include <thread>
#include <iostream>
#include <locale>
#include <codecvt>
#include <chrono>
#include <intrin.h>  // For _mm_pause()
#include <Windows.h>
#include <TlHelp32.h>
#include "imgui.h"

// High precision timer for frame limiting
class HighPrecisionTimer {
public:
    HighPrecisionTimer() {
        QueryPerformanceFrequency(&frequency);
        QueryPerformanceCounter(&lastTime);
    }

    // Returns elapsed time in microseconds since last call to reset()
    double getElapsedMicroseconds() const {
        LARGE_INTEGER currentTime;
        QueryPerformanceCounter(&currentTime);
        return static_cast<double>(currentTime.QuadPart - lastTime.QuadPart) * 1000000.0 / frequency.QuadPart;
    }

    void reset() {
        QueryPerformanceCounter(&lastTime);
    }

    // Precise wait using spin-lock (more accurate than Sleep)
    void waitUntilMicroseconds(double targetMicroseconds) {
        // Use Sleep for the bulk of waiting (saves CPU), then spin for precision
        double remaining = targetMicroseconds - getElapsedMicroseconds();

        // Sleep for most of the time (leave 1.5ms margin for spin)
        if (remaining > 2000.0) {
            Sleep(static_cast<DWORD>((remaining - 1500.0) / 1000.0));
        }

        // Spin-wait for the remaining time (high precision)
        while (getElapsedMicroseconds() < targetMicroseconds) {
            _mm_pause();  // CPU hint for spin-wait, reduces power consumption
        }
    }

private:
    LARGE_INTEGER frequency;
    LARGE_INTEGER lastTime;
};

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
                aimbot::init();  // Initialize aimbot module
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

    // High precision timer for frame limiting
    HighPrecisionTimer frameTimer;

    // Main loop
    while (sdl_renderer::running)
    {
        frameTimer.reset();

        sdl_renderer::pollEvents();
        sdl_renderer::updateWindowPosition();
        esp::updateEntities();
        aimbot::update();          // Update aimbot every frame
        aimbot::updateRCS();       // Update RCS every frame
        aimbot::updateTriggerbot(); // Update triggerbot every frame

        sdl_renderer::beginFrame();
        sdl_renderer::newFrameImGui();

        if (menu::espEnabled) {
            esp::render();
        }
        menu::render();

        sdl_renderer::renderImGui();
        sdl_renderer::endFrame();

        // High precision frame rate limiting
        double targetFrameTimeMicros = 1000000.0 / menu::targetFPS;
        frameTimer.waitUntilMicroseconds(targetFrameTimeMicros);
    }

    sdl_renderer::shutdownImGui();
    sdl_renderer::destroy();
    return 0;
}
