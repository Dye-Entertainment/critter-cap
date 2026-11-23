#include "audio/sound.h"

#include "audio/audio_manager.h"
#include "miniaudio/audio/audio_backend.h"
#include "miniaudio/audio/sound_instance.h"
#include "resource/asset_manager.h"
#include "utils/logger.h"

#include "compiled/warning_off.h"
#include <array>
#include <string>
#include "compiled/warning_on.h"

namespace smallcap
{
namespace
{
struct SfxInfo
{
    const Char *file;
    UInt32 voices;
};

constexpr std::array<SfxInfo, static_cast<size_t>(Sfx::COUNT)> SFX_INFO = {{
    {"sword", 3},     {"spin", 2},    {"charge", 1},  {"hit", 4},       {"enemy_die", 3},
    {"hurt", 2},      {"cut", 4},     {"pot", 3},     {"gem", 4},       {"heart", 2},
    {"key", 2},       {"chest", 1},   {"item", 1},    {"text", 3},      {"door", 2},
    {"unlock", 1},    {"shutter", 2}, {"push", 1},    {"switch", 2},    {"secret", 1},
    {"shrink", 1},    {"grow", 1},    {"roll", 2},    {"shoot", 3},     {"bounce", 3},
    {"boss_roar", 1}, {"boss_hit", 2}, {"boss_die", 1}, {"thud", 2},    {"menu", 2},
    {"die", 1},
}};

constexpr std::array<const Char *, static_cast<size_t>(Music::COUNT)> MUSIC_FILES = {
    nullptr, "title", "village", "woods", "tiny", "dungeon", "boss", "ending"
};

struct AudioState
{
    Bool initialized = false;
    std::array<audio::SoundPoolId, static_cast<size_t>(Sfx::COUNT)> pools{};
    std::array<resource::AssetProxy<resource::SoundStream>, static_cast<size_t>(Music::COUNT)>
        streams{};
    audio::SoundInstance *music = nullptr;
    Music current = Music::NONE;
};

AudioState state;
} // namespace

Bool InitializeAudio()
{
    state.pools.fill(audio::INVALID_SOUND_POOL);
    auto *manager = audio::AudioManager::CreateInstance();
    if (!manager->Initialize(new miniaudio::audio::AudioBackend()))
    {
        LOGWARNING("Audio device unavailable, continuing without sound");
        return false;
    }
    for (size_t i = 0; i < SFX_INFO.size(); ++i)
    {
        const std::string path = std::string("game://sounds/") + SFX_INFO[i].file + ".wav";
        state.pools[i] = manager->CreateSoundPool(resource::AssetPath(path), SFX_INFO[i].voices);
        if (state.pools[i] == audio::INVALID_SOUND_POOL)
        {
            LOGERROR("Failed to load %s", path.c_str());
        }
    }
    manager->SetMasterVolume(0.8f);
    state.initialized = true;
    return true;
}

void ShutdownAudio()
{
    if (!state.initialized)
    {
        return;
    }
    if (state.music)
    {
        audio::AudioManager::GetInstance()->StopSound(state.music);
        state.music = nullptr;
    }
    // Drop our references before the engine frees its asset pools.
    for (auto &stream : state.streams)
    {
        stream = {};
    }
    audio::AudioManager::GetInstance()->Destroy();
    state.initialized = false;
}

void PlaySfx(const Sfx sfx, const Float volume, const Float pitch)
{
    if (!state.initialized)
    {
        return;
    }
    audio::AudioManager::GetInstance()->PlaySound(
        state.pools[static_cast<size_t>(sfx)], audio::SoundParameters{volume, 0.0f, pitch}
    );
}

void PlayMusic(const Music music)
{
    if (!state.initialized || music == state.current)
    {
        return;
    }
    auto *manager = audio::AudioManager::GetInstance();
    if (state.music)
    {
        manager->StopSound(state.music);
        state.music = nullptr;
    }
    state.current = music;
    if (music == Music::NONE)
    {
        return;
    }
    auto &stream = state.streams[static_cast<size_t>(music)];
    if (!stream.isValid())
    {
        // Tracks decode fully into memory, so they are loaded the first time they play.
        const std::string path =
            std::string("game://music/") + MUSIC_FILES[static_cast<size_t>(music)] + ".flac";
        stream = resource::AssetManager::GetInstance()->Load<resource::SoundStream>(
            resource::AssetPath(path)
        );
        if (!stream.isValid())
        {
            LOGERROR("Failed to load music %s", path.c_str());
            return;
        }
    }
    state.music = manager->PlaySoundStream(stream, true);
    if (state.music)
    {
        // The decoded buffer is shared between plays; restart it from the beginning.
        ma_sound *sound = static_cast<miniaudio::audio::SoundInstance *>(state.music)->sound;
        ma_sound_seek_to_pcm_frame(sound, 0);
        ma_sound_set_volume(sound, 0.5f);
    }
}
} // namespace smallcap
