#pragma once
#include "game/critters.h"
#include "game/map.h"
#include "input/controls.h"
#include "math/random.h"

#include "compiled/warning_off.h"
#include <array>
#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "compiled/warning_on.h"

namespace crittercap
{
constexpr Float TICK = 1.0f / 60.0f;

enum class Dir : UInt8
{
    DOWN,
    UP,
    LEFT,
    RIGHT
};

enum class Kind : UInt8
{
    BUSH,
    POT,
    CHEST,
    SIGN,
    NPC,
    DOOR,
    BLOCK,
    SWITCH,
    TORCH,
    PICKUP,
    LEGEND,
    STUMP,
    PORTAL,
    WARP,
    TRIGGER,
    EFFECT
};

struct Entity
{
    Kind kind = Kind::EFFECT;
    Bool alive = true;
    // Waiting for its "showIf" condition (e.g. a chest that appears when a switch is pressed).
    Bool hidden = false;
    // Position of the pivot: bottom-centre for actors and objects.
    Float x = 0.0f;
    Float y = 0.0f;
    Float vx = 0.0f;
    Float vy = 0.0f;
    Float z = 0.0f;
    Float vz = 0.0f;
    // Collision box is [x - hw, x + hw] x [y - hh * 2, y].
    Float hw = 8.0f;
    Float hh = 8.0f;
    Dir dir = Dir::DOWN;
    Int32 state = 0;
    Float timer = 0.0f;
    Float anim = 0.0f;
    Float homeX = 0.0f;
    Float homeY = 0.0f;
    Float life = -1.0f;
    Int32 value = 0;
    Int32 room = -1;
    // Persistent id ("map:objectid") used for opened chests, beaten trainers, ...
    std::string flag;
    // Kind-specific variant: npc name, door kind, item id, effect sprite.
    std::string variant;
    std::unordered_map<std::string, std::string> props;

    const std::string &Prop(const std::string &key) const
    {
        static const std::string empty;
        const auto it = props.find(key);
        return it == props.end() ? empty : it->second;
    }
};

enum class PState : UInt8
{
    NORMAL,
    HOLD,
    SHRINK,
    GROW
};

struct TrailPoint
{
    Float x = 0.0f;
    Float y = 0.0f;
    Dir dir = Dir::DOWN;
};

struct Player
{
    Float x = 0.0f;
    Float y = 0.0f;
    Dir dir = Dir::DOWN;
    PState state = PState::NORMAL;
    Float t = 0.0f;
    Float anim = 0.0f;
    Bool moving = false;
    Bool running = false;
    Float pushT = 0.0f;
    Float scale = 1.0f;
    Int32 tileX = -1;
    Int32 tileY = -1;
    std::string holdItem;
    // Where the player has been, newest first; the lead critter follows along it.
    std::deque<TrailPoint> trail;
};

enum class RequestType : UInt8
{
    NONE,
    DIALOG,
    ITEM,
    WARP,
    SHRINK,
    GROW,
    BATTLE,
    HEAL,
    SHOP,
    STARTER
};

// Things the world asks the scene to do (show text, change map, start a battle, ...).
// For BATTLE/HEAL/SHOP/STARTER the pages are shown first.
struct Request
{
    RequestType type = RequestType::NONE;
    std::vector<std::string> pages;
    std::string item;
    std::string map;
    std::string spawn;
    // Item handed over after the dialog closes (an NPC giving something).
    std::string afterItem;
    // Battles: a wild critter, or a trainer's team ("blob_g:5,ant:6").
    Critter wild;
    std::string trainer;
    std::string trainerSprite;
    std::string team;
    Bool legend = false;
    // Flag set when the battle is won and the text shown after it.
    std::string winFlag;
    std::vector<std::string> winPages;
    Int32 prize = 0;
};

struct RoomScroll
{
    Bool active = false;
    Float t = 0.0f;
    Float fromX = 0.0f;
    Float fromY = 0.0f;
    Float toX = 0.0f;
    Float toY = 0.0f;
    Float playerVX = 0.0f;
    Float playerVY = 0.0f;
};

struct World
{
    Map map;
    TileInfo tileInfo;
    std::vector<Entity> entities;
    // Entities spawned during a tick; merged after the update so references stay valid.
    std::vector<Entity> spawned;
    Player player;
    std::unordered_set<std::string> flags;
    std::vector<Critter> party;
    // Critters caught while the party is full.
    std::vector<Critter> box;
    std::array<Bool, SPECIES_COUNT> seen{};
    std::array<Bool, SPECIES_COUNT> caught{};
    Int32 starter = -1;
    Int32 caps = 0;
    Int32 gems = 0;
    Int32 keys = 0;
    Bool bigKey = false;
    // Where the player wakes up after every critter has fainted.
    std::string healMap = "village";
    std::string healSpawn = "start";
    // Steps that must pass before the next wild encounter.
    Int32 encounterGrace = 0;
    Float time = 0.0f;
    Float cameraX = 0.0f;
    Float cameraY = 0.0f;
    Int32 roomX = 0;
    Int32 roomY = 0;
    RoomScroll scroll;
    Float shake = 0.0f;
    Request request;
    std::string prompt;
    std::string spawnMap;
    std::string spawnName;
    math::Random rng{0xC0FFEEu};
    Int32 tickCount = 0;
    // Test hook: no wild encounters (CRITTERCAP_PEACE).
    Bool peaceful = false;
};

// world.cpp
Bool HasFlag(const World &world, const std::string &flag);
Bool CheckCondition(const World &world, const std::string &condition);
void EnterMap(World &world, const std::string &map_name, const std::string &spawn, Renderer2D &renderer);
void UpdateWorld(World &world, const Controls &controls);
void ApplyItem(World &world, const std::string &item);
void UpdateCamera(World &world, Bool snap);
void PlacePlayerAtSpawn(World &world, const std::string &spawn);
Entity &SpawnEffect(World &world, const std::string &sprite, Float x, Float y, Float duration);
Bool BoxBlocked(const World &world, Float x0, Float y0, Float x1, Float y1, const Entity *ignore, Bool for_player);
Int32 RoomIndexAt(const World &world, Float x, Float y);
// Adds a caught critter to the party (or the box when the party is full).
// Returns true when it went into the party.
Bool AddCritter(World &world, const Critter &critter);
void HealParty(World &world);
Bool PartyCanFight(const World &world);
Bool PartyHasType(const World &world, Type type);
Int32 CaughtCount(const World &world);

// render_world.cpp
void RenderWorld(World &world, Renderer2D &renderer);

inline Float DirX(const Dir d)
{
    return d == Dir::LEFT ? -1.0f : (d == Dir::RIGHT ? 1.0f : 0.0f);
}
inline Float DirY(const Dir d)
{
    return d == Dir::UP ? -1.0f : (d == Dir::DOWN ? 1.0f : 0.0f);
}
} // namespace crittercap
