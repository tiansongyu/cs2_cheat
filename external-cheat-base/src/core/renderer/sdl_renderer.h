#pragma once

#include <SDL.h>
#include <SDL_syswm.h>
#include <Windows.h>
#include <cstdint>

extern uint32_t WIDTH;
extern uint32_t HEIGHT;
extern uint32_t WINDOW_W;
extern uint32_t WINDOW_H;
extern int32_t VIEWPORT_X;
extern int32_t VIEWPORT_Y;
extern uint32_t VIEWPORT_W;
extern uint32_t VIEWPORT_H;

namespace sdl_renderer
{
    inline SDL_Window* window = nullptr;
    inline SDL_Renderer* renderer = nullptr;
    inline bool running = true;
    inline HWND overlayHwnd = nullptr;
    inline HWND gameHwnd = nullptr;
    inline bool menuVisible = true;

    bool init(const wchar_t* targetWindowName, DWORD targetProcessId = 0);
    bool initWaiting();  // Initialize waiting screen (no game window)
    void destroy();
    bool beginFrame();
    void endFrame();
    void pollEvents();
    void updateWindowPosition();
    bool isGameVisible();
    float getDpiScale();
    uint32_t getDpiRevision();

    bool initImGui();
    void shutdownImGui();
    void newFrameImGui();
    void renderImGui();

    namespace draw
    {
        void line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        void box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        void filledBox(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        void filledEllipse(int cx, int cy, int rx, int ry, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);
        void text(int x, int y, const char* str, uint8_t r, uint8_t g, uint8_t b);
    }
}
