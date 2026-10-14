#pragma once

#include "skin_changer.hpp"

#include <cstdint>
#include <memory>

namespace skins
{
    class Runtime final
    {
    public:
        explicit Runtime(std::uintptr_t clientModule);
        ~Runtime();
        Runtime(const Runtime&) = delete;
        Runtime& operator=(const Runtime&) = delete;
        void tick(const Settings& settings, bool enabled, bool foreground,
                  bool diagnostics, std::uint64_t request);
        void shutdown();
        [[nodiscard]] Status status() const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}
