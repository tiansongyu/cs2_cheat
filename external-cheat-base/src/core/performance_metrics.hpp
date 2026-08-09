#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace performance_metrics
{
    struct DurationSnapshot
    {
        std::uint64_t samples{};
        double averageMilliseconds{};
        double p50Milliseconds{};
        double p95Milliseconds{};
        double p99Milliseconds{};
        double maximumMilliseconds{};
    };

    class DurationHistogram final
    {
    public:
        template <typename Rep, typename Period>
        void record(const std::chrono::duration<Rep, Period> duration) noexcept
        {
            const auto measured = std::chrono::duration_cast<
                std::chrono::microseconds>(duration).count();
            const std::uint64_t microseconds = measured <= 0
                ? 0
                : static_cast<std::uint64_t>(measured);

            samples_.fetch_add(1, std::memory_order_relaxed);
            totalMicroseconds_.fetch_add(
                microseconds,
                std::memory_order_relaxed);

            std::uint64_t maximum = maximumMicroseconds_.load(
                std::memory_order_relaxed);
            while (microseconds > maximum &&
                   !maximumMicroseconds_.compare_exchange_weak(
                       maximum,
                       microseconds,
                       std::memory_order_relaxed,
                       std::memory_order_relaxed)) {
            }

            buckets_[bucketIndex(microseconds)].fetch_add(
                1,
                std::memory_order_relaxed);
        }

        [[nodiscard]] DurationSnapshot snapshot() const noexcept
        {
            DurationSnapshot result;
            result.samples = samples_.load(std::memory_order_relaxed);
            if (result.samples == 0) {
                return result;
            }

            result.averageMilliseconds =
                static_cast<double>(totalMicroseconds_.load(
                    std::memory_order_relaxed)) /
                static_cast<double>(result.samples) /
                1000.0;
            result.p50Milliseconds = percentileMilliseconds(
                result.samples,
                50);
            result.p95Milliseconds = percentileMilliseconds(
                result.samples,
                95);
            result.p99Milliseconds = percentileMilliseconds(
                result.samples,
                99);
            result.maximumMilliseconds =
                static_cast<double>(maximumMicroseconds_.load(
                    std::memory_order_relaxed)) /
                1000.0;
            return result;
        }

        void reset() noexcept
        {
            samples_.store(0, std::memory_order_relaxed);
            totalMicroseconds_.store(0, std::memory_order_relaxed);
            maximumMicroseconds_.store(0, std::memory_order_relaxed);
            for (auto& bucket : buckets_) {
                bucket.store(0, std::memory_order_relaxed);
            }
        }

    private:
        inline static constexpr std::array<std::uint64_t, 15>
            upperBoundsMicroseconds{
                50, 100, 250, 500, 1'000,
                2'000, 4'000, 8'000, 12'000, 16'000,
                24'000, 33'000, 50'000, 100'000,
                std::numeric_limits<std::uint64_t>::max()
            };

        [[nodiscard]] static constexpr std::size_t bucketIndex(
            const std::uint64_t microseconds) noexcept
        {
            for (std::size_t index = 0;
                 index < upperBoundsMicroseconds.size();
                 ++index) {
                if (microseconds <= upperBoundsMicroseconds[index]) {
                    return index;
                }
            }
            return upperBoundsMicroseconds.size() - 1;
        }

        [[nodiscard]] double percentileMilliseconds(
            const std::uint64_t samples,
            const std::uint64_t percentile) const noexcept
        {
            const std::uint64_t target =
                (samples * percentile + 99) / 100;
            std::uint64_t accumulated = 0;
            for (std::size_t index = 0;
                 index < upperBoundsMicroseconds.size();
                 ++index) {
                accumulated += buckets_[index].load(
                    std::memory_order_relaxed);
                if (accumulated >= target) {
                    const std::uint64_t upper =
                        upperBoundsMicroseconds[index];
                    if (upper == std::numeric_limits<std::uint64_t>::max()) {
                        return static_cast<double>(maximumMicroseconds_.load(
                            std::memory_order_relaxed)) /
                            1000.0;
                    }
                    return static_cast<double>(upper) / 1000.0;
                }
            }
            return 0.0;
        }

        std::atomic<std::uint64_t> samples_{0};
        std::atomic<std::uint64_t> totalMicroseconds_{0};
        std::atomic<std::uint64_t> maximumMicroseconds_{0};
        std::array<std::atomic<std::uint64_t>,
            upperBoundsMicroseconds.size()> buckets_{};
    };

    inline DurationHistogram samplingDuration;
    inline DurationHistogram serializationDuration;
    inline DurationHistogram renderCpuDuration;
    inline std::atomic<std::uint64_t> missedSamplingDeadlines{0};
    inline std::atomic<std::uint64_t> missedRenderDeadlines{0};
    inline std::atomic<int> samplingRateHz{0};
    inline std::atomic<int> radarRateHz{0};

    inline void resetSession() noexcept
    {
        samplingDuration.reset();
        serializationDuration.reset();
        renderCpuDuration.reset();
        missedSamplingDeadlines.store(0, std::memory_order_relaxed);
        missedRenderDeadlines.store(0, std::memory_order_relaxed);
        samplingRateHz.store(0, std::memory_order_relaxed);
        radarRateHz.store(0, std::memory_order_relaxed);
    }
}
