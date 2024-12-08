#include "renderer/renderer.h"
#include "esp.hpp"
#include <thread>
#include <iostream>
#include <locale>
#include <codecvt>
#include <Windows.h>

uint32_t WIDTH;
uint32_t HEIGHT;
uint32_t WINDOW_W;
uint32_t WINDOW_H;

std::atomic<bool> aimBotEnabled(false);

void autoTriggerThread() {
    while (true) {
        if (aimBotEnabled) {
            esp::auto_trigger();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); 
    }
}

int main()
{
    // 设置控制台输出编码为 UTF-8
    SetConsoleOutputCP(CP_UTF8);

    // 设置区域设置为用户默认区域设置
    std::locale::global(std::locale(""));

    esp::pID = memory::GetProcID(L"cs2.exe");
    esp::modBase = memory::GetModuleBaseAddress(esp::pID, L"client.dll");
    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    WIDTH = width;
    HEIGHT = height;
    WINDOW_W = width;
    WINDOW_H = height;
    HWND hwnd = window::InitWindow(NULL);
    if (!hwnd) return -1;
    
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_EXSTYLE);
    SetWindowLongPtr(hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED | WS_EX_TRANSPARENT);

    if (!renderer::init(hwnd))
    {
        renderer::destroy();
        return -1;
    }

    std::thread read(esp::loop);
    if (renderer::running)
    {
        std::cout << "start " << std::endl;
    }
    else {
        std::cout << "failed " << std::endl;
    }
    std::thread triggerThread(autoTriggerThread);

    triggerThread.detach();


    while (!GetAsyncKeyState(VK_F9) && renderer::running)
    {
        esp::frame(aimBotEnabled);

        if (GetAsyncKeyState(VK_RSHIFT) & 0x8000)
        {
                aimBotEnabled = !aimBotEnabled;
                if (aimBotEnabled)
                {
                    MessageBox(NULL, L"Aim Bot Enabled", L"Info", MB_OK | MB_TOPMOST);
                }
                else
                {
                    MessageBox(NULL, L"Aim Bot Disabled", L"Info",MB_OK | MB_TOPMOST);
                }
            
        }
        if (aimBotEnabled && (GetAsyncKeyState(VK_CAPITAL) & 0x8000))
        {
            esp::aim_bot();
        }
    }
    renderer::destroy();

    return 0;
}
