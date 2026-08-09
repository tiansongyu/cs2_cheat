#include "web_radar_service.hpp"

#include <CivetServer.h>
#include <civetweb.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace web_radar
{
    namespace
    {
        constexpr const char* statusPath = "/api/v1/status";
        constexpr const char* streamPath = "/api/v1/stream";

        std::mutex& civetLibraryMutex()
        {
            static std::mutex mutex;
            return mutex;
        }

        bool acquireCivetLibrary()
        {
            std::scoped_lock lock(civetLibraryMutex());
            constexpr unsigned required =
                MG_FEATURES_FILES | MG_FEATURES_WEBSOCKET;
            const unsigned available = mg_init_library(required);
            if ((available & required) == required) {
                return true;
            }

            // mg_init_library is reference counted even when the requested
            // build features are unavailable.
            mg_exit_library();
            return false;
        }

        void releaseCivetLibrary() noexcept
        {
            std::scoped_lock lock(civetLibraryMutex());
            mg_exit_library();
        }

        bool isUrlSafeToken(const std::string& token)
        {
            return std::all_of(
                token.begin(),
                token.end(),
                [](const char character) {
                    const auto value =
                        static_cast<unsigned char>(character);
                    return (value >= 'A' && value <= 'Z')
                        || (value >= 'a' && value <= 'z')
                        || (value >= '0' && value <= '9')
                        || value == '_'
                        || value == '-';
                });
        }

        bool constantTimeTokenEquals(
            const std::string& expected,
            const std::string& supplied) noexcept
        {
            std::size_t difference = expected.size() ^ supplied.size();
            for (std::size_t index = 0; index < expected.size(); ++index) {
                const unsigned char suppliedByte = index < supplied.size()
                    ? static_cast<unsigned char>(supplied[index])
                    : 0;
                difference |= static_cast<std::size_t>(
                    static_cast<unsigned char>(expected[index])
                    ^ suppliedByte);
            }
            return difference == 0;
        }

        const char* stateName(const WebRadarServiceState state) noexcept
        {
            switch (state) {
            case WebRadarServiceState::stopped:
                return "stopped";
            case WebRadarServiceState::starting:
                return "starting";
            case WebRadarServiceState::running:
                return "running";
            case WebRadarServiceState::stopping:
                return "stopping";
            case WebRadarServiceState::failed:
                return "failed";
            }
            return "unknown";
        }

        std::string escapeJson(const std::string& input)
        {
            static constexpr char digits[] = "0123456789abcdef";
            std::string output;
            output.reserve(input.size() + 8);

            for (const char character : input) {
                const auto value = static_cast<unsigned char>(character);
                switch (value) {
                case '"':
                    output += "\\\"";
                    break;
                case '\\':
                    output += "\\\\";
                    break;
                case '\b':
                    output += "\\b";
                    break;
                case '\f':
                    output += "\\f";
                    break;
                case '\n':
                    output += "\\n";
                    break;
                case '\r':
                    output += "\\r";
                    break;
                case '\t':
                    output += "\\t";
                    break;
                default:
                    if (value < 0x20) {
                        output += "\\u00";
                        output += digits[value >> 4];
                        output += digits[value & 0x0f];
                    } else {
                        output += static_cast<char>(value);
                    }
                    break;
                }
            }
            return output;
        }

        std::string listeningPortValue(const WebRadarConfig& config)
        {
            std::string address = config.bindAddress;
            if (address.find(':') != std::string::npos
                && !(address.front() == '[' && address.back() == ']')) {
                address = '[' + address + ']';
            }
            return address + ':' + std::to_string(config.port);
        }

        void sendHttpError(
            const struct mg_connection* connection,
            const int status,
            const char* message)
        {
            mg_send_http_error(
                const_cast<struct mg_connection*>(connection),
                status,
                "%s",
                message);
        }
    }

    class WebRadarService::Impl final
    {
    public:
        explicit Impl(WebRadarConfig config)
            : config_(std::move(config))
        {
        }

        ~Impl()
        {
            stop();
        }

        bool start() noexcept
        {
            std::scoped_lock operationLock(lifecycleOperationMutex_);

            if (state_.load(std::memory_order_acquire)
                == WebRadarServiceState::running) {
                return true;
            }

            state_.store(
                WebRadarServiceState::starting,
                std::memory_order_release);
            setLastError({});

            const std::string validationError =
                WebRadarService::validateConfig(config_);
            if (!validationError.empty()) {
                fail(validationError);
                return false;
            }

            if (!acquireCivetLibrary()) {
                fail(
                    "CivetWeb was built without static-file or WebSocket "
                    "support (USE_WEBSOCKET is required)");
                return false;
            }
            libraryAcquired_ = true;

            try {
                std::error_code filesystemError;
                const std::filesystem::path absoluteDocumentRoot =
                    std::filesystem::absolute(
                        std::filesystem::path(config_.documentRoot),
                        filesystemError);
                if (filesystemError) {
                    throw std::runtime_error(
                        "Could not resolve Web Radar document root: "
                        + filesystemError.message());
                }

                std::vector<std::string> options{
                    "listening_ports",
                    listeningPortValue(config_),
                    "document_root",
                    absoluteDocumentRoot.string(),
                    "num_threads",
                    std::to_string(config_.workerThreads),
                    "request_timeout_ms",
                    std::to_string(config_.requestTimeoutMilliseconds),
                    "websocket_timeout_ms",
                    std::to_string(config_.websocketTimeoutMilliseconds),
                    "enable_websocket_ping_pong",
                    "yes",
                    "enable_keep_alive",
                    "yes",
                    "tcp_nodelay",
                    "1",
                    "max_request_size",
                    "8192",
                    "enable_directory_listing",
                    "no",
                    "enable_webdav",
                    "no",
                    "index_files",
                    "index.html",
                    "ssi_pattern",
                    "",
                    "access_control_allow_origin",
                    "",
                    "access_control_allow_methods",
                    "",
                    "access_control_allow_headers",
                    ""
                };

                auto statusHandler =
                    std::make_unique<StatusHandler>(*this);
                auto streamHandler =
                    std::make_unique<StreamHandler>(*this);
                auto server = std::make_unique<CivetServer>(options);

                server->addHandler(statusPath, statusHandler.get());
                server->addWebSocketHandler(
                    streamPath,
                    streamHandler.get());

                {
                    std::scoped_lock stateLock(stateMutex_);
                    statusHandler_ = std::move(statusHandler);
                    streamHandler_ = std::move(streamHandler);
                    server_ = std::move(server);
                }

                acceptingViewers_.store(true, std::memory_order_release);
                state_.store(
                    WebRadarServiceState::running,
                    std::memory_order_release);
                return true;
            } catch (const std::exception& exception) {
                acceptingViewers_.store(false, std::memory_order_release);
                cleanupFailedStart();
                fail(exception.what());
                return false;
            } catch (...) {
                acceptingViewers_.store(false, std::memory_order_release);
                cleanupFailedStart();
                fail("Unknown error while starting CivetWeb");
                return false;
            }
        }

        void stop() noexcept
        {
            std::scoped_lock operationLock(lifecycleOperationMutex_);

            const WebRadarServiceState oldState =
                state_.load(std::memory_order_acquire);
            if (oldState == WebRadarServiceState::stopped) {
                return;
            }

            state_.store(
                WebRadarServiceState::stopping,
                std::memory_order_release);
            acceptingViewers_.store(false, std::memory_order_release);

            std::unique_ptr<CivetServer> server;
            std::unique_ptr<StatusHandler> statusHandler;
            std::unique_ptr<StreamHandler> streamHandler;
            {
                std::scoped_lock stateLock(stateMutex_);
                server = std::move(server_);
                statusHandler = std::move(statusHandler_);
                streamHandler = std::move(streamHandler_);
            }

            stopAllViewers();

            if (server) {
                server->close();
                server.reset();
            }
            streamHandler.reset();
            statusHandler.reset();

            if (libraryAcquired_) {
                releaseCivetLibrary();
                libraryAcquired_ = false;
            }

            state_.store(
                WebRadarServiceState::stopped,
                std::memory_order_release);
        }

        void publish(std::shared_ptr<const std::string> absoluteState)
        {
            if (!absoluteState
                || state_.load(std::memory_order_acquire)
                    != WebRadarServiceState::running) {
                return;
            }

            Snapshot snapshot;
            {
                std::scoped_lock snapshotLock(snapshotMutex_);
                ++publishedGeneration_;
                snapshot = {
                    publishedGeneration_,
                    std::move(absoluteState),
                    std::chrono::steady_clock::now()
                };
                latestSnapshot_ = snapshot;
            }

            publishedFrames_.fetch_add(1, std::memory_order_relaxed);
            publishedBytes_.fetch_add(
                snapshot.payload->size(),
                std::memory_order_relaxed);

            thread_local std::vector<std::shared_ptr<Viewer>> viewers;
            viewers.clear();
            {
                std::scoped_lock viewerLock(viewersMutex_);
                viewers.reserve(viewers_.size());
                for (const auto& [connection, viewer] : viewers_) {
                    static_cast<void>(connection);
                    viewers.push_back(viewer);
                }
            }

            for (const auto& viewer : viewers) {
                if (viewer->enqueue(snapshot)) {
                    replacedFrames_.fetch_add(1, std::memory_order_relaxed);
                }
            }
            viewers.clear();
        }

        [[nodiscard]] bool isRunning() const noexcept
        {
            return state_.load(std::memory_order_acquire)
                == WebRadarServiceState::running;
        }

        [[nodiscard]] std::size_t viewerCount() const noexcept
        {
            return viewerCount_.load(std::memory_order_acquire);
        }

        [[nodiscard]] WebRadarStatus status() const
        {
            WebRadarStatus result;
            result.state = state_.load(std::memory_order_acquire);
            result.bindAddress = config_.bindAddress;
            result.port = config_.port;
            result.viewerCount = viewerCount();
            result.publishedFrames = publishedFrames_.load(
                std::memory_order_relaxed);
            result.sentFrames = sentFrames_.load(std::memory_order_relaxed);
            result.replacedFrames = replacedFrames_.load(
                std::memory_order_relaxed);
            result.publishedBytes = publishedBytes_.load(
                std::memory_order_relaxed);
            result.maximumSendLatencyMilliseconds =
                static_cast<double>(maximumSendLatencyMicroseconds_.load(
                    std::memory_order_relaxed)) /
                1000.0;
            {
                std::scoped_lock stateLock(stateMutex_);
                result.lastError = lastError_;
            }
            return result;
        }

    private:
        struct Snapshot
        {
            std::uint64_t generation = 0;
            std::shared_ptr<const std::string> payload;
            std::chrono::steady_clock::time_point queuedAt{};
        };

        void recordSent(
            const std::chrono::steady_clock::time_point queuedAt) noexcept
        {
            sentFrames_.fetch_add(1, std::memory_order_relaxed);
            if (queuedAt.time_since_epoch().count() == 0) {
                return;
            }
            const auto measured = std::chrono::duration_cast<
                std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - queuedAt).count();
            const std::uint64_t microseconds = measured <= 0
                ? 0
                : static_cast<std::uint64_t>(measured);
            std::uint64_t maximum = maximumSendLatencyMicroseconds_.load(
                std::memory_order_relaxed);
            while (microseconds > maximum &&
                   !maximumSendLatencyMicroseconds_.compare_exchange_weak(
                       maximum,
                       microseconds,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed)) {
            }
        }

        class Viewer final
        {
        public:
            Viewer(struct mg_connection* connection, Impl& owner)
                : connection_(connection), owner_(owner)
            {
            }

            ~Viewer()
            {
                stopAndJoin();
            }

            void start()
            {
                writer_ = std::thread([this] { writeLoop(); });
            }

            [[nodiscard]] bool enqueue(const Snapshot& snapshot)
            {
                if (!snapshot.payload) {
                    return false;
                }

                bool replaced = false;
                {
                    std::scoped_lock lock(mutex_);
                    if (closing_ || snapshot.generation <= newestGeneration_) {
                        return false;
                    }
                    replaced = pending_.payload != nullptr;
                    newestGeneration_ = snapshot.generation;
                    pending_ = snapshot;
                }
                wake_.notify_one();
                return replaced;
            }

            void stopAndJoin() noexcept
            {
                std::scoped_lock joinLock(joinMutex_);
                {
                    std::scoped_lock lock(mutex_);
                    closing_ = true;
                    pending_ = {};
                }
                wake_.notify_one();
                if (writer_.joinable()) {
                    writer_.join();
                }
            }

        private:
            void writeLoop() noexcept
            {
                for (;;) {
                    Snapshot snapshot;
                    {
                        std::unique_lock lock(mutex_);
                        wake_.wait(lock, [this] {
                            return closing_ || pending_.payload != nullptr;
                        });
                        if (closing_) {
                            return;
                        }
                        snapshot = std::move(pending_);
                        pending_ = {};
                    }

                    const int written = mg_websocket_write(
                        connection_,
                        MG_WEBSOCKET_OPCODE_TEXT,
                        snapshot.payload->data(),
                        snapshot.payload->size());
                    if (written <= 0) {
                        // For a server-side WebSocket this sets CivetWeb's
                        // close flag. The Civet worker owns final teardown.
                        mg_close_connection(connection_);
                        std::scoped_lock lock(mutex_);
                        closing_ = true;
                        pending_ = {};
                        return;
                    }
                    owner_.recordSent(snapshot.queuedAt);
                }
            }

            struct mg_connection* connection_ = nullptr;
            Impl& owner_;
            std::mutex mutex_;
            std::mutex joinMutex_;
            std::condition_variable wake_;
            Snapshot pending_;
            std::uint64_t newestGeneration_ = 0;
            bool closing_ = false;
            std::thread writer_;
        };

        class StatusHandler final : public CivetHandler
        {
        public:
            explicit StatusHandler(Impl& owner)
                : owner_(owner)
            {
            }

            bool handleGet(
                CivetServer*,
                struct mg_connection* connection) override
            {
                const std::string body = owner_.statusJson();
                const std::string headers =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json; charset=utf-8\r\n"
                    "Cache-Control: no-store\r\n"
                    "X-Content-Type-Options: nosniff\r\n"
                    "Content-Length: "
                    + std::to_string(body.size())
                    + "\r\nConnection: close\r\n\r\n";
                mg_write(connection, headers.data(), headers.size());
                mg_write(connection, body.data(), body.size());
                return true;
            }

        private:
            Impl& owner_;
        };

        class StreamHandler final : public CivetWebSocketHandler
        {
        public:
            explicit StreamHandler(Impl& owner)
                : owner_(owner)
            {
            }

            bool handleConnection(
                CivetServer*,
                const struct mg_connection* connection) override
            {
                if (!owner_.acceptingViewers_.load(
                        std::memory_order_acquire)) {
                    sendHttpError(connection, 503, "Web Radar is stopping");
                    return false;
                }
                if (!owner_.isAuthorized(connection)) {
                    sendHttpError(connection, 401, "Unauthorized");
                    return false;
                }
                if (owner_.viewerCount() >= owner_.config_.maxViewers) {
                    sendHttpError(connection, 503, "Viewer limit reached");
                    return false;
                }
                return true;
            }

            void handleReadyState(
                CivetServer*,
                struct mg_connection* connection) override
            {
                owner_.addViewer(connection);
            }

            bool handleData(
                CivetServer*,
                struct mg_connection*,
                int,
                char*,
                std::size_t) override
            {
                // Viewers are receive-only. CivetWeb handles ping/pong before
                // this callback when enable_websocket_ping_pong is enabled;
                // every application frame therefore closes the connection.
                return false;
            }

            void handleClose(
                CivetServer*,
                const struct mg_connection* connection) override
            {
                owner_.removeViewer(connection);
            }

        private:
            Impl& owner_;
        };

        bool isAuthorized(const struct mg_connection* connection) const
        {
            const struct mg_request_info* request =
                mg_get_request_info(connection);
            if (request == nullptr || request->query_string == nullptr) {
                return false;
            }

            char suppliedToken[129]{};
            const int length = mg_get_var(
                request->query_string,
                std::char_traits<char>::length(request->query_string),
                "token",
                suppliedToken,
                sizeof(suppliedToken));
            if (length < 0) {
                return false;
            }
            return constantTimeTokenEquals(
                config_.token,
                std::string(suppliedToken, static_cast<std::size_t>(length)));
        }

        void addViewer(struct mg_connection* connection) noexcept
        {
            std::shared_ptr<Viewer> viewer;
            bool accepted = false;
            try {
                viewer = std::make_shared<Viewer>(connection, *this);
                viewer->start();
                {
                    std::scoped_lock viewerLock(viewersMutex_);
                    if (acceptingViewers_.load(std::memory_order_acquire)
                        && viewers_.size() < config_.maxViewers) {
                        const auto [position, inserted] =
                            viewers_.emplace(connection, viewer);
                        static_cast<void>(position);
                        accepted = inserted;
                        viewerCount_.store(
                            viewers_.size(),
                            std::memory_order_release);
                    }
                }
            } catch (...) {
                accepted = false;
            }

            if (!accepted) {
                if (viewer) {
                    viewer->stopAndJoin();
                }
                mg_close_connection(connection);
                return;
            }

            Snapshot latest;
            {
                std::scoped_lock snapshotLock(snapshotMutex_);
                latest = latestSnapshot_;
            }
            latest.queuedAt = std::chrono::steady_clock::now();
            static_cast<void>(viewer->enqueue(latest));
        }

        void removeViewer(const struct mg_connection* connection) noexcept
        {
            std::shared_ptr<Viewer> viewer;
            {
                std::scoped_lock viewerLock(viewersMutex_);
                const auto position = viewers_.find(connection);
                if (position != viewers_.end()) {
                    viewer = std::move(position->second);
                    viewers_.erase(position);
                }
                viewerCount_.store(
                    viewers_.size(),
                    std::memory_order_release);
            }
            if (viewer) {
                viewer->stopAndJoin();
            }
        }

        void stopAllViewers() noexcept
        {
            for (;;) {
                std::shared_ptr<Viewer> viewer;
                {
                    std::scoped_lock viewerLock(viewersMutex_);
                    if (viewers_.empty()) {
                        viewerCount_.store(0, std::memory_order_release);
                        return;
                    }
                    const auto position = viewers_.begin();
                    viewer = std::move(position->second);
                    viewers_.erase(position);
                    viewerCount_.store(
                        viewers_.size(),
                        std::memory_order_release);
                }
                viewer->stopAndJoin();
            }
        }

        [[nodiscard]] std::string statusJson() const
        {
            const WebRadarStatus current = status();
            std::ostringstream json;
            json << "{\"service\":\"web-radar\",\"protocol\":1"
                 << ",\"state\":\"" << stateName(current.state) << '"'
                 << ",\"running\":"
                 << (current.isRunning() ? "true" : "false")
                 << ",\"bind\":\"" << escapeJson(current.bindAddress)
                 << "\",\"port\":" << current.port
                 << ",\"viewers\":" << current.viewerCount
                 << ",\"publishedFrames\":" << current.publishedFrames
                 << ",\"sentFrames\":" << current.sentFrames
                 << ",\"replacedFrames\":" << current.replacedFrames
                 << ",\"publishedBytes\":" << current.publishedBytes
                 << ",\"maximumSendLatencyMs\":"
                 << current.maximumSendLatencyMilliseconds << '}';
            return json.str();
        }

        void setLastError(std::string error)
        {
            std::scoped_lock stateLock(stateMutex_);
            lastError_ = std::move(error);
        }

        void fail(std::string error)
        {
            setLastError(std::move(error));
            state_.store(
                WebRadarServiceState::failed,
                std::memory_order_release);
        }

        void cleanupFailedStart() noexcept
        {
            std::unique_ptr<CivetServer> server;
            std::unique_ptr<StatusHandler> statusHandler;
            std::unique_ptr<StreamHandler> streamHandler;
            {
                std::scoped_lock stateLock(stateMutex_);
                server = std::move(server_);
                statusHandler = std::move(statusHandler_);
                streamHandler = std::move(streamHandler_);
            }
            stopAllViewers();
            if (server) {
                server->close();
            }
            streamHandler.reset();
            statusHandler.reset();
            server.reset();
            if (libraryAcquired_) {
                releaseCivetLibrary();
                libraryAcquired_ = false;
            }
        }

        WebRadarConfig config_;
        mutable std::mutex lifecycleOperationMutex_;
        mutable std::mutex stateMutex_;
        std::atomic<WebRadarServiceState> state_{
            WebRadarServiceState::stopped};
        std::atomic<bool> acceptingViewers_{false};
        std::string lastError_;
        bool libraryAcquired_ = false;

        std::unique_ptr<CivetServer> server_;
        std::unique_ptr<StatusHandler> statusHandler_;
        std::unique_ptr<StreamHandler> streamHandler_;

        mutable std::mutex viewersMutex_;
        std::unordered_map<
            const struct mg_connection*,
            std::shared_ptr<Viewer>> viewers_;
        std::atomic<std::size_t> viewerCount_{0};
        std::atomic<std::uint64_t> publishedFrames_{0};
        std::atomic<std::uint64_t> sentFrames_{0};
        std::atomic<std::uint64_t> replacedFrames_{0};
        std::atomic<std::uint64_t> publishedBytes_{0};
        std::atomic<std::uint64_t> maximumSendLatencyMicroseconds_{0};

        std::mutex snapshotMutex_;
        Snapshot latestSnapshot_;
        std::uint64_t publishedGeneration_ = 0;
    };

    WebRadarService::WebRadarService(WebRadarConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    WebRadarService::~WebRadarService() = default;

    bool WebRadarService::start() noexcept
    {
        return impl_->start();
    }

    void WebRadarService::stop() noexcept
    {
        impl_->stop();
    }

    void WebRadarService::publish(
        std::shared_ptr<const std::string> absoluteState)
    {
        impl_->publish(std::move(absoluteState));
    }

    bool WebRadarService::isRunning() const noexcept
    {
        return impl_->isRunning();
    }

    std::size_t WebRadarService::viewerCount() const noexcept
    {
        return impl_->viewerCount();
    }

    WebRadarStatus WebRadarService::status() const
    {
        return impl_->status();
    }

    std::string WebRadarService::validateConfig(
        const WebRadarConfig& config)
    {
        if (config.bindAddress.empty() || config.bindAddress.size() > 255) {
            return "Bind address must contain between 1 and 255 characters";
        }
        if (std::any_of(
                config.bindAddress.begin(),
                config.bindAddress.end(),
                [](const unsigned char value) {
                    return value <= 0x20 || value == ',' || value == '/';
                })) {
            return "Bind address contains an unsupported character";
        }
        if ((config.bindAddress.front() == '[')
            != (config.bindAddress.back() == ']')) {
            return "IPv6 bind addresses must use either no brackets or a "
                   "matching bracket pair";
        }
        if (config.port == 0) {
            return "Port must be between 1 and 65535";
        }
        if (config.documentRoot.empty()) {
            return "Web Radar document root must not be empty";
        }
        std::error_code filesystemError;
        if (!std::filesystem::is_directory(
                std::filesystem::path(config.documentRoot),
                filesystemError)) {
            return filesystemError
                ? "Could not inspect Web Radar document root: "
                    + filesystemError.message()
                : "Web Radar document root is not a directory";
        }
        if (config.token.size() < 16 || config.token.size() > 128) {
            return "Web Radar token must contain between 16 and 128 "
                   "characters";
        }
        if (!isUrlSafeToken(config.token)) {
            return "Web Radar token must contain only URL-safe letters, "
                   "digits, '_' or '-'";
        }
        if (config.maxViewers == 0 || config.maxViewers > 64) {
            return "Maximum viewer count must be between 1 and 64";
        }
        if (config.workerThreads < config.maxViewers + 4
            || config.workerThreads > 128) {
            return "CivetWeb worker thread count must be at least maximum "
                   "viewers + 4, and no greater than 128";
        }
        if (config.requestTimeoutMilliseconds < 250
            || config.requestTimeoutMilliseconds > 30000) {
            return "Request timeout must be between 250 and 30000 ms";
        }
        if (config.websocketTimeoutMilliseconds < 1000
            || config.websocketTimeoutMilliseconds > 300000) {
            return "WebSocket timeout must be between 1000 and 300000 ms";
        }
        return {};
    }
}
