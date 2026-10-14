#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace skins
{
    struct WeaponDefinition
    {
        std::uint16_t id;
        std::string_view name;
    };

    inline constexpr std::array weapons{
        WeaponDefinition{7, "AK-47"}, WeaponDefinition{16, "M4A4"},
        WeaponDefinition{60, "M4A1-S"}, WeaponDefinition{9, "AWP"},
        WeaponDefinition{4, "Glock-18"}, WeaponDefinition{61, "USP-S"},
        WeaponDefinition{1, "Desert Eagle"}, WeaponDefinition{8, "AUG"},
        WeaponDefinition{63, "CZ75-Auto"}, WeaponDefinition{2, "Dual Berettas"},
        WeaponDefinition{10, "FAMAS"}, WeaponDefinition{3, "Five-SeveN"},
        WeaponDefinition{11, "G3SG1"}, WeaponDefinition{13, "Galil AR"},
        WeaponDefinition{14, "M249"}, WeaponDefinition{17, "MAC-10"},
        WeaponDefinition{27, "MAG-7"}, WeaponDefinition{23, "MP5-SD"},
        WeaponDefinition{33, "MP7"}, WeaponDefinition{34, "MP9"},
        WeaponDefinition{28, "Negev"}, WeaponDefinition{35, "Nova"},
        WeaponDefinition{32, "P2000"}, WeaponDefinition{36, "P250"},
        WeaponDefinition{19, "P90"}, WeaponDefinition{26, "PP-Bizon"},
        WeaponDefinition{64, "R8 Revolver"}, WeaponDefinition{38, "SCAR-20"},
        WeaponDefinition{39, "SG 553"}, WeaponDefinition{40, "SSG 08"},
        WeaponDefinition{29, "Sawed-Off"}, WeaponDefinition{30, "Tec-9"},
        WeaponDefinition{24, "UMP-45"}, WeaponDefinition{25, "XM1014"}
    };

    struct PaintKit
    {
        std::uint16_t weapon;
        std::int32_t id;
        std::string_view name;
        float minimumWear;
        float maximumWear;
        bool legacyModel;
    };

    [[nodiscard]] inline std::string_view weaponName(const std::uint16_t id)
    {
        for (const auto& weapon : weapons) {
            if (weapon.id == id) return weapon.name;
        }
        return "Unsupported weapon";
    }
}

#include "skin_catalog_data.hpp"

namespace skins
{
    [[nodiscard]] inline const PaintKit* findPaintKit(const std::uint16_t weapon, const std::int32_t id)
    {
        for (const auto& kit : paintKits) {
            if (kit.weapon == weapon && kit.id == id) return &kit;
        }
        return nullptr;
    }
}
