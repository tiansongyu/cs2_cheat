#pragma once

#include "core/game/fixed_map_radar.hpp"
#include "core/game/game_snapshot.hpp"

#include <string>

namespace game::web_radar_json
{
    struct SerializationOptions
    {
        bool includePlayerNames{ true };
        bool includeSteamIds{ false };
    };

    // Serializes an absolute (non-delta) state frame using a stable field order.
    // uint64 player identifiers are strings so JavaScript cannot lose precision.
    [[nodiscard]] std::string serializeSnapshotV1(
        const GameSnapshot& snapshot,
        const SerializationOptions& options = {});

    [[nodiscard]] std::string serializeMapDefinitionV1(
        const fixed_map_radar::MapDefinition& map);
}
