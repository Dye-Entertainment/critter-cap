#include "game/world.h"

#include "audio/sound.h"
#include "game/text.h"
#include "utils/logger.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include "compiled/warning_on.h"

namespace crittercap
{
namespace
{
constexpr Float ROOM_W = SCREEN_W;
constexpr Float ROOM_H = SCREEN_H;
constexpr Float PLAYER_HW = 5.0f;
constexpr Float PLAYER_H = 8.0f;
constexpr Float WALK_SPEED = 1.1f;
constexpr Float RUN_SPEED = 1.9f;
constexpr size_t TRAIL_LENGTH = 64;
// Chance per new tile stepped on.
constexpr Float GRASS_ENCOUNTER = 0.11f;
constexpr Float CAVE_ENCOUNTER = 0.045f;

struct Box
{
    Float x0, y0, x1, y1;
};

Box EntityBox(const Entity &e)
{
    return {e.x - e.hw, e.y - e.hh * 2.0f, e.x + e.hw, e.y};
}

Box PlayerBox(const Player &p)
{
    return {p.x - PLAYER_HW, p.y - PLAYER_H, p.x + PLAYER_HW, p.y};
}

Bool Overlaps(const Box &a, const Box &b)
{
    return a.x0 < b.x1 && a.x1 > b.x0 && a.y0 < b.y1 && a.y1 > b.y0;
}

Bool BlocksMovement(const Entity &e)
{
    if (!e.alive || e.hidden)
    {
        return false;
    }
    switch (e.kind)
    {
    case Kind::BUSH:
    case Kind::POT:
    case Kind::CHEST:
    case Kind::SIGN:
    case Kind::NPC:
    case Kind::BLOCK:
    case Kind::TORCH:
    case Kind::LEGEND:
        return true;
    case Kind::DOOR:
        return e.state == 0;
    default:
        return false;
    }
}

void SpawnDrop(World &world, const Float x, const Float y)
{
    const Float r = world.rng.NextUnit();
    std::string variant;
    Int32 value = 1;
    if (r < 0.3f)
    {
        variant = "gem_g";
    }
    else if (r < 0.42f)
    {
        variant = "gem_b";
        value = 5;
    }
    else if (r < 0.55f)
    {
        variant = "cap";
    }
    if (variant.empty())
    {
        return;
    }
    Entity e;
    e.kind = Kind::PICKUP;
    e.variant = variant;
    e.value = value;
    e.x = x;
    e.y = y;
    e.hw = 4.0f;
    e.hh = 4.0f;
    e.vz = 2.2f;
    e.life = 9.0f;
    world.spawned.push_back(e);
}

void CutBush(World &world, Entity &e)
{
    e.alive = false;
    world.flags.insert(e.flag);
    const Float ex = e.x;
    const Float ey = e.y;
    PlaySfx(Sfx::CUT, 0.8f, world.rng.NextRange(0.9f, 1.1f));
    for (Int32 i = 0; i < 6; ++i)
    {
        Entity &leaf = SpawnEffect(world, "leaf", ex, ey - 8.0f, 0.5f);
        const Float a = static_cast<Float>(i) * 1.047f + world.rng.NextRange(-0.3f, 0.3f);
        leaf.vx = std::cos(a) * 1.1f;
        leaf.vy = std::sin(a) * 0.8f - 0.4f;
    }
    SpawnDrop(world, ex, ey - 2.0f);
}

void BreakPot(World &world, Entity &e)
{
    e.alive = false;
    world.flags.insert(e.flag);
    PlaySfx(Sfx::POT, 0.9f, world.rng.NextRange(0.9f, 1.1f));
    for (Int32 i = 0; i < 5; ++i)
    {
        Entity &shard = SpawnEffect(world, "shard", e.x, e.y - 6.0f, 0.45f);
        const Float a = static_cast<Float>(i) * 1.256f;
        shard.vx = std::cos(a) * 1.2f;
        shard.vy = std::sin(a) * 0.9f - 0.3f;
    }
    if (!e.Prop("drop").empty())
    {
        Entity drop;
        drop.kind = Kind::PICKUP;
        drop.variant = e.Prop("drop");
        drop.x = e.x;
        drop.y = e.y - 2.0f;
        drop.hw = 4.0f;
        drop.hh = 4.0f;
        drop.vz = 2.0f;
        drop.life = 10.0f;
        drop.value = drop.variant == "gem_b" ? 5 : (drop.variant == "gem_r" ? 20 : 1);
        world.spawned.push_back(drop);
    }
    else
    {
        SpawnDrop(world, e.x, e.y - 2.0f);
    }
}

Entity *BlockingEntity(World &world, const Box &box, Bool for_player)
{
    for (Entity &e : world.entities)
    {
        if (!BlocksMovement(e))
        {
            continue;
        }
        if (!for_player && e.kind == Kind::NPC)
        {
            continue;
        }
        if (Overlaps(box, EntityBox(e)))
        {
            return &e;
        }
    }
    return nullptr;
}

Bool PlayerBlocked(const World &world, const Float x, const Float y)
{
    return BoxBlocked(world, x - PLAYER_HW, y - PLAYER_H, x + PLAYER_HW, y, nullptr, true);
}

void TryUnlock(World &world, Entity &door)
{
    const std::string &kind = door.variant;
    if (kind == "locked" && world.keys > 0)
    {
        world.keys--;
    }
    else if (kind == "boss" && world.bigKey)
    {
    }
    else
    {
        if (world.tickCount % 90 == 0)
        {
            world.request.type = RequestType::DIALOG;
            world.request.pages = {kind == "boss"
                                       ? "A huge door with a big keyhole. You'll need the Big Key."
                                       : (kind == "locked" ? "It's locked. A small key would open it."
                                                           : "")};
            if (kind == "shutter")
            {
                world.request.type = RequestType::NONE;
            }
        }
        return;
    }
    door.alive = false;
    world.flags.insert(door.flag);
    PlaySfx(Sfx::UNLOCK);
    SpawnEffect(world, "poof", door.x, door.y - door.hh, 0.32f);
}

// Moves the player one axis at a time with Zelda-style corner nudging.
Bool MovePlayerAxis(World &world, const Float dx, const Float dy)
{
    Player &p = world.player;
    const Float steps = std::ceil(std::max(std::abs(dx), std::abs(dy)));
    if (steps <= 0.0f)
    {
        return true;
    }
    const Float sx = dx / steps;
    const Float sy = dy / steps;
    for (Int32 i = 0; i < static_cast<Int32>(steps); ++i)
    {
        if (!PlayerBlocked(world, p.x + sx, p.y + sy))
        {
            p.x += sx;
            p.y += sy;
            continue;
        }
        // Slide around corners: if a small perpendicular shift frees the way, nudge toward it.
        for (Int32 off = 1; off <= 6; ++off)
        {
            for (const Int32 sign : {-1, 1})
            {
                const Float ox = sx == 0.0f ? static_cast<Float>(off * sign) : 0.0f;
                const Float oy = sy == 0.0f ? static_cast<Float>(off * sign) : 0.0f;
                if (!PlayerBlocked(world, p.x + ox, p.y + oy) &&
                    !PlayerBlocked(world, p.x + ox + sx, p.y + oy + sy))
                {
                    const Float nx = sx == 0.0f ? static_cast<Float>(sign) * 0.75f : 0.0f;
                    const Float ny = sy == 0.0f ? static_cast<Float>(sign) * 0.75f : 0.0f;
                    if (!PlayerBlocked(world, p.x + nx, p.y + ny))
                    {
                        p.x += nx;
                        p.y += ny;
                    }
                    return false;
                }
            }
        }
        return false;
    }
    return true;
}

void HandlePush(World &world, const Float dx, const Float dy)
{
    Player &p = world.player;
    const Bool cardinal = (dx == 0.0f) != (dy == 0.0f);
    if (!cardinal)
    {
        p.pushT = 0.0f;
        return;
    }
    const Float px = dx > 0 ? 1.0f : (dx < 0 ? -1.0f : 0.0f);
    const Float py = dy > 0 ? 1.0f : (dy < 0 ? -1.0f : 0.0f);
    const Box probe{p.x - PLAYER_HW + px * 2.0f, p.y - PLAYER_H + py * 2.0f,
                    p.x + PLAYER_HW + px * 2.0f, p.y + py * 2.0f};
    Entity *e = BlockingEntity(world, probe, true);
    if (!e)
    {
        p.pushT = 0.0f;
        return;
    }
    if (e->kind == Kind::DOOR)
    {
        TryUnlock(world, *e);
        return;
    }
    if (e->kind != Kind::BLOCK || e->state != 0)
    {
        p.pushT = 0.0f;
        return;
    }
    p.pushT += TICK;
    if (p.pushT < 0.35f)
    {
        return;
    }
    p.pushT = 0.0f;
    const Float tx = e->x + px * TILE;
    const Float ty = e->y + py * TILE;
    const Box dest{tx - 7.5f, ty - 15.5f, tx + 7.5f, ty - 0.5f};
    if (BoxHitsTiles(world.map, dest.x0, dest.y0, dest.x1, dest.y1))
    {
        return;
    }
    for (const Entity &other : world.entities)
    {
        if (&other != e && BlocksMovement(other) && Overlaps(dest, EntityBox(other)))
        {
            return;
        }
    }
    if (world.map.rooms && RoomIndexAt(world, tx, ty - 8.0f) != e->room)
    {
        return;
    }
    e->state = 1;
    e->timer = 0.0f;
    e->vx = px * TILE / 16.0f;
    e->vy = py * TILE / 16.0f;
    PlaySfx(Sfx::PUSH);
}

Box InteractProbe(const Player &p)
{
    const Float cx = p.x + DirX(p.dir) * 10.0f;
    const Float cy = p.y - 4.0f + DirY(p.dir) * 10.0f;
    return {cx - 5.0f, cy - 5.0f, cx + 5.0f, cy + 5.0f};
}

Entity *FindInteractable(World &world, std::string &prompt)
{
    const Player &p = world.player;
    const Box probe = InteractProbe(p);
    for (Entity &e : world.entities)
    {
        if (!e.alive || e.hidden)
        {
            continue;
        }
        if (e.kind == Kind::STUMP || e.kind == Kind::PORTAL)
        {
            const Box zone = EntityBox(e);
            if (p.x > zone.x0 && p.x < zone.x1 && p.y - 4.0f > zone.y0 && p.y - 4.0f < zone.y1)
            {
                if (e.kind == Kind::STUMP && world.party.empty())
                {
                    continue;
                }
                prompt = e.kind == Kind::STUMP ? "Shrink" : "Grow";
                return &e;
            }
            continue;
        }
        const Bool usable = e.kind == Kind::NPC || e.kind == Kind::SIGN || e.kind == Kind::BUSH ||
                            e.kind == Kind::POT || e.kind == Kind::LEGEND ||
                            (e.kind == Kind::CHEST && e.state == 0);
        if (!usable)
        {
            continue;
        }
        Box target = EntityBox(e);
        if (e.kind == Kind::NPC)
        {
            target.y0 -= 6.0f;
        }
        if (!Overlaps(probe, target))
        {
            continue;
        }
        if (e.kind == Kind::CHEST && p.dir != Dir::UP)
        {
            continue;
        }
        switch (e.kind)
        {
        case Kind::NPC:
            prompt = "Talk";
            break;
        case Kind::SIGN:
            prompt = "Read";
            break;
        case Kind::BUSH:
            prompt = PartyHasType(world, Type::LEAF) ? "Chomp" : "Look";
            break;
        case Kind::POT:
            prompt = "Lift";
            break;
        case Kind::LEGEND:
            prompt = "Look";
            break;
        default:
            prompt = "Open";
            break;
        }
        return &e;
    }
    return nullptr;
}

// Replaces "rival" in a trainer's team with the starter that beats the player's own.
std::string ResolveTeam(const World &world, const std::string &team)
{
    static const Char *RIVAL_FOR[3] = {"blob_r", "blob_g", "blob_b"};
    std::string out = team;
    const size_t at = out.find("rival");
    if (at != std::string::npos)
    {
        const Int32 starter = world.starter >= 0 && world.starter < 3 ? world.starter : 0;
        out.replace(at, 5, RIVAL_FOR[starter]);
    }
    return out;
}

void TalkToNpc(World &world, Entity &e)
{
    Player &p = world.player;
    Request &r = world.request;
    // Turn to face the player.
    const Float dx = p.x - e.x;
    const Float dy = p.y - e.y;
    if (std::abs(dx) > std::abs(dy))
    {
        e.dir = dx < 0 ? Dir::LEFT : Dir::RIGHT;
    }
    else
    {
        e.dir = dy < 0 ? Dir::UP : Dir::DOWN;
    }
    r.type = RequestType::DIALOG;
    const std::string &after_if = e.Prop("afterIf");
    const Bool after = !after_if.empty() && CheckCondition(world, after_if);
    r.pages = SplitPages(after ? e.Prop("after") : e.Prop("text"));
    if (!after)
    {
        const std::string &give = e.Prop("give");
        if (!give.empty() && !HasFlag(world, "got_" + give))
        {
            r.afterItem = give;
        }
        if (e.Prop("starter") == "true")
        {
            r.type = RequestType::STARTER;
        }
    }
    if (e.Prop("heal") == "true")
    {
        r.type = RequestType::HEAL;
        // Healers that also sell caps ask about it afterwards.
        r.item = e.Prop("shop") == "true" ? "shop" : "";
        if (!e.Prop("healSpawn").empty())
        {
            world.healMap = world.map.name;
            world.healSpawn = e.Prop("healSpawn");
        }
    }
    else if (e.Prop("shop") == "true")
    {
        r.type = RequestType::SHOP;
    }
    const std::string &team = e.Prop("team");
    if (!team.empty() && !after && !world.party.empty())
    {
        r.type = RequestType::BATTLE;
        r.trainer = e.Prop("trainer").empty() ? e.variant : e.Prop("trainer");
        r.trainerSprite = e.variant;
        r.team = ResolveTeam(world, team);
        r.winFlag = e.Prop("winFlag").empty() ? e.flag + ":won" : e.Prop("winFlag");
        r.winPages = SplitPages(e.Prop("win"));
        r.prize = e.Prop("prize").empty() ? 20 : std::stoi(e.Prop("prize"));
    }
    if (!e.Prop("setFlag").empty())
    {
        world.flags.insert(e.Prop("setFlag"));
    }
}

void Interact(World &world, Entity &e)
{
    Request &r = world.request;
    switch (e.kind)
    {
    case Kind::NPC:
        TalkToNpc(world, e);
        break;
    case Kind::SIGN:
        r.type = RequestType::DIALOG;
        r.pages = SplitPages(e.Prop("text"));
        break;
    case Kind::BUSH:
    {
        const Critter *chomper = nullptr;
        for (const Critter &c : world.party)
        {
            if (c.Def().type == Type::LEAF)
            {
                chomper = &c;
                break;
            }
        }
        if (chomper)
        {
            const std::string name = chomper->Name();
            CutBush(world, e);
            if (!HasFlag(world, "chomped"))
            {
                world.flags.insert("chomped");
                r.type = RequestType::DIALOG;
                r.pages = {name + " chomped through the bush! Leafy critters can clear any bush."};
            }
        }
        else
        {
            r.type = RequestType::DIALOG;
            r.pages = {"A thick, leafy bush. A hungry LEAF critter could chew right through it."};
        }
        break;
    }
    case Kind::POT:
        BreakPot(world, e);
        break;
    case Kind::LEGEND:
    {
        Critter wild;
        if (!ParseCritter(e.Prop("critter"), wild))
        {
            break;
        }
        r.type = RequestType::BATTLE;
        r.wild = wild;
        r.legend = true;
        r.pages = SplitPages(e.Prop("text"));
        break;
    }
    case Kind::CHEST:
        e.state = 1;
        world.flags.insert(e.flag);
        PlaySfx(Sfx::CHEST);
        r.type = RequestType::ITEM;
        r.item = e.Prop("item");
        break;
    case Kind::STUMP:
        if (!CheckCondition(world, e.Prop("requires")))
        {
            r.type = RequestType::DIALOG;
            r.pages = SplitPages(e.Prop("locked"));
            break;
        }
        r.type = RequestType::SHRINK;
        r.map = e.Prop("map");
        r.spawn = e.Prop("spawn");
        if (!HasFlag(world, "stump_talk"))
        {
            world.flags.insert("stump_talk");
            r.pages = SplitPages(e.Prop("text"));
        }
        break;
    case Kind::PORTAL:
        r.type = RequestType::GROW;
        r.map = e.Prop("map");
        r.spawn = e.Prop("spawn");
        break;
    default:
        break;
    }
}

void CheckEncounter(World &world)
{
    Player &p = world.player;
    const auto tx = static_cast<Int32>(std::floor(p.x / TILE));
    const auto ty = static_cast<Int32>(std::floor((p.y - 2.0f) / TILE));
    if (tx == p.tileX && ty == p.tileY)
    {
        return;
    }
    p.tileX = tx;
    p.tileY = ty;
    if (world.encounterGrace > 0)
    {
        world.encounterGrace--;
        return;
    }
    if (world.peaceful || world.map.encounters.empty() || !PartyCanFight(world) ||
        world.request.type != RequestType::NONE)
    {
        return;
    }
    const Bool grass = IsGrassTile(world.map, tx, ty);
    if (!grass && !world.map.wildEverywhere)
    {
        return;
    }
    const Float chance = grass ? GRASS_ENCOUNTER : CAVE_ENCOUNTER;
    if (world.rng.NextUnit() >= chance)
    {
        return;
    }
    Critter wild;
    if (!RollEncounter(world.map.encounters, world.rng, wild))
    {
        return;
    }
    world.request.type = RequestType::BATTLE;
    world.request.wild = wild;
    p.moving = false;
}

void RecordTrail(Player &p)
{
    if (!p.trail.empty() && std::abs(p.trail.front().x - p.x) < 0.01f &&
        std::abs(p.trail.front().y - p.y) < 0.01f)
    {
        return;
    }
    p.trail.push_front({p.x, p.y, p.dir});
    if (p.trail.size() > TRAIL_LENGTH)
    {
        p.trail.pop_back();
    }
}

void UpdatePlayer(World &world, const Controls &c)
{
    Player &p = world.player;
    p.t += TICK;
    p.moving = false;
    if (p.state != PState::NORMAL)
    {
        return;
    }

    Float ix = 0.0f;
    Float iy = 0.0f;
    if (c.left.down)
        ix -= 1.0f;
    if (c.right.down)
        ix += 1.0f;
    if (c.up.down)
        iy -= 1.0f;
    if (c.down.down)
        iy += 1.0f;

    std::string prompt;
    Entity *target = FindInteractable(world, prompt);
    world.prompt = prompt;
    if (c.a.pressed && target)
    {
        Interact(world, *target);
        return;
    }
    if (ix == 0.0f && iy == 0.0f)
    {
        p.anim = 0.0f;
        p.pushT = 0.0f;
        p.running = false;
        return;
    }
    if (ix != 0.0f && iy == 0.0f)
        p.dir = ix < 0 ? Dir::LEFT : Dir::RIGHT;
    else if (iy != 0.0f && ix == 0.0f)
        p.dir = iy < 0 ? Dir::UP : Dir::DOWN;
    else
    {
        const Bool keep = (p.dir == Dir::LEFT && ix < 0) || (p.dir == Dir::RIGHT && ix > 0) ||
                          (p.dir == Dir::UP && iy < 0) || (p.dir == Dir::DOWN && iy > 0);
        if (!keep)
            p.dir = ix < 0 ? Dir::LEFT : Dir::RIGHT;
    }
    p.running = c.b.down;
    Float speed = p.running ? RUN_SPEED : WALK_SPEED;
    if (ix != 0.0f && iy != 0.0f)
    {
        speed *= 0.7071f;
    }
    const Bool moved_x = MovePlayerAxis(world, ix * speed, 0.0f);
    const Bool moved_y = MovePlayerAxis(world, 0.0f, iy * speed);
    p.moving = true;
    p.anim += TICK * (p.running ? 1.6f : 1.0f);
    if (!moved_x || !moved_y)
    {
        HandlePush(world, moved_x ? 0.0f : ix, moved_y ? 0.0f : iy);
    }
    else
    {
        p.pushT = 0.0f;
    }
    RecordTrail(p);
    CheckEncounter(world);
}

void UpdateObjects(World &world)
{
    Player &p = world.player;
    const Box body = PlayerBox(p);
    const Int32 player_room = world.map.rooms ? RoomIndexAt(world, p.x, p.y - 4.0f) : -1;
    const size_t count = world.entities.size();
    for (size_t i = 0; i < count; ++i)
    {
        Entity &e = world.entities[i];
        if (!e.alive)
        {
            continue;
        }
        if (e.hidden)
        {
            if (CheckCondition(world, e.Prop("showIf")))
            {
                e.hidden = false;
                SpawnEffect(world, "poof", e.x, e.y - 8.0f, 0.32f);
                PlaySfx(Sfx::SECRET);
            }
            continue;
        }
        e.anim += TICK;
        switch (e.kind)
        {
        case Kind::EFFECT:
        {
            e.x += e.vx;
            e.y += e.vy;
            e.vx *= 0.92f;
            e.vy *= 0.92f;
            e.life -= TICK;
            if (e.life <= 0.0f)
            {
                e.alive = false;
            }
            break;
        }
        case Kind::PICKUP:
        {
            e.z += e.vz;
            e.vz -= 0.18f;
            if (e.z <= 0.0f)
            {
                e.z = 0.0f;
                e.vz = std::abs(e.vz) > 0.8f ? -e.vz * 0.45f : 0.0f;
            }
            if (e.life > 0.0f)
            {
                e.life -= TICK;
                if (e.life <= 0.0f)
                {
                    e.alive = false;
                    break;
                }
            }
            if (e.z < 6.0f && Overlaps(body, EntityBox(e)) && world.request.type == RequestType::NONE)
            {
                const std::string &v = e.variant;
                if (v.rfind("gem", 0) == 0)
                {
                    world.gems = std::min(999, world.gems + e.value);
                    PlaySfx(Sfx::GEM, 0.8f);
                    e.alive = false;
                }
                else if (v == "cap")
                {
                    world.caps = std::min(99, world.caps + 1);
                    PlaySfx(Sfx::HEART);
                    e.alive = false;
                }
                else
                {
                    e.alive = false;
                    if (!e.flag.empty())
                    {
                        world.flags.insert(e.flag);
                    }
                    world.request.type = RequestType::ITEM;
                    world.request.item = v;
                }
            }
            break;
        }
        case Kind::BLOCK:
        {
            if (e.state == 1)
            {
                e.x += e.vx;
                e.y += e.vy;
                e.timer += TICK;
                if (e.timer >= 16.0f * TICK - 0.0001f)
                {
                    e.x = std::round(e.x);
                    e.y = std::round(e.y);
                    e.state = 0;
                }
            }
            break;
        }
        case Kind::SWITCH:
        {
            Bool pressed = false;
            for (const Entity &b : world.entities)
            {
                if (b.alive && b.kind == Kind::BLOCK && b.state == 0 &&
                    std::abs(b.x - e.x) < 3.0f && std::abs(b.y - e.y) < 3.0f)
                {
                    pressed = true;
                }
            }
            if (e.Prop("heavy") != "true" && std::abs(p.x - e.x) < 7.0f &&
                std::abs(p.y - 4.0f - (e.y - 8.0f)) < 7.0f)
            {
                pressed = true;
            }
            const std::string &target = e.Prop("target");
            if (pressed && e.state == 0)
            {
                PlaySfx(Sfx::SWITCH);
            }
            e.state = pressed ? 1 : 0;
            if (pressed && !target.empty() && !HasFlag(world, target))
            {
                world.flags.insert(target);
            }
            break;
        }
        case Kind::DOOR:
        {
            if (e.variant == "shutter")
            {
                const Bool closed = !CheckCondition(world, e.Prop("openIf")) && !Overlaps(body, EntityBox(e));
                const Int32 new_state = closed ? 0 : 1;
                if (new_state != e.state && e.timer > 0.0f)
                {
                    PlaySfx(closed ? Sfx::SHUTTER : Sfx::DOOR);
                }
                e.state = new_state;
                e.timer += TICK;
            }
            break;
        }
        case Kind::NPC:
        {
            // NPCs that step aside once something happened (the guard after you get a partner).
            const std::string &move_if = e.Prop("moveIf");
            if (!move_if.empty() && e.value == 0 && world.request.type == RequestType::NONE &&
                CheckCondition(world, move_if))
            {
                const Float nx = std::stof(e.Prop("moveX")) * TILE + 8.0f;
                const Float ny = std::stof(e.Prop("moveY")) * TILE + 16.0f;
                if (std::abs(nx - e.x) > 0.5f || std::abs(ny - e.y) > 0.5f)
                {
                    SpawnEffect(world, "poof", e.x, e.y - 8.0f, 0.32f);
                    e.x = nx;
                    e.y = ny;
                }
                e.value = 1;
            }
            if (e.Prop("wander") == "true")
            {
                e.timer -= TICK;
                if (e.timer <= 0.0f)
                {
                    e.timer = world.rng.NextRange(0.8f, 2.0f);
                    const Int32 choice = world.rng.NextInt(0, 5);
                    e.state = choice < 4 ? 1 : 0;
                    e.dir = static_cast<Dir>(choice % 4);
                    if (std::abs(e.x - e.homeX) > 36.0f)
                        e.dir = e.x > e.homeX ? Dir::LEFT : Dir::RIGHT;
                    else if (std::abs(e.y - e.homeY) > 28.0f)
                        e.dir = e.y > e.homeY ? Dir::UP : Dir::DOWN;
                }
                if (e.state == 1)
                {
                    const Float nx = e.x + DirX(e.dir) * 0.6f;
                    const Float ny = e.y + DirY(e.dir) * 0.6f;
                    const Box dest{nx - e.hw, ny - e.hh * 2.0f, nx + e.hw, ny};
                    if (!BoxHitsTiles(world.map, dest.x0, dest.y0, dest.x1, dest.y1) &&
                        !Overlaps(dest, PlayerBox(p)))
                    {
                        e.x = nx;
                        e.y = ny;
                    }
                    else
                    {
                        e.state = 0;
                    }
                }
            }
            // Trainers who spot the player walk up and challenge them.
            const std::string &sight = e.Prop("sight");
            if (!sight.empty() && world.request.type == RequestType::NONE && !world.party.empty() &&
                !CheckCondition(world, e.Prop("afterIf")))
            {
                const Float range = std::stof(sight) * TILE;
                const Float dx = p.x - e.x;
                const Float dy = p.y - e.y;
                const Bool seen = (e.dir == Dir::DOWN && std::abs(dx) < 10.0f && dy > 0 && dy < range) ||
                                  (e.dir == Dir::UP && std::abs(dx) < 10.0f && dy < 0 && -dy < range) ||
                                  (e.dir == Dir::LEFT && std::abs(dy) < 10.0f && dx < 0 && -dx < range) ||
                                  (e.dir == Dir::RIGHT && std::abs(dy) < 10.0f && dx > 0 && dx < range);
                if (seen)
                {
                    SpawnEffect(world, "alert", e.x, e.y - 24.0f, 0.6f);
                    PlaySfx(Sfx::SECRET, 0.6f);
                    p.dir = std::abs(dx) > std::abs(dy) ? (dx < 0 ? Dir::RIGHT : Dir::LEFT)
                                                        : (dy < 0 ? Dir::DOWN : Dir::UP);
                    TalkToNpc(world, world.entities[i]);
                }
            }
            break;
        }
        case Kind::TRIGGER:
        {
            if (world.request.type == RequestType::NONE && Overlaps(body, EntityBox(e)) &&
                CheckCondition(world, e.Prop("requires")))
            {
                e.alive = false;
                world.flags.insert(e.flag);
                world.request.type = RequestType::DIALOG;
                world.request.pages = SplitPages(e.Prop("text"));
            }
            break;
        }
        case Kind::WARP:
        {
            if (world.request.type == RequestType::NONE && Overlaps(body, EntityBox(e)))
            {
                world.request.type = RequestType::WARP;
                world.request.map = e.Prop("map");
                world.request.spawn = e.Prop("spawn");
            }
            break;
        }
        default:
            break;
        }
    }
    (void)player_room;
}

void UpdateRoomScroll(World &world)
{
    RoomScroll &s = world.scroll;
    Player &p = world.player;
    const Float duration = 0.55f;
    s.t += TICK;
    const Float k = std::min(1.0f, s.t / duration);
    const Float eased = k * k * (3.0f - 2.0f * k);
    world.cameraX = s.fromX + (s.toX - s.fromX) * eased;
    world.cameraY = s.fromY + (s.toY - s.fromY) * eased;
    p.x += s.playerVX;
    p.y += s.playerVY;
    p.moving = true;
    p.anim += TICK;
    if (k >= 1.0f)
    {
        s.active = false;
        world.roomX = static_cast<Int32>(std::floor(s.toX / ROOM_W + 0.5f));
        world.roomY = static_cast<Int32>(std::floor(s.toY / ROOM_H + 0.5f));
        const Int32 room = RoomIndexAt(world, p.x, p.y - 4.0f);
        // Unsolved push puzzles reset when you come back into their room.
        for (Entity &e : world.entities)
        {
            if (e.kind == Kind::BLOCK && e.room == room && !CheckCondition(world, e.Prop("lock")))
            {
                e.x = e.homeX;
                e.y = e.homeY;
                e.state = 0;
            }
            if (e.kind == Kind::DOOR && e.variant == "shutter")
            {
                e.timer = 0.01f;
            }
        }
        const std::string room_name = world.map.name + ":room" + std::to_string(room);
        world.flags.insert(room_name);
    }
}

void StartRoomScrollIfNeeded(World &world)
{
    if (!world.map.rooms)
    {
        return;
    }
    Player &p = world.player;
    const auto rx = static_cast<Int32>(std::floor(p.x / ROOM_W));
    const auto ry = static_cast<Int32>(std::floor((p.y - 4.0f) / ROOM_H));
    if (rx == world.roomX && ry == world.roomY)
    {
        return;
    }
    RoomScroll &s = world.scroll;
    s.active = true;
    s.t = 0.0f;
    s.fromX = static_cast<Float>(world.roomX) * ROOM_W;
    s.fromY = static_cast<Float>(world.roomY) * ROOM_H;
    s.toX = static_cast<Float>(rx) * ROOM_W;
    s.toY = static_cast<Float>(ry) * ROOM_H;
    const Float frames = 0.55f / TICK;
    s.playerVX = static_cast<Float>(rx - world.roomX) * 28.0f / frames;
    s.playerVY = static_cast<Float>(ry - world.roomY) * 28.0f / frames;
    p.state = PState::NORMAL;
    for (Entity &e : world.entities)
    {
        if (e.kind == Kind::EFFECT)
        {
            e.alive = false;
        }
    }
}
} // namespace

Bool HasFlag(const World &world, const std::string &flag)
{
    return world.flags.count(flag) != 0;
}

Bool CheckCondition(const World &world, const std::string &condition)
{
    if (condition.empty())
    {
        return true;
    }
    std::stringstream stream(condition);
    std::string part;
    while (std::getline(stream, part, '&'))
    {
        if (part.empty())
        {
            continue;
        }
        if (part[0] == '!')
        {
            if (HasFlag(world, part.substr(1)))
            {
                return false;
            }
        }
        else if (!HasFlag(world, part))
        {
            return false;
        }
    }
    return true;
}

Int32 RoomIndexAt(const World &world, const Float x, const Float y)
{
    const Int32 cols = std::max(1, world.map.width / 15);
    const auto rx = static_cast<Int32>(std::floor(x / ROOM_W));
    const auto ry = static_cast<Int32>(std::floor(y / ROOM_H));
    return ry * cols + rx;
}

Bool BoxBlocked(
    const World &world, const Float x0, const Float y0, const Float x1, const Float y1,
    const Entity *ignore, const Bool for_player
)
{
    if (BoxHitsTiles(world.map, x0, y0, x1, y1))
    {
        return true;
    }
    const Box box{x0, y0, x1, y1};
    for (const Entity &e : world.entities)
    {
        if (&e == ignore || !BlocksMovement(e))
        {
            continue;
        }
        if (!for_player && e.kind == Kind::NPC)
        {
            continue;
        }
        if (Overlaps(box, EntityBox(e)))
        {
            return true;
        }
    }
    return false;
}

Entity &SpawnEffect(World &world, const std::string &sprite, const Float x, const Float y,
                    const Float duration)
{
    Entity e;
    e.kind = Kind::EFFECT;
    e.variant = sprite;
    e.x = x;
    e.y = y;
    e.life = duration;
    e.timer = duration;
    e.value = sprite == "poof" ? 4 : (sprite == "sparkle" ? 3 : (sprite == "shard" ? 1 : 2));
    world.spawned.push_back(e);
    return world.spawned.back();
}

Bool AddCritter(World &world, const Critter &critter)
{
    world.seen[static_cast<size_t>(critter.species)] = true;
    world.caught[static_cast<size_t>(critter.species)] = true;
    if (world.party.size() < PARTY_SIZE)
    {
        world.party.push_back(critter);
        return true;
    }
    world.box.push_back(critter);
    return false;
}

void HealParty(World &world)
{
    for (Critter &c : world.party)
    {
        c.hp = c.MaxHp();
    }
    for (Critter &c : world.box)
    {
        c.hp = c.MaxHp();
    }
}

Bool PartyCanFight(const World &world)
{
    for (const Critter &c : world.party)
    {
        if (!c.Fainted())
        {
            return true;
        }
    }
    return false;
}

Bool PartyHasType(const World &world, const Type type)
{
    for (const Critter &c : world.party)
    {
        if (c.Def().type == type)
        {
            return true;
        }
    }
    return false;
}

Int32 CaughtCount(const World &world)
{
    Int32 count = 0;
    for (const Bool c : world.caught)
    {
        count += c ? 1 : 0;
    }
    return count;
}

void ApplyItem(World &world, const std::string &item)
{
    world.flags.insert("got_" + item);
    if (item == "key")
    {
        world.keys++;
    }
    else if (item == "bigkey")
    {
        world.bigKey = true;
    }
    else if (item.rfind("caps", 0) == 0)
    {
        world.caps = std::min(99, world.caps + std::atoi(item.c_str() + 4));
    }
    else if (item.rfind("gems", 0) == 0)
    {
        world.gems = std::min(999, world.gems + std::atoi(item.c_str() + 4));
    }
}

void PlacePlayerAtSpawn(World &world, const std::string &spawn)
{
    Player &p = world.player;
    for (const MapObject &o : world.map.objects)
    {
        if (o.type == "spawn" && o.name == spawn)
        {
            p.x = o.x + o.w * 0.5f;
            p.y = o.y + o.h;
            const std::string &dir = o.Prop("dir");
            p.dir = dir == "up" ? Dir::UP : (dir == "left" ? Dir::LEFT : (dir == "right" ? Dir::RIGHT : Dir::DOWN));
            return;
        }
    }
    LOGWARNING("Spawn %s not found in %s", spawn.c_str(), world.map.name.c_str());
    p.x = world.map.PixelWidth() * 0.5f;
    p.y = world.map.PixelHeight() * 0.5f;
}

void EnterMap(World &world, const std::string &map_name, const std::string &spawn, Renderer2D &renderer)
{
    if (world.map.name != map_name)
    {
        if (!LoadMap(map_name, world.tileInfo, renderer, world.map))
        {
            return;
        }
    }
    world.entities.clear();
    world.spawned.clear();
    world.scroll.active = false;
    world.spawnMap = map_name;
    world.spawnName = spawn;
    for (const MapObject &o : world.map.objects)
    {
        if (o.type == "spawn")
        {
            continue;
        }
        Entity e;
        e.props = o.props;
        e.flag = map_name + ":" + std::to_string(o.id);
        e.x = o.x + o.w * 0.5f;
        e.y = o.y + o.h;
        e.hw = std::max(1.0f, o.w * 0.5f);
        e.hh = std::max(1.0f, o.h * 0.5f);
        e.homeX = e.x;
        e.homeY = e.y;
        if (!o.Prop("hideIf").empty() && CheckCondition(world, o.Prop("hideIf")))
        {
            continue;
        }
        const std::string &t = o.type;
        if (t == "bush")
        {
            // Chomped bushes grow back when you leave, except the ones that guard something.
            if (o.Prop("stay") == "true" && HasFlag(world, e.flag))
            {
                continue;
            }
            e.kind = Kind::BUSH;
        }
        else if (t == "pot")
            e.kind = Kind::POT;
        else if (t == "chest")
        {
            e.kind = Kind::CHEST;
            e.state = HasFlag(world, e.flag) ? 1 : 0;
            e.hh = 6.0f;
        }
        else if (t == "sign")
        {
            e.kind = Kind::SIGN;
            e.hh = 5.0f;
            e.hw = 6.0f;
        }
        else if (t == "block")
            e.kind = Kind::BLOCK;
        else if (t == "switch")
            e.kind = Kind::SWITCH;
        else if (t == "torch")
        {
            e.kind = Kind::TORCH;
            e.hh = 5.0f;
            e.hw = 6.0f;
        }
        else if (t == "npc")
        {
            e.kind = Kind::NPC;
            e.variant = o.Prop("npc");
            e.hw = 6.0f;
            e.hh = 5.0f;
            const std::string &facing = o.Prop("dir");
            e.dir = facing == "up" ? Dir::UP
                                   : (facing == "left" ? Dir::LEFT : (facing == "right" ? Dir::RIGHT : Dir::DOWN));
            if (!o.Prop("moveIf").empty() && CheckCondition(world, o.Prop("moveIf")))
            {
                e.x = std::stof(o.Prop("moveX")) * TILE + 8.0f;
                e.y = std::stof(o.Prop("moveY")) * TILE + 16.0f;
            }
        }
        else if (t == "legend")
        {
            if (HasFlag(world, o.Prop("caughtFlag")))
            {
                continue;
            }
            e.kind = Kind::LEGEND;
            e.hw = 12.0f;
            e.hh = 9.0f;
        }
        else if (t == "door")
        {
            if (HasFlag(world, e.flag))
            {
                continue;
            }
            e.kind = Kind::DOOR;
            e.variant = o.Prop("kind");
            e.state = 0;
        }
        else if (t == "warp")
            e.kind = Kind::WARP;
        else if (t == "trigger")
        {
            if (HasFlag(world, e.flag))
            {
                continue;
            }
            e.kind = Kind::TRIGGER;
        }
        else if (t == "stump")
            e.kind = Kind::STUMP;
        else if (t == "portal")
            e.kind = Kind::PORTAL;
        else if (t == "pickup")
        {
            if (HasFlag(world, e.flag))
            {
                continue;
            }
            e.kind = Kind::PICKUP;
            e.variant = o.Prop("item");
            e.hw = 7.0f;
            e.hh = 7.0f;
        }
        else
        {
            continue;
        }
        if (!o.Prop("showIf").empty() && !CheckCondition(world, o.Prop("showIf")))
        {
            e.hidden = true;
        }
        e.room = world.map.rooms ? RoomIndexAt(world, e.x, e.y - 4.0f) : -1;
        world.entities.push_back(std::move(e));
    }
    PlacePlayerAtSpawn(world, spawn);
    Player &p = world.player;
    p.state = PState::NORMAL;
    p.t = 0.0f;
    p.scale = 1.0f;
    p.trail.clear();
    p.tileX = static_cast<Int32>(std::floor(p.x / TILE));
    p.tileY = static_cast<Int32>(std::floor((p.y - 2.0f) / TILE));
    world.encounterGrace = 3;
    if (world.map.rooms)
    {
        world.roomX = static_cast<Int32>(std::floor(p.x / ROOM_W));
        world.roomY = static_cast<Int32>(std::floor((p.y - 4.0f) / ROOM_H));
    }
    UpdateCamera(world, true);
}

void UpdateCamera(World &world, const Bool)
{
    if (world.map.rooms)
    {
        if (!world.scroll.active)
        {
            world.cameraX = static_cast<Float>(world.roomX) * ROOM_W;
            world.cameraY = static_cast<Float>(world.roomY) * ROOM_H;
        }
        return;
    }
    const Player &p = world.player;
    const Float max_x = std::max(0.0f, world.map.PixelWidth() - SCREEN_W);
    const Float max_y = std::max(0.0f, world.map.PixelHeight() - SCREEN_H);
    world.cameraX = std::clamp(p.x - SCREEN_W * 0.5f, 0.0f, max_x);
    world.cameraY = std::clamp(p.y - 10.0f - SCREEN_H * 0.5f, 0.0f, max_y);
}

void UpdateWorld(World &world, const Controls &controls)
{
    world.tickCount++;
    world.time += TICK;
    world.shake = std::max(0.0f, world.shake - TICK);
    world.prompt.clear();
    if (world.scroll.active)
    {
        UpdateRoomScroll(world);
        RecordTrail(world.player);
        return;
    }
    UpdatePlayer(world, controls);
    UpdateObjects(world);
    for (Entity &e : world.spawned)
    {
        world.entities.push_back(std::move(e));
    }
    world.spawned.clear();
    world.entities.erase(
        std::remove_if(world.entities.begin(), world.entities.end(),
                       [](const Entity &e) { return !e.alive; }),
        world.entities.end()
    );
    StartRoomScrollIfNeeded(world);
    UpdateCamera(world, false);
}
} // namespace crittercap
