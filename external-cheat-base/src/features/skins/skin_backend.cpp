#include "skin_backend.hpp"

#include <cmath>
#include <cstring>

namespace skins
{
    const char* failureText(const FailureCode code)
    {
        switch (code) {
        case FailureCode::none: return "None";
        case FailureCode::invalidProfile: return "The selected finish or parameters are invalid";
        case FailureCode::ownershipChanged: return "The weapon identity or owner changed";
        case FailureCode::ownershipRead: return "Could not revalidate weapon ownership";
        case FailureCode::parameterRead: return "Could not read finish fields; check offsets";
        case FailureCode::invalidParameter: return "Finish fields have unexpected values; check offsets";
        case FailureCode::attributeRead: return "Could not read existing finish attributes";
        case FailureCode::invalidAttributes: return "The attribute container layout is invalid";
        case FailureCode::guardRead: return "Could not read a model or attribute guard";
        case FailureCode::guardChanged: return "The model or attribute allocation changed";
        case FailureCode::preconditionChanged: return "The game changed a field before application";
        case FailureCode::writeDenied: return "Memory writes are locked";
        case FailureCode::writeFailed: return "A memory write failed; rollback was requested";
        case FailureCode::readbackFailed: return "Written parameters could not be verified";
        case FailureCode::restoreRead: return "Restore is waiting for a readable weapon";
        case FailureCode::restoreWrite: return "A restore write could not be verified";
        }
        return "Unknown";
    }

    bool MemoryBackend::fail(const FailureCode code)
    {
        failure_ = {code, memory_.lastError()};
        return false;
    }
    namespace
    {
        std::uint32_t floatBits(const float value)
        {
            static_assert(sizeof(float) == sizeof(std::uint32_t));
            std::uint32_t bits = 0;
            std::memcpy(&bits, &value, sizeof(bits));
            return bits;
        }
    }
    bool MemoryBackend::readBits(const std::uintptr_t address, const std::uint8_t size,
                                 std::uint64_t& value)
    {
        value = 0;
        return address && size > 0 && size <= sizeof(value) && memory_.read(address, &value, size);
    }

    game::AccessResult MemoryBackend::guardsMatch(const Mutation& mutation)
    {
        for (const auto& guard : mutation.guards) {
            if (!guard.address) continue;
            std::uint64_t current = 0;
            if (!readBits(guard.address, guard.size, current)) return game::AccessResult::unreadable;
            if (current != guard.value) return game::AccessResult::stale;
        }
        return game::AccessResult::valid;
    }

    bool MemoryBackend::append(std::vector<Mutation>& mutations, const std::uintptr_t address,
                               const std::uint64_t value, const std::uint8_t size,
                               const std::array<ValueGuard, 4>& guards, const bool restore)
    {
        std::uint64_t before = 0;
        if (!readBits(address, size, before)) return fail(FailureCode::parameterRead);
        if (before != value) mutations.push_back({address, before, value, size, guards, restore, false});
        return true;
    }

    void MemoryBackend::appendMesh(std::vector<Mutation>& mutations, const std::uintptr_t entity,
                                   const std::uint64_t mask)
    {
        std::uintptr_t node = 0;
        std::uintptr_t owner = 0;
        std::uintptr_t identity = 0;
        std::uint32_t rawHandle = 0;
        std::uint32_t ownerHandle = 0;
        std::uint64_t currentMask = 0;
        if (!entity || !memory_.readValue(entity + layout_.sceneNode, node) || !node ||
            !memory_.readValue(node + layout_.sceneOwner, owner) || owner != entity ||
            !memory_.readValue(entity + access_.layout().identity, identity) || !identity ||
            !memory_.readValue(identity + access_.layout().identityHandle, rawHandle) ||
            !memory_.readValue(entity + access_.layout().owner, ownerHandle) ||
            !memory_.readValue(node + layout_.modelState + layout_.meshMask, currentMask) ||
            (currentMask != 1 && currentMask != 2)) return;
        const std::array<ValueGuard, 4> guards{{
            {entity + layout_.sceneNode, node, 8},
            {node + layout_.sceneOwner, entity, 8},
            {identity + access_.layout().identityHandle, rawHandle, 4},
            {entity + access_.layout().owner, ownerHandle, 4}
        }};
        const auto previousSize = mutations.size();
        append(mutations, node + layout_.modelState + layout_.meshMask, mask, 8, guards);
        if (mutations.size() != previousSize) mutations.back().budgeted = false;
    }

    bool MemoryBackend::appendAttributes(std::vector<Mutation>& mutations, const std::uintptr_t item,
                                         const WeaponSkinConfig& profile)
    {
        struct AttributeVector { std::uint64_t count; std::uintptr_t elements; };
        AttributeVector attributes{};
        const auto address = item + layout_.attributeList + layout_.attributes;
        if (!memory_.readValue(address, attributes)) return fail(FailureCode::attributeRead);
        if (attributes.count > 64 || (attributes.count && !attributes.elements)) return fail(FailureCode::invalidAttributes);
        // Existing attributes can override fallback values. Update only the
        // finish attributes already owned by this item; never replace its vector.
        const std::array<ValueGuard, 4> guards{{
            {address, attributes.count, 8}, {address + 8, attributes.elements, 8}, {}, {}
        }};
        for (std::uint64_t index = 0; index < attributes.count; ++index) {
            const auto attribute = attributes.elements + index * layout_.attributeStride;
            std::uint16_t definition = 0;
            if (!memory_.readValue(attribute + layout_.attributeDefinition, definition)) return fail(FailureCode::attributeRead);
            float value = 0;
            switch (definition) {
            case 6: value = static_cast<float>(profile.paintKit); break;
            case 7: value = static_cast<float>(profile.seed); break;
            case 8: value = profile.wear; break;
            default: continue;
            }
            auto attributeGuards = guards;
            attributeGuards[2] = {attribute + layout_.attributeDefinition, definition, 2};
            const auto bits = floatBits(value);
            if (!append(mutations, attribute + layout_.attributeValue, bits, 4, attributeGuards) ||
                !append(mutations, attribute + layout_.attributeInitialValue, bits, 4, attributeGuards)) return false;
        }
        return true;
    }

    bool MemoryBackend::plan(const game::LocalWeapon& weapon, const WeaponSkinConfig& profile,
                             const bool forceRefresh, std::vector<Mutation>& mutations)
    {
        mutations.clear();
        failure_ = {};
        const PaintKit* kit = findPaintKit(profile.weapon, profile.paintKit);
        if (!kit || !validProfile(profile) || weapon.definition != profile.weapon) return fail(FailureCode::invalidProfile);
        const auto ownership = access_.ownership(weapon.key);
        if (ownership != game::AccessResult::valid) return fail(ownership == game::AccessResult::unreadable
            ? FailureCode::ownershipRead : FailureCode::ownershipChanged);
        const auto entity = weapon.key.entity;
        const auto item = entity + access_.layout().attributeManager + access_.layout().item;
        std::uint16_t definition = 0;
        std::int32_t currentPaint = 0;
        std::int32_t currentSeed = 0;
        float currentWear = 0;
        std::uint8_t initialized = 0;
        std::uint8_t disallow = 0;
        std::uint8_t visuals = 0;
        // Plausibility is an additional guard, never proof that offsets match.
        if (!memory_.readValue(entity + layout_.paintKit, currentPaint) ||
            !memory_.readValue(entity + layout_.seed, currentSeed) ||
            !memory_.readValue(entity + layout_.wear, currentWear) ||
            !memory_.readValue(item + layout_.initialized, initialized) ||
            !memory_.readValue(item + layout_.disallowSoc, disallow) ||
            !memory_.readValue(entity + layout_.visualsDataSet, visuals) ||
            !memory_.readValue(item + access_.layout().definition, definition)) return fail(FailureCode::parameterRead);
        if (currentPaint < -1 || currentPaint > 10000 || currentSeed < -1 || currentSeed > 1000 ||
            !std::isfinite(currentWear) || currentWear < 0 || currentWear > 1 || initialized > 1 ||
            disallow > 1 || visuals > 1 || definition != profile.weapon) return fail(FailureCode::invalidParameter);
        if (!append(mutations, entity + layout_.paintKit, static_cast<std::uint32_t>(profile.paintKit), 4) ||
            !append(mutations, entity + layout_.seed, static_cast<std::uint32_t>(profile.seed), 4) ||
            !append(mutations, entity + layout_.wear, floatBits(profile.wear), 4) ||
            !appendAttributes(mutations, item, profile) ||
            !append(mutations, item + layout_.disallowSoc, 1, 1) ||
            !append(mutations, item + layout_.initialized, 1, 1)) return false;

        appendMesh(mutations, entity, kit->legacyModel ? 2 : 1);
        if (weapon.active) {
            std::uint32_t armsHandle = 0;
            if (memory_.readValue(weapon.key.pawn + layout_.hudArms, armsHandle)) {
                const auto arms = access_.resolve(armsHandle);
                std::uintptr_t node = 0;
                std::uintptr_t child = 0;
                if (arms && memory_.readValue(arms + layout_.sceneNode, node) && node &&
                    memory_.readValue(node + layout_.sceneChild, child)) {
                    std::array<std::uintptr_t, 64> visited{};
                    for (std::size_t index = 0; child && index < visited.size(); ++index) {
                        bool cycle = false;
                        for (std::size_t prior = 0; prior < index; ++prior) cycle = cycle || visited[prior] == child;
                        if (cycle) break;
                        visited[index] = child;
                        std::uintptr_t owner = 0;
                        std::uint32_t ownerHandle = 0;
                        if (!memory_.readValue(child + layout_.sceneOwner, owner)) break;
                        if (owner && memory_.readValue(owner + access_.layout().owner, ownerHandle) &&
                            ownerHandle == weapon.key.handle) {
                            appendMesh(mutations, owner, kit->legacyModel ? 2 : 1);
                            break;
                        }
                        if (!memory_.readValue(child + layout_.sceneSibling, child)) break;
                    }
                }
            }
        }
        // Activate fallback only after the finish data has been written.
        if (!append(mutations, item + layout_.itemIdHigh, 0xFFFFFFFFU, 4)) return false;
        // Request normal client visual rebuilding once per actual change.
        // This does not call engine functions or execute code in the game.
        if (!mutations.empty() || forceRefresh) {
            const auto previousSize = mutations.size();
            if (!append(mutations, entity + layout_.visualsDataSet, 0, 1, {}, false)) return false;
            if (mutations.size() != previousSize) mutations.back().budgeted = false;
        }
        return true;
    }

    WriteResult MemoryBackend::apply(const game::WeaponKey& key, std::vector<Mutation>& mutations)
    {
        WriteResult result;
        failure_ = {};
        if (!writesAllowed()) {
            fail(FailureCode::writeDenied);
            result.failures = 1;
            return result;
        }
        for (auto& mutation : mutations) {
            std::uint64_t current = 0;
            const auto owner = access_.ownership(key);
            if (owner != game::AccessResult::valid) {
                fail(owner == game::AccessResult::unreadable ? FailureCode::ownershipRead : FailureCode::ownershipChanged);
                result.failures = 1;
                break;
            }
            const auto guards = guardsMatch(mutation);
            if (guards != game::AccessResult::valid ||
                !readBits(mutation.address, mutation.size, current) || current != mutation.before) {
                fail(guards == game::AccessResult::unreadable ? FailureCode::guardRead :
                    guards == game::AccessResult::stale ? FailureCode::guardChanged : FailureCode::preconditionChanged);
                result.failures = 1;
                break;
            }
            // Keep the journal even if the write succeeds but its verification
            // read fails. A later guarded restore can then recover that field.
            mutation.written = true;
            const bool wrote = memory_.write(mutation.address, &mutation.after, mutation.size);
            const auto writeError = memory_.lastError();
            ++result.writes;
            if (!mutation.restore && wrote) {
                // The game can consume this pulse immediately. Only durable
                // parameters participate in exact read-back and restoration.
                mutation.written = false;
                continue;
            }
            const bool readBack = readBits(mutation.address, mutation.size, current);
            if (!wrote || !readBack || current != mutation.after) {
                if (!wrote && readBack && current != mutation.after && current != mutation.before) {
                    bool partial = true;
                    for (std::uint8_t byte = 0; byte < mutation.size; ++byte) {
                        const auto shift = byte * 8U;
                        const auto observed = (current >> shift) & 0xFFU;
                        partial = partial && (observed == ((mutation.before >> shift) & 0xFFU) ||
                                              observed == ((mutation.after >> shift) & 0xFFU));
                    }
                    // A partial transfer gets its exact observed value as the
                    // rollback precondition. Unrelated replacement values are
                    // left alone rather than overwritten with our baseline.
                    if (partial) mutation.after = current;
                }
                result.failures = 1;
                fail(wrote ? FailureCode::readbackFailed : FailureCode::writeFailed);
                if (!wrote) failure_.nativeError = writeError;
                break;
            }
        }
        if (result.failures) {
            const auto primaryFailure = failure_;
            const auto rollback = restore(key, mutations);
            failure_ = primaryFailure;
            result.writes += rollback.writes;
            result.restored += rollback.restored;
            result.skipped += rollback.skipped;
            result.failures += rollback.failures;
            return result;
        }
        result.succeeded = true;
        return result;
    }

    void MemoryBackend::invalidate(const game::WeaponKey& key, WriteResult& result)
    {
        if (!access_.stillOwned(key)) return;
        std::uint8_t current = 0;
        if (memory_.readValue(key.entity + layout_.visualsDataSet, current) && current == 1) {
            const std::uint8_t clear = 0;
            ++result.writes;
            if (!memory_.write(key.entity + layout_.visualsDataSet, &clear, sizeof(clear))) {
                ++result.failures;
                fail(FailureCode::restoreWrite);
            }
        }
    }

    WriteResult MemoryBackend::restore(const game::WeaponKey& key, std::vector<Mutation>& mutations)
    {
        WriteResult result;
        failure_ = {};
        if (!writesAllowed()) {
            fail(FailureCode::writeDenied);
            result.failures = 1;
            return result;
        }
        for (auto iterator = mutations.rbegin(); iterator != mutations.rend(); ++iterator) {
            auto& mutation = *iterator;
            if (!mutation.written) continue;
            if (!mutation.restore) {
                mutation.written = false;
                continue;
            }
            const auto owner = access_.ownership(key);
            if (owner == game::AccessResult::unreadable) {
                ++result.failures;
                fail(FailureCode::restoreRead);
                continue;
            }
            if (owner == game::AccessResult::stale) {
                mutation.written = false;
                ++result.skipped;
                continue;
            }
            const auto guards = guardsMatch(mutation);
            if (guards == game::AccessResult::unreadable) {
                ++result.failures;
                fail(FailureCode::restoreRead);
                continue;
            }
            if (guards == game::AccessResult::stale) {
                mutation.written = false;
                ++result.skipped;
                continue;
            }
            if (mutation.size == 0 || mutation.size > sizeof(mutation.before)) {
                ++result.failures;
                continue;
            }
            std::uint64_t current = 0;
            if (!readBits(mutation.address, mutation.size, current)) {
                ++result.failures;
                fail(FailureCode::restoreRead);
                continue;
            }
            if (current == mutation.before) {
                mutation.written = false;
                continue;
            }
            if (current != mutation.after) {
                mutation.written = false;
                ++result.skipped;
                continue;
            }
            ++result.writes;
            if (!memory_.write(mutation.address, &mutation.before, mutation.size) ||
                !readBits(mutation.address, mutation.size, current) || current != mutation.before) {
                ++result.failures;
                fail(FailureCode::restoreWrite);
                continue;
            }
            mutation.written = false;
            ++result.restored;
        }
        if (result.restored) invalidate(key, result);
        result.succeeded = result.failures == 0;
        return result;
    }

    WeaponDiagnostic MemoryBackend::inspect(const game::LocalWeapon& weapon)
    {
        WeaponDiagnostic result{};
        result.weapon = weapon.definition;
        result.active = weapon.active;
        result.readSucceeded = access_.stillOwned(weapon.key) &&
            memory_.readValue(weapon.key.entity + layout_.paintKit, result.paintKit) &&
            memory_.readValue(weapon.key.entity + layout_.wear, result.wear) &&
            memory_.readValue(weapon.key.entity + layout_.seed, result.seed) &&
            std::isfinite(result.wear);
        return result;
    }
}
