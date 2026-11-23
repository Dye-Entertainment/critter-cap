#include "core/entry.h"
#include "critter_cap.h"

int core::entry()
{
    CritterCap application;
    core::WindowInfo window_info;
    window_info.windowName = "Critter Cap";
    // The game renders at the GBA-like 240x160 and is scaled 4x.
    window_info.windowSize = {960, 640};
    if (application.Initialize(window_info))
    {
        application.Update();
    }
    application.Destroy();
    return 0;
}
