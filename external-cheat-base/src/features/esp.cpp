#include "esp.hpp"
#include "menu.hpp"
#include "utils/weapon_names.hpp"
#include "core/memory/game_layout.hpp"
#include "core/renderer/viewport_math.hpp"
#include "imgui.h"
#include <iostream>
#include <cmath>
#include <algorithm>  // For std::min
#include <chrono>

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

    int consecutiveReadFailures = 0;
    std::chrono::steady_clock::time_point lastEntityCacheRefresh{};
    std::chrono::steady_clock::time_point lastWorldDiscovery{};
    std::chrono::steady_clock::time_point lastWorldPositionRefresh{};
    std::chrono::steady_clock::time_point lastBombRefresh{};

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
    cachedWorldEntities.clear();
}

esp::EnemySnapshot esp::getEnemySnapshot()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return enemies;
}

void esp::refreshEntityCache(const menu::RuntimeConfig& config)
{
    uintptr_t entity_list = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwEntityList);
    if (!entity_list) { cachedPawns.clear(); return; }

    uintptr_t localPlayerPawn = memory::Read<uintptr_t>(modBase + cs2_dumper::offsets::client_dll::dwLocalPlayerPawn);
    if (!localPlayerPawn) {
        cachedPawns.clear();
        return;
    }
    uint8_t myTeam = memory::Read<uint8_t>(localPlayerPawn + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
    if (!validTeam(myTeam)) {
        cachedPawns.clear();
        return;
    }

    std::vector<CachedPawn> newCache;
    newCache.reserve(16);

    for (uint32_t i = game_layout::FIRST_PLAYER_CONTROLLER;
         i <= game_layout::LAST_PLAYER_CONTROLLER;
         ++i)
    {
        const uintptr_t entityController =
            entityAddress(entity_list, i);
        if (!entityController) continue;

        bool pawnAlive = false;
        uint32_t pawnHandle = 0;
        if (!memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CCSPlayerController::m_bPawnIsAlive,
                pawnAlive) ||
            !pawnAlive ||
            !memory::TryRead(
                entityController +
                    cs2_dumper::schemas::client_dll::
                        CBasePlayerController::m_hPawn,
                pawnHandle)) {
            continue;
        }
        if (!pawnHandle || pawnHandle == 0xFFFFFFFF) continue;

        const uint32_t pawnIndex =
            pawnHandle & game_layout::ENTITY_HANDLE_MASK;
        const uintptr_t entity =
            entityAddress(entity_list, pawnIndex);
        if (!entity || entity == localPlayerPawn) continue;

        uint8_t team = memory::Read<uint8_t>(entity + cs2_dumper::schemas::client_dll::C_BaseEntity::m_iTeamNum);
        if (!validTeam(team) || team == myTeam) continue;

        CachedPawn cp;
        cp.controllerAddress = entityController;
        cp.pawnAddress = entity;
        cp.pawnHandle = pawnHandle;
        cp.entityIndex = pawnIndex;
        cp.team = team;

        // Read slow-changing data
        if (config.espEnabled && config.espWeapon) {
            cp.weaponName = "Unknown";
            uintptr_t weaponServices = memory::Read<uintptr_t>(entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_pWeaponServices);
            if (weaponServices) {
                uint32_t activeWeaponHandle = memory::Read<uint32_t>(weaponServices + cs2_dumper::schemas::client_dll::CPlayer_WeaponServices::m_hActiveWeapon);
                if (activeWeaponHandle && activeWeaponHandle != 0xFFFFFFFF) {
                    const uint32_t weaponIndex =
                        activeWeaponHandle &
                        game_layout::ENTITY_HANDLE_MASK;
                    const uintptr_t weaponEntity =
                        entityAddress(entity_list, weaponIndex);
                    if (weaponEntity) {
                            uintptr_t attributeManager = weaponEntity + cs2_dumper::schemas::client_dll::C_EconEntity::m_AttributeManager;
                            uint16_t itemDefIndex = memory::Read<uint16_t>(attributeManager + cs2_dumper::schemas::client_dll::C_AttributeContainer::m_Item + cs2_dumper::schemas::client_dll::C_EconItemView::m_iItemDefinitionIndex);
                            cp.weaponName = weapon_names::getWeaponName(itemDefIndex);
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
        publishWorldEntities({});
    }

    void refreshWorldEntityCache(
        const menu::RuntimeConfig& config)
    {
        std::vector<CachedWorldEntity> refreshed;
        if (!config.grenadeESP && !config.droppedWeaponESP) {
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
                const uint32_t ownerHandle = memory::Read<uint32_t>(
                    entity +
                        cs2_dumper::schemas::client_dll::
                            C_BaseEntity::m_hOwnerEntity);
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

        const uintptr_t entityList = memory::Read<uintptr_t>(
            esp::modBase +
                cs2_dumper::offsets::client_dll::dwEntityList);
        if (!entityList) {
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
            clearBombInfo();
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
    vec3 localPos{};
    vec3 viewOffset{};
    vec3 localEyeAngles{};
    uint8_t localTeam = 0;
    if (!memory::TryRead(
            modBase + cs2_dumper::offsets::client_dll::dwViewMatrix,
            localVm) ||
        !memory::TryRead(
            localPlayerPawn + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin,
            localPos) ||
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
        !isFiniteVec3(localPos) ||
        !isFiniteVec3(viewOffset) ||
        !isFiniteVec3(localEyeAngles) ||
        !validTeam(localTeam)) {
        recordUpdateFailure();
        return;
    }
    consecutiveReadFailures = 0;

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
    if (cachedPawns.empty() ||
        lastEntityCacheRefresh.time_since_epoch().count() == 0 ||
        updateNow - lastEntityCacheRefresh >=
            std::chrono::milliseconds(100)) {
        lastEntityCacheRefresh = updateNow;
        refreshEntityCache(config);
    }

    // Fast path: only read position, health, bones, angles from cached pawns
    std::vector<EnemyInfo> buffer;
    buffer.reserve(cachedPawns.size());

    for (const auto& cp : cachedPawns)
    {
        uintptr_t entity = cp.pawnAddress;

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
                        CBasePlayerController::m_hPawn,
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
            health <= 0 ||
            health > 200 ||
            lifeState != 0 ||
            liveTeam != cp.team ||
            liveTeam == localTeam) {
            continue;
        }

        uintptr_t gameSceneNode = 0;
        bool dormant = true;
        if (!memory::TryRead(
                entity +
                    cs2_dumper::schemas::client_dll::
                        C_BaseEntity::m_pGameSceneNode,
                gameSceneNode) ||
            !gameSceneNode ||
            !memory::TryRead(
                gameSceneNode +
                    cs2_dumper::schemas::client_dll::
                        CGameSceneNode::m_bDormant,
                dormant) ||
            dormant) {
            continue;
        }

        vec3 feetPos{};
        vec3 vOffset{};
        if (!memory::TryRead(
                entity + cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_vOldOrigin,
                feetPos) ||
            !memory::TryRead(
                entity + cs2_dumper::schemas::client_dll::C_BaseModelEntity::m_vecViewOffset,
                vOffset) ||
            !isFiniteVec3(feetPos) ||
            !isFiniteVec3(vOffset)) {
            continue;
        }
        vec3 headPos = feetPos + vOffset;

        float distance = static_cast<float>(player_distance(eyePos, feetPos));

        float enemyYaw = 0.0f;
        float angleToPlayer = 180.0f;
        bool viewAngleKnown = false;
        if ((config.espEnabled && config.espViewAngle) ||
            config.radarEnabled ||
            (config.headOffsetEnabled &&
                config.aimbotEnabled)) {
            vec3 eyeAngles{};
            if (memory::TryRead(
                    entity +
                        cs2_dumper::schemas::client_dll::
                            C_CSPlayerPawn::m_angEyeAngles,
                    eyeAngles) &&
                isFiniteVec3(eyeAngles)) {
                enemyYaw = eyeAngles.y;
                angleToPlayer =
                    calculateAngleToPlayer(
                        enemyYaw,
                        feetPos,
                        eyePos);
                viewAngleKnown = true;
            }
        }

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
        config.grenadeESP || config.droppedWeaponESP;
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

    if (!config.bombTimer) {
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
}

void esp::render()
{
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();

    // Take a snapshot of shared data under lock
    EnemySnapshot snapEnemies;
    WorldEntitySnapshot snapWorldEntities;
    viewMatrix snapVm;
    vec3 snapPlayerPos;
    float snapPlayerYaw;
    LocalPlayerCache snapLocalPlayer;
    {
        std::lock_guard<std::mutex> lock(dataMutex);
        snapEnemies = enemies;
        snapWorldEntities = worldEntities;
        snapVm = vm;
        snapPlayerPos = player_position;
        snapPlayerYaw = player_yaw;
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

    // ==================== RADAR OVERLAY ====================
    if (menu::radarEnabled) {


        // Calculate radar center position based on screen size
        const float scale = overlayScale();
        float radarCenterXPx =
            static_cast<float>(VIEWPORT_X) + VIEWPORT_W * menu::radarCenterX;
        float radarCenterYPx =
            static_cast<float>(VIEWPORT_Y) + VIEWPORT_H * menu::radarCenterY;
        float radarRadiusPx = VIEWPORT_H * menu::radarRadius;

        // Draw radar background (semi-transparent circle)
        uint8_t bgR = static_cast<uint8_t>(menu::radarBgColor[0] * 255);
        uint8_t bgG = static_cast<uint8_t>(menu::radarBgColor[1] * 255);
        uint8_t bgB = static_cast<uint8_t>(menu::radarBgColor[2] * 255);
        uint8_t bgA = static_cast<uint8_t>(menu::radarBgColor[3] * 255);
        drawList->AddCircleFilled(
            ImVec2(radarCenterXPx, radarCenterYPx),
            radarRadiusPx,
            IM_COL32(bgR, bgG, bgB, bgA), 32
        );
        // Draw radar border
        drawList->AddCircle(
            ImVec2(radarCenterXPx, radarCenterYPx),
            radarRadiusPx,
            IM_COL32(100, 100, 100, 255), 32, 2.0f * scale
        );

        // Draw player marker at center (you)
        if (menu::radarShowCenter) {
            uint8_t cr = static_cast<uint8_t>(menu::radarCenterColor[0] * 255);
            uint8_t cg = static_cast<uint8_t>(menu::radarCenterColor[1] * 255);
            uint8_t cb = static_cast<uint8_t>(menu::radarCenterColor[2] * 255);
            uint8_t ca = static_cast<uint8_t>(menu::radarCenterColor[3] * 255);

            // Draw player dot at center (1.5x size: 5 * 1.5 = 7.5)
            float playerDotRadius = 7.5f * scale;
            drawList->AddCircleFilled(
                ImVec2(radarCenterXPx, radarCenterYPx),
                playerDotRadius,
                IM_COL32(cr, cg, cb, ca)
            );

            // Draw player direction arrow (pointing up = forward)
            float arrowLen = 14.0f * scale;
            float arrowOffset = playerDotRadius + 2.0f;  // Start from circle edge
            drawList->AddTriangleFilled(
                ImVec2(radarCenterXPx, radarCenterYPx - arrowOffset - arrowLen),  // Top point
                ImVec2(radarCenterXPx - 6.0f * scale, radarCenterYPx - arrowOffset),      // Bottom left
                ImVec2(radarCenterXPx + 6.0f * scale, radarCenterYPx - arrowOffset),      // Bottom right
                IM_COL32(cr, cg, cb, ca)
            );
        }

        // Draw enemy dots on radar
        uint8_t er = static_cast<uint8_t>(menu::radarEnemyColor[0] * 255);
        uint8_t eg = static_cast<uint8_t>(menu::radarEnemyColor[1] * 255);
        uint8_t eb = static_cast<uint8_t>(menu::radarEnemyColor[2] * 255);
        uint8_t ea = static_cast<uint8_t>(menu::radarEnemyColor[3] * 255);

        // Convert player yaw to radians for rotation
        // CS2 yaw: 0 = East, 90 = North, 180/-180 = West, -90 = South
        // We need to rotate so that "up" on radar = player's forward direction
        float rotationRad = (90.0f - snapPlayerYaw) * (3.14159265f / 180.0f);
        float cosRot = std::cos(rotationRad);
        float sinRot = std::sin(rotationRad);

        for (const auto& enemy : *snapEnemies) {
            // Calculate relative position from player to enemy (world coords)
            float deltaX = enemy.position.x - snapPlayerPos.x;
            float deltaY = enemy.position.y - snapPlayerPos.y;

            // Rotate based on player's view direction
            // After rotation: +Y = forward (up on radar), +X = right
            float rotatedX = deltaX * cosRot - deltaY * sinRot;
            float rotatedY = deltaX * sinRot + deltaY * cosRot;

            // Scale the position to fit radar
            float scaleFactor = radarRadiusPx / (2000.0f * menu::radarScale);

            // Map to screen: +X = right, -rotatedY = up (since screen Y increases downward)
            float radarX = radarCenterXPx + rotatedX * scaleFactor;
            float radarY = radarCenterYPx - rotatedY * scaleFactor;

            // Calculate distance from radar center
            float distFromCenter = std::sqrt(
                (radarX - radarCenterXPx) * (radarX - radarCenterXPx) +
                (radarY - radarCenterYPx) * (radarY - radarCenterYPx)
            );

            // Only draw if within radar radius
            if (distFromCenter <= radarRadiusPx) {
                // Draw enemy dot (red by default) - 1.5x size (6 * 1.5 = 9)
                float dotRadius = 9.0f * scale;
                drawList->AddCircleFilled(
                    ImVec2(radarX, radarY),
                    dotRadius,
                    IM_COL32(er, eg, eb, ea)
                );

                if (!enemy.viewAngleKnown) {
                    continue;
                }

                // Get arrow color (white by default, separate from dot)
                uint8_t ar = static_cast<uint8_t>(menu::radarEnemyArrowColor[0] * 255);
                uint8_t ag = static_cast<uint8_t>(menu::radarEnemyArrowColor[1] * 255);
                uint8_t ab = static_cast<uint8_t>(menu::radarEnemyArrowColor[2] * 255);
                uint8_t aa = static_cast<uint8_t>(menu::radarEnemyArrowColor[3] * 255);

                // Draw enemy direction arrow showing where the enemy is FACING
                // enemy.viewYaw is the enemy's absolute facing direction in world coordinates
                // We need to transform it to radar coordinates (relative to player's view)
                //
                // The radar is already rotated so that "up" = player's forward direction
                // So we need to subtract player_yaw from enemy.viewYaw to get relative angle
                //
                // CS2 coordinate system:
                //   Yaw 0 deg = East (+X direction)
                //   Yaw 90 deg = North (+Y direction)
                //   Yaw 180 deg = West (-X direction)
                //   Yaw -90 deg = South (-Y direction)
                //
                // Screen coordinate system (after radar rotation):
                //   0 deg = Up (player's forward)
                //   90 deg = Right
                //   180 deg = Down
                //   -90 deg = Left
                //
                // Relative yaw = enemy.viewYaw - player_yaw
                // Screen angle = -(relative_yaw - 90 deg) in radians
                // This converts from CS2 yaw to screen angle where 0 deg = up

                float relativeYaw = enemy.viewYaw - snapPlayerYaw;
                // Convert to screen coordinates: screen 0 deg (up) = CS2 90 deg (north)
                // Screen angle = 90 deg - relativeYaw, then convert to radians
                // Negate because screen Y increases downward
                float enemyDirRad = (90.0f - relativeYaw) * (3.14159265f / 180.0f);

                // Arrow starts from edge of circle, not center (to avoid overlap)
                float arrowLen = 12.0f * scale;
                float arrowWidth = 6.0f * scale;
                float arrowOffset = dotRadius + 2.0f * scale;  // Start from circle edge + small gap

                // Calculate arrow direction vector (pointing where enemy is facing)
                // cos(angle) gives X component, -sin(angle) gives Y component (screen coords)
                float dirVecX = std::cos(enemyDirRad);
                float dirVecY = -std::sin(enemyDirRad);

                // Calculate arrow base center (at edge of circle)
                float baseCenterX = radarX + arrowOffset * dirVecX;
                float baseCenterY = radarY + arrowOffset * dirVecY;

                // Calculate arrow tip position (extends from base)
                float tipX = baseCenterX + arrowLen * dirVecX;
                float tipY = baseCenterY + arrowLen * dirVecY;

                // Calculate arrow base corners (perpendicular to direction)
                // Perpendicular vector: (-dirVecY, dirVecX)
                float baseX1 = baseCenterX - arrowWidth * (-dirVecY);
                float baseY1 = baseCenterY - arrowWidth * dirVecX;
                float baseX2 = baseCenterX + arrowWidth * (-dirVecY);
                float baseY2 = baseCenterY + arrowWidth * dirVecX;

                // Draw filled triangle arrow (white by default)
                drawList->AddTriangleFilled(
                    ImVec2(tipX, tipY),
                    ImVec2(baseX1, baseY1),
                    ImVec2(baseX2, baseY2),
                    IM_COL32(ar, ag, ab, aa)
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
