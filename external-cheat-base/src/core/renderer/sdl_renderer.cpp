#include "sdl_renderer.h"
#include <dwmapi.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "../../features/menu.hpp"

#pragma comment(lib, "dwmapi.lib")

// Initialize waiting screen (centered window, no game attachment)
bool sdl_renderer::initWaiting()
{
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    window = SDL_CreateWindow(
        "CS2 ESP - Waiting",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SHOWN
    );

    if (!window) {
        SDL_Quit();
        return false;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        overlayHwnd = wmInfo.info.win.window;
    }

    if (overlayHwnd) {
        LONG_PTR exStyle = GetWindowLongPtr(overlayHwnd, GWL_EXSTYLE);
        SetWindowLongPtr(overlayHwnd, GWL_EXSTYLE,
            exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(overlayHwnd, &margins);

        SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    }

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    return true;
}

bool sdl_renderer::init(const wchar_t* targetWindowName)
{
    gameHwnd = FindWindowW(nullptr, targetWindowName);
    if (!gameHwnd) {
        gameHwnd = FindWindowW(L"SDL_app", nullptr);
    }

    RECT gameRect;
    if (gameHwnd && GetWindowRect(gameHwnd, &gameRect)) {
        WIDTH = gameRect.right - gameRect.left;
        HEIGHT = gameRect.bottom - gameRect.top;
        WINDOW_W = WIDTH;
        WINDOW_H = HEIGHT;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    window = SDL_CreateWindow(
        "Overlay",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        WIDTH, HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP | SDL_WINDOW_SHOWN
    );

    if (!window) {
        SDL_Quit();
        return false;
    }

    SDL_SysWMinfo wmInfo;
    SDL_VERSION(&wmInfo.version);
    if (SDL_GetWindowWMInfo(window, &wmInfo)) {
        overlayHwnd = wmInfo.info.win.window;
    }

    if (overlayHwnd) {
        LONG_PTR exStyle = GetWindowLongPtr(overlayHwnd, GWL_EXSTYLE);
        // WS_EX_TRANSPARENT will be set when menu is hidden (F4)
        SetWindowLongPtr(overlayHwnd, GWL_EXSTYLE,
            exStyle | WS_EX_LAYERED | WS_EX_TOPMOST);

        SetLayeredWindowAttributes(overlayHwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);

        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(overlayHwnd, &margins);

        SetWindowPos(overlayHwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(overlayHwnd);
    }

    renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED);

    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    updateWindowPosition();

    return true;
}

void sdl_renderer::destroy()
{
    if (renderer) {
        SDL_DestroyRenderer(renderer);
        renderer = nullptr;
    }
    if (window) {
        SDL_DestroyWindow(window);
        window = nullptr;
    }
    SDL_Quit();
}

void sdl_renderer::beginFrame()
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
}

void sdl_renderer::endFrame()
{
    SDL_RenderPresent(renderer);
}

void sdl_renderer::pollEvents()
{
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT) {
            running = false;
        }
    }

    // Skip hotkey processing if we're binding a key
    if (menu::isBindingKey) {
        return;
    }

    // Check exit key (configurable, default: F9)
    if (GetAsyncKeyState(menu::exitKey) & 0x8000) {
        running = false;
    }

    // Check menu toggle key (configurable, default: F4)
    static bool menuKeyPressed = false;
    if (GetAsyncKeyState(menu::menuToggleKey) & 0x8000) {
        if (!menuKeyPressed) {
            menuKeyPressed = true;
            menuVisible = !menuVisible;
            if (menuVisible) {
                LONG_PTR exStyle = GetWindowLongPtr(overlayHwnd, GWL_EXSTYLE);
                SetWindowLongPtr(overlayHwnd, GWL_EXSTYLE, exStyle & ~WS_EX_TRANSPARENT);
            } else {
                LONG_PTR exStyle = GetWindowLongPtr(overlayHwnd, GWL_EXSTYLE);
                SetWindowLongPtr(overlayHwnd, GWL_EXSTYLE, exStyle | WS_EX_TRANSPARENT);
            }
        }
    } else {
        menuKeyPressed = false;
    }
}

void sdl_renderer::updateWindowPosition()
{
    if (!gameHwnd || !overlayHwnd) return;

    if (!IsWindow(gameHwnd)) {
        running = false;
        return;
    }

    // Throttle: only update every 16ms (~60Hz) to reduce Win32 API overhead
    static DWORD lastUpdate = 0;
    DWORD now = GetTickCount();
    if (now - lastUpdate < 16) return;
    lastUpdate = now;

    RECT gameRect;
    if (GetWindowRect(gameHwnd, &gameRect)) {
        int x = gameRect.left;
        int y = gameRect.top;
        int w = gameRect.right - gameRect.left;
        int h = gameRect.bottom - gameRect.top;

        WIDTH = w;
        HEIGHT = h;
        WINDOW_W = w;
        WINDOW_H = h;

        SetWindowPos(overlayHwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        BringWindowToTop(overlayHwnd);
    }
}

void sdl_renderer::draw::line(int x1, int y1, int x2, int y2, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_RenderDrawLine(renderer, x1, y1, x2, y2);
}

void sdl_renderer::draw::box(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderDrawRect(renderer, &rect);

    SDL_Rect rect2 = { x - 1, y - 1, w + 2, h + 2 };
    SDL_RenderDrawRect(renderer, &rect2);
}

void sdl_renderer::draw::filledBox(int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    SDL_SetRenderDrawColor(renderer, r, g, b, a);
    SDL_Rect rect = { x, y, w, h };
    SDL_RenderFillRect(renderer, &rect);
}

void sdl_renderer::draw::text(int x, int y, const char* str, uint8_t r, uint8_t g, uint8_t b)
{
}

void sdl_renderer::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    io.FontGlobalScale = 1.0f;
    ImFontConfig fontConfig;
    fontConfig.SizePixels = 18.0f;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    io.Fonts->AddFontDefault(&fontConfig);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.Alpha = 0.95f;
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.TabBarBorderSize = 1.0f;

    ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer2_Init(renderer);
}

void sdl_renderer::shutdownImGui()
{
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

void sdl_renderer::newFrameImGui()
{
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void sdl_renderer::renderImGui()
{
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData(), renderer);
}
