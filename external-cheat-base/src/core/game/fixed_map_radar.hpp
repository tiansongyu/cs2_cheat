#pragma once

#include "core/game/game_snapshot.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace game::fixed_map_radar
{
    struct MapLevel
    {
        std::string id;
        std::string imagePath;

        // Levels use [minimumZ, maximumZ). At a shared boundary, the upper
        // level therefore owns the point. Values outside all ranges select
        // the nearest valid level so an entity does not disappear.
        float minimumZ{ -std::numeric_limits<float>::max() };
        float maximumZ{ std::numeric_limits<float>::max() };
    };

    struct MapDefinition
    {
        std::string id;
        std::string displayName;
        std::string imagePath;

        // Source-style overview transform:
        // pixelX = (worldX - originX) / scale
        // pixelY = (originY - worldY) / scale
        float originX{};
        float originY{};
        float scale{ 1.0f };
        float pixelWidth{ 1024.0f };
        float pixelHeight{ 1024.0f };
        std::vector<MapLevel> levels;
    };

    struct NormalizedPosition
    {
        float x{};
        float y{};
        bool valid{};
        bool inside{};
        std::optional<std::size_t> levelIndex;
    };

    struct ScreenDirection
    {
        float x{};
        float y{};
        bool valid{};
    };

    [[nodiscard]] inline bool isFinite(const WorldPosition& position) noexcept
    {
        return std::isfinite(position.x)
            && std::isfinite(position.y)
            && std::isfinite(position.z);
    }

    [[nodiscard]] inline bool isValid(const MapDefinition& map) noexcept
    {
        return std::isfinite(map.originX)
            && std::isfinite(map.originY)
            && std::isfinite(map.scale)
            && std::isfinite(map.pixelWidth)
            && std::isfinite(map.pixelHeight)
            && map.scale > 0.0f
            && map.pixelWidth > 0.0f
            && map.pixelHeight > 0.0f;
    }

    [[nodiscard]] inline bool isValid(const MapLevel& level) noexcept
    {
        return std::isfinite(level.minimumZ)
            && std::isfinite(level.maximumZ)
            && level.minimumZ < level.maximumZ;
    }

    // Choose the floor background from a player who can still provide a
    // useful point of view. A dead local player must not pin a layered map to
    // the floor where they died while a live player is being observed.
    [[nodiscard]] inline std::optional<float> selectReferenceZ(
        const GameSnapshot& snapshot) noexcept
    {
        const auto validZ = [](const PlayerSnapshot& player)
            -> std::optional<float>
        {
            if (!player.position || !isFinite(*player.position))
                return std::nullopt;
            return player.position->z;
        };

        const auto findPlayer = [&snapshot](
            const std::optional<std::uint64_t>& id) -> const PlayerSnapshot*
        {
            if (!id)
                return nullptr;
            const auto player = std::find_if(
                snapshot.players.begin(),
                snapshot.players.end(),
                [id](const PlayerSnapshot& candidate)
                {
                    return candidate.id == *id;
                });
            return player == snapshot.players.end() ? nullptr : &*player;
        };

        const PlayerSnapshot* localPlayer =
            findPlayer(snapshot.localPlayerId);
        const PlayerSnapshot* observedPlayer =
            findPlayer(snapshot.observedPlayerId);

        if (localPlayer && localPlayer->alive)
        {
            if (const auto z = validZ(*localPlayer))
                return z;
        }

        if (observedPlayer && observedPlayer->alive)
        {
            if (const auto z = validZ(*observedPlayer))
                return z;
        }

        const Team referenceTeam = localPlayer &&
                (localPlayer->team == Team::Terrorists ||
                 localPlayer->team == Team::CounterTerrorists)
            ? localPlayer->team
            : snapshot.localTeam;
        if (referenceTeam == Team::Terrorists ||
            referenceTeam == Team::CounterTerrorists)
        {
            for (const PlayerSnapshot& player : snapshot.players)
            {
                if (player.alive && player.team == referenceTeam)
                {
                    if (const auto z = validZ(player))
                        return z;
                }
            }
        }

        for (const PlayerSnapshot& player : snapshot.players)
        {
            if (player.alive)
            {
                if (const auto z = validZ(player))
                    return z;
            }
        }

        // End-of-round/death-cam fallback: retain the local player's last
        // valid floor when nobody alive has a position, then use any player.
        if (localPlayer)
        {
            if (const auto z = validZ(*localPlayer))
                return z;
        }
        for (const PlayerSnapshot& player : snapshot.players)
        {
            if (const auto z = validZ(player))
                return z;
        }
        return std::nullopt;
    }

    [[nodiscard]] inline std::optional<std::size_t> selectLevel(
        const MapDefinition& map,
        float z) noexcept
    {
        if (!std::isfinite(z) || map.levels.empty())
            return std::nullopt;

        std::optional<std::size_t> nearestIndex;
        float nearestDistance = std::numeric_limits<float>::infinity();

        for (std::size_t index = 0; index < map.levels.size(); ++index)
        {
            const MapLevel& level = map.levels[index];
            if (!isValid(level))
                continue;

            if (z >= level.minimumZ && z < level.maximumZ)
                return index;

            const float distance = z < level.minimumZ
                ? level.minimumZ - z
                : z - level.maximumZ;

            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearestIndex = index;
            }
        }

        return nearestIndex;
    }

    [[nodiscard]] inline std::optional<std::size_t>
    selectLevelWithHysteresis(
        const MapDefinition& map,
        const float z,
        const std::optional<std::size_t> previousLevel,
        const float hysteresis = 24.0f) noexcept
    {
        const std::optional<std::size_t> selected = selectLevel(map, z);
        if (!selected || !previousLevel ||
            *previousLevel >= map.levels.size() ||
            *selected == *previousLevel ||
            !std::isfinite(hysteresis) || hysteresis <= 0.0f) {
            return selected;
        }

        const MapLevel& previous = map.levels[*previousLevel];
        if (!isValid(previous)) {
            return selected;
        }
        if (z >= previous.minimumZ - hysteresis &&
            z < previous.maximumZ + hysteresis) {
            return previousLevel;
        }
        return selected;
    }

    [[nodiscard]] inline float levelSelectionConfidence(
        const MapDefinition& map,
        const float z,
        const std::optional<std::size_t> levelIndex,
        const float boundaryRange = 24.0f) noexcept
    {
        if (!levelIndex || *levelIndex >= map.levels.size() ||
            !std::isfinite(z) || !std::isfinite(boundaryRange) ||
            boundaryRange <= 0.0f) {
            return 0.0f;
        }
        const MapLevel& level = map.levels[*levelIndex];
        if (!isValid(level)) {
            return 0.0f;
        }
        const float boundaryDistance = std::min(
            std::abs(z - level.minimumZ),
            std::abs(level.maximumZ - z));
        return std::clamp(boundaryDistance / boundaryRange, 0.0f, 1.0f);
    }

    [[nodiscard]] inline NormalizedPosition project(
        const MapDefinition& map,
        const WorldPosition& world) noexcept
    {
        NormalizedPosition result;
        if (!isValid(map) || !isFinite(world))
            return result;

        const float pixelX = (world.x - map.originX) / map.scale;
        const float pixelY = (map.originY - world.y) / map.scale;
        result.x = pixelX / map.pixelWidth;
        result.y = pixelY / map.pixelHeight;

        if (!std::isfinite(result.x) || !std::isfinite(result.y))
            return NormalizedPosition{};

        result.valid = true;
        result.inside = result.x >= 0.0f && result.x <= 1.0f
            && result.y >= 0.0f && result.y <= 1.0f;
        result.levelIndex = selectLevel(map, world.z);
        return result;
    }

    [[nodiscard]] inline float wrapDegrees(float degrees) noexcept
    {
        if (!std::isfinite(degrees))
            return std::numeric_limits<float>::quiet_NaN();

        float wrapped = std::fmod(degrees, 360.0f);
        if (wrapped < 0.0f)
            wrapped += 360.0f;
        if (wrapped == 0.0f)
            return 0.0f; // Normalize negative zero.
        return wrapped;
    }

    // CS yaw 0 points east and positive yaw turns counter-clockwise. CSS
    // rotation is clockwise; with an icon authored pointing up, east is 90deg.
    // spriteOffsetDegrees lets a differently-authored icon be corrected once.
    [[nodiscard]] inline float yawToCssRotation(
        float yawDegrees,
        float spriteOffsetDegrees = 0.0f) noexcept
    {
        if (!std::isfinite(yawDegrees)
            || !std::isfinite(spriteOffsetDegrees))
        {
            return std::numeric_limits<float>::quiet_NaN();
        }

        return wrapDegrees(90.0f - yawDegrees + spriteOffsetDegrees);
    }

    // On a fixed north-up map, CS yaw 0 points to screen-right/east and
    // positive yaw turns toward screen-up/north. This vector is independent
    // from the authored orientation of any icon sprite.
    [[nodiscard]] inline ScreenDirection yawToScreenDirection(
        float yawDegrees) noexcept
    {
        if (!std::isfinite(yawDegrees))
            return {};

        constexpr float degreesToRadians =
            3.14159265358979323846f / 180.0f;
        const float radians = yawDegrees * degreesToRadians;
        return ScreenDirection{
            std::cos(radians),
            -std::sin(radians),
            true
        };
    }
}
