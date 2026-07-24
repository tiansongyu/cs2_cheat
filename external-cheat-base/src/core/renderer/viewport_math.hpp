#pragma once

#include <algorithm>
#include <cmath>

namespace viewport_math
{
    struct Viewport
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
    };

    inline bool same(const Viewport& lhs, const Viewport& rhs)
    {
        return lhs.x == rhs.x &&
            lhs.y == rhs.y &&
            lhs.width == rhs.width &&
            lhs.height == rhs.height;
    }

    inline Viewport forAspect(
        int clientWidth,
        int clientHeight,
        float targetAspect)
    {
        Viewport result{ 0, 0, clientWidth, clientHeight };
        if (clientWidth <= 0 ||
            clientHeight <= 0 ||
            !std::isfinite(targetAspect) ||
            targetAspect <= 0.0f) {
            return result;
        }

        const float clientAspect =
            static_cast<float>(clientWidth) /
            static_cast<float>(clientHeight);
        if (clientAspect > targetAspect) {
            result.width = static_cast<int>(
                std::lround(
                    static_cast<float>(clientHeight) * targetAspect));
            result.x = (clientWidth - result.width) / 2;
        } else if (clientAspect < targetAspect) {
            result.height = static_cast<int>(
                std::lround(
                    static_cast<float>(clientWidth) / targetAspect));
            result.y = (clientHeight - result.height) / 2;
        }
        return result;
    }

    inline Viewport forMode(
        int mode,
        int clientWidth,
        int clientHeight)
    {
        switch (mode) {
            case 2:
                return forAspect(
                    clientWidth,
                    clientHeight,
                    4.0f / 3.0f);
            case 3:
                return forAspect(
                    clientWidth,
                    clientHeight,
                    16.0f / 10.0f);
            default:
                return Viewport{
                    0,
                    0,
                    clientWidth,
                    clientHeight
                };
        }
    }

    inline Viewport remap(
        const Viewport& viewport,
        int oldClientWidth,
        int oldClientHeight,
        int newClientWidth,
        int newClientHeight)
    {
        if (oldClientWidth <= 0 ||
            oldClientHeight <= 0 ||
            newClientWidth <= 0 ||
            newClientHeight <= 0 ||
            viewport.width <= 0 ||
            viewport.height <= 0) {
            return Viewport{
                0,
                0,
                newClientWidth,
                newClientHeight
            };
        }

        const auto scaleCoordinate = [](
            int value,
            int oldExtent,
            int newExtent) {
            return static_cast<int>(std::lround(
                static_cast<double>(value) *
                static_cast<double>(newExtent) /
                static_cast<double>(oldExtent)));
        };

        Viewport result{
            scaleCoordinate(
                viewport.x,
                oldClientWidth,
                newClientWidth),
            scaleCoordinate(
                viewport.y,
                oldClientHeight,
                newClientHeight),
            scaleCoordinate(
                viewport.width,
                oldClientWidth,
                newClientWidth),
            scaleCoordinate(
                viewport.height,
                oldClientHeight,
                newClientHeight)
        };
        result.x = std::clamp(
            result.x,
            0,
            newClientWidth);
        result.y = std::clamp(
            result.y,
            0,
            newClientHeight);
        result.width = std::clamp(
            result.width,
            0,
            newClientWidth - result.x);
        result.height = std::clamp(
            result.height,
            0,
            newClientHeight - result.y);
        return result;
    }

    inline bool validProjectedBox(
        float height,
        float viewportHeight)
    {
        return std::isfinite(height) &&
            std::isfinite(viewportHeight) &&
            height >= 1.0f &&
            viewportHeight > 0.0f &&
            height <= viewportHeight * 2.0f;
    }
}
