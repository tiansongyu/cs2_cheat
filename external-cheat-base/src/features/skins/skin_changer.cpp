#include "skin_changer.hpp"
#include "skin_build_info.hpp"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>

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
        case State::restoring: return "Restoration is pending; keep the game open and focused";
        }
        return "Unknown";
    }

    const char* readIssueText(const game::WeaponReadIssue issue)
    {
        switch (issue) {
        case game::WeaponReadIssue::none: return "None";
        case game::WeaponReadIssue::localPawn: return "Could not read the local player pointer";
        case game::WeaponReadIssue::identity: return "Could not validate the local player identity";
        case game::WeaponReadIssue::pawnState: return "Could not validate player identity, health or team";
        case game::WeaponReadIssue::services: return "Weapon services are unavailable";
        case game::WeaponReadIssue::inventory: return "Could not read a valid weapon container";
        case game::WeaponReadIssue::weapon: return "Some weapon handles are stale or unreadable";
        case game::WeaponReadIssue::inventoryChanged: return "Inventory changed during sampling; waiting for a stable sample";
        }
        return "Unknown";
    }

    std::string diagnosticReport(const Status& status, const Settings& settings)
    {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << "Aegis local firearm skins\nOffsets: " << build_info::offsetRevision
            << "\nGenerated: " << build_info::offsetDate << "\nClient schema SHA-256: " << build_info::clientSha256
            << "\nCatalog: " << build_info::catalogRevision
            << "\nState: " << stateText(status.state)
            << "\nFresh sample: " << status.sampleFresh << "; complete inventory: " << status.inventoryComplete
            << "; skipped handles: " << status.skippedHandles
            << "\nRead detail: " << readIssueText(status.readIssue)
            << "\nLast failure: " << failureText(status.lastFailure.code) << "; Win32 error: " << status.lastFailure.nativeError
            << "\nApplications: " << status.applications << "; writes: " << status.writes << "; failures: " << status.failures
            << "; restored fields: " << status.restoredFields << "; skipped fields: " << status.skippedFields << '\n';
        for (const auto& weapon : status.weapons) {
            out << "Weapon " << weapon.weapon << " (" << weaponName(weapon.weapon) << ")"
                << "; active=" << weapon.active << "; readable=" << weapon.readSucceeded
                << "; observed=" << weapon.paintKit << '/' << std::fixed << std::setprecision(4) << weapon.wear << '/' << weapon.seed
                << "; desired=" << weapon.targetPaintKit << '/' << weapon.targetWear << '/' << weapon.targetSeed
                << "; attempts=" << weapon.attempts << "; detail=" << failureText(weapon.failure.code) << '\n';
        }
        out << "Configured profiles:\n";
        for (const auto& profile : settings.profiles) {
            if (profile.paintKit) out << profile.weapon << '/' << profile.paintKit << '/' << profile.wear << '/' << profile.seed << '\n';
        }
        out << "Parameter verification does not confirm the rendered finish.\n";
        return out.str();
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
        record.failure = backend_.failureInfo();
        if (!result.succeeded) status_.lastFailure = record.failure;
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
            } else if (existing->written && existing->guards == mutation.guards && existing->size == mutation.size) {
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
        status_.sampleFresh = false;
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
        status_.readIssue = sample.issue;
        status_.inventoryComplete = sample.complete;
        status_.skippedHandles = sample.skippedHandles;
        if (sample.status != game::WeaponReadStatus::ready) {
            status_.state = !enabled && !records_.empty() ? State::restoring :
                sample.status == game::WeaponReadStatus::readFailed ? State::readFailed : State::waiting;
            return;
        }
        status_.weapons.clear();
        status_.sampleFresh = true;
        // Only a complete successful inventory sample may retire old identities.
        records_.erase(std::remove_if(records_.begin(), records_.end(), [&sample](const Record& record) {
            return sample.complete && std::none_of(sample.weapons.begin(), sample.weapons.end(), [&record](const auto& weapon) {
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
        bool restoring = false;
        for (const auto& weapon : sample.weapons) {
            const auto profile = settings.find(weapon.definition);
            auto record = std::find_if(records_.begin(), records_.end(), [&weapon](const Record& value) {
                return value.weapon.key == weapon.key;
            });
            const bool selected = enabled && profile && profile->paintKit != 0 && validProfile(*profile);
            if (record != records_.end() && (!selected || record->restoring)) {
                restoring = true;
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
                if (records_.size() >= 64) {
                    failed = true;
                    continue;
                }
                records_.push_back({weapon, *profile, {}, request, 0, {}});
                record = std::prev(records_.end());
            }
            const bool reapply = record->request != request || record->profile != *profile;
            if (reapply) {
                record->request = request;
                record->attempts = 0;
                record->retryAt = {};
                record->profile = *profile;
                record->failure = {};
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
                record->failure = backend_.failureInfo();
                status_.lastFailure = record->failure;
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
                record->failure = {};
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
                record->failure = backend_.failureInfo();
                status_.lastFailure = record->failure;
                record->lastSucceeded = false;
                ++record->attempts;
                record->retryAt = now + std::chrono::milliseconds(500);
                failed = true;
            } else {
                record->failure = {};
                record->lastSucceeded = true;
                ++status_.applications;
                // Limit repeated server overwrites as well as consecutive errors.
                if (parameterChange) ++record->attempts;
                record->retryAt = now + std::chrono::milliseconds(500);
                applied = true;
            }
        }
        for (const auto& weapon : sample.weapons) {
            auto row = backend_.inspect(weapon);
            if (const auto profile = settings.find(weapon.definition); profile && enabled) {
                row.targetPaintKit = profile->paintKit;
                row.targetWear = profile->wear;
                row.targetSeed = profile->seed;
            }
            for (const auto& record : records_) {
                if (record.weapon.key == weapon.key) {
                    row.attempts = record.attempts;
                    row.failure = record.failure;
                    break;
                }
            }
            status_.weapons.push_back(row);
        }
        status_.trackedWeapons = records_.size();
        status_.state = (!enabled && !records_.empty()) ? State::restoring : failed ? (restoring ? State::restoring : State::failed) :
            !enabled ? State::disabled : applied ? State::applied : State::ready;
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
