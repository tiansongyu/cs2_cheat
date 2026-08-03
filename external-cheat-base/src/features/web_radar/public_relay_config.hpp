#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

namespace web_radar
{
    struct PublicRelayEndpoint
    {
        std::string host;
        std::uint16_t port = 443;
        std::string path = "/";
    };

    struct PublicRelayConfig
    {
        // This is the producer WebSocket endpoint, normally
        // wss://radar.example.com/api/v1/publish. Plain ws:// is never
        // accepted and WinHTTP's normal certificate validation is mandatory.
        std::string endpointUrl;
        std::string room;
        std::string token;

        std::uint32_t resolveTimeoutMilliseconds = 5000;
        std::uint32_t connectTimeoutMilliseconds = 5000;
        std::uint32_t sendTimeoutMilliseconds = 5000;
        std::uint32_t receiveTimeoutMilliseconds = 5000;

        // These defaults mirror the production Relay. Keeping both limits
        // explicit prevents a stale or unexpectedly large snapshot from
        // turning a weak connection into a reconnect loop. Deployments that
        // deliberately tune the Relay may tune the producer to match.
        std::uint32_t maxSnapshotBytes = 512U * 1024U;
        std::uint32_t maximumQueuedSnapshotAgeMilliseconds = 5000;
    };

    enum class PublicRelayState : std::uint8_t
    {
        disabled,
        connecting,
        connected,
        backoff,
        retiring,
        failed
    };

    struct PublicRelayStatus
    {
        PublicRelayState state = PublicRelayState::disabled;
        std::uint64_t framesSent = 0;
        std::uint64_t replacedFrames = 0;
        std::uint64_t droppedFrames = 0;
        std::uint64_t reconnects = 0;
        std::string lastError;
    };

    namespace detail
    {
        [[nodiscard]] inline bool relayAsciiAlphaNumeric(
            const unsigned char character) noexcept
        {
            return (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9');
        }

        [[nodiscard]] inline bool validRelayDnsHost(
            const std::string_view host) noexcept
        {
            if (host.empty() || host.size() > 253) {
                return false;
            }
            std::size_t labelStart = 0;
            while (labelStart < host.size()) {
                const std::size_t separator = host.find('.', labelStart);
                const std::size_t labelEnd =
                    separator == std::string_view::npos
                        ? host.size()
                        : separator;
                const std::size_t labelSize = labelEnd - labelStart;
                if (labelSize == 0 || labelSize > 63 ||
                    !relayAsciiAlphaNumeric(
                        static_cast<unsigned char>(host[labelStart])) ||
                    !relayAsciiAlphaNumeric(
                        static_cast<unsigned char>(host[labelEnd - 1]))) {
                    return false;
                }
                for (std::size_t index = labelStart;
                     index < labelEnd;
                     ++index) {
                    const auto character =
                        static_cast<unsigned char>(host[index]);
                    if (!relayAsciiAlphaNumeric(character) &&
                        character != '-') {
                        return false;
                    }
                }
                if (separator == std::string_view::npos) {
                    return true;
                }
                labelStart = separator + 1;
            }
            return false;
        }

        [[nodiscard]] inline bool relayStartsWithWss(
            const std::string_view value) noexcept
        {
            if (value.size() < 6) {
                return false;
            }
            constexpr std::string_view expected = "wss://";
            for (std::size_t index = 0; index < expected.size(); ++index) {
                const auto character = static_cast<unsigned char>(value[index]);
                if (static_cast<char>(std::tolower(character)) != expected[index]) {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] inline bool validRelayPort(
            const std::string_view text,
            std::uint16_t& parsed) noexcept
        {
            if (text.empty() || text.size() > 5) {
                return false;
            }
            std::uint32_t value = 0;
            for (const char rawCharacter : text) {
                const auto character = static_cast<unsigned char>(rawCharacter);
                if (character < '0' || character > '9') {
                    return false;
                }
                value = value * 10U + static_cast<std::uint32_t>(character - '0');
            }
            if (value == 0 || value > std::numeric_limits<std::uint16_t>::max()) {
                return false;
            }
            parsed = static_cast<std::uint16_t>(value);
            return true;
        }
    }

    // Strict, allocation-light URL parsing shared by the UI tests and the
    // Windows transport. User info, query strings and fragments are rejected
    // so credentials cannot accidentally migrate into a URL or access log.
    [[nodiscard]] inline std::optional<PublicRelayEndpoint>
    parsePublicRelayEndpoint(
        const std::string_view url,
        std::string* error = nullptr)
    {
        const auto reject = [error](const char* message)
            -> std::optional<PublicRelayEndpoint> {
            if (error != nullptr) {
                *error = message;
            }
            return std::nullopt;
        };

        if (url.empty() || url.size() > 2048) {
            return reject("Relay URL must contain 1 to 2048 characters.");
        }
        if (!detail::relayStartsWithWss(url)) {
            return reject("Relay URL must use wss://.");
        }

        const std::string_view remainder = url.substr(6);
        const std::size_t pathOffset = remainder.find('/');
        const std::string_view authority = remainder.substr(0, pathOffset);
        const std::string_view path = pathOffset == std::string_view::npos
            ? std::string_view{"/"}
            : remainder.substr(pathOffset);
        if (authority.empty()) {
            return reject("Relay URL is missing a host.");
        }
        if (authority.find('@') != std::string_view::npos) {
            return reject("Relay URL must not contain user information.");
        }
        if (path.find('?') != std::string_view::npos ||
            path.find('#') != std::string_view::npos) {
            return reject("Relay URL must not contain a query or fragment.");
        }
        for (const char rawCharacter : path) {
            const auto character = static_cast<unsigned char>(rawCharacter);
            if (character <= 0x20U || character >= 0x7fU || character == '\\') {
                return reject("Relay URL path contains an unsupported character.");
            }
        }

        PublicRelayEndpoint endpoint;
        endpoint.path.assign(path);
        std::string_view host;
        std::string_view portText;
        bool explicitPort = false;
        if (authority.front() == '[') {
            const std::size_t closingBracket = authority.find(']');
            if (closingBracket == std::string_view::npos || closingBracket == 1) {
                return reject("Relay URL contains an invalid IPv6 host.");
            }
            host = authority.substr(1, closingBracket - 1);
            const std::string_view suffix = authority.substr(closingBracket + 1);
            if (!suffix.empty()) {
                if (suffix.front() != ':') {
                    return reject("Relay URL contains an invalid host suffix.");
                }
                explicitPort = true;
                portText = suffix.substr(1);
            }
            if (host.size() > 45 ||
                host.find(':') == std::string_view::npos) {
                return reject("Relay URL contains an invalid IPv6 host.");
            }
            for (const char rawCharacter : host) {
                const auto character = static_cast<unsigned char>(rawCharacter);
                const bool hexadecimal =
                    (character >= 'a' && character <= 'f') ||
                    (character >= 'A' && character <= 'F') ||
                    (character >= '0' && character <= '9');
                if (!hexadecimal && character != ':' && character != '.') {
                    return reject("Relay URL contains an invalid IPv6 host.");
                }
            }
        } else {
            const std::size_t colon = authority.rfind(':');
            if (colon != std::string_view::npos) {
                if (authority.find(':') != colon) {
                    return reject("IPv6 hosts in Relay URLs must use brackets.");
                }
                host = authority.substr(0, colon);
                explicitPort = true;
                portText = authority.substr(colon + 1);
            } else {
                host = authority;
            }
            if (host.empty()) {
                return reject("Relay URL is missing a host.");
            }
            if (!detail::validRelayDnsHost(host)) {
                return reject("Relay URL contains an invalid host.");
            }
        }

        endpoint.host.assign(host);
        if (explicitPort &&
            !detail::validRelayPort(portText, endpoint.port)) {
            return reject("Relay URL contains an invalid port.");
        }
        if (error != nullptr) {
            error->clear();
        }
        return endpoint;
    }

    [[nodiscard]] inline std::string validatePublicRelayConfig(
        const PublicRelayConfig& config)
    {
        std::string error;
        if (!parsePublicRelayEndpoint(config.endpointUrl, &error)) {
            return error;
        }
        if (config.room.size() < 3 || config.room.size() > 64) {
            return "Relay room must contain 3 to 64 characters.";
        }
        for (const char rawCharacter : config.room) {
            const auto character = static_cast<unsigned char>(rawCharacter);
            if (!detail::relayAsciiAlphaNumeric(character) &&
                character != '-' && character != '_') {
                return "Relay room may contain only letters, digits, '-' and '_'.";
            }
        }
        if (config.token.size() < 24 || config.token.size() > 512) {
            return "Relay producer token must contain 24 to 512 characters.";
        }
        for (const char rawCharacter : config.token) {
            const auto character = static_cast<unsigned char>(rawCharacter);
            if (character < 0x21U || character > 0x7eU) {
                return "Relay producer token must contain visible ASCII characters only.";
            }
        }
        const auto validTimeout = [](const std::uint32_t value) {
            return value >= 100 && value <= 30000;
        };
        if (!validTimeout(config.resolveTimeoutMilliseconds) ||
            !validTimeout(config.connectTimeoutMilliseconds) ||
            !validTimeout(config.sendTimeoutMilliseconds) ||
            !validTimeout(config.receiveTimeoutMilliseconds)) {
            return "Relay network timeouts must be between 100 and 30000 ms.";
        }
        if (config.maxSnapshotBytes < 4096U ||
            config.maxSnapshotBytes > 4U * 1024U * 1024U) {
            return "Relay snapshot limit must be between 4096 and 4194304 bytes.";
        }
        if (config.maximumQueuedSnapshotAgeMilliseconds < 250U ||
            config.maximumQueuedSnapshotAgeMilliseconds > 30000U) {
            return "Relay queued snapshot age must be between 250 and 30000 ms.";
        }
        return {};
    }

    [[nodiscard]] inline bool publicRelaySnapshotFits(
        const std::size_t snapshotBytes,
        const std::uint32_t maximumBytes) noexcept
    {
        return snapshotBytes != 0 && snapshotBytes <= maximumBytes;
    }

    [[nodiscard]] inline bool publicRelayQueuedSnapshotExpired(
        const std::chrono::milliseconds queuedFor,
        const std::uint32_t maximumAgeMilliseconds) noexcept
    {
        return queuedFor >
            std::chrono::milliseconds(maximumAgeMilliseconds);
    }

    // Authentication/configuration errors require user action. Capacity and
    // transient request statuses stay retryable so a producer can recover
    // without exposing transport-specific policy to the WinHTTP worker.
    [[nodiscard]] inline bool publicRelayHttpFailureIsPermanent(
        const std::uint32_t statusCode) noexcept
    {
        return statusCode >= 400U && statusCode < 500U &&
            statusCode != 408U && statusCode != 409U &&
            statusCode != 425U && statusCode != 429U;
    }

    inline constexpr auto publicRelayHealthyConnectionDuration =
        std::chrono::seconds(10);

    [[nodiscard]] inline bool publicRelayConnectionWasHealthy(
        const std::chrono::milliseconds connectedFor) noexcept
    {
        return connectedFor >= publicRelayHealthyConnectionDuration;
    }

    // Exponential retry: 1, 2, 4, 8, 16 and 30 seconds, with deterministic
    // +/-20% jitter. The entropy input makes the pure function easy to test;
    // production supplies a fresh pseudo-random value for each failure.
    [[nodiscard]] inline std::chrono::milliseconds publicRelayBackoffDelay(
        const std::uint32_t failureCount,
        const std::uint32_t entropy) noexcept
    {
        constexpr std::uint64_t initialMilliseconds = 1000;
        constexpr std::uint64_t maximumMilliseconds = 30000;
        const std::uint32_t shift = std::min(failureCount, 5U);
        const std::uint64_t exponential = std::min(
            initialMilliseconds << shift,
            maximumMilliseconds);
        const std::uint64_t jitterPercent = 80U + entropy % 41U;
        return std::chrono::milliseconds(std::min(
            exponential * jitterPercent / 100U,
            maximumMilliseconds));
    }
}
