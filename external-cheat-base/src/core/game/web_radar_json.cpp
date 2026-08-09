#include "core/game/web_radar_json.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    class JsonWriter
    {
    public:
        void reserve(const std::size_t bytes)
        {
            output_.reserve(bytes);
        }

        void raw(std::string_view value)
        {
            output_.append(value);
        }

        void boolean(bool value)
        {
            raw(value ? "true" : "false");
        }

        template <typename Integer>
        void integer(Integer value)
        {
            char buffer[32];
            const auto result = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value);
            if (result.ec != std::errc{})
            {
                raw("null");
                return;
            }
            output_.append(buffer, result.ptr);
        }

        void number(float value)
        {
            if (!std::isfinite(value))
            {
                raw("null");
                return;
            }

            if (value == 0.0f)
                value = 0.0f; // Avoid a platform-dependent negative zero.

            char buffer[64];
            const auto result = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value,
                std::chars_format::general,
                std::numeric_limits<float>::max_digits10);
            if (result.ec != std::errc{})
            {
                raw("null");
                return;
            }
            output_.append(buffer, result.ptr);
        }

        void string(std::string_view value)
        {
            output_.push_back('"');

            for (std::size_t index = 0; index < value.size();)
            {
                const auto byte = static_cast<unsigned char>(value[index]);
                if (byte < 0x80)
                {
                    appendAscii(byte);
                    ++index;
                    continue;
                }

                const std::size_t sequenceLength = utf8SequenceLength(byte);
                if (sequenceLength == 0
                    || index + sequenceLength > value.size()
                    || !validUtf8Sequence(value, index, sequenceLength))
                {
                    raw("\\uFFFD");
                    ++index;
                    continue;
                }

                const std::uint32_t codePoint = decodeUtf8(
                    value,
                    index,
                    sequenceLength);
                if (codePoint == 0x2028)
                    raw("\\u2028");
                else if (codePoint == 0x2029)
                    raw("\\u2029");
                else
                    output_.append(value.data() + index, sequenceLength);

                index += sequenceLength;
            }

            output_.push_back('"');
        }

        void identifier(std::uint64_t value)
        {
            char buffer[32];
            const auto result = std::to_chars(
                std::begin(buffer),
                std::end(buffer),
                value);
            if (result.ec != std::errc{})
            {
                raw("null");
                return;
            }
            string(std::string_view(
                buffer,
                static_cast<std::size_t>(result.ptr - buffer)));
        }

        [[nodiscard]] std::string finish()
        {
            return std::move(output_);
        }

    private:
        void appendAscii(unsigned char byte)
        {
            switch (byte)
            {
            case '"': raw("\\\""); break;
            case '\\': raw("\\\\"); break;
            case '\b': raw("\\b"); break;
            case '\f': raw("\\f"); break;
            case '\n': raw("\\n"); break;
            case '\r': raw("\\r"); break;
            case '\t': raw("\\t"); break;
            case '<': raw("\\u003C"); break;
            case '>': raw("\\u003E"); break;
            case '&': raw("\\u0026"); break;
            default:
                if (byte < 0x20)
                {
                    static constexpr char hex[] = "0123456789ABCDEF";
                    raw("\\u00");
                    output_.push_back(hex[(byte >> 4) & 0x0f]);
                    output_.push_back(hex[byte & 0x0f]);
                }
                else
                {
                    output_.push_back(static_cast<char>(byte));
                }
                break;
            }
        }

        [[nodiscard]] static std::size_t utf8SequenceLength(
            unsigned char firstByte) noexcept
        {
            if (firstByte >= 0xC2 && firstByte <= 0xDF)
                return 2;
            if (firstByte >= 0xE0 && firstByte <= 0xEF)
                return 3;
            if (firstByte >= 0xF0 && firstByte <= 0xF4)
                return 4;
            return 0;
        }

        [[nodiscard]] static bool validUtf8Sequence(
            std::string_view value,
            std::size_t index,
            std::size_t length) noexcept
        {
            for (std::size_t offset = 1; offset < length; ++offset)
            {
                const auto continuation = static_cast<unsigned char>(
                    value[index + offset]);
                if ((continuation & 0xC0) != 0x80)
                    return false;
            }

            const std::uint32_t codePoint = decodeUtf8(value, index, length);
            if ((length == 2 && codePoint < 0x80)
                || (length == 3 && codePoint < 0x800)
                || (length == 4 && codePoint < 0x10000))
            {
                return false;
            }

            return codePoint <= 0x10FFFF
                && !(codePoint >= 0xD800 && codePoint <= 0xDFFF);
        }

        [[nodiscard]] static std::uint32_t decodeUtf8(
            std::string_view value,
            std::size_t index,
            std::size_t length) noexcept
        {
            const auto first = static_cast<unsigned char>(value[index]);
            std::uint32_t codePoint = length == 2
                ? first & 0x1F
                : length == 3
                    ? first & 0x0F
                    : first & 0x07;

            for (std::size_t offset = 1; offset < length; ++offset)
            {
                codePoint = (codePoint << 6)
                    | (static_cast<unsigned char>(value[index + offset]) & 0x3F);
            }
            return codePoint;
        }

        std::string output_;
    };

    constexpr std::string_view toString(game::Team team) noexcept
    {
        switch (team)
        {
        case game::Team::Spectator: return "SPEC";
        case game::Team::Terrorists: return "T";
        case game::Team::CounterTerrorists: return "CT";
        case game::Team::Unknown: return "NONE";
        }
        return "NONE";
    }

    constexpr std::string_view toString(game::MapPhase phase) noexcept
    {
        switch (phase)
        {
        case game::MapPhase::Loading: return "loading";
        case game::MapPhase::Warmup: return "warmup";
        case game::MapPhase::FreezeTime: return "freezeTime";
        case game::MapPhase::Live: return "live";
        case game::MapPhase::RoundOver: return "roundOver";
        case game::MapPhase::MatchOver: return "matchOver";
        case game::MapPhase::Unknown: return "unknown";
        }
        return "unknown";
    }

    constexpr std::string_view toString(game::WeaponCategory category) noexcept
    {
        switch (category)
        {
        case game::WeaponCategory::Knife: return "knife";
        case game::WeaponCategory::Pistol: return "pistol";
        case game::WeaponCategory::Smg: return "smg";
        case game::WeaponCategory::Rifle: return "rifle";
        case game::WeaponCategory::Shotgun: return "shotgun";
        case game::WeaponCategory::MachineGun: return "machineGun";
        case game::WeaponCategory::SniperRifle: return "sniperRifle";
        case game::WeaponCategory::Grenade: return "grenade";
        case game::WeaponCategory::Equipment: return "equipment";
        case game::WeaponCategory::Bomb: return "bomb";
        case game::WeaponCategory::Unknown: return "unknown";
        }
        return "unknown";
    }

    constexpr std::string_view toString(game::BombState state) noexcept
    {
        switch (state)
        {
        case game::BombState::Carried: return "carried";
        case game::BombState::Dropped: return "dropped";
        case game::BombState::Planted: return "planted";
        case game::BombState::Defused: return "defused";
        case game::BombState::Exploded: return "exploded";
        case game::BombState::Unknown: return "unknown";
        }
        return "unknown";
    }

    constexpr std::string_view toString(game::BombSite site) noexcept
    {
        switch (site)
        {
        case game::BombSite::A: return "a";
        case game::BombSite::B: return "b";
        case game::BombSite::Unknown: return "unknown";
        }
        return "unknown";
    }

    void appendOptionalIdentifier(
        JsonWriter& writer,
        const std::optional<std::uint64_t>& identifier)
    {
        if (identifier)
            writer.identifier(*identifier);
        else
            writer.raw("null");
    }

    void appendOptionalFloat(
        JsonWriter& writer,
        const std::optional<float>& value)
    {
        if (value)
            writer.number(*value);
        else
            writer.raw("null");
    }

    void appendPosition(
        JsonWriter& writer,
        const std::optional<game::WorldPosition>& position)
    {
        if (!position
            || !std::isfinite(position->x)
            || !std::isfinite(position->y)
            || !std::isfinite(position->z))
        {
            writer.raw("null");
            return;
        }

        writer.raw("{\"x\":");
        writer.number(position->x);
        writer.raw(",\"y\":");
        writer.number(position->y);
        writer.raw(",\"z\":");
        writer.number(position->z);
        writer.raw("}");
    }

    void appendWeapon(JsonWriter& writer, const game::WeaponSnapshot& weapon)
    {
        writer.raw("{\"definitionIndex\":");
        writer.integer(weapon.definitionIndex);
        writer.raw(",\"name\":");
        writer.string(weapon.name);
        writer.raw(",\"displayName\":");
        writer.string(weapon.displayName);
        writer.raw(",\"category\":");
        writer.string(toString(weapon.category));
        writer.raw(",\"clipAmmo\":");
        writer.integer(weapon.clipAmmo);
        writer.raw(",\"reserveAmmo\":");
        writer.integer(weapon.reserveAmmo);
        writer.raw("}");
    }

    void appendInventory(
        JsonWriter& writer,
        const std::vector<game::WeaponSnapshot>& inventory)
    {
        thread_local std::vector<const game::WeaponSnapshot*> ordered;
        ordered.clear();
        ordered.reserve(inventory.size());
        for (const auto& weapon : inventory)
            ordered.push_back(&weapon);

        std::stable_sort(
            ordered.begin(),
            ordered.end(),
            [](const auto* left, const auto* right)
            {
                if (left->category != right->category)
                    return left->category < right->category;
                if (left->definitionIndex != right->definitionIndex)
                    return left->definitionIndex < right->definitionIndex;
                if (left->name != right->name)
                    return left->name < right->name;
                return left->displayName < right->displayName;
            });

        writer.raw("[");
        bool first = true;
        for (const auto* weapon : ordered)
        {
            if (!first)
                writer.raw(",");
            first = false;
            appendWeapon(writer, *weapon);
        }
        writer.raw("]");
    }

    void appendPlayer(
        JsonWriter& writer,
        const game::PlayerSnapshot& player,
        const game::web_radar_json::SerializationOptions& options)
    {
        writer.raw("{\"id\":");
        writer.identifier(player.id);
        writer.raw(",\"steamId\":");
        if (options.includeSteamIds)
            appendOptionalIdentifier(writer, player.steamId);
        else
            writer.raw("null");
        writer.raw(",\"name\":");
        if (options.includePlayerNames)
            writer.string(player.name);
        else
            writer.raw("null");
        writer.raw(",\"team\":");
        writer.string(toString(player.team));
        writer.raw(",\"competitiveColor\":");
        writer.integer(player.competitiveColor);
        writer.raw(",\"alive\":");
        writer.boolean(player.alive);
        writer.raw(",\"dormant\":");
        writer.boolean(player.dormant);
        writer.raw(",\"position\":");
        appendPosition(writer, player.position);
        writer.raw(",\"yaw\":");
        if (player.yaw)
            writer.number(*player.yaw);
        else
            writer.raw("null");
        writer.raw(",\"health\":");
        writer.integer(player.health);
        writer.raw(",\"armor\":");
        writer.integer(player.armor);
        writer.raw(",\"money\":");
        writer.integer(player.money);
        writer.raw(",\"hasHelmet\":");
        writer.boolean(player.hasHelmet);
        writer.raw(",\"hasDefuser\":");
        writer.boolean(player.hasDefuser);
        writer.raw(",\"hasBomb\":");
        writer.boolean(player.hasBomb);
        writer.raw(",\"activeWeapon\":");
        if (player.activeWeapon)
            appendWeapon(writer, *player.activeWeapon);
        else
            writer.raw("null");
        writer.raw(",\"inventory\":");
        appendInventory(writer, player.inventory);
        writer.raw("}");
    }

    void appendBomb(JsonWriter& writer, const game::BombSnapshot& bomb)
    {
        writer.raw("{\"state\":");
        writer.string(toString(bomb.state));
        writer.raw(",\"site\":");
        writer.string(toString(bomb.site));
        writer.raw(",\"position\":");
        appendPosition(writer, bomb.position);
        writer.raw(",\"carrierPlayerId\":");
        appendOptionalIdentifier(writer, bomb.carrierPlayerId);
        writer.raw(",\"explodeInSeconds\":");
        appendOptionalFloat(writer, bomb.explodeInSeconds);
        writer.raw(",\"beingDefused\":");
        writer.boolean(bomb.beingDefused);
        writer.raw(",\"defuseInSeconds\":");
        appendOptionalFloat(writer, bomb.defuseInSeconds);
        writer.raw(",\"defuseWillSucceed\":");
        writer.boolean(bomb.defuseWillSucceed);
        writer.raw("}");
    }

    bool includePlayer(
        const game::GameSnapshot& snapshot,
        const game::PlayerSnapshot& player,
        const game::web_radar_json::TeamViewPolicy policy) noexcept
    {
        using game::Team;
        using game::web_radar_json::TeamViewPolicy;
        if (policy == TeamViewPolicy::all) {
            return true;
        }
        if (snapshot.localTeam != Team::Terrorists &&
            snapshot.localTeam != Team::CounterTerrorists) {
            return false;
        }
        if (policy == TeamViewPolicy::localTeamOnly) {
            return player.team == snapshot.localTeam;
        }
        return (player.team == Team::Terrorists ||
                player.team == Team::CounterTerrorists) &&
            player.team != snapshot.localTeam;
    }
}

std::string game::web_radar_json::serializeSnapshotV1(
    const GameSnapshot& snapshot,
    const SerializationOptions& options)
{
    JsonWriter writer;
    writer.reserve(512U + snapshot.players.size() * 768U);
    writer.raw("{\"v\":1,\"type\":\"snapshot\",\"protocolVersion\":");
    writer.integer(snapshot.protocolVersion);
    writer.raw(",\"seq\":");
    writer.integer(snapshot.sequence);
    writer.raw(",\"capturedAtMs\":");
    writer.integer(snapshot.capturedAtMs);
    writer.raw(",\"map\":{\"id\":");
    writer.string(snapshot.map.id);
    writer.raw(",\"displayName\":");
    writer.string(snapshot.map.displayName);
    writer.raw(",\"phase\":");
    writer.string(toString(snapshot.map.phase));
    writer.raw(",\"roundNumber\":");
    writer.integer(snapshot.map.roundNumber);
    writer.raw(",\"connected\":");
    writer.boolean(snapshot.map.connected);
    writer.raw("},\"localPlayerId\":");
    appendOptionalIdentifier(writer, snapshot.localPlayerId);
    writer.raw(",\"observedPlayerId\":");
    appendOptionalIdentifier(writer, snapshot.observedPlayerId);
    writer.raw(",\"localTeam\":");
    writer.string(toString(snapshot.localTeam));
    writer.raw(",\"players\":[");

    thread_local std::vector<const PlayerSnapshot*> orderedPlayers;
    orderedPlayers.clear();
    orderedPlayers.reserve(snapshot.players.size());
    for (const auto& player : snapshot.players) {
        if (includePlayer(snapshot, player, options.teamViewPolicy)) {
            orderedPlayers.push_back(&player);
        }
    }

    std::stable_sort(
        orderedPlayers.begin(),
        orderedPlayers.end(),
        [](const auto* left, const auto* right)
        {
            if (left->id != right->id)
                return left->id < right->id;
            if (left->team != right->team)
                return left->team < right->team;
            return left->name < right->name;
        });

    bool firstPlayer = true;
    for (const PlayerSnapshot* player : orderedPlayers)
    {
        if (!firstPlayer)
            writer.raw(",");
        firstPlayer = false;
        appendPlayer(writer, *player, options);
    }

    writer.raw("],\"bomb\":");
    appendBomb(writer, snapshot.bomb);
    writer.raw("}");
    return writer.finish();
}

std::string game::web_radar_json::serializeMapDefinitionV1(
    const fixed_map_radar::MapDefinition& map)
{
    JsonWriter writer;
    writer.raw("{\"v\":1,\"type\":\"map\",\"map\":{\"id\":");
    writer.string(map.id);
    writer.raw(",\"displayName\":");
    writer.string(map.displayName);
    writer.raw(",\"imagePath\":");
    writer.string(map.imagePath);
    writer.raw(",\"originX\":");
    writer.number(map.originX);
    writer.raw(",\"originY\":");
    writer.number(map.originY);
    writer.raw(",\"scale\":");
    writer.number(map.scale);
    writer.raw(",\"pixelWidth\":");
    writer.number(map.pixelWidth);
    writer.raw(",\"pixelHeight\":");
    writer.number(map.pixelHeight);
    writer.raw(",\"levels\":[");

    bool firstLevel = true;
    for (const auto& level : map.levels)
    {
        if (!firstLevel)
            writer.raw(",");
        firstLevel = false;
        writer.raw("{\"id\":");
        writer.string(level.id);
        writer.raw(",\"imagePath\":");
        writer.string(level.imagePath);
        writer.raw(",\"minimumZ\":");
        writer.number(level.minimumZ);
        writer.raw(",\"maximumZ\":");
        writer.number(level.maximumZ);
        writer.raw("}");
    }

    writer.raw("]}}");
    return writer.finish();
}
