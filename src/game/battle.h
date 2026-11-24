#pragma once
#include "audio/sound.h"
#include "game/world.h"
#include "input/controls.h"
#include "render/renderer2d.h"
#include "ui/game_ui.h"

#include "compiled/warning_off.h"
#include <deque>
#include <string>
#include <vector>
#include "compiled/warning_on.h"

namespace crittercap
{
enum class BattleResult : UInt8
{
    NONE,
    WON,
    LOST,
    RAN,
    CAUGHT
};

// A turn-based critter battle. The party lives in the World; the battle only keeps the
// opposing team and its own presentation state.
class Battle final
{
  public:
    void Start(World &world, const Request &request);
    // Advances one fixed tick; returns the outcome once the battle is over.
    BattleResult Update(World &world, const Controls &controls);
    // Draws the battle scene: background, critters, trainers and the thrown cap.
    void Render(const World &world, Renderer2D &renderer) const;
    // Fills in the battle's info boxes, menus, messages and team screen.
    void FillUi(const World &world, const GameUi &ui, UiState &state) const;

    Bool IsTrainer() const
    {
        return !trainer.empty();
    }
    Bool IsLegend() const
    {
        return legend;
    }
    // The critter that was caught (valid when the result is CAUGHT).
    const Critter &Enemy() const
    {
        return enemies[enemyIndex];
    }

  private:
    enum class StepType : UInt8
    {
        TEXT,
        WAIT,
        ATTACK,
        HIT,
        THROW,
        SHAKE,
        CATCH,
        BREAK_FREE,
        FAINT,
        AFTER_FAINT,
        SEND_OUT,
        RECALL,
        SET_ACTIVE,
        TURN_END,
        END
    };
    struct Step
    {
        StepType type = StepType::TEXT;
        std::string text;
        Int32 side = 0;
        Int32 value = 0;
        Float duration = 0.0f;
        Sfx sfx = Sfx::COUNT;
    };
    enum class Mode : UInt8
    {
        STEPS,
        MENU,
        MOVES,
        TEAM,
        FORCED_TEAM
    };
    enum Side : Int32
    {
        PLAYER = 0,
        ENEMY = 1
    };

    void Text(const std::string &text, Sfx sfx = Sfx::COUNT);
    void Push(StepType type, Int32 side = 0, Int32 value = 0, Float duration = 0.0f);
    void PushFront(std::vector<Step> steps);
    Bool RunStep(World &world, const Controls &controls);
    void BeginStep(World &world);
    void ChooseFight(World &world, Int32 slot);
    void ChooseCap(World &world);
    void ChooseSwitch(World &world, Int32 index, Bool forced);
    void ChooseRun(World &world);
    Int32 EnemyMove();
    void DoAttack(World &world, Int32 side, Int32 move);
    void DoThrow(World &world);
    void AfterFaint(World &world, Int32 side);
    Critter &Active(World &world)
    {
        return world.party[static_cast<size_t>(active)];
    }
    const Critter &Active(const World &world) const
    {
        return world.party[static_cast<size_t>(active)];
    }

    void RenderScene(const World &world, Renderer2D &r) const;

    std::vector<Critter> enemies;
    size_t enemyIndex = 0;
    Int32 active = 0;
    std::string trainer;
    std::string trainerSprite;
    Int32 prize = 0;
    Bool legend = false;
    // Whether each side has sent out its first critter yet (the trainers stand there until then).
    Bool sentOut[2] = {false, false};
    std::string background;
    std::deque<Step> steps;
    Bool stepStarted = false;
    Float stepTime = 0.0f;
    Float typed = 0.0f;
    std::string shownText;
    Mode mode = Mode::STEPS;
    Int32 cursor = 0;
    Int32 moveCursor = 0;
    Int32 teamCursor = 0;
    BattleResult result = BattleResult::NONE;
    Int32 tick = 0;
    Float time = 0.0f;
    // Presentation.
    Float shownHp[2] = {0.0f, 0.0f};
    Float lunge[2] = {0.0f, 0.0f};
    Float flash[2] = {0.0f, 0.0f};
    // 1 = fully shown; animated for send-out (grow) and faint (sink and fade).
    Float presence[2] = {0.0f, 0.0f};
    Float sink[2] = {0.0f, 0.0f};
    Bool capOut = false;
    Float capT = 0.0f;
    Float capWobble = 0.0f;
    Bool caughtStars = false;
    Int32 shakes = 0;
    Bool caught = false;
    Int32 turnEnemyMove = 0;
};
} // namespace crittercap
