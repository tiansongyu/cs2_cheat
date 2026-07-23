#include "sdl_renderer.h"
#include <dwmapi.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "../../features/menu.hpp"

#pragma comment(lib, "dwmapi.lib")

namespace
{
    struct GameDisplayGeometry
    {
        RECT gameClient{};
        RECT monitor{};
        HMONITOR monitorHandle = nullptr;
    };

    GameDisplayGeometry currentGeometry{};

    int rectWidth(const RECT& rect)
    {
        return rect.right - rect.left;
    }

    int rectHeight(const RECT& rect)
    {
        return rect.bottom - rect.top;
    }

    bool sameRect(const RECT& lhs, const RECT& rhs)
    {
        return lhs.left == rhs.left && lhs.top == rhs.top &&
            lhs.right == rhs.right && lhs.bottom == rhs.bottom;
    }

    // Keep SDL and Win32 in the same physical-pixel coordinate system. This is
    // especially important when the game is on a monitor whose scale differs
    // from the primary monitor.
    void configureVideoHints()
    {
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
    }

    bool getGameDisplayGeometry(HWND gameWindow, GameDisplayGeometry& geometry)
    {
        if (!gameWindow || !IsWindow(gameWindow)) {
            return false;
        }

        RECT clientRect{};
        if (!GetClientRect(gameWindow, &clientRect)) {
            return false;
        }

        POINT topLeft{ clientRect.left, clientRect.top };
        POINT bottomRight{ clientRect.right, clientRect.bottom };
        if (!ClientToScreen(gameWindow, &topLeft) ||
            !ClientToScreen(gameWindow, &bottomRight)) {
            return false;
        }

        RECT clientScreenRect{
            topLeft.x,
            topLeft.y,
            bottomRight.x,
            bottomRight.y
        };
        if (rectWidth(clientScreenRect) <= 0 || rectHeight(clientScreenRect) <= 0) {
            return false;
        }

        // MonitorFromWindow selects the monitor with the largest intersection
        // with the game window, so this also handles monitors left/above the
        // primary display (negative virtual-desktop coordinates).
        HMONITOR monitor = MonitorFromWindow(gameWindow, MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!monitor || !GetMonitorInfoW(monitor, &monitorInfo)) {
            return false;
        }

        geometry.gameClient = clientScreenRect;
        geometry.monitor = monitorInfo.rcMonitor;
        geometry.monitorHandle = monitor;
        return true;
    }

    void updateRenderDimensions()
    {
        if (!sdl_renderer::window) {
            return;
        }

        // ImGui draw coordinates use SDL window coordinates. With DPI scaling
        // disabled above these are physical pixels and match the game client.
        int renderWidth = 0;
        int renderHeight = 0;
        SDL_GetWindowSize(sdl_renderer::window, &renderWidth, &renderHeight);
        if (renderWidth <= 0 || renderHeight <= 0) {
            return;
        }

        WIDTH = static_cast<uint32_t>(renderWidth);
        HEIGHT = static_cast<uint32_t>(renderHeight);
        WINDOW_W = WIDTH;
        WINDOW_H = HEIGHT;
    }
}

// Initialize waiting screen on the primary display (no game attachment)
bool sdl_renderer::initWaiting()
{
    configureVideoHints();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    SDL_Rect primaryDisplay{};
    if (SDL_GetDisplayBounds(0, &primaryDisplay) != 0) {
        primaryDisplay.x = 0;
        primaryDisplay.y = 0;
        primaryDisplay.w = GetSystemMetrics(SM_CXSCREEN);
        primaryDisplay.h = GetSystemMetrics(SM_CYSCREEN);
    }

    WIDTH = static_cast<uint32_t>(primaryDisplay.w);
    HEIGHT = static_cast<uint32_t>(primaryDisplay.h);
    WINDOW_W = WIDTH;
    WINDOW_H = HEIGHT;

    window = SDL_CreateWindow(
        "CS2 ESP - Waiting",
        primaryDisplay.x,
        primaryDisplay.y,
        WIDTH, HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN
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
    configureVideoHints();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    gameHwnd = FindWindowW(nullptr, targetWindowName);
    if (!gameHwnd) {
        gameHwnd = FindWindowW(L"SDL_app", nullptr);
    }

    GameDisplayGeometry initialGeometry{};
    if (!getGameDisplayGeometry(gameHwnd, initialGeometry)) {
        SDL_Quit();
        return false;
    }

    currentGeometry = initialGeometry;
    WIDTH = static_cast<uint32_t>(rectWidth(initialGeometry.gameClient));
    HEIGHT = static_cast<uint32_t>(rectHeight(initialGeometry.gameClient));
    WINDOW_W = WIDTH;
    WINDOW_H = HEIGHT;

    window = SDL_CreateWindow(
        "Overlay",
        initialGeometry.gameClient.left,
        initialGeometry.gameClient.top,
        WIDTH, HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_SHOWN
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
    updateRenderDimensions();

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
    overlayHwnd = nullptr;
    gameHwnd = nullptr;
    currentGeometry = {};
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

    // Window position rarely changes, throttle to ~4Hz
    static DWORD lastUpdate = 0;
    DWORD now = GetTickCount();
    if (now - lastUpdate < 250) return;
    lastUpdate = now;

    if (IsIconic(gameHwnd) || !IsWindowVisible(gameHwnd)) {
        ShowWindow(overlayHwnd, SW_HIDE);
        return;
    }

    GameDisplayGeometry geometry{};
    if (!getGameDisplayGeometry(gameHwnd, geometry)) {
        return;
    }

    const int x = geometry.gameClient.left;
    const int y = geometry.gameClient.top;
    const int width = rectWidth(geometry.gameClient);
    const int height = rectHeight(geometry.gameClient);

    // The monitor rectangle is deliberately not used as the render rectangle.
    // The visible game client is the viewport. Forcing the overlay to the
    // monitor's native resolution when a windowed/borderless game uses another
    // resolution produces non-uniform X/Y scaling and visibly deforms ESP.
    if (!sameRect(geometry.gameClient, currentGeometry.gameClient) ||
        !sameRect(geometry.monitor, currentGeometry.monitor) ||
        geometry.monitorHandle != currentGeometry.monitorHandle) {
        SetWindowPos(
            overlayHwnd,
            HWND_TOPMOST,
            x,
            y,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
        currentGeometry = geometry;
    } else {
        SetWindowPos(
            overlayHwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW
        );
    }

    updateRenderDimensions();
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
