#include "skin_config.hpp"

#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string_view>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#endif

namespace skins
{
    namespace
    {
        std::string_view trim(std::string_view value)
        {
            const auto first = value.find_first_not_of(" \t\r");
            if (first == std::string_view::npos) return {};
            const auto last = value.find_last_not_of(" \t\r");
            return value.substr(first, last - first + 1);
        }

        bool parseInt(const std::string_view value, int& result)
        {
            const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
            return parsed.ec == std::errc{} && parsed.ptr == value.data() + value.size();
        }

        bool parseFloat(const std::string_view value, float& result)
        {
            std::istringstream stream{std::string(value)};
            stream.imbue(std::locale::classic());
            stream >> result;
            return !stream.fail() && stream.peek() == std::char_traits<char>::eof() && std::isfinite(result);
        }
    }

    bool validProfile(const WeaponSkinConfig& profile)
    {
        if (!std::isfinite(profile.wear) || profile.seed < 0 || profile.seed > 1000) return false;
        if (profile.paintKit == 0) {
            for (const auto& weapon : weapons) {
                if (weapon.id == profile.weapon) return profile.wear >= 0 && profile.wear <= 1;
            }
            return false;
        }
        const PaintKit* kit = findPaintKit(profile.weapon, profile.paintKit);
        return kit && profile.wear >= kit->minimumWear && profile.wear <= kit->maximumWear;
    }

    bool validSettings(const Settings& settings)
    {
        for (std::size_t index = 0; index < weapons.size(); ++index) {
            if (settings.profiles[index].weapon != weapons[index].id || !validProfile(settings.profiles[index])) return false;
        }
        return true;
    }

    LoadResult loadSettings(const std::filesystem::path& path)
    {
        LoadResult result;
        std::error_code error;
        if (!std::filesystem::exists(path, error)) {
            if (error) result.error = "Could not inspect skin settings.";
            return result;
        }
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto length = file.tellg();
        if (!file || length < 0 || length > 65536) {
            result.error = "Skin settings are unreadable or exceed 64 KiB.";
            return result;
        }
        file.seekg(0);
        Settings candidate;
        WeaponSkinConfig* profile = nullptr;
        std::array<bool, weapons.size()> seen{};
        bool metadata = false;
        bool schemaSeen = false;
        std::string line;
        const auto reject = [&result]() {
            result.error = "Invalid skin settings. No profiles were loaded.";
            return result;
        };
        while (std::getline(file, line)) {
            const auto text = trim(line);
            if (text.empty() || text.front() == ';' || text.front() == '#') continue;
            if (text.front() == '[') {
                if (text.back() != ']') return reject();
                const auto section = text.substr(1, text.size() - 2);
                profile = nullptr;
                metadata = section == "skins";
                if (section.starts_with("weapon_")) {
                    int id = 0;
                    if (!parseInt(section.substr(7), id)) return reject();
                    for (std::size_t index = 0; index < weapons.size(); ++index) {
                        if (id == weapons[index].id) {
                            if (seen[index]) return reject();
                            seen[index] = true;
                            profile = &candidate.profiles[index];
                            break;
                        }
                    }
                    if (!profile) return reject();
                }
                continue;
            }
            const auto separator = text.find('=');
            if (separator == std::string_view::npos) return reject();
            const auto key = trim(text.substr(0, separator));
            const auto value = trim(text.substr(separator + 1));
            if (metadata && key == "schema") {
                if (schemaSeen || value != "1") return reject();
                schemaSeen = true;
            } else if (profile) {
                if (key == "paint_kit" && !parseInt(value, profile->paintKit)) return reject();
                if (key == "wear" && !parseFloat(value, profile->wear)) return reject();
                if (key == "seed" && !parseInt(value, profile->seed)) return reject();
            }
        }
        if (!file.eof() || !schemaSeen || !validSettings(candidate)) return reject();
        result.settings = candidate;
        return result;
    }

    std::string saveSettings(const std::filesystem::path& path, const Settings& settings)
    {
        if (!validSettings(settings)) return "Invalid profiles were not saved.";
        std::error_code error;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
        if (error) return "Could not create the skin settings directory.";
        auto temporary = path;
        temporary += L".tmp";
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            file.imbue(std::locale::classic());
            file << "[skins]\nschema=1\n" << std::setprecision(9);
            for (const auto& profile : settings.profiles) {
                file << "\n[weapon_" << profile.weapon << "]\npaint_kit=" << profile.paintKit
                     << "\nwear=" << profile.wear << "\nseed=" << profile.seed << '\n';
            }
            file.flush();
            if (!file) return "Could not write skin settings. The previous file was kept.";
        }
#ifdef _WIN32
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            std::filesystem::remove(temporary, error);
            return "Could not replace skin settings. The previous file was kept.";
        }
#else
        std::filesystem::rename(temporary, path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return "Could not replace skin settings. The previous file was kept.";
        }
#endif
        return {};
    }
}
