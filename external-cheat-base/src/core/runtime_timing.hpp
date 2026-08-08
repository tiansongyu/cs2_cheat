#pragma once

#include <algorithm>
#include <chrono>

namespace runtime_timing
{
    struct SamplingDemand
    {
        bool latencySensitive = false;
        bool displaySynchronized = false;
        bool periodic = false;
    };

    constexpr int dataSamplingRate(
        int displayRefreshRate,
        const SamplingDemand demand)
    {
        if (demand.latencySensitive) {
            return 240;
        }
        if (demand.displaySynchronized) {
            return std::clamp(displayRefreshRate, 60, 240);
        }
        if (demand.periodic) {
            return 60;
        }
        return 20;
    }

    constexpr std::chrono::nanoseconds intervalForRate(int rate)
    {
        return std::chrono::nanoseconds(
            1'000'000'000LL / std::max(1, rate));
    }
}
