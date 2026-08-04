#include "core/game/fixed_map_catalog.hpp"
#include "core/game/fixed_map_radar.hpp"
#include "core/game/game_snapshot.hpp"
#include "core/game/web_radar_json.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

namespace
{
    bool approximatelyEqual(
        float left,
        float right,
        float epsilon = 0.0001f)
    {
        return std::abs(left - right) <= epsilon;
    }

    void requireContains(
        const std::string& text,
        std::string_view expected)
    {
        assert(text.find(expected) != std::string::npos);
    }

    void requireNotContains(
        const std::string& text,
        std::string_view unexpected)
    {
        assert(text.find(unexpected) == std::string::npos);
    }

    game::fixed_map_radar::MapDefinition makeMap()
    {
        using game::fixed_map_radar::MapDefinition;
        using game::fixed_map_radar::MapLevel;

        MapDefinition map;
        map.id = "de_test";
        map.displayName = "Test Map";
        map.imagePath = "maps/de_test.webp";
        map.originX = -100.0f;
        map.originY = 100.0f;
        map.scale = 1.0f;
        map.pixelWidth = 200.0f;
        map.pixelHeight = 200.0f;
        map.levels = {
            MapLevel{ "lower", "maps/de_test_lower.webp", -100.0f, 0.0f },
            MapLevel{ "upper", "maps/de_test_upper.webp", 0.0f, 100.0f }
        };
        return map;
    }

    void testGeneratedMapCatalog()
    {
        using game::fixed_map_catalog::all;
        using game::fixed_map_catalog::find;
        using game::fixed_map_radar::project;
        using game::fixed_map_radar::selectLevel;

        const auto& definitions = all();
        assert(!definitions.empty());
        for (const auto& definition : definitions)
        {
            assert(!definition.id.empty());
            assert(definition.imagePath.starts_with("maps/"));
            assert(definition.imagePath.find("..") == std::string::npos);
            assert(find(definition.id) == &definition);
        }

        const auto* dust2 = find("de_dust2");
        assert(dust2 != nullptr);
        assert(dust2->displayName == "Dust2");
        assert(dust2->imagePath == "maps/de_dust2/radar.png");
        assert(approximatelyEqual(dust2->originX, -2476.0f));
        assert(approximatelyEqual(dust2->originY, 3239.0f));
        assert(approximatelyEqual(dust2->scale, 4.4f));
        assert(approximatelyEqual(dust2->pixelWidth, 1024.0f));
        assert(approximatelyEqual(dust2->pixelHeight, 1024.0f));
        assert(dust2->levels.empty());

        const auto dust2Center = project(
            *dust2,
            game::WorldPosition{
                dust2->originX + dust2->scale * 512.0f,
                dust2->originY - dust2->scale * 512.0f,
                0.0f });
        assert(dust2Center.valid);
        assert(dust2Center.inside);
        assert(approximatelyEqual(dust2Center.x, 0.5f));
        assert(approximatelyEqual(dust2Center.y, 0.5f));

        assert(find("") == nullptr);
        assert(find("de_unknown") == nullptr);
        assert(find("DE_DUST2") == nullptr);
        assert(find("../de_dust2") == nullptr);

        const auto* nuke = find("de_nuke");
        assert(nuke != nullptr);
        assert(nuke->levels.size() == 2);
        assert(nuke->levels[0].id == "default");
        assert(nuke->levels[1].id == "lower");
        assert(nuke->levels[0].imagePath == "maps/de_nuke/radar.png");
        assert(
            nuke->levels[1].imagePath == "maps/de_nuke/radar_lower.png");

        const auto atBoundary = selectLevel(*nuke, -495.0f);
        const auto belowBoundary = selectLevel(*nuke, -495.01f);
        assert(atBoundary.has_value());
        assert(belowBoundary.has_value());
        assert(*atBoundary == 0); // [minZ, maxZ): upper/default owns -495.
        assert(*belowBoundary == 1);
    }

    void testProjection()
    {
        using namespace game::fixed_map_radar;

        const MapDefinition map = makeMap();

        const NormalizedPosition zero = project(
            map,
            game::WorldPosition{ 0.0f, 0.0f, 0.0f });
        assert(zero.valid);
        assert(zero.inside);
        assert(approximatelyEqual(zero.x, 0.5f));
        assert(approximatelyEqual(zero.y, 0.5f));
        assert(zero.levelIndex == 1); // Upper range owns the z == 0 boundary.

        const NormalizedPosition topLeft = project(
            map,
            game::WorldPosition{ -100.0f, 100.0f, -1.0f });
        assert(topLeft.valid);
        assert(topLeft.inside);
        assert(approximatelyEqual(topLeft.x, 0.0f));
        assert(approximatelyEqual(topLeft.y, 0.0f));
        assert(topLeft.levelIndex == 0);

        const NormalizedPosition bottomRight = project(
            map,
            game::WorldPosition{ 100.0f, -100.0f, 100.0f });
        assert(bottomRight.valid);
        assert(bottomRight.inside);
        assert(approximatelyEqual(bottomRight.x, 1.0f));
        assert(approximatelyEqual(bottomRight.y, 1.0f));
        assert(bottomRight.levelIndex == 1); // Nearest-level fallback.

        const NormalizedPosition outside = project(
            map,
            game::WorldPosition{ 101.0f, -100.0f, 500.0f });
        assert(outside.valid);
        assert(!outside.inside);
        assert(outside.levelIndex == 1);

        MapDefinition invalidMap = map;
        invalidMap.scale = 0.0f;
        assert(!project(invalidMap, game::WorldPosition{}).valid);

        const NormalizedPosition invalidWorld = project(
            map,
            game::WorldPosition{
                std::numeric_limits<float>::quiet_NaN(),
                0.0f,
                0.0f });
        assert(!invalidWorld.valid);

        assert(!selectLevel(
            map,
            std::numeric_limits<float>::quiet_NaN()));
    }

    void testYawConversion()
    {
        using game::fixed_map_radar::yawToCssRotation;
        using game::fixed_map_radar::yawToScreenDirection;

        assert(approximatelyEqual(yawToCssRotation(0.0f), 90.0f));
        assert(approximatelyEqual(yawToCssRotation(90.0f), 0.0f));
        assert(approximatelyEqual(yawToCssRotation(450.0f), 0.0f));
        assert(approximatelyEqual(yawToCssRotation(-90.0f), 180.0f));
        assert(approximatelyEqual(yawToCssRotation(180.0f), 270.0f));
        assert(approximatelyEqual(yawToCssRotation(90.0f, 180.0f), 180.0f));
        assert(std::isnan(yawToCssRotation(
            std::numeric_limits<float>::infinity())));

        const auto east = yawToScreenDirection(0.0f);
        const auto north = yawToScreenDirection(90.0f);
        const auto west = yawToScreenDirection(180.0f);
        const auto south = yawToScreenDirection(-90.0f);
        assert(east.valid && approximatelyEqual(east.x, 1.0f) &&
            approximatelyEqual(east.y, 0.0f));
        assert(north.valid && approximatelyEqual(north.x, 0.0f) &&
            approximatelyEqual(north.y, -1.0f));
        assert(west.valid && approximatelyEqual(west.x, -1.0f) &&
            approximatelyEqual(west.y, 0.0f));
        assert(south.valid && approximatelyEqual(south.x, 0.0f) &&
            approximatelyEqual(south.y, 1.0f));
        assert(!yawToScreenDirection(
            std::numeric_limits<float>::quiet_NaN()).valid);
    }

    void testOpaquePlayerIdentity()
    {
        constexpr std::uint64_t first = game::makeOpaquePlayerId(1);
        constexpr std::uint64_t second = game::makeOpaquePlayerId(2);
        static_assert(first == UINT64_C(0x8000000000000001));
        static_assert(second == UINT64_C(0x8000000000000002));
        static_assert(first != second);
        static_assert(first != UINT64_C(76561198000000001));
    }

    game::GameSnapshot makeSnapshot()
    {
        game::GameSnapshot snapshot;
        snapshot.protocolVersion = game::kWebRadarProtocolVersion;
        snapshot.sequence = 42;
        snapshot.capturedAtMs = 123456789;
        snapshot.map.id = "de_test";
        snapshot.map.displayName = "Test <Map>";
        snapshot.map.phase = game::MapPhase::Live;
        snapshot.map.roundNumber = 7;
        snapshot.map.connected = true;
        snapshot.localPlayerId = 2;
        snapshot.localTeam = game::Team::CounterTerrorists;

        game::PlayerSnapshot second;
        second.id = 2;
        second.steamId = UINT64_C(76561198000000002);
        second.name = "second";
        second.team = game::Team::CounterTerrorists;
        second.alive = true;
        second.position = game::WorldPosition{ 0.0f, -0.0f, 0.0f };
        second.yaw = 90.0f;

        game::PlayerSnapshot first;
        first.id = 1;
        first.steamId = UINT64_C(76561198000000001);
        first.name = "A\"\\\n<>&";
        first.name.push_back('\x01');
        first.name.push_back(static_cast<char>(0xFF)); // Invalid UTF-8.
        first.team = game::Team::Terrorists;
        first.competitiveColor = 3;
        first.alive = true;
        first.position = game::WorldPosition{
            std::numeric_limits<float>::quiet_NaN(),
            2.0f,
            3.0f };
        first.yaw = std::numeric_limits<float>::infinity();
        first.health = 87;
        first.armor = 50;
        first.money = 4200;
        first.hasHelmet = true;
        first.hasBomb = true;
        first.activeWeapon = game::WeaponSnapshot{
            7,
            "weapon_ak47",
            "AK-47",
            game::WeaponCategory::Rifle,
            30,
            90
        };
        first.inventory = {
            game::WeaponSnapshot{
                44,
                "weapon_hegrenade",
                "HE Grenade",
                game::WeaponCategory::Grenade,
                1,
                0
            },
            *first.activeWeapon
        };

        snapshot.players = { std::move(second), std::move(first) };
        snapshot.bomb.state = game::BombState::Planted;
        snapshot.bomb.site = game::BombSite::A;
        snapshot.bomb.position = game::WorldPosition{ 0.0f, 10.0f, 20.0f };
        snapshot.bomb.explodeInSeconds = 31.25f;
        snapshot.bomb.beingDefused = true;
        snapshot.bomb.defuseInSeconds =
            std::numeric_limits<float>::quiet_NaN();
        snapshot.bomb.defuseWillSucceed = false;
        return snapshot;
    }

    void testSnapshotJson()
    {
        const game::GameSnapshot snapshot = makeSnapshot();
        const std::string json =
            game::web_radar_json::serializeSnapshotV1(snapshot);

        assert(json == game::web_radar_json::serializeSnapshotV1(snapshot));
        requireContains(json, "{\"v\":1,\"type\":\"snapshot\"");
        requireContains(json, "\"protocolVersion\":1");
        requireContains(json, "\"seq\":42");
        requireContains(json, "\"capturedAtMs\":123456789");
        requireContains(json, "\"map\":{\"id\":\"de_test\"");
        requireContains(json, "\"displayName\":\"Test \\u003CMap\\u003E\"");
        requireContains(json, "\"phase\":\"live\"");
        requireContains(json, "\"localPlayerId\":\"2\"");
        requireContains(json, "\"localTeam\":\"CT\"");

        // Canonical ordering does not depend on sampler/player enumeration order.
        const std::size_t firstPlayer = json.find("\"id\":\"1\"");
        const std::size_t secondPlayer = json.find("\"id\":\"2\"");
        assert(firstPlayer != std::string::npos);
        assert(secondPlayer != std::string::npos);
        assert(firstPlayer < secondPlayer);

        requireContains(
            json,
            "\"name\":\"A\\\"\\\\\\n\\u003C\\u003E\\u0026\\u0001\\uFFFD\"");
        requireContains(json, "\"steamId\":null");
        requireContains(json, "\"position\":null,\"yaw\":null");
        requireContains(json, "\"position\":{\"x\":0,\"y\":0,\"z\":0}");
        requireContains(json, "\"state\":\"planted\"");
        requireContains(json, "\"site\":\"a\"");
        requireContains(json, "\"explodeInSeconds\":31.25");
        requireContains(json, "\"defuseInSeconds\":null");
        requireNotContains(json, "nan");
        requireNotContains(json, "NaN");
        requireNotContains(json, "Infinity");
        requireNotContains(json, "address");

        game::web_radar_json::SerializationOptions privateFields;
        privateFields.includePlayerNames = false;
        privateFields.includeSteamIds = true;
        const std::string privacyJson =
            game::web_radar_json::serializeSnapshotV1(snapshot, privateFields);
        requireContains(
            privacyJson,
            "\"steamId\":\"76561198000000001\",\"name\":null");
    }

    void testBombStates()
    {
        constexpr std::pair<game::BombState, std::string_view> cases[] = {
            { game::BombState::Unknown, "unknown" },
            { game::BombState::Carried, "carried" },
            { game::BombState::Dropped, "dropped" },
            { game::BombState::Planted, "planted" },
            { game::BombState::Defused, "defused" },
            { game::BombState::Exploded, "exploded" }
        };

        for (const auto& [state, expected] : cases)
        {
            game::GameSnapshot snapshot;
            snapshot.bomb.state = state;
            const std::string json =
                game::web_radar_json::serializeSnapshotV1(snapshot);
            const std::string fragment =
                "\"state\":\"" + std::string(expected) + "\"";
            requireContains(json, fragment);
        }
    }

    void testMapDefinitionJson()
    {
        game::fixed_map_radar::MapDefinition map = makeMap();
        map.displayName = "A & B";
        map.originX = std::numeric_limits<float>::quiet_NaN();

        const std::string json =
            game::web_radar_json::serializeMapDefinitionV1(map);
        requireContains(json, "\"type\":\"map\"");
        requireContains(json, "\"id\":\"de_test\"");
        requireContains(json, "\"displayName\":\"A \\u0026 B\"");
        requireContains(json, "\"originX\":null");
        requireContains(json, "\"minimumZ\":-100");
        requireContains(json, "\"maximumZ\":100");
        requireNotContains(json, "nan");
    }
}

int main()
{
    testGeneratedMapCatalog();
    testProjection();
    testYawConversion();
    testOpaquePlayerIdentity();
    testSnapshotJson();
    testBombStates();
    testMapDefinitionJson();
    return 0;
}
