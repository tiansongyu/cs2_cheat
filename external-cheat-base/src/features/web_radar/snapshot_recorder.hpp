#pragma once

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace web_radar
{
    struct SnapshotRecorderStatus
    {
        bool recording = false;
        std::uint64_t framesWritten = 0;
        std::uint64_t replacedFrames = 0;
        std::uint64_t bytesWritten = 0;
        std::string path;
        std::string lastError;
    };

    class SnapshotRecorder final
    {
    public:
        explicit SnapshotRecorder(std::filesystem::path directory)
            : directory_(std::move(directory))
        {
        }

        ~SnapshotRecorder()
        {
            stop();
        }

        SnapshotRecorder(const SnapshotRecorder&) = delete;
        SnapshotRecorder& operator=(const SnapshotRecorder&) = delete;

        void sync(const bool enabled)
        {
            if (enabled) {
                start();
            } else {
                stop();
            }
        }

        void publish(std::shared_ptr<const std::string> frame)
        {
            if (!frame || frame->empty()) {
                return;
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!running_ || stopping_) {
                    return;
                }
                if (pending_) {
                    ++status_.replacedFrames;
                }
                pending_ = std::move(frame);
            }
            changed_.notify_one();
        }

        [[nodiscard]] SnapshotRecorderStatus status() const
        {
            std::lock_guard<std::mutex> lock(mutex_);
            return status_;
        }

        void stop() noexcept
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!running_) {
                    return;
                }
                stopping_ = true;
            }
            changed_.notify_one();
            if (worker_.joinable()) {
                worker_.join();
            }
            std::lock_guard<std::mutex> lock(mutex_);
            running_ = false;
            stopping_ = false;
            pending_.reset();
            status_.recording = false;
        }

    private:
        inline static constexpr std::uint64_t maximumBytes =
            UINT64_C(256) * 1024U * 1024U;

        void start()
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (running_) {
                return;
            }

            std::error_code error;
            std::filesystem::create_directories(directory_, error);
            if (error) {
                status_.lastError =
                    "Could not create recording directory: " +
                    error.message();
                return;
            }

            const std::filesystem::path path = directory_ / fileName();
            status_ = {};
            status_.path = path.string();
            status_.recording = true;
            running_ = true;
            stopping_ = false;
            worker_ = std::thread([this, path] { writeLoop(path); });
        }

        [[nodiscard]] static std::string fileName()
        {
            const std::time_t now = std::time(nullptr);
            std::tm local{};
#ifdef _WIN32
            localtime_s(&local, &now);
#else
            localtime_r(&now, &local);
#endif
            std::ostringstream name;
            name << "radar-" << std::put_time(&local, "%Y%m%d-%H%M%S")
                 << ".ndjson";
            return name.str();
        }

        void writeLoop(const std::filesystem::path& path) noexcept
        {
            std::ofstream output(path, std::ios::binary | std::ios::trunc);
            if (!output) {
                fail("Could not open the recording file");
                return;
            }

            std::uint32_t framesSinceFlush = 0;
            for (;;) {
                std::shared_ptr<const std::string> frame;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    changed_.wait(lock, [this] {
                        return stopping_ || pending_ != nullptr;
                    });
                    if (stopping_ && !pending_) {
                        break;
                    }
                    frame.swap(pending_);
                }

                const std::uint64_t nextBytes =
                    static_cast<std::uint64_t>(frame->size()) + 1U;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (status_.bytesWritten + nextBytes > maximumBytes) {
                        status_.lastError =
                            "Recording stopped at the 256 MB safety limit";
                        stopping_ = true;
                        status_.recording = false;
                        pending_.reset();
                        break;
                    }
                }

                output.write(frame->data(),
                    static_cast<std::streamsize>(frame->size()));
                output.put('\n');
                if (!output) {
                    fail("Writing the recording file failed");
                    return;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    ++status_.framesWritten;
                    status_.bytesWritten += nextBytes;
                }
                if (++framesSinceFlush >= 20) {
                    output.flush();
                    framesSinceFlush = 0;
                }
            }
            output.flush();
        }

        void fail(std::string message) noexcept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            status_.lastError = std::move(message);
            status_.recording = false;
            stopping_ = true;
            pending_.reset();
        }

        std::filesystem::path directory_;
        mutable std::mutex mutex_;
        std::condition_variable changed_;
        std::shared_ptr<const std::string> pending_;
        SnapshotRecorderStatus status_;
        bool running_ = false;
        bool stopping_ = false;
        std::thread worker_;
    };
}
