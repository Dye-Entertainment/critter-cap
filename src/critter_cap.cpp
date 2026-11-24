#include "critter_cap.h"

#include "audio/sound.h"
#include "core/scene_manager.h"
#include "game/game_scene.h"
#include "utils/logger.h"

Bool CritterCap::DerrivedInitialize()
{
    if (!renderer.Initialize())
    {
        LOGERROR("Critter Cap could not load its graphics");
        return false;
    }
    if (!ui.Initialize(&renderer))
    {
        LOGERROR("Critter Cap could not load its UI");
        return false;
    }
    crittercap::InitializeAudio();
    core::SceneManager::GetInstance()->AddScene("Critter Cap", new crittercap::GameScene(&renderer, &ui));
    return true;
}

Bool CritterCap::UpdateLoop(const Double)
{
    return true;
}

void CritterCap::Destroy()
{
    crittercap::ShutdownAudio();
    ui.Shutdown();
    renderer.Shutdown();
    GameApplication::Destroy();
}
