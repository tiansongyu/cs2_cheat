#pragma once

#include "imgui.h"
#include "core/renderer/sdl_renderer.h"
#include "core/memory/memory.hpp"
#include "core/diagnostics.hpp"
#include "core/performance_metrics.hpp"
#include "features/web_radar/public_relay_config.hpp"
#include "features/web_radar/snapshot_recorder.hpp"
#include <Windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cwchar>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace menu
{
    // RuntimeConfig is copied by the 240 Hz sampling loop. Keep the complete
    // connection configuration in one immutable allocation instead of
    // repeatedly allocating URL/room/token strings, and wipe the token before
    // that allocation is released.
    class PublicRelayConnectionSettings final
    {
    public:
        PublicRelayConnectionSettings(
            const std::string_view endpointUrl,
            const std::string_view room,
            const std::string_view token)
            : endpointUrl_(endpointUrl),
              room_(room),
              token_(token)
        {
        }

        ~PublicRelayConnectionSettings()
        {
            if (!token_.empty()) {
                SecureZeroMemory(token_.data(), token_.size());
            }
        }

        PublicRelayConnectionSettings(
            const PublicRelayConnectionSettings&) = delete;
        PublicRelayConnectionSettings& operator=(
            const PublicRelayConnectionSettings&) = delete;
        PublicRelayConnectionSettings(
            PublicRelayConnectionSettings&&) = delete;
        PublicRelayConnectionSettings& operator=(
            PublicRelayConnectionSettings&&) = delete;

        [[nodiscard]] std::string_view endpointUrl() const noexcept
        {
            return endpointUrl_;
        }

        [[nodiscard]] std::string_view room() const noexcept
        {
            return room_;
        }

        [[nodiscard]] std::string_view token() const noexcept
        {
            return token_;
        }

    private:
        std::string endpointUrl_;
        std::string room_;
        std::string token_;
    };

    // ImGui edits these values on the render thread. The worker copies one
    // coherent snapshot under this mutex at the start of each update pass.
    inline std::mutex configMutex;
    inline diagnostics::StartupReport startupReport;

    inline void setStartupReport(diagnostics::StartupReport report)
    {
        startupReport = std::move(report);
    }

    struct RuntimeConfig
    {
        bool espEnabled = true;
        bool espWeapon = true;
        bool espFlashIndicator = false;
        bool antiFlash = false;
        bool espViewAngle = true;
        bool localRadarEnabled = false;
        bool localRadarShowNames = true;
        float localRadarAnchorX = 0.02f;
        float localRadarAnchorY = 0.08f;
        float localRadarSize = 0.32f;
        float localRadarMarkerSize = 12.0f;
        bool webRadarEnabled = false;
        bool webRadarLanAccess = false;
        bool webRadarPauseWhenUnfocused = true;
        bool webRadarIncludePlayerNames = true;
        bool webRadarIncludeSteamIds = false;
        int webRadarTeamViewPolicy = 0;
        uint16_t webRadarPort = 22006;
        bool radarRecordingEnabled = false;
        int radarRefreshRateHz = 20;
        bool publicRelayEnabled = false;
        bool publicRelayIncludePlayerNames = true;
        bool publicRelayIncludeSteamIds = false;
        int publicRelayTeamViewPolicy = 0;
        std::shared_ptr<const PublicRelayConnectionSettings>
            publicRelayConnection;
        bool espWallCheck = true;
        bool espSkeleton = true;
        bool grenadeESP = false;
        bool droppedWeaponESP = false;
        bool bombTimer = true;

        bool headOffsetEnabled = true;
        float headOffsetAmount = 5.0f;
        float headOffsetAngleMin = 45.0f;
        float headOffsetAngleMax = 135.0f;
        bool aimbotEnabled = false;
        float aimbotFOV = 10.0f;
        float aimbotSmoothing = 5.0f;
        int aimbotBone = 0;
        bool aimbotVisibleOnly = true;
        int aimbotKey = VK_SHIFT;
        bool smartAimEnabled = false;
        int smartAimPriority = 0;
        float mouseSensitivity = 1.0f;

        bool triggerbotEnabled = false;
        int triggerbotDelay = 50;
        int triggerbotKey = 0x46;
        bool inputSuppressed = false;

        [[nodiscard]] bool radarSnapshotEnabled() const noexcept
        {
            return localRadarEnabled || webRadarEnabled ||
                publicRelayEnabled || radarRecordingEnabled;
        }
    };

    // Current tab index
    inline int currentTab = 0;

    // ESP Settings
    inline bool espEnabled = true;
    inline bool espBox = true;
    inline bool espHealth = true;
    inline bool espDistance = true;  // Default ON
    inline bool espWeapon = true;    // Weapon display - Default ON
    inline bool espViewAngle = true; // View angle indicator - Default ON
    inline bool espViewAngleText = false; // Show angle degree text
    inline bool espFlashIndicator = false; // Flashbang eye indicator - Default OFF
    inline bool espWallCheck = true; // CS2 spotted-state indicator
    inline bool espSnaplines = false;

    // Skeleton ESP
    inline bool espSkeleton = true;
    inline float espSkeletonColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };  // White

    // Colors
    inline float espBoxColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };          // Red - spotted state
    inline float espWallColor[4] = { 0.0f, 1.0f, 0.0f, 1.0f };         // Green - not spotted or unknown
    inline float espDistanceColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    inline float espWeaponColor[4] = { 0.0f, 1.0f, 1.0f, 1.0f };       // Cyan color for weapon
    inline float espFlashNormalColor[4] = { 1.0f, 0.0f, 0.0f, 1.0f };  // Red - normal eye state
    inline float espFlashColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };        // Yellow - flashed eye state
    inline float espSnaplinesColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };

    // Visual Settings
    inline int snaplinesOrigin = 0; // 0=Bottom, 1=Center, 2=Top

    // Aimbot Settings
    inline bool aimbotEnabled = false;     // Aimbot enabled
    inline float aimbotFOV = 10.0f;        // Field of view for aimbot (degrees)
    inline float aimbotSmoothing = 5.0f;   // Smoothing factor (1.0 = instant, higher = smoother)
    inline int aimbotBone = 0;             // 0=Head, 1=Neck, 2=Chest
    inline bool aimbotVisibleOnly = true;  // Only aim at enemies flagged as spotted
    inline int aimbotKey = VK_SHIFT;       // Aimbot activation key (default: Shift key)
    inline bool aimbotShowFOV = true;      // Show FOV circle on screen
    inline float aimbotFOVColor[4] = { 1.0f, 1.0f, 0.0f, 1.0f };  // Color-key overlays use opaque primitives

    // Head Offset Settings (for side-facing enemies)
    inline bool headOffsetEnabled = true;      // Enable head offset compensation
    inline float headOffsetAmount = 5.0f;      // Offset amount in game units (0-15)
    inline float headOffsetAngleMin = 45.0f;   // Minimum angle for offset (degrees)
    inline float headOffsetAngleMax = 135.0f;  // Maximum angle for offset (degrees)

    // Smart Aim Settings (auto-lock spotted enemies by priority)
    inline bool smartAimEnabled = false;      // Smart aim mode (ignores FOV, auto-selects best target)
    inline int smartAimPriority = 0;          // 0=Distance first, 1=Health first

    // Triggerbot Settings
    inline bool triggerbotEnabled = false; // Triggerbot enabled
    inline int triggerbotDelay = 50;       // Delay before shooting (milliseconds)
    inline int triggerbotKey = 0x46;       // Triggerbot activation key (default: F key, 0x46 = 'F')

    // Input Settings
    inline float mouseSensitivity = 1.0f;  // In-game mouse sensitivity (for aim/trigger mouse conversion)

    // Viewport mapping: 0=auto black-bar detection, 1=full client,
    // 2=force 4:3 black bars, 3=force 16:10 black bars.
    inline int viewportMode = 0;

    // The local overlay and browser use the same fixed north-up map catalogue
    // and full GameSnapshot. Position values are normalized to the available
    // viewport so resolution and black-bar changes do not move the panel out
    // of bounds.
    inline bool localRadarEnabled = false;
    inline bool localRadarShowNames = true;
    inline float localRadarAnchorX = 0.02f;
    inline float localRadarAnchorY = 0.08f;
    inline float localRadarSize = 0.32f;
    inline float localRadarMarkerSize = 12.0f;

    // Fixed-map browser Radar settings. The HTTP service is local-only unless
    // the user explicitly enables LAN access; stream URLs always carry a token.
    inline bool webRadarEnabled = false;
    inline bool webRadarLanAccess = false;
    inline bool webRadarPauseWhenUnfocused = true;
    inline bool webRadarIncludePlayerNames = true;
    inline bool webRadarIncludeSteamIds = false;
    inline int webRadarTeamViewPolicy = 0;
    inline int webRadarPort = 22006;
    inline bool radarRecordingEnabled = false;

    // Public Relay credentials intentionally live only in process memory and
    // are never included in any settings persistence path. The producer token
    // is rendered with ImGui's password mode and is never copied to status.
    inline bool publicRelayEnabled = false;
    inline bool publicRelayIncludePlayerNames = true;
    inline bool publicRelayIncludeSteamIds = false;
    inline int publicRelayTeamViewPolicy = 0;
    inline std::array<char, 512> publicRelayUrl{};
    inline std::array<char, 65> publicRelayRoom{};
    inline std::array<char, 513> publicRelayToken{};
    inline std::shared_ptr<const PublicRelayConnectionSettings>
        publicRelayConnectionSnapshot;

    struct WebRadarUiStatus
    {
        bool running = false;
        size_t viewers = 0;
        std::uint64_t publishedFrames = 0;
        std::uint64_t sentFrames = 0;
        std::uint64_t replacedFrames = 0;
        std::uint64_t publishedBytes = 0;
        double maximumSendLatencyMilliseconds = 0.0;
        std::string viewerUrl;
        std::string bindAddress = "127.0.0.1";
        std::string error;
    };

    inline std::mutex webRadarStatusMutex;
    inline WebRadarUiStatus webRadarStatus;

    inline void setWebRadarStatus(WebRadarUiStatus status)
    {
        std::lock_guard<std::mutex> lock(webRadarStatusMutex);
        webRadarStatus = std::move(status);
    }

    inline WebRadarUiStatus getWebRadarStatus()
    {
        std::lock_guard<std::mutex> lock(webRadarStatusMutex);
        return webRadarStatus;
    }

    inline std::mutex recorderStatusMutex;
    inline web_radar::SnapshotRecorderStatus recorderStatus;

    inline void setRecorderStatus(
        web_radar::SnapshotRecorderStatus status)
    {
        std::lock_guard<std::mutex> lock(recorderStatusMutex);
        recorderStatus = std::move(status);
    }

    inline web_radar::SnapshotRecorderStatus getRecorderStatus()
    {
        std::lock_guard<std::mutex> lock(recorderStatusMutex);
        return recorderStatus;
    }

    struct PublicRelayUiStatus
    {
        web_radar::PublicRelayState state =
            web_radar::PublicRelayState::disabled;
        std::uint64_t framesSent = 0;
        std::uint64_t replacedFrames = 0;
        std::uint64_t droppedFrames = 0;
        std::uint64_t reconnects = 0;
        std::string error;
    };

    inline std::mutex publicRelayStatusMutex;
    inline PublicRelayUiStatus publicRelayStatus;

    inline void setPublicRelayStatus(PublicRelayUiStatus status)
    {
        std::lock_guard<std::mutex> lock(publicRelayStatusMutex);
        publicRelayStatus = std::move(status);
    }

    inline PublicRelayUiStatus getPublicRelayStatus()
    {
        std::lock_guard<std::mutex> lock(publicRelayStatusMutex);
        return publicRelayStatus;
    }

    // Misc Settings
    inline bool antiFlash = false;          // Memory writes are opt-in
    inline bool bombTimer = true;            // Show bomb timer on screen
    inline bool grenadeESP = false;          // Show grenade positions
    inline bool droppedWeaponESP = false;    // Show dropped weapon positions

    // Menu Toggle Key
    inline int menuToggleKey = VK_F4;        // Menu toggle key (default: F4)
    inline int exitKey = VK_F9;              // Exit key (default: F9)

    // Hotkey binding state
    inline bool isBindingKey = false;
    inline int* bindingKeyTarget = nullptr;
    inline const char* bindingKeyName = nullptr;
    inline bool bindingWaitingForRelease = false;
    inline std::string bindingError;
    inline bool suppressHotkeysUntilRelease = false;

    inline RuntimeConfig buildRuntimeConfig()
    {
        RuntimeConfig config{};
        config.espEnabled = espEnabled;
        config.espWeapon = espWeapon;
        config.espFlashIndicator = espFlashIndicator;
        config.antiFlash = antiFlash;
        config.espViewAngle = espViewAngle;
        config.localRadarEnabled = localRadarEnabled;
        config.localRadarShowNames = localRadarShowNames;
        config.localRadarAnchorX = std::clamp(
            localRadarAnchorX,
            0.0f,
            1.0f);
        config.localRadarAnchorY = std::clamp(
            localRadarAnchorY,
            0.0f,
            1.0f);
        config.localRadarSize = std::clamp(
            localRadarSize,
            0.18f,
            0.65f);
        config.localRadarMarkerSize = std::clamp(
            localRadarMarkerSize,
            6.0f,
            24.0f);
        config.webRadarEnabled = webRadarEnabled;
        config.webRadarLanAccess = webRadarLanAccess;
        config.webRadarPauseWhenUnfocused =
            webRadarPauseWhenUnfocused;
        config.webRadarIncludePlayerNames =
            webRadarIncludePlayerNames;
        config.webRadarIncludeSteamIds =
            webRadarIncludeSteamIds;
        config.webRadarTeamViewPolicy = std::clamp(
            webRadarTeamViewPolicy,
            0,
            2);
        config.webRadarPort = static_cast<uint16_t>(
            std::clamp(webRadarPort, 1024, 65535));
        config.radarRecordingEnabled = radarRecordingEnabled;
        config.publicRelayEnabled = publicRelayEnabled;
        config.publicRelayIncludePlayerNames =
            publicRelayIncludePlayerNames;
        config.publicRelayIncludeSteamIds =
            publicRelayIncludeSteamIds;
        config.publicRelayTeamViewPolicy = std::clamp(
            publicRelayTeamViewPolicy,
            0,
            2);
        config.publicRelayConnection = publicRelayConnectionSnapshot;
        config.espWallCheck = espWallCheck;
        config.espSkeleton = espSkeleton;
        config.grenadeESP = grenadeESP;
        config.droppedWeaponESP = droppedWeaponESP;
        config.bombTimer = bombTimer;

        config.headOffsetEnabled = headOffsetEnabled;
        config.headOffsetAmount = headOffsetAmount;
        config.headOffsetAngleMin = headOffsetAngleMin;
        config.headOffsetAngleMax = headOffsetAngleMax;
        config.aimbotEnabled = aimbotEnabled;
        config.aimbotFOV = aimbotFOV;
        config.aimbotSmoothing = aimbotSmoothing;
        config.aimbotBone = aimbotBone;
        config.aimbotVisibleOnly = aimbotVisibleOnly;
        config.aimbotKey = aimbotKey;
        config.smartAimEnabled = smartAimEnabled;
        config.smartAimPriority = smartAimPriority;
        config.mouseSensitivity = mouseSensitivity;

        config.triggerbotEnabled = triggerbotEnabled;
        config.triggerbotDelay = triggerbotDelay;
        config.triggerbotKey = triggerbotKey;
        config.inputSuppressed =
            isBindingKey || suppressHotkeysUntilRelease;
        return config;
    }

    inline RuntimeConfig runtimeConfigSnapshot = buildRuntimeConfig();

    inline RuntimeConfig getRuntimeConfig()
    {
        std::lock_guard<std::mutex> lock(configMutex);
        return runtimeConfigSnapshot;
    }

    inline void publishRuntimeConfig()
    {
        const std::string_view currentUrl(publicRelayUrl.data());
        const std::string_view currentRoom(publicRelayRoom.data());
        const std::string_view currentToken(publicRelayToken.data());
        if (currentUrl.empty() && currentRoom.empty() && currentToken.empty()) {
            publicRelayConnectionSnapshot.reset();
        } else if (!publicRelayConnectionSnapshot ||
                   publicRelayConnectionSnapshot->endpointUrl() != currentUrl ||
                   publicRelayConnectionSnapshot->room() != currentRoom ||
                   publicRelayConnectionSnapshot->token() != currentToken) {
            publicRelayConnectionSnapshot =
                std::make_shared<const PublicRelayConnectionSettings>(
                    currentUrl,
                    currentRoom,
                    currentToken);
        }
        const RuntimeConfig updated = buildRuntimeConfig();
        std::lock_guard<std::mutex> lock(configMutex);
        runtimeConfigSnapshot = updated;
    }

    inline std::filesystem::path persistentSettingsPath()
    {
        std::array<wchar_t, 32768> localAppData{};
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA",
            localAppData.data(),
            static_cast<DWORD>(localAppData.size()));
        std::filesystem::path directory =
            length > 0 && length < localAppData.size()
                ? std::filesystem::path(
                    std::wstring_view(localAppData.data(), length)) /
                    L"AegisCS2"
                : std::filesystem::temp_directory_path() / L"AegisCS2";
        std::error_code error;
        std::filesystem::create_directories(directory, error);
        return directory / L"settings-v1.ini";
    }

    inline int readPersistentInt(
        const wchar_t* key,
        const int fallback,
        const std::filesystem::path& path)
    {
        return static_cast<int>(GetPrivateProfileIntW(
            L"settings",
            key,
            fallback,
            path.c_str()));
    }

    inline float readPersistentFloat(
        const wchar_t* key,
        const float fallback,
        const std::filesystem::path& path)
    {
        std::array<wchar_t, 64> fallbackText{};
        std::array<wchar_t, 64> value{};
        swprintf_s(fallbackText.data(), fallbackText.size(), L"%.6f", fallback);
        GetPrivateProfileStringW(
            L"settings",
            key,
            fallbackText.data(),
            value.data(),
            static_cast<DWORD>(value.size()),
            path.c_str());
        wchar_t* end = nullptr;
        const float parsed = std::wcstof(value.data(), &end);
        return end != value.data() && std::isfinite(parsed)
            ? parsed
            : fallback;
    }

    inline void loadPersistentSettings()
    {
        const std::filesystem::path path = persistentSettingsPath();
        if (readPersistentInt(L"schema", 0, path) != 1) {
            publishRuntimeConfig();
            return;
        }

        viewportMode = std::clamp(
            readPersistentInt(L"viewport_mode", viewportMode, path),
            0,
            3);
        localRadarShowNames = readPersistentInt(
            L"local_radar_names",
            localRadarShowNames ? 1 : 0,
            path) != 0;
        localRadarAnchorX = std::clamp(
            readPersistentFloat(
                L"local_radar_anchor_x",
                localRadarAnchorX,
                path),
            0.0f,
            1.0f);
        localRadarAnchorY = std::clamp(
            readPersistentFloat(
                L"local_radar_anchor_y",
                localRadarAnchorY,
                path),
            0.0f,
            1.0f);
        localRadarSize = std::clamp(
            readPersistentFloat(
                L"local_radar_size",
                localRadarSize,
                path),
            0.18f,
            0.65f);
        localRadarMarkerSize = std::clamp(
            readPersistentFloat(
                L"local_radar_marker_size",
                localRadarMarkerSize,
                path),
            6.0f,
            24.0f);
        webRadarPort = std::clamp(
            readPersistentInt(L"web_radar_port", webRadarPort, path),
            1024,
            65535);
        webRadarPauseWhenUnfocused = readPersistentInt(
            L"pause_shared_radar_unfocused",
            webRadarPauseWhenUnfocused ? 1 : 0,
            path) != 0;
        webRadarIncludePlayerNames = readPersistentInt(
            L"web_radar_player_names",
            webRadarIncludePlayerNames ? 1 : 0,
            path) != 0;
        webRadarTeamViewPolicy = std::clamp(
            readPersistentInt(
                L"web_radar_team_policy",
                webRadarTeamViewPolicy,
                path),
            0,
            2);
        publishRuntimeConfig();
    }

    inline void writePersistentValue(
        const wchar_t* key,
        const std::wstring_view value,
        const std::filesystem::path& path)
    {
        const std::wstring owned(value);
        WritePrivateProfileStringW(
            L"settings",
            key,
            owned.c_str(),
            path.c_str());
    }

    inline void savePersistentSettings()
    {
        const std::filesystem::path path = persistentSettingsPath();
        writePersistentValue(L"schema", L"1", path);
        writePersistentValue(
            L"viewport_mode",
            std::to_wstring(std::clamp(viewportMode, 0, 3)),
            path);
        writePersistentValue(
            L"local_radar_names",
            localRadarShowNames ? L"1" : L"0",
            path);

        const auto writeFloat = [&path](
            const wchar_t* key,
            const float value) {
            std::array<wchar_t, 64> text{};
            swprintf_s(text.data(), text.size(), L"%.6f", value);
            writePersistentValue(key, text.data(), path);
        };
        writeFloat(L"local_radar_anchor_x", localRadarAnchorX);
        writeFloat(L"local_radar_anchor_y", localRadarAnchorY);
        writeFloat(L"local_radar_size", localRadarSize);
        writeFloat(L"local_radar_marker_size", localRadarMarkerSize);
        writePersistentValue(
            L"web_radar_port",
            std::to_wstring(std::clamp(webRadarPort, 1024, 65535)),
            path);
        writePersistentValue(
            L"pause_shared_radar_unfocused",
            webRadarPauseWhenUnfocused ? L"1" : L"0",
            path);
        writePersistentValue(
            L"web_radar_player_names",
            webRadarIncludePlayerNames ? L"1" : L"0",
            path);
        writePersistentValue(
            L"web_radar_team_policy",
            std::to_wstring(std::clamp(webRadarTeamViewPolicy, 0, 2)),
            path);
    }

    // Convert virtual key code to key name
    inline const char* GetKeyName(int vkCode)
    {
        static char keyName[32];

        switch (vkCode)
        {
        // Special keys
        case VK_LBUTTON: return "Mouse1";
        case VK_RBUTTON: return "Mouse2";
        case VK_MBUTTON: return "Mouse3";
        case VK_XBUTTON1: return "Mouse4";
        case VK_XBUTTON2: return "Mouse5";
        case VK_BACK: return "Backspace";
        case VK_TAB: return "Tab";
        case VK_RETURN: return "Enter";
        case VK_SHIFT: return "Shift";
        case VK_CONTROL: return "Ctrl";
        case VK_MENU: return "Alt";
        case VK_PAUSE: return "Pause";
        case VK_CAPITAL: return "CapsLock";
        case VK_ESCAPE: return "Escape";
        case VK_SPACE: return "Space";
        case VK_PRIOR: return "PageUp";
        case VK_NEXT: return "PageDown";
        case VK_END: return "End";
        case VK_HOME: return "Home";
        case VK_LEFT: return "Left";
        case VK_UP: return "Up";
        case VK_RIGHT: return "Right";
        case VK_DOWN: return "Down";
        case VK_INSERT: return "Insert";
        case VK_DELETE: return "Delete";
        case VK_LSHIFT: return "LShift";
        case VK_RSHIFT: return "RShift";
        case VK_LCONTROL: return "LCtrl";
        case VK_RCONTROL: return "RCtrl";
        case VK_LMENU: return "LAlt";
        case VK_RMENU: return "RAlt";

        // Function keys
        case VK_F1: return "F1";
        case VK_F2: return "F2";
        case VK_F3: return "F3";
        case VK_F4: return "F4";
        case VK_F5: return "F5";
        case VK_F6: return "F6";
        case VK_F7: return "F7";
        case VK_F8: return "F8";
        case VK_F9: return "F9";
        case VK_F10: return "F10";
        case VK_F11: return "F11";
        case VK_F12: return "F12";

        // Numpad
        case VK_NUMPAD0: return "Num0";
        case VK_NUMPAD1: return "Num1";
        case VK_NUMPAD2: return "Num2";
        case VK_NUMPAD3: return "Num3";
        case VK_NUMPAD4: return "Num4";
        case VK_NUMPAD5: return "Num5";
        case VK_NUMPAD6: return "Num6";
        case VK_NUMPAD7: return "Num7";
        case VK_NUMPAD8: return "Num8";
        case VK_NUMPAD9: return "Num9";
        case VK_MULTIPLY: return "Num*";
        case VK_ADD: return "Num+";
        case VK_SUBTRACT: return "Num-";
        case VK_DECIMAL: return "Num.";
        case VK_DIVIDE: return "Num/";

        // Letters A-Z (0x41 - 0x5A)
        case 0x41: return "A";
        case 0x42: return "B";
        case 0x43: return "C";
        case 0x44: return "D";
        case 0x45: return "E";
        case 0x46: return "F";
        case 0x47: return "G";
        case 0x48: return "H";
        case 0x49: return "I";
        case 0x4A: return "J";
        case 0x4B: return "K";
        case 0x4C: return "L";
        case 0x4D: return "M";
        case 0x4E: return "N";
        case 0x4F: return "O";
        case 0x50: return "P";
        case 0x51: return "Q";
        case 0x52: return "R";
        case 0x53: return "S";
        case 0x54: return "T";
        case 0x55: return "U";
        case 0x56: return "V";
        case 0x57: return "W";
        case 0x58: return "X";
        case 0x59: return "Y";
        case 0x5A: return "Z";

        // Numbers 0-9 (0x30 - 0x39)
        case 0x30: return "0";
        case 0x31: return "1";
        case 0x32: return "2";
        case 0x33: return "3";
        case 0x34: return "4";
        case 0x35: return "5";
        case 0x36: return "6";
        case 0x37: return "7";
        case 0x38: return "8";
        case 0x39: return "9";

        default:
            sprintf_s(keyName, "Key(0x%02X)", vkCode);
            return keyName;
        }
    }

    // Check for key press during binding
    inline int GetPressedKey()
    {
        // Check mouse buttons
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) return VK_LBUTTON;
        if (GetAsyncKeyState(VK_RBUTTON) & 0x8000) return VK_RBUTTON;
        if (GetAsyncKeyState(VK_MBUTTON) & 0x8000) return VK_MBUTTON;
        if (GetAsyncKeyState(VK_XBUTTON1) & 0x8000) return VK_XBUTTON1;
        if (GetAsyncKeyState(VK_XBUTTON2) & 0x8000) return VK_XBUTTON2;

        // Check all keyboard keys
        for (int i = 0x08; i <= 0xFE; i++)
        {
            // Skip some keys that shouldn't be used
            if (i == VK_ESCAPE) continue;  // Escape cancels binding

            if (GetAsyncKeyState(i) & 0x8000)
                return i;
        }

        return 0;
    }

    inline bool AnyBindableKeyDown()
    {
        for (int key = 0x01; key <= 0xFE; ++key) {
            if (GetAsyncKeyState(key) & 0x8000) {
                return true;
            }
        }
        return false;
    }

    inline bool ConfiguredHotkeysReleased()
    {
        const int keys[] = {
            menuToggleKey,
            exitKey,
            aimbotKey,
            triggerbotKey
        };
        for (int key : keys) {
            if (key > 0 && key <= 0xFF &&
                (GetAsyncKeyState(key) & 0x8000)) {
                return false;
            }
        }
        return true;
    }

    inline const char* FindHotkeyConflict(
        const int* target,
        int candidate)
    {
        struct Binding
        {
            const char* name;
            const int* key;
        };
        const Binding bindings[] = {
            { "Menu Toggle", &menuToggleKey },
            { "Exit Program", &exitKey },
            { "Aimbot Key", &aimbotKey },
            { "Triggerbot Key", &triggerbotKey }
        };
        for (const Binding& binding : bindings) {
            if (binding.key != target && *binding.key == candidate) {
                return binding.name;
            }
        }
        return nullptr;
    }

    // Render hotkey button
    inline void RenderHotkeyButton(const char* label, int* keyCode, const char* tooltip = nullptr)
    {
        const float dpiScale = sdl_renderer::getDpiScale();
        ImGui::Text("%s:", label);
        ImGui::SameLine(150.0f * dpiScale);

        char buttonLabel[64];
        if (isBindingKey && bindingKeyTarget == keyCode)
        {
            sprintf_s(buttonLabel, "[Press Key...]##%s", label);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.8f, 0.4f, 0.0f, 1.0f));
        }
        else
        {
            sprintf_s(buttonLabel, "%s##%s", GetKeyName(*keyCode), label);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.4f, 0.6f, 1.0f));
        }

        if (ImGui::Button(buttonLabel, ImVec2(100.0f * dpiScale, 0.0f)))
        {
            isBindingKey = true;
            bindingKeyTarget = keyCode;
            bindingKeyName = label;
            bindingWaitingForRelease = true;
            bindingError.clear();
            suppressHotkeysUntilRelease = true;
        }

        ImGui::PopStyleColor();

        if (tooltip && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", tooltip);
    }

    // Update key binding (call every frame)
    inline void UpdateKeyBinding()
    {
        if (!isBindingKey || bindingKeyTarget == nullptr)
            return;

        // Check for escape to cancel
        if (GetAsyncKeyState(VK_ESCAPE) & 0x8000)
        {
            isBindingKey = false;
            bindingKeyTarget = nullptr;
            bindingKeyName = nullptr;
            bindingWaitingForRelease = false;
            suppressHotkeysUntilRelease = true;
            return;
        }

        // Do not capture the mouse click that opened the binding button, or
        // any modifier that was already held at that moment.
        if (bindingWaitingForRelease) {
            if (!AnyBindableKeyDown()) {
                bindingWaitingForRelease = false;
            }
            return;
        }

        int pressedKey = GetPressedKey();
        if (pressedKey != 0)
        {
            if (const char* conflict =
                    FindHotkeyConflict(bindingKeyTarget, pressedKey)) {
                bindingError =
                    std::string("Already assigned to ") + conflict;
                bindingWaitingForRelease = true;
                return;
            }

            *bindingKeyTarget = pressedKey;
            isBindingKey = false;
            bindingKeyTarget = nullptr;
            bindingKeyName = nullptr;
            bindingWaitingForRelease = false;
            suppressHotkeysUntilRelease = true;
        }
    }

    inline void BeginCard(
        const char* id,
        const char* title,
        const char* subtitle,
        float height,
        float width = 0.0f)
    {
        const float dpiScale = sdl_renderer::getDpiScale();
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.070f, 0.090f, 0.125f, 1.0f));
        ImGui::PushStyleColor(
            ImGuiCol_Border,
            ImVec4(0.145f, 0.185f, 0.240f, 1.0f));
        ImGui::BeginChild(
            id,
            ImVec2(
                width > 0.0f ? width * dpiScale : 0.0f,
                height * dpiScale),
            true);
        ImGui::TextColored(
            ImVec4(0.330f, 0.800f, 1.000f, 1.0f),
            "%s",
            title);
        if (subtitle && subtitle[0] != '\0') {
            ImGui::TextColored(
                ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
                "%s",
                subtitle);
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    inline void EndCard()
    {
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
    }

    inline void StatusValue(
        const char* label,
        bool enabled,
        const char* enabledText = "ON",
        const char* disabledText = "OFF")
    {
        ImGui::TextColored(
            ImVec4(0.610f, 0.665f, 0.750f, 1.0f),
            "%s",
            label);
        ImGui::SameLine();
        ImGui::TextColored(
            enabled
                ? ImVec4(0.250f, 0.900f, 0.600f, 1.0f)
                : ImVec4(0.930f, 0.420f, 0.430f, 1.0f),
            "%s",
            enabled ? enabledText : disabledText);
    }

    inline void RenderOverview()
    {
        const float dpiScale = sdl_renderer::getDpiScale();
        const float availableWidth =
            ImGui::GetContentRegionAvail().x;
        const float gap = 10.0f * dpiScale;
        const float cardWidth =
            std::max(180.0f * dpiScale,
                (availableWidth - gap) * 0.5f);

        ImGui::BeginGroup();
        BeginCard(
            "##QuickControls",
            "Quick controls",
            "The features you are most likely to toggle mid-session.",
            240.0f,
            cardWidth / dpiScale);
        ImGui::Checkbox("Player ESP", &espEnabled);
        ImGui::Checkbox("Aimbot", &aimbotEnabled);
        ImGui::Checkbox("Triggerbot", &triggerbotEnabled);
        ImGui::Checkbox("Local map Radar", &localRadarEnabled);
        ImGui::Checkbox("Web Radar", &webRadarEnabled);
        ImGui::Checkbox("Bomb timer", &bombTimer);
        EndCard();
        ImGui::EndGroup();

        if (availableWidth >= 430.0f * dpiScale) {
            ImGui::SameLine(0.0f, gap);
        }
        ImGui::BeginGroup();
        BeginCard(
            "##SessionStatus",
            "Session status",
            "Live renderer and safety information.",
            240.0f,
            cardWidth / dpiScale);
        StatusValue(
            "Renderer",
            sdl_renderer::isAcceleratedRenderer(),
            "HARDWARE",
            "SOFTWARE");
        StatusValue(
            "Game focus",
            sdl_renderer::isGameForeground(),
            "ACTIVE",
            "PAUSED");
        StatusValue(
            "Single monitor",
            sdl_renderer::isGameOnSingleMonitor(),
            "VALID",
            "MOVE GAME");
        StatusValue(
            "Memory writes",
            memory::WritesAllowed(),
            "UNLOCKED",
            "LOCKED");
        ImGui::Spacing();
        ImGui::Text(
            "%u x %u  |  %d Hz target",
            VIEWPORT_W,
            VIEWPORT_H,
            sdl_renderer::getTargetRefreshRate());
        ImGui::Text(
            "Overlay %.0f FPS",
            ImGui::GetIO().Framerate);
        EndCard();
        ImGui::EndGroup();

        ImGui::Spacing();
        BeginCard(
            "##SafetySummary",
            "Safe operating mode",
            "Input is injected only while the CS2 client itself is foreground.",
            130.0f);
        ImGui::TextWrapped(
            "The overlay pauses entity reads when CS2 loses focus. "
            "Memory writes remain disabled unless the program was started "
            "with --allow-memory-writes.");
        EndCard();
    }

    // Render Aimbot tab content
    inline void RenderAimbotTab()
    {
        ImGui::Checkbox("Enable Aimbot", &aimbotEnabled);

        if (aimbotEnabled)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::Checkbox("Smart Aim (Auto-Lock)", &smartAimEnabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Ignore FOV, auto-aim at best spotted target\nPriority: Spotted > Distance/Health");

            if (smartAimEnabled) {
                ImGui::Indent();
                const char* priorityItems[] = { "Distance First", "Health First" };
                ImGui::Combo("Priority", &smartAimPriority, priorityItems, IM_ARRAYSIZE(priorityItems));
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Distance: Aim at closest enemy\nHealth: Aim at lowest HP enemy");
                ImGui::Unindent();
            }

            if (!smartAimEnabled) {
                ImGui::SliderFloat("FOV", &aimbotFOV, 1.0f, 30.0f, "%.1f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Field of view - only aim at enemies within this angle");
            }

            ImGui::SliderFloat("Aim Smoothing", &aimbotSmoothing, 1.0f, 20.0f, "%.1f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("1.0 = instant lock, higher = smoother/slower");

            const char* boneItems[] = { "Head", "Neck", "Chest" };
            ImGui::Combo("Target Bone", &aimbotBone, boneItems, IM_ARRAYSIZE(boneItems));

            if (!smartAimEnabled) {
                ImGui::Checkbox("Spotted Only", &aimbotVisibleOnly);
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Uses CS2's spotted flag; this is not a geometric ray-cast");
            }

            ImGui::Checkbox("Show FOV Circle", &aimbotShowFOV);
            if (aimbotShowFOV) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##FOVColor", aimbotFOVColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Head Offset");

            ImGui::Checkbox("Enable (Side-facing)", &headOffsetEnabled);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Compensate for head position when enemy is facing sideways");

            if (headOffsetEnabled) {
                ImGui::Indent();
                ImGui::SliderFloat("Offset Amount", &headOffsetAmount, 0.0f, 15.0f, "%.1f units");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("How much to offset the head position (5-8 recommended)");

                ImGui::SliderFloat("Min Angle", &headOffsetAngleMin, 0.0f, 90.0f, "%.0f deg");
                ImGui::SliderFloat("Max Angle", &headOffsetAngleMax, 90.0f, 180.0f, "%.0f deg");
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("Angle range for offset (45-135 = side-facing)");

                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "0=facing you, 90=side, 180=back");
                ImGui::Unindent();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Input");
            ImGui::SliderFloat("Mouse Sensitivity", &mouseSensitivity, 0.1f, 10.0f, "%.2f");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Match your in-game mouse sensitivity");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Hold %s to aim", GetKeyName(aimbotKey));
        }
    }

    // Render Triggerbot tab content
    inline void RenderTriggerbotTab()
    {
        ImGui::Checkbox("Enable Triggerbot", &triggerbotEnabled);

        if (triggerbotEnabled)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            ImGui::SliderInt("Delay (ms)", &triggerbotDelay, 0, 500, "%d ms");
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Delay before shooting (milliseconds)");

            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Hold %s to activate", GetKeyName(triggerbotKey));
            ImGui::TextColored(
                ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                "Fires only when the crosshair is on a live enemy.");
        }
    }

    // Render ESP tab content
    inline void RenderESPTab()
    {
        ImGui::Checkbox("Enable ESP", &espEnabled);

        if (espEnabled)
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Box ESP
            ImGui::Checkbox("Box ESP", &espBox);
            if (espBox) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##BoxColor", espBoxColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
            }

            // Health Bar
            ImGui::Checkbox("Health Bar", &espHealth);

            // Weapon Display
            ImGui::Checkbox("Weapon", &espWeapon);
            if (espWeapon) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##WeaponColor", espWeaponColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
            }

            // View Direction
            ImGui::Checkbox("View Direction (Box Color)", &espViewAngle);
            if (espViewAngle) {
                ImGui::Indent();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Facing You: RED");
                ImGui::TextColored(ImVec4(1.0f, 0.65f, 0.0f, 1.0f), "Partial: ORANGE");
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Side: YELLOW");
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Back: GREEN");
                ImGui::Checkbox("Show Angle Degrees", &espViewAngleText);
                ImGui::Unindent();
            }

            // CS2 spotted-state check. This is intentionally not described as
            // a ray-cast: it is a conservative game-state signal.
            ImGui::Checkbox("Spotted Check (Triangle)", &espWallCheck);
            if (espWallCheck) {
                ImGui::Indent();
                ImGui::Text("Spotted Color:");
                ImGui::SameLine();
                ImGui::ColorEdit4("##BoxColor2", espBoxColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
                ImGui::Text("Not Spotted / Unknown:");
                ImGui::SameLine();
                ImGui::ColorEdit4("##WallColor", espWallColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
                ImGui::TextColored(
                    ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                    "Uses CS2 spotted state; never assumes distant targets visible.");
                ImGui::Unindent();
            }

            // Distance
            ImGui::Checkbox("Distance", &espDistance);
            if (espDistance) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##DistanceColor", espDistanceColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
            }

            // Flashbang Eye Indicator
            ImGui::Checkbox("Flashbang Eye Indicator", &espFlashIndicator);
            if (espFlashIndicator) {
                ImGui::Indent();
                ImGui::Text("Normal Eye:");
                ImGui::SameLine();
                ImGui::ColorEdit4("##FlashNormalColor", espFlashNormalColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
                ImGui::Text("Flashed Eye:");
                ImGui::SameLine();
                ImGui::ColorEdit4("##FlashColor", espFlashColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
                ImGui::Unindent();
            }

            // Snaplines
            ImGui::Checkbox("Snaplines", &espSnaplines);
            if (espSnaplines) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##SnaplinesColor", espSnaplinesColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
                ImGui::Indent();
                const char* origins[] = { "Bottom", "Center", "Top" };
                ImGui::Combo("Origin", &snaplinesOrigin, origins, IM_ARRAYSIZE(origins));
                ImGui::Unindent();
            }

            // Skeleton
            ImGui::Checkbox("Skeleton", &espSkeleton);
            if (espSkeleton) {
                ImGui::SameLine();
                ImGui::ColorEdit4("##SkeletonColor", espSkeletonColor, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel | ImGuiColorEditFlags_NoAlpha);
            }
        }
    }

    // Local and browser modes consume the same complete GameSnapshot and map
    // catalogue. Neither mode rotates or distance-crops the north-up map.
    inline void RenderRadarTab()
    {
        ImGui::TextColored(
            ImVec4(0.330f, 0.800f, 1.000f, 1.0f),
            "LOCAL FIXED-MAP OVERLAY");
        ImGui::TextWrapped(
            "Draws the complete north-up map inside the game overlay. It "
            "uses the same images and calibration as Web Radar; only player "
            "direction markers rotate.");
        ImGui::Spacing();

        ImGui::Checkbox("Enable local map overlay", &localRadarEnabled);
        if (localRadarEnabled) {
            ImGui::Checkbox("Show player names", &localRadarShowNames);
            ImGui::SliderFloat(
                "Horizontal position",
                &localRadarAnchorX,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::SliderFloat(
                "Vertical position",
                &localRadarAnchorY,
                0.0f,
                1.0f,
                "%.2f");
            ImGui::SliderFloat(
                "Map size",
                &localRadarSize,
                0.18f,
                0.65f,
                "%.2f");
            ImGui::SliderFloat(
                "Player marker size",
                &localRadarMarkerSize,
                6.0f,
                24.0f,
                "%.0f px");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(
            ImVec4(0.330f, 0.800f, 1.000f, 1.0f),
            "EMBEDDED BROWSER RADAR");
        ImGui::TextWrapped(
            "Serves the same fixed map through the embedded CivetWeb service "
            "for a local browser or trusted LAN viewers.");
        ImGui::Spacing();

        ImGui::Checkbox("Enable Web Radar", &webRadarEnabled);
        ImGui::InputInt("HTTP port", &webRadarPort, 1, 100);
        webRadarPort = std::clamp(webRadarPort, 1024, 65535);
        ImGui::Checkbox("Allow viewers on this LAN", &webRadarLanAccess);

        ImGui::Checkbox(
            "Pause browser/Relay sampling when CS2 loses focus",
            &webRadarPauseWhenUnfocused);
        ImGui::TextWrapped(
            "The local overlay always pauses when CS2 is unfocused; only "
            "explicitly shared viewers can opt into background sampling.");
        ImGui::Checkbox(
            "Share player names",
            &webRadarIncludePlayerNames);
        const char* teamPolicies[] = {
            "All teams",
            "Local team only",
            "Opponents only"
        };
        ImGui::Combo(
            "Shared teams",
            &webRadarTeamViewPolicy,
            teamPolicies,
            IM_ARRAYSIZE(teamPolicies));
        ImGui::Checkbox(
            "Share Steam IDs (profile links)",
            &webRadarIncludeSteamIds);

        if (webRadarLanAccess) {
            ImGui::Spacing();
            ImGui::TextColored(
                ImVec4(0.930f, 0.650f, 0.260f, 1.0f),
                "LAN MODE");
            ImGui::TextWrapped(
                "Anyone who receives the tokenized URL can view the stream. "
                "Only use it on a trusted private network; do not expose the "
                "port to the internet.");
        }

        const WebRadarUiStatus status = getWebRadarStatus();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Service: %s", status.running ? "RUNNING" : "STOPPED");
        ImGui::Text("Bind: %s:%d", status.bindAddress.c_str(), webRadarPort);
        ImGui::Text("Viewers: %zu", status.viewers);
        ImGui::Text(
            "Frames: %llu published | %llu sent | %llu replaced",
            static_cast<unsigned long long>(status.publishedFrames),
            static_cast<unsigned long long>(status.sentFrames),
            static_cast<unsigned long long>(status.replacedFrames));
        ImGui::Text(
            "Traffic: %.1f MB | max send latency %.1f ms",
            static_cast<double>(status.publishedBytes) /
                (1024.0 * 1024.0),
            status.maximumSendLatencyMilliseconds);

        if (!status.error.empty()) {
            ImGui::TextColored(
                ImVec4(0.930f, 0.420f, 0.430f, 1.0f),
                "Error: %s",
                status.error.c_str());
        }

        const bool canOpen = status.running && !status.viewerUrl.empty();
        ImGui::BeginDisabled(!canOpen);
        if (ImGui::Button("Open Radar")) {
            ShellExecuteA(
                nullptr,
                "open",
                status.viewerUrl.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy viewer URL")) {
            ImGui::SetClipboardText(status.viewerUrl.c_str());
        }
        ImGui::EndDisabled();

        if (canOpen) {
            ImGui::TextWrapped("%s", status.viewerUrl.c_str());
            if (webRadarLanAccess) {
                ImGui::TextWrapped(
                    "For another device, replace 127.0.0.1 in this URL with "
                    "this PC's private LAN IPv4 address.");
            }
        }

        ImGui::Spacing();
        ImGui::Checkbox(
            "Record sanitized Radar snapshots",
            &radarRecordingEnabled);
        const web_radar::SnapshotRecorderStatus recording =
            getRecorderStatus();
        if (recording.recording) {
            ImGui::Text(
                "Recording: %llu frames (%.1f MB), %llu replaced",
                static_cast<unsigned long long>(recording.framesWritten),
                static_cast<double>(recording.bytesWritten) /
                    (1024.0 * 1024.0),
                static_cast<unsigned long long>(recording.replacedFrames));
        }
        if (!recording.path.empty()) {
            ImGui::TextWrapped("File: %s", recording.path.c_str());
        }
        if (!recording.lastError.empty()) {
            ImGui::TextColored(
                ImVec4(0.930f, 0.420f, 0.430f, 1.0f),
                "%s",
                recording.lastError.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(
            ImVec4(0.330f, 0.800f, 1.000f, 1.0f),
            "PUBLIC RELAY (OUTBOUND WSS)");
        ImGui::TextWrapped(
            "Publishes snapshots through an authenticated, TLS-protected "
            "outbound connection. No inbound port or LAN mode is required.");
        ImGui::Checkbox("Enable Public Relay", &publicRelayEnabled);

        ImGui::BeginDisabled(publicRelayEnabled);
        ImGui::InputTextWithHint(
            "Relay WSS URL",
            "wss://radar.example.com/api/v1/publish",
            publicRelayUrl.data(),
            publicRelayUrl.size(),
            ImGuiInputTextFlags_CharsNoBlank |
                ImGuiInputTextFlags_AutoSelectAll);
        ImGui::InputTextWithHint(
            "Relay room",
            "match-room",
            publicRelayRoom.data(),
            publicRelayRoom.size(),
            ImGuiInputTextFlags_CharsNoBlank |
                ImGuiInputTextFlags_AutoSelectAll);
        ImGui::InputTextWithHint(
            "Producer token",
            "Paste the producer-only token",
            publicRelayToken.data(),
            publicRelayToken.size(),
            ImGuiInputTextFlags_Password |
                ImGuiInputTextFlags_CharsNoBlank |
                ImGuiInputTextFlags_AutoSelectAll);
        if (ImGui::Button("Clear Relay credentials")) {
            std::fill(publicRelayUrl.begin(), publicRelayUrl.end(), '\0');
            std::fill(publicRelayRoom.begin(), publicRelayRoom.end(), '\0');
            SecureZeroMemory(
                publicRelayToken.data(),
                publicRelayToken.size());
        }
        ImGui::EndDisabled();

        ImGui::Checkbox(
            "Share player names through Public Relay",
            &publicRelayIncludePlayerNames);
        ImGui::Combo(
            "Relay teams",
            &publicRelayTeamViewPolicy,
            teamPolicies,
            IM_ARRAYSIZE(teamPolicies));
        ImGui::Checkbox(
            "Share Steam IDs through Public Relay",
            &publicRelayIncludeSteamIds);
        ImGui::TextWrapped(
            "The producer token is kept in memory only and is never shown in "
            "status or logs. Use a producer token, never a viewer token.");

        const PublicRelayUiStatus relayStatus = getPublicRelayStatus();
        const char* relayState = "DISABLED";
        switch (relayStatus.state) {
        case web_radar::PublicRelayState::connecting:
            relayState = "CONNECTING";
            break;
        case web_radar::PublicRelayState::connected:
            relayState = "CONNECTED";
            break;
        case web_radar::PublicRelayState::backoff:
            relayState = "RETRY BACKOFF";
            break;
        case web_radar::PublicRelayState::retiring:
            relayState = "STOPPING";
            break;
        case web_radar::PublicRelayState::failed:
            relayState = "FAILED";
            break;
        case web_radar::PublicRelayState::disabled:
            break;
        }
        ImGui::Text("Relay: %s", relayState);
        ImGui::Text(
            "Frames sent: %llu  |  Replaced: %llu  |  Dropped: %llu  |  Reconnects: %llu",
            static_cast<unsigned long long>(relayStatus.framesSent),
            static_cast<unsigned long long>(relayStatus.replacedFrames),
            static_cast<unsigned long long>(relayStatus.droppedFrames),
            static_cast<unsigned long long>(relayStatus.reconnects));
        if (!relayStatus.error.empty()) {
            ImGui::TextColored(
                ImVec4(0.930f, 0.420f, 0.430f, 1.0f),
                "Relay error: %s",
                relayStatus.error.c_str());
        }
    }

    // Render Hotkeys tab content
    inline void RenderHotkeysTab()
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Key Bindings");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        RenderHotkeyButton("Menu Toggle", &menuToggleKey, "Key to show/hide menu");
        RenderHotkeyButton("Exit Program", &exitKey, "Key to exit the program");
        RenderHotkeyButton("Aimbot Key", &aimbotKey, "Hold to activate aimbot");
        RenderHotkeyButton("Triggerbot Key", &triggerbotKey, "Hold to activate triggerbot");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Click button and press any key to bind");
        ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Press ESC to cancel binding");
        if (!bindingError.empty()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.35f, 0.35f, 1.0f),
                "%s",
                bindingError.c_str());
        }
    }

    // Render Settings tab content
    inline void RenderMiscTab()
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Misc Features");
        ImGui::Separator();
        ImGui::Spacing();

        if (!memory::WritesAllowed()) {
            ImGui::BeginDisabled();
        }
        ImGui::Checkbox("Anti-Flash", &antiFlash);
        if (!memory::WritesAllowed()) {
            antiFlash = false;
            ImGui::EndDisabled();
            ImGui::TextColored(
                ImVec4(1.0f, 0.65f, 0.1f, 1.0f),
                "Memory writes locked. Start with --allow-memory-writes to enable.");
        }
        ImGui::Checkbox("Bomb Timer", &bombTimer);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "World ESP");
        ImGui::Spacing();
        ImGui::Checkbox("Grenade ESP", &grenadeESP);
        ImGui::Checkbox("Dropped Weapon ESP", &droppedWeaponESP);
    }

    inline void RenderSettingsTab()
    {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "Performance");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(
            ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
            "Overlay target: %d FPS (%s)",
            sdl_renderer::getTargetRefreshRate(),
            sdl_renderer::isVsyncEnabled()
                ? "VSync"
                : "paced fallback");
        ImGui::Text(
            "Renderer: %s",
            sdl_renderer::isAcceleratedRenderer()
                ? "Hardware accelerated"
                : "Software fallback (limited to 60 FPS)");
        if (!sdl_renderer::isGameOnSingleMonitor()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.5f, 0.1f, 1.0f),
                "Move CS2 fully onto one monitor for reliable mixed-DPI mapping.");
        }
        if (!sdl_renderer::isDpiAwarenessReliable()) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.25f, 0.25f, 1.0f),
                "Per-monitor DPI awareness is unavailable.");
        }
        const char* viewportModes[] = {
            "Auto-detect black bars",
            "Full client (stretched)",
            "Force 4:3 black bars",
            "Force 16:10 black bars"
        };
        ImGui::Combo(
            "Game Viewport",
            &viewportMode,
            viewportModes,
            IM_ARRAYSIZE(viewportModes));
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Auto is recommended. Use a forced mode only if a capture-"
                "protected or very dark scene prevents black-bar detection.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.0f, 1.0f), "System Info");
        ImGui::Spacing();

        ImGui::Text("Resolution: %dx%d", WIDTH, HEIGHT);
        ImGui::Text(
            "Game viewport: %dx%d at (%d, %d)",
            VIEWPORT_W,
            VIEWPORT_H,
            VIEWPORT_X,
            VIEWPORT_Y);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        const float frameRate = ImGui::GetIO().Framerate;
        ImGui::Text(
            "Frame Time: %.3f ms",
            frameRate > 0.0f ? 1000.0f / frameRate : 0.0f);

        const auto samplingMetrics =
            performance_metrics::samplingDuration.snapshot();
        const auto serializationMetrics =
            performance_metrics::serializationDuration.snapshot();
        const auto renderMetrics =
            performance_metrics::renderCpuDuration.snapshot();
        const memory::ReadMetrics readMetrics = memory::GetReadMetrics();
        ImGui::Text(
            "Sampling: %d Hz | Radar: %d Hz | avg %.2f ms | P95 %.2f | P99 %.2f",
            performance_metrics::samplingRateHz.load(
                std::memory_order_relaxed),
            performance_metrics::radarRateHz.load(
                std::memory_order_relaxed),
            samplingMetrics.averageMilliseconds,
            samplingMetrics.p95Milliseconds,
            samplingMetrics.p99Milliseconds);
        ImGui::Text(
            "Render CPU: avg %.2f ms | P95 %.2f | P99 %.2f",
            renderMetrics.averageMilliseconds,
            renderMetrics.p95Milliseconds,
            renderMetrics.p99Milliseconds);
        ImGui::Text(
            "Radar JSON: avg %.2f ms | P95 %.2f | max %.2f",
            serializationMetrics.averageMilliseconds,
            serializationMetrics.p95Milliseconds,
            serializationMetrics.maximumMilliseconds);
        ImGui::Text(
            "RPM: %llu calls | %.1f MB | %llu failed",
            static_cast<unsigned long long>(readMetrics.calls),
            static_cast<double>(readMetrics.bytesRequested) /
                (1024.0 * 1024.0),
            static_cast<unsigned long long>(readMetrics.failures));
        ImGui::Text(
            "Missed deadlines: sample %llu | render %llu",
            static_cast<unsigned long long>(
                performance_metrics::missedSamplingDeadlines.load(
                    std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                performance_metrics::missedRenderDeadlines.load(
                    std::memory_order_relaxed)));

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(
            startupReport.ready()
                ? ImVec4(0.250f, 0.900f, 0.600f, 1.0f)
                : ImVec4(0.930f, 0.420f, 0.430f, 1.0f),
            "Startup self-check: %s",
            startupReport.ready() ? "READY" : "ATTENTION");
        ImGui::Text(
            "Admin %s | SDL %s | Web bundle %s | Maps %s",
            startupReport.administrator ? "OK" : "FAIL",
            startupReport.sdlRuntimePresent ? "OK" : "FAIL",
            startupReport.webRadarBundlePresent ? "OK" : "FAIL",
            startupReport.mapMetadataPresent ? "OK" : "FAIL");
        if (!startupReport.installationError.empty()) {
            ImGui::TextWrapped(
                "%s",
                startupReport.installationError.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "CS2 External ESP v2.0");
        ImGui::Text("SDL2 + ImGui Overlay");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "github.com/tiansongyu/cs2_cheat");
    }

    inline void RenderPageHeader(
        const char* title,
        const char* description)
    {
        ImGui::TextColored(
            ImVec4(0.930f, 0.960f, 1.000f, 1.0f),
            "%s",
            title);
        ImGui::TextColored(
            ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
            "%s",
            description);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    inline void RenderCombatPage()
    {
        RenderPageHeader(
            "Combat assistance",
            "Target selection and input automation. All input is focus-gated.");
        BeginCard(
            "##AimbotCard",
            "Aimbot",
            "Real bone targets with stable target retention.",
            590.0f);
        RenderAimbotTab();
        EndCard();
        ImGui::Spacing();
        BeginCard(
            "##TriggerCard",
            "Triggerbot",
            "Uses the actual entity under the crosshair; no angular guessing.",
            190.0f);
        RenderTriggerbotTab();
        EndCard();
    }

    inline void RenderPlayerVisualsPage()
    {
        RenderPageHeader(
            "Player visuals",
            "Configure information drawn around validated live enemy pawns.");
        BeginCard(
            "##PlayerEspCard",
            "Player ESP",
            "Boxes, health, skeleton, equipment and threat direction.",
            650.0f);
        RenderESPTab();
        EndCard();
    }

    inline void RenderWorldPage()
    {
        RenderPageHeader(
            "World and match",
            "Shared Web Radar, bomb state and moving world entities.");
        BeginCard(
            "##RadarCard",
            "Fixed-map Radar",
            "One north-up map model for the local overlay, CivetWeb and Relay.",
            870.0f);
        RenderRadarTab();
        EndCard();
        ImGui::Spacing();
        BeginCard(
            "##WorldUtilityCard",
            "Match utilities",
            "Bomb timer, projectiles, dropped equipment and anti-flash.",
            255.0f);
        RenderMiscTab();
        EndCard();
    }

    inline void RenderSystemPage()
    {
        RenderPageHeader(
            "System",
            "Display mapping, performance diagnostics and key bindings.");
        BeginCard(
            "##DisplayCard",
            "Display and renderer",
            "Monitor-aware viewport mapping and live diagnostics.",
            460.0f);
        RenderSettingsTab();
        EndCard();
        ImGui::Spacing();
        BeginCard(
            "##HotkeyCard",
            "Hotkeys",
            "Bindings must be unique; input pauses while rebinding.",
            330.0f);
        RenderHotkeysTab();
        EndCard();
    }

    inline bool NavigationButton(
        const char* label,
        int page,
        const ImVec2& size)
    {
        const bool selected = currentTab == page;
        if (selected) {
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImVec4(0.075f, 0.330f, 0.470f, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(0.085f, 0.390f, 0.540f, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImVec4(0.100f, 0.440f, 0.600f, 1.0f));
        } else {
            ImGui::PushStyleColor(
                ImGuiCol_Button,
                ImVec4(0.055f, 0.072f, 0.100f, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonHovered,
                ImVec4(0.095f, 0.130f, 0.175f, 1.0f));
            ImGui::PushStyleColor(
                ImGuiCol_ButtonActive,
                ImVec4(0.110f, 0.155f, 0.205f, 1.0f));
        }

        const bool pressed = ImGui::Button(label, size);
        ImGui::PopStyleColor(3);
        if (pressed) {
            currentTab = page;
        }
        return pressed;
    }

    // Main render function
    inline void render()
    {
        if (!sdl_renderer::menuVisible) return;

        // Update key binding
        UpdateKeyBinding();

        const float dpiScale = sdl_renderer::getDpiScale();
        const float margin = std::max(8.0f, 16.0f * dpiScale);
        const float availableWidth =
            std::max(1.0f, static_cast<float>(WIDTH) - margin * 2.0f);
        const float availableHeight =
            std::max(1.0f, static_cast<float>(HEIGHT) - margin * 2.0f);
        const float defaultWidth =
            std::min(920.0f * dpiScale, availableWidth);
        const float defaultHeight =
            std::min(720.0f * dpiScale, availableHeight);
        const float minimumWidth =
            std::min(680.0f * dpiScale, availableWidth);
        const float minimumHeight =
            std::min(500.0f * dpiScale, availableHeight);

        ImGui::SetNextWindowSizeConstraints(
            ImVec2(minimumWidth, minimumHeight),
            ImVec2(availableWidth, availableHeight));
        ImGui::SetNextWindowSize(
            ImVec2(defaultWidth, defaultHeight),
            ImGuiCond_FirstUseEver
        );
        ImGui::SetNextWindowPos(
            ImVec2(
                (static_cast<float>(WIDTH) - defaultWidth) / 2.0f,
                (static_cast<float>(HEIGHT) - defaultHeight) / 2.0f),
            ImGuiCond_FirstUseEver
        );

        ImGui::Begin(
            "Aegis // CS2 Overlay",
            nullptr,
            ImGuiWindowFlags_NoCollapse);

        // Saved ImGui positions may belong to another monitor/resolution.
        // Clamp without resetting a valid user-selected position or size.
        const ImVec2 windowPos = ImGui::GetWindowPos();
        const ImVec2 windowSize = ImGui::GetWindowSize();
        const ImVec2 clampedPos(
            std::clamp(
                windowPos.x,
                margin,
                std::max(margin, static_cast<float>(WIDTH) - windowSize.x - margin)),
            std::clamp(
                windowPos.y,
                margin,
                std::max(margin, static_cast<float>(HEIGHT) - windowSize.y - margin)));
        if (clampedPos.x != windowPos.x || clampedPos.y != windowPos.y) {
            ImGui::SetWindowPos(clampedPos);
        }
        {
            const ImVec2 interactivePosition = ImGui::GetWindowPos();
            const ImVec2 interactiveSize = ImGui::GetWindowSize();
            sdl_renderer::setInteractiveRect(
                interactivePosition.x,
                interactivePosition.y,
                interactiveSize.x,
                interactiveSize.y);
        }

        const float sidebarWidth = 184.0f * dpiScale;
        const float navigationHeight = 42.0f * dpiScale;

        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.038f, 0.052f, 0.075f, 1.0f));
        ImGui::BeginChild(
            "##Navigation",
            ImVec2(sidebarWidth, 0.0f),
            true);
        ImGui::TextColored(
            ImVec4(0.330f, 0.800f, 1.000f, 1.0f),
            "AEGIS");
        ImGui::TextColored(
            ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
            "CS2 OVERLAY");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const ImVec2 navigationSize(
            ImGui::GetContentRegionAvail().x,
            navigationHeight);
        NavigationButton("Overview", 0, navigationSize);
        NavigationButton("Combat", 1, navigationSize);
        NavigationButton("Player visuals", 2, navigationSize);
        NavigationButton("World & Radar", 3, navigationSize);
        NavigationButton("System", 4, navigationSize);

        const float footerHeight = 104.0f * dpiScale;
        if (ImGui::GetContentRegionAvail().y > footerHeight) {
            ImGui::SetCursorPosY(
                ImGui::GetWindowHeight() - footerHeight);
        }
        ImGui::Separator();
        ImGui::TextColored(
            sdl_renderer::isGameForeground()
                ? ImVec4(0.250f, 0.900f, 0.600f, 1.0f)
                : ImVec4(0.930f, 0.650f, 0.260f, 1.0f),
            sdl_renderer::isGameForeground()
                ? "GAME ACTIVE"
                : "INPUT PAUSED");
        ImGui::TextColored(
            ImVec4(0.500f, 0.570f, 0.670f, 1.0f),
            "%s menu  |  %s exit",
            GetKeyName(menuToggleKey),
            GetKeyName(exitKey));
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImGui::SameLine(0.0f, 12.0f * dpiScale);
        ImGui::BeginChild(
            "##PageContent",
            ImVec2(0.0f, 0.0f),
            false);
        switch (currentTab) {
        case 1:
            RenderCombatPage();
            break;
        case 2:
            RenderPlayerVisualsPage();
            break;
        case 3:
            RenderWorldPage();
            break;
        case 4:
            RenderSystemPage();
            break;
        default:
            currentTab = 0;
            RenderPageHeader(
                "Overview",
                "Quick controls and a live view of the current session.");
            RenderOverview();
            break;
        }
        ImGui::EndChild();

        ImGui::End();
        if (ImGui::IsPopupOpen(
                nullptr,
                ImGuiPopupFlags_AnyPopup)) {
            // Popups may extend beyond the main menu rectangle. Keep the whole
            // overlay interactive while one is open so their first click cannot
            // pass through to CS2.
            sdl_renderer::setInteractiveRect(
                0.0f,
                0.0f,
                static_cast<float>(WIDTH),
                static_cast<float>(HEIGHT));
        }
        publishRuntimeConfig();
    }
}
