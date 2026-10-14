#include "features/skins/skin_changer.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

namespace
{
    class FakeMemory final : public game::RemoteMemory
    {
    public:
        std::vector<unsigned char> bytes = std::vector<unsigned char>(0x40000);
        bool allowed = true;
        std::uintptr_t unreadable{};
        std::uintptr_t verifyFailure{};
        std::uintptr_t failWriteAt{};
        bool partialWrite = false;
        std::uint64_t writes{};

        bool read(const std::uintptr_t address, void* value, const std::size_t size) override
        {
            if (address == unreadable || address == verifyFailure || !address || address > bytes.size() || size > bytes.size() - address) {
                if (address == verifyFailure) verifyFailure = 0;
                return false;
            }
            std::memcpy(value, bytes.data() + address, size);
            return true;
        }
        bool write(const std::uintptr_t address, const void* value, const std::size_t size) override
        {
            assert(allowed);
            ++writes;
            if (address == failWriteAt) {
                failWriteAt = 0;
                if (partialWrite) std::memcpy(bytes.data() + address, value, 1);
                return false;
            }
            if (!address || address > bytes.size() || size > bytes.size() - address) return false;
            std::memcpy(bytes.data() + address, value, size);
            return true;
        }
        [[nodiscard]] bool writesAllowed() const override { return allowed; }

        template <typename T> void put(const std::uintptr_t address, const T value)
        {
            assert(address + sizeof(value) <= bytes.size());
            std::memcpy(bytes.data() + address, &value, sizeof(value));
        }
        template <typename T> T get(const std::uintptr_t address)
        {
            T value{};
            assert(readValue(address, value));
            return value;
        }
    };

    game::EntityLayout entityLayout()
    {
        game::EntityLayout layout;
        layout.entityList = 0x100;
        layout.localPawn = 0x108;
        layout.identity = 0x10;
        layout.identityFlags = 0x30;
        layout.health = 0x40;
        layout.lifeState = 0x44;
        layout.team = 0x45;
        layout.weaponServices = 0x50;
        layout.weapons = 0x48;
        layout.activeWeapon = 0x60;
        layout.owner = 0x60;
        layout.attributeManager = 0x500;
        layout.item = 0x50;
        layout.definition = 0x10;
        return layout;
    }

    skins::SkinLayout skinLayout()
    {
        skins::SkinLayout layout;
        layout.paintKit = 0x80;
        layout.seed = 0x84;
        layout.wear = 0x88;
        layout.itemIdHigh = 0x18;
        layout.initialized = 0x20;
        layout.disallowSoc = 0x21;
        layout.visualsDataSet = 0x90;
        layout.sceneNode = 0x98;
        layout.sceneOwner = 8;
        layout.sceneChild = 16;
        layout.sceneSibling = 24;
        layout.modelState = 0x40;
        layout.meshMask = 0x28;
        layout.hudArms = 0xA0;
        layout.attributeList = 0x40;
        layout.attributes = 8;
        layout.attributeDefinition = 0x30;
        layout.attributeValue = 0x34;
        layout.attributeInitialValue = 0x38;
        return layout;
    }

    struct Fixture
    {
        static constexpr std::uintptr_t module = 0x1000;
        static constexpr std::uintptr_t list = 0x2000;
        static constexpr std::uintptr_t chunk = 0x3000;
        static constexpr std::uintptr_t pawn = 0x10000;
        static constexpr std::uintptr_t pawnIdentity = 0x11000;
        static constexpr std::uintptr_t weapon = 0x12000;
        static constexpr std::uintptr_t weaponIdentity = 0x13000;
        static constexpr std::uintptr_t services = 0x14000;
        static constexpr std::uintptr_t handles = 0x15000;
        static constexpr std::uintptr_t node = 0x16000;
        static constexpr std::uintptr_t item = weapon + 0x550;
        static constexpr std::uint32_t pawnHandle = 0x18001;
        static constexpr std::uint32_t weaponHandle = 0x8000 | 65;
        FakeMemory memory;
        game::EntityAccess access{memory, entityLayout(), module};
        skins::MemoryBackend backend{memory, entityLayout(), skinLayout(), module};
        skins::SkinChanger changer{backend};
        skins::Settings settings;
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::time_point{} + std::chrono::seconds(10);

        Fixture()
        {
            memory.put(module + 0x100, list);
            memory.put(module + 0x108, pawn);
            memory.put(list + 0x10, chunk);
            memory.put(chunk + 0x70, pawn);
            memory.put(chunk + 0x70 * 65, weapon);
            memory.put(pawn + 0x10, pawnIdentity);
            memory.put(pawnIdentity + 0x10, pawnHandle);
            memory.put(pawn + 0x40, std::int32_t{100});
            memory.put(pawn + 0x45, std::uint8_t{2});
            memory.put(pawn + 0x50, services);
            memory.put(services + 0x48, std::uint32_t{1});
            memory.put(services + 0x50, handles);
            memory.put(services + 0x60, weaponHandle);
            memory.put(handles, weaponHandle);
            memory.put(weapon + 0x10, weaponIdentity);
            memory.put(weaponIdentity + 0x10, weaponHandle);
            memory.put(weapon + 0x60, pawnHandle);
            memory.put(item + 0x10, std::uint16_t{7});
            memory.put(item + 0x18, std::uint32_t{123});
            memory.put(item + 0x20, std::uint8_t{1});
            memory.put(weapon + 0x90, std::uint8_t{1});
            memory.put(weapon + 0x98, node);
            memory.put(node + 8, weapon);
            memory.put(node + 0x68, std::uint64_t{1});
            settings.profiles[0] = {7, 282, 0.2f, 42};
            assert(skins::validSettings(settings));
        }

        void tick(const bool enabled = true, const std::uint64_t request = 0,
                  const bool foreground = true, const bool diagnostics = false)
        {
            changer.tick(settings, enabled, foreground, diagnostics, request, now);
            now += std::chrono::seconds(1);
        }
    };

    void testCatalogAndPersistence()
    {
        assert(skins::paintKits.size() >= 1400);
        for (const auto& weapon : skins::weapons) {
            assert(std::any_of(skins::paintKits.begin(), skins::paintKits.end(), [&weapon](const auto& kit) { return kit.weapon == weapon.id; }));
        }
        assert(!skins::validProfile({7, 344, 0.2f, 0})); // AWP Dragon Lore on AK-47.
        assert(!skins::validProfile({7, 282, 0.01f, 0})); // Redline minimum wear.
        assert(!skins::validProfile({7, 282, std::numeric_limits<float>::quiet_NaN(), 0}));
        assert(!skins::validProfile({7, 282, 0.2f, 1001}));
        const auto directory = std::filesystem::temp_directory_path() /
            ("cs2-skins-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto path = directory / "skins-v1.ini";
        Fixture fixture;
        assert(skins::saveSettings(path, fixture.settings).empty());
        auto loaded = skins::loadSettings(path);
        assert(loaded.error.empty() && loaded.settings == fixture.settings);
        fixture.settings.profiles[0].seed = 999;
        assert(skins::saveSettings(path, fixture.settings).empty());
        loaded = skins::loadSettings(path);
        assert(loaded.error.empty() && loaded.settings == fixture.settings);
        auto invalid = fixture.settings;
        invalid.profiles[0].wear = std::numeric_limits<float>::infinity();
        assert(!skins::saveSettings(path, invalid).empty());
        assert(skins::loadSettings(path).settings == fixture.settings);
        {
            std::ofstream file(path);
            file << "[skins]\nschema=1\n[weapon_7]\npaint_kit=282\nwear=nan\nseed=0\n";
        }
        loaded = skins::loadSettings(path);
        assert(!loaded.error.empty() && loaded.settings == skins::Settings{});
        {
            std::ofstream file(path);
            file << "[skins]\nschema=2\n";
        }
        assert(!skins::loadSettings(path).error.empty());
        {
            std::ofstream file(path);
            file << std::string(65537, 'x');
        }
        assert(!skins::loadSettings(path).error.empty());
        std::filesystem::remove_all(directory);
    }

    void testIdentityAndInventory()
    {
        Fixture fixture;
        assert(fixture.access.sample().status == game::WeaponReadStatus::ready);
        assert(fixture.access.resolve(Fixture::weaponHandle) == Fixture::weapon);
        assert(fixture.access.resolve(Fixture::weaponHandle + 0x8000) == 0);
        fixture.memory.put(Fixture::weaponIdentity + 0x10, Fixture::weaponHandle + 0x8000);
        fixture.memory.put(Fixture::weaponIdentity + 0x30, std::uint32_t{1});
        assert(fixture.access.resolve(Fixture::weaponHandle) == Fixture::weapon);
        fixture.memory.put(Fixture::services + 0x48, std::uint32_t{33});
        assert(fixture.access.sample().status == game::WeaponReadStatus::readFailed);
        fixture.memory.put(Fixture::services + 0x48, std::uint32_t{2});
        fixture.memory.put(Fixture::handles + 4, Fixture::weaponHandle);
        assert(fixture.access.sample().weapons.size() == 1);
        fixture.memory.put(Fixture::weapon + 0x60, std::uint32_t{0x20001});
        assert(fixture.access.sample().weapons.empty());
    }

    void testWritesAndRestore()
    {
        Fixture fixture;
        fixture.memory.allowed = false;
        fixture.tick();
        assert(fixture.memory.writes == 0 && fixture.changer.status().state == skins::State::writeLocked);
        fixture.memory.allowed = true;
        fixture.tick();
        assert(fixture.changer.status().state == skins::State::applied);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 282);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x84) == 42);
        assert(fixture.memory.get<float>(Fixture::weapon + 0x88) == 0.2f);
        assert(fixture.memory.get<std::uint32_t>(Fixture::item + 0x18) == 0xFFFFFFFFU);
        assert(fixture.memory.get<std::uint64_t>(Fixture::node + 0x68) == 2);
        const auto writes = fixture.memory.writes;
        fixture.memory.put(Fixture::weapon + 0x90, std::uint8_t{1}); // Client consumed the invalidation.
        for (int tick = 0; tick < 10; ++tick) fixture.tick();
        assert(fixture.memory.writes == writes);
        fixture.tick(true, 1);
        assert(fixture.memory.get<std::uint8_t>(Fixture::weapon + 0x90) == 0);
        fixture.tick(false, 1);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 0);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x84) == 0);
        assert(fixture.memory.get<float>(Fixture::weapon + 0x88) == 0);
        assert(fixture.memory.get<std::uint32_t>(Fixture::item + 0x18) == 123);
        assert(fixture.memory.get<std::uint64_t>(Fixture::node + 0x68) == 1);
        assert(fixture.changer.status().trackedWeapons == 0);
    }

    void testPauseReadFailuresAndConflicts()
    {
        Fixture fixture;
        fixture.tick();
        const auto writes = fixture.memory.writes;
        fixture.tick(false, 0, false);
        assert(fixture.memory.writes == writes && fixture.changer.status().trackedWeapons == 1);
        fixture.memory.unreadable = Fixture::module + 0x108;
        fixture.tick(false);
        assert(fixture.memory.writes == writes && fixture.changer.status().state == skins::State::readFailed);
        fixture.memory.unreadable = 0;
        fixture.memory.put(Fixture::weapon + 0x84, std::int32_t{123}); // Game replaced this field.
        fixture.tick(false);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x84) == 123);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 0);
        assert(fixture.changer.status().skippedFields > 0);
    }

    void testPartialFailures()
    {
        for (const auto failure : {Fixture::weapon + 0x80, Fixture::weapon + 0x88, Fixture::item + 0x18}) {
            Fixture fixture;
            fixture.memory.failWriteAt = failure;
            fixture.memory.partialWrite = true;
            fixture.tick();
            assert(fixture.changer.status().state == skins::State::failed);
            assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 0);
            assert(fixture.memory.get<float>(Fixture::weapon + 0x88) == 0);
            assert(fixture.memory.get<std::uint32_t>(Fixture::item + 0x18) == 123);
            fixture.tick();
            assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 282);
            fixture.tick(false);
            assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 0);
        }
    }

    void testSettingsChangesAndOverwriteBudget()
    {
        Fixture fixture;
        fixture.tick();
        fixture.settings.profiles[0].wear = 0.3f;
        fixture.settings.profiles[0].seed = 100;
        fixture.tick(true, 1);
        assert(fixture.memory.get<float>(Fixture::weapon + 0x88) == 0.3f);
        fixture.tick(false);
        assert(fixture.memory.get<float>(Fixture::weapon + 0x88) == 0);
        fixture.tick();
        for (int index = 0; index < 2; ++index) {
            fixture.memory.put(Fixture::weapon + 0x80, std::int32_t{0});
            fixture.tick();
        }
        fixture.tick(); // A stable third application remains successful.
        assert(fixture.changer.status().state == skins::State::applied);
        fixture.memory.put(Fixture::weapon + 0x80, std::int32_t{0});
        const auto writes = fixture.memory.writes;
        fixture.tick();
        fixture.tick();
        assert(fixture.memory.writes == writes && fixture.changer.status().state == skins::State::failed);
        fixture.tick(true, 2);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 282);
        fixture.tick(false);
        assert(fixture.changer.status().trackedWeapons == 0);
    }

    void testEntityReuseAndDeath()
    {
        Fixture fixture;
        fixture.tick();
        fixture.memory.put(Fixture::pawn + 0x40, std::int32_t{0});
        const auto writes = fixture.memory.writes;
        fixture.tick();
        assert(fixture.memory.writes == writes && fixture.changer.status().state == skins::State::waiting);
        fixture.memory.put(Fixture::pawn + 0x40, std::int32_t{100});
        const auto newHandle = Fixture::weaponHandle + 0x8000;
        fixture.memory.put(Fixture::handles, newHandle);
        fixture.memory.put(Fixture::services + 0x60, newHandle);
        fixture.memory.put(Fixture::weaponIdentity + 0x10, newHandle);
        fixture.memory.put(Fixture::weapon + 0x80, std::int32_t{180});
        fixture.tick();
        fixture.tick(false);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 180);
    }

    void testAttributeGuards()
    {
        Fixture fixture;
        constexpr std::uintptr_t attributes = 0x19000;
        fixture.memory.put(Fixture::item + 0x48, std::uint64_t{3});
        fixture.memory.put(Fixture::item + 0x50, attributes);
        for (std::uint16_t index = 0; index < 3; ++index) {
            fixture.memory.put(attributes + index * 0x48 + 0x30, static_cast<std::uint16_t>(6 + index));
            fixture.memory.put(attributes + index * 0x48 + 0x34, 0.5f);
            fixture.memory.put(attributes + index * 0x48 + 0x38, 0.5f);
        }
        fixture.tick();
        assert(fixture.memory.get<float>(attributes + 0x34) == 282.0f);
        fixture.memory.put(Fixture::item + 0x50, std::uintptr_t{0x1A000});
        fixture.tick(false);
        assert(fixture.memory.get<float>(attributes + 0x34) == 282.0f); // Retired allocation is untouched.
        assert(fixture.changer.status().skippedFields >= 6);
    }

    void testViewModelGuardsAndCycles()
    {
        Fixture fixture;
        constexpr std::uintptr_t arms = 0x1C000;
        constexpr std::uintptr_t armsIdentity = 0x1D000;
        constexpr std::uintptr_t view = 0x1E000;
        constexpr std::uintptr_t viewIdentity = 0x1F000;
        constexpr std::uintptr_t armsNode = 0x20000;
        constexpr std::uintptr_t viewNode = 0x21000;
        constexpr std::uint32_t armsHandle = 0x8000 | 128;
        fixture.memory.put(Fixture::chunk + 0x70 * 128, arms);
        fixture.memory.put(arms + 0x10, armsIdentity);
        fixture.memory.put(armsIdentity + 0x10, armsHandle);
        fixture.memory.put(Fixture::pawn + 0xA0, armsHandle);
        fixture.memory.put(arms + 0x98, armsNode);
        fixture.memory.put(armsNode + 16, viewNode);
        fixture.memory.put(view + 0x98, viewNode);
        fixture.memory.put(view + 0x10, viewIdentity);
        fixture.memory.put(viewIdentity + 0x10, std::uint32_t{0x8000 | 129});
        fixture.memory.put(view + 0x60, Fixture::weaponHandle);
        fixture.memory.put(viewNode + 8, view);
        fixture.memory.put(viewNode + 0x68, std::uint64_t{1});
        fixture.tick();
        assert(fixture.memory.get<std::uint64_t>(viewNode + 0x68) == 2);
        fixture.memory.put(view + 0x60, std::uint32_t{0x8000 | 130});
        fixture.tick(false);
        assert(fixture.memory.get<std::uint64_t>(viewNode + 0x68) == 2);
        assert(fixture.memory.get<std::uint64_t>(Fixture::node + 0x68) == 1);
        fixture.memory.put(viewNode + 24, viewNode); // Corrupt cyclic child list.
        fixture.tick(); // Must finish without writing that view model.
        assert(fixture.changer.status().state == skins::State::applied);
    }

    void testInvalidLayoutAndShutdown()
    {
        Fixture fixture;
        fixture.memory.put(Fixture::item + 0x21, std::uint8_t{200});
        for (int index = 0; index < 6; ++index) fixture.tick();
        assert(fixture.memory.writes == 0 && fixture.changer.status().failures == 3);
        fixture.memory.put(Fixture::item + 0x21, std::uint8_t{0});
        fixture.tick(true, 1);
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 282);
        fixture.changer.shutdown();
        assert(fixture.memory.get<int>(Fixture::weapon + 0x80) == 0);
    }
}

int main()
{
    testCatalogAndPersistence();
    testIdentityAndInventory();
    testWritesAndRestore();
    testPauseReadFailuresAndConflicts();
    testPartialFailures();
    testSettingsChangesAndOverwriteBudget();
    testEntityReuseAndDeath();
    testAttributeGuards();
    testViewModelGuardsAndCycles();
    testInvalidLayoutAndShutdown();
}
