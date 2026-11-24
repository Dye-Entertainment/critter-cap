#include "game/battle.h"

#include "game/text.h"
#include "utils/logger.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include "compiled/warning_on.h"

namespace crittercap
{
namespace
{
constexpr Float TYPE_SPEED = 60.0f;
constexpr size_t BATTLE_LINE = 36;
constexpr Float INTRO_TIME = 0.8f;
constexpr Float GLYPH_ADVANCE = 6.0f;

Float Quantize(const Float v)
{
    return std::round(std::clamp(v, 0.0f, 1.0f) * 8.0f) / 8.0f;
}
} // namespace

void Battle::Start(World &world, const Request &request)
{
    *this = Battle{};
    if (!request.team.empty())
    {
        std::stringstream stream(request.team);
        std::string item;
        while (std::getline(stream, item, ','))
        {
            Critter c;
            if (ParseCritter(item, c))
            {
                enemies.push_back(c);
            }
        }
        trainer = request.trainer;
        trainerSprite = request.trainerSprite;
        prize = request.prize;
    }
    if (enemies.empty())
    {
        enemies.push_back(request.wild);
    }
    legend = request.legend;
    for (size_t i = 0; i < world.party.size(); ++i)
    {
        if (!world.party[i].Fainted())
        {
            active = static_cast<Int32>(i);
            break;
        }
    }
    background = world.map.name == "dungeon" ? "stone" : (world.map.name == "tiny" ? "soil" : "grass");
    shownHp[PLAYER] = static_cast<Float>(Active(world).hp);
    shownHp[ENEMY] = static_cast<Float>(enemies[0].hp);
    Push(StepType::WAIT, 0, 0, INTRO_TIME + 0.1f);
    const std::string &enemy_name = enemies[0].Name();
    if (IsTrainer())
    {
        Text(trainer + " wants to battle!");
        Text(trainer + " sent out " + enemy_name + "!");
        Push(StepType::SEND_OUT, ENEMY);
    }
    else
    {
        world.seen[static_cast<size_t>(enemies[0].species)] = true;
        presence[ENEMY] = 1.0f;
        sentOut[ENEMY] = true;
        Text(legend ? "The mighty " + enemy_name + " blocks the way!" : "A wild " + enemy_name + " appeared!",
             legend ? Sfx::BOSS_ROAR : Sfx::COUNT);
    }
    Text(std::string("Go, ") + Active(world).Name() + "!");
    Push(StepType::SEND_OUT, PLAYER);
    Push(StepType::TURN_END);
}

void Battle::Text(const std::string &text, const Sfx sfx)
{
    Step s;
    s.type = StepType::TEXT;
    s.text = text;
    s.sfx = sfx;
    steps.push_back(s);
}

void Battle::Push(const StepType type, const Int32 side, const Int32 value, const Float duration)
{
    Step s;
    s.type = type;
    s.side = side;
    s.value = value;
    s.duration = duration;
    steps.push_back(s);
}

void Battle::PushFront(std::vector<Step> front)
{
    for (auto it = front.rbegin(); it != front.rend(); ++it)
    {
        steps.push_front(*it);
    }
}

Int32 Battle::EnemyMove()
{
    const Critter &e = enemies[enemyIndex];
    // Wild critters pick at random; trainers and the legend favour their strongest move.
    const Int32 count = e.MoveCount();
    const Int32 pick = static_cast<Int32>((static_cast<UInt32>(tick) * 2654435761u) >> 16) % count;
    if ((IsTrainer() || legend) && pick == 0 && count > 1)
    {
        return e.MoveAt(count - 1);
    }
    return e.MoveAt(pick);
}

void Battle::ChooseFight(World &world, const Int32 slot)
{
    shownText.clear();
    const Int32 move = Active(world).MoveAt(slot);
    const Int32 enemy_move = EnemyMove();
    const Bool player_first = Active(world).Speed() >= enemies[enemyIndex].Speed();
    if (player_first)
    {
        Push(StepType::ATTACK, PLAYER, move);
        Push(StepType::ATTACK, ENEMY, enemy_move);
    }
    else
    {
        Push(StepType::ATTACK, ENEMY, enemy_move);
        Push(StepType::ATTACK, PLAYER, move);
    }
    Push(StepType::TURN_END);
    mode = Mode::STEPS;
}

void Battle::ChooseCap(World &world)
{
    mode = Mode::STEPS;
    if (IsTrainer())
    {
        Text("You can't cap another trainer's critter!");
        Push(StepType::TURN_END);
        return;
    }
    if (world.caps <= 0)
    {
        Text("You're out of caps! Mira sells more in Hearthwood.");
        Push(StepType::TURN_END);
        return;
    }
    world.caps--;
    const Critter &e = enemies[enemyIndex];
    const Float max_hp = static_cast<Float>(e.MaxHp());
    const Float hp_factor = (3.0f * max_hp - 2.0f * static_cast<Float>(e.hp)) / (3.0f * max_hp);
    const Float chance = std::clamp(static_cast<Float>(e.Def().catchRate) / 255.0f * hp_factor * 1.5f + 0.04f, 0.02f, 1.0f);
    // One draw decides the catch; near misses wobble longer before breaking out.
    const Float roll = world.rng.NextUnit();
    caught = roll < chance;
    shakes = caught ? 3 : std::clamp(static_cast<Int32>(chance / std::max(roll, 0.001f) * 3.0f), 0, 2);
    Text("Pip threw a cap!");
    Push(StepType::THROW, ENEMY, 0, 0.6f);
    for (Int32 i = 0; i < shakes; ++i)
    {
        Push(StepType::SHAKE, ENEMY, i, 0.75f);
    }
    if (caught)
    {
        Push(StepType::CATCH);
    }
    else
    {
        Push(StepType::BREAK_FREE);
    }
}

void Battle::ChooseSwitch(World &world, const Int32 index, const Bool forced)
{
    mode = Mode::STEPS;
    if (!forced)
    {
        Text(std::string("Come back, ") + Active(world).Name() + "!");
        Push(StepType::RECALL, PLAYER, 0, 0.35f);
    }
    Push(StepType::SET_ACTIVE, PLAYER, index);
    Text(std::string("Go, ") + world.party[static_cast<size_t>(index)].Name() + "!");
    Push(StepType::SEND_OUT, PLAYER);
    if (!forced)
    {
        Push(StepType::ATTACK, ENEMY, EnemyMove());
    }
    Push(StepType::TURN_END);
}

void Battle::ChooseRun(World &world)
{
    mode = Mode::STEPS;
    if (IsTrainer())
    {
        Text("No running from a trainer battle!");
        Push(StepType::TURN_END);
        return;
    }
    if (legend)
    {
        Text(std::string(enemies[enemyIndex].Name()) + " won't let you leave!");
        Push(StepType::TURN_END);
        return;
    }
    const Float speed_gap = static_cast<Float>(Active(world).Speed() - enemies[enemyIndex].Speed());
    const Float chance = std::clamp(0.6f + speed_gap * 0.04f, 0.35f, 0.95f);
    if (world.rng.NextUnit() < chance)
    {
        Text("Got away safely!", Sfx::ROLL);
        Push(StepType::END, 0, static_cast<Int32>(BattleResult::RAN));
        return;
    }
    Text("Couldn't get away!");
    Push(StepType::ATTACK, ENEMY, EnemyMove());
    Push(StepType::TURN_END);
}

void Battle::DoAttack(World &world, const Int32 side, const Int32 move_index)
{
    Critter &attacker = side == PLAYER ? Active(world) : enemies[enemyIndex];
    Critter &defender = side == PLAYER ? enemies[enemyIndex] : Active(world);
    if (attacker.Fainted() || defender.Fainted())
    {
        return;
    }
    const MoveDef &move = Move(move_index);
    const std::string who = side == PLAYER ? std::string(attacker.Name())
                                           : (IsTrainer() ? "Foe " : (legend ? "" : "Wild ")) + std::string(attacker.Name());
    std::vector<Step> out;
    Step used;
    used.text = who + " used " + move.name + "!";
    out.push_back(used);
    if (world.rng.NextInt(0, 99) >= move.accuracy)
    {
        Step miss;
        miss.text = "But it missed!";
        out.push_back(miss);
        PushFront(out);
        return;
    }
    const Float effect = Effectiveness(move.type, defender.Def().type);
    const Float stab = move.type == attacker.Def().type && move.type != Type::NORMAL ? 1.25f : 1.0f;
    const Bool crit = world.rng.NextInt(0, 15) == 0;
    const Float base = (static_cast<Float>(2 * attacker.level) / 5.0f + 2.0f) * static_cast<Float>(move.power) *
                       static_cast<Float>(attacker.Attack()) / static_cast<Float>(defender.Defense()) / 25.0f + 2.0f;
    const Float damage_f = base * stab * effect * (crit ? 1.5f : 1.0f) * world.rng.NextRange(0.85f, 1.0f);
    const Int32 damage = std::max(1, static_cast<Int32>(damage_f));

    Step hit;
    hit.type = StepType::HIT;
    hit.side = side;
    hit.value = damage;
    out.push_back(hit);
    if (crit)
    {
        Step s;
        s.text = "A critical hit!";
        out.push_back(s);
    }
    if (effect > 1.0f)
    {
        Step s;
        s.text = "It's super effective!";
        out.push_back(s);
    }
    else if (effect < 1.0f)
    {
        Step s;
        s.text = "It's not very effective...";
        out.push_back(s);
    }
    if (damage >= defender.hp)
    {
        const Int32 target = side == PLAYER ? ENEMY : PLAYER;
        Step faint;
        faint.type = StepType::FAINT;
        faint.side = target;
        out.push_back(faint);
        Step text;
        text.text = (target == ENEMY && !legend ? (IsTrainer() ? "Foe " : "Wild ") : "") + std::string(defender.Name()) +
                    " fainted!";
        out.push_back(text);
        Step after;
        after.type = StepType::AFTER_FAINT;
        after.side = target;
        out.push_back(after);
    }
    PushFront(out);
}

void Battle::AfterFaint(World &world, const Int32 side)
{
    // Whatever was left of the turn is dropped.
    steps.clear();
    if (side == ENEMY)
    {
        Critter &mine = Active(world);
        const Critter &foe = enemies[enemyIndex];
        Int32 gain = foe.level * 6 + 6;
        if (IsTrainer() || legend)
        {
            gain = gain * 3 / 2;
        }
        if (!mine.Fainted() && mine.level < MAX_LEVEL)
        {
            Text(std::string(mine.Name()) + " gained " + std::to_string(gain) + " XP!");
            mine.xp += gain;
            while (mine.xp >= mine.XpToNext() && mine.level < MAX_LEVEL)
            {
                mine.xp -= mine.XpToNext();
                const Int32 old_max = mine.MaxHp();
                mine.level++;
                mine.hp += mine.MaxHp() - old_max;
                Text(std::string(mine.Name()) + " grew to level " + std::to_string(mine.level) + "!", Sfx::LEVEL_UP);
                if (mine.level == LEARN_LEVEL)
                {
                    Text(std::string(mine.Name()) + " learned " + Move(mine.MoveAt(2)).name + "!", Sfx::ITEM);
                }
            }
        }
        if (legend)
        {
            Text(std::string(foe.Name()) + " curled up and rolled away into the roots...");
            Text("Capo: Aww! We have to CATCH it, Pip. Weaken it, then throw caps!");
            Push(StepType::END, 0, static_cast<Int32>(BattleResult::WON));
            return;
        }
        if (IsTrainer() && enemyIndex + 1 < enemies.size())
        {
            Push(StepType::WAIT, 0, 1, 0.2f);
            Step next;
            next.type = StepType::SET_ACTIVE;
            next.side = ENEMY;
            next.value = static_cast<Int32>(enemyIndex + 1);
            steps.push_back(next);
            Text(trainer + " sent out " + enemies[enemyIndex + 1].Name() + "!");
            Push(StepType::SEND_OUT, ENEMY);
            Push(StepType::TURN_END);
            return;
        }
        if (IsTrainer())
        {
            Text("You beat " + trainer + "!");
            if (prize > 0)
            {
                world.gems = std::min(999, world.gems + prize);
                Text("You got " + std::to_string(prize) + " gems for winning!", Sfx::GEM);
            }
        }
        Push(StepType::END, 0, static_cast<Int32>(BattleResult::WON));
        return;
    }
    if (PartyCanFight(world))
    {
        mode = Mode::FORCED_TEAM;
        teamCursor = 0;
        while (world.party[static_cast<size_t>(teamCursor)].Fainted())
        {
            teamCursor++;
        }
        return;
    }
    Text("Pip has no critters left that can fight!");
    Text("Pip scooped them up and hurried back to safety...", Sfx::DIE);
    Push(StepType::END, 0, static_cast<Int32>(BattleResult::LOST));
}

void Battle::BeginStep(World &world)
{
    Step &s = steps.front();
    stepStarted = true;
    stepTime = 0.0f;
    if (s.sfx != Sfx::COUNT)
    {
        PlaySfx(s.sfx);
    }
    switch (s.type)
    {
    case StepType::TEXT:
        shownText = s.text;
        typed = 0.0f;
        break;
    case StepType::HIT:
    {
        Critter &target = s.side == PLAYER ? enemies[enemyIndex] : Active(world);
        target.hp = std::max(0, target.hp - s.value);
        lunge[s.side] = 1.0f;
        flash[1 - s.side] = 0.6f;
        PlaySfx(legend && s.side == ENEMY ? Sfx::BOSS_HIT : Sfx::HIT);
        break;
    }
    case StepType::FAINT:
        PlaySfx(Sfx::ENEMY_DIE);
        break;
    case StepType::SEND_OUT:
        sentOut[s.side] = true;
        presence[s.side] = 0.0f;
        sink[s.side] = 0.0f;
        shownHp[s.side] = static_cast<Float>(s.side == PLAYER ? Active(world).hp : enemies[enemyIndex].hp);
        if (s.side == ENEMY)
        {
            world.seen[static_cast<size_t>(enemies[enemyIndex].species)] = true;
        }
        PlaySfx(Sfx::POT, 0.6f, 1.3f);
        break;
    case StepType::RECALL:
        PlaySfx(Sfx::SHRINK, 0.4f, 2.0f);
        break;
    case StepType::THROW:
        PlaySfx(Sfx::THROW);
        capT = 0.0f;
        break;
    case StepType::SHAKE:
        PlaySfx(Sfx::BOUNCE, 0.8f);
        break;
    default:
        break;
    }
}

Bool Battle::RunStep(World &world, const Controls &controls)
{
    if (steps.empty())
    {
        mode = Mode::MENU;
        return false;
    }
    const Step front = steps.front();
    // Instant steps run and make room for whatever they queue.
    switch (front.type)
    {
    case StepType::ATTACK:
        steps.pop_front();
        DoAttack(world, front.side, front.value);
        return true;
    case StepType::AFTER_FAINT:
        steps.pop_front();
        AfterFaint(world, front.side);
        return mode == Mode::STEPS;
    case StepType::SET_ACTIVE:
        steps.pop_front();
        if (front.side == PLAYER)
        {
            active = front.value;
            presence[PLAYER] = 0.0f;
            sink[PLAYER] = 0.0f;
            shownHp[PLAYER] = static_cast<Float>(Active(world).hp);
        }
        else
        {
            enemyIndex = static_cast<size_t>(front.value);
            presence[ENEMY] = 0.0f;
            sink[ENEMY] = 0.0f;
            shownHp[ENEMY] = static_cast<Float>(enemies[enemyIndex].hp);
        }
        return true;
    case StepType::CATCH:
    {
        steps.pop_front();
        caughtStars = true;
        PlaySfx(Sfx::CAUGHT);
        const Critter caught_critter = enemies[enemyIndex];
        const Bool fresh = !world.caught[static_cast<size_t>(caught_critter.species)];
        const Bool to_party = AddCritter(world, caught_critter);
        std::vector<Step> out;
        Step s;
        s.text = std::string("Gotcha! ") + caught_critter.Name() + " was caught!";
        out.push_back(s);
        if (fresh)
        {
            s.text = std::string(caught_critter.Name()) + "'s data was added to the Critterdex.";
            out.push_back(s);
        }
        if (!to_party)
        {
            s.text = std::string(caught_critter.Name()) + " hopped into Capo's spare pocket.";
            out.push_back(s);
        }
        Step end;
        end.type = StepType::END;
        end.value = static_cast<Int32>(BattleResult::CAUGHT);
        out.push_back(end);
        PushFront(out);
        return true;
    }
    case StepType::BREAK_FREE:
    {
        steps.pop_front();
        capOut = false;
        presence[ENEMY] = 1.0f;
        PlaySfx(Sfx::POT);
        static const Char *LINES[4] = {"Oh no! It broke free!", "Aww! It appeared to be caught!",
                                       "Argh! Almost had it!", "Shoot! It was so close!"};
        Text(LINES[std::clamp(shakes, 0, 3)]);
        Push(StepType::ATTACK, ENEMY, EnemyMove());
        Push(StepType::TURN_END);
        return true;
    }
    case StepType::TURN_END:
        steps.pop_front();
        if (steps.empty())
        {
            shownText.clear();
            mode = Mode::MENU;
            return false;
        }
        return true;
    case StepType::END:
        steps.pop_front();
        result = static_cast<BattleResult>(front.value);
        return false;
    default:
        break;
    }

    if (!stepStarted)
    {
        BeginStep(world);
    }
    stepTime += TICK;
    const Step &s = steps.front();
    Bool done = false;
    const Bool drained = std::abs(shownHp[PLAYER] - static_cast<Float>(Active(world).hp)) < 0.5f &&
                         std::abs(shownHp[ENEMY] - static_cast<Float>(enemies[enemyIndex].hp)) < 0.5f;
    switch (s.type)
    {
    case StepType::TEXT:
    {
        const Float before = typed;
        typed = std::min(static_cast<Float>(s.text.size()), typed + TYPE_SPEED * TICK);
        if (static_cast<Int32>(before) / 3 != static_cast<Int32>(typed) / 3 &&
            typed < static_cast<Float>(s.text.size()))
        {
            PlaySfx(Sfx::TEXT, 0.3f);
        }
        if (stepTime > 0.1f && (controls.a.pressed || controls.b.pressed))
        {
            if (typed < static_cast<Float>(s.text.size()))
            {
                typed = static_cast<Float>(s.text.size());
            }
            else
            {
                PlaySfx(Sfx::MENU, 0.4f);
                done = true;
            }
        }
        break;
    }
    case StepType::WAIT:
        done = stepTime >= s.duration;
        break;
    case StepType::HIT:
        done = stepTime >= 0.35f && drained;
        break;
    case StepType::FAINT:
        sink[s.side] = std::min(1.0f, stepTime / 0.5f);
        if (stepTime >= 0.55f)
        {
            presence[s.side] = 0.0f;
            done = true;
        }
        break;
    case StepType::SEND_OUT:
        presence[s.side] = std::min(1.0f, stepTime / 0.3f);
        done = stepTime >= 0.4f;
        break;
    case StepType::RECALL:
        presence[s.side] = std::max(0.0f, 1.0f - stepTime / 0.3f);
        done = stepTime >= 0.35f;
        break;
    case StepType::THROW:
        capT = std::min(1.0f, stepTime / s.duration);
        if (stepTime >= s.duration)
        {
            capOut = true;
            presence[ENEMY] = 0.0f;
            PlaySfx(Sfx::SHRINK, 0.5f, 1.8f);
            done = true;
        }
        break;
    case StepType::SHAKE:
        capWobble = stepTime < 0.4f ? std::sin(stepTime / 0.4f * 6.283f * 2.0f) : 0.0f;
        done = stepTime >= s.duration;
        break;
    default:
        done = true;
        break;
    }
    if (done)
    {
        steps.pop_front();
        stepStarted = false;
        return true;
    }
    return false;
}

BattleResult Battle::Update(World &world, const Controls &c)
{
    tick++;
    time += TICK;
    for (Int32 side = 0; side < 2; ++side)
    {
        lunge[side] = std::max(0.0f, lunge[side] - TICK * 4.0f);
        flash[side] = std::max(0.0f, flash[side] - TICK);
        const Float actual = static_cast<Float>(side == PLAYER ? Active(world).hp : enemies[enemyIndex].hp);
        const Float max_hp = static_cast<Float>(side == PLAYER ? Active(world).MaxHp() : enemies[enemyIndex].MaxHp());
        const Float rate = std::max(0.25f, max_hp * TICK * 1.2f);
        if (shownHp[side] > actual)
            shownHp[side] = std::max(actual, shownHp[side] - rate);
        else if (shownHp[side] < actual)
            shownHp[side] = std::min(actual, shownHp[side] + rate);
    }
    switch (mode)
    {
    case Mode::STEPS:
        for (Int32 i = 0; i < 16 && mode == Mode::STEPS && result == BattleResult::NONE; ++i)
        {
            if (!RunStep(world, c))
            {
                break;
            }
        }
        break;
    case Mode::MENU:
    {
        const Int32 before = cursor;
        if (c.left.pressed || c.right.pressed)
            cursor ^= 1;
        if (c.up.pressed || c.down.pressed)
            cursor ^= 2;
        if (cursor != before)
            PlaySfx(Sfx::MENU, 0.4f);
        if (c.a.pressed)
        {
            PlaySfx(Sfx::MENU);
            switch (cursor)
            {
            case 0:
                mode = Mode::MOVES;
                moveCursor = std::min(moveCursor, Active(world).MoveCount() - 1);
                break;
            case 1:
                ChooseCap(world);
                break;
            case 2:
                mode = Mode::TEAM;
                teamCursor = active;
                break;
            default:
                ChooseRun(world);
                break;
            }
        }
        break;
    }
    case Mode::MOVES:
    {
        const Int32 count = Active(world).MoveCount();
        const Int32 before = moveCursor;
        if (c.left.pressed || c.right.pressed)
            moveCursor ^= 1;
        if (c.up.pressed || c.down.pressed)
            moveCursor ^= 2;
        if (moveCursor >= count)
            moveCursor = before;
        if (moveCursor != before)
            PlaySfx(Sfx::MENU, 0.4f);
        if (c.b.pressed)
        {
            mode = Mode::MENU;
            PlaySfx(Sfx::MENU, 0.5f);
        }
        else if (c.a.pressed)
        {
            PlaySfx(Sfx::MENU);
            ChooseFight(world, moveCursor);
        }
        break;
    }
    case Mode::TEAM:
    case Mode::FORCED_TEAM:
    {
        const Int32 count = static_cast<Int32>(world.party.size());
        if (c.up.pressed)
        {
            teamCursor = (teamCursor + count - 1) % count;
            PlaySfx(Sfx::MENU, 0.4f);
        }
        if (c.down.pressed)
        {
            teamCursor = (teamCursor + 1) % count;
            PlaySfx(Sfx::MENU, 0.4f);
        }
        if (mode == Mode::TEAM && c.b.pressed)
        {
            mode = Mode::MENU;
            PlaySfx(Sfx::MENU, 0.5f);
        }
        else if (c.a.pressed)
        {
            const Critter &pick = world.party[static_cast<size_t>(teamCursor)];
            if (pick.Fainted() || (teamCursor == active && mode == Mode::TEAM))
            {
                PlaySfx(Sfx::THUD, 0.6f);
            }
            else
            {
                PlaySfx(Sfx::MENU);
                ChooseSwitch(world, teamCursor, mode == Mode::FORCED_TEAM);
            }
        }
        break;
    }
    }
    if (result != BattleResult::NONE)
    {
        return result;
    }
    return BattleResult::NONE;
}

// -----------------------------------------------------------------------------------------------
// Rendering
// -----------------------------------------------------------------------------------------------
void Battle::Render(const World &world, Renderer2D &r) const
{
    RenderScene(world, r);
}

void Battle::RenderScene(const World &world, Renderer2D &r) const
{
    Color sky{0.62f, 0.84f, 0.98f, 1.0f};
    Color far{0.5f, 0.78f, 0.42f, 1.0f};
    Color ground{0.56f, 0.82f, 0.38f, 1.0f};
    if (background == "soil")
    {
        sky = {0.98f, 0.86f, 0.62f, 1.0f};
        far = {0.45f, 0.72f, 0.33f, 1.0f};
        ground = {0.8f, 0.6f, 0.4f, 1.0f};
    }
    else if (background == "stone")
    {
        sky = {0.2f, 0.15f, 0.16f, 1.0f};
        far = {0.32f, 0.25f, 0.22f, 1.0f};
        ground = {0.46f, 0.39f, 0.33f, 1.0f};
    }
    r.FillRect(0.0f, 0.0f, SCREEN_W, SCREEN_H, sky);
    r.FillRect(0.0f, 36.0f, SCREEN_W, 10.0f, far);
    r.FillRect(0.0f, 46.0f, SCREEN_W, 70.0f, ground);
    // A few light stripes for depth.
    for (Int32 i = 0; i < 4; ++i)
    {
        const Float y = 52.0f + static_cast<Float>(i * i) * 5.0f;
        r.FillRect(0.0f, y, SCREEN_W, 1.0f, {1.0f, 1.0f, 1.0f, 0.12f});
    }

    DrawOptions o;
    o.screen = true;
    const Float slide = 1.0f - std::min(1.0f, time / INTRO_TIME);
    const Float eased = slide * slide;
    const Float enemy_x = 176.0f + eased * 180.0f;
    const Float player_x = 62.0f - eased * 180.0f;
    const Float enemy_y = 64.0f;
    const Float player_y = 108.0f;
    const std::string pad = "pad_" + background;
    r.Draw(pad, enemy_x, enemy_y, o);
    DrawOptions big_pad = o;
    big_pad.scale = 1.25f;
    r.Draw(pad, player_x, player_y + 2.0f, big_pad);

    const Int32 frame = (tick / 24) % 2;
    // Enemy side.
    const Critter &enemy = enemies[enemyIndex];
    if (!sentOut[ENEMY] && IsTrainer())
    {
        DrawOptions t = o;
        t.scale = 2.0f;
        r.Draw(trainerSprite + "_down_0", enemy_x, enemy_y - 2.0f, t);
    }
    else if (presence[ENEMY] > 0.0f)
    {
        DrawOptions e = o;
        const Float base_scale = enemy.species == LEGEND_SPECIES ? 1.6f : 2.0f;
        e.scale = std::max(0.25f, base_scale * Quantize(presence[ENEMY]));
        e.flash = flash[ENEMY] > 0.0f && (tick / 3) % 2 == 0 ? 0.85f : 0.0f;
        e.tint.a = 1.0f - Quantize(sink[ENEMY]);
        const Float lx = -lunge[ENEMY] * 10.0f;
        const Float ly = lunge[ENEMY] * 5.0f + sink[ENEMY] * 14.0f;
        const Float bob = (tick / 24) % 2 == 0 ? 0.0f : -1.0f;
        r.Draw(SpeciesSprite(enemy.species, false, frame), enemy_x + lx, enemy_y - 2.0f + ly + bob, e);
    }
    // Player side.
    if (!sentOut[PLAYER])
    {
        DrawOptions t = o;
        t.scale = 2.0f;
        r.Draw("hero_up_0", player_x, player_y - 2.0f, t);
    }
    else if (presence[PLAYER] > 0.0f && !world.party.empty())
    {
        const Critter &mine = Active(world);
        DrawOptions p = o;
        p.scale = std::max(0.25f, 2.5f * Quantize(presence[PLAYER]));
        p.flash = flash[PLAYER] > 0.0f && (tick / 3) % 2 == 0 ? 0.85f : 0.0f;
        p.tint.a = 1.0f - Quantize(sink[PLAYER]);
        const Float lx = lunge[PLAYER] * 10.0f;
        const Float ly = -lunge[PLAYER] * 5.0f + sink[PLAYER] * 14.0f;
        r.Draw(SpeciesSprite(mine.species, true, frame), player_x + lx, player_y - 2.0f + ly, p);
    }
    // The thrown cap.
    if (!steps.empty() && steps.front().type == StepType::THROW && stepStarted)
    {
        const Float t = capT;
        const Float x = 40.0f + (enemy_x - 40.0f) * t;
        const Float y = 104.0f + (enemy_y - 6.0f - 104.0f) * t - std::sin(t * 3.1416f) * 46.0f;
        DrawOptions c = o;
        c.flipX = (tick / 4) % 2 == 0;
        r.Draw("cap", x, y, c);
    }
    else if (capOut)
    {
        DrawOptions c = o;
        c.scale = 1.5f;
        r.Draw("cap", enemy_x + capWobble * 4.0f, enemy_y - 4.0f, c);
        if (caughtStars)
        {
            for (Int32 i = 0; i < 3; ++i)
            {
                const Float a = time * 3.0f + static_cast<Float>(i) * 2.094f;
                r.Draw("star", enemy_x + std::cos(a) * 16.0f, enemy_y - 16.0f + std::sin(a) * 6.0f, o);
            }
        }
    }
}

void Battle::FillUi(const World &world, const GameUi &ui, UiState &state) const
{
    char buffer[48];
    state.arrowFrame = (tick / 15) % 2;

    // The info boxes show up once the critters are out.
    const Float slide = 1.0f - std::min(1.0f, time / INTRO_TIME);
    state.enemy = {};
    if (sentOut[ENEMY] && presence[ENEMY] > 0.0f && slide <= 0.0f)
    {
        const Critter &e = enemies[enemyIndex];
        const Float ratio = std::clamp(shownHp[ENEMY] / static_cast<Float>(e.MaxHp()), 0.0f, 1.0f);
        state.enemy.visible = true;
        state.enemy.name = e.Name();
        state.enemy.nameWidth = static_cast<Float>(state.enemy.name.size()) * GLYPH_ADVANCE;
        std::snprintf(buffer, sizeof(buffer), "Lv%d", e.level);
        state.enemy.level = buffer;
        state.enemy.caught = world.caught[static_cast<size_t>(e.species)];
        state.enemy.hpWidth = std::round(78.0f * ratio);
        state.enemy.hpColor = HpColor(ratio);
    }
    state.mine = {};
    if (sentOut[PLAYER] && presence[PLAYER] > 0.0f && !world.party.empty())
    {
        const Critter &m = Active(world);
        const Float ratio = std::clamp(shownHp[PLAYER] / static_cast<Float>(m.MaxHp()), 0.0f, 1.0f);
        state.mine.visible = true;
        state.mine.name = m.Name();
        std::snprintf(buffer, sizeof(buffer), "Lv%d", m.level);
        state.mine.level = buffer;
        state.mine.hpWidth = std::round(78.0f * ratio);
        state.mine.hpColor = HpColor(ratio);
        std::snprintf(buffer, sizeof(buffer), "%d/%d", static_cast<Int32>(std::ceil(shownHp[PLAYER])), m.MaxHp());
        state.mine.hpText = buffer;
        const Float xp = static_cast<Float>(m.xp) / static_cast<Float>(m.XpToNext());
        state.mine.xpWidth = std::round(94.0f * std::clamp(xp, 0.0f, 1.0f));
    }
    else if (!world.party.empty())
    {
        state.mine.name = Active(world).Name();
    }

    // The bottom box: the action menu, the moves or the battle messages.
    state.battleMode.clear();
    state.battleOptions.clear();
    state.battleText.clear();
    state.battleMore = false;
    if (mode == Mode::MENU)
    {
        state.battleMode = "menu";
        static const Char *OPTIONS[4] = {"FIGHT", "CAP", "TEAM", "RUN"};
        for (Int32 i = 0; i < 4; ++i)
        {
            UiOption option;
            option.label = OPTIONS[i];
            option.x = 140.0f + static_cast<Float>(i % 2) * 50.0f;
            option.y = 124.0f + static_cast<Float>(i / 2) * 16.0f;
            option.disabled = (i == 1 && (IsTrainer() || world.caps <= 0)) || (i == 3 && (IsTrainer() || legend));
            option.selected = i == cursor;
            state.battleOptions.push_back(option);
        }
    }
    else if (mode == Mode::MOVES)
    {
        state.battleMode = "moves";
        const Critter &m = Active(world);
        for (Int32 i = 0; i < m.MoveCount(); ++i)
        {
            UiOption option;
            option.label = Move(m.MoveAt(i)).name;
            option.x = 16.0f + static_cast<Float>(i % 2) * 72.0f;
            option.y = 124.0f + static_cast<Float>(i / 2) * 16.0f;
            option.selected = i == moveCursor;
            state.battleOptions.push_back(option);
        }
        const MoveDef &move = Move(m.MoveAt(moveCursor));
        state.moveType = TypeName(move.type);
        std::snprintf(buffer, sizeof(buffer), "Pow %d", move.power);
        state.movePower = buffer;
        const Float effect = Effectiveness(move.type, enemies[enemyIndex].Def().type);
        state.moveEffect.clear();
        if (world.caught[static_cast<size_t>(enemies[enemyIndex].species)] && effect != 1.0f)
        {
            state.moveEffect = effect > 1.0f ? "Strong!" : "Weak...";
            state.moveStrong = effect > 1.0f;
        }
    }
    else if (mode == Mode::STEPS && !shownText.empty())
    {
        // The last message stays up while the animations that follow it play.
        state.battleMode = "text";
        const Bool typing = !steps.empty() && steps.front().type == StepType::TEXT && stepStarted;
        const std::vector<std::string> lines = Wrap(shownText, BATTLE_LINE);
        size_t remaining = typing ? static_cast<size_t>(typed) : shownText.size();
        for (size_t i = 0; i < lines.size() && i < 3; ++i)
        {
            const size_t n = std::min(remaining, lines[i].size());
            if (i > 0)
            {
                state.battleText += '\n';
            }
            state.battleText += lines[i].substr(0, n);
            remaining = remaining > lines[i].size() + 1 ? remaining - lines[i].size() - 1 : 0;
        }
        state.battleText = ToUiText(state.battleText);
        state.battleMore = typing && typed >= static_cast<Float>(shownText.size());
    }

    // Picking a critter to switch to.
    state.battleTeam = mode == Mode::TEAM || mode == Mode::FORCED_TEAM;
    state.battleTeamBack = mode == Mode::TEAM;
    if (!state.battleTeam)
    {
        return;
    }
    state.battleTeamTitle = mode == Mode::FORCED_TEAM ? "Who's next?" : "Switch to which critter?";
    state.party.clear();
    for (size_t i = 0; i < world.party.size(); ++i)
    {
        const Critter &c = world.party[i];
        UiCritterRow row;
        row.y = 32.0f + static_cast<Float>(i) * 28.0f;
        row.selected = static_cast<Int32>(i) == teamCursor;
        row.fainted = c.Fainted();
        row.out = static_cast<Int32>(i) == active;
        row.name = c.Name();
        std::snprintf(buffer, sizeof(buffer), "Lv%d", c.level);
        row.level = buffer;
        const Float ratio = std::clamp(static_cast<Float>(c.hp) / static_cast<Float>(c.MaxHp()), 0.0f, 1.0f);
        row.hpWidth = std::round(80.0f * ratio);
        row.hpColor = HpColor(ratio);
        if (c.Fainted())
        {
            row.hpText = "FNT";
        }
        else
        {
            std::snprintf(buffer, sizeof(buffer), "%d/%d", c.hp, c.MaxHp());
            row.hpText = buffer;
        }
        row.sprite = ui.Sprite(SpeciesSprite(c.species, false, row.selected ? (tick / 12) % 2 : 0), 48.0f, row.y + 18.0f);
        state.party.push_back(row);
    }
}
} // namespace crittercap
