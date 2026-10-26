#pragma once

#include "remote_memory.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace game
{
    struct EntityLayout
    {
        std::uintptr_t entityList{};
        std::uintptr_t localPawn{};
        std::uintptr_t identity{};
        std::uintptr_t identityFlags{};
        std::uintptr_t identityHandle = 0x10;
        std::uintptr_t chunkArray = 0x10;
        std::uintptr_t identityStride = 0x70;
        std::uintptr_t health{};
        std::uintptr_t lifeState{};
        std::uintptr_t team{};
        std::uintptr_t weaponServices{};
        std::uintptr_t weapons{};
        std::uintptr_t activeWeapon{};
        std::uintptr_t owner{};
        std::uintptr_t attributeManager{};
        std::uintptr_t item{};
        std::uintptr_t definition{};
    };

    struct WeaponKey
    {
        std::uintptr_t pawn{};
        std::uint32_t pawnHandle{};
        std::uintptr_t entity{};
        std::uint32_t handle{};
        bool operator==(const WeaponKey&) const = default;
    };

    struct LocalWeapon
    {
        WeaponKey key;
        std::uint16_t definition{};
        bool active{};
    };

    enum class WeaponReadStatus { ready, noLocalPlayer, notAlive, readFailed };
    enum class AccessResult { valid, stale, unreadable };
    enum class WeaponReadIssue { none, localPawn, identity, pawnState, services, inventory, weapon, inventoryChanged };

    struct LocalWeapons
    {
        WeaponReadStatus status = WeaponReadStatus::readFailed;
        std::vector<LocalWeapon> weapons;
        WeaponReadIssue issue = WeaponReadIssue::none;
        bool complete = false;
        std::uint32_t skippedHandles{};
    };

    class EntityAccess
    {
    public:
        EntityAccess(RemoteMemory& memory, EntityLayout layout, std::uintptr_t module)
            : memory_(memory), layout_(layout), module_(module) {}

        [[nodiscard]] const EntityLayout& layout() const { return layout_; }

        bool referenceHandle(const std::uintptr_t entity, std::uint32_t& handle)
        {
            std::uintptr_t identity = 0;
            std::uint32_t flags = 0;
            if (!memory_.readValue(entity + layout_.identity, identity) || !identity ||
                !memory_.readValue(identity + layout_.identityHandle, handle) ||
                !memory_.readValue(identity + layout_.identityFlags, flags) ||
                handle == 0xFFFFFFFFU) {
                return false;
            }
            // CEntityIdentity may store the next serial for a predicted entity.
            handle -= (flags & 1U) << 15U;
            return (handle & 0x7FFFU) != 0;
        }

        AccessResult resolveChecked(const std::uint32_t handle, std::uintptr_t& entity)
        {
            entity = 0;
            if (handle == 0 || handle == 0xFFFFFFFFU) {
                return AccessResult::stale;
            }
            std::uintptr_t list = 0;
            std::uintptr_t chunk = 0;
            const std::uint32_t index = handle & 0x7FFFU;
            std::uint32_t currentHandle = 0;
            if (!index) return AccessResult::stale;
            if (!memory_.readValue(module_ + layout_.entityList, list)) return AccessResult::unreadable;
            if (!list) return AccessResult::stale;
            if (!memory_.readValue(list + layout_.chunkArray + sizeof(std::uintptr_t) * (index >> 9U), chunk)) return AccessResult::unreadable;
            if (!chunk) return AccessResult::stale;
            if (!memory_.readValue(chunk + layout_.identityStride * (index & 0x1FFU), entity)) return AccessResult::unreadable;
            if (!entity) return AccessResult::stale;
            if (!referenceHandle(entity, currentHandle)) return AccessResult::unreadable;
            return currentHandle == handle ? AccessResult::valid : AccessResult::stale;
        }

        std::uintptr_t resolve(const std::uint32_t handle)
        {
            std::uintptr_t entity = 0;
            return resolveChecked(handle, entity) == AccessResult::valid ? entity : 0;
        }

        AccessResult ownership(const WeaponKey& key)
        {
            std::uintptr_t pawn = 0;
            std::uint32_t pawnHandle = 0;
            std::uint32_t owner = 0;
            std::int32_t health = 0;
            std::uint8_t life = 0;
            if (!memory_.readValue(module_ + layout_.localPawn, pawn)) return AccessResult::unreadable;
            if (!pawn || pawn != key.pawn) return AccessResult::stale;
            if (!referenceHandle(pawn, pawnHandle)) return AccessResult::unreadable;
            if (pawnHandle != key.pawnHandle) return AccessResult::stale;
            std::uintptr_t resolved = 0;
            const auto resolution = resolveChecked(key.handle, resolved);
            if (resolution != AccessResult::valid) return resolution;
            if (resolved != key.entity) return AccessResult::stale;
            std::uintptr_t identity = 0;
            if (!memory_.readValue(key.entity + layout_.identity, identity)) return AccessResult::unreadable;
            if (!identity) return AccessResult::stale;
            std::uint32_t handle = 0;
            if (!referenceHandle(key.entity, handle)) return AccessResult::unreadable;
            if (handle != key.handle) return AccessResult::stale;
            if (!memory_.readValue(pawn + layout_.health, health) ||
                !memory_.readValue(pawn + layout_.lifeState, life) ||
                !memory_.readValue(key.entity + layout_.owner, owner)) return AccessResult::unreadable;
            return health > 0 && health <= 200 && life == 0 && owner == key.pawnHandle
                ? AccessResult::valid : AccessResult::stale;
        }

        bool stillOwned(const WeaponKey& key)
        {
            return ownership(key) == AccessResult::valid;
        }

        LocalWeapons sample()
        {
            LocalWeapons result;
            std::uintptr_t pawn = 0;
            if (!memory_.readValue(module_ + layout_.localPawn, pawn)) {
                result.issue = WeaponReadIssue::localPawn;
                return result;
            }
            if (!pawn) {
                result.status = WeaponReadStatus::noLocalPlayer;
                return result;
            }
            std::int32_t health = 0;
            std::uint8_t life = 0;
            std::uint8_t team = 0;
            std::uint32_t pawnHandle = 0;
            if (!referenceHandle(pawn, pawnHandle) ||
                !memory_.readValue(pawn + layout_.health, health) ||
                !memory_.readValue(pawn + layout_.lifeState, life) ||
                !memory_.readValue(pawn + layout_.team, team)) {
                result.issue = WeaponReadIssue::pawnState;
                return result;
            }
            if (health <= 0 || life != 0 || (team != 2 && team != 3)) {
                result.status = WeaponReadStatus::notAlive;
                return result;
            }
            if (health > 200) {
                result.issue = WeaponReadIssue::pawnState;
                return result;
            }
            std::uintptr_t services = 0;
            std::uint32_t active = 0;
            struct RemoteVector
            {
                std::uint32_t count{};
                std::uint32_t padding{};
                std::uintptr_t elements{};
                bool operator==(const RemoteVector&) const = default;
            };
            static_assert(sizeof(RemoteVector) == 16);
            RemoteVector vector;
            if (!memory_.readValue(pawn + layout_.weaponServices, services) || !services ||
                !memory_.readValue(services + layout_.activeWeapon, active) ||
                !memory_.readValue(services + layout_.weapons, vector) ||
                vector.count > 32 || (vector.count && !vector.elements)) {
                result.issue = WeaponReadIssue::inventory;
                return result;
            }
            std::array<std::uint32_t, 32> handles{};
            if (vector.count && !memory_.read(vector.elements, handles.data(), vector.count * sizeof(handles[0]))) {
                result.issue = WeaponReadIssue::inventory;
                return result;
            }
            result.complete = true;
            for (std::uint32_t index = 0; index < vector.count; ++index) {
                const auto handle = handles[index];
                bool duplicate = false;
                for (std::uint32_t previous = 0; previous < index; ++previous) {
                    duplicate = duplicate || handles[previous] == handle;
                }
                if (duplicate || handle == 0 || handle == 0xFFFFFFFFU) {
                    continue;
                }
                const std::uintptr_t entity = resolve(handle);
                std::uint32_t owner = 0;
                std::uint16_t definition = 0;
                if (!entity || !memory_.readValue(entity + layout_.owner, owner) ||
                    !memory_.readValue(entity + layout_.attributeManager + layout_.item + layout_.definition, definition)) {
                    result.complete = false;
                    result.issue = WeaponReadIssue::weapon;
                    ++result.skippedHandles;
                    continue;
                }
                if (owner == pawnHandle) {
                    result.weapons.push_back({{pawn, pawnHandle, entity, handle}, definition, handle == active});
                }
            }
            RemoteVector check;
            std::array<std::uint32_t, 32> checkedHandles{};
            std::uintptr_t pawnCheck = 0;
            if (!memory_.readValue(services + layout_.weapons, check) || check != vector ||
                !memory_.readValue(module_ + layout_.localPawn, pawnCheck) || pawnCheck != pawn ||
                (vector.count && !memory_.read(vector.elements, checkedHandles.data(), vector.count * sizeof(handles[0]))) ||
                checkedHandles != handles) {
                result.weapons.clear();
                result.complete = false;
                result.issue = WeaponReadIssue::inventoryChanged;
                return result;
            }
            result.status = WeaponReadStatus::ready;
            return result;
        }

    private:
        RemoteMemory& memory_;
        EntityLayout layout_;
        std::uintptr_t module_;
    };
}
