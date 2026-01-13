#include "features/esp.hpp"
#include "core/renderer/sdl_renderer.h"
#include "features/menu.hpp"
#include <thread>
#include <iostream>
#include <locale>
#include <codecvt>
#include <Windows.h>
#include <TlHelp32.h>

// 定义 SHOW_CONSOLE 宏来显示控制台窗口（调试用）
// 编译时添加 /DSHOW_CONSOLE 或在此处取消注释来启用
// #define SHOW_CONSOLE

// 程序唯一标识符（用于单进程检测）
constexpr const wchar_t* MUTEX_NAME = L"CS2_ESP_SINGLE_INSTANCE_MUTEX";
constexpr const wchar_t* PROCESS_NAME = L"external-cheat-base.exe";

// 关闭其他同名进程
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

int main(int argc, char* argv[])
{
    // 单进程检测：关闭其他已运行的实例
    KillOtherInstances();
    Sleep(100); // 等待旧进程完全退出

#ifndef SHOW_CONSOLE
    // Release模式下隐藏控制台窗口
    FreeConsole();
#else
    SetConsoleOutputCP(CP_UTF8);
    std::locale::global(std::locale(""));

    std::cout << "======================================" << std::endl;
    std::cout << "    CS2 ESP Tool (SDL2 + ImGui)" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << std::endl;
#endif

    if (!esp::init()) {
#ifdef SHOW_CONSOLE
        std::cout << "Make sure CS2 is running." << std::endl;
        system("pause");
#endif
        return -1;
    }

    WIDTH = GetSystemMetrics(SM_CXSCREEN);
    HEIGHT = GetSystemMetrics(SM_CYSCREEN);
    WINDOW_W = WIDTH;
    WINDOW_H = HEIGHT;

#ifdef SHOW_CONSOLE
    std::cout << "Initializing SDL2 overlay..." << std::endl;
#endif
    if (!sdl_renderer::init(L"Counter-Strike 2")) {
#ifdef SHOW_CONSOLE
        std::cout << "ERROR: Failed to initialize SDL2 renderer!" << std::endl;
        std::cout << "Make sure CS2 window is visible." << std::endl;
        system("pause");
#endif
        return -1;
    }

#ifdef SHOW_CONSOLE
    std::cout << "Initializing ImGui..." << std::endl;
#endif
    sdl_renderer::initImGui();

#ifdef SHOW_CONSOLE
    std::cout << "SDL2 overlay initialized successfully!" << std::endl;
    std::cout << "Window size: " << WIDTH << "x" << HEIGHT << std::endl;
    std::cout << std::endl;
    std::cout << "========== Controls ==========" << std::endl;
    std::cout << "  INSERT - Toggle menu" << std::endl;
    std::cout << "  F9     - Exit program" << std::endl;
    std::cout << "==============================" << std::endl;
    std::cout << std::endl;
    std::cout << "ESP is now active!" << std::endl;
#endif

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

#ifdef SHOW_CONSOLE
    std::cout << "Cleaning up..." << std::endl;
#endif
    sdl_renderer::shutdownImGui();
    sdl_renderer::destroy();

#ifdef SHOW_CONSOLE
    std::cout << "Exiting..." << std::endl;
#endif
    return 0;
}
