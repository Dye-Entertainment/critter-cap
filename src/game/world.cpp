#include "game/world.h"

#include "audio/sound.h"
#include "utils/logger.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cmath>
#include <sstream>
#include "compiled/warning_on.h"

namespace smallcap
{
namespace
{
constexpr Float ROOM_W = SCREEN_W;
constexpr Float ROOM_H = SCREEN_H;
constexpr Float PLAYER_HW = 5.0f;
constexpr Float PLAYER_H = 8.0f;
constexpr Float SWING_TIME = 0.22f;
constexpr Float SPIN_TIME = 0.45f;
constexpr Float CHARGE_TIME = 0.7f;
constexpr Float ROLL_TIME = 0.3f;

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

std::vector<std::string> SplitPages(const std::string &text)
{
    std::vector<std::string> pages;
    std::string current;
    for (const char c : text)
    {
        if (c == '|')
        {
            pages.push_back(current);
            current.clear();
        }
        else
        {
            current += c;
        }
    }
    if (!current.empty())
    {
        pages.push_back(current);
    }
    return pages;
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
        return true;
    case Kind::DOOR:
        return e.state == 0;
    default:
        return false;
    }
}

Kind KindFromEnemyName(const std::string &name)
{
    if (name == "pod")
        return Kind::POD;
    if (name == "beetle")
        return Kind::BEETLE;
    if (name == "bat")
        return Kind::BAT;
    if (name == "ant")
        return Kind::ANT;
    if (name == "boss")
        return Kind::BOSS;
    return Kind::BLOB;
}

Int32 EnemyHp(const Kind kind, const std::string &variant)
{
    switch (kind)
    {
    case Kind::BLOB:
        return variant == "r" ? 3 : 2;
    case Kind::POD:
        return 2;
    case Kind::BEETLE:
        return 3;
    case Kind::BAT:
    case Kind::ANT:
        return 1;
    case Kind::BOSS:
        return 10;
    default:
        return 1;
    }
}

void CutBush(World &world, Entity &e)
{
    e.alive = false;
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

void SwordHits(World &world, const Box &box, const Int32 damage, const Int32 stamp)
{
    Player &p = world.player;
    const size_t count = world.entities.size();
    for (size_t i = 0; i < count; ++i)
    {
        Entity &e = world.entities[i];
        if (!e.alive || e.hidden || e.hitStamp == stamp)
        {
            continue;
        }
        Box target = EntityBox(e);
        target.y0 -= e.z;
        target.y1 -= e.z;
        if (!Overlaps(box, target))
        {
            continue;
        }
        switch (e.kind)
        {
        case Kind::BUSH:
            e.hitStamp = stamp;
            CutBush(world, world.entities[i]);
            break;
        case Kind::POT:
            e.hitStamp = stamp;
            BreakPot(world, world.entities[i]);
            break;
        case Kind::SEED:
            e.hitStamp = stamp;
            e.alive = false;
            SpawnEffect(world, "hit", e.x, e.y - 3.0f, 0.12f);
            PlaySfx(Sfx::BOUNCE, 0.7f);
            break;
        case Kind::BOSS:
            e.hitStamp = stamp;
            if (e.state == 4)
            {
                DamageEnemy(world, world.entities[i], damage, p.x, p.y);
            }
            else if (e.hurt <= 0.0f)
            {
                // The shell is too hard: the sword bounces off.
                PlaySfx(Sfx::THUD, 0.9f, 1.3f);
                SpawnEffect(world, "hit", (p.x + e.x) * 0.5f, (p.y + e.y) * 0.5f - 10.0f, 0.12f);
                p.knockX = p.x < e.x ? -1.0f : 1.0f;
                p.knockY = 0.0f;
            }
            break;
        default:
            if (IsEnemy(e.kind))
            {
                e.hitStamp = stamp;
                DamageEnemy(world, world.entities[i], damage, p.x, p.y);
            }
            break;
        }
    }
}

Box SwingBox(const Player &p)
{
    switch (p.dir)
    {
    case Dir::DOWN:
        return {p.x - 13.0f, p.y - 6.0f, p.x + 13.0f, p.y + 14.0f};
    case Dir::UP:
        return {p.x - 13.0f, p.y - 28.0f, p.x + 13.0f, p.y - 8.0f};
    case Dir::LEFT:
        return {p.x - 21.0f, p.y - 22.0f, p.x - 2.0f, p.y + 2.0f};
    case Dir::RIGHT:
    default:
        return {p.x + 2.0f, p.y - 22.0f, p.x + 21.0f, p.y + 2.0f};
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
                if (e.kind == Kind::STUMP && !world.hasSword)
                {
                    continue;
                }
                prompt = e.kind == Kind::STUMP ? "Shrink" : "Grow";
                return &e;
            }
            continue;
        }
        if (e.kind != Kind::NPC && e.kind != Kind::SIGN && !(e.kind == Kind::CHEST && e.state == 0))
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
        prompt = e.kind == Kind::NPC ? "Talk" : (e.kind == Kind::SIGN ? "Read" : "Open");
        return &e;
    }
    return nullptr;
}

void Interact(World &world, Entity &e)
{
    Player &p = world.player;
    Request &r = world.request;
    switch (e.kind)
    {
    case Kind::NPC:
    {
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
        if (!after_if.empty() && CheckCondition(world, after_if))
        {
            r.pages = SplitPages(e.Prop("after"));
        }
        else
        {
            r.pages = SplitPages(e.Prop("text"));
            const std::string &give = e.Prop("give");
            if (!give.empty() && !HasFlag(world, "got_" + give))
            {
                r.afterItem = give;
            }
        }
        if (!e.Prop("setFlag").empty())
        {
            world.flags.insert(e.Prop("setFlag"));
        }
        break;
    }
    case Kind::SIGN:
        r.type = RequestType::DIALOG;
        r.pages = SplitPages(e.Prop("text"));
        break;
    case Kind::CHEST:
        e.state = 1;
        world.flags.insert(e.flag);
        PlaySfx(Sfx::CHEST);
        r.type = RequestType::ITEM;
        r.item = e.Prop("item");
        break;
    case Kind::STUMP:
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

void UpdatePlayer(World &world, const Controls &c)
{
    Player &p = world.player;
    p.t += TICK;
    p.iframes = std::max(0.0f, p.iframes - TICK);
    p.rollCooldown = std::max(0.0f, p.rollCooldown - TICK);
    p.moving = false;

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

    switch (p.state)
    {
    case PState::NORMAL:
    {
        std::string prompt;
        Entity *target = FindInteractable(world, prompt);
        if (!p.charging)
        {
            world.prompt = prompt;
        }
        if (c.action.pressed && target && !p.charging)
        {
            Interact(world, *target);
            return;
        }
        if (c.sword.pressed && world.hasSword && !p.charging)
        {
            p.state = PState::SWING;
            p.t = 0.0f;
            p.swingStamp++;
            PlaySfx(Sfx::SWORD, 0.8f, world.rng.NextRange(0.95f, 1.05f));
            return;
        }
        if (c.roll.pressed && p.rollCooldown <= 0.0f && !p.charging)
        {
            p.state = PState::ROLL;
            p.t = 0.0f;
            if (ix != 0.0f || iy != 0.0f)
            {
                const Float len = std::sqrt(ix * ix + iy * iy);
                p.rollX = ix / len;
                p.rollY = iy / len;
                if (std::abs(ix) > 0.0f && iy == 0.0f)
                    p.dir = ix < 0 ? Dir::LEFT : Dir::RIGHT;
                if (std::abs(iy) > 0.0f && ix == 0.0f)
                    p.dir = iy < 0 ? Dir::UP : Dir::DOWN;
            }
            else
            {
                p.rollX = DirX(p.dir);
                p.rollY = DirY(p.dir);
            }
            PlaySfx(Sfx::ROLL, 0.7f);
            return;
        }
        if (p.charging)
        {
            const Float before = p.charge;
            p.charge += TICK;
            if (before < CHARGE_TIME && p.charge >= CHARGE_TIME)
            {
                PlaySfx(Sfx::CHARGE, 0.8f);
            }
            if (!c.sword.down)
            {
                p.charging = false;
                if (p.charge >= CHARGE_TIME)
                {
                    p.state = PState::SPIN;
                    p.t = 0.0f;
                    p.swingStamp++;
                    PlaySfx(Sfx::SPIN);
                    return;
                }
            }
        }
        if (ix != 0.0f || iy != 0.0f)
        {
            if (!p.charging)
            {
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
            }
            Float speed = p.charging ? 0.7f : 1.25f;
            if (ix != 0.0f && iy != 0.0f)
            {
                speed *= 0.7071f;
            }
            const Bool moved_x = MovePlayerAxis(world, ix * speed, 0.0f);
            const Bool moved_y = MovePlayerAxis(world, 0.0f, iy * speed);
            p.moving = true;
            p.anim += TICK;
            if (!moved_x || !moved_y)
            {
                HandlePush(world, moved_x ? 0.0f : ix, moved_y ? 0.0f : iy);
            }
            else
            {
                p.pushT = 0.0f;
            }
        }
        else
        {
            p.anim = 0.0f;
            p.pushT = 0.0f;
        }
        break;
    }
    case PState::SWING:
    {
        SwordHits(world, SwingBox(p), 1, p.swingStamp);
        if (p.knockX != 0.0f)
        {
            MovePlayerAxis(world, p.knockX * 1.5f, 0.0f);
        }
        if (p.t >= SWING_TIME)
        {
            p.knockX = 0.0f;
            p.state = PState::NORMAL;
            p.t = 0.0f;
            if (c.sword.down)
            {
                p.charging = true;
                p.charge = 0.0f;
            }
        }
        break;
    }
    case PState::SPIN:
    {
        const Box spin{p.x - 24.0f, p.y - 30.0f, p.x + 24.0f, p.y + 16.0f};
        SwordHits(world, spin, 2, p.swingStamp);
        if (p.t >= SPIN_TIME)
        {
            p.state = PState::NORMAL;
            p.t = 0.0f;
            p.knockX = 0.0f;
        }
        break;
    }
    case PState::ROLL:
    {
        const Float speed = 2.3f;
        const Bool ok_x = MovePlayerAxis(world, p.rollX * speed, 0.0f);
        const Bool ok_y = MovePlayerAxis(world, 0.0f, p.rollY * speed);
        p.anim += TICK;
        const Bool bonk = (!ok_x && p.rollX != 0.0f && p.rollY == 0.0f) ||
                          (!ok_y && p.rollY != 0.0f && p.rollX == 0.0f);
        if (bonk)
        {
            PlaySfx(Sfx::THUD, 0.7f);
            world.shake = 0.15f;
            p.state = PState::HURT;
            p.t = 0.1f;
            p.knockX = -p.rollX * 0.6f;
            p.knockY = -p.rollY * 0.6f;
            p.rollCooldown = 0.3f;
            break;
        }
        if (p.t >= ROLL_TIME)
        {
            p.state = PState::NORMAL;
            p.t = 0.0f;
            p.rollCooldown = 0.15f;
        }
        break;
    }
    case PState::HURT:
    {
        MovePlayerAxis(world, p.knockX * 2.4f, 0.0f);
        MovePlayerAxis(world, 0.0f, p.knockY * 2.4f);
        if (p.t >= 0.22f)
        {
            p.state = PState::NORMAL;
            p.t = 0.0f;
            p.knockX = 0.0f;
            p.knockY = 0.0f;
        }
        break;
    }
    case PState::DEAD:
    {
        if (p.t >= 2.2f && world.request.type == RequestType::NONE)
        {
            world.request.type = RequestType::GAME_OVER;
            p.t = -100.0f;
        }
        break;
    }
    default:
        break;
    }
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
        if (IsEnemy(e.kind) || e.kind == Kind::SEED)
        {
            if (world.map.rooms && e.room != player_room)
            {
                continue;
            }
            if (p.state != PState::DEAD)
            {
                UpdateEnemy(world, world.entities[i]);
            }
            continue;
        }
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
            if (e.z < 6.0f && Overlaps(body, EntityBox(e)) && p.state != PState::DEAD &&
                world.request.type == RequestType::NONE)
            {
                const std::string &v = e.variant;
                if (v.rfind("gem", 0) == 0)
                {
                    world.gems = std::min(999, world.gems + e.value);
                    PlaySfx(Sfx::GEM, 0.8f);
                    e.alive = false;
                }
                else if (v == "heart")
                {
                    p.hp = std::min(p.maxHp, p.hp + 2);
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
                Bool closed;
                const std::string &open_if = e.Prop("openIf");
                if (!open_if.empty())
                {
                    closed = !CheckCondition(world, open_if);
                }
                else
                {
                    const Box door = EntityBox(e);
                    Bool touches = false;
                    if (player_room >= 0)
                    {
                        const Int32 cols = world.map.width / 15;
                        const Float rx = static_cast<Float>(player_room % cols) * ROOM_W;
                        const Float ry = static_cast<Float>(player_room / cols) * ROOM_H;
                        touches = door.x0 < rx + ROOM_W && door.x1 > rx && door.y0 < ry + ROOM_H &&
                                  door.y1 > ry;
                    }
                    closed = touches && RoomHasEnemies(world, player_room) &&
                             !Overlaps(body, door);
                }
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
            if (world.request.type == RequestType::NONE && Overlaps(body, EntityBox(e)) &&
                p.state != PState::DEAD)
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
    p.charging = false;
    // Effects and projectiles left in the old room vanish.
    for (Entity &e : world.entities)
    {
        if (e.kind == Kind::SEED || e.kind == Kind::EFFECT)
        {
            e.alive = false;
        }
    }
}

void UpdateBossDeath(World &world)
{
    if (world.bossDeathTimer < 0.0f)
    {
        return;
    }
    world.bossDeathTimer += TICK;
    Entity *boss = nullptr;
    for (Entity &e : world.entities)
    {
        if (e.kind == Kind::BOSS && e.alive)
        {
            boss = &e;
        }
    }
    if (boss)
    {
        boss->hurt = 0.2f;
        if (world.tickCount % 8 == 0)
        {
            SpawnEffect(world, "poof", boss->x + world.rng.NextRange(-14.0f, 14.0f),
                        boss->y - 12.0f + world.rng.NextRange(-12.0f, 10.0f), 0.32f);
            PlaySfx(Sfx::ENEMY_DIE, 0.6f, world.rng.NextRange(0.8f, 1.2f));
        }
        if (world.bossDeathTimer > 2.0f)
        {
            boss->alive = false;
            world.flags.insert(boss->flag);
            world.flags.insert("boss_dead");
            PlaySfx(Sfx::BOSS_DIE);
            world.shake = 0.5f;
            Entity heart;
            heart.kind = Kind::PICKUP;
            heart.variant = "heartc";
            heart.flag = "roothollow:heartc";
            heart.x = boss->x;
            heart.y = boss->y;
            heart.hw = 7.0f;
            heart.hh = 7.0f;
            heart.vz = 3.0f;
            world.spawned.push_back(heart);
            world.bossDeathTimer = -1.0f;
            PlayMusic(Music::NONE);
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

Bool IsEnemy(const Kind kind)
{
    switch (kind)
    {
    case Kind::BLOB:
    case Kind::POD:
    case Kind::BEETLE:
    case Kind::BAT:
    case Kind::ANT:
    case Kind::BOSS:
        return true;
    default:
        return false;
    }
}

Int32 RoomIndexAt(const World &world, const Float x, const Float y)
{
    const Int32 cols = std::max(1, world.map.width / 15);
    const auto rx = static_cast<Int32>(std::floor(x / ROOM_W));
    const auto ry = static_cast<Int32>(std::floor(y / ROOM_H));
    return ry * cols + rx;
}

Bool RoomHasEnemies(const World &world, const Int32 room)
{
    for (const Entity &e : world.entities)
    {
        if (e.alive && !e.hidden && IsEnemy(e.kind) && e.room == room)
        {
            return true;
        }
    }
    return false;
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

Bool MoveActor(World &world, Entity &e, const Float dx, const Float dy)
{
    Bool ok = true;
    const auto try_move = [&](const Float mx, const Float my) {
        const Float nx = e.x + mx;
        const Float ny = e.y + my;
        const Float x0 = nx - e.hw;
        const Float y0 = ny - e.hh * 2.0f;
        const Float x1 = nx + e.hw;
        const Float y1 = ny;
        if (world.map.rooms && e.room >= 0)
        {
            const Int32 cols = std::max(1, world.map.width / 15);
            const Float rx = static_cast<Float>(e.room % cols) * ROOM_W;
            const Float ry = static_cast<Float>(e.room / cols) * ROOM_H;
            if (x0 < rx + TILE || x1 > rx + ROOM_W - TILE || y0 < ry + TILE ||
                y1 > ry + ROOM_H - TILE)
            {
                return false;
            }
        }
        const Bool flying = e.kind == Kind::BAT;
        if (flying)
        {
            if (x0 < 0 || y0 < 0 || x1 > world.map.PixelWidth() || y1 > world.map.PixelHeight())
            {
                return false;
            }
        }
        else if (BoxBlocked(world, x0, y0, x1, y1, &e, false))
        {
            return false;
        }
        e.x = nx;
        e.y = ny;
        return true;
    };
    if (dx != 0.0f && !try_move(dx, 0.0f))
    {
        ok = false;
    }
    if (dy != 0.0f && !try_move(0.0f, dy))
    {
        ok = false;
    }
    return ok;
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

void SpawnDrop(World &world, const Float x, const Float y)
{
    const Float r = world.rng.NextUnit();
    std::string variant;
    Int32 value = 1;
    if (r < 0.28f)
    {
        variant = "gem_g";
    }
    else if (r < 0.36f)
    {
        variant = "gem_b";
        value = 5;
    }
    else if (r < 0.52f && world.player.hp < world.player.maxHp)
    {
        variant = "heart";
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

void DamageEnemy(World &world, Entity &e, const Int32 damage, const Float from_x, const Float from_y)
{
    if (e.hurt > 0.0f || !e.alive)
    {
        return;
    }
    e.hp -= damage;
    e.hurt = 0.35f;
    Float dx = e.x - from_x;
    Float dy = (e.y - e.hh) - (from_y - 4.0f);
    const Float len = std::max(0.01f, std::sqrt(dx * dx + dy * dy));
    const Float knock = e.kind == Kind::BOSS ? 0.0f : 2.2f;
    e.knockX = dx / len * knock;
    e.knockY = dy / len * knock;
    e.knockT = 0.18f;
    SpawnEffect(world, "hit", e.x, e.y - e.hh - e.z, 0.12f);
    if (e.kind == Kind::BOSS)
    {
        PlaySfx(Sfx::BOSS_HIT);
        world.shake = 0.15f;
        e.value++;
        if (e.hp <= 0)
        {
            e.hp = 0;
            e.state = 9;
            world.bossDeathTimer = 0.0f;
            PlaySfx(Sfx::BOSS_ROAR);
        }
        return;
    }
    PlaySfx(Sfx::HIT, 0.9f, world.rng.NextRange(0.95f, 1.1f));
    if (e.hp <= 0)
    {
        e.alive = false;
        SpawnEffect(world, "poof", e.x, e.y - e.hh - e.z, 0.32f);
        PlaySfx(Sfx::ENEMY_DIE, 0.8f);
        if (world.map.rooms && !e.flag.empty())
        {
            world.flags.insert(e.flag);
        }
        SpawnDrop(world, e.x, e.y);
    }
}

void HurtPlayer(World &world, const Int32 damage, const Float from_x, const Float from_y)
{
    Player &p = world.player;
    if (p.iframes > 0.0f || p.state == PState::DEAD || p.state == PState::HOLD ||
        p.state == PState::SHRINK || p.state == PState::GROW || world.scroll.active)
    {
        return;
    }
    p.hp -= damage;
    p.iframes = 1.1f;
    p.charging = false;
    Float dx = p.x - from_x;
    Float dy = p.y - from_y;
    const Float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f)
    {
        dx = -DirX(p.dir);
        dy = -DirY(p.dir);
    }
    else
    {
        dx /= len;
        dy /= len;
    }
    p.knockX = dx;
    p.knockY = dy;
    if (p.hp <= 0)
    {
        p.hp = 0;
        p.state = PState::DEAD;
        p.t = 0.0f;
        PlaySfx(Sfx::DIE);
        PlayMusic(Music::NONE);
        return;
    }
    p.state = PState::HURT;
    p.t = 0.0f;
    PlaySfx(Sfx::HURT);
}

void ApplyItem(World &world, const std::string &item)
{
    Player &p = world.player;
    world.flags.insert("got_" + item);
    if (item == "sword")
    {
        world.hasSword = true;
    }
    else if (item == "key")
    {
        world.keys++;
    }
    else if (item == "bigkey")
    {
        world.bigKey = true;
    }
    else if (item == "heartc")
    {
        p.maxHp += 2;
        p.hp = p.maxHp;
    }
    else if (item == "gems20")
    {
        world.gems = std::min(999, world.gems + 20);
    }
    else if (item == "gems50")
    {
        world.gems = std::min(999, world.gems + 50);
    }
    else if (item == "heart")
    {
        p.hp = p.maxHp;
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
    world.bossDeathTimer = -1.0f;
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
            e.kind = Kind::BUSH;
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
            if (!o.Prop("moveIf").empty() && CheckCondition(world, o.Prop("moveIf")))
            {
                e.x = std::stof(o.Prop("moveX")) * TILE + 8.0f;
                e.y = std::stof(o.Prop("moveY")) * TILE + 16.0f;
            }
        }
        else if (t == "enemy")
        {
            if (world.peaceful && o.Prop("kind") != "boss")
            {
                continue;
            }
            if (world.map.rooms && HasFlag(world, e.flag))
            {
                continue;
            }
            e.kind = KindFromEnemyName(o.Prop("kind"));
            e.variant = o.Prop("color").empty() ? "g" : o.Prop("color");
            e.hp = EnemyHp(e.kind, e.variant);
            e.hw = e.kind == Kind::BOSS ? 12.0f : 6.0f;
            e.hh = e.kind == Kind::BOSS ? 9.0f : 5.0f;
            e.timer = world.rng.NextRange(0.2f, 1.2f);
            e.dir = static_cast<Dir>(world.rng.NextInt(0, 3));
            if (e.kind == Kind::BOSS)
            {
                e.dir = Dir::DOWN;
                e.timer = 1.5f;
            }
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
    p.charging = false;
    p.scale = 1.0f;
    p.knockX = p.knockY = 0.0f;
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
        return;
    }
    UpdatePlayer(world, controls);
    UpdateObjects(world);
    UpdateBossDeath(world);
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
} // namespace smallcap
