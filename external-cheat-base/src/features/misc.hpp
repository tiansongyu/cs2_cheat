#pragma once

namespace misc
{
    // Per-frame tick: handles Auto Stop (counter-strafe) and Bunny Hop.
    // Call from the data thread after esp::updateEntities().
    void update();

    // Release any keys this module is currently holding down.
    // Call when the program is shutting down or features are disabled.
    void releaseAll();
}
