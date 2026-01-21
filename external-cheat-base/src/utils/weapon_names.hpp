#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace weapon_names
{
    // CS2 Weapon Item Definition Indices
    // Source: items_game.txt from CS2 game files
    // Reference: https://github.com/SteamDatabase/GameTracking-CS2
    inline const std::unordered_map<uint16_t, std::string> weaponMap = {
        // ===== PISTOLS =====
        {1, "Desert Eagle"},
        {2, "Dual Berettas"},
        {3, "Five-SeveN"},
        {4, "Glock-18"},
        {30, "Tec-9"},
        {32, "P2000"},
        {36, "P250"},
        {61, "USP-S"},
        {63, "CZ75-Auto"},
        {64, "R8 Revolver"},

        // ===== RIFLES =====
        {7, "AK-47"},
        {8, "AUG"},
        {10, "FAMAS"},
        {13, "Galil AR"},
        {16, "M4A4"},
        {39, "SG 553"},
        {60, "M4A1-S"},

        // ===== SNIPER RIFLES =====
        {9, "AWP"},
        {11, "G3SG1"},
        {38, "SCAR-20"},
        {40, "SSG 08"},

        // ===== SMGS (Sub-Machine Guns) =====
        {17, "MAC-10"},
        {19, "P90"},
        {23, "MP5-SD"},
        {24, "UMP-45"},
        {26, "PP-Bizon"},
        {33, "MP7"},
        {34, "MP9"},

        // ===== HEAVY =====
        {14, "M249"},
        {28, "Negev"},
        {25, "XM1014"},
        {27, "MAG-7"},
        {29, "Sawed-Off"},
        {35, "Nova"},

        // ===== GRENADES =====
        {43, "Flashbang"},
        {44, "HE Grenade"},
        {45, "Smoke Grenade"},
        {46, "Molotov"},
        {47, "Decoy Grenade"},
        {48, "Incendiary"},
        {68, "TA Grenade"},
        {81, "Frag Grenade"},
        {82, "Snowball"},
        {84, "Breach Charge"},

        // ===== EQUIPMENT =====
        {31, "Zeus x27"},
        {42, "Knife (CT)"},
        {49, "C4 Explosive"},
        {50, "Kevlar Vest"},
        {51, "Kevlar + Helmet"},
        {52, "Defuse Kit"},
        {54, "Rescue Kit"},
        {55, "Medi-Shot"},
        {57, "Healthshot"},
        {59, "Knife (T)"},
        {80, "Shield"},

        // ===== KNIVES (Skins) =====
        {500, "Bayonet"},
        {503, "Karambit"},
        {505, "Flip Knife"},
        {506, "Gut Knife"},
        {507, "M9 Bayonet"},
        {508, "Huntsman Knife"},
        {509, "Falchion Knife"},
        {512, "Bowie Knife"},
        {514, "Butterfly Knife"},
        {515, "Shadow Daggers"},
        {516, "Paracord Knife"},
        {517, "Survival Knife"},
        {518, "Ursus Knife"},
        {519, "Navaja Knife"},
        {520, "Nomad Knife"},
        {521, "Stiletto Knife"},
        {522, "Talon Knife"},
        {523, "Classic Knife"},
        {525, "Skeleton Knife"},

        // ===== DANGER ZONE ITEMS =====
        {85, "Tablet"},
        {86, "Axe"},
        {87, "Hammer"},
        {88, "Wrench"},
        {89, "Spanner"},

        // ===== GLOVES =====
        {5027, "Hand Wraps"},
        {5028, "Sport Gloves"},
        {5029, "Driver Gloves"},
        {5030, "Specialist Gloves"},
        {5031, "Moto Gloves"},
        {5032, "Bloodhound Gloves"},
        {5033, "Hydra Gloves"},
        {5034, "Broken Fang Gloves"},

        // ===== AGENTS (Character Models) =====
        {4619, "Agent | FBI"},
        {4680, "Agent | GIGN"},
        {4711, "Agent | SAS"},
        {4712, "Agent | GSG-9"},
        {4713, "Agent | Phoenix"},
        {4714, "Agent | Elite Crew"},
        {4715, "Agent | Professionals"},
        {4716, "Agent | Sabre"},

        // ===== MUSIC KITS =====
        {1314, "Music Kit"},

        // ===== STICKERS & PATCHES =====
        {1209, "Sticker"},
        {1348, "Patch"},
        {1349, "Graffiti"},

        // ===== CASES & KEYS =====
        {1210, "Case Key"},

        // ===== TOOLS =====
        {1200, "Name Tag"},
        {1201, "Stattrak Swap Tool"},
    };

    inline std::string getWeaponName(uint16_t itemDefIndex)
    {
        auto it = weaponMap.find(itemDefIndex);
        if (it != weaponMap.end()) {
            return it->second;
        }
        return "Unknown";
    }
}

