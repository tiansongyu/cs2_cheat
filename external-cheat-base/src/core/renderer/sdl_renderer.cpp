#include "sdl_renderer.h"
#include <dwmapi.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "../../features/menu.hpp"
#include <algorithm>
#include <cmath>
#include <iterator>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "gdi32.lib")

namespace
{
    constexpr Uint8 TRANSPARENCY_KEY_R = 1;
    constexpr Uint8 TRANSPARENCY_KEY_G = 0;
    constexpr Uint8 TRANSPARENCY_KEY_B = 1;
    constexpr COLORREF TRANSPARENCY_COLOR_KEY =
        RGB(TRANSPARENCY_KEY_R, TRANSPARENCY_KEY_G, TRANSPARENCY_KEY_B);
    constexpr float BASE_FONT_SIZE = 18.0f;

    struct GameDisplayGeometry
    {
        RECT gameClient{};
        RECT monitor{};
        HMONITOR monitorHandle = nullptr;
    };

    struct ContentViewport
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    struct WindowSearchContext
    {
        DWORD processId = 0;
        const wchar_t* expectedTitle = nullptr;
        HWND bestWindow = nullptr;
        uint64_t bestScore = 0;
    };

    GameDisplayGeometry currentGeometry{};
    ContentViewport currentViewport{};
    ContentViewport pendingViewport{};
    int pendingViewportSamples = 0;
    int appliedViewportMode = -1;
    ImGuiStyle baseImGuiStyle{};
    float currentDpiScale = 1.0f;
    uint32_t dpiRevision = 0;
    bool baseImGuiStyleReady = false;
    bool imguiInitialized = false;
    bool gameVisible = true;

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

    bool sameViewport(const ContentViewport& lhs, const ContentViewport& rhs)
    {
        return lhs.x == rhs.x && lhs.y == rhs.y &&
            lhs.width == rhs.width && lhs.height == rhs.height;
    }

    void publishViewport(const ContentViewport& viewport)
    {
        currentViewport = viewport;
        VIEWPORT_X = viewport.x;
        VIEWPORT_Y = viewport.y;
        VIEWPORT_W = static_cast<uint32_t>(std::max(0, viewport.width));
        VIEWPORT_H = static_cast<uint32_t>(std::max(0, viewport.height));
    }

    BOOL CALLBACK findProcessWindow(HWND hwnd, LPARAM parameter)
    {
        auto* context = reinterpret_cast<WindowSearchContext*>(parameter);
        if (!context || GetWindow(hwnd, GW_OWNER)) {
            return TRUE;
        }

        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(hwnd, &windowProcessId);
        if (context->processId != 0 && windowProcessId != context->processId) {
            return TRUE;
        }

        RECT client{};
        if (!GetClientRect(hwnd, &client)) {
            return TRUE;
        }

        const int width = rectWidth(client);
        const int height = rectHeight(client);
        if (width < 640 || height < 480) {
            return TRUE;
        }

        wchar_t title[256]{};
        GetWindowTextW(hwnd, title, static_cast<int>(std::size(title)));
        const bool exactTitle =
            context->expectedTitle &&
            _wcsicmp(title, context->expectedTitle) == 0;
        const bool visible = IsWindowVisible(hwnd) != FALSE;

        // Prefer the expected title, then visibility and area among top-level
        // windows owned by cs2.exe. Minimized games remain attachable.
        const uint64_t area =
            static_cast<uint64_t>(width) * static_cast<uint64_t>(height);
        const uint64_t score =
            area +
            (exactTitle ? (uint64_t{1} << 62) : 0) +
            (visible ? (uint64_t{1} << 61) : 0);
        if (score > context->bestScore) {
            context->bestScore = score;
            context->bestWindow = hwnd;
        }
        return TRUE;
    }

    HWND findGameWindow(DWORD processId, const wchar_t* expectedTitle)
    {
        WindowSearchContext context{};
        context.processId = processId;
        context.expectedTitle = expectedTitle;
        EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&context));
        if (context.bestWindow) {
            return context.bestWindow;
        }

        // The title fallback is only used if no process id was supplied. A
        // process-scoped search must never silently attach to another process.
        return processId == 0 ? FindWindowW(nullptr, expectedTitle) : nullptr;
    }

    bool isNearBlack(COLORREF color)
    {
        return color != CLR_INVALID &&
            GetRValue(color) <= 12 &&
            GetGValue(color) <= 12 &&
            GetBValue(color) <= 12;
    }

    float blackRatioForColumn(HDC desktop, int x, const RECT& rect)
    {
        constexpr int SAMPLE_COUNT = 17;
        int valid = 0;
        int black = 0;
        const int height = rectHeight(rect);
        for (int i = 1; i <= SAMPLE_COUNT; ++i) {
            const int y = rect.top + (height * i) / (SAMPLE_COUNT + 1);
            const COLORREF color = GetPixel(desktop, x, y);
            if (color == CLR_INVALID) {
                continue;
            }
            ++valid;
            black += isNearBlack(color) ? 1 : 0;
        }
        return valid > 0
            ? static_cast<float>(black) / static_cast<float>(valid)
            : 0.0f;
    }

    float blackRatioForRow(HDC desktop, int y, const RECT& rect)
    {
        constexpr int SAMPLE_COUNT = 17;
        int valid = 0;
        int black = 0;
        const int width = rectWidth(rect);
        for (int i = 1; i <= SAMPLE_COUNT; ++i) {
            const int x = rect.left + (width * i) / (SAMPLE_COUNT + 1);
            const COLORREF color = GetPixel(desktop, x, y);
            if (color == CLR_INVALID) {
                continue;
            }
            ++valid;
            black += isNearBlack(color) ? 1 : 0;
        }
        return valid > 0
            ? static_cast<float>(black) / static_cast<float>(valid)
            : 0.0f;
    }

    ContentViewport detectContentViewport(const RECT& clientScreenRect)
    {
        const int width = rectWidth(clientScreenRect);
        const int height = rectHeight(clientScreenRect);
        ContentViewport result{ 0, 0, width, height };
        if (width <= 0 || height <= 0) {
            return result;
        }

        HDC desktop = GetDC(nullptr);
        if (!desktop) {
            return result;
        }

        const int maxHorizontalBorder = width * 3 / 10;
        const int maxVerticalBorder = height * 3 / 10;
        int left = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;

        while (left < maxHorizontalBorder &&
            blackRatioForColumn(
                desktop,
                clientScreenRect.left + left,
                clientScreenRect) >= 0.88f) {
            ++left;
        }
        while (right < maxHorizontalBorder &&
            blackRatioForColumn(
                desktop,
                clientScreenRect.right - 1 - right,
                clientScreenRect) >= 0.88f) {
            ++right;
        }
        while (top < maxVerticalBorder &&
            blackRatioForRow(
                desktop,
                clientScreenRect.top + top,
                clientScreenRect) >= 0.88f) {
            ++top;
        }
        while (bottom < maxVerticalBorder &&
            blackRatioForRow(
                desktop,
                clientScreenRect.bottom - 1 - bottom,
                clientScreenRect) >= 0.88f) {
            ++bottom;
        }

        const int horizontalTolerance = std::max(4, width / 100);
        const int verticalTolerance = std::max(4, height / 100);
        const int minimumHorizontalBorder = std::max(8, width / 100);
        const int minimumVerticalBorder = std::max(8, height / 100);
        const int horizontalProbe =
            std::min(width - 1, left + std::max(3, width / 200));
        const int verticalProbe =
            std::min(height - 1, top + std::max(3, height / 200));

        const bool hasSideBars =
            left >= minimumHorizontalBorder &&
            right >= minimumHorizontalBorder &&
            std::abs(left - right) <= horizontalTolerance &&
            blackRatioForColumn(
                desktop,
                clientScreenRect.left + horizontalProbe,
                clientScreenRect) < 0.65f;
        const bool hasTopBars =
            top >= minimumVerticalBorder &&
            bottom >= minimumVerticalBorder &&
            std::abs(top - bottom) <= verticalTolerance &&
            blackRatioForRow(
                desktop,
                clientScreenRect.top + verticalProbe,
                clientScreenRect) < 0.65f;

        ReleaseDC(nullptr, desktop);

        // A real game viewport normally letterboxes on only one axis. If both
        // candidates match, retain the stronger normalized pair.
        if (hasSideBars &&
            (!hasTopBars ||
             static_cast<float>(left + right) / width >=
                 static_cast<float>(top + bottom) / height)) {
            const int symmetricBorder = (left + right) / 2;
            result.x = symmetricBorder;
            result.width = width - symmetricBorder * 2;
        } else if (hasTopBars) {
            const int symmetricBorder = (top + bottom) / 2;
            result.y = symmetricBorder;
            result.height = height - symmetricBorder * 2;
        }

        if (result.width < width / 2 || result.height < height / 2) {
            return ContentViewport{ 0, 0, width, height };
        }
        return result;
    }

    ContentViewport viewportForAspect(
        int clientWidth,
        int clientHeight,
        float targetAspect)
    {
        ContentViewport result{ 0, 0, clientWidth, clientHeight };
        if (clientWidth <= 0 ||
            clientHeight <= 0 ||
            targetAspect <= 0.0f) {
            return result;
        }

        const float clientAspect =
            static_cast<float>(clientWidth) /
            static_cast<float>(clientHeight);
        if (clientAspect > targetAspect) {
            result.width = static_cast<int>(
                std::round(clientHeight * targetAspect));
            result.x = (clientWidth - result.width) / 2;
        } else if (clientAspect < targetAspect) {
            result.height = static_cast<int>(
                std::round(clientWidth / targetAspect));
            result.y = (clientHeight - result.height) / 2;
        }
        return result;
    }

    ContentViewport viewportForMode(
        int mode,
        int clientWidth,
        int clientHeight)
    {
        switch (mode) {
            case 2:
                return viewportForAspect(
                    clientWidth,
                    clientHeight,
                    4.0f / 3.0f);
            case 3:
                return viewportForAspect(
                    clientWidth,
                    clientHeight,
                    16.0f / 10.0f);
            default:
                return ContentViewport{
                    0,
                    0,
                    clientWidth,
                    clientHeight
                };
        }
    }

    // Keep SDL and Win32 in the same physical-pixel coordinate system. This is
    // especially important when the game is on a monitor whose scale differs
    // from the primary monitor.
    void configureVideoHints()
    {
        using SetProcessDpiAwarenessContextFn =
            BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        static SetProcessDpiAwarenessContextFn setProcessDpiAwarenessContext =
            []() {
                HMODULE user32 = GetModuleHandleW(L"user32.dll");
                return user32
                    ? reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                        GetProcAddress(
                            user32,
                            "SetProcessDpiAwarenessContext"))
                    : nullptr;
            }();

        if (setProcessDpiAwarenessContext) {
            // Failure is expected if the host has already selected an equal
            // or stronger awareness mode; SDL's hint below remains in place.
            setProcessDpiAwarenessContext(
                DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        } else {
            using SetProcessDpiAwareFn = BOOL(WINAPI*)();
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            const auto setProcessDpiAware = user32
                ? reinterpret_cast<SetProcessDpiAwareFn>(
                    GetProcAddress(user32, "SetProcessDPIAware"))
                : nullptr;
            if (setProcessDpiAware) {
                setProcessDpiAware();
            }
        }

        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
    }

    float getWindowDpiScale(HWND targetWindow)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static GetDpiForWindowFn getDpiForWindowFn = []() {
            HMODULE user32 = GetModuleHandleW(L"user32.dll");
            return user32
                ? reinterpret_cast<GetDpiForWindowFn>(
                    GetProcAddress(user32, "GetDpiForWindow"))
                : nullptr;
        }();

        UINT dpi = 96;
        bool hasWindowDpi = false;
        if (targetWindow && getDpiForWindowFn) {
            const UINT windowDpi = getDpiForWindowFn(targetWindow);
            if (windowDpi != 0) {
                dpi = windowDpi;
                hasWindowDpi = true;
            }
        }
        if (!hasWindowDpi && targetWindow) {
            using GetDpiForMonitorFn =
                HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static GetDpiForMonitorFn getDpiForMonitorFn = []() {
                HMODULE shcore = LoadLibraryW(L"shcore.dll");
                return shcore
                    ? reinterpret_cast<GetDpiForMonitorFn>(
                        GetProcAddress(shcore, "GetDpiForMonitor"))
                    : nullptr;
            }();
            if (getDpiForMonitorFn) {
                const HMONITOR monitor = MonitorFromWindow(
                    targetWindow,
                    MONITOR_DEFAULTTONEAREST);
                UINT dpiX = 0;
                UINT dpiY = 0;
                if (monitor &&
                    SUCCEEDED(getDpiForMonitorFn(
                        monitor,
                        0,
                        &dpiX,
                        &dpiY)) &&
                    dpiX != 0) {
                    dpi = dpiX;
                    hasWindowDpi = true;
                }
            }
        }
        if (!hasWindowDpi) {
            using GetDpiForSystemFn = UINT(WINAPI*)();
            static GetDpiForSystemFn getDpiForSystemFn = []() {
                HMODULE user32 = GetModuleHandleW(L"user32.dll");
                return user32
                    ? reinterpret_cast<GetDpiForSystemFn>(
                        GetProcAddress(user32, "GetDpiForSystem"))
                    : nullptr;
            }();
            if (getDpiForSystemFn) {
                const UINT systemDpi = getDpiForSystemFn();
                if (systemDpi != 0) {
                    dpi = systemDpi;
                }
            } else {
                HDC desktop = GetDC(nullptr);
                if (desktop) {
                    const int logicalDpi = GetDeviceCaps(desktop, LOGPIXELSX);
                    ReleaseDC(nullptr, desktop);
                    if (logicalDpi > 0) {
                        dpi = static_cast<UINT>(logicalDpi);
                    }
                }
            }
        }

        float scale = static_cast<float>(dpi) / 96.0f;
        if (scale < 0.5f) scale = 0.5f;
        if (scale > 4.0f) scale = 4.0f;
        return scale;
    }

    void applyImGuiDpiScale(bool force = false)
    {
        if (!baseImGuiStyleReady || !ImGui::GetCurrentContext()) {
            return;
        }

        const float scale = getWindowDpiScale(sdl_renderer::overlayHwnd);
        if (!force &&
            scale > currentDpiScale - 0.01f &&
            scale < currentDpiScale + 0.01f) {
            return;
        }

        // Always scale from the unmodified reference style. ScaleAllSizes()
        // rounds values, so repeatedly scaling the current style would drift
        // after moving between monitors several times.
        ImGuiStyle scaledStyle = baseImGuiStyle;
        scaledStyle.FontScaleDpi = scale;
        scaledStyle.ScaleAllSizes(scale);
        ImGui::GetStyle() = scaledStyle;

        currentDpiScale = scale;
        ++dpiRevision;
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

        const bool dimensionsChanged =
            WINDOW_W != static_cast<uint32_t>(renderWidth) ||
            WINDOW_H != static_cast<uint32_t>(renderHeight);
        WIDTH = static_cast<uint32_t>(renderWidth);
        HEIGHT = static_cast<uint32_t>(renderHeight);
        WINDOW_W = WIDTH;
        WINDOW_H = HEIGHT;
        if (dimensionsChanged ||
            currentViewport.width <= 0 ||
            currentViewport.height <= 0) {
            publishViewport(viewportForMode(
                menu::viewportMode,
                renderWidth,
                renderHeight));
            pendingViewport = {};
            pendingViewportSamples = 0;
        }
    }

    bool configureOverlayWindow(HWND hwnd)
    {
        if (!hwnd) {
            return false;
        }

        SetLastError(0);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (exStyle == 0 && GetLastError() != 0) {
            return false;
        }

        SetLastError(0);
        const LONG_PTR previousStyle = SetWindowLongPtrW(
            hwnd,
            GWL_EXSTYLE,
            exStyle | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW);
        if (previousStyle == 0 && GetLastError() != 0) {
            return false;
        }

        if (!SetLayeredWindowAttributes(
                hwnd,
                TRANSPARENCY_COLOR_KEY,
                0,
                LWA_COLORKEY)) {
            return false;
        }

        MARGINS margins = { -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);
        return SetWindowPos(
            hwnd,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != FALSE;
    }

    bool setClickThrough(HWND hwnd, bool enabled)
    {
        if (!hwnd) {
            return false;
        }

        SetLastError(0);
        const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (exStyle == 0 && GetLastError() != 0) {
            return false;
        }

        const LONG_PTR newStyle = enabled
            ? exStyle | WS_EX_TRANSPARENT
            : exStyle & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT);
        if (newStyle == exStyle) {
            return true;
        }

        SetLastError(0);
        const LONG_PTR previous =
            SetWindowLongPtrW(hwnd, GWL_EXSTYLE, newStyle);
        return previous != 0 || GetLastError() == 0;
    }

    SDL_Renderer* createRenderer()
    {
        SDL_Renderer* newRenderer = SDL_CreateRenderer(
            sdl_renderer::window,
            -1,
            SDL_RENDERER_ACCELERATED);
        if (!newRenderer) {
            newRenderer = SDL_CreateRenderer(
                sdl_renderer::window,
                -1,
                SDL_RENDERER_SOFTWARE);
        }
        if (newRenderer &&
            SDL_SetRenderDrawBlendMode(newRenderer, SDL_BLENDMODE_BLEND) != 0) {
            SDL_DestroyRenderer(newRenderer);
            return nullptr;
        }
        return newRenderer;
    }

    bool recoverRenderer()
    {
        if (!sdl_renderer::window) {
            return false;
        }

        const bool restoreImGui = imguiInitialized;
        if (restoreImGui) {
            ImGui_ImplSDLRenderer2_Shutdown();
            ImGui_ImplSDL2_Shutdown();
        }
        if (sdl_renderer::renderer) {
            SDL_DestroyRenderer(sdl_renderer::renderer);
        }

        sdl_renderer::renderer = createRenderer();
        if (!sdl_renderer::renderer) {
            imguiInitialized = false;
            return false;
        }

        if (restoreImGui) {
            const bool platformRecovered =
                ImGui_ImplSDL2_InitForSDLRenderer(
                    sdl_renderer::window,
                    sdl_renderer::renderer);
            const bool rendererRecovered =
                platformRecovered &&
                ImGui_ImplSDLRenderer2_Init(sdl_renderer::renderer);
            if (!rendererRecovered) {
                if (platformRecovered) {
                    ImGui_ImplSDL2_Shutdown();
                }
                SDL_DestroyRenderer(sdl_renderer::renderer);
                sdl_renderer::renderer = nullptr;
                imguiInitialized = false;
                return false;
            }
        }
        return true;
    }
}

// Initialize the waiting screen on the display the user is currently using.
bool sdl_renderer::initWaiting()
{
    configureVideoHints();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    int initialDisplayIndex = 0;
    POINT cursor{};
    if (GetCursorPos(&cursor)) {
        const int displayCount = SDL_GetNumVideoDisplays();
        for (int displayIndex = 0;
             displayIndex < displayCount;
             ++displayIndex) {
            SDL_Rect bounds{};
            if (SDL_GetDisplayBounds(displayIndex, &bounds) == 0 &&
                cursor.x >= bounds.x &&
                cursor.x < bounds.x + bounds.w &&
                cursor.y >= bounds.y &&
                cursor.y < bounds.y + bounds.h) {
                initialDisplayIndex = displayIndex;
                break;
            }
        }
    }

    SDL_Rect initialDisplay{};
    if (SDL_GetDisplayBounds(initialDisplayIndex, &initialDisplay) != 0) {
        initialDisplay.x = 0;
        initialDisplay.y = 0;
        initialDisplay.w = GetSystemMetrics(SM_CXSCREEN);
        initialDisplay.h = GetSystemMetrics(SM_CYSCREEN);
    }

    WIDTH = static_cast<uint32_t>(initialDisplay.w);
    HEIGHT = static_cast<uint32_t>(initialDisplay.h);
    WINDOW_W = WIDTH;
    WINDOW_H = HEIGHT;
    publishViewport(ContentViewport{
        0,
        0,
        initialDisplay.w,
        initialDisplay.h
    });

    window = SDL_CreateWindow(
        "CS2 ESP - Waiting",
        initialDisplay.x,
        initialDisplay.y,
        WIDTH, HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN
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

    if (!configureOverlayWindow(overlayHwnd)) {
        SDL_DestroyWindow(window);
        window = nullptr;
        overlayHwnd = nullptr;
        SDL_Quit();
        return false;
    }
    if (!setClickThrough(overlayHwnd, !menuVisible)) {
        SDL_DestroyWindow(window);
        window = nullptr;
        overlayHwnd = nullptr;
        SDL_Quit();
        return false;
    }

    renderer = createRenderer();

    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    ShowWindow(overlayHwnd, SW_SHOWNOACTIVATE);
    return true;
}

bool sdl_renderer::init(
    const wchar_t* targetWindowName,
    DWORD targetProcessId)
{
    configureVideoHints();

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return false;
    }

    gameHwnd = findGameWindow(targetProcessId, targetWindowName);

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
    appliedViewportMode = menu::viewportMode;
    publishViewport(
        appliedViewportMode == 0
            ? detectContentViewport(initialGeometry.gameClient)
            : viewportForMode(
                appliedViewportMode,
                rectWidth(initialGeometry.gameClient),
                rectHeight(initialGeometry.gameClient)));

    window = SDL_CreateWindow(
        "Overlay",
        initialGeometry.gameClient.left,
        initialGeometry.gameClient.top,
        WIDTH, HEIGHT,
        SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP |
        SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_HIDDEN
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

    if (!configureOverlayWindow(overlayHwnd)) {
        SDL_DestroyWindow(window);
        window = nullptr;
        overlayHwnd = nullptr;
        SDL_Quit();
        return false;
    }
    if (!setClickThrough(overlayHwnd, !menuVisible)) {
        SDL_DestroyWindow(window);
        window = nullptr;
        overlayHwnd = nullptr;
        SDL_Quit();
        return false;
    }

    renderer = createRenderer();

    if (!renderer) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
    }

    ShowWindow(overlayHwnd, SW_SHOWNOACTIVATE);
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
    currentViewport = {};
    pendingViewport = {};
    pendingViewportSamples = 0;
    appliedViewportMode = -1;
    VIEWPORT_X = 0;
    VIEWPORT_Y = 0;
    VIEWPORT_W = 0;
    VIEWPORT_H = 0;
    gameVisible = true;
    SDL_Quit();
}

bool sdl_renderer::beginFrame()
{
    if (!renderer && !recoverRenderer()) {
        running = false;
        return false;
    }

    // This reserved near-black color is the only transparent color. Pure black
    // remains available for ImGui backgrounds, outlines, text, and future UI.
    const auto clearFrame = []() {
        return SDL_SetRenderDrawColor(
                sdl_renderer::renderer,
                TRANSPARENCY_KEY_R,
                TRANSPARENCY_KEY_G,
                TRANSPARENCY_KEY_B,
                255) == 0 &&
            SDL_RenderClear(sdl_renderer::renderer) == 0;
    };

    if (!clearFrame() &&
        (!recoverRenderer() || !clearFrame())) {
        running = false;
        return false;
    }
    return true;
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
        } else if (
            event.type == SDL_RENDER_DEVICE_RESET ||
            event.type == SDL_RENDER_TARGETS_RESET) {
            ImGui_ImplSDLRenderer2_DestroyDeviceObjects();
            ImGui_ImplSDLRenderer2_CreateDeviceObjects();
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
            if (!setClickThrough(overlayHwnd, !menuVisible)) {
                running = false;
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

    // Poll often enough that dragging the game between monitors does not leave
    // the overlay visibly behind it.
    static DWORD lastUpdate = 0;
    DWORD now = GetTickCount();
    if (now - lastUpdate < 50) return;
    lastUpdate = now;

    if (IsIconic(gameHwnd) || !IsWindowVisible(gameHwnd)) {
        gameVisible = false;
        ShowWindow(overlayHwnd, SW_HIDE);
        return;
    }
    if (!gameVisible) {
        ShowWindow(overlayHwnd, SW_SHOWNOACTIVATE);
    }
    gameVisible = true;

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
    // resolution would non-uniformly scale and visibly deform the ESP.
    if (!sameRect(geometry.gameClient, currentGeometry.gameClient) ||
        !sameRect(geometry.monitor, currentGeometry.monitor) ||
        geometry.monitorHandle != currentGeometry.monitorHandle) {
        if (!SetWindowPos(
            overlayHwnd,
            HWND_TOPMOST,
            x,
            y,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW
        )) {
            running = false;
            return;
        }
        currentGeometry = geometry;
        publishViewport(viewportForMode(
            menu::viewportMode,
            width,
            height));
        pendingViewport = {};
        pendingViewportSamples = 0;
    } else {
        // Reassert top-most state occasionally instead of doing it on every
        // geometry poll.
        static DWORD lastTopmostUpdate = 0;
        if (now - lastTopmostUpdate >= 1000) {
            lastTopmostUpdate = now;
            if (!SetWindowPos(
                    overlayHwnd,
                    HWND_TOPMOST,
                    0,
                    0,
                    0,
                    0,
                    SWP_NOMOVE | SWP_NOSIZE |
                        SWP_NOACTIVATE | SWP_SHOWWINDOW)) {
                running = false;
                return;
            }
        }
    }

    updateRenderDimensions();

    if (menu::viewportMode != appliedViewportMode) {
        appliedViewportMode = menu::viewportMode;
        publishViewport(viewportForMode(
            appliedViewportMode,
            width,
            height));
        pendingViewport = {};
        pendingViewportSamples = 0;
    }

    // Color-key overlays appear in desktop captures, so refresh black-bar
    // detection only while click-through mode is active. The initial viewport
    // is sampled before the overlay window is shown.
    static DWORD lastViewportCheck = 0;
    if (menu::viewportMode == 0 &&
        !menuVisible &&
        now - lastViewportCheck >= 1000) {
        lastViewportCheck = now;
        const ContentViewport detected =
            detectContentViewport(geometry.gameClient);
        if (sameViewport(detected, pendingViewport)) {
            ++pendingViewportSamples;
        } else {
            pendingViewport = detected;
            pendingViewportSamples = 1;
        }
        if (pendingViewportSamples >= 2 &&
            !sameViewport(detected, currentViewport)) {
            publishViewport(detected);
        }
    }

    applyImGuiDpiScale();
}

float sdl_renderer::getDpiScale()
{
    return currentDpiScale;
}

bool sdl_renderer::isGameVisible()
{
    return gameVisible;
}

uint32_t sdl_renderer::getDpiRevision()
{
    return dpiRevision;
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

bool sdl_renderer::initImGui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig fontConfig;
    fontConfig.SizePixels = BASE_FONT_SIZE;
    fontConfig.OversampleH = 1;
    fontConfig.OversampleV = 1;
    io.Fonts->AddFontDefaultVector(&fontConfig);

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = BASE_FONT_SIZE;
    style.FontScaleMain = 1.0f;
    style.FontScaleDpi = 1.0f;
    style.WindowRounding = 8.0f;
    style.FrameRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.Alpha = 0.95f;
    style.FramePadding = ImVec2(6.0f, 4.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
    style.TabBarBorderSize = 1.0f;

    baseImGuiStyle = style;
    baseImGuiStyleReady = true;
    applyImGuiDpiScale(true);

    const bool platformInitialized =
        ImGui_ImplSDL2_InitForSDLRenderer(window, renderer);
    const bool rendererInitialized =
        platformInitialized && ImGui_ImplSDLRenderer2_Init(renderer);
    if (!rendererInitialized) {
        if (platformInitialized) {
            ImGui_ImplSDL2_Shutdown();
        }
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        baseImGuiStyleReady = false;
        return false;
    }
    imguiInitialized = true;
    return true;
}

void sdl_renderer::shutdownImGui()
{
    if (!imguiInitialized) {
        if (ImGui::GetCurrentContext()) {
            ImGui::DestroyContext();
        }
        baseImGuiStyleReady = false;
        return;
    }
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    imguiInitialized = false;
    baseImGuiStyleReady = false;
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
