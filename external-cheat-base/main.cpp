#include "renderer/renderer.h"
#include "esp.hpp"
#include <thread>
#include <iostream>

uint32_t WIDTH;
uint32_t HEIGHT;
uint32_t WINDOW_W;
uint32_t WINDOW_H;
int main()
{
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

    bool aimBotEnabled = false;
        while (!GetAsyncKeyState(VK_F9) && renderer::running)
        {
            esp::frame();

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
            if (aimBotEnabled && (GetAsyncKeyState(VK_LSHIFT) & 0x8000))
            {
                esp::aim_bot();
            }
            if (aimBotEnabled) {
                esp::auto_trigger();

            }
        }
    renderer::destroy();

    return 0;
}
