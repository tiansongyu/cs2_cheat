#include "core/runtime_timing.hpp"

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
    return 0;
}
