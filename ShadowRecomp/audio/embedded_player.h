#pragma once

struct EmbeddedPlayer
{
    // Arbitrarily picked volume to match the mixing in the original game.
    static constexpr float MUSIC_VOLUME = 0.8f;
    static constexpr float EFFECTS_VOLUME = 0.7f;

    static inline bool s_isActive = false;

    static bool Init();
    static void Play(const char* name);
    static void PlayMusic();
    static void FadeOutMusic();
    static bool IsPlayingSound();
    static bool IsPlayingMusic();
    static void Shutdown();
};
