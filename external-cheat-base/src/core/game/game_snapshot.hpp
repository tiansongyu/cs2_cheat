#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace game
{
    inline constexpr std::uint32_t kWebRadarProtocolVersion = 1;

    // A controller slot is stable for the current game session and reveals no
    // external account identifier. Keep generated IDs in the upper namespace
    // so they cannot be confused with ordinary slot values.
    [[nodiscard]] inline constexpr std::uint64_t makeOpaquePlayerId(
        const std::uint32_t controllerSlot) noexcept
    {
        return UINT64_C(0x8000000000000000) | controllerSlot;
    }

    struct WorldPosition
    {
        float x{};
        float y{};
        float z{};
    };

    enum class Team : std::uint8_t
    {
        Unknown = 0,
        Spectator = 1,
        Terrorists = 2,
        CounterTerrorists = 3
    };

    enum class MapPhase : std::uint8_t
    {
        Unknown,
        Loading,
        Warmup,
        FreezeTime,
        Live,
        RoundOver,
        MatchOver
    };

    enum class WeaponCategory : std::uint8_t
    {
        Unknown,
        Knife,
        Pistol,
        Smg,
        Rifle,
        Shotgun,
        MachineGun,
        SniperRifle,
        Grenade,
        Equipment,
        Bomb
    };

    struct WeaponSnapshot
    {
        std::uint16_t definitionIndex{};
        std::string name;
        std::string displayName;
        WeaponCategory category{ WeaponCategory::Unknown };
        int clipAmmo{};
        int reserveAmmo{};
    };

    struct PlayerSnapshot
    {
        // This is a sampler-owned opaque identity, never a Steam ID or a
        // process/pawn address.
        std::uint64_t id{};
        std::optional<std::uint64_t> steamId;
        std::string name;

        Team team{ Team::Unknown };
        int competitiveColor{ -1 };
        bool alive{};
        bool dormant{};

        std::optional<WorldPosition> position;
        float yaw{};

        int health{};
        int armor{};
        int money{};
        bool hasHelmet{};
        bool hasDefuser{};
        bool hasBomb{};

        std::optional<WeaponSnapshot> activeWeapon;
        std::vector<WeaponSnapshot> inventory;
    };

    enum class BombState : std::uint8_t
    {
        Unknown,
        Carried,
        Dropped,
        Planted,
        Defused,
        Exploded
    };

    enum class BombSite : std::uint8_t
    {
        Unknown,
        A,
        B
    };

    struct BombSnapshot
    {
        BombState state{ BombState::Unknown };
        BombSite site{ BombSite::Unknown };
        std::optional<WorldPosition> position;
        std::optional<std::uint64_t> carrierPlayerId;

        std::optional<float> explodeInSeconds;
        bool beingDefused{};
        std::optional<float> defuseInSeconds;
        bool defuseWillSucceed{};
    };

    struct MapState
    {
        std::string id;
        std::string displayName;
        MapPhase phase{ MapPhase::Unknown };
        std::uint32_t roundNumber{};
        bool connected{};
    };

    struct GameSnapshot
    {
        std::uint32_t protocolVersion{ kWebRadarProtocolVersion };
        std::uint64_t sequence{};
        std::uint64_t capturedAtMs{};

        MapState map;
        std::optional<std::uint64_t> localPlayerId;
        Team localTeam{ Team::Unknown };
        std::vector<PlayerSnapshot> players;
        BombSnapshot bomb;
    };
}
