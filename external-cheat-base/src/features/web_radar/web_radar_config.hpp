#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace web_radar
{
    struct WebRadarConfig
    {
        // Safe by default: callers must explicitly opt in to a LAN bind such
        // as 0.0.0.0.
        std::string bindAddress = "127.0.0.1";
        std::uint16_t port = 22006;
        std::string documentRoot = "web-radar/dist";

        // URL-safe bearer token (A-Z, a-z, 0-9, '_' and '-'). Generate at
        // least 128 bits of entropy; a 32-character random token is suitable.
        std::string token;

        std::size_t maxViewers = 16;
        std::uint32_t workerThreads = 24;
        std::uint32_t requestTimeoutMilliseconds = 2000;
        std::uint32_t websocketTimeoutMilliseconds = 15000;
    };
}
