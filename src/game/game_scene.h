#pragma once
#include "core/scene.h"
#include "game/battle.h"
#include "game/world.h"
#include "input/controls.h"
#include "render/renderer2d.h"
#include "resource/raw_texture.h"

#include "compiled/warning_off.h"
#include <future>
#include <string>
#include <vector>
#include "compiled/warning_on.h"

namespace crittercap
{
enum class Phase : UInt8
{
    TITLE,
    PLAY,
    DIALOG,
    ITEM_GET,
    FADE_OUT,
    FADE_IN,
    SHRINK,
    GROW,
    BATTLE_IN,
    BATTLE,
    BATTLE_OUT,
    STARTER,
    CHOICE,
    MENU,
    ENDING
};

class GameScene final : public core::Scene
{
  public:
    explicit GameScene(Renderer2D *renderer);
    void Update(const double elapsed_seconds) override;
    void Render() override;

  private:
    void Tick();
    void NewGame();
    void HandleRequest();
    void OpenDialog(const std::vector<std::string> &pages);
    void AdvanceDialog();
    void FinishDialog();
    void StartItemGet(const std::string &item);
    void StartWarp(const std::string &map, const std::string &spawn);
    void StartAction(const Request &request);
    void StartBattle(const Request &request);
    void FinishBattle();
    void UpdateMusic();
    void UpdateMenu();
    void RenderDialog();
    void RenderTitle();
    void RenderEnding();
    void RenderMenu();
    void RenderStarter();
    void RenderChoice();

    Renderer2D *renderer = nullptr;
    World world;
    Battle battle;
    BattleResult battleResult = BattleResult::NONE;
    Request battleRequest;
    Controls controls;
    Phase phase = Phase::TITLE;
    Double accumulator = 0.0;
    Float phaseTime = 0.0f;
    Float brightness = 1.0f;
    Float playTime = 0.0f;

    // Dialog state.
    std::vector<std::string> pages;
    size_t page = 0;
    Float typed = 0.0f;

    // What happens once the current dialog closes.
    std::string pendingItem;
    Bool pendingShrink = false;
    Bool pendingEnding = false;
    Request pendingAction;
    std::string warpMap;
    std::string warpSpawn;
    std::string currentItem;

    // Starter pick, yes/no questions and the pause menu.
    Int32 starterCursor = 0;
    std::string choiceQuestion;
    Int32 choiceCursor = 0;
    Int32 menuTab = 0;
    Int32 menuCursor = 0;
    std::string afterStarterItem;

    // Test hook: CRITTERCAP_INPUT="right:60 z:1 wait:30" drives the controls.
    struct ScriptStep
    {
        std::string buttons;
        Int32 frames = 0;
    };
    std::vector<ScriptStep> script;
    size_t scriptIndex = 0;
    Int32 scriptFrame = 0;
    void ApplyScript();

    // Test hook: CRITTERCAP_SHOTS="120,300" saves screenshots at those ticks, then quits.
    std::vector<Int32> shotTicks;
    std::vector<std::pair<std::string, std::future<resource::RawTexture>>> pendingShots;
    Int32 totalTicks = 0;
    void HandleShots();
};
} // namespace crittercap
