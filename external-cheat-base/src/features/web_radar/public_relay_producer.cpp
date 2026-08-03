#include "public_relay_producer.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace web_radar
{
    namespace
    {
        class InternetHandle final
        {
        public:
            InternetHandle() noexcept = default;

            explicit InternetHandle(HINTERNET value) noexcept
                : value_(value)
            {
            }

            ~InternetHandle()
            {
                reset();
            }

            InternetHandle(const InternetHandle&) = delete;
            InternetHandle& operator=(const InternetHandle&) = delete;

            InternetHandle(InternetHandle&& other) noexcept
                : value_(std::exchange(other.value_, nullptr))
            {
            }

            InternetHandle& operator=(InternetHandle&& other) noexcept
            {
                if (this != &other) {
                    reset(std::exchange(other.value_, nullptr));
                }
                return *this;
            }

            [[nodiscard]] HINTERNET get() const noexcept
            {
                return value_;
            }

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return value_ != nullptr;
            }

            void reset(HINTERNET value = nullptr) noexcept
            {
                if (value_ != nullptr) {
                    WinHttpCloseHandle(value_);
                }
                value_ = value;
            }

        private:
            HINTERNET value_ = nullptr;
        };

        struct RelayConnection
        {
            InternetHandle session;
            InternetHandle connection;
            InternetHandle socket;
        };

        struct ConnectionAttempt
        {
            RelayConnection connection;
            std::string error;
            bool permanentFailure = false;

            [[nodiscard]] explicit operator bool() const noexcept
            {
                return static_cast<bool>(connection.socket);
            }
        };

        [[nodiscard]] std::wstring asciiToWide(const std::string_view value)
        {
            return std::wstring(value.begin(), value.end());
        }

        void securelyErase(std::wstring& value) noexcept
        {
            if (!value.empty()) {
                SecureZeroMemory(
                    value.data(),
                    value.size() * sizeof(std::wstring::value_type));
            }
            value.clear();
        }

        void securelyErase(std::string& value) noexcept
        {
            if (!value.empty()) {
                SecureZeroMemory(value.data(), value.size());
            }
            value.clear();
        }

        [[nodiscard]] std::string winHttpFailure(
            const char* operation,
            const DWORD error)
        {
            if (error == ERROR_WINHTTP_TIMEOUT) {
                return std::string(operation) + " timed out.";
            }
            if (error == ERROR_WINHTTP_SECURE_FAILURE) {
                return "Relay TLS certificate validation failed.";
            }
            if (error == ERROR_WINHTTP_CANNOT_CONNECT) {
                return "Relay connection could not be established.";
            }
            if (error == ERROR_WINHTTP_NAME_NOT_RESOLVED) {
                return "Relay host name could not be resolved.";
            }
            return std::string(operation) + " failed (WinHTTP " +
                std::to_string(error) + ").";
        }

        [[nodiscard]] std::string httpFailure(const DWORD statusCode)
        {
            switch (statusCode) {
            case 400:
                return "Relay rejected the producer handshake (HTTP 400).";
            case 401:
                return "Relay rejected the producer credentials (HTTP 401).";
            case 403:
                return "Relay denied producer access to this room (HTTP 403).";
            case 404:
                return "Relay publish endpoint was not found (HTTP 404).";
            case 409:
                return "Another producer is already active for this room (HTTP 409).";
            case 429:
                return "Relay producer rate limit reached (HTTP 429).";
            default:
                return "Relay WebSocket handshake failed (HTTP " +
                    std::to_string(statusCode) + ").";
            }
        }

        [[nodiscard]] bool permanentHttpFailure(const DWORD statusCode) noexcept
        {
            return statusCode >= 400 && statusCode < 500 &&
                statusCode != 408 && statusCode != 409 &&
                statusCode != 425 && statusCode != 429;
        }
    }

    class PublicRelayProducer::Impl final
    {
    public:
        explicit Impl(PublicRelayConfig config)
            : config_(std::move(config))
        {
        }

        ~Impl()
        {
            stop();
            securelyErase(config_.token);
        }

        [[nodiscard]] bool start() noexcept
        {
            std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (started_) {
                    return status_.state != PublicRelayState::failed;
                }
                const std::string validationError =
                    validatePublicRelayConfig(config_);
                if (!validationError.empty()) {
                    status_.state = PublicRelayState::failed;
                    status_.lastError = validationError;
                    return false;
                }
                stopping_ = false;
                started_ = true;
                status_.state = PublicRelayState::connecting;
                status_.lastError.clear();
            }

            try {
                worker_ = std::thread([this] {
                    workerMain();
                });
            } catch (...) {
                std::lock_guard<std::mutex> lock(mutex_);
                started_ = false;
                status_.state = PublicRelayState::failed;
                status_.lastError =
                    "Unable to start the Relay network worker.";
                return false;
            }
            return true;
        }

        void stop() noexcept
        {
            std::lock_guard<std::mutex> lifecycleLock(lifecycleMutex_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!started_) {
                    status_.state = PublicRelayState::disabled;
                    status_.lastError.clear();
                    return;
                }
                stopping_ = true;
            }
            changed_.notify_all();

            // Only the worker owns WinHTTP handles. Microsoft explicitly warns
            // against closing a synchronous HINTERNET from another thread, and
            // WinHttpWebSocketSend has no documented hard cancellation
            // deadline. Runtime callers therefore execute this join only on a
            // detached reaper, never on the UI or sampling thread.
            if (worker_.joinable()) {
                worker_.join();
            }

            std::lock_guard<std::mutex> lock(mutex_);
            started_ = false;
            stopping_ = false;
            pending_.reset();
            pendingGeneration_ = 0;
            consumedGeneration_ = 0;
            sentGeneration_ = 0;
            status_.state = PublicRelayState::disabled;
            status_.lastError.clear();
        }

        void requestStop() noexcept
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!started_ || stopping_) {
                    return;
                }
                stopping_ = true;
                status_.state = PublicRelayState::retiring;
                status_.lastError.clear();
            }
            changed_.notify_all();
        }

        void publish(std::shared_ptr<const std::string> frame)
        {
            if (!frame || frame->empty()) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!started_ || stopping_) {
                    return;
                }
                if (pending_ &&
                    pendingGeneration_ != consumedGeneration_) {
                    ++status_.replacedFrames;
                }
                pending_ = std::move(frame);
                ++pendingGeneration_;
                if (pendingGeneration_ == 0) {
                    ++pendingGeneration_;
                }
            }
            changed_.notify_one();
        }

        [[nodiscard]] PublicRelayStatus status() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return status_;
        }

    private:
        [[nodiscard]] ConnectionAttempt connect()
        {
            std::string parseError;
            const auto endpoint = parsePublicRelayEndpoint(
                config_.endpointUrl,
                &parseError);
            if (!endpoint) {
                return ConnectionAttempt{{}, std::move(parseError), true};
            }

            RelayConnection result;
            result.session.reset(WinHttpOpen(
                L"CS2-Web-Radar-Producer/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                WINHTTP_NO_PROXY_NAME,
                WINHTTP_NO_PROXY_BYPASS,
                0));
            if (!result.session) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay session initialization", GetLastError()),
                    false};
            }
            if (!WinHttpSetTimeouts(
                    result.session.get(),
                    static_cast<int>(config_.resolveTimeoutMilliseconds),
                    static_cast<int>(config_.connectTimeoutMilliseconds),
                    static_cast<int>(config_.sendTimeoutMilliseconds),
                    static_cast<int>(config_.receiveTimeoutMilliseconds))) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay timeout configuration", GetLastError()),
                    true};
            }

            DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
            secureProtocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
            if (!WinHttpSetOption(
                    result.session.get(),
                    WINHTTP_OPTION_SECURE_PROTOCOLS,
                    &secureProtocols,
                    sizeof(secureProtocols))) {
                secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
                if (!WinHttpSetOption(
                        result.session.get(),
                        WINHTTP_OPTION_SECURE_PROTOCOLS,
                        &secureProtocols,
                        sizeof(secureProtocols))) {
                    return ConnectionAttempt{
                        {},
                        winHttpFailure("Relay TLS configuration", GetLastError()),
                        true};
                }
            }

            const std::wstring host = asciiToWide(endpoint->host);
            result.connection.reset(WinHttpConnect(
                result.session.get(),
                host.c_str(),
                endpoint->port,
                0));
            if (!result.connection) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay connection", GetLastError()),
                    false};
            }

            const std::wstring path = asciiToWide(endpoint->path);
            InternetHandle request(WinHttpOpenRequest(
                result.connection.get(),
                L"GET",
                path.c_str(),
                nullptr,
                WINHTTP_NO_REFERER,
                WINHTTP_DEFAULT_ACCEPT_TYPES,
                WINHTTP_FLAG_SECURE));
            if (!request) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay request creation", GetLastError()),
                    false};
            }

            DWORD disabledFeatures = WINHTTP_DISABLE_REDIRECTS;
            if (!WinHttpSetOption(
                    request.get(),
                    WINHTTP_OPTION_DISABLE_FEATURE,
                    &disabledFeatures,
                    sizeof(disabledFeatures))) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay redirect protection", GetLastError()),
                    true};
            }
            if (!WinHttpSetOption(
                    request.get(),
                    WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                    nullptr,
                    0)) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay WebSocket upgrade", GetLastError()),
                    true};
            }

            std::wstring authorization = L"Authorization: Bearer ";
            authorization += asciiToWide(config_.token);
            const std::wstring roomHeader =
                L"X-Radar-Room: " + asciiToWide(config_.room);
            const bool authorizationAdded = WinHttpAddRequestHeaders(
                request.get(),
                authorization.c_str(),
                static_cast<DWORD>(-1L),
                WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) != FALSE;
            securelyErase(authorization);
            if (!authorizationAdded ||
                !WinHttpAddRequestHeaders(
                    request.get(),
                    roomHeader.c_str(),
                    static_cast<DWORD>(-1L),
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay authentication header", GetLastError()),
                    true};
            }

            if (!WinHttpSendRequest(
                    request.get(),
                    WINHTTP_NO_ADDITIONAL_HEADERS,
                    0,
                    WINHTTP_NO_REQUEST_DATA,
                    0,
                    0,
                    0)) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay handshake send", GetLastError()),
                    false};
            }
            if (!WinHttpReceiveResponse(request.get(), nullptr)) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay handshake response", GetLastError()),
                    false};
            }

            DWORD statusCode = 0;
            DWORD statusCodeSize = sizeof(statusCode);
            if (!WinHttpQueryHeaders(
                    request.get(),
                    WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                    WINHTTP_HEADER_NAME_BY_INDEX,
                    &statusCode,
                    &statusCodeSize,
                    WINHTTP_NO_HEADER_INDEX)) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay handshake status", GetLastError()),
                    false};
            }
            if (statusCode != 101) {
                return ConnectionAttempt{
                    {},
                    httpFailure(statusCode),
                    permanentHttpFailure(statusCode)};
            }

            result.socket.reset(WinHttpWebSocketCompleteUpgrade(
                request.get(),
                0));
            if (!result.socket) {
                return ConnectionAttempt{
                    {},
                    winHttpFailure("Relay WebSocket completion", GetLastError()),
                    false};
            }
            return ConnectionAttempt{std::move(result), {}, false};
        }

        void workerMain() noexcept
        {
            std::uint32_t failureCount = 0;
            std::uint32_t entropy = static_cast<std::uint32_t>(
                GetTickCount64() ^
                reinterpret_cast<std::uintptr_t>(this));
            bool attemptedConnection = false;

            while (!shouldStop()) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_.state = PublicRelayState::connecting;
                    if (attemptedConnection) {
                        ++status_.reconnects;
                    }
                }
                attemptedConnection = true;

                ConnectionAttempt attempt = connect();
                if (!attempt) {
                    if (attempt.permanentFailure) {
                        std::lock_guard<std::mutex> lock(mutex_);
                        status_.state = PublicRelayState::failed;
                        status_.lastError = std::move(attempt.error);
                        return;
                    }
                    if (!waitAfterFailure(
                            std::move(attempt.error),
                            failureCount,
                            nextEntropy(entropy))) {
                        return;
                    }
                    failureCount = std::min(failureCount + 1U, 31U);
                    continue;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_.state = PublicRelayState::connected;
                    status_.lastError.clear();
                }

                std::string sendError;
                std::uint64_t framesSentOnConnection = 0;
                if (sendFrames(
                        attempt.connection.socket.get(),
                        sendError,
                        framesSentOnConnection)) {
                    return;
                }
                attempt.connection = RelayConnection{};
                // A connection that carried at least one second of the normal
                // 20 Hz stream is considered healthy. Immediate upgrade/close
                // loops retain their failure count and therefore continue the
                // exponential backoff instead of reconnecting every second.
                if (framesSentOnConnection >= 20) {
                    failureCount = 0;
                }
                if (!waitAfterFailure(
                        std::move(sendError),
                        failureCount,
                        nextEntropy(entropy))) {
                    return;
                }
                failureCount = std::min(failureCount + 1U, 31U);
            }
        }

        [[nodiscard]] bool sendFrames(
            const HINTERNET socket,
            std::string& error,
            std::uint64_t& framesSentOnConnection)
        {
            while (true) {
                std::shared_ptr<const std::string> frame;
                std::uint64_t generation = 0;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    changed_.wait(lock, [this] {
                        return stopping_ ||
                            (pending_ &&
                                pendingGeneration_ != sentGeneration_);
                    });
                    if (stopping_) {
                        return true;
                    }
                    if (!pending_) {
                        continue;
                    }
                    frame = pending_;
                    generation = pendingGeneration_;
                    consumedGeneration_ = generation;
                }

                if (frame->size() >
                    static_cast<std::size_t>(std::numeric_limits<DWORD>::max())) {
                    error = "Relay snapshot exceeds the WinHTTP frame limit.";
                    return false;
                }
                const DWORD result = WinHttpWebSocketSend(
                    socket,
                    WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                    const_cast<char*>(frame->data()),
                    static_cast<DWORD>(frame->size()));
                if (result != NO_ERROR) {
                    error = winHttpFailure("Relay WebSocket send", result);
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++status_.framesSent;
                    sentGeneration_ = generation;
                }
                ++framesSentOnConnection;
            }
        }

        [[nodiscard]] bool waitAfterFailure(
            std::string error,
            const std::uint32_t failureCount,
            const std::uint32_t entropy)
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stopping_) {
                return false;
            }
            status_.state = PublicRelayState::backoff;
            status_.lastError = std::move(error);
            changed_.wait_for(
                lock,
                publicRelayBackoffDelay(failureCount, entropy),
                [this] {
                    return stopping_;
                });
            return !stopping_;
        }

        [[nodiscard]] bool shouldStop() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return stopping_;
        }

        [[nodiscard]] static std::uint32_t nextEntropy(
            std::uint32_t& state) noexcept
        {
            state ^= state << 13U;
            state ^= state >> 17U;
            state ^= state << 5U;
            return state;
        }

        PublicRelayConfig config_;
        mutable std::mutex mutex_;
        std::mutex lifecycleMutex_;
        std::condition_variable changed_;
        std::thread worker_;
        bool started_ = false;
        bool stopping_ = false;
        std::shared_ptr<const std::string> pending_;
        std::uint64_t pendingGeneration_ = 0;
        std::uint64_t consumedGeneration_ = 0;
        std::uint64_t sentGeneration_ = 0;
        PublicRelayStatus status_;
    };

    PublicRelayProducer::PublicRelayProducer(PublicRelayConfig config)
        : impl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    PublicRelayProducer::~PublicRelayProducer() = default;

    bool PublicRelayProducer::start() noexcept
    {
        return impl_->start();
    }

    void PublicRelayProducer::requestStop() noexcept
    {
        impl_->requestStop();
    }

    void PublicRelayProducer::stop() noexcept
    {
        impl_->stop();
    }

    void PublicRelayProducer::publish(
        std::shared_ptr<const std::string> absoluteState)
    {
        impl_->publish(std::move(absoluteState));
    }

    PublicRelayStatus PublicRelayProducer::status() const
    {
        return impl_->status();
    }
}
