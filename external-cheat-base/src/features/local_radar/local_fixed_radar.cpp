#include "features/local_radar/local_fixed_radar.hpp"

#include "core/game/fixed_map_catalog.hpp"
#include "core/game/fixed_map_radar.hpp"
#include "core/renderer/sdl_renderer.h"
#include "imgui.h"

#include <objbase.h>
#include <wincodec.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace
{
    constexpr std::uint64_t MAX_PNG_FILE_BYTES = 16U * 1024U * 1024U;
    constexpr std::uint64_t MAX_DECODED_BYTES = 64U * 1024U * 1024U;
    constexpr UINT MAX_TEXTURE_DIMENSION = 4096;
    constexpr UINT RADAR_TEXTURE_DIMENSION = 1024;
    constexpr std::uint64_t STALE_AFTER_MILLISECONDS = 1500;

    template <typename Interface>
    class ComObject final
    {
    public:
        ComObject() = default;
        ~ComObject()
        {
            reset();
        }

        ComObject(const ComObject&) = delete;
        ComObject& operator=(const ComObject&) = delete;

        [[nodiscard]] Interface* get() const noexcept
        {
            return value_;
        }

        [[nodiscard]] Interface** receive() noexcept
        {
            reset();
            return &value_;
        }

        void reset() noexcept
        {
            if (value_) {
                value_->Release();
                value_ = nullptr;
            }
        }

    private:
        Interface* value_ = nullptr;
    };

    class ScopedComInitialization final
    {
    public:
        ScopedComInitialization() noexcept
            : result_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)),
              ownsInitialization_(result_ == S_OK || result_ == S_FALSE)
        {
        }

        ~ScopedComInitialization()
        {
            if (ownsInitialization_) {
                CoUninitialize();
            }
        }

        [[nodiscard]] bool usable() const noexcept
        {
            // COM objects remain usable when another apartment model was
            // already selected for this thread.
            return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
        }

    private:
        HRESULT result_{};
        bool ownsInitialization_{};
    };

    struct TextureCache
    {
        SDL_Texture* texture = nullptr;
        std::uint64_t rendererRevision = 0;
        std::string imagePath;
        std::string error;
        std::chrono::steady_clock::time_point retryAfter{};
    };

    TextureCache textureCache;

    std::filesystem::path executableDirectory()
    {
        std::array<wchar_t, 32768> buffer{};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            buffer.data(),
            static_cast<DWORD>(buffer.size()));
        if (length == 0 || length >= buffer.size()) {
            return std::filesystem::current_path();
        }
        return std::filesystem::path(
            std::wstring_view(buffer.data(), length)).parent_path();
    }

    std::filesystem::path imageFilePath(const std::string_view relativePath)
    {
        // Catalogue paths are generated from a validated manifest and contain
        // only portable ASCII components beneath the distribution root.
        const std::wstring wideRelative(
            relativePath.begin(),
            relativePath.end());
        return executableDirectory() /
            L"web-radar" /
            L"dist" /
            std::filesystem::path(wideRelative);
    }

    SDL_Texture* loadPngTexture(
        SDL_Renderer* renderer,
        const std::filesystem::path& path,
        std::string& error)
    {
        if (!renderer) {
            error = "SDL renderer is unavailable";
            return nullptr;
        }

        std::error_code fileError;
        if (!std::filesystem::is_regular_file(path, fileError)) {
            error = "map PNG is missing from web-radar/dist";
            return nullptr;
        }
        const std::uintmax_t fileBytes =
            std::filesystem::file_size(path, fileError);
        if (fileError || fileBytes == 0 ||
            fileBytes > MAX_PNG_FILE_BYTES) {
            error = "map PNG has an invalid file size";
            return nullptr;
        }

        ScopedComInitialization com;
        if (!com.usable()) {
            error = "Windows imaging initialization failed";
            return nullptr;
        }

        ComObject<IWICImagingFactory> factory;
        HRESULT result = CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWICImagingFactory,
            reinterpret_cast<void**>(factory.receive()));
        if (FAILED(result) || !factory.get()) {
            error = "Windows Imaging Component is unavailable";
            return nullptr;
        }

        ComObject<IWICBitmapDecoder> decoder;
        result = factory.get()->CreateDecoderFromFilename(
            path.c_str(),
            nullptr,
            GENERIC_READ,
            WICDecodeMetadataCacheOnLoad,
            decoder.receive());
        if (FAILED(result) || !decoder.get()) {
            error = "map PNG could not be decoded";
            return nullptr;
        }

        ComObject<IWICBitmapFrameDecode> frame;
        result = decoder.get()->GetFrame(0, frame.receive());
        if (FAILED(result) || !frame.get()) {
            error = "map PNG contains no image frame";
            return nullptr;
        }

        UINT width = 0;
        UINT height = 0;
        result = frame.get()->GetSize(&width, &height);
        if (FAILED(result) || width == 0 || height == 0 ||
            width > MAX_TEXTURE_DIMENSION ||
            height > MAX_TEXTURE_DIMENSION) {
            error = "map PNG dimensions are invalid";
            return nullptr;
        }
        if (width != RADAR_TEXTURE_DIMENSION ||
            height != RADAR_TEXTURE_DIMENSION) {
            error = "map PNG must be 1024 x 1024";
            return nullptr;
        }

        ComObject<IWICFormatConverter> converter;
        result = factory.get()->CreateFormatConverter(converter.receive());
        if (FAILED(result) || !converter.get()) {
            error = "map pixel converter could not be created";
            return nullptr;
        }
        result = converter.get()->Initialize(
            frame.get(),
            GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom);
        if (FAILED(result)) {
            error = "map pixels could not be converted to RGBA";
            return nullptr;
        }

        const std::uint64_t stride64 =
            static_cast<std::uint64_t>(width) * 4U;
        const std::uint64_t decodedBytes64 =
            stride64 * static_cast<std::uint64_t>(height);
        if (stride64 > std::numeric_limits<UINT>::max() ||
            decodedBytes64 > MAX_DECODED_BYTES ||
            decodedBytes64 > std::numeric_limits<UINT>::max()) {
            error = "decoded map image is too large";
            return nullptr;
        }

        std::vector<std::uint8_t> pixels(
            static_cast<std::size_t>(decodedBytes64));
        result = converter.get()->CopyPixels(
            nullptr,
            static_cast<UINT>(stride64),
            static_cast<UINT>(decodedBytes64),
            pixels.data());
        if (FAILED(result)) {
            error = "map pixels could not be read";
            return nullptr;
        }

        SDL_Texture* texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STATIC,
            static_cast<int>(width),
            static_cast<int>(height));
        if (!texture) {
            error = "SDL could not create the map texture";
            return nullptr;
        }
        if (SDL_UpdateTexture(
                texture,
                nullptr,
                pixels.data(),
                static_cast<int>(stride64)) != 0 ||
            SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND) != 0 ||
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeLinear) != 0) {
            SDL_DestroyTexture(texture);
            error = "SDL could not upload the map texture";
            return nullptr;
        }
        return texture;
    }

    SDL_Texture* textureForImage(const std::string_view imagePath)
    {
        const std::uint64_t revision =
            sdl_renderer::getRendererRevision();
        if (textureCache.rendererRevision != revision) {
            // SDL_DestroyRenderer already released textures owned by the old
            // renderer. Never dereference the stale handle.
            textureCache = {};
            textureCache.rendererRevision = revision;
        }

        const auto now = std::chrono::steady_clock::now();
        if (textureCache.imagePath == imagePath &&
            (textureCache.texture || now < textureCache.retryAfter)) {
            return textureCache.texture;
        }

        if (textureCache.texture) {
            SDL_DestroyTexture(textureCache.texture);
            textureCache.texture = nullptr;
        }
        textureCache.imagePath.assign(imagePath);
        textureCache.error.clear();
        textureCache.retryAfter = {};
        textureCache.texture = loadPngTexture(
            sdl_renderer::renderer,
            imageFilePath(imagePath),
            textureCache.error);
        if (!textureCache.texture) {
            // A temporarily unavailable file or WIC service must not leave the
            // overlay permanently blank, but retrying a decoder every frame
            // would be unnecessarily expensive.
            textureCache.retryAfter = now + std::chrono::seconds(2);
        }
        return textureCache.texture;
    }

    ImU32 withAlpha(const ImU32 color, const std::uint8_t alpha)
    {
        return (color & IM_COL32(255, 255, 255, 0)) |
            (static_cast<ImU32>(alpha) << IM_COL32_A_SHIFT);
    }

    ImU32 playerColor(const game::PlayerSnapshot& player)
    {
        static constexpr std::array<ImU32, 6> competitivePalette{
            IM_COL32(154, 164, 178, 255),
            IM_COL32(90, 180, 255, 255),
            IM_COL32(184, 140, 255, 255),
            IM_COL32(114, 214, 157, 255),
            IM_COL32(255, 209, 102, 255),
            IM_COL32(255, 140, 105, 255)
        };
        if (player.competitiveColor >= 0 &&
            player.competitiveColor <
                static_cast<int>(competitivePalette.size())) {
            return competitivePalette[
                static_cast<std::size_t>(player.competitiveColor)];
        }
        if (player.team == game::Team::CounterTerrorists) {
            return IM_COL32(86, 199, 242, 255);
        }
        if (player.team == game::Team::Terrorists) {
            return IM_COL32(241, 185, 91, 255);
        }
        return IM_COL32(161, 168, 175, 255);
    }

    std::optional<float> referenceHeight(
        const game::GameSnapshot& snapshot)
    {
        if (snapshot.localPlayerId) {
            for (const game::PlayerSnapshot& player : snapshot.players) {
                if (player.id == *snapshot.localPlayerId &&
                    player.position) {
                    return player.position->z;
                }
            }
        }
        for (const game::PlayerSnapshot& player : snapshot.players) {
            if (player.alive && player.position) {
                return player.position->z;
            }
        }
        return std::nullopt;
    }

    std::optional<game::WorldPosition> bombPosition(
        const game::GameSnapshot& snapshot)
    {
        if (snapshot.bomb.position) {
            return snapshot.bomb.position;
        }
        if (snapshot.bomb.carrierPlayerId) {
            for (const game::PlayerSnapshot& player : snapshot.players) {
                if (player.id == *snapshot.bomb.carrierPlayerId) {
                    return player.position;
                }
            }
        }
        for (const game::PlayerSnapshot& player : snapshot.players) {
            if (player.hasBomb) {
                return player.position;
            }
        }
        return std::nullopt;
    }

    bool snapshotIsStale(const game::GameSnapshot& snapshot)
    {
        if (snapshot.capturedAtMs == 0) {
            return true;
        }
        const auto now = std::chrono::system_clock::now();
        const auto nowMilliseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count());
        return nowMilliseconds >= snapshot.capturedAtMs &&
            nowMilliseconds - snapshot.capturedAtMs >
                STALE_AFTER_MILLISECONDS;
    }

    void drawCenteredMessage(
        ImDrawList* drawList,
        const ImVec2 minimum,
        const ImVec2 maximum,
        const char* title,
        const std::string_view detail)
    {
        const ImVec2 titleSize = ImGui::CalcTextSize(title);
        const ImVec2 center(
            (minimum.x + maximum.x) * 0.5f,
            (minimum.y + maximum.y) * 0.5f);
        drawList->AddText(
            ImVec2(center.x - titleSize.x * 0.5f, center.y - 14.0f),
            IM_COL32(225, 232, 237, 255),
            title);
        if (!detail.empty()) {
            const std::string detailText(detail);
            const ImVec2 detailSize = ImGui::CalcTextSize(detailText.c_str());
            drawList->AddText(
                ImVec2(center.x - detailSize.x * 0.5f, center.y + 8.0f),
                IM_COL32(145, 157, 166, 255),
                detailText.c_str());
        }
    }

    void drawAlivePlayer(
        ImDrawList* drawList,
        const ImVec2 center,
        const std::optional<float> yaw,
        const float radius,
        const ImU32 color,
        const bool local)
    {
        if (yaw && std::isfinite(*yaw)) {
            const game::fixed_map_radar::ScreenDirection direction =
                game::fixed_map_radar::yawToScreenDirection(*yaw);
            const ImVec2 forward(
                direction.x,
                direction.y);
            const ImVec2 right(-forward.y, forward.x);
            const ImVec2 outlineTip(
                center.x + forward.x * radius * 2.0f,
                center.y + forward.y * radius * 2.0f);
            const ImVec2 outlineBase(
                center.x + forward.x * radius * 0.45f,
                center.y + forward.y * radius * 0.45f);
            drawList->AddTriangleFilled(
                outlineTip,
                ImVec2(
                    outlineBase.x + right.x * radius * 0.72f,
                    outlineBase.y + right.y * radius * 0.72f),
                ImVec2(
                    outlineBase.x - right.x * radius * 0.72f,
                    outlineBase.y - right.y * radius * 0.72f),
                IM_COL32(11, 15, 18, 255));

            const ImVec2 tip(
                center.x + forward.x * radius * 1.78f,
                center.y + forward.y * radius * 1.78f);
            const ImVec2 base(
                center.x + forward.x * radius * 0.50f,
                center.y + forward.y * radius * 0.50f);
            drawList->AddTriangleFilled(
                tip,
                ImVec2(
                    base.x + right.x * radius * 0.48f,
                    base.y + right.y * radius * 0.48f),
                ImVec2(
                    base.x - right.x * radius * 0.48f,
                    base.y - right.y * radius * 0.48f),
                color);
        }
        drawList->AddCircleFilled(
            center,
            radius + 2.0f,
            IM_COL32(11, 15, 18, 255),
            20);
        drawList->AddCircleFilled(center, radius, color, 20);
        drawList->AddCircle(
            center,
            radius,
            IM_COL32(235, 242, 246, 215),
            20,
            1.0f);
        if (local) {
            drawList->AddCircle(
                center,
                radius + 3.5f,
                IM_COL32(255, 255, 255, 255),
                24,
                2.0f);
        }
    }

    void drawDeadPlayer(
        ImDrawList* drawList,
        const ImVec2 center,
        const float radius)
    {
        const float arm = radius * 0.9f;
        drawList->AddLine(
            ImVec2(center.x - arm, center.y - arm),
            ImVec2(center.x + arm, center.y + arm),
            IM_COL32(173, 181, 186, 230),
            2.0f);
        drawList->AddLine(
            ImVec2(center.x + arm, center.y - arm),
            ImVec2(center.x - arm, center.y + arm),
            IM_COL32(173, 181, 186, 230),
            2.0f);
    }
}

void local_fixed_radar::render(
    const game::GameSnapshot& snapshot,
    const RenderConfig& config)
{
    if (VIEWPORT_W == 0 || VIEWPORT_H == 0) {
        return;
    }

    const float viewportWidth = static_cast<float>(VIEWPORT_W);
    const float viewportHeight = static_cast<float>(VIEWPORT_H);
    const float side = std::clamp(
        config.sizeFraction,
        0.18f,
        0.65f) * std::min(viewportWidth, viewportHeight);
    const float availableX = std::max(0.0f, viewportWidth - side);
    const float availableY = std::max(0.0f, viewportHeight - side);
    const float left = static_cast<float>(VIEWPORT_X) +
        std::clamp(config.anchorX, 0.0f, 1.0f) * availableX;
    const float top = static_cast<float>(VIEWPORT_Y) +
        std::clamp(config.anchorY, 0.0f, 1.0f) * availableY;
    const ImVec2 minimum(left, top);
    const ImVec2 maximum(left + side, top + side);

    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    drawList->AddRectFilled(
        minimum,
        maximum,
        IM_COL32(10, 14, 17, 255));

    const game::fixed_map_radar::MapDefinition* map =
        game::fixed_map_catalog::find(snapshot.map.id);
    if (!snapshot.map.connected || snapshot.map.id.empty()) {
        drawCenteredMessage(
            drawList,
            minimum,
            maximum,
            "WAITING FOR MAP",
            "Game snapshot is not connected");
        drawList->AddRect(
            minimum,
            maximum,
            IM_COL32(170, 186, 196, 180));
        return;
    }
    if (!map) {
        drawCenteredMessage(
            drawList,
            minimum,
            maximum,
            "MAP NOT AVAILABLE",
            snapshot.map.id);
        drawList->AddRect(
            minimum,
            maximum,
            IM_COL32(170, 186, 196, 180));
        return;
    }

    const std::optional<float> referenceZ = referenceHeight(snapshot);
    const std::optional<std::size_t> levelIndex = referenceZ
        ? game::fixed_map_radar::selectLevel(*map, *referenceZ)
        : std::nullopt;
    const std::string& imagePath = levelIndex
        ? map->levels[*levelIndex].imagePath
        : map->imagePath;
    SDL_Texture* texture = textureForImage(imagePath);
    if (texture) {
        const ImTextureID textureId = static_cast<ImTextureID>(
            reinterpret_cast<std::uintptr_t>(texture));
        drawList->AddImage(
            ImTextureRef(textureId),
            minimum,
            maximum);
    } else {
        drawCenteredMessage(
            drawList,
            minimum,
            maximum,
            "MAP IMAGE UNAVAILABLE",
            textureCache.error);
    }

    drawList->PushClipRect(minimum, maximum, true);

    const float markerRadius = std::clamp(
        config.markerSize,
        6.0f,
        24.0f) *
        std::clamp(side / 420.0f, 0.72f, 1.35f) * 0.5f;
    for (const game::PlayerSnapshot& player : snapshot.players) {
        if (!player.position ||
            (player.team != game::Team::Terrorists &&
             player.team != game::Team::CounterTerrorists)) {
            continue;
        }
        const game::fixed_map_radar::NormalizedPosition point =
            game::fixed_map_radar::project(*map, *player.position);
        if (!point.valid || !point.inside) {
            continue;
        }

        const ImVec2 center(
            minimum.x + point.x * side,
            minimum.y + point.y * side);
        ImU32 color = playerColor(player);
        if (player.dormant) {
            color = withAlpha(color, 115);
        }
        const bool local = snapshot.localPlayerId &&
            player.id == *snapshot.localPlayerId;
        if (player.alive) {
            drawAlivePlayer(
                drawList,
                center,
                player.yaw,
                markerRadius,
                color,
                local);
        } else {
            drawDeadPlayer(drawList, center, markerRadius);
        }

        if (levelIndex) {
            const auto& level = map->levels[*levelIndex];
            const char* floorMarker = nullptr;
            if (player.position->z < level.minimumZ) {
                floorMarker = "v";
            } else if (player.position->z >= level.maximumZ) {
                floorMarker = "^";
            }
            if (floorMarker) {
                drawList->AddText(
                    ImVec2(
                        center.x + markerRadius + 2.0f,
                        center.y - markerRadius - 3.0f),
                    IM_COL32(237, 242, 245, 255),
                    floorMarker);
            }
        }

        if (config.showNames && !player.name.empty()) {
            const float fontSize = std::max(8.0f, markerRadius * 1.15f);
            const ImVec2 nameSize = ImGui::CalcTextSize(player.name.c_str());
            const float nameX = std::clamp(
                center.x - nameSize.x * 0.5f,
                minimum.x + 2.0f,
                std::max(minimum.x + 2.0f, maximum.x - nameSize.x - 2.0f));
            drawList->AddText(
                ImGui::GetFont(),
                fontSize,
                ImVec2(nameX, center.y + markerRadius + 4.0f),
                IM_COL32(244, 246, 247, 255),
                player.name.c_str());
        }
    }

    const bool bombVisible =
        snapshot.bomb.state == game::BombState::Carried ||
        snapshot.bomb.state == game::BombState::Dropped ||
        snapshot.bomb.state == game::BombState::Planted;
    if (bombVisible) {
        const std::optional<game::WorldPosition> position =
            bombPosition(snapshot);
        if (position) {
            const game::fixed_map_radar::NormalizedPosition point =
                game::fixed_map_radar::project(*map, *position);
            if (point.valid && point.inside) {
                const ImVec2 center(
                    minimum.x + point.x * side,
                    minimum.y + point.y * side);
                const float radius = markerRadius * 0.85f;
                drawList->AddQuadFilled(
                    ImVec2(center.x, center.y - radius),
                    ImVec2(center.x + radius, center.y),
                    ImVec2(center.x, center.y + radius),
                    ImVec2(center.x - radius, center.y),
                    IM_COL32(255, 94, 94, 255));
                drawList->AddText(
                    ImVec2(center.x + radius + 2.0f, center.y - radius),
                    IM_COL32(255, 210, 210, 255),
                    "C4");
            }
        }
    }

    drawList->PopClipRect();

    drawList->AddRectFilled(
        minimum,
        ImVec2(maximum.x, minimum.y + 22.0f),
        IM_COL32(8, 12, 15, 218));
    drawList->AddText(
        ImVec2(minimum.x + 7.0f, minimum.y + 3.0f),
        IM_COL32(225, 234, 239, 255),
        "N ^");
    const std::string mapLabel = map->displayName.empty()
        ? snapshot.map.id
        : map->displayName;
    drawList->AddText(
        ImVec2(minimum.x + 43.0f, minimum.y + 3.0f),
        IM_COL32(225, 234, 239, 255),
        mapLabel.c_str());
    if (snapshotIsStale(snapshot)) {
        const char* stale = "STALE";
        const ImVec2 staleSize = ImGui::CalcTextSize(stale);
        drawList->AddText(
            ImVec2(maximum.x - staleSize.x - 7.0f, minimum.y + 3.0f),
            IM_COL32(255, 185, 92, 255),
            stale);
    }
    drawList->AddRect(
        minimum,
        maximum,
        IM_COL32(205, 221, 230, 210),
        0.0f,
        0,
        1.5f);
}

void local_fixed_radar::reset() noexcept
{
    if (textureCache.texture &&
        textureCache.rendererRevision ==
            sdl_renderer::getRendererRevision() &&
        sdl_renderer::renderer) {
        SDL_DestroyTexture(textureCache.texture);
    }
    textureCache = {};
}
