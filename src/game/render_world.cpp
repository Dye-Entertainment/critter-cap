#include "game/world.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "compiled/warning_on.h"

namespace crittercap
{
namespace
{
const char *DirName(const Dir d)
{
    switch (d)
    {
    case Dir::UP:
        return "up";
    case Dir::LEFT:
    case Dir::RIGHT:
        return "right";
    case Dir::DOWN:
    default:
        return "down";
    }
}

void DrawPlayer(const World &world, Renderer2D &r)
{
    const Player &p = world.player;
    DrawOptions o;
    o.flipX = p.dir == Dir::LEFT;
    o.scale = p.scale;
    DrawOptions shadow;
    shadow.tint = {0.0f, 0.0f, 0.0f, 0.45f};
    shadow.scale = p.scale;
    r.Draw("shadow", p.x, p.y, shadow);
    std::string name;
    switch (p.state)
    {
    case PState::HOLD:
        name = "hero_hold";
        o.flipX = false;
        break;
    case PState::SHRINK:
    case PState::GROW:
        name = "hero_down_0";
        o.flipX = false;
        o.flash = (world.tickCount / 3) % 2 == 0 ? 0.6f : 0.0f;
        break;
    default:
    {
        Int32 frame = 0;
        if (p.moving)
        {
            frame = static_cast<Int32>(p.anim * 8.0f) % 4;
        }
        name = std::string("hero_") + DirName(p.dir) + "_" + std::to_string(frame);
        break;
    }
    }
    r.Draw(name, p.x, p.y, o);
    if (p.state == PState::HOLD && !p.holdItem.empty())
    {
        r.Draw(p.holdItem, p.x, p.y - 22.0f, {});
    }
}

// The lead critter trots along behind Pip.
Bool FollowerPosition(const World &world, Float &x, Float &y, Dir &dir)
{
    const Player &p = world.player;
    if (world.party.empty() || p.state == PState::SHRINK || p.state == PState::GROW)
    {
        return false;
    }
    if (p.trail.size() < 2)
    {
        // Fresh on a map: wait one step behind Pip.
        x = p.x - DirX(p.dir) * 14.0f;
        y = p.y - DirY(p.dir) * 14.0f;
        dir = p.dir;
        return true;
    }
    // Walk back along the trail about 14 pixels.
    Float remaining = 14.0f;
    Float px = p.x;
    Float py = p.y;
    for (const TrailPoint &t : p.trail)
    {
        const Float dx = t.x - px;
        const Float dy = t.y - py;
        const Float len = std::sqrt(dx * dx + dy * dy);
        dir = t.dir;
        if (len >= remaining)
        {
            x = px + dx / len * remaining;
            y = py + dy / len * remaining;
            return true;
        }
        remaining -= len;
        px = t.x;
        py = t.y;
    }
    x = px;
    y = py;
    return true;
}

void DrawFollower(const World &world, Renderer2D &r, const Float x, const Float y, const Dir dir)
{
    const Critter &lead = world.party.front();
    const Int32 d = dir == Dir::DOWN ? 0 : (dir == Dir::UP ? 1 : (dir == Dir::LEFT ? 2 : 3));
    const Int32 frame = world.player.moving ? static_cast<Int32>(world.player.anim * 7.0f) : (world.tickCount / 30);
    Bool flip = false;
    const std::string name = FollowerSprite(lead.species, d, frame, flip);
    DrawOptions o;
    o.flipX = flip;
    DrawOptions shadow;
    shadow.tint = {0.0f, 0.0f, 0.0f, 0.45f};
    r.Draw("shadow", x, y, shadow);
    const Float hop = lead.Def().directional ? 0.0f : (frame % 2 == 0 ? 0.0f : 1.0f);
    const Float lift = lead.Def().sprite == std::string("bat") ? 6.0f : 0.0f;
    r.Draw(name, x, y - hop - lift, o);
}

std::string PickupSprite(const Entity &e, const Int32 tick)
{
    const std::string &v = e.variant;
    if (v.rfind("gem_", 0) == 0)
    {
        return v + "_" + std::to_string((tick / 10) % 2);
    }
    if (v == "cap")
    {
        return "cap";
    }
    if (v == "sunstone")
    {
        return "sunstone_" + std::to_string((tick / 12) % 2);
    }
    if (v == "key" || v == "bigkey")
    {
        return v;
    }
    return "gem_g_0";
}

void DrawEntity(const World &world, const Entity &e, Renderer2D &r)
{
    DrawOptions o;
    DrawOptions shadow;
    shadow.tint = {0.0f, 0.0f, 0.0f, 0.45f};
    const Int32 tick = world.tickCount;
    switch (e.kind)
    {
    case Kind::BUSH:
        r.Draw("bush", e.x, e.y, o);
        break;
    case Kind::POT:
        r.Draw("pot", e.x, e.y, o);
        break;
    case Kind::CHEST:
        r.Draw(e.state == 0 ? "chest_closed" : "chest_open", e.x, e.y, o);
        break;
    case Kind::SIGN:
        r.Draw("sign", e.x, e.y, o);
        break;
    case Kind::BLOCK:
        r.Draw("block", e.x, e.y, o);
        break;
    case Kind::TORCH:
        r.Draw("torch_" + std::to_string((tick / 8) % 2), e.x, e.y + 2.0f, o);
        break;
    case Kind::NPC:
    {
        const Bool dirs = e.variant == "tobi" || e.variant == "guard";
        const Dir d = dirs ? e.dir : Dir::DOWN;
        Int32 frame = 0;
        if (e.state == 1)
        {
            frame = static_cast<Int32>(e.anim * 8.0f) % 4;
        }
        else if (static_cast<Int32>(e.anim * 1.5f) % 4 == 3)
        {
            frame = 1;
        }
        o.flipX = d == Dir::LEFT;
        r.Draw("shadow", e.x, e.y, shadow);
        r.Draw(e.variant + "_" + DirName(d) + "_" + std::to_string(frame), e.x, e.y, o);
        break;
    }
    case Kind::DOOR:
    {
        if (e.state != 0)
        {
            break;
        }
        const Bool tall = e.hh > e.hw;
        const std::string name = "door_" + e.variant + (tall ? "_h" : "_v");
        const Float x0 = e.x - e.hw;
        const Float y0 = e.y - e.hh * 2.0f;
        for (Float ty = y0; ty < e.y - 1.0f; ty += TILE)
        {
            for (Float tx = x0; tx < e.x + e.hw - 1.0f; tx += TILE)
            {
                r.Draw(name, tx + 8.0f, ty + 16.0f, o);
            }
        }
        break;
    }
    case Kind::PICKUP:
    {
        if (e.life > 0.0f && e.life < 2.5f && (tick / 3) % 2 == 0)
        {
            break;
        }
        const Bool floating = e.variant == "sunstone";
        const Float bob = floating && e.vz == 0.0f ? std::sin(world.time * 3.0f) * 2.0f + 3.0f : 0.0f;
        r.Draw("shadow", e.x, e.y, shadow);
        r.Draw(PickupSprite(e, tick), e.x, e.y - e.z - bob, o);
        if (e.variant == "sunstone" && tick % 20 < 10)
        {
            r.Draw("sparkle_" + std::to_string((tick / 4) % 3), e.x + 8.0f, e.y - 22.0f - bob, {});
        }
        break;
    }
    case Kind::LEGEND:
    {
        r.Draw("shadow_big", e.x, e.y - 1.0f, shadow);
        // Asleep until you get close, then it stirs.
        const Float dx = world.player.x - e.x;
        const Float dy = world.player.y - e.y;
        const Bool near = dx * dx + dy * dy < 56.0f * 56.0f;
        const std::string name = std::string("boss_down_") + std::to_string(near ? (tick / 10) % 2 : 0);
        r.Draw(name, e.x + (near && (tick / 2) % 8 == 0 ? 1.0f : 0.0f), e.y, o);
        if (!near && (tick / 40) % 2 == 0)
        {
            DrawOptions z;
            z.tint = {0.8f, 0.85f, 1.0f, 1.0f};
            r.Draw("sleep_z", e.x + 14.0f, e.y - 34.0f - static_cast<Float>((tick / 6) % 6), z);
        }
        break;
    }
    case Kind::EFFECT:
    {
        if (e.variant == "leaf")
        {
            r.Draw("leaf_" + std::to_string((tick / 5) % 2), e.x, e.y, o);
            break;
        }
        if (e.variant == "shard")
        {
            r.Draw("shard", e.x, e.y, o);
            break;
        }
        const Float t = 1.0f - std::max(0.0f, e.life) / std::max(0.001f, e.timer);
        const Int32 frame = std::min(e.value - 1, static_cast<Int32>(t * static_cast<Float>(e.value)));
        r.Draw(e.variant + "_" + std::to_string(frame), e.x, e.y, o);
        break;
    }
    default:
        break;
    }
}

Bool IsFlat(const Entity &e)
{
    return e.kind == Kind::SWITCH || e.kind == Kind::PORTAL ||
           (e.kind == Kind::EFFECT && e.variant == "dust");
}

Bool IsOverlay(const Entity &e)
{
    return e.kind == Kind::EFFECT && e.variant != "dust";
}
} // namespace

void RenderWorld(World &world, Renderer2D &r)
{
    Float sx = 0.0f;
    Float sy = 0.0f;
    if (world.shake > 0.0f)
    {
        sx = static_cast<Float>((world.tickCount * 7) % 5 - 2);
        sy = static_cast<Float>((world.tickCount * 3) % 3 - 1);
    }
    r.SetCamera(world.cameraX + sx, world.cameraY + sy);
    r.DrawTileLayer(world.map.layer, static_cast<Int32>(world.time * 2.0f));

    // Flat things on the floor first.
    for (const Entity &e : world.entities)
    {
        if (!e.alive || e.hidden || !IsFlat(e))
        {
            continue;
        }
        if (e.kind == Kind::SWITCH)
        {
            const Bool down = e.state == 1 || CheckCondition(world, e.Prop("target"));
            r.Draw(down ? "switch_down" : "switch_up", e.x, e.y, {});
        }
        else if (e.kind == Kind::PORTAL)
        {
            r.Draw("portal_" + std::to_string((world.tickCount / 6) % 4), e.x, e.y - e.hh, {});
        }
        else
        {
            const Float t = 1.0f - std::max(0.0f, e.life) / std::max(0.001f, e.timer);
            r.Draw("dust_" + std::to_string(t < 0.5f ? 0 : 1), e.x, e.y, {});
        }
    }
    // Stump shimmer once the player can use it.
    for (const Entity &e : world.entities)
    {
        if (e.kind == Kind::STUMP && !world.party.empty() && (world.tickCount / 8) % 3 != 0)
        {
            const Float a = world.time * 2.0f;
            r.Draw("sparkle_" + std::to_string((world.tickCount / 5) % 3),
                   e.x + std::cos(a) * 9.0f, e.y - e.hh + std::sin(a) * 5.0f, {});
        }
    }

    // Y-sorted actors and objects.
    std::vector<const Entity *> sorted;
    sorted.reserve(world.entities.size());
    for (const Entity &e : world.entities)
    {
        if (e.alive && !e.hidden && !IsFlat(e) && !IsOverlay(e))
        {
            sorted.push_back(&e);
        }
    }
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Entity *a, const Entity *b) { return a->y < b->y; });
    Float fx = 0.0f;
    Float fy = 0.0f;
    Dir fdir = Dir::DOWN;
    const Bool follower = FollowerPosition(world, fx, fy, fdir);
    Bool player_drawn = false;
    Bool follower_drawn = !follower;
    const auto draw_actors_above = [&](const Float y) {
        // Pip and the follower, in y order, if they stand above y.
        for (Int32 pass = 0; pass < 2; ++pass)
        {
            const Bool follower_first = fy <= world.player.y;
            const Bool do_follower = (pass == 0) == follower_first;
            if (do_follower && !follower_drawn && fy < y)
            {
                DrawFollower(world, r, fx, fy, fdir);
                follower_drawn = true;
            }
            if (!do_follower && !player_drawn && world.player.y < y)
            {
                DrawPlayer(world, r);
                player_drawn = true;
            }
        }
    };
    for (const Entity *e : sorted)
    {
        draw_actors_above(e->y);
        DrawEntity(world, *e, r);
    }
    draw_actors_above(1.0e9f);
    for (const Entity &e : world.entities)
    {
        if (e.alive && IsOverlay(e))
        {
            DrawEntity(world, e, r);
        }
    }
}

} // namespace crittercap
