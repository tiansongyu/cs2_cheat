#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace weapon_names
{
    // CS2 Weapon Item Definition Indices
    // Based on CS2 item definitions
    inline const std::unordered_map<uint16_t, std::string> weaponMap = {
        // Pistols
        {1, "Desert Eagle"},
        {2, "Dual Berettas"},
        {3, "Five-SeveN"},
        {4, "Glock-18"},
        {7, "AK-47"},
        {8, "AUG"},
        {9, "AWP"},
        {10, "FAMAS"},
        {11, "G3SG1"},
        {13, "Galil AR"},
        {14, "M249"},
        {16, "M4A4"},
        {17, "MAC-10"},
        {19, "P90"},
        {23, "MP5-SD"},
        {24, "UMP-45"},
        {25, "XM1014"},
        {26, "PP-Bizon"},
        {27, "MAG-7"},
        {28, "Negev"},
        {29, "Sawed-Off"},
        {30, "Tec-9"},
        {31, "Zeus x27"},
        {32, "P2000"},
        {33, "MP7"},
        {34, "MP9"},
        {35, "Nova"},
        {36, "P250"},
        {38, "SCAR-20"},
        {39, "SG 553"},
        {40, "SSG 08"},
        {60, "M4A1-S"},
        {61, "USP-S"},
        {63, "CZ75-Auto"},
        {64, "R8 Revolver"},
        
        // Grenades
        {43, "Flashbang"},
        {44, "HE Grenade"},
        {45, "Smoke Grenade"},
        {46, "Molotov"},
        {47, "Decoy Grenade"},
        {48, "Incendiary"},
        
        // Equipment
        {49, "C4 Explosive"},
        {50, "Kevlar Vest"},
        {51, "Kevlar + Helmet"},
        {52, "Defuse Kit"},
        {54, "Rescue Kit"},
        {55, "Medi-Shot"},
        {56, "Tactical Awareness Grenade"},
        {57, "Breach Charge"},
        {59, "Knife"},
        {500, "Bayonet"},
        {503, "Karambit"},
        {505, "Flip Knife"},
        {506, "Gut Knife"},··
        {507, "M9 Bayonet"},        ··· 
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

