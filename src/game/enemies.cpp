#include "audio/sound.h"
#include "game/world.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cmath>
#include "compiled/warning_on.h"

namespace smallcap
{
namespace
{
// Boss states.
constexpr Int32 BOSS_WALK = 0;
constexpr Int32 BOSS_AIM = 1;
constexpr Int32 BOSS_CHARGE = 2;
constexpr Int32 BOSS_STUNNED = 4;
constexpr Int32 BOSS_RECOVER = 5;
constexpr Int32 BOSS_DYING = 9;

Dir DirToward(const Float dx, const Float dy)
{
    if (std::abs(dx) > std::abs(dy))
    {
        return dx < 0 ? Dir::LEFT : Dir::RIGHT;
    }
    return dy < 0 ? Dir::UP : Dir::DOWN;
}

Float DistanceToPlayer(const World &world, const Entity &e, Float &dx, Float &dy)
{
    dx = world.player.x - e.x;
    dy = (world.player.y - 4.0f) - (e.y - e.hh);
    return std::sqrt(dx * dx + dy * dy);
}

void TouchDamage(World &world, Entity &e, const Int32 damage)
{
    const Player &p = world.player;
    const Float ex0 = e.x - e.hw + 1.0f;
    const Float ex1 = e.x + e.hw - 1.0f;
    const Float ey0 = e.y - e.hh * 2.0f - e.z + 1.0f;
    const Float ey1 = e.y - e.z;
    if (ex0 < p.x + 5.0f && ex1 > p.x - 5.0f && ey0 < p.y && ey1 > p.y - 13.0f)
    {
        if (e.kind == Kind::BAT || e.z < 6.0f)
        {
            HurtPlayer(world, damage, e.x, e.y - e.hh);
        }
    }
}

void UpdateBlob(World &world, Entity &e)
{
    // state 0: rest, 1: hop
    e.timer -= TICK;
    if (e.state == 0)
    {
        if (e.timer <= 0.0f)
        {
            Float dx, dy;
            const Float dist = DistanceToPlayer(world, e, dx, dy);
            Float angle;
            if (dist < 96.0f)
            {
                angle = std::atan2(dy, dx) + world.rng.NextRange(-0.4f, 0.4f);
            }
            else
            {
                angle = world.rng.NextRange(0.0f, 6.283f);
            }
            const Float speed = e.variant == "r" ? 1.25f : 1.0f;
            e.vx = std::cos(angle) * speed;
            e.vy = std::sin(angle) * speed;
            e.vz = 2.0f;
            e.state = 1;
        }
    }
    else
    {
        MoveActor(world, e, e.vx, e.vy);
        e.z += e.vz;
        e.vz -= 0.2f;
        if (e.z <= 0.0f)
        {
            e.z = 0.0f;
            e.state = 0;
            e.timer = world.rng.NextRange(0.35f, 0.9f);
            e.anim = 0.0f;
        }
    }
    TouchDamage(world, e, 1);
}

void UpdatePod(World &world, Entity &e)
{
    Float dx, dy;
    const Float dist = DistanceToPlayer(world, e, dx, dy);
    e.timer -= TICK;
    if (e.state == 0)
    {
        if (dist < 130.0f)
        {
            e.dir = DirToward(dx, dy);
        }
        const Bool aligned = std::abs(dx) < 14.0f || std::abs(dy) < 14.0f;
        if (e.timer <= 0.0f && dist < 120.0f && aligned)
        {
            e.state = 1;
            e.timer = 0.35f;
        }
    }
    else if (e.timer <= 0.0f)
    {
        Entity seed;
        seed.kind = Kind::SEED;
        seed.x = e.x + DirX(e.dir) * 8.0f;
        seed.y = e.y - 4.0f + DirY(e.dir) * 8.0f;
        seed.vx = DirX(e.dir) * 1.9f;
        seed.vy = DirY(e.dir) * 1.9f;
        seed.hw = 3.0f;
        seed.hh = 3.0f;
        seed.life = 2.5f;
        seed.room = e.room;
        world.spawned.push_back(seed);
        PlaySfx(Sfx::SHOOT, 0.7f);
        e.state = 0;
        e.timer = world.rng.NextRange(1.4f, 2.4f);
    }
    TouchDamage(world, e, 1);
}

void UpdateWalker(World &world, Entity &e, const Float speed, const Float chase_speed)
{
    Float dx, dy;
    const Float dist = DistanceToPlayer(world, e, dx, dy);
    e.timer -= TICK;
    Float s = speed;
    if (dist < 64.0f && (std::abs(dx) < 10.0f || std::abs(dy) < 10.0f))
    {
        e.dir = DirToward(dx, dy);
        s = chase_speed;
    }
    else if (e.timer <= 0.0f)
    {
        e.timer = world.rng.NextRange(0.8f, 2.0f);
        e.dir = static_cast<Dir>(world.rng.NextInt(0, 3));
    }
    if (!MoveActor(world, e, DirX(e.dir) * s, DirY(e.dir) * s))
    {
        e.dir = static_cast<Dir>(world.rng.NextInt(0, 3));
        e.timer = world.rng.NextRange(0.4f, 1.2f);
    }
    TouchDamage(world, e, 1);
}

void UpdateBat(World &world, Entity &e)
{
    Float dx, dy;
    const Float dist = DistanceToPlayer(world, e, dx, dy);
    e.timer -= TICK;
    if (e.state == 0)
    {
        e.z = 4.0f;
        if (dist < 72.0f && e.timer <= 0.0f)
        {
            e.state = 1;
            e.timer = world.rng.NextRange(1.8f, 2.8f);
            PlaySfx(Sfx::BOUNCE, 0.3f, 1.6f);
        }
    }
    else
    {
        e.z = 8.0f + std::sin(e.anim * 9.0f) * 3.0f;
        const Float len = std::max(1.0f, dist);
        const Float wobble = std::sin(e.anim * 5.0f) * 0.8f;
        MoveActor(world, e, dx / len * 0.9f - dy / len * wobble, dy / len * 0.9f + dx / len * wobble);
        if (e.timer <= 0.0f)
        {
            e.state = 0;
            e.timer = world.rng.NextRange(0.8f, 1.4f);
        }
    }
    TouchDamage(world, e, 1);
}

void UpdateSeed(World &world, Entity &e)
{
    e.x += e.vx;
    e.y += e.vy;
    e.life -= TICK;
    if (e.life <= 0.0f || BoxHitsTiles(world.map, e.x - 2.0f, e.y - 4.0f, e.x + 2.0f, e.y))
    {
        e.alive = false;
        SpawnEffect(world, "dust", e.x, e.y - 2.0f, 0.2f);
        return;
    }
    const Player &p = world.player;
    if (std::abs(e.x - p.x) < 7.0f && e.y - 3.0f < p.y && e.y + 3.0f > p.y - 13.0f)
    {
        e.alive = false;
        HurtPlayer(world, 1, e.x - e.vx * 4.0f, e.y - e.vy * 4.0f);
    }
}

void UpdateBoss(World &world, Entity &e)
{
    Float dx, dy;
    DistanceToPlayer(world, e, dx, dy);
    const Bool enraged = e.hp <= 5;
    e.timer -= TICK;
    switch (e.state)
    {
    case BOSS_WALK:
    {
        e.dir = DirToward(dx, dy);
        const Float len = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
        MoveActor(world, e, dx / len * 0.45f, dy / len * 0.45f);
        if (e.timer <= 0.0f)
        {
            e.state = BOSS_AIM;
            e.timer = enraged ? 0.45f : 0.7f;
            e.dir = DirToward(dx, dy);
            PlaySfx(Sfx::BOSS_ROAR, 0.6f, 1.2f);
        }
        TouchDamage(world, e, 2);
        break;
    }
    case BOSS_AIM:
        if (e.timer <= 0.0f)
        {
            e.state = BOSS_CHARGE;
            e.timer = 3.0f;
        }
        TouchDamage(world, e, 2);
        break;
    case BOSS_CHARGE:
    {
        const Float speed = enraged ? 3.4f : 2.7f;
        if (world.tickCount % 6 == 0)
        {
            SpawnEffect(world, "dust", e.x - DirX(e.dir) * 10.0f, e.y - 2.0f, 0.25f);
        }
        if (!MoveActor(world, e, DirX(e.dir) * speed, DirY(e.dir) * speed) || e.timer <= 0.0f)
        {
            // Slammed into the wall: flips onto its back and is vulnerable.
            e.state = BOSS_STUNNED;
            e.timer = enraged ? 2.2f : 2.8f;
            e.value = 0;
            world.shake = 0.35f;
            PlaySfx(Sfx::THUD);
            for (Int32 i = 0; i < 4; ++i)
            {
                Entity &d = SpawnEffect(world, "dust", e.x + world.rng.NextRange(-12.0f, 12.0f),
                                        e.y - world.rng.NextRange(0.0f, 10.0f), 0.3f);
                d.vx = world.rng.NextRange(-0.8f, 0.8f);
            }
        }
        TouchDamage(world, e, 2);
        break;
    }
    case BOSS_STUNNED:
        // Three hits (counted in DamageEnemy) or the timer end flips it back over.
        if (e.timer <= 0.0f || e.value >= 3)
        {
            e.state = BOSS_RECOVER;
            e.timer = 0.6f;
            PlaySfx(Sfx::BOSS_ROAR, 0.7f);
            if (enraged)
            {
                // Shakes loose a couple of blobs.
                for (Int32 i = 0; i < 2; ++i)
                {
                    Entity blob;
                    blob.kind = Kind::BLOB;
                    blob.variant = "r";
                    blob.hp = 1;
                    blob.x = e.x + (i == 0 ? -20.0f : 20.0f);
                    blob.y = e.y;
                    blob.hw = 6.0f;
                    blob.hh = 5.0f;
                    blob.vz = 2.5f;
                    blob.z = 1.0f;
                    blob.state = 1;
                    blob.room = e.room;
                    world.spawned.push_back(blob);
                }
            }
        }
        break;
    case BOSS_RECOVER:
        if (e.timer <= 0.0f)
        {
            e.state = BOSS_WALK;
            e.timer = world.rng.NextRange(1.2f, 2.0f) * (enraged ? 0.7f : 1.0f);
        }
        break;
    case BOSS_DYING:
    default:
        break;
    }
}
} // namespace

void UpdateEnemy(World &world, Entity &e)
{
    if (e.hurt > 0.0f)
    {
        e.hurt -= TICK;
    }
    if (e.knockT > 0.0f)
    {
        e.knockT -= TICK;
        MoveActor(world, e, e.knockX, e.knockY);
        if (e.kind != Kind::BOSS)
        {
            return;
        }
    }
    switch (e.kind)
    {
    case Kind::BLOB:
        UpdateBlob(world, e);
        break;
    case Kind::POD:
        UpdatePod(world, e);
        break;
    case Kind::BEETLE:
        UpdateWalker(world, e, 0.5f, 1.3f);
        break;
    case Kind::ANT:
        UpdateWalker(world, e, 0.8f, 1.2f);
        break;
    case Kind::BAT:
        UpdateBat(world, e);
        break;
    case Kind::SEED:
        UpdateSeed(world, e);
        break;
    case Kind::BOSS:
        UpdateBoss(world, e);
        break;
    default:
        break;
    }
}
} // namespace smallcap
