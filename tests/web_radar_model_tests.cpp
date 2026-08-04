#include "core/game/fixed_map_catalog.hpp"
#include "core/game/fixed_map_radar.hpp"
#include "core/game/game_snapshot.hpp"
#include "core/game/radar_player_sample.hpp"
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

    void testFloorReferenceSelection()
    {
        using game::fixed_map_radar::selectLevel;
        using game::fixed_map_radar::selectReferenceZ;

        game::GameSnapshot snapshot;
        snapshot.localPlayerId = 1;
        snapshot.observedPlayerId = 3;
        snapshot.localTeam = game::Team::CounterTerrorists;

        game::PlayerSnapshot local;
        local.id = 1;
        local.team = game::Team::CounterTerrorists;
        local.alive = false;
        local.position = game::WorldPosition{ 0.0f, 0.0f, -50.0f };

        game::PlayerSnapshot otherAlive;
        otherAlive.id = 2;
        otherAlive.team = game::Team::Terrorists;
        otherAlive.alive = true;
        otherAlive.position = game::WorldPosition{ 0.0f, 0.0f, -25.0f };

        game::PlayerSnapshot observed;
        observed.id = 3;
        observed.team = game::Team::Terrorists;
        observed.alive = true;
        observed.position = game::WorldPosition{ 0.0f, 0.0f, 50.0f };

        game::PlayerSnapshot teammate;
        teammate.id = 4;
        teammate.team = game::Team::CounterTerrorists;
        teammate.alive = true;
        teammate.position = game::WorldPosition{ 0.0f, 0.0f, -60.0f };

        snapshot.players = { local, otherAlive, observed, teammate };
        const auto observedZ = selectReferenceZ(snapshot);
        assert(observedZ.has_value());
        assert(approximatelyEqual(*observedZ, 50.0f));
        assert(selectLevel(makeMap(), *observedZ) == 1);

        snapshot.players[0].alive = true;
        const auto liveLocalZ = selectReferenceZ(snapshot);
        assert(liveLocalZ.has_value());
        assert(approximatelyEqual(*liveLocalZ, -50.0f));
        assert(selectLevel(makeMap(), *liveLocalZ) == 0);

        snapshot.players[0].alive = false;
        snapshot.players[2].alive = false;
        const auto liveTeammateZ = selectReferenceZ(snapshot);
        assert(liveTeammateZ.has_value());
        assert(approximatelyEqual(*liveTeammateZ, -60.0f));

        // An invalid X/Y coordinate makes the complete position unusable,
        // even when its Z coordinate would otherwise select a valid floor.
        snapshot.players[3].position = game::WorldPosition{
            std::numeric_limits<float>::quiet_NaN(),
            0.0f,
            -60.0f };
        const auto anyLivePlayerZ = selectReferenceZ(snapshot);
        assert(anyLivePlayerZ.has_value());
        assert(approximatelyEqual(*anyLivePlayerZ, -25.0f));

        snapshot.players[1].alive = false;
        snapshot.players[3].alive = false;
        const auto deadLocalFallbackZ = selectReferenceZ(snapshot);
        assert(deadLocalFallbackZ.has_value());
        assert(approximatelyEqual(*deadLocalFallbackZ, -50.0f));

        snapshot.players[0].position = game::WorldPosition{
            0.0f,
            0.0f,
            std::numeric_limits<float>::quiet_NaN() };
        const auto anyPositionFallbackZ = selectReferenceZ(snapshot);
        assert(anyPositionFallbackZ.has_value());
        assert(approximatelyEqual(*anyPositionFallbackZ, -25.0f));

        snapshot.players[1].position.reset();
        snapshot.players[2].position.reset();
        snapshot.players[3].position.reset();
        assert(!selectReferenceZ(snapshot).has_value());
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

    void testRadarPlayerSamplingPolicy()
    {
        using namespace game::radar_player_sample;

        const PositionRead absolute{
            true,
            game::WorldPosition{ 120.0f, -240.0f, 16.0f }
        };
        const PositionRead old{
            true,
            game::WorldPosition{ 100.0f, -200.0f, 8.0f }
        };
        const auto preferred = choosePosition(absolute, old);
        assert(preferred.has_value());
        assert(approximatelyEqual(preferred->x, 120.0f));
        assert(approximatelyEqual(preferred->y, -240.0f));

        const auto fallbackAfterReadFailure = choosePosition(
            PositionRead{},
            old);
        assert(fallbackAfterReadFailure.has_value());
        assert(approximatelyEqual(fallbackAfterReadFailure->x, 100.0f));

        const auto fallbackAfterInvalidAbsolute = choosePosition(
            PositionRead{
                true,
                game::WorldPosition{ kWorldCoordinateLimit, 0.0f, 0.0f }
            },
            old);
        assert(fallbackAfterInvalidAbsolute.has_value());
        assert(approximatelyEqual(fallbackAfterInvalidAbsolute->z, 8.0f));

        const auto fallbackAfterZeroAbsolute = choosePosition(
            PositionRead{ true, game::WorldPosition{} },
            old);
        assert(fallbackAfterZeroAbsolute.has_value());
        assert(approximatelyEqual(fallbackAfterZeroAbsolute->x, 100.0f));

        const auto validWorldOrigin = choosePosition(
            PositionRead{ true, game::WorldPosition{} },
            PositionRead{ true, game::WorldPosition{} });
        assert(validWorldOrigin.has_value());
        assert(approximatelyEqual(validWorldOrigin->x, 0.0f));
        assert(approximatelyEqual(validWorldOrigin->y, 0.0f));
        assert(approximatelyEqual(validWorldOrigin->z, 0.0f));

        assert(!choosePosition(
            PositionRead{
                true,
                game::WorldPosition{
                    std::numeric_limits<float>::quiet_NaN(),
                    1.0f,
                    1.0f }
            },
            PositionRead{}).has_value());

        constexpr PlayerSampleIdentity identity{
            17,
            0x1000,
            0x2000,
            0x1234,
            2,
            76561198000000017ULL,
            true
        };
        static_assert(canReuseLastKnown(identity, identity, 1150, 1000));
        static_assert(!canReuseLastKnown(identity, identity, 1151, 1000));
        static_assert(!canReuseLastKnown(identity, identity, 999, 1000));

        constexpr PlayerSampleIdentity respawned{
            17,
            0x1000,
            0x2000,
            0x2234,
            2,
            76561198000000017ULL,
            true
        };
        constexpr PlayerSampleIdentity recycledAddress{
            17,
            0x1000,
            0x3000,
            0x1234,
            2,
            76561198000000017ULL,
            true
        };
        static_assert(!canReuseLastKnown(
            respawned,
            identity,
            1050,
            1000));
        static_assert(!canReuseLastKnown(
            recycledAddress,
            identity,
            1050,
            1000));

        PlayerLastKnownState state;
        const game::WorldPosition livePosition{ 10.0f, 20.0f, 30.0f };
        const game::WorldPosition changedPosition{ 40.0f, 50.0f, 60.0f };
        auto retained = retainPlayerSample(
            state,
            identity,
            1000,
            true,
            false,
            livePosition,
            45.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 10.0f));
        assert(retained.yaw.has_value());
        assert(approximatelyEqual(*retained.yaw, 45.0f));

        retained = retainPlayerSample(
            state,
            identity,
            1150,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(retained.position.has_value());
        assert(retained.yaw.has_value());
        retained = retainPlayerSample(
            state,
            identity,
            1151,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(!retained.position.has_value());
        assert(!retained.yaw.has_value());

        // A dormant player keeps the last valid client-known state beyond the
        // short torn-read TTL and ignores changing dormant memory values.
        retained = retainPlayerSample(
            state,
            identity,
            2000,
            true,
            true,
            changedPosition,
            90.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 10.0f));
        assert(retained.yaw.has_value());
        assert(approximatelyEqual(*retained.yaw, 45.0f));

        // Death freezes the last live point; corpse movement is ignored.
        retained = retainPlayerSample(
            state,
            identity,
            3000,
            false,
            false,
            changedPosition,
            90.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 10.0f));
        retained = retainPlayerSample(
            state,
            identity,
            4000,
            false,
            false,
            game::WorldPosition{ 70.0f, 80.0f, 90.0f },
            180.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 10.0f));

        // A same-handle revival starts a new generation and cannot inherit the
        // frozen corpse position.
        retained = retainPlayerSample(
            state,
            identity,
            4100,
            true,
            false,
            changedPosition,
            90.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 40.0f));
        assert(retained.yaw.has_value());
        assert(approximatelyEqual(*retained.yaw, 90.0f));

        // Identity/team/generation changes never reuse the old state.
        retained = retainPlayerSample(
            state,
            respawned,
            4150,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(!retained.position.has_value());
        assert(!retained.yaw.has_value());

        PlayerSampleIdentity changedTeam = identity;
        changedTeam.team = 3;
        retained = retainPlayerSample(
            state,
            changedTeam,
            4200,
            true,
            false,
            livePosition,
            std::numeric_limits<float>::infinity());
        assert(retained.position.has_value());
        assert(!retained.yaw.has_value());
    }

    void testRadarPlayerSamplingBoundaries()
    {
        using namespace game::radar_player_sample;

        const PlayerSampleIdentity identity{
            21,
            0x1100,
            0x2200,
            0x4321,
            2,
            76561198000000021ULL,
            true
        };
        const game::WorldPosition firstPosition{ 15.0f, 25.0f, 35.0f };

        // A player first discovered after death still gets its first verified
        // marker; no prior live sample is required.
        PlayerLastKnownState firstDeadState;
        auto retained = retainPlayerSample(
            firstDeadState,
            identity,
            1000,
            false,
            false,
            firstPosition,
            30.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 15.0f));
        assert(retained.yaw.has_value());
        assert(approximatelyEqual(*retained.yaw, 30.0f));

        // A player first discovered while dormant similarly publishes the
        // first client-known point and keeps it frozen while dormant.
        PlayerLastKnownState firstDormantState;
        retained = retainPlayerSample(
            firstDormantState,
            identity,
            1100,
            true,
            true,
            firstPosition,
            60.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 15.0f));
        retained = retainPlayerSample(
            firstDormantState,
            identity,
            2100,
            true,
            true,
            game::WorldPosition{ 99.0f, 99.0f, 99.0f },
            120.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 15.0f));
        assert(retained.yaw.has_value());
        assert(approximatelyEqual(*retained.yaw, 60.0f));

        // Team and Steam identity changes must invalidate cached coordinates,
        // even when controller, pawn address, and full handle are unchanged.
        PlayerLastKnownState changedIdentityState;
        retained = retainPlayerSample(
            changedIdentityState,
            identity,
            3000,
            true,
            false,
            firstPosition,
            45.0f);
        PlayerSampleIdentity changedTeam = identity;
        changedTeam.team = 3;
        retained = retainPlayerSample(
            changedIdentityState,
            changedTeam,
            3050,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(!retained.position.has_value());
        assert(!retained.yaw.has_value());

        retained = retainPlayerSample(
            changedIdentityState,
            identity,
            3100,
            true,
            false,
            firstPosition,
            45.0f);
        PlayerSampleIdentity changedSteamId = identity;
        ++changedSteamId.steamId;
        retained = retainPlayerSample(
            changedIdentityState,
            changedSteamId,
            3150,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(!retained.position.has_value());
        assert(!retained.yaw.has_value());

        // A failed Steam-ID read is unknown, not a new zero-valued identity.
        // Keep the last known binding and its short-lived sample.
        PlayerLastKnownState unknownSteamState;
        retained = retainPlayerSample(
            unknownSteamState,
            identity,
            3200,
            true,
            false,
            firstPosition,
            45.0f);
        PlayerSampleIdentity unknownSteam = identity;
        unknownSteam.steamId = 0;
        unknownSteam.steamIdKnown = false;
        assert(isCompatibleIdentityObservation(unknownSteam, identity));
        retained = retainPlayerSample(
            unknownSteamState,
            unknownSteam,
            3250,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(retained.position.has_value());
        assert(retained.yaw.has_value());
        assert(unknownSteamState.identity.steamIdKnown);
        assert(unknownSteamState.identity.steamId == identity.steamId);

        PlayerSampleIdentity newlyKnownSteam = identity;
        ++newlyKnownSteam.steamId;
        assert(!isCompatibleIdentityObservation(
            newlyKnownSteam,
            unknownSteamState.identity));
        retained = retainPlayerSample(
            unknownSteamState,
            newlyKnownSteam,
            3300,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(!retained.position.has_value());
        assert(!retained.yaw.has_value());

        // A same-address, same-handle dead-to-alive transition begins a new
        // generation. With no new live read, the corpse point must disappear.
        PlayerLastKnownState revivalState;
        retained = retainPlayerSample(
            revivalState,
            identity,
            4000,
            true,
            false,
            firstPosition,
            90.0f);
        retained = retainPlayerSample(
            revivalState,
            identity,
            4050,
            false,
            false,
            std::nullopt,
            std::nullopt);
        assert(retained.position.has_value());
        retained = retainPlayerSample(
            revivalState,
            identity,
            4100,
            true,
            false,
            std::nullopt,
            std::nullopt);
        assert(!retained.position.has_value());
        assert(!retained.yaw.has_value());

        // A successful world-origin sample is legitimate and survives the
        // state machine without being confused with an RPM failure.
        PlayerLastKnownState zeroState;
        retained = retainPlayerSample(
            zeroState,
            identity,
            5000,
            true,
            false,
            game::WorldPosition{},
            0.0f);
        assert(retained.position.has_value());
        assert(approximatelyEqual(retained.position->x, 0.0f));
        assert(approximatelyEqual(retained.position->y, 0.0f));
        assert(approximatelyEqual(retained.position->z, 0.0f));
        assert(retained.yaw.has_value());
        assert(approximatelyEqual(*retained.yaw, 0.0f));
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
        snapshot.observedPlayerId = 1;
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
        requireContains(json, "\"observedPlayerId\":\"1\"");
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
    testFloorReferenceSelection();
    testOpaquePlayerIdentity();
    testRadarPlayerSamplingPolicy();
    testRadarPlayerSamplingBoundaries();
    testSnapshotJson();
    testBombStates();
    testMapDefinitionJson();
    return 0;
}
