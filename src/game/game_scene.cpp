#include "game/game_scene.h"

#include "audio/sound.h"
#include "core/application.h"
#include "game/text.h"
#include "graphics/renderer.h"
#include "utils/logger.h"

#include "compiled/warning_off.h"
#include <stb/stb_image_write.h>
#include "compiled/warning_on.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include "compiled/warning_on.h"

namespace crittercap
{
namespace
{
constexpr Float FADE_TIME = 0.3f;
constexpr size_t LINE_CHARS = 34;
constexpr size_t PAGE_LINES = 3;
constexpr Float TYPE_SPEED = 55.0f;
constexpr Int32 CAP_PRICE = 20;
constexpr Int32 CAP_BUNDLE = 5;

struct ItemDef
{
    const Char *id;
    const Char *sprite;
    const Char *text;
};

const ItemDef ITEMS[] = {
    {"caps5", "cap", "You got 5 caps!|Throw one at a worn-out wild critter to catch it."},
    {"caps10", "cap", "You got 10 caps! That's a lot of hats."},
    {"key", "key", "You found a small key!|It opens a locked door somewhere in this dungeon."},
    {"bigkey", "bigkey", "You got the Big Key!|It opens the great door of Root Hollow's master."},
    {"gems20", "gem_r_0", "You found 20 gems!"},
    {"gems50", "gem_r_0", "You found 50 gems! What a haul!"},
    {"sunstone", "sunstone_0",
     "You got the Sunstone!|Its warm light pours through the roots of the hollow..."},
};

const ItemDef *FindItem(const std::string &id)
{
    for (const ItemDef &item : ITEMS)
    {
        if (id == item.id)
        {
            return &item;
        }
    }
    return nullptr;
}

const Color GOLD_TEXT{1.0f, 0.86f, 0.35f, 1.0f};
const Color SOFT_TEXT{0.75f, 0.8f, 0.95f, 1.0f};
const Color DIM_TEXT{0.5f, 0.5f, 0.6f, 1.0f};

Bool Confirm(const Controls &c)
{
    return c.a.pressed || c.start.pressed;
}
} // namespace

GameScene::GameScene(Renderer2D *renderer_2d) : renderer(renderer_2d)
{
    if (!LoadTileInfo(world.tileInfo))
    {
        LOGERROR("Tile info failed to load");
    }
    EnterMap(world, "village", "start", *renderer);
    PlayMusic(Music::TITLE);
    if (const char *input = std::getenv("CRITTERCAP_INPUT"))
    {
        std::stringstream stream(input);
        std::string token;
        while (stream >> token)
        {
            const size_t colon = token.find(':');
            if (colon == std::string::npos)
            {
                continue;
            }
            script.push_back({token.substr(0, colon), std::atoi(token.c_str() + colon + 1)});
        }
    }
    if (const char *shots = std::getenv("CRITTERCAP_SHOTS"))
    {
        std::stringstream stream(shots);
        std::string token;
        while (std::getline(stream, token, ','))
        {
            shotTicks.push_back(std::atoi(token.c_str()));
        }
    }
    if (std::getenv("CRITTERCAP_AUTOSTART") || std::getenv("CRITTERCAP_MAP"))
    {
        NewGame();
    }
}

void GameScene::NewGame()
{
    world.flags.clear();
    world.party.clear();
    world.box.clear();
    world.seen.fill(false);
    world.caught.fill(false);
    world.starter = -1;
    world.caps = 0;
    world.gems = 0;
    world.keys = 0;
    world.bigKey = false;
    world.healMap = "village";
    world.healSpawn = "start";
    world.player = Player{};
    world.map.name.clear();
    playTime = 0.0f;
    pendingItem.clear();
    pendingShrink = pendingEnding = false;
    pendingAction = Request{};

    // Debug/test hooks: jump straight into an area with a ready-made team.
    const char *map = std::getenv("CRITTERCAP_MAP");
    const char *spawn = std::getenv("CRITTERCAP_SPAWN");
    if (const char *party = std::getenv("CRITTERCAP_PARTY"))
    {
        std::stringstream stream(party);
        std::string item;
        while (std::getline(stream, item, ','))
        {
            Critter c;
            if (ParseCritter(item, c))
            {
                AddCritter(world, c);
                if (world.starter < 0 && c.species < 3)
                {
                    world.starter = c.species;
                }
            }
        }
        if (!world.party.empty())
        {
            world.flags.insert("got_starter");
            world.caps = 10;
        }
    }
    if (const char *caps = std::getenv("CRITTERCAP_CAPS"))
    {
        world.caps = std::atoi(caps);
    }
    if (const char *flags = std::getenv("CRITTERCAP_FLAGS"))
    {
        std::stringstream stream(flags);
        std::string flag;
        while (std::getline(stream, flag, ','))
        {
            world.flags.insert(flag);
            if (flag == "bigkey")
                world.bigKey = true;
            if (flag == "key")
                world.keys++;
        }
    }
    world.peaceful = std::getenv("CRITTERCAP_PEACE") != nullptr;
    EnterMap(world, map ? map : "village", spawn ? spawn : "start", *renderer);
    if (const char *pos = std::getenv("CRITTERCAP_POS"))
    {
        Float tx = 0.0f;
        Float ty = 0.0f;
        if (std::sscanf(pos, "%f,%f", &tx, &ty) == 2)
        {
            world.player.x = tx * TILE;
            world.player.y = ty * TILE;
            world.roomX = static_cast<Int32>(std::floor(world.player.x / SCREEN_W));
            world.roomY = static_cast<Int32>(std::floor((world.player.y - 4.0f) / SCREEN_H));
            UpdateCamera(world, true);
        }
    }
    if (const char *encounter = std::getenv("CRITTERCAP_BATTLE"))
    {
        // Starts a battle right away: "blob_g:5" (wild), "legend:boss:15" or "trainer:blob_g:5,ant:6".
        std::string spec = encounter;
        world.request = Request{};
        world.request.type = RequestType::BATTLE;
        if (spec.rfind("trainer:", 0) == 0)
        {
            world.request.trainer = "Tobi";
            world.request.trainerSprite = "tobi";
            world.request.team = spec.substr(8);
            world.request.prize = 20;
        }
        else
        {
            world.request.legend = spec.rfind("legend:", 0) == 0;
            ParseCritter(world.request.legend ? spec.substr(7) : spec, world.request.wild);
        }
    }
    UpdateMusic();
    phase = Phase::FADE_IN;
    phaseTime = 0.0f;
    if (!map)
    {
        world.request.type = RequestType::DIALOG;
        world.request.pages = SplitPages(
            "Capo: Pip! Psst, Pip! Wake up, sleepyhead!|Capo: Yes, it's me, Capo, the smartest hat "
            "in Hearthwood. Today's the day! Elder Bram is giving you your very first critter!|"
            "Capo: He's waiting in the village square. Use the arrow keys to walk and Z to talk. "
            "Let's go!"
        );
    }
}

void GameScene::ApplyScript()
{
    if (scriptIndex >= script.size())
    {
        return;
    }
    const ScriptStep &step = script[scriptIndex];
    const auto set = [&](Button &b, const char *name) {
        const Bool down = step.buttons.find(name) != std::string::npos;
        b.pressed = down && (scriptFrame == 0 || !b.down);
        b.released = !down && b.down;
        b.down = down;
    };
    set(controls.left, "left");
    set(controls.right, "right");
    set(controls.up, "up");
    set(controls.down, "down");
    set(controls.a, "z");
    set(controls.b, "x");
    set(controls.start, "start");
    if (++scriptFrame >= step.frames)
    {
        scriptFrame = 0;
        scriptIndex++;
    }
}

void GameScene::Update(const double elapsed_seconds)
{
    if (script.empty())
    {
        PollControls(controls);
    }
    accumulator += std::min(elapsed_seconds, 0.1);
    Int32 steps = 0;
    while (accumulator >= TICK && steps < 4)
    {
        if (!script.empty())
        {
            ApplyScript();
        }
        Tick();
        ConsumePresses(controls);
        accumulator -= TICK;
        ++steps;
    }
}

void GameScene::UpdateMusic()
{
    if (phase == Phase::BATTLE || phase == Phase::BATTLE_IN)
    {
        return;
    }
    Music desired = world.map.music;
    if (world.map.rooms)
    {
        const Int32 room = RoomIndexAt(world, world.player.x, world.player.y - 4.0f);
        for (const Entity &e : world.entities)
        {
            if (e.kind == Kind::LEGEND && e.room == room && e.alive)
            {
                desired = Music::NONE;
            }
        }
    }
    PlayMusic(desired);
}

void GameScene::OpenDialog(const std::vector<std::string> &source_pages)
{
    pages.clear();
    for (const std::string &p : source_pages)
    {
        const std::vector<std::string> wrapped = Wrap(p, LINE_CHARS);
        for (size_t i = 0; i < wrapped.size(); i += PAGE_LINES)
        {
            std::string chunk;
            for (size_t j = i; j < std::min(wrapped.size(), i + PAGE_LINES); ++j)
            {
                chunk += (j == i ? "" : "\n") + wrapped[j];
            }
            pages.push_back(chunk);
        }
    }
    if (pages.empty())
    {
        FinishDialog();
        return;
    }
    page = 0;
    typed = 0.0f;
    phase = Phase::DIALOG;
    phaseTime = 0.0f;
}

void GameScene::AdvanceDialog()
{
    const size_t total = pages[page].size();
    if (typed < static_cast<Float>(total))
    {
        typed = static_cast<Float>(total);
        return;
    }
    PlaySfx(Sfx::MENU, 0.5f);
    page++;
    typed = 0.0f;
    if (page >= pages.size())
    {
        FinishDialog();
    }
}

void GameScene::FinishDialog()
{
    phase = Phase::PLAY;
    phaseTime = 0.0f;
    world.player.state = world.player.state == PState::HOLD ? PState::NORMAL : world.player.state;
    world.player.holdItem.clear();
    if (pendingAction.type != RequestType::NONE)
    {
        Request action = pendingAction;
        pendingAction = Request{};
        action.pages.clear();
        StartAction(action);
        return;
    }
    if (!pendingItem.empty())
    {
        const std::string item = pendingItem;
        pendingItem.clear();
        StartItemGet(item);
        return;
    }
    if (pendingShrink)
    {
        pendingShrink = false;
        phase = Phase::SHRINK;
        phaseTime = 0.0f;
        world.player.state = PState::SHRINK;
        PlaySfx(Sfx::SHRINK);
        PlayMusic(Music::NONE);
        return;
    }
    if (pendingEnding)
    {
        pendingEnding = false;
        phase = Phase::ENDING;
        phaseTime = 0.0f;
        PlayMusic(Music::ENDING);
        return;
    }
}

void GameScene::StartItemGet(const std::string &item)
{
    const ItemDef *def = FindItem(item);
    phase = Phase::ITEM_GET;
    phaseTime = 0.0f;
    currentItem = item;
    world.player.state = PState::HOLD;
    world.player.dir = Dir::DOWN;
    world.player.holdItem = def ? def->sprite : "gem_r_0";
    ApplyItem(world, item);
    PlaySfx(Sfx::ITEM);
}

void GameScene::StartWarp(const std::string &map, const std::string &spawn)
{
    warpMap = map;
    warpSpawn = spawn;
    phase = Phase::FADE_OUT;
    phaseTime = 0.0f;
    PlaySfx(Sfx::DOOR, 0.5f);
}

// Battles, healing, shopping and the starter pick, once any lead-in dialog is over.
void GameScene::StartAction(const Request &request)
{
    switch (request.type)
    {
    case RequestType::BATTLE:
        StartBattle(request);
        break;
    case RequestType::HEAL:
        HealParty(world);
        PlaySfx(Sfx::HEAL);
        world.flags.insert("healed_once");
        if (request.item == "shop" && !world.party.empty())
        {
            pendingAction = Request{};
            pendingAction.type = RequestType::SHOP;
        }
        OpenDialog(world.party.empty() ? SplitPages("Come back once you have a critter of your own!")
                                       : SplitPages("Your critters are bright-eyed and fully healed!"));
        break;
    case RequestType::SHOP:
        phase = Phase::CHOICE;
        phaseTime = 0.0f;
        choiceCursor = 0;
        choiceQuestion = "Buy " + std::to_string(CAP_BUNDLE) + " caps for " + std::to_string(CAP_PRICE) +
                         " gems? You have " + std::to_string(world.gems) + ".";
        break;
    case RequestType::STARTER:
        phase = Phase::STARTER;
        phaseTime = 0.0f;
        starterCursor = 0;
        afterStarterItem = request.afterItem;
        break;
    default:
        break;
    }
}

void GameScene::StartBattle(const Request &request)
{
    if (!PartyCanFight(world))
    {
        return;
    }
    battleRequest = request;
    battle.Start(world, request);
    phase = Phase::BATTLE_IN;
    phaseTime = 0.0f;
    world.player.moving = false;
    PlayMusic(request.legend || !request.team.empty() ? Music::BOSS : Music::BATTLE);
    PlaySfx(request.legend ? Sfx::BOSS_ROAR : Sfx::SHUTTER, 0.7f);
}

void GameScene::FinishBattle()
{
    world.encounterGrace = 4;
    std::vector<std::string> after;
    switch (battleResult)
    {
    case BattleResult::LOST:
    {
        HealParty(world);
        world.map.name.clear();
        EnterMap(world, world.healMap, world.healSpawn, *renderer);
        after = SplitPages(world.healMap == "tiny"
                               ? "Thimbling: Oh, you poor dears! There, there... all patched up. Rest a "
                                 "moment before you head out again."
                               : "Mira: Pip! You look worn out. Let me see your critters... There, all "
                                 "better! Be careful out there.");
        break;
    }
    case BattleResult::WON:
        if (!battleRequest.winFlag.empty())
        {
            world.flags.insert(battleRequest.winFlag);
            after = battleRequest.winPages;
        }
        break;
    case BattleResult::CAUGHT:
        if (battleRequest.legend)
        {
            world.flags.insert("legend_caught");
            for (Entity &e : world.entities)
            {
                if (e.kind == Kind::LEGEND)
                {
                    e.alive = false;
                    SpawnEffect(world, "poof", e.x, e.y - 12.0f, 0.32f);
                }
            }
            after = SplitPages(
                "Capo: We did it, Pip! Bramblord is ours!|Capo: Look! It coughed up the Sunstone's "
                "light! Go grab it!"
            );
        }
        else if (CaughtCount(world) == SPECIES_COUNT - 1 && !HasFlag(world, "dex_hint"))
        {
            world.flags.insert("dex_hint");
            after = SplitPages("Capo: Seven kinds caught! Only the master of Root Hollow is left.");
        }
        break;
    default:
        break;
    }
    UpdateMusic();
    phase = Phase::FADE_IN;
    phaseTime = 0.0f;
    if (!after.empty())
    {
        world.request = Request{};
        world.request.type = RequestType::DIALOG;
        world.request.pages = after;
    }
}

void GameScene::HandleRequest()
{
    Request request = world.request;
    world.request = Request{};
    switch (request.type)
    {
    case RequestType::DIALOG:
        pendingItem = request.afterItem;
        OpenDialog(request.pages);
        break;
    case RequestType::ITEM:
        StartItemGet(request.item);
        break;
    case RequestType::WARP:
        StartWarp(request.map, request.spawn);
        break;
    case RequestType::SHRINK:
        warpMap = request.map;
        warpSpawn = request.spawn;
        pendingShrink = true;
        if (!request.pages.empty())
        {
            OpenDialog(request.pages);
        }
        else
        {
            FinishDialog();
        }
        break;
    case RequestType::GROW:
        warpMap = request.map;
        warpSpawn = request.spawn;
        phase = Phase::GROW;
        phaseTime = 0.0f;
        world.player.state = PState::GROW;
        PlaySfx(Sfx::GROW);
        PlayMusic(Music::NONE);
        break;
    case RequestType::BATTLE:
    case RequestType::HEAL:
    case RequestType::SHOP:
    case RequestType::STARTER:
        if (request.pages.empty())
        {
            StartAction(request);
        }
        else
        {
            pendingAction = request;
            OpenDialog(request.pages);
        }
        break;
    case RequestType::NONE:
    default:
        break;
    }
}

void GameScene::HandleShots()
{
    for (auto it = pendingShots.begin(); it != pendingShots.end();)
    {
        if (it->second.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        {
            ++it;
            continue;
        }
        resource::RawTexture shot = it->second.get();
        stbi_flip_vertically_on_write(1);
        stbi_write_png(it->first.c_str(), shot.width, shot.height, shot.nrOfChannels,
                       shot.pixels.data(), shot.width * shot.nrOfChannels);
        LOGINFO("Saved %s", it->first.c_str());
        it = pendingShots.erase(it);
    }
    if (!shotTicks.empty())
    {
        if (shotTicks.front() >= 0 && totalTicks >= shotTicks.front())
        {
            const char *prefix = std::getenv("CRITTERCAP_SHOT_PREFIX");
            const std::string path =
                std::string(prefix ? prefix : "shot") + "_" + std::to_string(shotTicks.front()) + ".png";
            pendingShots.emplace_back(path, graphics::Renderer::GetInstance()->RequestScreenshot());
            shotTicks.erase(shotTicks.begin());
            if (shotTicks.empty())
            {
                shotTicks.push_back(-1);
            }
        }
    }
    if (shotTicks.size() == 1 && shotTicks.front() == -1 && pendingShots.empty())
    {
        core::Application::GetInstance()->Quit();
    }
}

void GameScene::UpdateMenu()
{
    const Controls &c = controls;
    if ((c.start.pressed || c.b.pressed) && phaseTime > 0.1f)
    {
        phase = Phase::PLAY;
        phaseTime = 0.0f;
        PlaySfx(Sfx::MENU);
        return;
    }
    // Left/right flips from the team to the dex; inside the dex they move the cursor, and
    // stepping off its left edge goes back to the team.
    const Bool to_dex = menuTab == 0 && (c.left.pressed || c.right.pressed);
    const Bool to_team = menuTab == 1 && c.left.pressed && menuCursor % 4 == 0;
    if (to_dex || to_team)
    {
        menuTab ^= 1;
        menuCursor = 0;
        PlaySfx(Sfx::MENU, 0.4f);
        return;
    }
    if (menuTab == 0)
    {
        const Int32 count = static_cast<Int32>(world.party.size());
        if (count == 0)
        {
            return;
        }
        if (c.up.pressed)
        {
            menuCursor = (menuCursor + count - 1) % count;
            PlaySfx(Sfx::MENU, 0.4f);
        }
        if (c.down.pressed)
        {
            menuCursor = (menuCursor + 1) % count;
            PlaySfx(Sfx::MENU, 0.4f);
        }
        if (c.a.pressed && menuCursor > 0)
        {
            // Make the chosen critter the lead (it walks behind Pip and battles first).
            std::swap(world.party[0], world.party[static_cast<size_t>(menuCursor)]);
            menuCursor = 0;
            PlaySfx(Sfx::SWITCH);
        }
    }
    else
    {
        const Int32 before = menuCursor;
        if (c.left.pressed)
            menuCursor--;
        if (c.right.pressed && menuCursor % 4 != 3)
            menuCursor++;
        if (c.up.pressed)
            menuCursor = (menuCursor + SPECIES_COUNT - 4) % SPECIES_COUNT;
        if (c.down.pressed)
            menuCursor = (menuCursor + 4) % SPECIES_COUNT;
        if (menuCursor != before)
            PlaySfx(Sfx::MENU, 0.4f);
    }
}

void GameScene::Tick()
{
    totalTicks++;
    phaseTime += TICK;
    switch (phase)
    {
    case Phase::TITLE:
    {
        world.time += TICK;
        world.tickCount++;
        // Slow pan across the village behind the title.
        const Float pan = (std::sin(phaseTime * 0.12f) * 0.5f + 0.5f);
        world.cameraX = pan * std::max(0.0f, world.map.PixelWidth() - SCREEN_W);
        world.cameraY = 96.0f;
        brightness = std::min(1.0f, phaseTime * 2.0f);
        if (phaseTime > 0.5f && Confirm(controls))
        {
            PlaySfx(Sfx::MENU);
            NewGame();
        }
        break;
    }
    case Phase::PLAY:
    {
        brightness = 1.0f;
        playTime += TICK;
        if (controls.start.pressed && !world.scroll.active)
        {
            phase = Phase::MENU;
            phaseTime = 0.0f;
            menuCursor = 0;
            PlaySfx(Sfx::MENU);
            break;
        }
        UpdateWorld(world, controls);
        UpdateMusic();
        if (world.request.type != RequestType::NONE)
        {
            HandleRequest();
        }
        break;
    }
    case Phase::DIALOG:
    {
        world.time += TICK;
        const Float before = typed;
        typed = std::min(static_cast<Float>(pages[page].size()), typed + TYPE_SPEED * TICK);
        if (static_cast<Int32>(before) / 3 != static_cast<Int32>(typed) / 3 &&
            typed < static_cast<Float>(pages[page].size()))
        {
            PlaySfx(Sfx::TEXT, 0.35f);
        }
        if (phaseTime > 0.15f && (controls.a.pressed || controls.b.pressed || controls.start.pressed))
        {
            AdvanceDialog();
        }
        break;
    }
    case Phase::ITEM_GET:
    {
        world.time += TICK;
        if (phaseTime >= 1.1f)
        {
            const ItemDef *def = FindItem(currentItem);
            if (currentItem == "sunstone")
            {
                pendingEnding = true;
            }
            OpenDialog(SplitPages(def ? def->text : "You found something!"));
        }
        break;
    }
    case Phase::FADE_OUT:
    {
        brightness = std::max(0.0f, 1.0f - phaseTime / FADE_TIME);
        if (phaseTime >= FADE_TIME)
        {
            EnterMap(world, warpMap, warpSpawn, *renderer);
            if (!world.map.healSpawn.empty())
            {
                world.healMap = world.map.name;
                world.healSpawn = world.map.healSpawn;
            }
            UpdateMusic();
            phase = Phase::FADE_IN;
            phaseTime = 0.0f;
        }
        break;
    }
    case Phase::FADE_IN:
    {
        brightness = std::min(1.0f, phaseTime / FADE_TIME);
        if (phaseTime >= FADE_TIME)
        {
            phase = Phase::PLAY;
            phaseTime = 0.0f;
            brightness = 1.0f;
            if (world.request.type != RequestType::NONE)
            {
                HandleRequest();
            }
        }
        break;
    }
    case Phase::SHRINK:
    {
        world.time += TICK;
        world.tickCount++;
        Player &p = world.player;
        p.scale = std::max(0.12f, 1.0f - phaseTime / 1.3f * 0.88f);
        if (world.tickCount % 5 == 0)
        {
            const Float a = phaseTime * 9.0f;
            SpawnEffect(world, "sparkle", p.x + std::cos(a) * 12.0f, p.y - 8.0f + std::sin(a) * 10.0f, 0.3f);
        }
        UpdateWorld(world, Controls{});
        if (phaseTime >= 1.7f)
        {
            StartWarp(warpMap, warpSpawn);
        }
        break;
    }
    case Phase::GROW:
    {
        world.time += TICK;
        Player &p = world.player;
        p.scale = 1.0f + std::min(1.0f, phaseTime / 1.2f) * 1.6f;
        if (world.tickCount % 5 == 0)
        {
            const Float a = phaseTime * 9.0f;
            SpawnEffect(world, "sparkle", p.x + std::cos(a) * 16.0f, p.y - 12.0f + std::sin(a) * 12.0f, 0.3f);
        }
        UpdateWorld(world, Controls{});
        if (phaseTime >= 1.5f)
        {
            StartWarp(warpMap, warpSpawn);
        }
        break;
    }
    case Phase::BATTLE_IN:
    {
        // Three quick flashes, then the battle fades in.
        world.time += TICK;
        brightness = (static_cast<Int32>(phaseTime * 10.0f) % 2 == 0) ? 1.0f : 0.35f;
        if (phaseTime >= 0.75f)
        {
            phase = Phase::BATTLE;
            phaseTime = 0.0f;
            brightness = 0.0f;
        }
        break;
    }
    case Phase::BATTLE:
    {
        brightness = std::min(1.0f, brightness + TICK * 5.0f);
        playTime += TICK;
        const BattleResult result = battle.Update(world, controls);
        if (result != BattleResult::NONE)
        {
            battleResult = result;
            phase = Phase::BATTLE_OUT;
            phaseTime = 0.0f;
        }
        break;
    }
    case Phase::BATTLE_OUT:
    {
        brightness = std::max(0.0f, 1.0f - phaseTime / 0.4f);
        if (phaseTime >= 0.4f)
        {
            FinishBattle();
        }
        break;
    }
    case Phase::STARTER:
    {
        world.time += TICK;
        world.tickCount++;
        if (controls.left.pressed)
        {
            starterCursor = (starterCursor + 2) % 3;
            PlaySfx(Sfx::MENU, 0.5f);
        }
        if (controls.right.pressed)
        {
            starterCursor = (starterCursor + 1) % 3;
            PlaySfx(Sfx::MENU, 0.5f);
        }
        if (phaseTime > 0.3f && Confirm(controls))
        {
            world.starter = starterCursor;
            world.flags.insert("got_starter");
            AddCritter(world, MakeCritter(starterCursor, 5));
            PlaySfx(Sfx::CAUGHT);
            const std::string name = Species(starterCursor).name;
            pendingItem = afterStarterItem;
            OpenDialog(SplitPages(
                "Pip chose " + name + "!|Bram: A fine pick! " + name +
                " will follow you everywhere now.|Capo: And here's my part of the deal. I can pop off "
                "spare caps for you! Throw one at a tired wild critter, and pop - it's yours!"
            ));
        }
        break;
    }
    case Phase::CHOICE:
    {
        world.time += TICK;
        if (controls.up.pressed || controls.down.pressed)
        {
            choiceCursor ^= 1;
            PlaySfx(Sfx::MENU, 0.5f);
        }
        if (controls.b.pressed)
        {
            choiceCursor = 1;
        }
        if (phaseTime > 0.2f && (Confirm(controls) || controls.b.pressed))
        {
            if (choiceCursor == 0 && world.gems >= CAP_PRICE)
            {
                world.gems -= CAP_PRICE;
                world.caps = std::min(99, world.caps + CAP_BUNDLE);
                PlaySfx(Sfx::GEM);
                OpenDialog(SplitPages("Here are your caps! Happy catching!"));
            }
            else if (choiceCursor == 0)
            {
                PlaySfx(Sfx::THUD, 0.6f);
                OpenDialog(SplitPages("Oh dear, you don't have enough gems. Trainers pay out when you "
                                      "beat them, and pots sometimes hide a few."));
            }
            else
            {
                PlaySfx(Sfx::MENU);
                OpenDialog(SplitPages("Come back any time!"));
            }
        }
        break;
    }
    case Phase::MENU:
        UpdateMenu();
        break;
    case Phase::ENDING:
    {
        brightness = 1.0f;
        world.time += TICK;
        world.tickCount++;
        if (phaseTime > 8.0f && Confirm(controls))
        {
            phase = Phase::TITLE;
            phaseTime = 0.0f;
            world.flags.clear();
            world.map.name.clear();
            EnterMap(world, "village", "start", *renderer);
            PlayMusic(Music::TITLE);
        }
        break;
    }
    }
}

void GameScene::Render()
{
    if (!shotTicks.empty() || !pendingShots.empty())
    {
        HandleShots();
    }
    renderer->BeginFrame(phase == Phase::ENDING ? 1.0f : brightness);
    if (phase == Phase::ENDING)
    {
        RenderEnding();
        return;
    }
    if (phase == Phase::BATTLE || phase == Phase::BATTLE_OUT)
    {
        battle.Render(world, *renderer);
        return;
    }
    RenderWorld(world, *renderer);
    if (phase == Phase::TITLE)
    {
        RenderTitle();
        return;
    }
    if (phase != Phase::BATTLE_IN)
    {
        RenderHud(world, *renderer);
    }
    if (phase == Phase::DIALOG)
    {
        RenderDialog();
    }
    if (phase == Phase::MENU)
    {
        RenderMenu();
    }
    if (phase == Phase::STARTER)
    {
        RenderStarter();
    }
    if (phase == Phase::CHOICE)
    {
        RenderChoice();
    }
}

void GameScene::RenderDialog()
{
    Renderer2D &r = *renderer;
    const Float box_y = world.player.y - world.cameraY > 100.0f ? 8.0f : 104.0f;
    r.Box(8.0f, box_y, 224.0f, 48.0f);
    const std::string &text = pages[page];
    const auto shown = static_cast<size_t>(typed);
    Float y = box_y + 7.0f;
    size_t start = 0;
    Bool first_line = true;
    while (start <= text.size())
    {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        if (start >= shown)
        {
            break;
        }
        std::string line = text.substr(start, std::min(end, shown) - start);
        Float x = 16.0f;
        // A leading "Name:" is drawn as the speaker in gold.
        if (first_line)
        {
            const size_t colon = line.find(':');
            const size_t full_colon = text.find(':');
            if (colon != std::string::npos && colon < 12 && full_colon == colon &&
                text.substr(0, colon).find(' ') == std::string::npos)
            {
                r.Text(line.substr(0, colon + 1), x, y, GOLD_TEXT);
                x += Renderer2D::MeasureText(line.substr(0, colon + 1));
                line = line.substr(colon + 1);
            }
        }
        r.Text(line, x, y);
        y += 12.0f;
        first_line = false;
        start = end + 1;
    }
    if (shown >= text.size())
    {
        DrawOptions o;
        o.screen = true;
        r.Draw("arrow_" + std::to_string((world.tickCount / 15) % 2), 220.0f, box_y + 38.0f, o);
    }
}

void GameScene::RenderTitle()
{
    Renderer2D &r = *renderer;
    r.FillRect(0.0f, 0.0f, SCREEN_W, SCREEN_H, {0.05f, 0.04f, 0.12f, 0.55f});
    DrawOptions o;
    o.screen = true;
    const Float bob = std::sin(phaseTime * 2.0f) * 2.0f;
    r.Draw("capo_big", 120.0f, 92.0f + bob, o);
    // The three starters bounce around the hat.
    DrawOptions s = o;
    s.scale = 2.0f;
    for (Int32 i = 0; i < 3; ++i)
    {
        const Float x = 64.0f + static_cast<Float>(i) * 56.0f;
        const Float hop = std::max(0.0f, std::sin(phaseTime * 4.0f + static_cast<Float>(i) * 2.1f)) * 6.0f;
        if (i == 1)
        {
            continue;
        }
        r.Draw(SpeciesSprite(i, false, static_cast<Int32>(phaseTime * 3.0f)), x, 96.0f - hop, s);
    }
    r.Draw(SpeciesSprite(1, false, static_cast<Int32>(phaseTime * 3.0f)), 120.0f, 104.0f, o);
    r.TextCentered("CRITTER CAP", 121.0f, 15.0f, {0.2f, 0.1f, 0.05f, 1.0f}, 3.0f);
    r.TextCentered("CRITTER CAP", 120.0f, 14.0f, GOLD_TEXT, 3.0f);
    r.TextCentered("Pip's Pocket Critters", 120.0f, 44.0f, SOFT_TEXT);
    if (static_cast<Int32>(phaseTime * 2.0f) % 2 == 0)
    {
        r.TextCentered("Press Enter", 120.0f, 112.0f);
    }
    r.TextCentered("Arrows move  Z talk  X run", 120.0f, 130.0f, SOFT_TEXT);
    r.TextCentered("Enter: team and Critterdex", 120.0f, 142.0f, SOFT_TEXT);
}

void GameScene::RenderStarter()
{
    Renderer2D &r = *renderer;
    DrawOptions o;
    o.screen = true;
    r.FillRect(0.0f, 0.0f, SCREEN_W, SCREEN_H, {0.0f, 0.0f, 0.05f, 0.55f});
    r.Box(8.0f, 16.0f, 224.0f, 128.0f);
    r.TextCentered("Choose your partner!", 120.0f, 24.0f, GOLD_TEXT);
    for (Int32 i = 0; i < 3; ++i)
    {
        const Float x = 48.0f + static_cast<Float>(i) * 72.0f;
        const Bool selected = i == starterCursor;
        if (selected)
        {
            r.FillRect(x - 28.0f, 38.0f, 56.0f, 64.0f, {1.0f, 1.0f, 1.0f, 0.14f});
        }
        DrawOptions s = o;
        s.scale = selected ? 3.0f : 2.0f;
        const Float hop = selected ? std::max(0.0f, std::sin(phaseTime * 6.0f)) * 4.0f : 0.0f;
        r.Draw("pad_grass", x, 92.0f, o);
        r.Draw(SpeciesSprite(i, false, selected ? static_cast<Int32>(phaseTime * 4.0f) : 0), x, 90.0f - hop, s);
        r.TextCentered(Species(i).name, x, 104.0f, selected ? GOLD_TEXT : Color{});
    }
    const SpeciesDef &def = Species(starterCursor);
    r.TextCentered(std::string(TypeName(def.type)) + " type", 120.0f, 116.0f, SOFT_TEXT);
    const std::vector<std::string> lines = Wrap(def.dex, 34);
    r.TextCentered(lines.empty() ? "" : lines[0], 120.0f, 128.0f, SOFT_TEXT);
}

void GameScene::RenderChoice()
{
    Renderer2D &r = *renderer;
    r.Box(8.0f, 104.0f, 224.0f, 48.0f);
    const std::vector<std::string> lines = Wrap(choiceQuestion, 22);
    Float y = 111.0f;
    for (size_t i = 0; i < lines.size() && i < 3; ++i)
    {
        r.Text(lines[i], 16.0f, y);
        y += 12.0f;
    }
    r.Box(168.0f, 64.0f, 64.0f, 40.0f);
    r.Text("YES", 188.0f, 72.0f);
    r.Text("NO", 188.0f, 86.0f);
    r.Text(">", 178.0f, choiceCursor == 0 ? 72.0f : 86.0f, GOLD_TEXT);
}

void GameScene::RenderMenu()
{
    Renderer2D &r = *renderer;
    DrawOptions o;
    o.screen = true;
    r.FillRect(0.0f, 0.0f, SCREEN_W, SCREEN_H, {0.0f, 0.0f, 0.05f, 0.6f});
    r.Box(8.0f, 8.0f, 224.0f, 144.0f);
    r.Text("TEAM", 24.0f, 16.0f, menuTab == 0 ? GOLD_TEXT : DIM_TEXT);
    r.Text("CRITTERDEX", 64.0f, 16.0f, menuTab == 1 ? GOLD_TEXT : DIM_TEXT);
    char buffer[64];
    const auto seconds = static_cast<Int32>(playTime);
    std::snprintf(buffer, sizeof(buffer), "%d:%02d", seconds / 60, seconds % 60);
    r.Text(buffer, 220.0f - Renderer2D::MeasureText(buffer), 16.0f, SOFT_TEXT);
    r.FillRect(16.0f, 27.0f, 208.0f, 1.0f, {0.5f, 0.55f, 0.8f, 1.0f});
    const Int32 frame = (world.tickCount / 15) % 2;
    if (menuTab == 0)
    {
        if (world.party.empty())
        {
            r.TextCentered("No critters yet!", 120.0f, 70.0f, SOFT_TEXT);
            r.TextCentered("Elder Bram is in the village square.", 120.0f, 84.0f, SOFT_TEXT);
        }
        for (size_t i = 0; i < world.party.size(); ++i)
        {
            const Critter &c = world.party[i];
            const Float y = 32.0f + static_cast<Float>(i) * 24.0f;
            const Bool selected = static_cast<Int32>(i) == menuCursor;
            if (selected)
            {
                r.FillRect(16.0f, y - 1.0f, 208.0f, 23.0f, {1.0f, 1.0f, 1.0f, 0.12f});
            }
            r.Draw(SpeciesSprite(c.species, false, selected ? frame : 0), 32.0f, y + 18.0f, o);
            r.Text(c.Name(), 48.0f, y + 1.0f, c.Fainted() ? DIM_TEXT : Color{});
            std::snprintf(buffer, sizeof(buffer), "Lv%d %s", c.level, TypeName(c.Def().type));
            r.Text(buffer, 120.0f, y + 1.0f, SOFT_TEXT);
            const Float ratio = static_cast<Float>(c.hp) / static_cast<Float>(c.MaxHp());
            r.FillRect(48.0f, y + 13.0f, 82.0f, 5.0f, {0.1f, 0.08f, 0.15f, 1.0f});
            r.FillRect(49.0f, y + 14.0f, std::round(80.0f * ratio), 3.0f,
                       ratio > 0.5f ? Color{0.35f, 0.85f, 0.4f, 1.0f}
                                    : (ratio > 0.2f ? Color{0.98f, 0.8f, 0.25f, 1.0f} : Color{0.95f, 0.3f, 0.25f, 1.0f}));
            std::snprintf(buffer, sizeof(buffer), "%d/%d", c.hp, c.MaxHp());
            r.Text(buffer, 136.0f, y + 11.0f);
            if (i == 0)
            {
                r.Text("LEAD", 190.0f, y + 11.0f, GOLD_TEXT);
            }
        }
        r.Draw("hud_cap", 18.0f, 131.0f, o);
        std::snprintf(buffer, sizeof(buffer), "x%d", world.caps);
        r.Text(buffer, 29.0f, 130.0f);
        r.Draw("hud_gem", 60.0f, 130.0f, o);
        std::snprintf(buffer, sizeof(buffer), "%d", world.gems);
        r.Text(buffer, 70.0f, 130.0f);
        if (!world.box.empty())
        {
            std::snprintf(buffer, sizeof(buffer), "Pocket: %zu", world.box.size());
            r.Text(buffer, 100.0f, 130.0f, SOFT_TEXT);
        }
        r.Text("Z: make lead", 150.0f, 130.0f, SOFT_TEXT);
        r.TextCentered("Right: Critterdex   X or Enter: close", 120.0f, 141.0f, DIM_TEXT);
        return;
    }
    // Critterdex: 4x2 grid and a description of the selected entry.
    for (Int32 i = 0; i < SPECIES_COUNT; ++i)
    {
        const Float x = 40.0f + static_cast<Float>(i % 4) * 53.0f;
        const Float y = 58.0f + static_cast<Float>(i / 4) * 34.0f;
        const auto index = static_cast<size_t>(i);
        if (i == menuCursor)
        {
            r.FillRect(x - 22.0f, y - 26.0f, 44.0f, 32.0f, {1.0f, 1.0f, 1.0f, 0.14f});
        }
        if (world.seen[index])
        {
            DrawOptions s = o;
            if (!world.caught[index])
            {
                s.tint = {0.1f, 0.1f, 0.2f, 1.0f};
            }
            s.scale = i == LEGEND_SPECIES ? 0.75f : 1.0f;
            r.Draw(SpeciesSprite(i, false, i == menuCursor ? frame : 0), x, y - 4.0f, s);
            if (world.caught[index])
            {
                r.Draw("hud_cap", x + 10.0f, y - 24.0f, o);
            }
        }
        else
        {
            r.TextCentered("?", x, y - 14.0f, DIM_TEXT);
        }
        std::snprintf(buffer, sizeof(buffer), "%02d", i + 1);
        r.TextCentered(buffer, x, y - 1.0f, DIM_TEXT);
    }
    std::snprintf(buffer, sizeof(buffer), "Caught %d/%d", CaughtCount(world), SPECIES_COUNT);
    r.Text(buffer, 24.0f, 30.0f, SOFT_TEXT);
    const auto sel = static_cast<size_t>(menuCursor);
    const SpeciesDef &def = Species(menuCursor);
    if (world.caught[sel])
    {
        r.Text(std::string(def.name) + "  " + TypeName(def.type), 24.0f, 104.0f, GOLD_TEXT);
        const std::vector<std::string> lines = Wrap(def.dex, 34);
        for (size_t i = 0; i < lines.size() && i < 2; ++i)
        {
            r.Text(lines[i], 24.0f, 116.0f + static_cast<Float>(i) * 11.0f);
        }
    }
    else if (world.seen[sel])
    {
        r.Text(def.name, 24.0f, 104.0f, GOLD_TEXT);
        r.Text("Seen, but not caught yet.", 24.0f, 116.0f, SOFT_TEXT);
    }
    else
    {
        r.Text("???", 24.0f, 104.0f, DIM_TEXT);
        r.Text("Not seen yet.", 24.0f, 116.0f, SOFT_TEXT);
    }
}

void GameScene::RenderEnding()
{
    Renderer2D &r = *renderer;
    r.FillRect(0.0f, 0.0f, SCREEN_W, SCREEN_H, {0.07f, 0.06f, 0.16f, 1.0f});
    DrawOptions o;
    o.screen = true;
    const Float t = phaseTime;
    for (Int32 i = 0; i < 14; ++i)
    {
        const Float a = t * 0.4f + static_cast<Float>(i) * 0.449f;
        r.Draw("sparkle_" + std::to_string((world.tickCount / 6 + i) % 3),
               120.0f + std::cos(a) * (60.0f + 10.0f * std::sin(t + static_cast<Float>(i))),
               60.0f + std::sin(a) * 20.0f, o);
    }
    r.Draw("sunstone_" + std::to_string((world.tickCount / 12) % 2), 120.0f, 58.0f, o);
    r.Draw("capo_small", 106.0f, 78.0f + std::sin(t * 3.0f) * 1.5f, o);
    r.Draw("hero_down_0", 126.0f, 80.0f, o);
    // The team lines up under Pip.
    for (size_t i = 0; i < world.party.size(); ++i)
    {
        const Float x = 120.0f + (static_cast<Float>(i) - static_cast<Float>(world.party.size() - 1) * 0.5f) * 22.0f;
        const Float hop = std::max(0.0f, std::sin(t * 5.0f + static_cast<Float>(i))) * 3.0f;
        r.Draw(SpeciesSprite(world.party[i].species, false, (world.tickCount / 15) % 2), x, 98.0f - hop, o);
    }
    const char *lines[] = {
        "The Sunstone shines again,",
        "and the critters of the Rootwood",
        "are calm and happy once more.",
        "",
        "Thanks for playing CRITTER CAP!",
    };
    Float y = 104.0f;
    for (Int32 i = 0; i < 5; ++i)
    {
        if (t > 0.8f + static_cast<Float>(i) * 0.7f)
        {
            r.TextCentered(lines[i], 120.0f, y, i >= 4 ? GOLD_TEXT : Color{});
        }
        y += 10.0f;
    }
    r.TextCentered("CRITTER CAP", 120.0f, 6.0f, GOLD_TEXT, 2.0f);
    if (t > 5.0f)
    {
        char buffer[64];
        const auto seconds = static_cast<Int32>(playTime);
        std::snprintf(buffer, sizeof(buffer), "Time %d:%02d   Critterdex %d/%d", seconds / 60, seconds % 60,
                      CaughtCount(world), SPECIES_COUNT);
        r.TextCentered(buffer, 120.0f, 24.0f, SOFT_TEXT);
    }
}
} // namespace crittercap
