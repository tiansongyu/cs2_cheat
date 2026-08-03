#include "../public_relay_config.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    void require(const bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    web_radar::PublicRelayConfig validConfig()
    {
        web_radar::PublicRelayConfig config;
        config.endpointUrl =
            "wss://radar.example.com/api/v1/publish";
        config.room = "match_2026-08-03";
        config.token = "0123456789abcdef0123456789abcdef";
        return config;
    }
}

int main()
{
    using namespace std::chrono_literals;

    auto config = validConfig();
    require(
        web_radar::validatePublicRelayConfig(config).empty(),
        "Expected the baseline Relay configuration to be valid");

    std::string error;
    auto endpoint = web_radar::parsePublicRelayEndpoint(
        "WSS://relay.example.test:8443/custom/publish",
        &error);
    require(endpoint.has_value(), "Expected case-insensitive WSS scheme");
    require(endpoint->host == "relay.example.test", "Unexpected parsed host");
    require(endpoint->port == 8443, "Unexpected parsed port");
    require(endpoint->path == "/custom/publish", "Unexpected parsed path");

    endpoint = web_radar::parsePublicRelayEndpoint(
        "wss://[2001:db8::1]/api/v1/publish",
        &error);
    require(endpoint.has_value(), "Expected bracketed IPv6 to be supported");
    require(endpoint->host == "2001:db8::1", "Unexpected IPv6 host");

    require(
        !web_radar::parsePublicRelayEndpoint(
            "ws://radar.example.com/api/v1/publish"),
        "Plain WS must be rejected");
    require(
        !web_radar::parsePublicRelayEndpoint(
            "wss://user@radar.example.com/api/v1/publish"),
        "URL user information must be rejected");
    require(
        !web_radar::parsePublicRelayEndpoint(
            "wss://radar.example.com/api/v1/publish?token=secret"),
        "URL query strings must be rejected");
    require(
        !web_radar::parsePublicRelayEndpoint(
            "wss://radar.example.com/api/v1/publish#fragment"),
        "URL fragments must be rejected");
    require(
        !web_radar::parsePublicRelayEndpoint(
            "wss://radar.example.com:0/api/v1/publish"),
        "Port zero must be rejected");
    require(
        !web_radar::parsePublicRelayEndpoint(
            "wss://radar.example.com:/api/v1/publish"),
        "An empty explicit port must be rejected");

    config = validConfig();
    config.room = "bad room";
    require(
        !web_radar::validatePublicRelayConfig(config).empty(),
        "Whitespace in room header must be rejected");
    config = validConfig();
    config.token = "0123456789abcdef012345\r\nInjected: yes";
    require(
        !web_radar::validatePublicRelayConfig(config).empty(),
        "Header injection in token must be rejected");
    config = validConfig();
    config.connectTimeoutMilliseconds = 31'000;
    require(
        !web_radar::validatePublicRelayConfig(config).empty(),
        "Unbounded timeout configuration must be rejected");

    require(
        web_radar::publicRelayBackoffDelay(0, 20) == 1s,
        "Unexpected first retry delay");
    require(
        web_radar::publicRelayBackoffDelay(1, 20) == 2s,
        "Unexpected second retry delay");
    require(
        web_radar::publicRelayBackoffDelay(2, 20) == 4s,
        "Unexpected third retry delay");
    require(
        web_radar::publicRelayBackoffDelay(3, 20) == 8s,
        "Unexpected fourth retry delay");
    require(
        web_radar::publicRelayBackoffDelay(4, 20) == 16s,
        "Unexpected fifth retry delay");
    require(
        web_radar::publicRelayBackoffDelay(5, 20) == 30s,
        "Unexpected capped retry delay");
    require(
        web_radar::publicRelayBackoffDelay(30, 40) <= 30s,
        "Jitter must never exceed the retry cap");
    require(
        web_radar::publicRelayBackoffDelay(0, 0) == 800ms &&
        web_radar::publicRelayBackoffDelay(0, 40) == 1200ms,
        "Retry jitter must stay within +/-20 percent");

    std::cout << "public relay config tests passed\n";
    return 0;
}
