#include "sdl_renderer.h"
#include <dwmapi.h>
#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_sdlrenderer2.h"
#include "viewport_math.hpp"
#include "../diagnostics.hpp"
#include "../../features/menu.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <condition_variable>
#include <iterator>
#include <mutex>
#include <thread>
#include <vector>

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

    using ContentViewport = viewport_math::Viewport;

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
    bool mixedMonitorBlocked = false;
    bool overlayPositionFailed = false;
    std::atomic<bool> gameForeground{ false };
    std::atomic<bool> inputAllowed{ false };
    std::atomic<bool> gameDisconnected{ false };
    std::atomic<bool> acceleratedRenderer{ false };
    std::atomic<bool> vsyncEnabled{ false };
    std::atomic<bool> dpiAwarenessReliable{ false };
    std::atomic<bool> gameOnSingleMonitor{ true };
    std::atomic<int> targetRefreshRate{ 144 };
    std::atomic<uint64_t> rendererRevision{ 0 };
    RECT interactiveRect{};
    WNDPROC originalWindowProc = nullptr;

    struct ViewportDetectionRequest
    {
        RECT rect{};
        uint64_t generation = 0;
    };

    struct ViewportDetectionResult
    {
        RECT rect{};
        ContentViewport viewport{};
        uint64_t generation = 0;
    };

    std::mutex viewportWorkerMutex;
    std::condition_variable viewportWorkerCv;
    std::thread viewportWorker;
    bool viewportWorkerStopping = false;
    bool viewportRequestReady = false;
    bool viewportResultReady = false;
    ViewportDetectionRequest viewportRequest{};
    ViewportDetectionResult viewportResult{};
    uint64_t viewportGeneration = 0;
    DWORD lastViewportQueueTime = 0;

    template <typename Function>
    Function loadFunction(
        HMODULE module,
        const char* functionName)
    {
        static_assert(sizeof(Function) == sizeof(FARPROC));
        return module
            ? std::bit_cast<Function>(
                GetProcAddress(module, functionName))
            : nullptr;
    }

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
        return viewport_math::same(lhs, rhs);
    }

    void publishViewport(const ContentViewport& viewport)
    {
        currentViewport = viewport;
        VIEWPORT_X = viewport.x;
        VIEWPORT_Y = viewport.y;
        VIEWPORT_W = static_cast<uint32_t>(std::max(0, viewport.width));
        VIEWPORT_H = static_cast<uint32_t>(std::max(0, viewport.height));
    }

    struct AsyncKeyTracker
    {
        int virtualKey = 0;
        bool wasDown = false;
    };

    bool consumeAsyncKeyPress(int virtualKey, AsyncKeyTracker& tracker)
    {
        if (virtualKey <= 0 || virtualKey > 0xFF) {
            tracker = {};
            return false;
        }

        if (tracker.virtualKey != virtualKey) {
            tracker.virtualKey = virtualKey;
            tracker.wasDown = false;
        }

        const SHORT state = GetAsyncKeyState(virtualKey);
        const bool isDown = (state & 0x8000) != 0;
        const bool pressedSinceLastPoll = (state & 0x0001) != 0;
        const bool pressed =
            pressedSinceLastPoll || (isDown && !tracker.wasDown);
        tracker.wasDown = isDown;
        return pressed;
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

    struct CapturedClientSample
    {
        int width = 0;
        int height = 0;
        std::vector<uint32_t> pixels;
    };

    bool captureClientSample(
        const RECT& clientScreenRect,
        CapturedClientSample& sample)
    {
        constexpr int MAX_SAMPLE_WIDTH = 320;
        constexpr int MAX_SAMPLE_HEIGHT = 180;

        const int sourceWidth = rectWidth(clientScreenRect);
        const int sourceHeight = rectHeight(clientScreenRect);
        if (sourceWidth <= 0 || sourceHeight <= 0) {
            return false;
        }

        const float scale = std::min(
            1.0f,
            std::min(
                static_cast<float>(MAX_SAMPLE_WIDTH) / sourceWidth,
                static_cast<float>(MAX_SAMPLE_HEIGHT) / sourceHeight));
        const int sampleWidth = std::max(
            1,
            static_cast<int>(std::lround(sourceWidth * scale)));
        const int sampleHeight = std::max(
            1,
            static_cast<int>(std::lround(sourceHeight * scale)));

        HDC desktop = GetDC(nullptr);
        if (!desktop) {
            return false;
        }

        HDC memory = CreateCompatibleDC(desktop);
        if (!memory) {
            ReleaseDC(nullptr, desktop);
            return false;
        }

        BITMAPINFO bitmapInfo{};
        bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmapInfo.bmiHeader.biWidth = sampleWidth;
        bitmapInfo.bmiHeader.biHeight = -sampleHeight;
        bitmapInfo.bmiHeader.biPlanes = 1;
        bitmapInfo.bmiHeader.biBitCount = 32;
        bitmapInfo.bmiHeader.biCompression = BI_RGB;

        void* bitmapBits = nullptr;
        HBITMAP bitmap = CreateDIBSection(
            memory,
            &bitmapInfo,
            DIB_RGB_COLORS,
            &bitmapBits,
            nullptr,
            0);
        if (!bitmap || !bitmapBits) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            DeleteDC(memory);
            ReleaseDC(nullptr, desktop);
            return false;
        }

        HGDIOBJ previousBitmap = SelectObject(memory, bitmap);
        SetStretchBltMode(memory, COLORONCOLOR);
        const BOOL captured = StretchBlt(
            memory,
            0,
            0,
            sampleWidth,
            sampleHeight,
            desktop,
            clientScreenRect.left,
            clientScreenRect.top,
            sourceWidth,
            sourceHeight,
            SRCCOPY);

        if (previousBitmap) {
            SelectObject(memory, previousBitmap);
        }

        if (captured) {
            sample.width = sampleWidth;
            sample.height = sampleHeight;
            sample.pixels.resize(
                static_cast<size_t>(sampleWidth) *
                static_cast<size_t>(sampleHeight));
            std::copy_n(
                static_cast<const uint32_t*>(bitmapBits),
                sample.pixels.size(),
                sample.pixels.begin());
        }

        DeleteObject(bitmap);
        DeleteDC(memory);
        ReleaseDC(nullptr, desktop);
        return captured != FALSE;
    }

    bool isNearBlack(uint32_t pixel)
    {
        // A top-down 32-bit BI_RGB DIB stores pixels as B, G, R, X.
        const uint8_t blue = static_cast<uint8_t>(pixel);
        const uint8_t green = static_cast<uint8_t>(pixel >> 8);
        const uint8_t red = static_cast<uint8_t>(pixel >> 16);
        return red <= 12 && green <= 12 && blue <= 12;
    }

    float blackRatioForColumn(
        const CapturedClientSample& sample,
        int x)
    {
        constexpr int SAMPLE_COUNT = 17;
        int black = 0;
        for (int i = 1; i <= SAMPLE_COUNT; ++i) {
            const int y =
                (sample.height * i) / (SAMPLE_COUNT + 1);
            const size_t index =
                static_cast<size_t>(y) * sample.width + x;
            black += isNearBlack(sample.pixels[index]) ? 1 : 0;
        }
        return static_cast<float>(black) /
            static_cast<float>(SAMPLE_COUNT);
    }

    float blackRatioForRow(
        const CapturedClientSample& sample,
        int y)
    {
        constexpr int SAMPLE_COUNT = 17;
        int black = 0;
        for (int i = 1; i <= SAMPLE_COUNT; ++i) {
            const int x =
                (sample.width * i) / (SAMPLE_COUNT + 1);
            const size_t index =
                static_cast<size_t>(y) * sample.width + x;
            black += isNearBlack(sample.pixels[index]) ? 1 : 0;
        }
        return static_cast<float>(black) /
            static_cast<float>(SAMPLE_COUNT);
    }

    ContentViewport detectContentViewport(const RECT& clientScreenRect)
    {
        const int width = rectWidth(clientScreenRect);
        const int height = rectHeight(clientScreenRect);
        ContentViewport result{ 0, 0, width, height };
        if (width <= 0 || height <= 0) {
            return result;
        }

        CapturedClientSample sample{};
        if (!captureClientSample(clientScreenRect, sample)) {
            return result;
        }

        const int maxHorizontalBorder = sample.width * 3 / 10;
        const int maxVerticalBorder = sample.height * 3 / 10;
        int left = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;

        while (left < maxHorizontalBorder &&
            blackRatioForColumn(
                sample,
                left) >= 0.88f) {
            ++left;
        }
        while (right < maxHorizontalBorder &&
            blackRatioForColumn(
                sample,
                sample.width - 1 - right) >= 0.88f) {
            ++right;
        }
        while (top < maxVerticalBorder &&
            blackRatioForRow(
                sample,
                top) >= 0.88f) {
            ++top;
        }
        while (bottom < maxVerticalBorder &&
            blackRatioForRow(
                sample,
                sample.height - 1 - bottom) >= 0.88f) {
            ++bottom;
        }

        const int horizontalTolerance =
            std::max(2, sample.width / 100);
        const int verticalTolerance =
            std::max(2, sample.height / 100);
        const int minimumHorizontalBorder =
            std::max(2, sample.width / 100);
        const int minimumVerticalBorder =
            std::max(2, sample.height / 100);
        const int horizontalProbe =
            std::min(
                sample.width - 1,
                left + std::max(2, sample.width / 200));
        const int verticalProbe =
            std::min(
                sample.height - 1,
                top + std::max(2, sample.height / 200));

        const bool hasSideBars =
            left >= minimumHorizontalBorder &&
            right >= minimumHorizontalBorder &&
            std::abs(left - right) <= horizontalTolerance &&
            blackRatioForColumn(
                sample,
                horizontalProbe) < 0.65f;
        const bool hasTopBars =
            top >= minimumVerticalBorder &&
            bottom >= minimumVerticalBorder &&
            std::abs(top - bottom) <= verticalTolerance &&
            blackRatioForRow(
                sample,
                verticalProbe) < 0.65f;

        // A real game viewport normally letterboxes on only one axis. If both
        // candidates match, retain the stronger normalized pair.
        if (hasSideBars &&
            (!hasTopBars ||
             static_cast<float>(left + right) / sample.width >=
                 static_cast<float>(top + bottom) / sample.height)) {
            const int symmetricSampleBorder = (left + right) / 2;
            const int symmetricBorder = static_cast<int>(std::lround(
                static_cast<float>(symmetricSampleBorder) * width /
                sample.width));
            result.x = symmetricBorder;
            result.width = width - symmetricBorder * 2;
        } else if (hasTopBars) {
            const int symmetricSampleBorder = (top + bottom) / 2;
            const int symmetricBorder = static_cast<int>(std::lround(
                static_cast<float>(symmetricSampleBorder) * height /
                sample.height));
            result.y = symmetricBorder;
            result.height = height - symmetricBorder * 2;
        }

        if (result.width < width / 2 || result.height < height / 2) {
            return ContentViewport{ 0, 0, width, height };
        }
        return result;
    }

    void queueViewportDetection(
        const RECT& rect,
        uint64_t generation)
    {
        {
            std::lock_guard<std::mutex> lock(viewportWorkerMutex);
            if (viewportWorkerStopping) {
                return;
            }
            viewportRequest = ViewportDetectionRequest{ rect, generation };
            viewportRequestReady = true;
        }
        viewportWorkerCv.notify_one();
    }

    void startViewportDetectionWorker()
    {
        std::lock_guard<std::mutex> lock(viewportWorkerMutex);
        if (viewportWorker.joinable()) {
            return;
        }
        viewportWorkerStopping = false;
        viewportRequestReady = false;
        viewportResultReady = false;
        viewportWorker = std::thread([]() {
            while (true) {
                ViewportDetectionRequest request{};
                {
                    std::unique_lock<std::mutex> lock(viewportWorkerMutex);
                    viewportWorkerCv.wait(lock, []() {
                        return viewportWorkerStopping ||
                            viewportRequestReady;
                    });
                    if (viewportWorkerStopping) {
                        return;
                    }
                    request = viewportRequest;
                    viewportRequestReady = false;
                }

                const ContentViewport detected =
                    detectContentViewport(request.rect);

                {
                    std::lock_guard<std::mutex> lock(viewportWorkerMutex);
                    if (!viewportWorkerStopping) {
                        viewportResult = ViewportDetectionResult{
                            request.rect,
                            detected,
                            request.generation
                        };
                        viewportResultReady = true;
                    }
                }
            }
        });
    }

    void stopViewportDetectionWorker()
    {
        {
            std::lock_guard<std::mutex> lock(viewportWorkerMutex);
            viewportWorkerStopping = true;
            viewportRequestReady = false;
        }
        viewportWorkerCv.notify_all();
        if (viewportWorker.joinable()) {
            viewportWorker.join();
        }

        std::lock_guard<std::mutex> lock(viewportWorkerMutex);
        viewportResultReady = false;
        viewportWorkerStopping = false;
    }

    bool consumeViewportDetection(ViewportDetectionResult& result)
    {
        std::lock_guard<std::mutex> lock(viewportWorkerMutex);
        if (!viewportResultReady) {
            return false;
        }
        result = viewportResult;
        viewportResultReady = false;
        return true;
    }

    void resetAutoViewportDetection(const RECT& rect)
    {
        ++viewportGeneration;
        pendingViewport = currentViewport;
        pendingViewportSamples = 1;
        lastViewportQueueTime = GetTickCount();
        queueViewportDetection(rect, viewportGeneration);
    }

    ContentViewport viewportForMode(
        int mode,
        int clientWidth,
        int clientHeight)
    {
        return viewport_math::forMode(mode, clientWidth, clientHeight);
    }

    // Keep SDL and Win32 in the same physical-pixel coordinate system. This is
    // especially important when the game is on a monitor whose scale differs
    // from the primary monitor.
    bool configureVideoHints()
    {
        using SetProcessDpiAwarenessContextFn =
            BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        static SetProcessDpiAwarenessContextFn setProcessDpiAwarenessContext =
            []() {
                return loadFunction<
                    SetProcessDpiAwarenessContextFn>(
                        GetModuleHandleW(L"user32.dll"),
                        "SetProcessDpiAwarenessContext");
            }();

        bool awarenessConfigured = false;
        if (setProcessDpiAwarenessContext) {
            // Failure is expected if the host has already selected an equal
            // or stronger awareness mode; SDL's hint below remains in place.
            awarenessConfigured =
                setProcessDpiAwarenessContext(
                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != FALSE;
        } else {
            using SetProcessDpiAwareFn = BOOL(WINAPI*)();
            const auto setProcessDpiAware =
                loadFunction<SetProcessDpiAwareFn>(
                    GetModuleHandleW(L"user32.dll"),
                    "SetProcessDPIAware");
            if (setProcessDpiAware) {
                awarenessConfigured = setProcessDpiAware() != FALSE;
            }
        }

        using GetThreadDpiAwarenessContextFn =
            DPI_AWARENESS_CONTEXT(WINAPI*)();
        using AreDpiAwarenessContextsEqualFn =
            BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT, DPI_AWARENESS_CONTEXT);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto getThreadDpiAwarenessContext =
            loadFunction<GetThreadDpiAwarenessContextFn>(
                user32,
                "GetThreadDpiAwarenessContext");
        const auto areDpiAwarenessContextsEqual =
            loadFunction<AreDpiAwarenessContextsEqualFn>(
                user32,
                "AreDpiAwarenessContextsEqual");
        if (getThreadDpiAwarenessContext &&
            areDpiAwarenessContextsEqual) {
            const DPI_AWARENESS_CONTEXT actual =
                getThreadDpiAwarenessContext();
            awarenessConfigured =
                areDpiAwarenessContextsEqual(
                    actual,
                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) ||
                areDpiAwarenessContextsEqual(
                    actual,
                    DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        }

        SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2");
        SDL_SetHint(SDL_HINT_WINDOWS_DPI_SCALING, "0");
        SDL_SetHint(SDL_HINT_RENDER_VSYNC, "0");
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d");
        dpiAwarenessReliable.store(
            awarenessConfigured,
            std::memory_order_relaxed);
        return awarenessConfigured;
    }

    float getWindowDpiScale(HWND targetWindow)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static GetDpiForWindowFn getDpiForWindowFn = []() {
            return loadFunction<GetDpiForWindowFn>(
                GetModuleHandleW(L"user32.dll"),
                "GetDpiForWindow");
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
                return loadFunction<GetDpiForMonitorFn>(
                    LoadLibraryW(L"shcore.dll"),
                    "GetDpiForMonitor");
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
                return loadFunction<GetDpiForSystemFn>(
                    GetModuleHandleW(L"user32.dll"),
                    "GetDpiForSystem");
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

    bool clientFitsMonitor(const GameDisplayGeometry& geometry)
    {
        return geometry.gameClient.left >= geometry.monitor.left &&
            geometry.gameClient.top >= geometry.monitor.top &&
            geometry.gameClient.right <= geometry.monitor.right &&
            geometry.gameClient.bottom <= geometry.monitor.bottom;
    }

    int refreshRateForMonitor(HMONITOR monitor)
    {
        MONITORINFOEXW monitorInfo{};
        monitorInfo.cbSize = sizeof(monitorInfo);
        if (!monitor ||
            !GetMonitorInfoW(
                monitor,
                reinterpret_cast<MONITORINFO*>(&monitorInfo))) {
            return 144;
        }

        DEVMODEW displayMode{};
        displayMode.dmSize = sizeof(displayMode);
        if (!EnumDisplaySettingsW(
                monitorInfo.szDevice,
                ENUM_CURRENT_SETTINGS,
                &displayMode) ||
            displayMode.dmDisplayFrequency <= 1) {
            return 144;
        }
        return std::clamp(
            static_cast<int>(displayMode.dmDisplayFrequency),
            60,
            1000);
    }

    bool isGameClientForeground()
    {
        return sdl_renderer::gameHwnd &&
            GetForegroundWindow() == sdl_renderer::gameHwnd;
    }

    bool foregroundBelongsToGameUi()
    {
        const HWND foreground = GetForegroundWindow();
        if (!foreground) {
            return false;
        }
        if (foreground == sdl_renderer::gameHwnd ||
            foreground == sdl_renderer::overlayHwnd) {
            return true;
        }

        DWORD foregroundProcessId = 0;
        DWORD gameProcessId = 0;
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
        GetWindowThreadProcessId(
            sdl_renderer::gameHwnd,
            &gameProcessId);
        return gameProcessId != 0 &&
            foregroundProcessId == gameProcessId;
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

    LRESULT CALLBACK overlayWindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        if (message == WM_NCHITTEST) {
            if (!sdl_renderer::menuVisible) {
                return HTTRANSPARENT;
            }

            POINT cursor{};
            if (GetCursorPos(&cursor) &&
                ScreenToClient(hwnd, &cursor) &&
                PtInRect(&interactiveRect, cursor)) {
                return HTCLIENT;
            }
            return HTTRANSPARENT;
        }

        return originalWindowProc
            ? CallWindowProcW(
                originalWindowProc,
                hwnd,
                message,
                wParam,
                lParam)
            : DefWindowProcW(hwnd, message, wParam, lParam);
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

        SetLastError(0);
        const LONG_PTR previousWindowProc = SetWindowLongPtrW(
            hwnd,
            GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(overlayWindowProc));
        if (previousWindowProc == 0 && GetLastError() != 0) {
            return false;
        }
        originalWindowProc =
            reinterpret_cast<WNDPROC>(previousWindowProc);

        if (!SetLayeredWindowAttributes(
                hwnd,
                TRANSPARENCY_COLOR_KEY,
                0,
                LWA_COLORKEY)) {
            return false;
        }

        MARGINS margins = { -1, -1, -1, -1 };
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

    void discardOverlayWindow()
    {
        if (sdl_renderer::window) {
            SDL_DestroyWindow(sdl_renderer::window);
            sdl_renderer::window = nullptr;
        }
        sdl_renderer::overlayHwnd = nullptr;
        originalWindowProc = nullptr;
    }

    SDL_Renderer* createRenderer()
    {
        SDL_Renderer* newRenderer = SDL_CreateRenderer(
            sdl_renderer::window,
            -1,
            SDL_RENDERER_ACCELERATED |
                SDL_RENDERER_PRESENTVSYNC);
        if (!newRenderer) {
            newRenderer = SDL_CreateRenderer(
                sdl_renderer::window,
                -1,
                SDL_RENDERER_ACCELERATED);
        }
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
        SDL_RendererInfo rendererInfo{};
        const bool accelerated =
            newRenderer &&
            SDL_GetRendererInfo(newRenderer, &rendererInfo) == 0 &&
            (rendererInfo.flags & SDL_RENDERER_ACCELERATED) != 0;
        const bool synchronized =
            newRenderer &&
            (rendererInfo.flags & SDL_RENDERER_PRESENTVSYNC) != 0;
        acceleratedRenderer.store(
            accelerated,
            std::memory_order_relaxed);
        vsyncEnabled.store(
            synchronized,
            std::memory_order_relaxed);
        if (newRenderer) {
            rendererRevision.fetch_add(1, std::memory_order_relaxed);
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
    if (!configureVideoHints()) {
        return false;
    }

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
        discardOverlayWindow();
        SDL_Quit();
        return false;
    }
    if (!setClickThrough(overlayHwnd, !menuVisible)) {
        discardOverlayWindow();
        SDL_Quit();
        return false;
    }

    renderer = createRenderer();

    if (!renderer) {
        discardOverlayWindow();
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
    if (!configureVideoHints()) {
        return false;
    }

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
    gameDisconnected.store(false, std::memory_order_relaxed);
    gameOnSingleMonitor.store(
        clientFitsMonitor(initialGeometry),
        std::memory_order_relaxed);
    targetRefreshRate.store(
        refreshRateForMonitor(initialGeometry.monitorHandle),
        std::memory_order_relaxed);
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
    ++viewportGeneration;
    pendingViewport = currentViewport;
    pendingViewportSamples = 1;
    lastViewportQueueTime = GetTickCount();

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
        discardOverlayWindow();
        SDL_Quit();
        return false;
    }
    if (!setClickThrough(overlayHwnd, !menuVisible)) {
        discardOverlayWindow();
        SDL_Quit();
        return false;
    }

    renderer = createRenderer();

    if (!renderer) {
        discardOverlayWindow();
        SDL_Quit();
        return false;
    }

    startViewportDetectionWorker();
    ShowWindow(overlayHwnd, SW_SHOWNOACTIVATE);
    updateWindowPosition();
    updateRenderDimensions();

    return true;
}

void sdl_renderer::destroy()
{
    stopViewportDetectionWorker();
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
    lastViewportQueueTime = 0;
    ++viewportGeneration;
    VIEWPORT_X = 0;
    VIEWPORT_Y = 0;
    VIEWPORT_W = 0;
    VIEWPORT_H = 0;
    gameVisible = true;
    mixedMonitorBlocked = false;
    overlayPositionFailed = false;
    gameForeground.store(false, std::memory_order_relaxed);
    inputAllowed.store(false, std::memory_order_relaxed);
    acceleratedRenderer.store(false, std::memory_order_relaxed);
    vsyncEnabled.store(false, std::memory_order_relaxed);
    gameOnSingleMonitor.store(true, std::memory_order_relaxed);
    interactiveRect = {};
    originalWindowProc = nullptr;
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
    static AsyncKeyTracker exitKeyTracker{};
    static AsyncKeyTracker menuKeyTracker{};

    bool rendererResetRequested = false;
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT) {
            running = false;
        } else if (
            event.type == SDL_RENDER_DEVICE_RESET ||
            event.type == SDL_RENDER_TARGETS_RESET) {
            rendererResetRequested = true;
        }
    }
    if (rendererResetRequested && !recoverRenderer()) {
        diagnostics::log(
            L"SDL render device recovery failed.");
        running = false;
        return;
    }

    // Skip global hotkeys while binding, and until every configured key has
    // been released after a bind. This prevents the newly assigned key from
    // immediately hiding the menu, exiting, aiming, or firing.
    if (menu::isBindingKey ||
        menu::suppressHotkeysUntilRelease) {
        exitKeyTracker = {};
        menuKeyTracker = {};
        if (!menu::isBindingKey &&
            menu::ConfiguredHotkeysReleased()) {
            menu::suppressHotkeysUntilRelease = false;
            menu::publishRuntimeConfig();
        }
        return;
    }

    const bool gameOrOverlayForeground =
        gameHwnd
            ? foregroundBelongsToGameUi()
            : GetForegroundWindow() == overlayHwnd;

    // Always sample the keys so GetAsyncKeyState's low-order latch cannot
    // replay a press made in another application when CS2 regains focus.
    const bool exitPressed =
        consumeAsyncKeyPress(menu::exitKey, exitKeyTracker);
    const bool menuPressed =
        consumeAsyncKeyPress(menu::menuToggleKey, menuKeyTracker);

    // Check exit key (configurable, default: F9).
    if (gameOrOverlayForeground && exitPressed) {
        running = false;
        return;
    }

    // The low-order GetAsyncKeyState bit records a short press that happened
    // between frames. This keeps F4 responsive even if a frame is delayed.
    if (gameOrOverlayForeground && gameHwnd && menuPressed) {
        menuVisible = !menuVisible;
        if (!setClickThrough(overlayHwnd, !menuVisible)) {
            menuVisible = !menuVisible;
            inputAllowed.store(false, std::memory_order_relaxed);
            diagnostics::log(
                L"Unable to change overlay click-through state; "
                L"the previous menu state was restored.");
            return;
        }

        // Clicking the menu can activate the overlay HWND. When hiding it,
        // return focus to CS2 when Windows permits that transition. Injection
        // stays blocked unless the game client itself is confirmed foreground.
        if (!menuVisible &&
            GetForegroundWindow() == overlayHwnd &&
            IsWindow(gameHwnd)) {
            SetForegroundWindow(gameHwnd);
        }
        inputAllowed.store(
            !menuVisible &&
                isGameClientForeground() &&
                !menu::isBindingKey &&
                !menu::suppressHotkeysUntilRelease,
            std::memory_order_relaxed);
    }
}

void sdl_renderer::updateWindowPosition()
{
    if (!gameHwnd || !overlayHwnd) return;

    if (!IsWindow(gameHwnd)) {
        gameDisconnected.store(true, std::memory_order_relaxed);
        gameForeground.store(false, std::memory_order_relaxed);
        inputAllowed.store(false, std::memory_order_relaxed);
        ShowWindow(overlayHwnd, SW_HIDE);
        return;
    }

    // Poll often enough that dragging the game between monitors does not leave
    // the overlay visibly behind it.
    static DWORD lastUpdate = 0;
    DWORD now = GetTickCount();
    if (now - lastUpdate < 50) return;
    lastUpdate = now;

    const bool uiForeground = foregroundBelongsToGameUi();
    const bool gameClientForeground = isGameClientForeground();
    gameForeground.store(
        gameClientForeground,
        std::memory_order_relaxed);
    inputAllowed.store(
        gameClientForeground &&
            !menuVisible &&
            !menu::isBindingKey &&
            !menu::suppressHotkeysUntilRelease,
        std::memory_order_relaxed);

    if (IsIconic(gameHwnd) ||
        !IsWindowVisible(gameHwnd) ||
        !uiForeground) {
        gameVisible = false;
        ShowWindow(overlayHwnd, SW_HIDE);
        return;
    }
    GameDisplayGeometry geometry{};
    if (!getGameDisplayGeometry(gameHwnd, geometry)) {
        return;
    }

    const bool fitsOneMonitor = clientFitsMonitor(geometry);
    gameOnSingleMonitor.store(
        fitsOneMonitor,
        std::memory_order_relaxed);
    if (!fitsOneMonitor) {
        if (!mixedMonitorBlocked) {
            diagnostics::log(
                L"Overlay paused: the CS2 client spans multiple monitors. "
                L"Move it fully onto one monitor to preserve pixel mapping.");
            mixedMonitorBlocked = true;
        }
        gameVisible = false;
        inputAllowed.store(false, std::memory_order_relaxed);
        ShowWindow(overlayHwnd, SW_HIDE);
        return;
    }
    if (mixedMonitorBlocked) {
        diagnostics::log(
            L"Overlay resumed: the CS2 client is fully contained by one monitor.");
        mixedMonitorBlocked = false;
    }
    if (!gameVisible) {
        ShowWindow(overlayHwnd, SW_SHOWNOACTIVATE);
    }
    gameVisible = true;

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
        const int previousWidth =
            rectWidth(currentGeometry.gameClient);
        const int previousHeight =
            rectHeight(currentGeometry.gameClient);
        if (!SetWindowPos(
            overlayHwnd,
            HWND_TOPMOST,
            x,
            y,
            width,
            height,
            SWP_NOACTIVATE | SWP_SHOWWINDOW
        )) {
            if (!overlayPositionFailed) {
                diagnostics::log(
                    L"Overlay positioning failed; hiding it and retrying.");
            }
            overlayPositionFailed = true;
            gameVisible = false;
            inputAllowed.store(false, std::memory_order_relaxed);
            ShowWindow(overlayHwnd, SW_HIDE);
            return;
        }
        if (overlayPositionFailed) {
            diagnostics::log(
                L"Overlay positioning recovered.");
            overlayPositionFailed = false;
        }
        currentGeometry = geometry;
        targetRefreshRate.store(
            refreshRateForMonitor(geometry.monitorHandle),
            std::memory_order_relaxed);
        if (menu::viewportMode == 0) {
            publishViewport(viewport_math::remap(
                currentViewport,
                previousWidth,
                previousHeight,
                width,
                height));
        } else {
            publishViewport(viewportForMode(
                menu::viewportMode,
                width,
                height));
        }
        WIDTH = static_cast<uint32_t>(width);
        HEIGHT = static_cast<uint32_t>(height);
        WINDOW_W = WIDTH;
        WINDOW_H = HEIGHT;
        if (menu::viewportMode == 0) {
            resetAutoViewportDetection(geometry.gameClient);
        } else {
            pendingViewport = {};
            pendingViewportSamples = 0;
        }
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
                diagnostics::log(
                    L"Unable to reassert overlay top-most state; will retry.");
                return;
            }
        }
    }

    updateRenderDimensions();

    if (menu::viewportMode != appliedViewportMode) {
        appliedViewportMode = menu::viewportMode;
        if (appliedViewportMode == 0) {
            resetAutoViewportDetection(geometry.gameClient);
        } else {
            publishViewport(viewportForMode(
                appliedViewportMode,
                width,
                height));
            pendingViewport = {};
            pendingViewportSamples = 0;
        }
    }

    // A low-cost desktop sample runs on a worker. Periodic confirmation catches
    // loading-screen false negatives and internal resolution/aspect changes
    // that do not resize the game HWND, without blocking the render thread.
    if (menu::viewportMode == 0 &&
        !menuVisible &&
        now - lastViewportQueueTime >= 2000) {
        lastViewportQueueTime = now;
        queueViewportDetection(
            geometry.gameClient,
            viewportGeneration);
    }

    ViewportDetectionResult detection{};
    if (menu::viewportMode == 0 &&
        consumeViewportDetection(detection) &&
        detection.generation == viewportGeneration &&
        sameRect(detection.rect, geometry.gameClient)) {
        if (sameViewport(detection.viewport, pendingViewport)) {
            ++pendingViewportSamples;
        } else {
            pendingViewport = detection.viewport;
            pendingViewportSamples = 1;
            // Confirm a changed viewport immediately instead of leaving a
            // stretched/stale mapping visible until the normal 2-second probe.
            lastViewportQueueTime = now;
            queueViewportDetection(
                geometry.gameClient,
                viewportGeneration);
        }
        if (pendingViewportSamples >= 2) {
            if (!sameViewport(detection.viewport, currentViewport)) {
                publishViewport(detection.viewport);
            }
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

bool sdl_renderer::isGameForeground()
{
    return gameForeground.load(std::memory_order_relaxed);
}

bool sdl_renderer::isInputAllowed()
{
    return inputAllowed.load(std::memory_order_relaxed) &&
        isGameClientForeground();
}

bool sdl_renderer::isGameDisconnected()
{
    return gameDisconnected.load(std::memory_order_relaxed);
}

int sdl_renderer::getTargetRefreshRate()
{
    const int refreshRate =
        targetRefreshRate.load(std::memory_order_relaxed);
    if (!acceleratedRenderer.load(std::memory_order_relaxed)) {
        return std::min(refreshRate, 60);
    }
    return refreshRate;
}

bool sdl_renderer::isAcceleratedRenderer()
{
    return acceleratedRenderer.load(std::memory_order_relaxed);
}

bool sdl_renderer::isVsyncEnabled()
{
    return vsyncEnabled.load(std::memory_order_relaxed);
}

bool sdl_renderer::isDpiAwarenessReliable()
{
    return dpiAwarenessReliable.load(std::memory_order_relaxed);
}

bool sdl_renderer::isGameOnSingleMonitor()
{
    return gameOnSingleMonitor.load(std::memory_order_relaxed);
}

void sdl_renderer::setInteractiveRect(
    float x,
    float y,
    float width,
    float height)
{
    interactiveRect.left = static_cast<LONG>(std::floor(x));
    interactiveRect.top = static_cast<LONG>(std::floor(y));
    interactiveRect.right =
        static_cast<LONG>(std::ceil(x + std::max(0.0f, width)));
    interactiveRect.bottom =
        static_cast<LONG>(std::ceil(y + std::max(0.0f, height)));
}

uint32_t sdl_renderer::getDpiRevision()
{
    return dpiRevision;
}

uint64_t sdl_renderer::getRendererRevision()
{
    return rendererRevision.load(std::memory_order_relaxed);
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
    style.Colors[ImGuiCol_WindowBg] =
        ImVec4(0.045f, 0.060f, 0.085f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] =
        ImVec4(0.070f, 0.090f, 0.125f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] =
        ImVec4(0.060f, 0.078f, 0.108f, 1.0f);
    style.Colors[ImGuiCol_Border] =
        ImVec4(0.145f, 0.185f, 0.240f, 1.0f);
    style.Colors[ImGuiCol_Separator] =
        ImVec4(0.135f, 0.180f, 0.235f, 1.0f);
    style.Colors[ImGuiCol_Text] =
        ImVec4(0.920f, 0.945f, 0.980f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] =
        ImVec4(0.500f, 0.555f, 0.640f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] =
        ImVec4(0.095f, 0.125f, 0.170f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] =
        ImVec4(0.125f, 0.175f, 0.235f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] =
        ImVec4(0.145f, 0.215f, 0.300f, 1.0f);
    style.Colors[ImGuiCol_Button] =
        ImVec4(0.100f, 0.310f, 0.510f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] =
        ImVec4(0.120f, 0.410f, 0.670f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] =
        ImVec4(0.090f, 0.260f, 0.440f, 1.0f);
    style.Colors[ImGuiCol_Header] =
        ImVec4(0.090f, 0.255f, 0.410f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] =
        ImVec4(0.110f, 0.350f, 0.565f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] =
        ImVec4(0.100f, 0.300f, 0.490f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] =
        ImVec4(0.250f, 0.790f, 1.000f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] =
        ImVec4(0.250f, 0.730f, 0.980f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] =
        ImVec4(0.390f, 0.840f, 1.000f, 1.0f);
    style.Colors[ImGuiCol_Tab] =
        ImVec4(0.075f, 0.115f, 0.165f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] =
        ImVec4(0.110f, 0.350f, 0.565f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] =
        ImVec4(0.100f, 0.300f, 0.490f, 1.0f);
    for (ImVec4& color : style.Colors) {
        color.w = 1.0f;
    }
    style.FontSizeBase = BASE_FONT_SIZE;
    style.FontScaleMain = 1.0f;
    style.FontScaleDpi = 1.0f;
    style.WindowRounding = 12.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 7.0f;
    style.PopupRounding = 9.0f;
    style.ScrollbarRounding = 9.0f;
    style.GrabRounding = 7.0f;
    style.TabRounding = 7.0f;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    // Color-keyed layered windows cannot preserve true per-pixel translucency.
    // Keep the menu opaque so it does not blend against the reserved key and
    // leave a dark halo over the game.
    style.Alpha = 1.0f;
    style.WindowPadding = ImVec2(16.0f, 14.0f);
    style.FramePadding = ImVec2(9.0f, 6.0f);
    style.ItemSpacing = ImVec2(9.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 6.0f);
    style.ScrollbarSize = 13.0f;
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
