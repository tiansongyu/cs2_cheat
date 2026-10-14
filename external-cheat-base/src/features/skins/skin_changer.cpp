#include "skin_changer.hpp"

#include <algorithm>

namespace skins
{
    const char* stateText(const State state)
    {
        switch (state) {
        case State::disabled: return "Disabled";
        case State::writeLocked: return "Memory writes are locked";
        case State::paused: return "Paused while the game is unfocused";
        case State::waiting: return "Waiting for a living local player";
        case State::readFailed: return "Weapon read failed; check the game version and offsets";
        case State::ready: return "Ready; choose a skin for a held firearm";
        case State::applied: return "Skin data verified; visual refresh may require switching or rebuying";
        case State::failed: return "Application failed; use Reapply after checking the game state";
        }
        return "Unknown";
    }

    void SkinChanger::account(const WriteResult& result)
    {
        status_.writes += result.writes;
        status_.failures += result.failures;
        status_.restoredFields += result.restored;
        status_.skippedFields += result.skipped;
    }

    bool SkinChanger::restore(Record& record)
    {
        const auto result = backend_.restore(record.weapon.key, record.originals);
        account(result);
        return result.succeeded;
    }

    void SkinChanger::remember(Record& record, const std::vector<Mutation>& mutations)
    {
        for (const auto& mutation : mutations) {
            if (!mutation.written || !mutation.restore) continue;
            const auto existing = std::find_if(record.originals.begin(), record.originals.end(),
                [&mutation](const Mutation& original) { return original.address == mutation.address; });
            if (existing == record.originals.end()) {
                record.originals.push_back(mutation);
            } else if (existing->guards == mutation.guards && existing->size == mutation.size) {
                existing->after = mutation.after;
                existing->written = true;
            } else {
                // A view model or attribute allocation was replaced at the same
                // address. Its new identity gets a new baseline.
                *existing = mutation;
            }
        }
    }

    void SkinChanger::tick(const Settings& settings, const bool enabled, const bool foreground,
                           const bool diagnostics, const std::uint64_t request,
                           const std::chrono::steady_clock::time_point now)
    {
        status_.weapons.clear();
        status_.trackedWeapons = records_.size();
        if (!foreground) {
            status_.state = enabled || !records_.empty() ? State::paused : State::disabled;
            return;
        }
        if (!enabled && records_.empty() && !diagnostics) {
            status_.state = State::disabled;
            return;
        }
        const auto sample = backend_.sample();
        if (sample.status != game::WeaponReadStatus::ready) {
            status_.state = sample.status == game::WeaponReadStatus::readFailed ? State::readFailed : State::waiting;
            return;
        }
        // Only a complete successful inventory sample may retire old identities.
        records_.erase(std::remove_if(records_.begin(), records_.end(), [&sample](const Record& record) {
            return std::none_of(sample.weapons.begin(), sample.weapons.end(), [&record](const auto& weapon) {
                return weapon.key == record.weapon.key;
            });
        }), records_.end());

        if (!backend_.writesAllowed()) {
            for (const auto& weapon : sample.weapons) status_.weapons.push_back(backend_.inspect(weapon));
            status_.state = enabled ? State::writeLocked : State::disabled;
            status_.trackedWeapons = records_.size();
            return;
        }
        bool failed = false;
        bool applied = false;
        for (const auto& weapon : sample.weapons) {
            const auto profile = settings.find(weapon.definition);
            auto record = std::find_if(records_.begin(), records_.end(), [&weapon](const Record& value) {
                return value.weapon.key == weapon.key;
            });
            const bool selected = enabled && profile && profile->paintKit != 0 && validProfile(*profile);
            if (record != records_.end() && (!selected || record->profile != *profile)) {
                if (!record->restoring || record->request != request) {
                    record->attempts = 0;
                    record->retryAt = {};
                    record->request = request;
                }
                record->restoring = true;
                if (record->attempts >= 3 || now < record->retryAt) {
                    failed = true;
                    continue;
                }
                if (!restore(*record)) {
                    ++record->attempts;
                    record->retryAt = now + std::chrono::milliseconds(500);
                    failed = true;
                    continue;
                }
                records_.erase(record);
                record = records_.end();
            }
            if (!selected) continue;
            if (record == records_.end()) {
                records_.push_back({weapon, *profile, {}, request, 0, {}});
                record = std::prev(records_.end());
            }
            const bool reapply = record->request != request;
            if (reapply) {
                record->request = request;
                record->attempts = 0;
                record->retryAt = {};
            }
            record->weapon = weapon;
            if (record->attempts >= 3 && !record->lastSucceeded) {
                failed = true;
                continue;
            }
            if (now < record->retryAt) {
                if (record->lastSucceeded) applied = true;
                else failed = true;
                continue;
            }
            std::vector<Mutation> mutations;
            if (!backend_.plan(weapon, *profile, reapply, mutations)) {
                ++record->attempts;
                ++status_.failures;
                record->lastSucceeded = false;
                record->retryAt = now + std::chrono::milliseconds(500);
                failed = true;
                continue;
            }
            if (mutations.empty()) {
                applied = true;
                record->lastSucceeded = true;
                record->retryAt = now + std::chrono::milliseconds(500);
                continue;
            }
            const bool parameterChange = std::any_of(mutations.begin(), mutations.end(),
                [](const Mutation& mutation) { return mutation.budgeted; });
            if (record->attempts >= 3 && parameterChange) {
                failed = true;
                continue;
            }
            const auto result = backend_.apply(weapon.key, mutations);
            account(result);
            remember(*record, mutations);
            if (!result.succeeded) {
                record->lastSucceeded = false;
                ++record->attempts;
                record->retryAt = now + std::chrono::milliseconds(500);
                failed = true;
            } else {
                record->lastSucceeded = true;
                ++status_.applications;
                // Limit repeated server overwrites as well as consecutive errors.
                if (parameterChange) ++record->attempts;
                record->retryAt = now + std::chrono::milliseconds(500);
                applied = true;
            }
        }
        for (const auto& weapon : sample.weapons) status_.weapons.push_back(backend_.inspect(weapon));
        status_.trackedWeapons = records_.size();
        status_.state = failed ? State::failed : !enabled ? State::disabled : applied ? State::applied : State::ready;
    }

    void SkinChanger::shutdown()
    {
        for (auto& record : records_) restore(record);
        records_.clear();
        status_.trackedWeapons = 0;
        status_.state = State::disabled;
        status_.weapons.clear();
    }
}
