#include "skin_backend.hpp"

#include <cmath>
#include <cstring>

namespace skins
{
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

    bool MemoryBackend::guardsMatch(const Mutation& mutation)
    {
        for (const auto& guard : mutation.guards) {
            if (!guard.address) continue;
            std::uint64_t current = 0;
            if (!readBits(guard.address, guard.size, current) || current != guard.value) return false;
        }
        return true;
    }

    bool MemoryBackend::append(std::vector<Mutation>& mutations, const std::uintptr_t address,
                               const std::uint64_t value, const std::uint8_t size,
                               const std::array<ValueGuard, 4>& guards, const bool restore)
    {
        std::uint64_t before = 0;
        if (!readBits(address, size, before)) return false;
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
        if (!memory_.readValue(address, attributes) || attributes.count > 64 ||
            (attributes.count && !attributes.elements)) return false;
        // Existing attributes can override fallback values. Update only the
        // finish attributes already owned by this item; never replace its vector.
        const std::array<ValueGuard, 4> guards{{
            {address, attributes.count, 8}, {address + 8, attributes.elements, 8}, {}, {}
        }};
        for (std::uint64_t index = 0; index < attributes.count; ++index) {
            const auto attribute = attributes.elements + index * layout_.attributeStride;
            std::uint16_t definition = 0;
            if (!memory_.readValue(attribute + layout_.attributeDefinition, definition)) return false;
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
        const PaintKit* kit = findPaintKit(profile.weapon, profile.paintKit);
        if (!kit || !validProfile(profile) || weapon.definition != profile.weapon ||
            !access_.stillOwned(weapon.key)) return false;
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
        if (!memory_.readValue(entity + layout_.paintKit, currentPaint) || currentPaint < -1 || currentPaint > 10000 ||
            !memory_.readValue(entity + layout_.seed, currentSeed) || currentSeed < -1 || currentSeed > 1000 ||
            !memory_.readValue(entity + layout_.wear, currentWear) || !std::isfinite(currentWear) || currentWear < 0 || currentWear > 1 ||
            !memory_.readValue(item + layout_.initialized, initialized) || initialized > 1 ||
            !memory_.readValue(item + layout_.disallowSoc, disallow) || disallow > 1 ||
            !memory_.readValue(entity + layout_.visualsDataSet, visuals) || visuals > 1) return false;
        if (!memory_.readValue(item + access_.layout().definition, definition) || definition != profile.weapon ||
            !append(mutations, entity + layout_.paintKit, static_cast<std::uint32_t>(profile.paintKit), 4) ||
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
        if (!writesAllowed()) {
            result.failures = 1;
            return result;
        }
        for (auto& mutation : mutations) {
            std::uint64_t current = 0;
            if (!access_.stillOwned(key) || !guardsMatch(mutation) ||
                !readBits(mutation.address, mutation.size, current) || current != mutation.before) {
                result.failures = 1;
                break;
            }
            // Keep the journal even if the write succeeds but its verification
            // read fails. A later guarded restore can then recover that field.
            mutation.written = true;
            const bool wrote = memory_.write(mutation.address, &mutation.after, mutation.size);
            ++result.writes;
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
                break;
            }
        }
        if (result.failures) {
            const auto rollback = restore(key, mutations);
            result.restored += rollback.restored;
            result.skipped += rollback.skipped;
            result.failures += rollback.failures;
            return result;
        }
        result.succeeded = true;
        return result;
    }

    void MemoryBackend::invalidate(const game::WeaponKey& key)
    {
        if (!access_.stillOwned(key)) return;
        std::uint8_t current = 0;
        if (memory_.readValue(key.entity + layout_.visualsDataSet, current) && current == 1) {
            const std::uint8_t clear = 0;
            memory_.write(key.entity + layout_.visualsDataSet, &clear, sizeof(clear));
        }
    }

    WriteResult MemoryBackend::restore(const game::WeaponKey& key, std::vector<Mutation>& mutations)
    {
        WriteResult result;
        if (!writesAllowed()) {
            result.failures = 1;
            return result;
        }
        for (auto iterator = mutations.rbegin(); iterator != mutations.rend(); ++iterator) {
            auto& mutation = *iterator;
            if (!mutation.written) continue;
            if (!mutation.restore || !access_.stillOwned(key) || !guardsMatch(mutation)) {
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
                continue;
            }
            if (current != mutation.after) {
                mutation.written = false;
                ++result.skipped;
                continue;
            }
            if (!memory_.write(mutation.address, &mutation.before, mutation.size) ||
                !readBits(mutation.address, mutation.size, current) || current != mutation.before) {
                ++result.failures;
                continue;
            }
            ++result.writes;
            mutation.written = false;
            ++result.restored;
        }
        if (result.restored) invalidate(key);
        result.succeeded = result.failures == 0;
        return result;
    }

    WeaponDiagnostic MemoryBackend::inspect(const game::LocalWeapon& weapon)
    {
        WeaponDiagnostic result{weapon.definition, 0, 0, weapon.active, false};
        result.readSucceeded = access_.stillOwned(weapon.key) &&
            memory_.readValue(weapon.key.entity + layout_.paintKit, result.paintKit) &&
            memory_.readValue(weapon.key.entity + layout_.wear, result.wear) &&
            std::isfinite(result.wear);
        return result;
    }
}
