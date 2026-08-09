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
        int idleRate = 20;
    };

    struct RadarDemand
    {
        bool localOverlay = false;
        bool embeddedService = false;
        bool embeddedViewers = false;
        bool publicRelay = false;
        bool recording = false;
        bool foreground = true;
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
        return std::clamp(demand.idleRate, 1, 60);
    }

    constexpr int radarSamplingRate(const RadarDemand demand)
    {
        if (demand.foreground &&
            (demand.localOverlay || demand.embeddedViewers ||
             demand.publicRelay || demand.recording)) {
            return 20;
        }
        if (!demand.foreground &&
            (demand.embeddedViewers || demand.publicRelay)) {
            return 10;
        }
        if (demand.embeddedService) {
            return 4;
        }
        return 0;
    }

    constexpr std::chrono::milliseconds metadataRefreshInterval(
        const bool latencySensitive)
    {
        return latencySensitive
            ? std::chrono::milliseconds(100)
            : std::chrono::milliseconds(250);
    }

    constexpr std::chrono::nanoseconds intervalForRate(int rate)
    {
        return std::chrono::nanoseconds(
            1'000'000'000LL / std::max(1, rate));
    }
}
