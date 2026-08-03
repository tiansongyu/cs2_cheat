#pragma once

#include "public_relay_config.hpp"

#include <memory>
#include <string>

namespace web_radar
{
    // Outbound-only WSS producer for the hosted Radar Relay. Network work is
    // isolated to one background thread and publish() only replaces a single
    // latest-state slot, so game sampling can never wait on the Internet.
    class PublicRelayProducer final
    {
    public:
        explicit PublicRelayProducer(PublicRelayConfig config);
        ~PublicRelayProducer();

        PublicRelayProducer(const PublicRelayProducer&) = delete;
        PublicRelayProducer& operator=(const PublicRelayProducer&) = delete;
        PublicRelayProducer(PublicRelayProducer&&) = delete;
        PublicRelayProducer& operator=(PublicRelayProducer&&) = delete;

        [[nodiscard]] bool start() noexcept;

        // Signals the worker to leave without waiting for synchronous WinHTTP
        // work to return. This is safe to call from the UI thread; stop() is
        // intentionally reserved for a background reaper because a WebSocket
        // send has no documented hard cancellation deadline.
        void requestStop() noexcept;
        void stop() noexcept;

        // Non-blocking with respect to DNS, TLS and socket I/O. Null or empty
        // frames are ignored. When called faster than the network can send,
        // only the newest complete snapshot is retained.
        void publish(std::shared_ptr<const std::string> absoluteState);

        [[nodiscard]] PublicRelayStatus status() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
