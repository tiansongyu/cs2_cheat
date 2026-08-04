#pragma once

#include "core/game/game_snapshot.hpp"

namespace local_fixed_radar
{
    struct RenderConfig
    {
        float anchorX{ 0.02f };
        float anchorY{ 0.08f };
        float sizeFraction{ 0.32f };
        float markerSize{ 12.0f };
        bool showNames{ true };
    };

    // Must be called from the SDL/ImGui render thread. The renderer consumes
    // the same immutable snapshot as Web Radar and never reads game memory.
    void render(
        const game::GameSnapshot& snapshot,
        const RenderConfig& config);

    // Release the current SDL texture before the owning SDL_Renderer is
    // destroyed. It is safe to call when no map has been loaded.
    void reset() noexcept;
}
