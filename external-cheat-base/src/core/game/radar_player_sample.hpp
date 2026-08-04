#pragma once

#include "core/game/game_snapshot.hpp"

#include <cmath>
#include <cstdint>
#include <optional>

namespace game::radar_player_sample
{
    // Keep corrupted samples out of both render paths while still accepting a
    // legitimate world origin at exactly {0, 0, 0} when no better candidate
    // exists.
    inline constexpr float kWorldCoordinateLimit = 10000000.0f;
    inline constexpr float kZeroPositionEpsilon = 0.001f;
    inline constexpr float kMaximumAbsoluteYawDegrees = 1000000.0f;
    inline constexpr std::uint64_t kLastKnownMaximumAgeMs = 150;

    struct PositionRead
    {
        bool succeeded{};
        WorldPosition value{};
    };

    struct PlayerSampleIdentity
    {
        std::uint64_t playerId{};
        std::uintptr_t controllerAddress{};
        std::uintptr_t pawnAddress{};
        std::uint32_t pawnHandle{};
        std::uint8_t team{};
        std::uint64_t steamId{};
        bool steamIdKnown{};

        [[nodiscard]] friend constexpr bool operator==(
            const PlayerSampleIdentity&,
            const PlayerSampleIdentity&) noexcept = default;
    };

    [[nodiscard]] inline constexpr bool hasSameStableIdentity(
        const PlayerSampleIdentity& left,
        const PlayerSampleIdentity& right) noexcept
    {
        return left.playerId == right.playerId &&
            left.controllerAddress == right.controllerAddress &&
            left.pawnAddress == right.pawnAddress &&
            left.pawnHandle == right.pawnHandle &&
            left.team == right.team;
    }

    [[nodiscard]] inline constexpr bool hasConflictingSteamIdentity(
        const PlayerSampleIdentity& left,
        const PlayerSampleIdentity& right) noexcept
    {
        return left.steamIdKnown && right.steamIdKnown &&
            left.steamId != right.steamId;
    }

    // A failed Steam-ID RPM is an unknown observation, not evidence that the
    // controller now belongs to a different player. The stable pawn identity
    // remains usable until a different known Steam ID is observed.
    [[nodiscard]] inline constexpr bool isCompatibleIdentityObservation(
        const PlayerSampleIdentity& current,
        const PlayerSampleIdentity& cached) noexcept
    {
        return hasSameStableIdentity(current, cached) &&
            !hasConflictingSteamIdentity(current, cached);
    }

    [[nodiscard]] inline bool isValidPosition(
        const WorldPosition& position) noexcept
    {
        if (!std::isfinite(position.x) ||
            !std::isfinite(position.y) ||
            !std::isfinite(position.z) ||
            std::abs(position.x) >= kWorldCoordinateLimit ||
            std::abs(position.y) >= kWorldCoordinateLimit ||
            std::abs(position.z) >= kWorldCoordinateLimit) {
            return false;
        }

        return true;
    }

    [[nodiscard]] inline bool isUsableYaw(const float yaw) noexcept
    {
        return std::isfinite(yaw) &&
            std::abs(yaw) <= kMaximumAbsoluteYawDegrees;
    }

    [[nodiscard]] inline bool isZeroPosition(
        const WorldPosition& position) noexcept
    {
        return std::abs(position.x) <= kZeroPositionEpsilon &&
            std::abs(position.y) <= kZeroPositionEpsilon &&
            std::abs(position.z) <= kZeroPositionEpsilon;
    }

    [[nodiscard]] inline bool isNonZeroValidPosition(
        const WorldPosition& position) noexcept
    {
        return isValidPosition(position) && !isZeroPosition(position);
    }

    // SceneNode absolute origin is the rendered entity's authoritative world
    // position. m_vOldOrigin is only a fallback for transitions where the
    // SceneNode pointer/value cannot be sampled coherently.
    [[nodiscard]] inline std::optional<WorldPosition> choosePosition(
        const PositionRead& absoluteOrigin,
        const PositionRead& oldOrigin) noexcept
    {
        const bool absoluteValid = absoluteOrigin.succeeded &&
            isValidPosition(absoluteOrigin.value);
        const bool oldValid = oldOrigin.succeeded &&
            isValidPosition(oldOrigin.value);

        // A zero SceneNode commonly appears briefly while an entity is being
        // initialized. Prefer a valid non-zero old origin in that case, but do
        // not globally reject zero because it is a valid world coordinate.
        if (absoluteValid && !isZeroPosition(absoluteOrigin.value)) {
            return absoluteOrigin.value;
        }
        if (oldValid && !isZeroPosition(oldOrigin.value)) {
            return oldOrigin.value;
        }
        if (absoluteValid)
            return absoluteOrigin.value;
        if (oldValid)
            return oldOrigin.value;
        return std::nullopt;
    }

    [[nodiscard]] inline constexpr bool canReuseLastKnown(
        const PlayerSampleIdentity& currentIdentity,
        const PlayerSampleIdentity& cachedIdentity,
        const std::uint64_t nowMs,
        const std::uint64_t sampledAtMs,
        const std::uint64_t maximumAgeMs =
            kLastKnownMaximumAgeMs) noexcept
    {
        return isCompatibleIdentityObservation(
                currentIdentity,
                cachedIdentity) &&
            sampledAtMs <= nowMs &&
            nowMs - sampledAtMs <= maximumAgeMs;
    }

    struct PlayerLastKnownState
    {
        PlayerSampleIdentity identity{};
        bool identityKnown{};
        bool stateKnown{};
        bool wasAlive{};
        bool wasDormant{};
        std::optional<WorldPosition> position;
        std::uint64_t positionSampledAtMs{};
        std::optional<float> yaw;
        std::uint64_t yawSampledAtMs{};
        std::uint64_t lastObservedAtMs{};
    };

    struct RetainedPlayerSample
    {
        std::optional<WorldPosition> position;
        std::optional<float> yaw;
    };

    // Active players use a short cache only to bridge a torn RPM sample.
    // Dormant players retain the last client-known state, and dead players
    // freeze at the last live position. A same-handle respawn is treated as a
    // new state generation so corpse coordinates cannot leak into the spawn.
    [[nodiscard]] inline RetainedPlayerSample retainPlayerSample(
        PlayerLastKnownState& state,
        const PlayerSampleIdentity& identity,
        const std::uint64_t nowMs,
        const bool alive,
        const bool dormant,
        std::optional<WorldPosition> currentPosition,
        std::optional<float> currentYaw) noexcept
    {
        if (currentPosition && !isValidPosition(*currentPosition))
            currentPosition.reset();
        if (currentYaw && !isUsableYaw(*currentYaw))
            currentYaw.reset();

        const bool identityChanged = !state.identityKnown ||
            !isCompatibleIdentityObservation(identity, state.identity);
        const bool revivedWithSameIdentity = !identityChanged &&
            state.stateKnown && !state.wasAlive && alive;
        if (identityChanged || revivedWithSameIdentity)
        {
            state = {};
            state.identity = identity;
            state.identityKnown = true;
        }
        else if (identity.steamIdKnown)
        {
            // Bind a previously unknown identity, while retaining a known
            // identity across a single failed/unknown Steam-ID read.
            state.identity.steamId = identity.steamId;
            state.identity.steamIdKnown = true;
        }

        state.lastObservedAtMs = nowMs;
        RetainedPlayerSample result;
        const bool freezeLastKnown = dormant || !alive;
        if (freezeLastKnown)
        {
            // If the first verified sample is already dormant/dead, capture it
            // once. Subsequent corpse movement or dormant memory changes must
            // not move the marker.
            if (!state.position && currentPosition)
            {
                state.position = currentPosition;
                state.positionSampledAtMs = nowMs;
            }
            if (!state.yaw && currentYaw)
            {
                state.yaw = currentYaw;
                state.yawSampledAtMs = nowMs;
            }
            result.position = state.position;
            result.yaw = state.yaw;
        }
        else
        {
            if (currentPosition)
            {
                state.position = currentPosition;
                state.positionSampledAtMs = nowMs;
                result.position = currentPosition;
            }
            else if (state.position && canReuseLastKnown(
                identity,
                state.identity,
                nowMs,
                state.positionSampledAtMs))
            {
                result.position = state.position;
            }

            if (currentYaw)
            {
                state.yaw = currentYaw;
                state.yawSampledAtMs = nowMs;
                result.yaw = currentYaw;
            }
            else if (state.yaw && canReuseLastKnown(
                identity,
                state.identity,
                nowMs,
                state.yawSampledAtMs))
            {
                result.yaw = state.yaw;
            }
        }

        state.stateKnown = true;
        state.wasAlive = alive;
        state.wasDormant = dormant;
        return result;
    }
}
