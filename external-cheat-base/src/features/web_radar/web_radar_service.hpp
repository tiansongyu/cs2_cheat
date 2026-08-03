#pragma once

#include "web_radar_config.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace web_radar
{
    enum class WebRadarServiceState : std::uint8_t
    {
        stopped,
        starting,
        running,
        stopping,
        failed
    };

    struct WebRadarStatus
    {
        WebRadarServiceState state = WebRadarServiceState::stopped;
        std::string bindAddress = "127.0.0.1";
        std::uint16_t port = 22006;
        std::size_t viewerCount = 0;
        std::string lastError;

        [[nodiscard]] bool isRunning() const noexcept
        {
            return state == WebRadarServiceState::running;
        }
    };

    // RAII owner for the embedded CivetWeb HTTP/WebSocket server.
    //
    // publish() is non-blocking with respect to network I/O. Each viewer has
    // one replaceable pending snapshot, so a slow viewer skips stale states
    // instead of growing an unbounded queue or delaying other viewers.
    class WebRadarService final
    {
    public:
        explicit WebRadarService(WebRadarConfig config);
        ~WebRadarService();

        WebRadarService(const WebRadarService&) = delete;
        WebRadarService& operator=(const WebRadarService&) = delete;
        WebRadarService(WebRadarService&&) = delete;
        WebRadarService& operator=(WebRadarService&&) = delete;

        // Idempotent. Returns true when the service is running. On failure,
        // status().lastError contains a user-facing reason.
        [[nodiscard]] bool start() noexcept;

        // Idempotent. Waits for the bounded-time writer workers and CivetWeb
        // server threads to finish.
        void stop() noexcept;

        // Publishes a complete, absolute JSON state. Null payloads are
        // ignored. The caller may reuse the immutable shared payload after
        // this call returns.
        void publish(std::shared_ptr<const std::string> absoluteState);

        [[nodiscard]] bool isRunning() const noexcept;
        [[nodiscard]] std::size_t viewerCount() const noexcept;
        [[nodiscard]] WebRadarStatus status() const;

        // Pure validation hook used by unit tests and the configuration UI.
        // An empty return value means the configuration is valid.
        [[nodiscard]] static std::string validateConfig(
            const WebRadarConfig& config);

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
