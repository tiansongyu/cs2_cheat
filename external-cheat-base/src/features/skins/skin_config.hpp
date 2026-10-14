#pragma once

#include "skin_catalog.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

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
}
