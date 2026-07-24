#include "core/renderer/viewport_math.hpp"

#include <cassert>
#include <limits>

int main()
{
    using viewport_math::Viewport;

    const Viewport full =
        viewport_math::forMode(1, 1920, 1080);
    assert(viewport_math::same(
        full,
        Viewport{ 0, 0, 1920, 1080 }));

    const Viewport fourByThree =
        viewport_math::forMode(2, 1920, 1080);
    assert(viewport_math::same(
        fourByThree,
        Viewport{ 240, 0, 1440, 1080 }));

    const Viewport sixteenByTen =
        viewport_math::forMode(3, 1920, 1080);
    assert(viewport_math::same(
        sixteenByTen,
        Viewport{ 96, 0, 1728, 1080 }));

    const Viewport pillarFree =
        viewport_math::forMode(2, 1280, 960);
    assert(viewport_math::same(
        pillarFree,
        Viewport{ 0, 0, 1280, 960 }));

    const Viewport ultrawide =
        viewport_math::forAspect(2560, 1080, 16.0f / 9.0f);
    assert(viewport_math::same(
        ultrawide,
        Viewport{ 320, 0, 1920, 1080 }));

    const Viewport letterboxed =
        viewport_math::forAspect(1920, 1200, 16.0f / 9.0f);
    assert(viewport_math::same(
        letterboxed,
        Viewport{ 0, 60, 1920, 1080 }));

    const Viewport invalidAspect =
        viewport_math::forAspect(1600, 900, 0.0f);
    assert(viewport_math::same(
        invalidAspect,
        Viewport{ 0, 0, 1600, 900 }));

    const Viewport remappedPillarbox =
        viewport_math::remap(
            Viewport{ 240, 0, 1440, 1080 },
            1920,
            1080,
            2560,
            1440);
    assert(viewport_math::same(
        remappedPillarbox,
        Viewport{ 320, 0, 1920, 1440 }));

    const Viewport remappedInvalid =
        viewport_math::remap(
            Viewport{},
            0,
            0,
            1280,
            720);
    assert(viewport_math::same(
        remappedInvalid,
        Viewport{ 0, 0, 1280, 720 }));

    assert(viewport_math::validProjectedBox(1.0f, 1080.0f));
    assert(viewport_math::validProjectedBox(1080.0f, 1080.0f));
    assert(!viewport_math::validProjectedBox(-1.0f, 1080.0f));
    assert(!viewport_math::validProjectedBox(3000.0f, 1080.0f));
    assert(!viewport_math::validProjectedBox(10.0f, 0.0f));
    assert(!viewport_math::validProjectedBox(
        std::numeric_limits<float>::quiet_NaN(),
        1080.0f));
    return 0;
}
