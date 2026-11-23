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
    crittercap::InitializeAudio();
    core::SceneManager::GetInstance()->AddScene("Critter Cap", new crittercap::GameScene(&renderer));
    return true;
}

Bool CritterCap::UpdateLoop(const Double)
{
    return true;
}

void CritterCap::Destroy()
{
    crittercap::ShutdownAudio();
    renderer.Shutdown();
    GameApplication::Destroy();
}
