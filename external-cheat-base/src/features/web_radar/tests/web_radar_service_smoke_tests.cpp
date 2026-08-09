#include "web_radar_service.hpp"

#include <civetweb.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
    using namespace std::chrono_literals;

    void require(const bool condition, const char* message)
    {
        if (!condition) {
            throw std::runtime_error(message);
        }
    }

    class TemporaryDocumentRoot final
    {
    public:
        TemporaryDocumentRoot()
        {
            const auto unique = std::chrono::steady_clock::now()
                .time_since_epoch()
                .count();
            path_ = std::filesystem::temp_directory_path()
                / ("cs2-web-radar-test-" + std::to_string(unique));
            std::filesystem::create_directories(path_);
            std::ofstream(path_ / "index.html") << "web-radar-smoke-test";
        }

        ~TemporaryDocumentRoot()
        {
            std::error_code ignored;
            std::filesystem::remove_all(path_, ignored);
        }

        [[nodiscard]] const std::filesystem::path& path() const noexcept
        {
            return path_;
        }

    private:
        std::filesystem::path path_;
    };

    struct HttpResponse
    {
        int status = 0;
        std::string body;
    };

    HttpResponse get(const std::uint16_t port, const char* path)
    {
        char error[256]{};
        struct mg_connection* connection = mg_connect_client(
            "127.0.0.1",
            port,
            0,
            error,
            sizeof(error));
        if (connection == nullptr) {
            throw std::runtime_error(
                std::string("HTTP connection failed: ") + error);
        }

        mg_printf(
            connection,
            "GET %s HTTP/1.1\r\nHost: 127.0.0.1\r\n"
            "Connection: close\r\n\r\n",
            path);
        if (mg_get_response(connection, error, sizeof(error), 3000) < 0) {
            mg_close_connection(connection);
            throw std::runtime_error(
                std::string("HTTP response failed: ") + error);
        }

        const struct mg_response_info* information =
            mg_get_response_info(connection);
        require(information != nullptr, "Missing HTTP response information");

        HttpResponse response;
        response.status = information->status_code;
        const long long contentLength = information->content_length;
        while (contentLength < 0
               || response.body.size()
                    < static_cast<std::size_t>(contentLength)) {
            char buffer[512]{};
            const int count = mg_read(connection, buffer, sizeof(buffer));
            if (count <= 0) {
                break;
            }
            response.body.append(buffer, static_cast<std::size_t>(count));
        }
        mg_close_connection(connection);
        return response;
    }

    struct WebSocketState
    {
        std::mutex mutex;
        std::condition_variable changed;
        std::string latestMessage;
        bool closed = false;
    };

    int onWebSocketData(
        struct mg_connection* connection,
        const int flags,
        char* data,
        const std::size_t length,
        void* userData)
    {
        auto& state = *static_cast<WebSocketState*>(userData);
        const int opcode = flags & 0x0f;
        if (opcode == MG_WEBSOCKET_OPCODE_PING) {
            mg_websocket_client_write(
                connection,
                MG_WEBSOCKET_OPCODE_PONG,
                data,
                length);
            return 1;
        }
        if (opcode == MG_WEBSOCKET_OPCODE_TEXT) {
            {
                std::scoped_lock lock(state.mutex);
                state.latestMessage.assign(data, length);
            }
            state.changed.notify_all();
        }
        return opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE ? 0 : 1;
    }

    void onWebSocketClose(const struct mg_connection*, void* userData)
    {
        auto& state = *static_cast<WebSocketState*>(userData);
        {
            std::scoped_lock lock(state.mutex);
            state.closed = true;
        }
        state.changed.notify_all();
    }

    template <typename Predicate>
    bool waitFor(WebSocketState& state, Predicate predicate)
    {
        std::unique_lock lock(state.mutex);
        return state.changed.wait_for(lock, 3s, predicate);
    }

    bool waitForViewerCount(
        const web_radar::WebRadarService& service,
        const std::size_t expected)
    {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (std::chrono::steady_clock::now() < deadline) {
            if (service.viewerCount() == expected) {
                return true;
            }
            std::this_thread::sleep_for(10ms);
        }
        return service.viewerCount() == expected;
    }
}

int main()
{
    try {
        TemporaryDocumentRoot documentRoot;
        web_radar::WebRadarConfig config;
        config.documentRoot = documentRoot.path().string();
        config.token = "0123456789abcdef0123456789abcdef";

        web_radar::WebRadarConfig invalid = config;
        invalid.token = "short";
        require(
            !web_radar::WebRadarService::validateConfig(invalid).empty(),
            "Short token was accepted");
        invalid = config;
        invalid.bindAddress = "127.0.0.1,0.0.0.0";
        require(
            !web_radar::WebRadarService::validateConfig(invalid).empty(),
            "Multiple listener injection was accepted");
        require(
            web_radar::WebRadarService::validateConfig(config).empty(),
            "Valid configuration was rejected");

        std::unique_ptr<web_radar::WebRadarService> service;
        const auto seed = static_cast<unsigned>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            config.port = static_cast<std::uint16_t>(
                32000 + ((seed + attempt) % 20000));
            auto candidate =
                std::make_unique<web_radar::WebRadarService>(config);
            if (candidate->start()) {
                service = std::move(candidate);
                break;
            }
        }
        require(service != nullptr, "Could not bind a smoke-test port");
        require(service->start(), "Idempotent start failed");

        const HttpResponse status = get(config.port, "/api/v1/status");
        require(status.status == 200, "Status endpoint did not return 200");
        require(
            status.body.find("\"running\":true") != std::string::npos,
            "Status endpoint did not report running state");
        require(
            status.body.find(config.token) == std::string::npos,
            "Status endpoint leaked its token");

        const HttpResponse index = get(config.port, "/");
        require(index.status == 200, "Static index did not return 200");
        require(
            index.body == "web-radar-smoke-test",
            "Static index body did not match");

        char error[256]{};
        WebSocketState rejectedState;
        struct mg_connection* rejected = mg_connect_websocket_client(
            "127.0.0.1",
            config.port,
            0,
            error,
            sizeof(error),
            "/api/v1/stream?token=wrong-token-value",
            "http://127.0.0.1",
            onWebSocketData,
            onWebSocketClose,
            &rejectedState);
        require(rejected == nullptr, "Invalid WebSocket token was accepted");

        service->publish(
            std::make_shared<const std::string>("{\"sequence\":41}"));

        WebSocketState viewerState;
        const std::string streamPath =
            "/api/v1/stream?token=" + config.token;
        struct mg_connection* viewer = mg_connect_websocket_client(
            "127.0.0.1",
            config.port,
            0,
            error,
            sizeof(error),
            streamPath.c_str(),
            "http://127.0.0.1",
            onWebSocketData,
            onWebSocketClose,
            &viewerState);
        require(viewer != nullptr, "Valid WebSocket token was rejected");
        require(
            waitForViewerCount(*service, 1),
            "Viewer count did not increase");
        require(
            waitFor(viewerState, [&viewerState] {
                return viewerState.latestMessage == "{\"sequence\":41}";
            }),
            "New viewer did not receive the latest absolute snapshot");

        const auto snapshot =
            std::make_shared<const std::string>("{\"sequence\":42}");
        service->publish(snapshot);
        require(
            waitFor(viewerState, [&viewerState] {
                return viewerState.latestMessage == "{\"sequence\":42}";
            }),
            "Published snapshot was not received");
        const web_radar::WebRadarStatus metrics = service->status();
        require(metrics.publishedFrames == 2,
            "Published frame metric did not advance");
        require(metrics.sentFrames >= 2,
            "Sent frame metric did not advance");
        require(metrics.publishedBytes > 0,
            "Published byte metric did not advance");

        mg_websocket_client_write(
            viewer,
            MG_WEBSOCKET_OPCODE_TEXT,
            "{}",
            2);
        require(
            waitFor(viewerState, [&viewerState] {
                return viewerState.closed;
            }),
            "Receive-only viewer was not closed after publishing data");
        require(
            waitForViewerCount(*service, 0),
            "Viewer count did not decrease");
        mg_close_connection(viewer);

        WebSocketState stopState;
        viewer = mg_connect_websocket_client(
            "127.0.0.1",
            config.port,
            0,
            error,
            sizeof(error),
            streamPath.c_str(),
            "http://127.0.0.1",
            onWebSocketData,
            onWebSocketClose,
            &stopState);
        require(viewer != nullptr, "Second viewer could not connect");
        require(
            waitForViewerCount(*service, 1),
            "Second viewer count did not increase");
        service->stop();
        require(
            waitFor(stopState, [&stopState] { return stopState.closed; }),
            "Active viewer was not closed during service stop");
        mg_close_connection(viewer);
        service->stop();
        require(!service->isRunning(), "Idempotent stop failed");

        std::cout << "web_radar_service smoke tests passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "web_radar_service smoke test failed: "
                  << exception.what() << '\n';
        return 1;
    }
}
