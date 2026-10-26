#pragma once

#include "skin_catalog.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace skins
{
    struct WeaponSkinConfig
    {
        std::uint16_t weapon{};
        std::int32_t paintKit{}; // Zero means restore the sampled original.
        float wear = 0.1f;
        std::int32_t seed{};
        bool operator==(const WeaponSkinConfig&) const = default;
    };

    struct Settings
    {
        std::array<WeaponSkinConfig, weapons.size()> profiles{};

        Settings()
        {
            for (std::size_t index = 0; index < weapons.size(); ++index) {
                profiles[index].weapon = weapons[index].id;
            }
        }

        [[nodiscard]] const WeaponSkinConfig* find(const std::uint16_t weapon) const
        {
            for (const auto& profile : profiles) {
                if (profile.weapon == weapon) return &profile;
            }
            return nullptr;
        }

        bool operator==(const Settings&) const = default;
    };

    [[nodiscard]] bool validProfile(const WeaponSkinConfig& profile);
    [[nodiscard]] bool validSettings(const Settings& settings);

    struct LoadResult
    {
        Settings settings;
        std::string error;
    };

    [[nodiscard]] LoadResult loadSettings(const std::filesystem::path& path);
    // Returns an empty string on success. A failed save leaves the old file intact.
    [[nodiscard]] std::string saveSettings(const std::filesystem::path& path, const Settings& settings);
    [[nodiscard]] bool validPresetName(std::string_view name);
    struct PresetList
    {
        std::vector<std::string> names;
        std::string error;
    };
    [[nodiscard]] PresetList listPresets(const std::filesystem::path& directory);
    [[nodiscard]] std::string savePreset(const std::filesystem::path& directory,
                                         std::string_view name, const Settings& settings);
    [[nodiscard]] LoadResult loadPreset(const std::filesystem::path& directory, std::string_view name);
}
