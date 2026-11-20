#include <audio/embedded_player.h>
#include <kernel/function.h>
#include <kernel/heap.h>
#include <kernel/memory.h>
#include <ui/game_window.h>

uint32_t IsndSeInstance = 0;

X86_FUNC_IMPL(__imp__sub_55A80);
X86_OVERRIDE_FUNC(sub_55A80)
{

    __imp__sub_55A80(ctx, base);

     IsndSeInstance = ctx.eax.u32;
}
enum class GameSoundID
{
    Cursor = 0xE002,
    Decide = 0xE003,
    Cancel = 0xE004,
    Cannot = 0xE005

};

static const std::unordered_map<std::string_view, GameSoundID> g_GameSoundMap =
{
    { "cursor", GameSoundID::Cursor },
    { "decide", GameSoundID::Decide },
    { "cancel", GameSoundID::Cancel },
    { "cannot_decide", GameSoundID::Cannot },
};

void Game_PlaySound(X86Context* ctx, const char* pName)
{
    if (EmbeddedPlayer::s_isActive)
    {
        EmbeddedPlayer::Play(pName);
    }
    else if (ctx)
    {
        auto it = g_GameSoundMap.find(pName); // example of game using play sound: 0x292DAC
        if (it != g_GameSoundMap.end())
        {
            GameSoundID id = it->second;
            GuestToHostFunction<void, CallConvention::Thiscall>(*ctx, sub_25BF80, IsndSeInstance, id, 0, 0);
        }

    }

}
