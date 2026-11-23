#pragma once
#include "platform/types.h"

namespace crittercap
{
enum class Sfx : UInt8
{
    SWORD,
    SPIN,
    CHARGE,
    HIT,
    ENEMY_DIE,
    HURT,
    CUT,
    POT,
    GEM,
    HEART,
    KEY,
    CHEST,
    ITEM,
    TEXT,
    DOOR,
    UNLOCK,
    SHUTTER,
    PUSH,
    SWITCH,
    SECRET,
    SHRINK,
    GROW,
    ROLL,
    SHOOT,
    BOUNCE,
    BOSS_ROAR,
    BOSS_HIT,
    BOSS_DIE,
    THUD,
    MENU,
    DIE,
    THROW,
    CAUGHT,
    LEVEL_UP,
    HEAL,
    COUNT
};

enum class Music : UInt8
{
    NONE,
    TITLE,
    VILLAGE,
    WOODS,
    TINY,
    DUNGEON,
    BOSS,
    ENDING,
    BATTLE,
    COUNT
};

Bool InitializeAudio();
void ShutdownAudio();
void PlaySfx(Sfx sfx, Float volume = 1.0f, Float pitch = 1.0f);
void PlayMusic(Music music);
} // namespace crittercap
