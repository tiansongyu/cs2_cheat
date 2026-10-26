#pragma once

#include "skin_backend.hpp"

#include <chrono>
#include <cstdint>
#include <vector>

namespace skins
{
    enum class State { disabled, writeLocked, paused, waiting, readFailed, ready, applied, failed, restoring };

    struct Status
    {
        State state = State::disabled;
        std::uint64_t applications{};
        std::uint64_t writes{};
        std::uint64_t failures{};
        std::uint64_t restoredFields{};
        std::uint64_t skippedFields{};
        std::size_t trackedWeapons{};
        std::vector<WeaponDiagnostic> weapons;
        FailureInfo lastFailure;
        game::WeaponReadIssue readIssue = game::WeaponReadIssue::none;
        std::uint32_t skippedHandles{};
        bool inventoryComplete = true;
        bool sampleFresh = false;
    };

    // Owned by the sampling thread. Status is copied by the runtime wrapper.
    class SkinChanger
    {
    public:
        explicit SkinChanger(Backend& backend) : backend_(backend) {}
        void tick(const Settings& settings, bool enabled, bool foreground, bool diagnostics,
                  std::uint64_t request, std::chrono::steady_clock::time_point now);
        void shutdown();
        [[nodiscard]] const Status& status() const { return status_; }

    private:
        struct Record
        {
            game::LocalWeapon weapon;
            WeaponSkinConfig profile;
            std::vector<Mutation> originals;
            std::uint64_t request{};
            unsigned attempts{};
            std::chrono::steady_clock::time_point retryAt{};
            bool lastSucceeded = false;
            bool restoring = false;
            FailureInfo failure{};
        };

        void account(const WriteResult& result);
        bool restore(Record& record);
        void remember(Record& record, const std::vector<Mutation>& mutations);

        Backend& backend_;
        Status status_;
        std::vector<Record> records_;
    };

    [[nodiscard]] const char* stateText(State state);
    [[nodiscard]] const char* readIssueText(game::WeaponReadIssue issue);
    [[nodiscard]] std::string diagnosticReport(const Status& status, const Settings& settings);
}
