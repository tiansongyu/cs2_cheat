#include "esp.hpp"
#include "menu.hpp"
#include "utils/weapon_names.hpp"
#include "core/memory/game_layout.hpp"
#include "core/game/radar_player_sample.hpp"
#include "core/renderer/viewport_math.hpp"
#include "imgui.h"
#include <iostream>
#include <cmath>
#include <algorithm>  // For std::min
#include <chrono>
#include <cctype>
#include <cstring>
#include <optional>
#include <string_view>
#include <unordered_map>

namespace
{
    bool isFiniteVec3(const vec3& value)
    {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z) &&
            std::abs(value.x) < 10000000.0f &&
            std::abs(value.y) < 10000000.0f &&
            std::abs(value.z) < 10000000.0f;
    }

    game::WorldPosition toWorldPosition(const vec3& value)
    {
        return { value.x, value.y, value.z };
    }

    vec3 toVec3(const game::WorldPosition& value)
    {
        return { value.x, value.y, value.z };
    }

    game::radar_player_sample::PositionRead readOldWorldPosition(
        const uintptr_t pawn)
    {
        using game::radar_player_sample::PositionRead;
        PositionRead oldOrigin;
        vec3 oldValue{};
        oldOrigin.succeeded = memory::TryRead(
            pawn +
                cs2_dumper::schemas::client_dll::
                    C_BasePlayerPawn::m_vOldOrigin,
            oldValue);
        oldOrigin.value = toWorldPosition(oldValue);
        return oldOrigin;
    }

    struct RadarSceneSample
    {
        std::optional<game::WorldPosition> position;
        bool ownerMatched{};
    };

    RadarSceneSample readRadarSceneSample(
        const uintptr_t pawn,
        const uintptr_t sceneNode,
        const game::radar_player_sample::PositionRead& oldOrigin)
    {
        using game::radar_player_sample::PositionRead;
        using game::radar_player_sample::choosePosition;

        RadarSceneSample sample;
        uintptr_t sceneOwner = 0;
        sample.ownerMatched = sceneNode && memory::TryRead(
            sceneNode +
                cs2_dumper::schemas::client_dll::
                    CGameSceneNode::m_pOwner,
            sceneOwner) &&
            sceneOwner == pawn;

        PositionRead absoluteOrigin;
        if (sample.ownerMatched) {
            vec3 value{};
            absoluteOrigin.succeeded = memory::TryRead(
                sceneNode +
                    cs2_dumper::schemas::client_dll::
                        CGameSceneNode::m_vecAbsOrigin,
                value);
            absoluteOrigin.value = toWorldPosition(value);
        }

        sample.position = choosePosition(absoluteOrigin, oldOrigin);
        return sample;
    }

    bool isUsableViewMatrix(const viewMatrix& matrix)
    {
        bool hasNonZeroValue = false;
        for (float value : matrix.m) {
            if (!std::isfinite(value) || std::abs(value) > 100000.0f) {
                return false;
            }
            hasNonZeroValue = hasNonZeroValue || std::abs(value) > 0.000001f;
        }
        return hasNonZeroValue;
    }

    uintptr_t entityAddress(
        uintptr_t entityList,
        uint32_t index)
    {
        const uintptr_t listEntry = memory::Read<uintptr_t>(
            entityList +
            game_layout::ENTITY_LIST_CHUNK_ARRAY +
            sizeof(uintptr_t) *
                (index >> game_layout::ENTITY_CHUNK_SHIFT));
        if (!listEntry) {
            return 0;
        }
        return memory::Read<uintptr_t>(
            listEntry +
            game_layout::ENTITY_IDENTITY_STRIDE *
                (index & game_layout::ENTITY_CHUNK_MASK));
    }

    bool validTeam(uint8_t team)
    {
        return team == 2 || team == 3;
    }

    game::Team snapshotTeam(uint8_t team)
    {
        if (team == 2) {
            return game::Team::Terrorists;
        }
        if (team == 3) {
            return game::Team::CounterTerrorists;
        }
        if (team == 1) {
            return game::Team::Spectator;
        }
        return game::Team::Unknown;
    }

    game::WeaponCategory weaponCategory(uint16_t definitionIndex)
    {
        switch (definitionIndex) {
        case 1: case 2: case 3: case 4: case 30: case 32: case 36:
        case 61: case 63: case 64:
            return game::WeaponCategory::Pistol;
        case 7: case 8: case 10: case 13: case 16: case 39: case 60:
            return game::WeaponCategory::Rifle;
        case 9: case 11: case 38: case 40:
            return game::WeaponCategory::SniperRifle;
        case 17: case 19: case 23: case 24: case 26: case 33: case 34:
            return game::WeaponCategory::Smg;
        case 25: case 27: case 29: case 35:
            return game::WeaponCategory::Shotgun;
        case 14: case 28:
            return game::WeaponCategory::MachineGun;
        case 43: case 44: case 45: case 46: case 47: case 48:
        case 68: case 81: case 82: case 84:
            return game::WeaponCategory::Grenade;
        case 42: case 59: case 500: case 503: case 505: case 506:
        case 507: case 508: case 509: case 512: case 514: case 515:
        case 516: case 517: case 518: case 519: case 520: case 521:
        case 522: case 523: case 525:
            return game::WeaponCategory::Knife;
        case 49:
            return game::WeaponCategory::Bomb;
        case 31: case 50: case 51: case 52: case 54: case 55:
        case 57: case 80:
            return game::WeaponCategory::Equipment;
        default:
            return game::WeaponCategory::Unknown;
        }
    }

    std::string weaponProtocolName(uint16_t definitionIndex)
    {
        return "item_" + std::to_string(definitionIndex);
    }

    std::optional<game::WeaponSnapshot> readWeaponSnapshot(
        uintptr_t entityList,
        uint32_t weaponHandle)
    {
        if (!weaponHandle || weaponHandle == 0xFFFFFFFF) {
            return std::nullopt;
        }

        const uintptr_t weaponEntity = entityAddress(
            entityList,
            weaponHandle & game_layout::ENTITY_HANDLE_MASK);
        if (!weaponEntity) {
            return std::nullopt;
        }

        const uintptr_t itemView =
            weaponEntity +
            cs2_dumper::schemas::client_dll::C_EconEntity::m_AttributeManager +
            cs2_dumper::schemas::client_dll::C_AttributeContainer::m_Item;
        uint16_t definitionIndex = 0;
        if (!memory::TryRead(
                itemView +
                    cs2_dumper::schemas::client_dll::
                        C_EconItemView::m_iItemDefinitionIndex,
                definitionIndex) ||
            definitionIndex == 0) {
            return std::nullopt;
        }

        game::WeaponSnapshot weapon;
        weapon.definitionIndex = definitionIndex;
        weapon.name = weaponProtocolName(definitionIndex);
        weapon.displayName = weapon_names::getWeaponName(definitionIndex);
        weapon.category = weaponCategory(definitionIndex);

        int clipAmmo = 0;
        if (memory::TryRead(
                weaponEntity +
                    cs2_dumper::schemas::client_dll::
                        C_BasePlayerWeapon::m_iClip1,
                clipAmmo) &&
            clipAmmo >= 0 && clipAmmo <= 1000) {
            weapon.clipAmmo = clipAmmo;
        }

        int reserveAmmo = 0;
        if (memory::TryRead(
                weaponEntity +
                    cs2_dumper::schemas::client_dll::
                        C_BasePlayerWeapon::m_pReserveAmmo,
                reserveAmmo) &&
            reserveAmmo >= 0 && reserveAmmo <= 5000) {
            weapon.reserveAmmo = reserveAmmo;
        }
        return weapon;
    }

    std::string readPlayerName(uintptr_t controller)
    {
        char name[128]{};
        const uintptr_t sanitizedName = memory::Read<uintptr_t>(
            controller +
                cs2_dumper::schemas::client_dll::
                    CCSPlayerController::m_sSanitizedPlayerName);
        bool read = sanitizedName && memory::ReadRaw(
            sanitizedName,
            name,
            sizeof(name) - 1);
        if (!read) {
            read = memory::ReadRaw(
                controller +
                    cs2_dumper::schemas::client_dll::
                        CBasePlayerController::m_iszPlayerName,
                name,
                sizeof(name) - 1);
        }
        if (!read) {
            return {};
        }
        name[sizeof(name) - 1] = '\0';
        return std::string(name, strnlen(name, sizeof(name)));
    }

    std::string normalizeMapId(std::string value)
    {
        const std::size_t slash = value.find_last_of("/\\");
        if (slash != std::string::npos) {
            value.erase(0, slash + 1);
        }
        const std::size_t extension = value.find('.');
        if (extension != std::string::npos) {
            value.erase(extension);
        }
        if (value.empty() || value.size() > 64) {
            return {};
        }
        for (const unsigned char character : value) {
            if (!std::isalnum(character) &&
                character != '_' && character != '-') {
                return {};
            }
        }
        return value;
    }

    game::MapState sampleMapState()
    {
        game::MapState map;
        map.connected = true;

        const uintptr_t globalVars = memory::Read<uintptr_t>(
            esp::modBase +
                cs2_dumper::offsets::client_dll::dwGlobalVars);
        if (globalVars) {
            const uintptr_t mapNamePointer = memory::Read<uintptr_t>(
                globalVars + game_layout::GLOBAL_VARS_MAP_NAME);
            char mapName[128]{};
            if (mapNamePointer && memory::ReadRaw(
                    mapNamePointer,
                    mapName,
                    sizeof(mapName) - 1)) {
                map.id = normalizeMapId(std::string(
                    mapName,
                    strnlen(mapName, sizeof(mapName))));
            }
        }
        map.displayName = map.id;
        map.phase = game::MapPhase::Live;

        const uintptr_t gameRules = memory::Read<uintptr_t>(
            esp::modBase +
                cs2_dumper::offsets::client_dll::dwGameRules);
        if (gameRules) {
            bool warmup = false;
            bool freeze = false;
            int rounds = 0;
            memory::TryRead(
                gameRules +
                    cs2_dumper::schemas::client_dll::
                        C_CSGameRules::m_bWarmupPeriod,
                warmup);
            memory::TryRead(
                gameRules +
                    cs2_dumper::schemas::client_dll::
                        C_CSGameRules::m_bFreezePeriod,
                freeze);
            if (memory::TryRead(
                    gameRules +
                        cs2_dumper::schemas::client_dll::
                            C_CSGameRules::m_totalRoundsPlayed,
                    rounds) &&
                rounds >= 0 && rounds < 1000) {
                map.roundNumber = static_cast<uint32_t>(rounds + 1);
            }
            if (warmup) {
                map.phase = game::MapPhase::Warmup;
            } else if (freeze) {
                map.phase = game::MapPhase::FreezeTime;
            }
        }
        return map;
    }

    int consecutiveReadFailures = 0;
    std::chrono::steady_clock::time_point lastEntityCacheRefresh{};
    std::chrono::steady_clock::time_point lastWorldDiscovery{};
    std::chrono::steady_clock::time_point lastWorldPositionRefresh{};
    std::chrono::steady_clock::time_point lastBombRefresh{};
    std::chrono::steady_clock::time_point lastRadarSnapshotRefresh{};
    game::MapState cachedMapState{};
    std::optional<game::WorldPosition> droppedBombPosition;
    uint64_t snapshotSequence = 0;

    std::unordered_map<
        uint64_t,
        game::radar_player_sample::PlayerLastKnownState>
        radarPlayerLastKnownSamples;
    uintptr_t radarSamplingLocalPawn = 0;
    std::string radarSamplingMapId;
    std::optional<uint32_t> cachedObservedPawnHandle;
    uint64_t observedPawnHandleSampledAtMs = 0;

    uint64_t steadyMilliseconds(
        const std::chrono::steady_clock::time_point value)
    {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                value.time_since_epoch()).count());
    }

    struct CachedWorldEntity
    {
        uint32_t entityIndex = 0;
        uintptr_t entityAddress = 0;
        int type = -1;
        std::string displayName;
    };

    std::vector<CachedWorldEntity> cachedWorldEntities;

    void recordUpdateFailure()
    {
        ++consecutiveReadFailures;
        if (consecutiveReadFailures >= 3) {
            esp::clearRuntimeState();
            return;
        }

        std::lock_guard<std::mutex> lock(esp::dataMutex);
        esp::localPlayer.isValid = false;
    }

    float overlayScale()
    {
        return std::clamp(sdl_renderer::getDpiScale(), 0.75f, 3.0f);
    }

    vec3 pointAlongView(
        const vec3& origin,
        float pitchDegrees,
        float yawDegrees,
        float distance)
    {
        constexpr float DEG_TO_RAD = 3.14159265f / 180.0f;
        const float pitch = pitchDegrees * DEG_TO_RAD;
        const float yaw = yawDegrees * DEG_TO_RAD;
        const float cosPitch = std::cos(pitch);
        return {
            origin.x + cosPitch * std::cos(yaw) * distance,
            origin.y + cosPitch * std::sin(yaw) * distance,
            origin.z - std::sin(pitch) * distance
        };
    }
}

bool esp::init()
{
    pID = memory::GetProcID(L"cs2.exe");
    if (!pID) {
        std::cout << "ERROR: Cannot find cs2.exe process!" << std::endl;
        return false;
    }
    std::cout << "Found cs2.exe, PID: " << pID << std::endl;

    modBase = memory::GetModuleBaseAddress(pID, L"client.dll");
    if (!modBase) {
        std::cout << "ERROR: Cannot find client.dll!" << std::endl;
        return false;
    }
    std::cout << "client.dll base: 0x" << std::hex << modBase << std::dec << std::endl;

    return true;
}

void esp::clearRuntimeState()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    enemies = std::make_shared<const std::vector<EnemyInfo>>();
    worldEntities =
        std::make_shared<const std::vector<WorldEntityInfo>>();
    auto disconnected = std::make_shared<game::GameSnapshot>();
    disconnected->sequence = ++snapshotSequence;
    disconnected->capturedAtMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    gameSnapshot = disconnected;
    vm = {};
    player_position = {};
    player_yaw = 0.0f;
    localPlayer = {};
    bombInfo = {};
    cachedPawns.clear();
    consecutiveReadFailures = 0;
    lastEntityCacheRefresh = {};
    lastWorldDiscovery = {};
    lastWorldPositionRefresh = {};
    lastBombRefresh = {};
    lastRadarSnapshotRefresh = {};
    cachedMapState = {};
    droppedBombPosition.reset();
    cachedWorldEntities.clear();
    radarPlayerLastKnownSamples.clear();
    radarSamplingLocalPawn = 0;
    radarSamplingMapId.clear();
    cachedObservedPawnHandle.reset();
    observedPawnHandleSampledAtMs = 0;
}

esp::EnemySnapshot esp::getEnemySnapshot()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return enemies;
}

esp::GameSnapshot esp::getGameSnapshot()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return gameSnapshot;
}

void esp::refreshEntityCache(const menu::RuntimeConfig& config)
{
    const uintptr_t entityList = memory::Read<uintptr_t>(
        modBase + cs2_dumper::offsets::client_dll::dwEntityList);
    if (!entityList) {
        cachedPawns.clear();
        radarPlayerLastKnownSamples.clear();
        radarSamplingLocalPawn = 0;
        radarSamplingMapId.clear();
        cachedObservedPawnHandle.reset();
        observedPawnHandleSampledAtMs = 0;
        return;
    }

    const uintptr_t localPlayerPawn = memory::Read<uintptr_t>(
        modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        cachedPawns.clear();
        radarPlayerLastKnownSamples.clear();
        radarSamplingLocalPawn = 0;
        radarSamplingMapId.clear();
        cachedObservedPawnHandle.reset();
        observedPawnHandleSampledAtMs = 0;
        return;
    }
    uint8_t myTeam = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
    if (!validTeam(myTeam)) {
        cachedPawns.clear();
        radarPlayerLastKnownSamples.clear();
        radarSamplingLocalPawn = 0;
        radarSamplingMapId.clear();
        cachedObservedPawnHandle.reset();
        observedPawnHandleSampledAtMs = 0;
        return;
    }

    std::vector<CachedPawn> newCache;
    newCache.reserve(32);

    struct RemoteWeaponVector
    {
        uint32_t size = 0;
        uint32_t padding = 0;
        uintptr_t elements = 0;
    };
    static_assert(sizeof(RemoteWeaponVector) == 16);

    for (uint32_t i = game_layout::FIRST_PLAYER_CONTROLLER;
         i <= game_layout::LAST_PLAYER_CONTROLLER;
         ++i)
    {
        const uintptr_t entityController = entityAddress(entityList, i);
        if (!entityController) continue;

        bool pawnAlive = false;
        uint32_t pawnHandle = 0;
        if (!memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_bPawnIsAlive,
                pawnAlive) ||
            !memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_hPlayerPawn,
                pawnHandle)) {
            continue;
        }
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

        const uint32_t pawnIndex =
            pawnHandle & game_layout::ENTITY_HANDLE_MASK;
        const uintptr_t entity = entityAddress(entityList, pawnIndex);
        if (!entity) continue;

        const uint8_t team = memory::Read<uint8_t>(
            entity +
                cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
        if (!validTeam(team)) continue;
        if (!config.radarSnapshotEnabled() &&
            (!pawnAlive || entity == localPlayerPawn || team == myTeam)) {
            continue;
        }

        CachedPawn cp;
        cp.controllerAddress = entityController;
        cp.pawnAddress = entity;
        cp.pawnHandle = pawnHandle;
        cp.entityIndex = pawnIndex;
        cp.team = team;
        cp.alive = pawnAlive;
        if (config.radarSnapshotEnabled()) {
            cp.isLocal = entity == localPlayerPawn;
            bool controllerIsLocal = false;
            if (memory::TryRead(
                    entityController +
                        cs2_dumper::schemas::client_dll::
                            CBasePlayerController::
                                m_bIsLocalPlayerController,
                    controllerIsLocal)) {
                cp.isLocal = cp.isLocal || controllerIsLocal;
            }
            cp.steamIdKnown = memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CBasePlayerController::m_steamID,
                cp.steamId);
            // Keep the protocol identity independent from Steam identity.
            // Controller slots are stable for the active session and the
            // high bit keeps these generated IDs in a separate namespace.
            // Steam IDs are serialized only through the explicit opt-in
            // field below.
            cp.playerId = game::makeOpaquePlayerId(i);
            cp.playerName = readPlayerName(entityController);
            if (cp.playerName.empty()) {
                cp.playerName = "Player " + std::to_string(i);
            }
            memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_iCompTeammateColor,
                cp.competitiveColor);
            memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_iPawnArmor,
                cp.armor);
            cp.armor = std::clamp(cp.armor, 0, 200);
            memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_bPawnHasHelmet,
                cp.hasHelmet);
            memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_bPawnHasDefuser,
                cp.hasDefuser);

            const uintptr_t moneyServices = memory::Read<uintptr_t>(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_pInGameMoneyServices);
            if (moneyServices) {
                memory::TryRead(
                    moneyServices +
                        cs2_dumper::schemas::client_dll::
                            CCSPlayerController_InGameMoneyServices::
                                m_iAccount,
                    cp.money);
                cp.money = std::clamp(cp.money, 0, 1000000);
            }
        }

        // Read slow-changing data
        if (config.radarSnapshotEnabled() ||
            (config.espEnabled && config.espWeapon)) {
            cp.weaponName = "Unknown";
            const uintptr_t weaponServices = memory::Read<uintptr_t>(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BasePlayerPawn::m_pWeaponServices);
            if (weaponServices) {
                const uint32_t activeWeaponHandle = memory::Read<uint32_t>(
                    weaponServices +
                        cs2_dumper::schemas::client_dll::
                            CPlayer_WeaponServices::m_hActiveWeapon);
                cp.activeWeapon = readWeaponSnapshot(
                    entityList,
                    activeWeaponHandle);
                if (cp.activeWeapon) {
                    cp.weaponName = cp.activeWeapon->displayName;
                }

                if (config.radarSnapshotEnabled()) {
                    RemoteWeaponVector remoteWeapons{};
                    if (memory::TryRead(
                            weaponServices +
                                cs2_dumper::schemas::client_dll::
                                    CPlayer_WeaponServices::m_hMyWeapons,
                            remoteWeapons) &&
                        remoteWeapons.elements &&
                        remoteWeapons.size > 0 &&
                        remoteWeapons.size <= 32) {
                        uint32_t handles[32]{};
                        if (memory::ReadRaw(
                                remoteWeapons.elements,
                                handles,
                                remoteWeapons.size * sizeof(uint32_t))) {
                            cp.inventory.reserve(remoteWeapons.size);
                            for (uint32_t index = 0;
                                 index < remoteWeapons.size;
                                 ++index) {
                                auto weapon = readWeaponSnapshot(
                                    entityList,
                                    handles[index]);
                                if (!weapon) {
                                    continue;
                                }
                                if (weapon->category ==
                                    game::WeaponCategory::Bomb) {
                                    cp.hasBomb = true;
                                }
                                cp.inventory.push_back(std::move(*weapon));
                            }
                        }
                    }
                }
            }
        }

        if (config.espEnabled && config.espFlashIndicator) {
            cp.flashDuration = memory::Read<float>(entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration);
            float flashMaxAlpha = memory::Read<float>(entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashMaxAlpha);
            cp.isFlashed = (cp.flashDuration > 0.0f) && (flashMaxAlpha >= 0.5f);
        }

        newCache.push_back(std::move(cp));
    }

    cachedPawns = std::move(newCache);
}

namespace
{
    void publishWorldEntities(
        std::vector<WorldEntityInfo>&& entities)
    {
        std::lock_guard<std::mutex> lock(esp::dataMutex);
        esp::worldEntities = std::make_shared<
            const std::vector<WorldEntityInfo>>(
                std::move(entities));
    }

    void clearWorldEntities()
    {
        cachedWorldEntities.clear();
        droppedBombPosition.reset();
        publishWorldEntities({});
    }

    void refreshWorldEntityCache(
        const menu::RuntimeConfig& config)
    {
        std::vector<CachedWorldEntity> refreshed;
        if (!config.grenadeESP &&
            !config.droppedWeaponESP &&
            !config.radarSnapshotEnabled()) {
            cachedWorldEntities.clear();
            return;
        }

        const uintptr_t entityList = memory::Read<uintptr_t>(
            esp::modBase +
                cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) {
            cachedWorldEntities.clear();
            return;
        }

        uint32_t highestIndex = 512;
        const uintptr_t gameEntitySystem =
            memory::Read<uintptr_t>(
                esp::modBase +
                    cs2_dumper::offsets::client_dll::
                        dwGameEntitySystem);
        uint32_t sampledHighestIndex = 0;
        if (gameEntitySystem &&
            memory::TryRead(
                gameEntitySystem +
                    cs2_dumper::offsets::client_dll::
                        dwGameEntitySystem_highestEntityIndex,
                sampledHighestIndex) &&
            sampledHighestIndex >=
                game_layout::FIRST_WORLD_ENTITY &&
            sampledHighestIndex <=
                game_layout::MAX_WORLD_ENTITIES) {
            highestIndex = sampledHighestIndex;
        }

        refreshed.reserve(64);
        for (uint32_t index = game_layout::FIRST_WORLD_ENTITY;
             index <= highestIndex;
             ++index) {
            const uintptr_t entity = entityAddress(
                entityList,
                index);
            if (!entity) {
                continue;
            }

            const uintptr_t identity = memory::Read<uintptr_t>(
                entity +
                    cs2_dumper::schemas::client_dll::
                        CEntityInstance::m_pEntity);
            if (!identity) {
                continue;
            }

            const uintptr_t namePointer =
                memory::Read<uintptr_t>(
                    identity +
                        cs2_dumper::schemas::client_dll::
                            CEntityIdentity::m_designerName);
            if (!namePointer) {
                continue;
            }

            char className[64]{};
            if (!memory::ReadRaw(
                    namePointer,
                    className,
                    sizeof(className) - 1)) {
                continue;
            }

            const std::string name(className);
            int type = -1;
            std::string displayName;
            uint32_t ownerHandle = 0xFFFFFFFF;
            if (name.starts_with("weapon_")) {
                memory::TryRead(
                    entity +
                        cs2_dumper::schemas::client_dll::
                            C_BaseEntity::m_hOwnerEntity,
                    ownerHandle);
            }

            if (config.radarSnapshotEnabled() &&
                name == "weapon_c4" &&
                (ownerHandle == 0 || ownerHandle == 0xFFFFFFFF)) {
                type = 6;
                displayName = "C4";
            }
            if (config.grenadeESP) {
                if (name == "smokegrenade_projectile") {
                    type = 0;
                    displayName = "Smoke";
                } else if (name == "flashbang_projectile") {
                    type = 1;
                    displayName = "Flash";
                } else if (name == "hegrenade_projectile") {
                    type = 2;
                    displayName = "HE";
                } else if (name == "molotov_projectile") {
                    type = 3;
                    displayName = "Molotov";
                } else if (name == "decoy_projectile") {
                    type = 4;
                    displayName = "Decoy";
                }
            }

            if (config.droppedWeaponESP && type < 0 &&
                name.starts_with("weapon_")) {
                if (ownerHandle == 0 ||
                    ownerHandle == 0xFFFFFFFF) {
                    type = 5;
                    displayName = name.substr(7);
                }
            }

            if (type >= 0) {
                refreshed.push_back(CachedWorldEntity{
                    index,
                    entity,
                    type,
                    std::move(displayName)
                });
            }
        }

        cachedWorldEntities = std::move(refreshed);
    }

    void updateWorldEntityPositions()
    {
        std::vector<WorldEntityInfo> positioned;
        positioned.reserve(cachedWorldEntities.size());
        std::optional<game::WorldPosition> sampledDroppedBomb;

        const uintptr_t entityList = memory::Read<uintptr_t>(
            esp::modBase +
                cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) {
            droppedBombPosition.reset();
            publishWorldEntities({});
            return;
        }

        for (const CachedWorldEntity& cached :
             cachedWorldEntities) {
            // Ensure this index still resolves to the same allocation before
            // using the cached class/type.
            if (entityAddress(
                    entityList,
                    cached.entityIndex) !=
                cached.entityAddress) {
                continue;
            }

            uintptr_t sceneNode = 0;
            vec3 position{};
            if (!memory::TryRead(
                    cached.entityAddress +
                        cs2_dumper::schemas::client_dll::
                            C_BaseEntity::m_pGameSceneNode,
                    sceneNode) ||
                !sceneNode ||
                !memory::TryRead(
                    sceneNode +
                        cs2_dumper::schemas::client_dll::
                            CGameSceneNode::m_vecAbsOrigin,
                    position) ||
                !isFiniteVec3(position)) {
                continue;
            }

            if (cached.type == 6) {
                sampledDroppedBomb = game::WorldPosition{
                    position.x,
                    position.y,
                    position.z
                };
                continue;
            }

            positioned.push_back(WorldEntityInfo{
                position,
                cached.type,
                cached.displayName,
                static_cast<float>(
                    esp::player_distance(
                        esp::player_position,
                        position))
            });
        }

        droppedBombPosition = sampledDroppedBomb;
        publishWorldEntities(std::move(positioned));
    }

    void publishBombInfo(const BombInfo& info)
    {
        std::lock_guard<std::mutex> lock(esp::dataMutex);
        esp::bombInfo = info;
    }

    void clearBombInfo()
    {
        publishBombInfo(BombInfo{});
    }

    void updateBombInfo()
    {
        uintptr_t globalVars = 0;
        uintptr_t plantedC4List = 0;
        if (!memory::TryRead(
                esp::modBase +
                    cs2_dumper::offsets::client_dll::dwGlobalVars,
                globalVars) ||
            !memory::TryRead(
                esp::modBase +
                    cs2_dumper::offsets::client_dll::dwPlantedC4,
                plantedC4List) ||
            !globalVars ||
            !plantedC4List) {
            clearBombInfo();
            return;
        }

        uintptr_t plantedC4 = 0;
        if (!memory::TryRead(plantedC4List, plantedC4) ||
            !plantedC4) {
            clearBombInfo();
            return;
        }

        bool ticking = false;
        if (!memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_bBombTicking,
                ticking)) {
            return;
        }
        if (!ticking) {
            BombInfo finalState{};
            if (!memory::TryRead(
                    plantedC4 +
                        cs2_dumper::schemas::client_dll::
                            C_PlantedC4::m_bHasExploded,
                    finalState.hasExploded) ||
                !memory::TryRead(
                    plantedC4 +
                        cs2_dumper::schemas::client_dll::
                            C_PlantedC4::m_bBombDefused,
                    finalState.isDefused) ||
                (!finalState.hasExploded && !finalState.isDefused)) {
                clearBombInfo();
                return;
            }

            finalState.isPlanted = true;
            memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_nBombSite,
                finalState.bombSite);
            const uintptr_t sceneNode = memory::Read<uintptr_t>(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_BaseEntity::m_pGameSceneNode);
            if (sceneNode && memory::TryRead(
                    sceneNode +
                        cs2_dumper::schemas::client_dll::
                            CGameSceneNode::m_vecAbsOrigin,
                    finalState.position) &&
                isFiniteVec3(finalState.position)) {
                finalState.positionKnown = true;
            }
            finalState.sampledAtMilliseconds = GetTickCount64();
            publishBombInfo(finalState);
            return;
        }

        BombInfo sampled{};
        sampled.isPlanted = true;
        if (!memory::TryRead(
                globalVars +
                    game_layout::GLOBAL_VARS_CURRENT_TIME,
                sampled.curtime) ||
            !memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_flC4Blow,
                sampled.blowTime) ||
            !memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_bBeingDefused,
                sampled.isDefusing) ||
            !memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_bHasExploded,
                sampled.hasExploded) ||
            !memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_bBombDefused,
                sampled.isDefused) ||
            !memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_nBombSite,
                sampled.bombSite)) {
            return;
        }

        if (sampled.isDefusing &&
            !memory::TryRead(
                plantedC4 +
                    cs2_dumper::schemas::client_dll::
                        C_PlantedC4::m_flDefuseCountDown,
                sampled.defuseCountDown)) {
            return;
        }

        const uintptr_t sceneNode = memory::Read<uintptr_t>(
            plantedC4 +
                cs2_dumper::schemas::client_dll::
                    C_BaseEntity::m_pGameSceneNode);
        if (sceneNode && memory::TryRead(
                sceneNode +
                    cs2_dumper::schemas::client_dll::
                        CGameSceneNode::m_vecAbsOrigin,
                sampled.position) &&
            isFiniteVec3(sampled.position)) {
            sampled.positionKnown = true;
        }

        const bool timesValid =
            std::isfinite(sampled.curtime) &&
            std::isfinite(sampled.blowTime) &&
            sampled.curtime >= 0.0f &&
            sampled.curtime < 1000000000.0f &&
            sampled.blowTime >= sampled.curtime - 1.0f &&
            sampled.blowTime <= sampled.curtime + 120.0f &&
            (!sampled.isDefusing ||
             (std::isfinite(sampled.defuseCountDown) &&
              sampled.defuseCountDown >= sampled.curtime - 1.0f &&
              sampled.defuseCountDown <=
                  sampled.curtime + 30.0f));
        if (!timesValid) {
            return;
        }

        if (sampled.bombSite != 0 && sampled.bombSite != 1) {
            sampled.bombSite = 0;
        }
        sampled.sampledAtMilliseconds = GetTickCount64();
        publishBombInfo(sampled);
    }
}

void esp::updateEntities(const menu::RuntimeConfig& config)
{
    uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        recordUpdateFailure();
        return;
    }

    // Treat the local-player state as one coherent sample. If any required
    // read fails while the game changes level or shuts down, keep the previous
    // render snapshot instead of publishing partial/zeroed coordinates.
    viewMatrix localVm{};
    vec3 viewOffset{};
    vec3 localEyeAngles{};
    uint8_t localTeam = 0;
    const game::radar_player_sample::PositionRead localOldOrigin =
        readOldWorldPosition(localPlayerPawn);
    const std::optional<game::WorldPosition> localWorldPosition =
        game::radar_player_sample::choosePosition(
            game::radar_player_sample::PositionRead{},
            localOldOrigin);
    if (!localWorldPosition ||
        !memory::TryRead(
            modBase + cs2_dumper::offsets::client_dll::dwViewMatrix,
            localVm) ||
        !memory::TryRead(
            localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset,
            viewOffset) ||
        !memory::TryRead(
            localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_angEyeAngles,
            localEyeAngles) ||
        !memory::TryRead(
            localPlayerPawn +
                cs2_dumper::schemas::client_dll::
                    C_BaseEntity::m_iTeamNum,
            localTeam) ||
        !isUsableViewMatrix(localVm) ||
        !isFiniteVec3(viewOffset) ||
        !isFiniteVec3(localEyeAngles) ||
        !validTeam(localTeam)) {
        recordUpdateFailure();
        return;
    }
    consecutiveReadFailures = 0;
    const vec3 localPos = toVec3(*localWorldPosition);

    // Get local player info and cache it for aimbot/triggerbot
    vec3 eyePos = { localPos.x + viewOffset.x, localPos.y + viewOffset.y, localPos.z + viewOffset.z };
    int32_t crosshairEntityIndex = -1;
    if (config.triggerbotEnabled) {
        int32_t sampledCrosshairIndex = -1;
        if (memory::TryRead(
                localPlayerPawn +
                    cs2_dumper::schemas::client_dll::
                        C_CSPlayerPawn::m_iIDEntIndex,
                sampledCrosshairIndex) &&
            sampledCrosshairIndex > 0 &&
            sampledCrosshairIndex <=
                static_cast<int32_t>(
                    game_layout::MAX_WORLD_ENTITIES)) {
            crosshairEntityIndex = sampledCrosshairIndex;
        }
    }

    {
        std::lock_guard<std::mutex> lock(dataMutex);
        vm = localVm;
        localPlayer.pawn = localPlayerPawn;
        localPlayer.position = localPos;
        localPlayer.eyePosition = eyePos;
        localPlayer.viewAngle = { localEyeAngles.x, localEyeAngles.y };
        localPlayer.crosshairEntityIndex = crosshairEntityIndex;
        localPlayer.team = localTeam;
        localPlayer.isValid = true;
        player_position = eyePos;
        player_yaw = localEyeAngles.y;
    }

    // Anti-flash: zero out flash duration on local player
    if (config.antiFlash) {
        memory::Write<float>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_CSPlayerPawnBase::m_flFlashDuration, 0.0f);
    }

    const auto updateNow = std::chrono::steady_clock::now();
    const uint64_t updateNowMs = steadyMilliseconds(updateNow);
    const bool radarSamplingEnabled = config.radarSnapshotEnabled();
    if (!radarSamplingEnabled) {
        radarPlayerLastKnownSamples.clear();
        radarSamplingLocalPawn = 0;
        radarSamplingMapId.clear();
        cachedObservedPawnHandle.reset();
        observedPawnHandleSampledAtMs = 0;
    } else if (radarSamplingLocalPawn != localPlayerPawn) {
        // A local pawn transition defines a new sampling epoch. Clearing here
        // prevents a fast reconnect/respawn from inheriting another pawn's
        // short-lived samples even if controller slots are recycled.
        radarPlayerLastKnownSamples.clear();
        radarSamplingLocalPawn = localPlayerPawn;
        radarSamplingMapId.clear();
        cachedObservedPawnHandle.reset();
        observedPawnHandleSampledAtMs = 0;
    }

    bool captureRadarSnapshot = false;
    if (!radarSamplingEnabled) {
        lastRadarSnapshotRefresh = {};
    } else if (
        lastRadarSnapshotRefresh.time_since_epoch().count() == 0 ||
        updateNow - lastRadarSnapshotRefresh >=
            std::chrono::milliseconds(50)) {
        lastRadarSnapshotRefresh = updateNow;
        captureRadarSnapshot = true;
    }
    const bool radarMetadataIncomplete =
        radarSamplingEnabled &&
        std::any_of(
            cachedPawns.begin(),
            cachedPawns.end(),
            [](const CachedPawn& pawn) {
                return pawn.playerId == 0;
            });
    if (cachedPawns.empty() ||
        radarMetadataIncomplete ||
        lastEntityCacheRefresh.time_since_epoch().count() == 0 ||
        updateNow - lastEntityCacheRefresh >=
            std::chrono::milliseconds(100)) {
        lastEntityCacheRefresh = updateNow;
        refreshEntityCache(config);
    }

    // Fast path: only read position, health, bones, angles from cached pawns
    std::vector<EnemyInfo> buffer;
    buffer.reserve(cachedPawns.size());
    std::vector<game::PlayerSnapshot> sampledPlayers;
    std::optional<uint64_t> sampledLocalPlayerId;
    std::optional<uint64_t> sampledObservedPlayerId;
    std::optional<uint64_t> bombCarrierId;
    std::optional<uint32_t> sampledObservedPawnHandle;
    const bool needsHighFrequencyPlayerSamples =
        config.espEnabled ||
        config.aimbotEnabled ||
        config.triggerbotEnabled;
    if (captureRadarSnapshot) {
        if (radarPlayerLastKnownSamples.bucket_count() < 64) {
            radarPlayerLastKnownSamples.reserve(64);
        }
        sampledPlayers.reserve(cachedPawns.size());

        cachedMapState = sampleMapState();
        const bool mapEpochChanged = cachedMapState.id.empty() ||
            radarSamplingMapId != cachedMapState.id;
        if (mapEpochChanged) {
            // World coordinates and observer handles are map-local. An empty
            // ID is a loading/read-failure boundary, never the same epoch.
            radarPlayerLastKnownSamples.clear();
            cachedObservedPawnHandle.reset();
            observedPawnHandleSampledAtMs = 0;
            radarSamplingMapId = cachedMapState.id.empty()
                ? std::string{}
                : cachedMapState.id;
        }

        if (!cachedMapState.id.empty()) {
            uintptr_t observerServices = 0;
            const bool observerServicesKnown = memory::TryRead(
                localPlayerPawn +
                    cs2_dumper::schemas::client_dll::
                        C_BasePlayerPawn::m_pObserverServices,
                observerServices);
            uint32_t observerTarget = 0;
            bool observerTargetKnown = false;
            if (observerServicesKnown) {
                observerTargetKnown = !observerServices || memory::TryRead(
                    observerServices +
                        cs2_dumper::schemas::client_dll::
                            CPlayer_ObserverServices::m_hObserverTarget,
                    observerTarget);
            }
            if (observerTargetKnown) {
                if (observerTarget != 0 &&
                    observerTarget != 0xFFFFFFFF) {
                    cachedObservedPawnHandle = observerTarget;
                    observedPawnHandleSampledAtMs = updateNowMs;
                    sampledObservedPawnHandle = observerTarget;
                } else {
                    cachedObservedPawnHandle.reset();
                    observedPawnHandleSampledAtMs = 0;
                }
            } else if (cachedObservedPawnHandle &&
                observedPawnHandleSampledAtMs <= updateNowMs &&
                updateNowMs - observedPawnHandleSampledAtMs <=
                    game::radar_player_sample::
                        kLastKnownMaximumAgeMs) {
                sampledObservedPawnHandle =
                    cachedObservedPawnHandle;
            } else {
                cachedObservedPawnHandle.reset();
                observedPawnHandleSampledAtMs = 0;
            }
        }
    }

    for (const auto& cp : cachedPawns)
    {
        // Radar snapshots deliberately include every team, corpse, and local
        // player, but ordinary ESP/aim consumers only use live opponents. Do
        // not turn those extra Radar cache entries into a 240 Hz RPM cost.
        if (!captureRadarSnapshot &&
            (!needsHighFrequencyPlayerSamples ||
             cp.isLocal ||
             cp.team == localTeam ||
             !cp.alive)) {
            continue;
        }

        const uintptr_t entity = cp.pawnAddress;

        // A cached address is only a performance hint. Validate its owning
        // controller and live state on every high-frequency pass so an address
        // recycled during death/respawn cannot become a teammate or stale aim
        // target for the remainder of the cache interval.
        uint32_t livePawnHandle = 0;
        int32_t health = 0;
        uint8_t lifeState = 0xFF;
        uint8_t liveTeam = 0;
        if (!memory::TryRead(
                cp.controllerAddress +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_hPlayerPawn,
                livePawnHandle) ||
            livePawnHandle != cp.pawnHandle ||
            !memory::TryRead(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BaseEntity::m_iHealth,
                health) ||
            !memory::TryRead(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BaseEntity::m_lifeState,
                lifeState) ||
            !memory::TryRead(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BaseEntity::m_iTeamNum,
                liveTeam) ||
            health > 200 ||
            liveTeam != cp.team) {
            continue;
        }

        bool controllerPawnAlive = cp.alive;
        if (captureRadarSnapshot) {
            bool sampledPawnAlive = false;
            if (memory::TryRead(
                    cp.controllerAddress +
                        cs2_dumper::schemas::client_dll::
                            CCSPlayerController::m_bPawnIsAlive,
                    sampledPawnAlive)) {
                controllerPawnAlive = sampledPawnAlive;
            }
        }
        const bool alive = health > 0 && lifeState == 0 &&
            controllerPawnAlive;

        uintptr_t gameSceneNode = 0;
        bool dormant = true;
        bool dormantKnown = false;
        const bool sceneNodeKnown = memory::TryRead(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BaseEntity::m_pGameSceneNode,
                gameSceneNode) &&
            gameSceneNode;
        if (sceneNodeKnown) {
            bool sampledDormant = false;
            dormantKnown = memory::TryRead(
                gameSceneNode +
                    cs2_dumper::schemas::client_dll::
                        CGameSceneNode::m_bDormant,
                sampledDormant);
            if (dormantKnown) {
                dormant = sampledDormant;
            }
        }

        // Preserve the ordinary 240Hz ESP path: one old-origin RPM per remote
        // pawn. The local pawn reuses the coherent sample from this update.
        const game::radar_player_sample::PositionRead oldOrigin = cp.isLocal
            ? localOldOrigin
            : readOldWorldPosition(entity);
        const std::optional<game::WorldPosition> oldWorldPosition =
            game::radar_player_sample::choosePosition(
                game::radar_player_sample::PositionRead{},
                oldOrigin);
        vec3 feetPos = oldWorldPosition
            ? toVec3(*oldWorldPosition)
            : vec3{};
        const bool validPosition = oldWorldPosition.has_value();

        RadarSceneSample radarScene;
        bool radarDormant = true;
        if (captureRadarSnapshot) {
            radarScene = readRadarSceneSample(
                entity,
                sceneNodeKnown ? gameSceneNode : 0,
                oldOrigin);
            radarDormant = radarScene.ownerMatched && dormantKnown
                ? dormant
                : true;
        }

        float enemyYaw = 0.0f;
        float angleToPlayer = 180.0f;
        bool viewAngleKnown = false;
        if (captureRadarSnapshot ||
            (config.espEnabled && config.espViewAngle) ||
            (config.headOffsetEnabled && config.aimbotEnabled)) {
            vec3 eyeAngles{};
            if (memory::TryRead(
                    entity +
                        cs2_dumper::schemas::client_dll::
                            C_CSPlayerPawn::m_angEyeAngles,
                    eyeAngles) &&
                isFiniteVec3(eyeAngles)) {
                enemyYaw = eyeAngles.y;
                if (validPosition) {
                    angleToPlayer = calculateAngleToPlayer(
                        enemyYaw,
                        feetPos,
                        eyePos);
                }
                viewAngleKnown = true;
            }
        }

        std::optional<game::WorldPosition> radarPosition;
        std::optional<float> radarYaw;
        if (captureRadarSnapshot) {
            radarPosition = radarScene.position;
            if (viewAngleKnown) {
                radarYaw = enemyYaw;
            }
        }

        if (captureRadarSnapshot &&
            !cachedMapState.id.empty() &&
            cp.playerId != 0) {
            const game::radar_player_sample::PlayerSampleIdentity identity{
                cp.playerId,
                cp.controllerAddress,
                cp.pawnAddress,
                livePawnHandle,
                liveTeam,
                cp.steamId,
                cp.steamIdKnown
            };
            auto [sampleIterator, inserted] =
                radarPlayerLastKnownSamples.try_emplace(cp.playerId);
            game::radar_player_sample::PlayerLastKnownState& lastKnown =
                sampleIterator->second;
            (void)inserted;
            const game::radar_player_sample::RetainedPlayerSample retained =
                game::radar_player_sample::retainPlayerSample(
                    lastKnown,
                    identity,
                    updateNowMs,
                    alive,
                    radarDormant,
                    radarPosition,
                    radarYaw);
            radarPosition = retained.position;
            radarYaw = retained.yaw;
        }

        if (captureRadarSnapshot) {
            game::PlayerSnapshot player;
            player.id = cp.playerId;
            if (cp.steamIdKnown && cp.steamId != 0) {
                player.steamId = cp.steamId;
            }
            player.name = cp.playerName;
            player.team = snapshotTeam(liveTeam);
            player.competitiveColor = cp.competitiveColor;
            player.alive = alive;
            player.dormant = radarDormant;
            player.position = radarPosition;
            player.yaw = radarYaw;
            player.health = alive ? health : 0;
            player.armor = cp.armor;
            player.money = cp.money;
            player.hasHelmet = cp.hasHelmet;
            player.hasDefuser = cp.hasDefuser;
            player.hasBomb = cp.hasBomb;
            player.activeWeapon = cp.activeWeapon;
            player.inventory = cp.inventory;
            if (cp.isLocal) {
                sampledLocalPlayerId = cp.playerId;
            }
            if (sampledObservedPawnHandle &&
                *sampledObservedPawnHandle == livePawnHandle &&
                alive) {
                sampledObservedPlayerId = cp.playerId;
            }
            if (cp.hasBomb) {
                bombCarrierId = cp.playerId;
            }
            sampledPlayers.push_back(std::move(player));
        }

        if (!needsHighFrequencyPlayerSamples ||
            !alive ||
            liveTeam == localTeam ||
            dormant ||
            !validPosition) {
            continue;
        }
        vec3 vOffset{};
        const bool viewOffsetKnown = memory::TryRead(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BaseModelEntity::m_vecViewOffset,
                vOffset) &&
            isFiniteVec3(vOffset);
        if (!viewOffsetKnown) {
            continue;
        }
        vec3 headPos = feetPos + vOffset;

        float distance = static_cast<float>(player_distance(eyePos, feetPos));

        const bool needsSpottedState =
            (config.espEnabled && config.espWallCheck) ||
            (config.aimbotEnabled &&
                (config.aimbotVisibleOnly ||
                 config.smartAimEnabled));
        bool isSpotted = false;
        bool visibilityKnown = false;
        if (needsSpottedState) {
            uintptr_t entitySpottedState = entity + cs2_dumper::schemas::client_dll::C_CSPlayerPawn::m_entitySpottedState;
            visibilityKnown = memory::TryRead(
                entitySpottedState +
                    game_layout::spottedFlagOffset(),
                isSpotted);
        }

        EnemyInfo enemy;
        enemy.pawnAddress = entity;
        enemy.entityIndex = cp.entityIndex;
        enemy.position = feetPos;
        enemy.headPosition = headPos;
        enemy.health = health;
        enemy.distance = distance;
        enemy.weaponName = cp.weaponName;
        enemy.viewYaw = enemyYaw;
        enemy.angleToPlayer = angleToPlayer;
        enemy.viewAngleKnown = viewAngleKnown;
        enemy.flashDuration = cp.flashDuration;
        enemy.isFlashed = cp.isFlashed;
        enemy.isSpotted = isSpotted;
        enemy.visibilityKnown = visibilityKnown;
        enemy.hasBones = false;

        // Real bones are also the source of aimbot target positions. They are
        // read even with skeleton drawing disabled whenever input features need
        // them; the view-offset estimate remains a fail-safe fallback.
        const bool needsBones =
            (config.espEnabled && config.espSkeleton) ||
            config.aimbotEnabled;
        if (needsBones) {
            if (gameSceneNode) {
                uintptr_t boneArray = memory::Read<uintptr_t>(
                    gameSceneNode +
                    game_layout::boneArrayPointerOffset());
                if (boneArray) {
                    struct BoneData
                    {
                        float x;
                        float y;
                        float z;
                        char pad[
                            game_layout::BONE_STRIDE -
                            sizeof(float) * 3];
                    };
                    static_assert(
                        sizeof(BoneData) ==
                        game_layout::BONE_STRIDE);
                    BoneData bones[BoneIndex::BONE_COUNT]{};
                    if (memory::ReadRaw(
                        boneArray,
                        bones,
                        sizeof(BoneData) * BoneIndex::BONE_COUNT)) {
                        bool bonesValid = true;
                        for (int b = 0; b < BoneIndex::BONE_COUNT; b++) {
                            enemy.bonePositions[b] = { bones[b].x, bones[b].y, bones[b].z };
                            bonesValid =
                                bonesValid &&
                                isFiniteVec3(enemy.bonePositions[b]);
                        }
                        const double headToFeet = player_distance(
                            enemy.bonePositions[BoneIndex::HEAD],
                            feetPos);
                        bonesValid =
                            bonesValid &&
                            headToFeet > 10.0 &&
                            headToFeet < 200.0;
                        enemy.hasBones = bonesValid;
                        if (bonesValid) {
                            enemy.headPosition =
                                enemy.bonePositions[BoneIndex::HEAD];
                        }
                    }
                }
            }
        }

        buffer.push_back(enemy);
    }

    if (captureRadarSnapshot) {
        for (auto iterator = radarPlayerLastKnownSamples.begin();
             iterator != radarPlayerLastKnownSamples.end();) {
            const uint64_t lastObserved = iterator->second.lastObservedAtMs;
            if (lastObserved > updateNowMs ||
                updateNowMs - lastObserved >
                    game::radar_player_sample::
                        kLastKnownMaximumAgeMs) {
                iterator = radarPlayerLastKnownSamples.erase(iterator);
            } else {
                ++iterator;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(dataMutex);
        enemies = std::make_shared<
            const std::vector<EnemyInfo>>(
                std::move(buffer));
    }

    // Discovery is intentionally slow, but positions and bomb time are sampled
    // at 60 Hz. This keeps moving grenades and countdown text smooth without
    // rescanning thousands of entity identities every frame.
    const bool worldEnabled =
        config.grenadeESP ||
        config.droppedWeaponESP ||
        config.radarSnapshotEnabled();
    if (!worldEnabled) {
        if (!cachedWorldEntities.empty() ||
            lastWorldDiscovery.time_since_epoch().count() != 0) {
            clearWorldEntities();
            lastWorldDiscovery = {};
            lastWorldPositionRefresh = {};
        }
    } else {
        if (lastWorldDiscovery.time_since_epoch().count() == 0 ||
            updateNow - lastWorldDiscovery >=
                std::chrono::milliseconds(250)) {
            lastWorldDiscovery = updateNow;
            refreshWorldEntityCache(config);
        }
        if (lastWorldPositionRefresh.time_since_epoch().count() == 0 ||
            updateNow - lastWorldPositionRefresh >=
                std::chrono::milliseconds(16)) {
            lastWorldPositionRefresh = updateNow;
            updateWorldEntityPositions();
        }
    }

    const bool bombSamplingEnabled =
        config.bombTimer || config.radarSnapshotEnabled();
    if (!bombSamplingEnabled) {
        if (lastBombRefresh.time_since_epoch().count() != 0) {
            clearBombInfo();
            lastBombRefresh = {};
        }
    } else if (
        lastBombRefresh.time_since_epoch().count() == 0 ||
        updateNow - lastBombRefresh >=
            std::chrono::milliseconds(16)) {
        lastBombRefresh = updateNow;
        updateBombInfo();
    }

    if (captureRadarSnapshot) {
        BombInfo sampledBomb;
        {
            std::lock_guard<std::mutex> lock(dataMutex);
            sampledBomb = bombInfo;
        }

        auto snapshot = std::make_shared<game::GameSnapshot>();
        snapshot->sequence = ++snapshotSequence;
        snapshot->capturedAtMs = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        snapshot->map = cachedMapState;
        snapshot->map.connected = true;
        snapshot->localPlayerId = sampledLocalPlayerId;
        snapshot->observedPlayerId = sampledObservedPlayerId;
        snapshot->localTeam = snapshotTeam(localTeam);
        snapshot->players = std::move(sampledPlayers);

        if (sampledBomb.isPlanted) {
            if (sampledBomb.hasExploded) {
                snapshot->bomb.state = game::BombState::Exploded;
            } else if (sampledBomb.isDefused) {
                snapshot->bomb.state = game::BombState::Defused;
            } else {
                snapshot->bomb.state = game::BombState::Planted;
            }
            snapshot->bomb.site = sampledBomb.bombSite == 1
                ? game::BombSite::B
                : game::BombSite::A;
            if (sampledBomb.positionKnown) {
                snapshot->bomb.position = game::WorldPosition{
                    sampledBomb.position.x,
                    sampledBomb.position.y,
                    sampledBomb.position.z
                };
            }

            if (!sampledBomb.hasExploded && !sampledBomb.isDefused) {
                const float sampleAgeSeconds =
                    sampledBomb.sampledAtMilliseconds == 0
                        ? 0.0f
                        : static_cast<float>(
                            GetTickCount64() -
                            sampledBomb.sampledAtMilliseconds) / 1000.0f;
                const float explodeIn =
                    sampledBomb.blowTime -
                    sampledBomb.curtime -
                    sampleAgeSeconds;
                if (std::isfinite(explodeIn)) {
                    snapshot->bomb.explodeInSeconds =
                        std::max(0.0f, explodeIn);
                }
                snapshot->bomb.beingDefused = sampledBomb.isDefusing;
                if (sampledBomb.isDefusing) {
                    const float defuseIn =
                        sampledBomb.defuseCountDown -
                        sampledBomb.curtime -
                        sampleAgeSeconds;
                    if (std::isfinite(defuseIn)) {
                        snapshot->bomb.defuseInSeconds =
                            std::max(0.0f, defuseIn);
                        snapshot->bomb.defuseWillSucceed =
                            !snapshot->bomb.explodeInSeconds ||
                            *snapshot->bomb.defuseInSeconds <=
                                *snapshot->bomb.explodeInSeconds;
                    }
                }
            }
        } else if (bombCarrierId) {
            snapshot->bomb.state = game::BombState::Carried;
            snapshot->bomb.carrierPlayerId = bombCarrierId;
        } else if (droppedBombPosition) {
            snapshot->bomb.state = game::BombState::Dropped;
            snapshot->bomb.position = droppedBombPosition;
        }

        std::lock_guard<std::mutex> lock(dataMutex);
        gameSnapshot = std::move(snapshot);
    }
}

void esp::render()
{
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Take a snapshot of shared data under lock
    EnemySnapshot snapEnemies;
    WorldEntitySnapshot snapWorldEntities;
    viewMatrix snapVm;
    LocalPlayerCache snapLocalPlayer;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        snapEnemies = enemies;
        snapWorldEntities = worldEntities;
        snapVm = vm;
        snapLocalPlayer = localPlayer;
    }
    if (!snapEnemies) {
        snapEnemies =
            std::make_shared<const std::vector<EnemyInfo>>();
    }
    if (!snapWorldEntities) {
        snapWorldEntities =
            std::make_shared<
                const std::vector<WorldEntityInfo>>();
    }

    const bool clipToViewport =
        VIEWPORT_W > 0 && VIEWPORT_H > 0;
    if (clipToViewport) {
        drawList->PushClipRect(
            ImVec2(
                static_cast<float>(VIEWPORT_X),
                static_cast<float>(VIEWPORT_Y)),
            ImVec2(
                static_cast<float>(VIEWPORT_X) +
                    static_cast<float>(VIEWPORT_W),
                static_cast<float>(VIEWPORT_Y) +
                    static_cast<float>(VIEWPORT_H)),
            true);
    }

    // Project the same angular boundary used by the aimbot through the game's
    // current view matrix. Unlike a fixed tan()/screen-height approximation,
    // this follows the actual aspect ratio, camera projection, and viewport.
    if (menu::aimbotEnabled &&
        menu::aimbotShowFOV &&
        snapLocalPlayer.isValid)
    {
        ImU32 fovColor = IM_COL32(
            static_cast<int>(menu::aimbotFOVColor[0] * 255),
            static_cast<int>(menu::aimbotFOVColor[1] * 255),
            static_cast<int>(menu::aimbotFOVColor[2] * 255),
            static_cast<int>(menu::aimbotFOVColor[3] * 255)
        );

        constexpr int SEGMENTS = 48;
        constexpr float TWO_PI = 6.28318530f;
        ImVec2 points[SEGMENTS];
        bool projectionValid = true;
        for (int index = 0; index < SEGMENTS; ++index) {
            const float phase =
                TWO_PI * static_cast<float>(index) /
                static_cast<float>(SEGMENTS);
            const float pitchOffset = std::sin(phase) * menu::aimbotFOV;
            const float yawOffset = std::cos(phase) * menu::aimbotFOV;
            const vec3 rayPoint = pointAlongView(
                snapLocalPlayer.eyePosition,
                snapLocalPlayer.viewAngle.x + pitchOffset,
                snapLocalPlayer.viewAngle.y + yawOffset,
                4096.0f);
            vec2 projected{};
            if (!w2s(rayPoint, projected, snapVm.m)) {
                projectionValid = false;
                break;
            }
            points[index] = ImVec2(projected.x, projected.y);
        }
        if (projectionValid) {
            drawList->AddPolyline(
                points,
                SEGMENTS,
                fovColor,
                ImDrawFlags_Closed,
                1.5f * overlayScale());
        }
    }

    if (menu::espEnabled) {
        for (const auto& enemy : *snapEnemies)
        {
        vec2 screenFeet, screenHead;

        if (!w2s(enemy.position, screenFeet, snapVm.m)) continue;
        if (!w2s(enemy.headPosition, screenHead, snapVm.m)) continue;

        float height = screenFeet.y - screenHead.y;
        if (!viewport_math::validProjectedBox(
                height,
                static_cast<float>(VIEWPORT_H))) {
            continue;
        }
        float width = height / 2.5f;

        int x = static_cast<int>(screenHead.x - width / 2);
        int y = static_cast<int>(screenHead.y);
        int w = static_cast<int>(width);
        int h = static_cast<int>(height);
        const float scale = overlayScale();

        // Build a DPI-aware stack above the box. Every enabled indicator gets
        // its own vertical slot, so scaled text cannot overlap the eye marker
        // or visibility arrow.
        float topCursor = static_cast<float>(y) - 3.0f * scale;
        ImVec2 weaponTextSize{};
        float weaponTextY = topCursor;
        if (menu::espWeapon) {
            weaponTextSize = ImGui::CalcTextSize(enemy.weaponName.c_str());
            weaponTextY = topCursor - weaponTextSize.y;
            topCursor = weaponTextY - 3.0f * scale;
        }

        const float ellipseRadiusY = 4.0f * scale;
        float ellipseCenterY = topCursor;
        if (menu::espFlashIndicator) {
            ellipseCenterY = topCursor - ellipseRadiusY;
            topCursor = ellipseCenterY - ellipseRadiusY - 3.0f * scale;
        }

        const float arrowSize = 7.0f * scale;
        const float arrowTopY = topCursor - arrowSize;

        // Determine box color based on ANGLE (threat level)
        // Red = enemy facing you (danger), Green = enemy facing away (safe)
        uint8_t r, g, b, a;

        if (menu::espViewAngle && enemy.viewAngleKnown) {
            // Use angle-based color for box
            if (enemy.angleToPlayer < 45.0f) {
                // Facing player (RED - DANGER!)
                r = 255; g = 0; b = 0; a = 255;
            } else if (enemy.angleToPlayer < 90.0f) {
                // Partially facing (ORANGE)
                r = 255; g = 165; b = 0; a = 255;
            } else if (enemy.angleToPlayer < 135.0f) {
                // Side view (YELLOW)
                r = 255; g = 255; b = 0; a = 255;
            } else {
                // Back to player (GREEN - SAFE)
                r = 0; g = 255; b = 0; a = 255;
            }
        } else {
            // View angle disabled, use default box color
            r = static_cast<uint8_t>(menu::espBoxColor[0] * 255);
            g = static_cast<uint8_t>(menu::espBoxColor[1] * 255);
            b = static_cast<uint8_t>(menu::espBoxColor[2] * 255);
            a = static_cast<uint8_t>(menu::espBoxColor[3] * 255);
        }

        // Draw box (color indicates threat level based on angle)
        if (menu::espBox) {

            ImU32 boxColor = IM_COL32(r, g, b, a);
            drawList->AddRect(
                ImVec2((float)x, (float)y),
                ImVec2((float)(x + w), (float)(y + h)),
                boxColor,
                0.0f,
                0,
                2.0f * scale);
        }

        // Draw ellipse "eye" indicator below weapon name
        if (menu::espFlashIndicator) {


            // Calculate ellipse position (below weapon name, above the box)
            float ellipseCenterX = static_cast<float>(x + w / 2);

            float ellipseRadiusX = w * 0.25f;  // 25% of box width
            ellipseRadiusX = std::max(ellipseRadiusX, 6.0f * scale);

            // Determine eye color: Red (normal) or Yellow (flashed)
            ImU32 eyeColor;
            if (enemy.isFlashed) {
                // Yellow - enemy is flashed
                uint8_t fr = static_cast<uint8_t>(menu::espFlashColor[0] * 255);
                uint8_t fg = static_cast<uint8_t>(menu::espFlashColor[1] * 255);
                uint8_t fb = static_cast<uint8_t>(menu::espFlashColor[2] * 255);
                uint8_t fa = static_cast<uint8_t>(menu::espFlashColor[3] * 255);
                eyeColor = IM_COL32(fr, fg, fb, fa);
            } else {
                // Red - normal state
                uint8_t nr = static_cast<uint8_t>(menu::espFlashNormalColor[0] * 255);
                uint8_t ng = static_cast<uint8_t>(menu::espFlashNormalColor[1] * 255);
                uint8_t nb = static_cast<uint8_t>(menu::espFlashNormalColor[2] * 255);
                uint8_t na = static_cast<uint8_t>(menu::espFlashNormalColor[3] * 255);
                eyeColor = IM_COL32(nr, ng, nb, na);
            }

            // Draw filled ellipse
            const int segments = 32;
            drawList->AddEllipseFilled(
                ImVec2(ellipseCenterX, ellipseCenterY),  // center
                ImVec2(ellipseRadiusX, ellipseRadiusY),  // radius (x, y)
                eyeColor,                                 // color
                0.0f,                                     // rotation
                segments                                  // num_segments
            );
        }

        // Draw health bar (vertical bar on left side)
        if (menu::espHealth) {

            int healthBarWidth = std::max(2, static_cast<int>(std::round(4.0f * scale)));
            int healthBarHeight = h;
            int healthBarX = x - static_cast<int>(std::round(8.0f * scale));
            int healthBarY = y;

            // Background bar
            drawList->AddRectFilled(
                ImVec2((float)healthBarX, (float)healthBarY),
                ImVec2((float)(healthBarX + healthBarWidth), (float)(healthBarY + healthBarHeight)),
                IM_COL32(50, 50, 50, 255));

            // Health bar (fills from bottom to top)
            const float healthFraction = std::clamp(
                enemy.health / 100.0f,
                0.0f,
                1.0f);
            int healthHeight =
                static_cast<int>(healthFraction * healthBarHeight);
            int healthY = healthBarY + (healthBarHeight - healthHeight);
            uint8_t healthR =
                static_cast<uint8_t>(255 * (1.0f - healthFraction));
            uint8_t healthG =
                static_cast<uint8_t>(255 * healthFraction);
            drawList->AddRectFilled(
                ImVec2((float)healthBarX, (float)healthY),
                ImVec2((float)(healthBarX + healthBarWidth), (float)(healthY + healthHeight)),
                IM_COL32(healthR, healthG, 0, 255));
        }

        // Draw weapon name above the box
        if (menu::espWeapon) {
            uint8_t wr = static_cast<uint8_t>(menu::espWeaponColor[0] * 255);
            uint8_t wg = static_cast<uint8_t>(menu::espWeaponColor[1] * 255);
            uint8_t wb = static_cast<uint8_t>(menu::espWeaponColor[2] * 255);
            uint8_t wa = static_cast<uint8_t>(menu::espWeaponColor[3] * 255);


            drawList->AddText(
                ImVec2(
                    static_cast<float>(x + w / 2) - weaponTextSize.x / 2.0f,
                    weaponTextY),
                IM_COL32(wr, wg, wb, wa),
                enemy.weaponName.c_str()
            );
        }

        // Draw the spotted-state indicator. It deliberately does not claim
        // geometric line-of-sight: red is spotted, green is unknown/not spotted.
        if (menu::espWallCheck) {


            // Determine triangle color from the spotted state.
            uint8_t vr, vg, vb, va;
            if (enemy.isSpotted) {
                // Enemy is spotted - RED
                vr = static_cast<uint8_t>(menu::espBoxColor[0] * 255);
                vg = static_cast<uint8_t>(menu::espBoxColor[1] * 255);
                vb = static_cast<uint8_t>(menu::espBoxColor[2] * 255);
                va = static_cast<uint8_t>(menu::espBoxColor[3] * 255);
            } else {
                // Enemy is not spotted or the state is unknown - GREEN
                vr = static_cast<uint8_t>(menu::espWallColor[0] * 255);
                vg = static_cast<uint8_t>(menu::espWallColor[1] * 255);
                vb = static_cast<uint8_t>(menu::espWallColor[2] * 255);
                va = static_cast<uint8_t>(menu::espWallColor[3] * 255);
            }

            // Draw arrow indicator at top of box
            float centerX = static_cast<float>(x + w / 2);
            float topY = arrowTopY;

            // Draw filled triangle pointing in enemy's view direction relative to player
            // Triangle direction still shows where enemy is facing
            // Color shows spotted-state status.
            if (enemy.angleToPlayer < 90.0f) {
                // Enemy is facing towards player - arrow points down
                ImVec2 p1(centerX, topY + arrowSize);           // Bottom point
                ImVec2 p2(centerX - arrowSize, topY);           // Top left
                ImVec2 p3(centerX + arrowSize, topY);           // Top right
                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(vr, vg, vb, va));
            } else {
                // Enemy is facing away from player - arrow points up
                ImVec2 p1(centerX, topY);                       // Top point
                ImVec2 p2(centerX - arrowSize, topY + arrowSize); // Bottom left
                ImVec2 p3(centerX + arrowSize, topY + arrowSize); // Bottom right
                drawList->AddTriangleFilled(p1, p2, p3, IM_COL32(vr, vg, vb, va));
            }

            // Draw angle text (still shows angle degree)
            if (menu::espViewAngleText &&
                enemy.viewAngleKnown) {
                char angleText[16];
                snprintf(angleText, sizeof(angleText), "%.0f deg", enemy.angleToPlayer);
                ImVec2 angleTextSize = ImGui::CalcTextSize(angleText);
                drawList->AddText(
                    ImVec2(
                        centerX - angleTextSize.x / 2,
                        topY - angleTextSize.y - 2.0f * scale),
                    IM_COL32(vr, vg, vb, va),
                    angleText
                );
            }
        }

        // Flashbang text indicator removed - now using ellipse eye indicator above

        // Draw distance text using ImGui
        if (menu::espDistance) {
            char distText[32];
            snprintf(distText, sizeof(distText), "%.0fm", enemy.distance / 100.0f);

            uint8_t dr = static_cast<uint8_t>(menu::espDistanceColor[0] * 255);
            uint8_t dg = static_cast<uint8_t>(menu::espDistanceColor[1] * 255);
            uint8_t db = static_cast<uint8_t>(menu::espDistanceColor[2] * 255);
            uint8_t da = static_cast<uint8_t>(menu::espDistanceColor[3] * 255);

            const ImVec2 distanceTextSize = ImGui::CalcTextSize(distText);
            drawList->AddText(
                ImVec2(
                    static_cast<float>(x + w / 2) - distanceTextSize.x / 2.0f,
                    static_cast<float>(y + h) + 2.0f * scale),
                IM_COL32(dr, dg, db, da),
                distText
            );
        }

        // Draw skeleton (bone connections)
        if (menu::espSkeleton && enemy.hasBones) {


            uint8_t skR = static_cast<uint8_t>(menu::espSkeletonColor[0] * 255);
            uint8_t skG = static_cast<uint8_t>(menu::espSkeletonColor[1] * 255);
            uint8_t skB = static_cast<uint8_t>(menu::espSkeletonColor[2] * 255);
            uint8_t skA = static_cast<uint8_t>(menu::espSkeletonColor[3] * 255);
            ImU32 boneColor = IM_COL32(skR, skG, skB, skA);

            static const BoneConnection connections[] = {
                // Body: neck -> shoulder connections
                { BoneIndex::NECK, BoneIndex::LEFT_SHOULDER },
                { BoneIndex::NECK, BoneIndex::RIGHT_SHOULDER },
                // Body: neck -> spine -> hips
                { BoneIndex::NECK, BoneIndex::SPINE_2 },
                { BoneIndex::SPINE_2, BoneIndex::LEFT_HIP },
                { BoneIndex::SPINE_2, BoneIndex::RIGHT_HIP },
                // Left arm: shoulder -> elbow -> hand
                { BoneIndex::LEFT_SHOULDER, BoneIndex::LEFT_ELBOW },
                { BoneIndex::LEFT_ELBOW, BoneIndex::LEFT_HAND },
                // Right arm: shoulder -> elbow -> hand
                { BoneIndex::RIGHT_SHOULDER, BoneIndex::RIGHT_ELBOW },
                { BoneIndex::RIGHT_ELBOW, BoneIndex::RIGHT_HAND },
                // Left leg: hip -> knee -> foot
                { BoneIndex::LEFT_HIP, BoneIndex::LEFT_KNEE },
                { BoneIndex::LEFT_KNEE, BoneIndex::LEFT_FOOT },
                // Right leg: hip -> knee -> foot
                { BoneIndex::RIGHT_HIP, BoneIndex::RIGHT_KNEE },
                { BoneIndex::RIGHT_KNEE, BoneIndex::RIGHT_FOOT },
            };

            for (const auto& conn : connections) {
                vec3 from = enemy.bonePositions[conn.from];
                vec3 to = enemy.bonePositions[conn.to];

                vec2 screenFrom, screenTo;
                if (w2s(from, screenFrom, snapVm.m) &&
                    w2s(to, screenTo, snapVm.m)) {
                    drawList->AddLine(
                        ImVec2(screenFrom.x, screenFrom.y),
                        ImVec2(screenTo.x, screenTo.y),
                        boneColor, 1.5f * scale
                    );
                }
            }

            // Draw head: line from neck to head, circle above head
            vec2 screenHead, screenNeck;
            if (w2s(enemy.bonePositions[BoneIndex::HEAD], screenHead, snapVm.m) &&
                w2s(enemy.bonePositions[BoneIndex::NECK], screenNeck, snapVm.m)) {
                float dx = screenHead.x - screenNeck.x;
                float dy = screenHead.y - screenNeck.y;
                float dist = std::sqrt(dx * dx + dy * dy);
                float radius = dist * 0.7f;
                if (radius < 3.0f * scale) radius = 3.0f * scale;
                // Circle center above head position
                float cx = screenHead.x + dx * 0.7f;
                float cy = screenHead.y + dy * 0.7f;
                drawList->AddLine(
                    ImVec2(screenNeck.x, screenNeck.y),
                    ImVec2(cx, cy - radius),
                    boneColor, 1.5f * scale
                );
                drawList->AddCircle(
                    ImVec2(cx, cy),
                    radius, boneColor, 16, 1.5f * scale
                );
            }
        }

        // Draw snaplines
        if (menu::espSnaplines) {
            int startX, startY;
            switch (menu::snaplinesOrigin) {
                case 0: // Bottom
                    startX = VIEWPORT_X + static_cast<int>(VIEWPORT_W / 2);
                    startY = VIEWPORT_Y + static_cast<int>(VIEWPORT_H);
                    break;
                case 1: // Center
                    startX = VIEWPORT_X + static_cast<int>(VIEWPORT_W / 2);
                    startY = VIEWPORT_Y + static_cast<int>(VIEWPORT_H / 2);
                    break;
                case 2: // Top
                    startX = VIEWPORT_X + static_cast<int>(VIEWPORT_W / 2);
                    startY = VIEWPORT_Y;
                    break;
                default:
                    startX = VIEWPORT_X + static_cast<int>(VIEWPORT_W / 2);
                    startY = VIEWPORT_Y + static_cast<int>(VIEWPORT_H);
            }

            uint8_t sr = static_cast<uint8_t>(menu::espSnaplinesColor[0] * 255);
            uint8_t sg = static_cast<uint8_t>(menu::espSnaplinesColor[1] * 255);
            uint8_t sb = static_cast<uint8_t>(menu::espSnaplinesColor[2] * 255);
            uint8_t sa = static_cast<uint8_t>(menu::espSnaplinesColor[3] * 255);


            drawList->AddLine(
                ImVec2((float)startX, (float)startY),
                ImVec2(screenFeet.x, screenFeet.y),
                IM_COL32(sr, sg, sb, sa), 1.5f * overlayScale()
            );
        }
        }
    }

    // World entity ESP (grenades, dropped weapons)
    if (menu::grenadeESP || menu::droppedWeaponESP) {
        const float scale = overlayScale();

        for (const auto& we : *snapWorldEntities) {
            if (we.type <= 4 && !menu::grenadeESP) continue;
            if (we.type == 5 && !menu::droppedWeaponESP) continue;
            if (we.distance > 2000.0f) continue;

            vec2 screenPos;
            if (!w2s(we.position, screenPos, snapVm.m)) continue;

            ImU32 color;
            float radius;
            switch (we.type) {
                case 0: color = IM_COL32(100, 100, 255, 255); radius = 10.0f; break; // Smoke - blue
                case 1: color = IM_COL32(255, 255, 0, 255); radius = 9.0f; break;    // Flash - bright yellow
                case 2: color = IM_COL32(255, 30, 30, 255); radius = 9.0f; break;    // HE - bright red
                case 3: color = IM_COL32(255, 100, 0, 255); radius = 10.0f; break;   // Molotov - orange
                case 4: color = IM_COL32(0, 255, 100, 255); radius = 7.0f; break;    // Decoy - green
                case 5: color = IM_COL32(0, 200, 255, 255); radius = 8.0f; break;    // Weapon - cyan
                default: color = IM_COL32(255, 255, 255, 255); radius = 6.0f; break;
            }
            radius *= scale;

            float sx = screenPos.x;
            float sy = screenPos.y;

            if (we.type <= 4) {
                // Grenades: filled circle with outline
                drawList->AddCircleFilled(ImVec2(sx, sy), radius, color, 12);
                drawList->AddCircle(
                    ImVec2(sx, sy),
                    radius,
                    IM_COL32(255, 255, 255, 255),
                    12,
                    2.0f * scale);
            } else {
                // Dropped weapons: diamond shape with glow
                drawList->AddQuadFilled(
                    ImVec2(sx, sy - radius),
                    ImVec2(sx + radius, sy),
                    ImVec2(sx, sy + radius),
                    ImVec2(sx - radius, sy),
                    color
                );
                drawList->AddQuad(
                    ImVec2(sx, sy - radius),
                    ImVec2(sx + radius, sy),
                    ImVec2(sx, sy + radius),
                    ImVec2(sx - radius, sy),
                    IM_COL32(255, 255, 255, 255), 2.0f * scale
                );
                // Weapon name below
                char label[32];
                snprintf(label, sizeof(label), "%s", we.name.c_str());
                const ImVec2 labelSize = ImGui::CalcTextSize(label);
                drawList->AddText(
                    ImVec2(
                        sx - labelSize.x / 2.0f,
                        sy + radius + 2.0f * scale),
                    IM_COL32(0, 220, 255, 255),
                    label);
            }
        }
    }

    if (clipToViewport) {
        drawList->PopClipRect();
    }

    // Bomb timer display (moved to renderBombTimer)
}

void esp::renderBombTimer()
{
    BombInfo snapBomb;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        snapBomb = bombInfo;
    }

    if (!menu::bombTimer ||
        !snapBomb.isPlanted ||
        snapBomb.hasExploded ||
        snapBomb.isDefused) {
        return;
    }

    const uint64_t nowMilliseconds = GetTickCount64();
    if (snapBomb.sampledAtMilliseconds == 0 ||
        nowMilliseconds < snapBomb.sampledAtMilliseconds ||
        nowMilliseconds - snapBomb.sampledAtMilliseconds > 1000) {
        return;
    }
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    const float elapsedSeconds =
        static_cast<float>(
            nowMilliseconds -
            snapBomb.sampledAtMilliseconds) /
        1000.0f;
    const float interpolatedCurrentTime =
        snapBomb.curtime + elapsedSeconds;
    float timeLeft =
        snapBomb.blowTime - interpolatedCurrentTime;
    if (timeLeft < 0.0f) timeLeft = 0.0f;

    const char* site = snapBomb.bombSite == 0 ? "A" : "B";
    char bombText[64];
    snprintf(bombText, sizeof(bombText), "BOMB [%s]: %.1fs", site, timeLeft);

    ImU32 bombColor;
    if (timeLeft <= 5.0f)
        bombColor = IM_COL32(255, 0, 0, 255);
    else if (timeLeft <= 10.0f)
        bombColor = IM_COL32(255, 165, 0, 255);
    else
        bombColor = IM_COL32(255, 255, 0, 255);

    const float scale = overlayScale();
    const ImVec2 bombTextSize = ImGui::CalcTextSize(bombText);
    float textX =
        static_cast<float>(VIEWPORT_X) +
        VIEWPORT_W / 2.0f -
        bombTextSize.x / 2.0f;
    float textY = static_cast<float>(VIEWPORT_Y) + 60.0f * scale;
    drawList->AddText(ImVec2(textX, textY), bombColor, bombText);

    if (snapBomb.isDefusing) {
        float defuseLeft =
            snapBomb.defuseCountDown -
            interpolatedCurrentTime;
        if (defuseLeft < 0.0f) defuseLeft = 0.0f;

        char defuseText[64];
        snprintf(defuseText, sizeof(defuseText), "DEFUSING: %.1fs", defuseLeft);

        ImU32 defuseColor;
        if (defuseLeft < timeLeft)
            defuseColor = IM_COL32(0, 255, 0, 255);
        else
            defuseColor = IM_COL32(255, 0, 0, 255);

        const ImVec2 defuseTextSize = ImGui::CalcTextSize(defuseText);
        const float defuseX =
            static_cast<float>(VIEWPORT_X) +
            VIEWPORT_W / 2.0f -
            defuseTextSize.x / 2.0f;
        drawList->AddText(
            ImVec2(defuseX, textY + bombTextSize.y + 3.0f * scale),
            defuseColor,
            defuseText);
    }
}

bool esp::w2s(const vec3& world, vec2& screen, float m[16])
{
    vec4 clipCoords;
    clipCoords.x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
    clipCoords.y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
    clipCoords.w = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];

    if (!std::isfinite(clipCoords.x) ||
        !std::isfinite(clipCoords.y) ||
        !std::isfinite(clipCoords.w) ||
        clipCoords.w < 0.1f ||
        VIEWPORT_W == 0 ||
        VIEWPORT_H == 0) {
        return false;
    }

    vec3 ndc;
    ndc.x = clipCoords.x / clipCoords.w;
    ndc.y = clipCoords.y / clipCoords.w;
    if (!std::isfinite(ndc.x) ||
        !std::isfinite(ndc.y) ||
        std::abs(ndc.x) > 8.0f ||
        std::abs(ndc.y) > 8.0f) {
        return false;
    }

    screen.x =
        static_cast<float>(VIEWPORT_X) +
        (VIEWPORT_W / 2.0f * ndc.x) +
        VIEWPORT_W / 2.0f;
    screen.y =
        static_cast<float>(VIEWPORT_Y) -
        (VIEWPORT_H / 2.0f * ndc.y) +
        VIEWPORT_H / 2.0f;

    const float maxX =
        static_cast<float>(VIEWPORT_W) * 4.0f;
    const float maxY =
        static_cast<float>(VIEWPORT_H) * 4.0f;
    return std::isfinite(screen.x) &&
        std::isfinite(screen.y) &&
        screen.x >= static_cast<float>(VIEWPORT_X) - maxX &&
        screen.x <= static_cast<float>(VIEWPORT_X) +
            static_cast<float>(VIEWPORT_W) + maxX &&
        screen.y >= static_cast<float>(VIEWPORT_Y) - maxY &&
        screen.y <= static_cast<float>(VIEWPORT_Y) +
            static_cast<float>(VIEWPORT_H) + maxY;
}

double esp::player_distance(const vec3& a, const vec3& b)
{
    return std::sqrt(
        std::pow(a.x - b.x, 2) +
        std::pow(a.y - b.y, 2) +
        std::pow(a.z - b.z, 2)
    );
}

// Normalize angle to -180 to 180 range
float esp::normalizeAngle(float angle)
{
    while (angle > 180.0f) angle -= 360.0f;
    while (angle < -180.0f) angle += 360.0f;
    return angle;
}

// Calculate yaw angle from position A to position B
float esp::calculateYawToTarget(const vec3& from, const vec3& to)
{
    float deltaX = to.x - from.x;
    float deltaY = to.y - from.y;
    float yaw = std::atan2(deltaY, deltaX) * (180.0f / 3.14159265f);
    return normalizeAngle(yaw);
}

// Calculate angle difference between enemy view and player direction
float esp::calculateAngleToPlayer(float enemyYaw, const vec3& enemyPos, const vec3& playerPos)
{
    float yawToPlayer = calculateYawToTarget(enemyPos, playerPos);
    float angleDiff = std::abs(normalizeAngle(enemyYaw - yawToPlayer));
    return angleDiff;
}
