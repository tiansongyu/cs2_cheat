#pragma once

#include "core/game/entity_access.hpp"
#include "skin_config.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace skins
{
    struct SkinLayout
    {
        std::uintptr_t paintKit{};
        std::uintptr_t seed{};
        std::uintptr_t wear{};
        std::uintptr_t itemIdHigh{};
        std::uintptr_t initialized{};
        std::uintptr_t disallowSoc{};
        std::uintptr_t visualsDataSet{};
        std::uintptr_t sceneNode{};
        std::uintptr_t sceneOwner{};
        std::uintptr_t sceneChild{};
        std::uintptr_t sceneSibling{};
        std::uintptr_t modelState{};
        std::uintptr_t meshMask{};
        std::uintptr_t hudArms{};
        std::uintptr_t attributeList{};
        std::uintptr_t attributes{};
        std::uintptr_t attributeDefinition{};
        std::uintptr_t attributeValue{};
        std::uintptr_t attributeInitialValue{};
        std::uintptr_t attributeStride = 0x48;
    };

    struct ValueGuard
    {
        std::uintptr_t address{};
        std::uint64_t value{};
        std::uint8_t size{};
        bool operator==(const ValueGuard&) const = default;
    };

    struct Mutation
    {
        std::uintptr_t address{};
        std::uint64_t before{};
        std::uint64_t after{};
        std::uint8_t size{};
        std::array<ValueGuard, 4> guards{};
        bool restore = true;
        bool written = false;
        bool budgeted = true;
    };

    struct WriteResult
    {
        bool succeeded = false;
        std::uint32_t writes{};
        std::uint32_t restored{};
        std::uint32_t skipped{};
        std::uint32_t failures{};
    };

    struct WeaponDiagnostic
    {
        std::uint16_t weapon{};
        std::int32_t paintKit{};
        float wear{};
        bool active{};
        bool readSucceeded{};
    };

    class Backend
    {
    public:
        virtual ~Backend() = default;
        [[nodiscard]] virtual bool writesAllowed() const = 0;
        virtual game::LocalWeapons sample() = 0;
        virtual bool plan(const game::LocalWeapon& weapon, const WeaponSkinConfig& profile,
                          bool forceRefresh, std::vector<Mutation>& mutations) = 0;
        virtual WriteResult apply(const game::WeaponKey& key, std::vector<Mutation>& mutations) = 0;
        virtual WriteResult restore(const game::WeaponKey& key, std::vector<Mutation>& mutations) = 0;
        virtual WeaponDiagnostic inspect(const game::LocalWeapon& weapon) = 0;
    };

    class MemoryBackend final : public Backend
    {
    public:
        MemoryBackend(game::RemoteMemory& memory, game::EntityLayout entities,
                      SkinLayout skins, std::uintptr_t module)
            : memory_(memory), access_(memory, entities, module), layout_(skins) {}

        [[nodiscard]] bool writesAllowed() const override { return memory_.writesAllowed(); }
        game::LocalWeapons sample() override { return access_.sample(); }
        bool plan(const game::LocalWeapon& weapon, const WeaponSkinConfig& profile,
                  bool forceRefresh, std::vector<Mutation>& mutations) override;
        WriteResult apply(const game::WeaponKey& key, std::vector<Mutation>& mutations) override;
        WriteResult restore(const game::WeaponKey& key, std::vector<Mutation>& mutations) override;
        WeaponDiagnostic inspect(const game::LocalWeapon& weapon) override;

    private:
        bool readBits(std::uintptr_t address, std::uint8_t size, std::uint64_t& value);
        bool guardsMatch(const Mutation& mutation);
        bool append(std::vector<Mutation>& mutations, std::uintptr_t address,
                    std::uint64_t value, std::uint8_t size,
                    const std::array<ValueGuard, 4>& guards = {}, bool restore = true);
        void appendMesh(std::vector<Mutation>& mutations, std::uintptr_t entity,
                        std::uint64_t mask);
        bool appendAttributes(std::vector<Mutation>& mutations, std::uintptr_t item,
                              const WeaponSkinConfig& profile);
        void invalidate(const game::WeaponKey& key);

        game::RemoteMemory& memory_;
        game::EntityAccess access_;
        SkinLayout layout_;
    };
}
