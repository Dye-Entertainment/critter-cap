#include "game/critters.h"

#include "compiled/warning_off.h"
#include <algorithm>
#include <cstdlib>
#include <sstream>
#include "compiled/warning_on.h"

namespace crittercap
{
namespace
{
const MoveDef MOVES[] = {
    {"Tackle", Type::NORMAL, 35, 95},       // 0
    {"Leaf Flick", Type::LEAF, 40, 100},    // 1
    {"Bubble", Type::WATER, 40, 100},       // 2
    {"Ember", Type::FIRE, 40, 100},         // 3
    {"Seed Shot", Type::LEAF, 35, 95},      // 4
    {"Headbutt", Type::NORMAL, 45, 90},     // 5
    {"Horn Jab", Type::BUG, 45, 95},        // 6
    {"Gust", Type::AIR, 40, 100},           // 7
    {"Bite", Type::NORMAL, 45, 95},         // 8
    {"Pinch", Type::BUG, 35, 100},          // 9
    {"Leaf Storm", Type::LEAF, 65, 90},     // 10
    {"Tidal Splash", Type::WATER, 65, 90},  // 11
    {"Flare Burst", Type::FIRE, 65, 90},    // 12
    {"Wing Slash", Type::AIR, 60, 95},      // 13
    {"Shell Slam", Type::NORMAL, 60, 90},   // 14
    {"Bramble Rush", Type::BUG, 70, 90},    // 15
    {"Swarm", Type::BUG, 60, 95},           // 16
};

const SpeciesDef SPECIES[SPECIES_COUNT] = {
    {"blob_g", "Sproutle", Type::LEAF, 50, 48, 50, 45, 190, "blob_g", false, {0, 1, 10},
     "A leafy blob that naps in the sun. Chews through bushes in seconds."},
    {"blob_b", "Drizzlet", Type::WATER, 55, 45, 52, 43, 190, "blob_b", false, {0, 2, 11},
     "Wobbles like a dew drop. Leaves a trail of puddles wherever it bounces."},
    {"blob_r", "Emberlob", Type::FIRE, 46, 54, 44, 52, 190, "blob_r", false, {0, 3, 12},
     "Always warm to the touch. Villagers borrow one on cold nights."},
    {"pod", "Podpop", Type::LEAF, 44, 50, 40, 50, 200, "pod", true, {4, 5, 10},
     "Spits seeds from its snout. The seeds grow into more bushes."},
    {"beetle", "Shellby", Type::BUG, 56, 50, 66, 30, 150, "beetle", true, {0, 6, 14},
     "Its shell is harder than an acorn. Slow, but very stubborn."},
    {"bat", "Flitter", Type::AIR, 40, 46, 36, 70, 180, "bat", false, {7, 8, 13},
     "Flutters in dark roots. Sleeps upside down in old caps."},
    {"ant", "Nibbit", Type::BUG, 38, 52, 40, 60, 220, "ant", true, {8, 9, 16},
     "Never alone for long. Where there is one Nibbit, there are ninety."},
    {"boss", "Bramblord", Type::BUG, 95, 70, 80, 40, 45, "boss", true, {6, 14, 15},
     "The beetle king of Root Hollow. It swallowed the Sunstone's light."},
};
} // namespace

const SpeciesDef &Species(const Int32 index)
{
    return SPECIES[std::clamp(index, 0, SPECIES_COUNT - 1)];
}

const MoveDef &Move(const Int32 index)
{
    constexpr Int32 count = static_cast<Int32>(sizeof(MOVES) / sizeof(MOVES[0]));
    return MOVES[std::clamp(index, 0, count - 1)];
}

Int32 FindSpecies(const std::string &id)
{
    for (Int32 i = 0; i < SPECIES_COUNT; ++i)
    {
        if (id == SPECIES[i].id)
        {
            return i;
        }
    }
    return -1;
}

const Char *TypeName(const Type type)
{
    switch (type)
    {
    case Type::LEAF:
        return "LEAF";
    case Type::WATER:
        return "WATER";
    case Type::FIRE:
        return "FIRE";
    case Type::BUG:
        return "BUG";
    case Type::AIR:
        return "AIR";
    case Type::NORMAL:
    default:
        return "NORMAL";
    }
}

Float Effectiveness(const Type attack, const Type defend)
{
    switch (attack)
    {
    case Type::FIRE:
        if (defend == Type::LEAF || defend == Type::BUG)
            return 2.0f;
        if (defend == Type::WATER || defend == Type::FIRE)
            return 0.5f;
        break;
    case Type::WATER:
        if (defend == Type::FIRE)
            return 2.0f;
        if (defend == Type::LEAF || defend == Type::WATER)
            return 0.5f;
        break;
    case Type::LEAF:
        if (defend == Type::WATER)
            return 2.0f;
        if (defend == Type::FIRE || defend == Type::BUG || defend == Type::AIR || defend == Type::LEAF)
            return 0.5f;
        break;
    case Type::BUG:
        if (defend == Type::LEAF)
            return 2.0f;
        if (defend == Type::FIRE || defend == Type::AIR)
            return 0.5f;
        break;
    case Type::AIR:
        if (defend == Type::BUG || defend == Type::LEAF)
            return 2.0f;
        break;
    default:
        break;
    }
    return 1.0f;
}

Int32 Critter::MaxHp() const
{
    return Def().hp * level / 25 + level + 12;
}

Int32 Critter::Attack() const
{
    return Def().attack * level / 25 + 5;
}

Int32 Critter::Defense() const
{
    return Def().defense * level / 25 + 5;
}

Int32 Critter::Speed() const
{
    return Def().speed * level / 25 + 5;
}

Int32 Critter::XpToNext() const
{
    return level * 8 + 10;
}

Int32 Critter::MoveCount() const
{
    return level >= LEARN_LEVEL ? 3 : 2;
}

Int32 Critter::MoveAt(const Int32 slot) const
{
    return Def().moves[std::clamp(slot, 0, 2)];
}

Critter MakeCritter(const Int32 species, const Int32 level)
{
    Critter c;
    c.species = species;
    c.level = std::clamp(level, 1, MAX_LEVEL);
    c.hp = c.MaxHp();
    return c;
}

Bool ParseCritter(const std::string &text, Critter &out)
{
    const size_t colon = text.find(':');
    const Int32 species = FindSpecies(text.substr(0, colon));
    if (species < 0)
    {
        return false;
    }
    const Int32 level = colon == std::string::npos ? 5 : std::atoi(text.c_str() + colon + 1);
    out = MakeCritter(species, level);
    return true;
}

Bool RollEncounter(const std::string &table, math::Random &rng, Critter &out)
{
    struct Entry
    {
        Int32 species;
        Int32 low;
        Int32 high;
        Int32 weight;
    };
    std::vector<Entry> entries;
    Int32 total = 0;
    std::stringstream stream(table);
    std::string item;
    while (std::getline(stream, item, ','))
    {
        // id:low-high:weight
        Entry e{-1, 3, 3, 1};
        std::stringstream parts(item);
        std::string id, levels, weight;
        std::getline(parts, id, ':');
        std::getline(parts, levels, ':');
        std::getline(parts, weight, ':');
        e.species = FindSpecies(id);
        if (e.species < 0)
        {
            continue;
        }
        const size_t dash = levels.find('-');
        e.low = std::atoi(levels.c_str());
        e.high = dash == std::string::npos ? e.low : std::atoi(levels.c_str() + dash + 1);
        e.weight = weight.empty() ? 1 : std::max(1, std::atoi(weight.c_str()));
        total += e.weight;
        entries.push_back(e);
    }
    if (entries.empty())
    {
        return false;
    }
    Int32 roll = rng.NextInt(0, total - 1);
    for (const Entry &e : entries)
    {
        if (roll < e.weight)
        {
            out = MakeCritter(e.species, rng.NextInt(e.low, std::max(e.low, e.high)));
            return true;
        }
        roll -= e.weight;
    }
    return false;
}

std::string SpeciesSprite(const Int32 species, const Bool back, const Int32 frame)
{
    const SpeciesDef &def = Species(species);
    const std::string base = def.sprite;
    const std::string f = std::to_string(frame % 2);
    if (def.directional)
    {
        return base + (back ? "_up_" : "_down_") + f;
    }
    return base + "_" + f;
}

std::string FollowerSprite(const Int32 species, const Int32 dir, const Int32 frame, Bool &flip)
{
    const SpeciesDef &def = Species(species);
    const std::string base = def.sprite;
    const std::string f = std::to_string(frame % 2);
    flip = dir == 2;
    if (def.directional)
    {
        const char *name = dir == 0 ? "_down_" : (dir == 1 ? "_up_" : "_right_");
        return base + name + f;
    }
    return base + "_" + f;
}
} // namespace crittercap
