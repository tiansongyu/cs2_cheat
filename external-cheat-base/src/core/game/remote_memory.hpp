#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace game
{
    // The same access layer is exercised against a fake address space in tests.
    class RemoteMemory
    {
    public:
        virtual ~RemoteMemory() = default;
        virtual bool read(std::uintptr_t address, void* value, std::size_t size) = 0;
        virtual bool write(std::uintptr_t address, const void* value, std::size_t size) = 0;
        [[nodiscard]] virtual bool writesAllowed() const = 0;
        [[nodiscard]] virtual std::uint32_t lastError() const { return 0; }

        template <typename T>
        bool readValue(const std::uintptr_t address, T& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            value = {};
            return address != 0 && read(address, &value, sizeof(value));
        }
    };
}
