#pragma once
#include "math/random.h"
#include "platform/types.h"

#include "compiled/warning_off.h"
#include <string>
#include <vector>
#include "compiled/warning_on.h"

namespace crittercap
{
enum class Type : UInt8
{
    NORMAL,
    LEAF,
    WATER,
    FIRE,
    BUG,
    AIR,
    COUNT
};

struct MoveDef
{
    const Char *name;
    Type type;
    Int32 power;
    // Chance to hit, in percent.
    Int32 accuracy;
};

struct SpeciesDef
{
    const Char *id;
    const Char *name;
    Type type;
    Int32 hp;
    Int32 attack;
    Int32 defense;
    Int32 speed;
    // 0..255, higher is easier to catch.
    Int32 catchRate;
    // Sprite name prefix and how the sprite set is laid out (see SpeciesSprite).
    const Char *sprite;
    Bool directional;
    // Two starting moves and one learned at LEARN_LEVEL.
    Int32 moves[3];
    const Char *dex;
};

constexpr Int32 SPECIES_COUNT = 8;
constexpr Int32 LEGEND_SPECIES = 7;
constexpr Int32 LEARN_LEVEL = 10;
constexpr Int32 MAX_LEVEL = 50;
constexpr size_t PARTY_SIZE = 4;

const SpeciesDef &Species(Int32 index);
const MoveDef &Move(Int32 index);
Int32 FindSpecies(const std::string &id);
const Char *TypeName(Type type);
// Damage multiplier of an attack of type `attack` against a critter of type `defend`.
Float Effectiveness(Type attack, Type defend);

struct Critter
{
    Int32 species = 0;
    Int32 level = 1;
    Int32 hp = 1;
    Int32 xp = 0;

    const SpeciesDef &Def() const
    {
        return Species(species);
    }
    const Char *Name() const
    {
        return Def().name;
    }
    Int32 MaxHp() const;
    Int32 Attack() const;
    Int32 Defense() const;
    Int32 Speed() const;
    Int32 XpToNext() const;
    // Moves known at the current level.
    Int32 MoveCount() const;
    Int32 MoveAt(Int32 slot) const;
    Bool Fainted() const
    {
        return hp <= 0;
    }
};

Critter MakeCritter(Int32 species, Int32 level);
// "blob_g:5" -> species + level; returns false when the species is unknown.
Bool ParseCritter(const std::string &text, Critter &out);
// Picks a wild critter from an encounter table such as "blob_g:3-5:40,ant:4-6:20".
Bool RollEncounter(const std::string &table, math::Random &rng, Critter &out);

// Sprite names: the front view (facing the player) and the back view (the player's side).
std::string SpeciesSprite(Int32 species, Bool back, Int32 frame);
// Overworld walking sprite for the follower; dir is 0 down, 1 up, 2 left, 3 right.
std::string FollowerSprite(Int32 species, Int32 dir, Int32 frame, Bool &flip);
} // namespace crittercap
