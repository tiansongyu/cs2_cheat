// Run against this test process, never a game. Exercises the production adapter
// and the generated layout through real ReadProcessMemory/WriteProcessMemory.
#include "features/skins/skin_runtime.hpp"
#include "core/memory/memory.hpp"
#include "core/memory/game_layout.hpp"
#include "client_dll.hpp"
#include "offsets.hpp"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
    struct Allocations
    {
        std::vector<void*> blocks;
        ~Allocations()
        {
            for (auto block : blocks) VirtualFree(block, 0, MEM_RELEASE);
        }
        std::uintptr_t allocate(const std::size_t size)
        {
            auto block = VirtualAlloc(nullptr, size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            assert(block);
            blocks.push_back(block);
            return reinterpret_cast<std::uintptr_t>(block);
        }
    };

    template <typename T> void put(const std::uintptr_t address, const T value)
    {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(value));
    }
}

int main()
{
    using namespace cs2_dumper::schemas::client_dll;
    using namespace cs2_dumper::offsets::client_dll;
    Allocations allocations;
    const auto module = allocations.allocate(static_cast<std::size_t>(std::max(dwEntityList, dwLocalPlayerPawn)) + 4096);
    const auto list = allocations.allocate(4096);
    const auto chunk = allocations.allocate(0x10000);
    const auto pawn = allocations.allocate(0x4000);
    const auto pawnIdentity = allocations.allocate(4096);
    const auto weapon = allocations.allocate(0x4000);
    const auto weaponIdentity = allocations.allocate(4096);
    const auto services = allocations.allocate(4096);
    const auto handles = allocations.allocate(4096);
    const auto node = allocations.allocate(4096);
    constexpr std::uint32_t pawnHandle = 0x18001;
    constexpr std::uint32_t weaponHandle = 0x8000 | 65;
    put(module + dwEntityList, list);
    put(module + dwLocalPlayerPawn, pawn);
    put(list + game_layout::ENTITY_LIST_CHUNK_ARRAY, chunk);
    put(chunk + game_layout::ENTITY_IDENTITY_STRIDE, pawn);
    put(chunk + game_layout::ENTITY_IDENTITY_STRIDE * 65, weapon);
    put(pawn + CEntityInstance::m_pEntity, pawnIdentity);
    put(pawnIdentity + game_layout::ENTITY_IDENTITY_HANDLE, pawnHandle);
    put(pawn + C_BaseEntity::m_iHealth, std::int32_t{100});
    put(pawn + C_BaseEntity::m_iTeamNum, std::uint8_t{2});
    put(pawn + C_BasePlayerPawn::m_pWeaponServices, services);
    put(services + CPlayer_WeaponServices::m_hMyWeapons, std::uint32_t{1});
    put(services + CPlayer_WeaponServices::m_hMyWeapons + 8, handles);
    put(services + CPlayer_WeaponServices::m_hActiveWeapon, weaponHandle);
    put(handles, weaponHandle);
    put(weapon + CEntityInstance::m_pEntity, weaponIdentity);
    put(weaponIdentity + game_layout::ENTITY_IDENTITY_HANDLE, weaponHandle);
    put(weapon + C_BaseEntity::m_hOwnerEntity, pawnHandle);
    const auto item = weapon + C_EconEntity::m_AttributeManager + C_AttributeContainer::m_Item;
    put(item + C_EconItemView::m_iItemDefinitionIndex, std::uint16_t{7});
    put(item + C_EconItemView::m_iItemIDHigh, std::uint32_t{123});
    put(item + C_EconItemView::m_bInitialized, std::uint8_t{1});
    put(weapon + C_CSWeaponBase::m_bVisualsDataSet, std::uint8_t{1});
    put(weapon + C_BaseEntity::m_pGameSceneNode, node);
    put(node + CGameSceneNode::m_pOwner, weapon);
    put(node + CSkeletonInstance::m_modelState + CModelState::m_MeshGroupMask, std::uint64_t{1});

    memory::pID = GetCurrentProcessId();
    memory::gHandle = OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentProcessId());
    assert(memory::gHandle);
    skins::Settings settings;
    settings.profiles[0] = {7, 282, 0.2f, 42};
    skins::Runtime runtime(module);
    memory::SetWritesAllowed(false);
    runtime.tick(settings, true, true, true, 0);
    assert(runtime.status().state == skins::State::writeLocked);
    assert(memory::Read<int>(weapon + C_EconEntity::m_nFallbackPaintKit) == 0);
    memory::SetWritesAllowed(true);
    runtime.tick(settings, true, true, true, 1);
    assert(runtime.status().state == skins::State::applied);
    assert(memory::Read<int>(weapon + C_EconEntity::m_nFallbackPaintKit) == 282);
    assert(memory::Read<float>(weapon + C_EconEntity::m_flFallbackWear) == 0.2f);
    assert(memory::Read<std::uint32_t>(item + C_EconItemView::m_iItemIDHigh) == 0xFFFFFFFFU);
    runtime.tick(settings, false, true, true, 2);
    assert(runtime.status().trackedWeapons == 0);
    assert(memory::Read<int>(weapon + C_EconEntity::m_nFallbackPaintKit) == 0);
    assert(memory::Read<std::uint32_t>(item + C_EconItemView::m_iItemIDHigh) == 123);
    runtime.shutdown();
    memory::SetWritesAllowed(false);
    memory::Close();
}
