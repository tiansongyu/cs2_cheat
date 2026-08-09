#include "core/runtime_timing.hpp"
#include "core/performance_metrics.hpp"

#include <cassert>
#include <chrono>

int main()
{
    using runtime_timing::SamplingDemand;
    using runtime_timing::dataSamplingRate;

    assert(dataSamplingRate(144, SamplingDemand{}) == 20);
    assert(dataSamplingRate(144, SamplingDemand{false, false, true}) == 60);

    const SamplingDemand visuals{false, true, false};
    assert(dataSamplingRate(0, visuals) == 60);
    assert(dataSamplingRate(60, visuals) == 60);
    assert(dataSamplingRate(144, visuals) == 144);
    assert(dataSamplingRate(240, visuals) == 240);
    assert(dataSamplingRate(360, visuals) == 240);

    const SamplingDemand input{true, false, false};
    assert(dataSamplingRate(60, input) == 240);
    assert(dataSamplingRate(360, input) == 240);

    assert(
        runtime_timing::intervalForRate(240) ==
        std::chrono::nanoseconds(4'166'666));
    assert(
        runtime_timing::intervalForRate(20) ==
        std::chrono::milliseconds(50));

    performance_metrics::DurationHistogram histogram;
    histogram.record(std::chrono::microseconds(100));
    histogram.record(std::chrono::microseconds(200));
    histogram.record(std::chrono::microseconds(4'000));
    histogram.record(std::chrono::microseconds(20'000));
    const auto metrics = histogram.snapshot();
    assert(metrics.samples == 4);
    assert(metrics.averageMilliseconds == 6.075);
    assert(metrics.p50Milliseconds == 0.25);
    assert(metrics.p95Milliseconds == 24.0);
    assert(metrics.p99Milliseconds == 24.0);
    assert(metrics.maximumMilliseconds == 20.0);

    histogram.reset();
    assert(histogram.snapshot().samples == 0);
    return 0;
}
