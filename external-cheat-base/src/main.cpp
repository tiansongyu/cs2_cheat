#include "features/esp.hpp"
#include "features/aimbot.hpp"
#include "core/renderer/sdl_renderer.h"
#include "features/menu.hpp"
#include "features/web_radar/public_relay_producer.hpp"
#include "features/web_radar/web_radar_service.hpp"
#include "features/web_radar/snapshot_recorder.hpp"
#include "features/local_radar/local_fixed_radar.hpp"
#include "core/game/web_radar_json.hpp"
#include "core/diagnostics.hpp"
#include "core/performance_metrics.hpp"
#include "core/runtime_timing.hpp"
#include <algorithm>
#include <array>
#include <bcrypt.h>
#include <thread>
#include <atomic>
#include <filesystem>
#include <iostream>
#include <locale>
#include <codecvt>
#include <chrono>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <Windows.h>
#include "imgui.h"

// #define SHOW_CONSOLE

uint32_t WIDTH;
uint32_t HEIGHT;
uint32_t WINDOW_W;
uint32_t WINDOW_H;
int32_t VIEWPORT_X;
int32_t VIEWPORT_Y;
uint32_t VIEWPORT_W;
uint32_t VIEWPORT_H;

// Render waiting screen with ImGui
void renderWaitingScreen(int dotCount)
{
    const float dpiScale = sdl_renderer::getDpiScale();
    const float margin = std::max(8.0f, 16.0f * dpiScale);
    const float windowWidth = std::min(
        480.0f * dpiScale,
        std::max(1.0f, static_cast<float>(WIDTH) - margin * 2.0f));
    const float windowHeight = std::min(
        270.0f * dpiScale,
        std::max(1.0f, static_cast<float>(HEIGHT) - margin * 2.0f));
    ImGui::SetNextWindowPos(
        ImVec2(
            WIDTH / 2.0f - windowWidth / 2.0f,
            HEIGHT / 2.0f - windowHeight / 2.0f
        ),
        ImGuiCond_Always
    );
    ImGui::SetNextWindowSize(
        ImVec2(windowWidth, windowHeight),
        ImGuiCond_Always
    );

    ImGui::Begin("Aegis // Connection", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    {
        const ImVec2 position = ImGui::GetWindowPos();
        const ImVec2 size = ImGui::GetWindowSize();
        sdl_renderer::setInteractiveRect(
            position.x,
            position.y,
            size.x,
            size.y);
    }

    char dots[4] = {0};
    for (int i = 0; i < dotCount; i++) dots[i] = '.';

    ImGui::TextColored(
        ImVec4(0.330f, 0.800f, 1.000f, 1.0f),
        "AEGIS");
    ImGui::SameLine();
    ImGui::TextColored(
        ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
        "CS2 OVERLAY");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0.0f, 12.0f * dpiScale));
    ImGui::TextColored(
        ImVec4(0.930f, 0.960f, 1.000f, 1.0f),
        "Waiting for Counter-Strike 2%s",
        dots);
    ImGui::TextColored(
        ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
        "The client and its active monitor will be detected automatically.");

    ImGui::Dummy(ImVec2(0.0f, 14.0f * dpiScale));
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(0.070f, 0.090f, 0.125f, 1.0f));
    ImGui::PushStyleColor(
        ImGuiCol_Border,
        ImVec4(0.145f, 0.185f, 0.240f, 1.0f));
    ImGui::BeginChild(
        "##WaitingHint",
        ImVec2(0.0f, 90.0f * dpiScale),
        true);
    ImGui::TextColored(
        ImVec4(0.930f, 0.650f, 0.260f, 1.0f),
        "DISPLAY MODE");
    ImGui::TextWrapped(
        "Use Fullscreen Windowed in CS2. The overlay will then map "
        "the game viewport to the correct monitor and aspect ratio.");
    ImGui::EndChild();
    ImGui::PopStyleColor(2);

    ImGui::Dummy(ImVec2(0.0f, 8.0f * dpiScale));
    ImGui::TextColored(
        ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
        "Checking every 3 seconds  |  Press F9 to exit");

    ImGui::End();
}

namespace
{
    class ScopedHandle
    {
    public:
        explicit ScopedHandle(HANDLE handle = nullptr)
            : handle_(handle)
        {
        }

        ~ScopedHandle()
        {
            if (handle_) {
                CloseHandle(handle_);
            }
        }

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        HANDLE get() const
        {
            return handle_;
        }

    private:
        HANDLE handle_ = nullptr;
    };

    bool hasArgument(
        int argc,
        char* argv[],
        const char* expected)
    {
        for (int index = 1; index < argc; ++index) {
            if (_stricmp(argv[index], expected) == 0) {
                return true;
            }
        }
        return false;
    }

    void showFatalError(const wchar_t* message)
    {
        diagnostics::log(message);
        MessageBoxW(
            nullptr,
            message,
            L"CS2 ESP",
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }

    std::string makeViewerToken()
    {
        static constexpr char alphabet[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789-_";
        static_assert(sizeof(alphabet) - 1 == 64);

        std::array<unsigned char, 32> entropy{};
        const NTSTATUS result = BCryptGenRandom(
            nullptr,
            entropy.data(),
            static_cast<ULONG>(entropy.size()),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG);
        if (result != 0) {
            throw std::runtime_error(
                "Windows could not generate a Web Radar access token");
        }

        std::string token(32, 'A');
        for (std::size_t index = 0; index < token.size(); ++index) {
            token[index] = alphabet[entropy[index] & 63U];
        }
        return token;
    }

    std::string webRadarDocumentRoot()
    {
        std::array<wchar_t, 32768> executablePath{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            executablePath.data(),
            static_cast<DWORD>(executablePath.size()));
        if (length == 0 || length >= executablePath.size()) {
            return "web-radar/dist";
        }
        return (
            std::filesystem::path(
                std::wstring_view(executablePath.data(), length))
                .parent_path() /
            L"web-radar" /
            L"dist").string();
    }

    class WebRadarRuntime final
    {
    public:
        WebRadarRuntime()
            : documentRoot_(webRadarDocumentRoot())
        {
        }

        ~WebRadarRuntime()
        {
            stop();
        }

        void sync(const menu::RuntimeConfig& config)
        {
            const Settings desired{
                config.webRadarEnabled,
                config.webRadarLanAccess,
                config.webRadarPort
            };
            if (settings_ && *settings_ == desired) {
                publishUiStatus();
                return;
            }

            const bool rotateToken =
                desired.enabled &&
                (!settings_ || !settings_->enabled);
            settings_ = desired;
            detachAndStop();
            lastError_.clear();

            if (!desired.enabled) {
                publishUiStatus(true);
                return;
            }

            try {
                if (rotateToken || token_.empty()) {
                    token_ = makeViewerToken();
                }
                web_radar::WebRadarConfig serviceConfig;
                serviceConfig.bindAddress = desired.lanAccess
                    ? "0.0.0.0"
                    : "127.0.0.1";
                serviceConfig.port = desired.port;
                serviceConfig.documentRoot = documentRoot_;
                serviceConfig.token = token_;

                auto service = std::make_shared<
                    web_radar::WebRadarService>(
                        std::move(serviceConfig));
                if (!service->start()) {
                    lastError_ = service->status().lastError;
                    service->stop();
                    publishUiStatus(true);
                    return;
                }

                std::lock_guard<std::mutex> lock(serviceMutex_);
                service_ = std::move(service);
            } catch (const std::exception& error) {
                lastError_ = error.what();
            }
            publishUiStatus(true);
        }

        void publish(
            const esp::GameSnapshot& snapshot,
            bool includeSteamIds)
        {
            if (!snapshot) {
                return;
            }

            std::shared_ptr<web_radar::WebRadarService> service;
            {
                std::lock_guard<std::mutex> lock(serviceMutex_);
                service = service_;
            }
            if (!service || !service->isRunning()) {
                return;
            }

            game::web_radar_json::SerializationOptions options;
            options.includeSteamIds = includeSteamIds;
            const auto serializationStarted =
                std::chrono::steady_clock::now();
            service->publish(std::make_shared<const std::string>(
                game::web_radar_json::serializeSnapshotV1(
                    *snapshot,
                    options)));
            performance_metrics::serializationDuration.record(
                std::chrono::steady_clock::now() - serializationStarted);
        }

        void stop()
        {
            settings_.reset();
            detachAndStop();
            publishUiStatus(true);
        }

    private:
        struct Settings
        {
            bool enabled = false;
            bool lanAccess = false;
            uint16_t port = 22006;

            bool operator==(const Settings&) const = default;
        };

        void detachAndStop()
        {
            std::shared_ptr<web_radar::WebRadarService> service;
            {
                std::lock_guard<std::mutex> lock(serviceMutex_);
                service.swap(service_);
            }
            if (service) {
                service->stop();
            }
        }

        void publishUiStatus(bool force = false)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!force && now < nextUiStatusPublish_) {
                return;
            }
            nextUiStatusPublish_ = now + std::chrono::milliseconds(250);

            menu::WebRadarUiStatus ui;
            ui.bindAddress = settings_ && settings_->lanAccess
                ? "0.0.0.0"
                : "127.0.0.1";
            ui.error = lastError_;

            std::shared_ptr<web_radar::WebRadarService> service;
            {
                std::lock_guard<std::mutex> lock(serviceMutex_);
                service = service_;
            }
            if (service) {
                const web_radar::WebRadarStatus status = service->status();
                ui.running = status.isRunning();
                ui.viewers = status.viewerCount;
                ui.bindAddress = status.bindAddress;
                if (!status.lastError.empty()) {
                    ui.error = status.lastError;
                }
                if (ui.running) {
                    ui.viewerUrl =
                        "http://127.0.0.1:" +
                        std::to_string(status.port) +
                        "/?token=" + token_;
                }
            }
            menu::setWebRadarStatus(std::move(ui));
        }

        std::mutex serviceMutex_;
        std::shared_ptr<web_radar::WebRadarService> service_;
        std::optional<Settings> settings_;
        std::string token_;
        std::string documentRoot_;
        std::string lastError_;
        std::chrono::steady_clock::time_point nextUiStatusPublish_{};
    };

    struct AbandonedPublicRelayProducerSlot final
    {
        std::mutex mutex;
        std::shared_ptr<web_radar::PublicRelayProducer> producer;
    };

    // Placement construction deliberately leaves this one-slot registry out of
    // C++ static destruction. It is used only if the OS cannot create the
    // detached reaper thread: retaining the producer until ExitProcess is safer
    // than destroying it on the UI thread and entering a potentially blocking
    // WinHTTP join. A runtime enters a permanent failed state after using it, so
    // one process can never need more than this single slot.
    alignas(AbandonedPublicRelayProducerSlot)
        unsigned char abandonedPublicRelayProducerStorage[
            sizeof(AbandonedPublicRelayProducerSlot)]{};
    AbandonedPublicRelayProducerSlot* const
        abandonedPublicRelayProducerSlot = ::new (
            static_cast<void*>(abandonedPublicRelayProducerStorage))
                AbandonedPublicRelayProducerSlot{};

    void retainAbandonedPublicRelayProducer(
        std::shared_ptr<web_radar::PublicRelayProducer> producer) noexcept
    {
        std::lock_guard<std::mutex> lock(
            abandonedPublicRelayProducerSlot->mutex);
        abandonedPublicRelayProducerSlot->producer = std::move(producer);
    }

    class PublicRelayRuntime final
    {
    public:
        ~PublicRelayRuntime()
        {
            stop();
        }

        void sync(const menu::RuntimeConfig& config)
        {
            const Settings desired{
                config.publicRelayEnabled,
                config.publicRelayEnabled
                    ? config.publicRelayConnection
                    : std::shared_ptr<
                        const menu::PublicRelayConnectionSettings>{}
            };
            const bool settingsChanged =
                !settings_ || *settings_ != desired;
            if (settingsChanged) {
                settings_ = desired;
                if (!retirementFailed_) {
                    lastError_.clear();
                    beginRetirement();
                }
            }

            consumeCompletedRetirement();
            if (retirementFailed_ || retirement_) {
                publishUiStatus(settingsChanged);
                return;
            }
            if (!settings_ || !settings_->enabled) {
                publishUiStatus(settingsChanged);
                return;
            }

            {
                std::lock_guard<std::mutex> lock(producerMutex_);
                if (producer_) {
                    publishUiStatusLockedProducer(
                        producer_,
                        settingsChanged);
                    return;
                }
            }

            try {
                web_radar::PublicRelayConfig relayConfig;
                if (settings_->connection) {
                    const std::string_view endpointUrl =
                        settings_->connection->endpointUrl();
                    relayConfig.endpointUrl.assign(
                        endpointUrl.data(),
                        endpointUrl.size());
                    const std::string_view room =
                        settings_->connection->room();
                    relayConfig.room.assign(
                        room.data(),
                        room.size());
                    const std::string_view token =
                        settings_->connection->token();
                    relayConfig.token.assign(
                        token.data(),
                        token.size());
                }
                auto producer = std::make_shared<
                    web_radar::PublicRelayProducer>(
                        std::move(relayConfig));
                const bool started = producer->start();
                if (!started) {
                    lastError_ = producer->status().lastError;
                }
                std::lock_guard<std::mutex> lock(producerMutex_);
                producer_ = std::move(producer);
            } catch (const std::exception& error) {
                lastError_ = error.what();
            }
            publishUiStatus(true);
        }

        void publish(
            const esp::GameSnapshot& snapshot,
            const bool includeSteamIds)
        {
            if (!snapshot) {
                return;
            }

            std::shared_ptr<web_radar::PublicRelayProducer> producer;
            {
                std::lock_guard<std::mutex> lock(producerMutex_);
                producer = producer_;
            }
            if (!producer) {
                return;
            }
            const web_radar::PublicRelayState state =
                producer->status().state;
            if (state == web_radar::PublicRelayState::disabled ||
                state == web_radar::PublicRelayState::retiring ||
                state == web_radar::PublicRelayState::failed) {
                return;
            }

            game::web_radar_json::SerializationOptions options;
            options.includeSteamIds = includeSteamIds;
            const auto serializationStarted =
                std::chrono::steady_clock::now();
            producer->publish(std::make_shared<const std::string>(
                game::web_radar_json::serializeSnapshotV1(
                    *snapshot,
                    options)));
            performance_metrics::serializationDuration.record(
                std::chrono::steady_clock::now() - serializationStarted);
        }

        void stop()
        {
            settings_.reset();
            consumeCompletedRetirement();
            if (!retirementFailed_) {
                beginRetirement();
            }
            publishUiStatus(true);
        }

    private:
        struct Settings
        {
            bool enabled = false;
            std::shared_ptr<const menu::PublicRelayConnectionSettings>
                connection;

            bool operator==(const Settings&) const = default;
        };

        struct RetirementTicket final
        {
            std::atomic<bool> complete{false};
        };

        void beginRetirement()
        {
            std::shared_ptr<web_radar::PublicRelayProducer> producer;
            {
                std::lock_guard<std::mutex> lock(producerMutex_);
                producer.swap(producer_);
            }
            if (!producer) {
                return;
            }

            producer->requestStop();
            std::unique_ptr<std::thread> reaper;
            try {
                const auto ticket =
                    std::make_shared<RetirementTicket>();
                reaper = std::make_unique<std::thread>(
                    [producer, ticket]() mutable noexcept {
                        producer->stop();
                        producer.reset();
                        ticket->complete.store(
                            true,
                            std::memory_order_release);
                    });
                try {
                    reaper->detach();
                } catch (...) {
                    // A joinable std::thread terminates the process when its
                    // destructor runs. On the exceptional detach path, leak
                    // only that small wrapper so neither destruction nor a UI
                    // join can occur; Windows reclaims it at process exit.
                    static_cast<void>(reaper.release());
                    throw;
                }
                reaper.reset();
                retirement_ = ticket;
            } catch (...) {
                if (reaper && reaper->joinable()) {
                    static_cast<void>(reaper.release());
                }
                // Never fall back to a synchronous join here. If the OS cannot
                // create or detach the reaper, requestStop still prevents new
                // frames and this process-lifetime slot keeps the joinable
                // producer alive until Windows tears down the process.
                retainAbandonedPublicRelayProducer(
                    std::move(producer));
                retirement_.reset();
                retirementFailed_ = true;
                lastError_ =
                    "Unable to retire the previous Relay connection safely. "
                    "Restart the program before enabling Public Relay again.";
            }
        }

        void consumeCompletedRetirement()
        {
            if (!retirement_ ||
                !retirement_->complete.load(
                    std::memory_order_acquire)) {
                return;
            }
            retirement_.reset();
            lastError_.clear();
        }

        void publishUiStatusLockedProducer(
            const std::shared_ptr<
                web_radar::PublicRelayProducer>& producer,
            bool force)
        {
            if (!beginUiStatusPublish(force)) {
                return;
            }
            menu::PublicRelayUiStatus ui;
            const web_radar::PublicRelayStatus status =
                producer->status();
            ui.state = status.state;
            ui.framesSent = status.framesSent;
            ui.replacedFrames = status.replacedFrames;
            ui.droppedFrames = status.droppedFrames;
            ui.reconnects = status.reconnects;
            ui.error = status.lastError.empty()
                ? lastError_
                : status.lastError;
            menu::setPublicRelayStatus(std::move(ui));
        }

        bool beginUiStatusPublish(bool force)
        {
            const auto now = std::chrono::steady_clock::now();
            if (!force && now < nextUiStatusPublish_) {
                return false;
            }
            nextUiStatusPublish_ = now + std::chrono::milliseconds(250);
            return true;
        }

        void publishUiStatus(bool force = false)
        {
            if (!beginUiStatusPublish(force)) {
                return;
            }
            menu::PublicRelayUiStatus ui;
            ui.error = lastError_;
            if (retirementFailed_) {
                ui.state = web_radar::PublicRelayState::failed;
                menu::setPublicRelayStatus(std::move(ui));
                return;
            }
            if (retirement_) {
                ui.state = web_radar::PublicRelayState::retiring;
                menu::setPublicRelayStatus(std::move(ui));
                return;
            }
            std::shared_ptr<web_radar::PublicRelayProducer> producer;
            {
                std::lock_guard<std::mutex> lock(producerMutex_);
                producer = producer_;
            }
            if (producer) {
                const web_radar::PublicRelayStatus status =
                    producer->status();
                ui.state = status.state;
                ui.framesSent = status.framesSent;
                ui.replacedFrames = status.replacedFrames;
                ui.droppedFrames = status.droppedFrames;
                ui.reconnects = status.reconnects;
                if (!status.lastError.empty()) {
                    ui.error = status.lastError;
                }
            }
            menu::setPublicRelayStatus(std::move(ui));
        }

        std::mutex producerMutex_;
        std::shared_ptr<web_radar::PublicRelayProducer> producer_;
        std::shared_ptr<RetirementTicket> retirement_;
        std::optional<Settings> settings_;
        std::string lastError_;
        bool retirementFailed_ = false;
        std::chrono::steady_clock::time_point nextUiStatusPublish_{};
    };
}

int main(int argc, char* argv[])
{
    SetLastError(ERROR_SUCCESS);
    ScopedHandle instanceMutex(CreateMutexW(
        nullptr,
        FALSE,
        L"Local\\CS2ExternalOverlay-76F5E6C2-235A-4AA0"));
    const DWORD mutexError = GetLastError();
    if (!instanceMutex.get()) {
        showFatalError(
            L"Unable to create the single-instance guard.");
        return -1;
    }
    if (mutexError == ERROR_ALREADY_EXISTS) {
        MessageBoxW(
            nullptr,
            L"CS2 ESP is already running.",
            L"CS2 ESP",
            MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
        return 0;
    }

    memory::SetWritesAllowed(
        hasArgument(
            argc,
            argv,
            "--allow-memory-writes"));
    menu::loadPersistentSettings();

#ifndef SHOW_CONSOLE
    FreeConsole();
#endif

    WebRadarRuntime webRadar;
    PublicRelayRuntime publicRelay;
    web_radar::SnapshotRecorder snapshotRecorder(
        menu::persistentSettingsPath().parent_path() / L"recordings");
    menu::setStartupReport(diagnostics::inspectInstallation(
        std::filesystem::path(webRadarDocumentRoot())));

    while (sdl_renderer::running)
    {
        sdl_renderer::menuVisible = true;
        if (!sdl_renderer::initWaiting()) {
            showFatalError(
                L"Unable to initialize the overlay. Windows 10/11 with "
                L"per-monitor DPI awareness and a working SDL2.dll are required.");
            memory::Close();
            return -1;
        }
        if (!sdl_renderer::initImGui()) {
            showFatalError(L"Unable to initialize ImGui.");
            local_fixed_radar::reset();
            sdl_renderer::destroy();
            memory::Close();
            return -1;
        }

        int dotCount = 0;
        DWORD lastCheckTime = 0;
        bool gameFound = false;
        while (sdl_renderer::running && !gameFound)
        {
            sdl_renderer::pollEvents();

            const DWORD currentTime = GetTickCount();
            if (currentTime - lastCheckTime >= 3000 ||
                lastCheckTime == 0) {
                lastCheckTime = currentTime;
                dotCount = (dotCount % 3) + 1;
                if (esp::init()) {
                    gameFound = true;
                    break;
                }
            }

            if (!sdl_renderer::beginFrame()) {
                break;
            }
            sdl_renderer::newFrameImGui();
            renderWaitingScreen(dotCount);
            sdl_renderer::renderImGui();
            sdl_renderer::endFrame();
            Sleep(16);
        }

        sdl_renderer::shutdownImGui();
        local_fixed_radar::reset();
        sdl_renderer::destroy();
        if (!sdl_renderer::running) {
            break;
        }
        if (!gameFound ||
            !sdl_renderer::init(
                L"Counter-Strike 2",
                static_cast<DWORD>(esp::pID)) ||
            !sdl_renderer::initImGui() ||
            !aimbot::init()) {
            diagnostics::log(
                L"Game attach failed or raced with shutdown; retrying.");
            sdl_renderer::shutdownImGui();
            local_fixed_radar::reset();
            sdl_renderer::destroy();
            esp::clearRuntimeState();
            memory::Close();
            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));
            continue;
        }

        // Visual sampling follows the display refresh rate. Latency-sensitive
        // input features retain 240 Hz, while Radar-only work runs at its actual
        // 20 Hz publication rate instead of consuming a core without visible
        // benefit.
        std::atomic<bool> dataRunning{true};
        performance_metrics::resetSession();
        memory::ResetReadMetrics();
        std::thread dataThread(
            [&dataRunning, &webRadar, &publicRelay, &snapshotRecorder]() {
            constexpr auto radarInterval =
                std::chrono::milliseconds(50);
            auto nextDataTime = std::chrono::steady_clock::now();
            auto nextRadarTime = nextDataTime;
            int appliedSamplingRate = 0;
            int appliedThreadPriority = 0x7FFFFFFF;
            bool stateClearedWhileInactive = false;
            while (dataRunning.load(std::memory_order_relaxed)) {
                const menu::RuntimeConfig config =
                    menu::getRuntimeConfig();
                const bool gameForeground =
                    sdl_renderer::isGameForeground();
                const bool latencySensitive =
                    gameForeground &&
                    !config.inputSuppressed &&
                    (config.aimbotEnabled ||
                     config.triggerbotEnabled);
                const int desiredThreadPriority = latencySensitive
                    ? THREAD_PRIORITY_NORMAL
                    : THREAD_PRIORITY_BELOW_NORMAL;
                if (desiredThreadPriority != appliedThreadPriority &&
                    SetThreadPriority(
                        GetCurrentThread(),
                        desiredThreadPriority)) {
                    appliedThreadPriority = desiredThreadPriority;
                }
                const bool radarSamplingEnabled =
                    config.radarSnapshotEnabled();
                const bool backgroundRadarSampling =
                    (config.webRadarEnabled ||
                     config.publicRelayEnabled) &&
                    !config.webRadarPauseWhenUnfocused;
                if (!gameForeground && !backgroundRadarSampling) {
                    if (!stateClearedWhileInactive) {
                        esp::clearRuntimeState();
                        stateClearedWhileInactive = true;
                        if (config.webRadarEnabled) {
                            webRadar.publish(
                                esp::getGameSnapshot(),
                                config.webRadarIncludeSteamIds);
                        }
                        if (config.publicRelayEnabled) {
                            publicRelay.publish(
                                esp::getGameSnapshot(),
                                config.publicRelayIncludeSteamIds);
                        }
                    }
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(25));
                    nextDataTime = std::chrono::steady_clock::now();
                    nextRadarTime = nextDataTime;
                    continue;
                }
                stateClearedWhileInactive = false;

                const runtime_timing::SamplingDemand samplingDemand{
                    latencySensitive,
                    gameForeground &&
                        (config.espEnabled ||
                         config.grenadeESP ||
                         config.droppedWeaponESP),
                    gameForeground &&
                        (config.antiFlash || config.bombTimer)
                };
                const int samplingRate =
                    runtime_timing::dataSamplingRate(
                        sdl_renderer::getTargetRefreshRate(),
                        samplingDemand);
                if (samplingRate != appliedSamplingRate) {
                    appliedSamplingRate = samplingRate;
                    performance_metrics::samplingRateHz.store(
                        samplingRate,
                        std::memory_order_relaxed);
                    nextDataTime = std::chrono::steady_clock::now();
                }
                const auto dataInterval =
                    runtime_timing::intervalForRate(samplingRate);

                menu::RuntimeConfig samplingConfig = config;
                if (!gameForeground) {
                    samplingConfig.antiFlash = false;
                    samplingConfig.aimbotEnabled = false;
                    samplingConfig.triggerbotEnabled = false;
                    samplingConfig.espEnabled = false;
                }
                const auto samplingStarted =
                    std::chrono::steady_clock::now();
                esp::updateEntities(samplingConfig);
                if (gameForeground) {
                    aimbot::update(config);
                    aimbot::updateTriggerbot(config);
                }

                const auto sampleComplete =
                    std::chrono::steady_clock::now();
                performance_metrics::samplingDuration.record(
                    sampleComplete - samplingStarted);
                if (radarSamplingEnabled &&
                    sampleComplete >= nextRadarTime) {
                    const esp::GameSnapshot snapshot =
                        esp::getGameSnapshot();
                    if (config.webRadarEnabled) {
                        webRadar.publish(
                            snapshot,
                            config.webRadarIncludeSteamIds);
                    }
                    if (config.publicRelayEnabled) {
                        publicRelay.publish(
                            snapshot,
                            config.publicRelayIncludeSteamIds);
                    }
                    if (config.radarRecordingEnabled && snapshot) {
                        game::web_radar_json::SerializationOptions options;
                        options.includePlayerNames = false;
                        const auto serializationStarted =
                            std::chrono::steady_clock::now();
                        snapshotRecorder.publish(
                            std::make_shared<const std::string>(
                                game::web_radar_json::serializeSnapshotV1(
                                    *snapshot,
                                    options)));
                        performance_metrics::serializationDuration.record(
                            std::chrono::steady_clock::now() -
                            serializationStarted);
                    }
                    nextRadarTime =
                        sampleComplete + radarInterval;
                } else if (!radarSamplingEnabled) {
                    nextRadarTime = sampleComplete;
                }

                nextDataTime += dataInterval;
                const auto now = std::chrono::steady_clock::now();
                if (nextDataTime > now) {
                    std::this_thread::sleep_until(nextDataTime);
                } else {
                    // Never spin continuously when RPM work exceeds its current
                    // budget. A short backoff keeps one slow map/update from
                    // monopolizing a CPU core.
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(1));
                    performance_metrics::missedSamplingDeadlines.fetch_add(
                        1,
                        std::memory_order_relaxed);
                    nextDataTime = now + dataInterval;
                }
            }
        });

        auto nextRenderTime = std::chrono::steady_clock::now();
        while (sdl_renderer::running &&
               !sdl_renderer::isGameDisconnected())
        {
            const auto renderStarted = std::chrono::steady_clock::now();
            sdl_renderer::pollEvents();
            sdl_renderer::updateWindowPosition();
            if (!sdl_renderer::isGameVisible()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(25));
                nextRenderTime = std::chrono::steady_clock::now();
                continue;
            }

            if (!sdl_renderer::beginFrame()) {
                break;
            }
            sdl_renderer::newFrameImGui();

            if (menu::espEnabled ||
                menu::grenadeESP ||
                menu::droppedWeaponESP ||
                (menu::aimbotEnabled &&
                 menu::aimbotShowFOV)) {
                esp::render();
            }
            const menu::RuntimeConfig renderConfig =
                menu::getRuntimeConfig();
            if (renderConfig.localRadarEnabled) {
                const esp::GameSnapshot snapshot =
                    esp::getGameSnapshot();
                if (snapshot) {
                    local_fixed_radar::render(
                        *snapshot,
                        local_fixed_radar::RenderConfig{
                            renderConfig.localRadarAnchorX,
                            renderConfig.localRadarAnchorY,
                            renderConfig.localRadarSize,
                            renderConfig.localRadarMarkerSize,
                            renderConfig.localRadarShowNames
                        });
                }
            } else {
                local_fixed_radar::reset();
            }
            esp::renderBombTimer();
            menu::render();
            {
                const menu::RuntimeConfig config =
                    menu::getRuntimeConfig();
                webRadar.sync(config);
                publicRelay.sync(config);
                snapshotRecorder.sync(config.radarRecordingEnabled);
                menu::setRecorderStatus(snapshotRecorder.status());
            }

            sdl_renderer::renderImGui();
            sdl_renderer::endFrame();
            performance_metrics::renderCpuDuration.record(
                std::chrono::steady_clock::now() - renderStarted);

            const int refreshRate = std::max(
                60,
                sdl_renderer::getTargetRefreshRate());
            const auto renderInterval =
                std::chrono::nanoseconds(
                    1000000000LL / refreshRate);
            nextRenderTime += renderInterval;
            const auto now = std::chrono::steady_clock::now();
            if (nextRenderTime > now) {
                std::this_thread::sleep_until(nextRenderTime);
            } else {
                performance_metrics::missedRenderDeadlines.fetch_add(
                    1,
                    std::memory_order_relaxed);
                nextRenderTime = now;
            }
        }

        dataRunning.store(false, std::memory_order_relaxed);
        dataThread.join();
        sdl_renderer::shutdownImGui();
        local_fixed_radar::reset();
        sdl_renderer::destroy();
        esp::clearRuntimeState();
        {
            const menu::RuntimeConfig config =
                menu::getRuntimeConfig();
            if (config.webRadarEnabled) {
                webRadar.publish(
                    esp::getGameSnapshot(),
                    config.webRadarIncludeSteamIds);
            }
            if (config.publicRelayEnabled) {
                publicRelay.publish(
                    esp::getGameSnapshot(),
                    config.publicRelayIncludeSteamIds);
            }
        }
        memory::Close();

        if (sdl_renderer::running) {
            diagnostics::log(
                L"CS2 disconnected; returning to the waiting screen.");
            std::this_thread::sleep_for(
                std::chrono::milliseconds(500));
        }
    }

    webRadar.stop();
    publicRelay.stop();
    snapshotRecorder.stop();
    menu::setRecorderStatus(snapshotRecorder.status());
    menu::savePersistentSettings();
    memory::Close();
    return 0;
}
