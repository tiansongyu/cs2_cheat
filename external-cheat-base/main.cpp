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

int main()
{
    // Set console output encoding to UTF-8
    SetConsoleOutputCP(CP_UTF8);

    // Set locale to user default
    std::locale::global(std::locale(""));

    std::cout << "======================================" << std::endl;
    std::cout << "    CS2 Entity Debug Tool" << std::endl;
    std::cout << "======================================" << std::endl;
    std::cout << std::endl;

    esp::pID = memory::GetProcID(L"cs2.exe");
    if (!esp::pID) {
        std::cout << "ERROR: Cannot find cs2.exe process!" << std::endl;
        std::cout << "Make sure CS2 is running." << std::endl;
        system("pause");
        return -1;
    }
    std::cout << "Found cs2.exe, PID: " << esp::pID << std::endl;

    esp::modBase = memory::GetModuleBaseAddress(esp::pID, L"client.dll");
    if (!esp::modBase) {
        std::cout << "ERROR: Cannot find client.dll!" << std::endl;
        system("pause");
        return -1;
    }
    std::cout << "client.dll base: 0x" << std::hex << esp::modBase << std::dec << std::endl;
    std::cout << std::endl;

    int width = GetSystemMetrics(SM_CXSCREEN);
    int height = GetSystemMetrics(SM_CYSCREEN);

    WIDTH = width;
    HEIGHT = height;
    WINDOW_W = width;
    WINDOW_H = height;

    std::cout << "Press ENTER to scan entities (must be in a game with bots)..." << std::endl;
    std::cout << "Press F9 to exit." << std::endl;
    std::cout << std::endl;

    while (!GetAsyncKeyState(VK_F9))
    {
        if (GetAsyncKeyState(VK_RETURN) & 0x8000)
        {
            std::cout << "\n--- Scanning... ---\n" << std::endl;
            esp::debug_loop();
            std::cout << "\nPress ENTER to scan again, F9 to exit.\n" << std::endl;
            Sleep(500); // Prevent continuous trigger
        }
        Sleep(50);
    }

    std::cout << "Exiting..." << std::endl;
    return 0;
}
