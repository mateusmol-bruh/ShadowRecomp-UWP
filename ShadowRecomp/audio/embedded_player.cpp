#include <audio/audio.h>
#include <audio/embedded_player.h>
#include <user/config.h>
#include <ui/game_window.h>

#include <res/music/installer.ogg.h>
#include <res/sounds/sys_menu_ok.ogg.h>
#include <res/sounds/sys_menu_cancel.ogg.h>
#include <res/sounds/sys_menu_cursor.ogg.h>
#include <res/sounds/sys_menu_invalid.ogg.h>


enum class EmbeddedSound
{
    Cursor,
    Decide,
    CannotDecide,
    CancelDecide,
    Count
};

struct EmbeddedSoundData
{
    MIX_Audio* audio{};
};

static std::array<EmbeddedSoundData, size_t(EmbeddedSound::Count)> g_embeddedSoundData = {};

static MIX_Mixer* g_mixer;
static MIX_Track* g_musicTrack;
static std::array<MIX_Track*, 8> g_soundTracks;

static float GetMasterVolume()
{
    if (Config::MuteOnFocusLost && !GameWindow::s_isFocused)
        return 0.0f;

    return Config::MasterVolume;
}

static const std::unordered_map<std::string_view, EmbeddedSound> g_embeddedSoundMap =
{
    { "cursor", EmbeddedSound::Cursor },
    { "decide", EmbeddedSound::Decide },
    { "cancel", EmbeddedSound::CancelDecide },
    { "cannot_decide", EmbeddedSound::CannotDecide },
};

static size_t g_channelIndex;

static void PlayEmbeddedSound(EmbeddedSound s)
{
    EmbeddedSoundData& data = g_embeddedSoundData[size_t(s)];
    if (data.audio == nullptr)
    {
        // The sound hasn't been created yet, create it and pick it.
        const void* soundData = nullptr;
        size_t soundDataSize = 0;
        switch (s)
        {
        case EmbeddedSound::Cursor:
            soundData = g_sys_menu_cursor;
            soundDataSize = sizeof(g_sys_menu_cursor);
            break;
        case EmbeddedSound::Decide:
            soundData = g_sys_menu_ok;
            soundDataSize = sizeof(g_sys_menu_ok);
            break;
        case EmbeddedSound::CannotDecide:
            soundData = g_sys_menu_invalid;
            soundDataSize = sizeof(g_sys_menu_invalid);
            break;
        case EmbeddedSound::CancelDecide:
            soundData = g_sys_menu_cancel;
            soundDataSize = sizeof(g_sys_menu_cancel);
            break;
        default:
            assert(false && "Unknown embedded sound.");
            return;
        }

        data.audio = MIX_LoadAudio_IO(g_mixer, SDL_IOFromConstMem(soundData, soundDataSize), true, true);
    }

    auto track = g_soundTracks[g_channelIndex % g_soundTracks.size()];
    if (!track || !data.audio)
        return;

    MIX_SetTrackAudio(track, data.audio);
    MIX_SetTrackGain(track, GetMasterVolume() * Config::EffectsVolume * EmbeddedPlayer::EFFECTS_VOLUME);
    MIX_PlayTrack(track, 0);
    ++g_channelIndex;
}

static MIX_Audio* g_installerMusic;

static bool InitMixer()
{
    if (g_mixer)
        return true;

    if (!MIX_Init())
        return false;

    SDL_AudioSpec spec{ SDL_AUDIO_F32, 2, XAUDIO_SAMPLES_HZ };
    g_mixer = MIX_CreateMixerDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (!g_mixer)
    {
        MIX_Quit();
        return false;
    }

    for (auto& track : g_soundTracks)
    {
        track = MIX_CreateTrack(g_mixer);
    }

    return true;
}

bool EmbeddedPlayer::Init()
{
    if (!InitMixer())
        return false;

    s_isActive = true;
    g_musicTrack = MIX_CreateTrack(g_mixer);
    if (!g_musicTrack)
        return false;

    g_installerMusic = MIX_LoadAudioNoCopy(g_mixer, g_installer_music, sizeof(g_installer_music), false);
    if (!g_installerMusic || !MIX_SetTrackAudio(g_musicTrack, g_installerMusic))
    {
        MIX_DestroyTrack(g_musicTrack);
        g_musicTrack = nullptr;
        return false;
    }
    return true;
}

void EmbeddedPlayer::Play(const char* name)
{
    if (!InitMixer())
    {
        return;
    }

    auto it = g_embeddedSoundMap.find(name);
    if (it == g_embeddedSoundMap.end())
    {
        return;
    }

    PlayEmbeddedSound(it->second);
}

void EmbeddedPlayer::PlayMusic()
{
    if (!g_musicTrack)
        return;

    if (!MIX_TrackPlaying(g_musicTrack))
    {
        auto options = SDL_CreateProperties();
        SDL_SetNumberProperty(options, MIX_PROP_PLAY_LOOPS_NUMBER, -1);
        MIX_PlayTrack(g_musicTrack, options);
        SDL_DestroyProperties(options);
    }

    MIX_SetTrackGain(g_musicTrack, 1.0f);
}

void EmbeddedPlayer::FadeOutMusic()
{
    if (g_musicTrack && MIX_TrackPlaying(g_musicTrack))
        MIX_StopTrack(g_musicTrack, MIX_TrackMSToFrames(g_musicTrack, 1000));
}

bool EmbeddedPlayer::IsPlayingSound()
{
    return std::any_of(g_soundTracks.begin(), g_soundTracks.end(), [](MIX_Track* track) { return track && MIX_TrackPlaying(track); });
}

bool EmbeddedPlayer::IsPlayingMusic()
{
    return g_musicTrack && MIX_TrackPlaying(g_musicTrack);
}

void EmbeddedPlayer::Shutdown()
{
    if (!g_mixer)
    {
        s_isActive = false;
        return;
    }

    MIX_DestroyMixer(g_mixer);
    g_mixer = nullptr;
    g_musicTrack = nullptr;
    g_soundTracks.fill(nullptr);

    for (EmbeddedSoundData& data : g_embeddedSoundData)
    {
        MIX_DestroyAudio(data.audio);
        data.audio = nullptr;
    }

    MIX_DestroyAudio(g_installerMusic);
    g_installerMusic = nullptr;

    MIX_Quit();
    s_isActive = false;
}
