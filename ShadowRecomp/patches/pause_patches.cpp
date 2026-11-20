#include <kernel/memory.h>
#include <ui/options_menu.h>

#include "aspect_ratio_patches.h"
#include "prompt_patches.h"
#include <locale/locale.h>
#include <gpu/video.h>
#include <ui/imgui_utils.h>
#include <gpu/imgui/imgui_snapshot.h>
#include <user/config.h>
#include <input/padstate.h>
#include <patches/patch_utils.h>
#include <exports.h>
#include <ui/achievement_menu.h>
#include "multiplayer_mod_patches.h"


// We patch pause to add an extra Options in the menu
// This works by intercepting CSD data on the fly (see aspect_ratio_patches.cpp)
// Once we detect that the game is about to draw the pause text (resume, restart, quit etc.) we prevent it to display
// The options can then be re created through Imgui

static const uint8_t vanillaPauseItemsCount = 3u;
static const uint8_t pauseItemsCount = 5u;
static const uint8_t addedPauseItemsCount = pauseItemsCount - vanillaPauseItemsCount;

// (0.1562 * 640) = ~100
static constexpr float vanillaPauseLabelWidth = 100.0f;

enum PauseMode : uint8_t
{
	ModeRunningP1 = 2,
	ModeRunningP2 = 3,
	ModeRestart = 5,
	ModeQuit = 6,
	ModeContinue = 9,
	ModeOptions = 15
};

enum PauseSelection : uint8_t
{
	SelContinue = 0,
	SelRestart,
	SelAchievements,
	SelOptions,
	SelQuit
};

// Return value enum from sub_5AAF0
enum PauseAction : uint8_t
{
	PauseAction_None = 0,
	PauseAction_Resume = 1,
	PauseAction_StoryRoute = 2, 
	PauseAction_Restart = 3,
	PauseAction_Quit = 4,
	// added pause actions:
	PauseAction_Options = 5,
	PauseAction_Achievements,
};


static bool g_pauseCSD = false; // ensure our CSD patch worked
static bool g_pauseBoss = false; // CSD pause for boss is different and require pos adjust
static ImFont* g_fntDFPZong;

static inline bool s_isVisible = false;
static inline bool s_quitRequest = false;
static inline bool s_OptionsRequest = false;
static inline bool s_quitOptionsRequest = false;
static inline bool s_AchievementRequest = false;
static inline uint8_t s_currentMode = 0;
static inline uint8_t g_PlayerPauseMode = 0; // mode is 2 if player 1 open pause, but 3 if it's player 2.
static inline bool g_animDonePlaying = false;

static int8_t s_index = 0;
static uint32_t g_pauseModeAddr = 0;
static uint32_t g_pauseRestartAddr = 0;
static bool g_up{};
static bool g_upWasHeld{};
static bool g_down{};
static bool g_downWasHeld{};
static bool g_left{};
static bool g_leftWasHeld{};
static bool g_right{};
static bool g_rightWasHeld{};
static bool g_isAccepted{};
static bool g_inputAllowed = false;
static bool g_isBPressed{};
static bool g_waitBRelease{};

static bool g_isDrawingSubtitle = false;
static bool g_isDrawingMessageTask = false;

void TogglePauseCSD(bool toggle)
{
	g_pauseCSD = toggle;
}

void TogglePauseBossCSD(bool toggle)
{
	g_pauseBoss = toggle;
}

void ToggleMessageTaskDraw(bool toggle)
{
	g_isDrawingMessageTask = toggle;
}

const uint8_t GetAddedPauseItemsCount()
{
	return addedPauseItemsCount;
}

void RequestQuitOptionForPause()
{
	s_quitOptionsRequest = true;
}

ImVec2 VirtualToScreen(float vx, float vy, ImVec2 displaySize)
{
	return {
		(vx / 640.0f) * displaySize.x,
		(vy / 480.0f) * displaySize.y
	};
}

static float ScaleX(float vWidth, float displayWidth) { return (vWidth / 640.0f) * displayWidth; }
static float ScaleY(float vHeight, float displayHeight) { return (vHeight / 480.0f) * displayHeight; }

// The game removes "restart" option from the pause menu in 2 players mode or when player 1 has no life left
static bool IsRestartAvailable()
{
	if (g_pauseRestartAddr == 0) // pause object not seen yet, assume vanilla menu
		return true;

	auto base = g_memory.base;

	return X86_LOAD_U8(g_pauseRestartAddr) != 0;
}

static uint8_t BuildPauseItems(uint8_t(&items)[pauseItemsCount])
{
	uint8_t count = 0;

	for (uint8_t i = 0; i < pauseItemsCount; i++)
	{
		if (i == SelRestart && !IsRestartAvailable())
			continue;

		items[count++] = i;
	}

	return count;
}

// The game only knows 3 options slots for the pause menu
static uint32_t GetVanillaCursorIndex(int8_t selection)
{
	switch (selection)
	{
	case SelRestart:
		return 1;
	case SelQuit:
		return 2;
	default:
		return 0;
	}
}


static int8_t GetPauseItemRow(const uint8_t* items, uint8_t count, int8_t selection)
{
	for (uint8_t i = 0; i < count; i++)
	{
		if (items[i] == selection)
			return (int8_t)i;
	}

	return 0;
}

static uint8_t GetPausePadIndex()
{
	const uint32_t port = GetMultiplayerPausePort();
	if (port < 4)
	{
		return port;
	}
	return g_PlayerPauseMode == ModeRunningP2 ? 1 : 0;
}

static bool IsHudVisible()
{
	auto base = g_memory.base;
	return X86_LOAD_U8(0x54DE78) != 0;
}

static bool IsCustomPauseActive()
{
	return g_pauseCSD || !IsHudVisible();
}

static void ResetPauseInputs(bool upIsHeld, bool downIsHeld, bool leftIsHeld, bool rightIsHeld)
{
	g_up = false;
	g_upWasHeld = upIsHeld;
	g_down = false;
	g_downWasHeld = downIsHeld;
	g_left = false;
	g_leftWasHeld = leftIsHeld;
	g_right = false;
	g_rightWasHeld = rightIsHeld;
	g_isAccepted = false;
}

void DrawPauseText(X86Context* ctx)
{
	if (s_isVisible == false || IsCustomPauseActive() == false) // ensure our CSD patch worked so we can draw pause text safely
		return;

	auto base = g_memory.base;

	if (g_pauseModeAddr)
	{
		// Picking "No" on the quit or restart prompt make our custom text of the pause menu
		// overlapping the prompt text, so we hide it while any prompt is running.
		auto mode = X86_LOAD_U32(g_pauseModeAddr);
		if (mode > 3 || s_OptionsRequest || s_AchievementRequest || IsMessageBoxActive())
		{
			g_inputAllowed = false;
			return;
		}
	}

	uint8_t playerIndex = GetPausePadIndex();


	bool upIsHeld = false;
	bool downIsHeld = false;
	bool leftIsHeld = false;
	bool rightIsHeld = false;



	if (auto pInputState = PJSPeripheral::GetControllerSlot(playerIndex))
	{
		if (g_inputAllowed)
		{
			if (PJSPeripheral::IsHeld(playerIndex, PJSPeripheral::BTN_DPAD_UP) || -pInputState->leftY > 0.5f)
				upIsHeld = true;

			if (!g_upWasHeld && upIsHeld)
				g_up = true;

			if (PJSPeripheral::IsHeld(playerIndex, PJSPeripheral::BTN_DPAD_DOWN) || -pInputState->leftY < -0.5f)
				downIsHeld = true;

			if (!g_downWasHeld && downIsHeld)
				g_down = true;

			if (PJSPeripheral::IsHeld(playerIndex, PJSPeripheral::BTN_DPAD_LEFT) || -pInputState->leftX > 0.5f)
				leftIsHeld = true;

			if (!g_leftWasHeld && leftIsHeld)
				g_left = true;

			if (PJSPeripheral::IsHeld(playerIndex, PJSPeripheral::BTN_DPAD_RIGHT) || -pInputState->leftX < -0.5f)
				rightIsHeld = true;

			if (!g_rightWasHeld && rightIsHeld)
				g_right = true;

			if (PJSPeripheral::IsJustPressed(playerIndex, PJSPeripheral::BTN_A) || PJSPeripheral::IsJustPressed(playerIndex, PJSPeripheral::BTN_START))
				g_isAccepted = true;
		}
	}


	auto scrollUp = g_up;
	auto scrollDown = g_down;

	uint8_t items[pauseItemsCount];
	const uint8_t itemCount = BuildPauseItems(items);

	int8_t row = GetPauseItemRow(items, itemCount, s_index);
	s_index = items[row];

	if (g_inputAllowed)
	{
		if (scrollUp)
		{
			--row;

			if (row < 0)
			{
				row = itemCount - 1;
				Game_PlaySound(ctx, "cursor");
			}

			s_index = items[row];
		}
		else if (scrollDown)
		{
			++row;

			if (row < itemCount && items[row] == SelQuit)
			{
				Game_PlaySound(ctx, "cursor");
			}

			if (row >= itemCount)
			{
				row = 0;
				Game_PlaySound(ctx, "cursor");
			}

			s_index = items[row];
		}

		if (g_isAccepted)
		{
			switch (s_index)
			{
			case SelOptions:
				Game_PlaySound(ctx, "decide");
				s_OptionsRequest = true;
				break;
			case SelAchievements:
				Game_PlaySound(ctx, "decide");
				s_AchievementRequest = true;
				break;
			case SelQuit:
				s_quitRequest = true;
				break;
			}
		}
	}


	if (IsHudVisible() == false)
	{
		ResetPauseInputs(upIsHeld, downIsHeld, leftIsHeld, rightIsHeld);
		return;
	}

	auto drawList = ImGui::GetBackgroundDrawList();
	auto& res = ImGui::GetIO().DisplaySize;
	ImVec2 screenCentre = { res.x / 2.0f, res.y / 2.0f };


	// virtual size of the texture sprite
	float vWidth = 120.0f;
	float vHeight = 26.0f;

	const float offsetY = 30.f;

	float virtualItemSpacing = 20.0f; // pixels in 640x480 space
	float virtualStartY = g_pauseBoss ? 200.f : 125.f;

	virtualStartY += virtualItemSpacing * 0.5f * (float)(pauseItemsCount - itemCount);

	// the pause "box" is unstretched around the center of the screen (640x480 layout)
	const float scaleY = g_hudUnstretchY;
	virtualStartY = 240.0f + (virtualStartY - 240.0f) * scaleY;

	// convert to screen space, centered horizontally

	float screenY = VirtualToScreen(0, virtualStartY, res).y;


	const char* menuItems[] = { Localise("Pause_Menu_Continue").c_str(),
		Localise("Pause_Menu_Restart").c_str(), Localise("Achievements_Name").c_str(),
		Localise("Pause_Menu_Options").c_str(), Localise("Pause_Menu_Quit").c_str()
	};
	float startY = screenY;
	float spacing = ScaleY(20.0f * scaleY, res.y);

	// the pause box is a lot smaller once unstretched, so long strings need a smaller font.
	float fontSize = Scale(Config::Language == ELanguage::Japanese ? 19 : 18);
	const float maxTextWidth = ScaleX(vanillaPauseLabelWidth * g_hudUnstretchX, res.x);
	float widestText = 0.0f;

	for (uint8_t i = 0; i < itemCount; i++)
		widestText = std::max(widestText, g_fntDFPZong->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, menuItems[items[i]]).x);

	// keep a smaller font if we exceed the size of the text
	float rowOffsetY = 0.0f;

	if (widestText > maxTextWidth)
	{
		const float fittedSize = fontSize * maxTextWidth / widestText;
		rowOffsetY = (fontSize - fittedSize) * 0.5f;
		fontSize = fittedSize;
	}

	for (uint8_t i = 0; i < itemCount; i++)
	{
		uint8_t selection = items[i];
		uint8_t color = selection == s_index ? 255 : 150;
		AddTextCentered(drawList, g_fntDFPZong, fontSize,
			res.x / 2.0f,
			startY + i * spacing + rowOffsetY,
			IM_COL32(color, color, color, 255),  // white text
			IM_COL32(0, 0, 0, 255),        // black outline
			menuItems[selection]);
	}

	ResetPauseInputs(upIsHeld, downIsHeld, leftIsHeld, rightIsHeld);
}

// TPauseManagerExec
X86_FUNC_IMPL(__imp__sub_5AEE0);
X86_OVERRIDE_FUNC(sub_5AEE0)
{
	auto flagdisabled = X86_LOAD_U8(ctx.ecx.u32 + 0x3C);

	g_pauseModeAddr = ctx.ecx.u32 + 0x30;
	g_pauseRestartAddr = ctx.ecx.u32 + 0x3D;
	auto mode = X86_LOAD_U32(g_pauseModeAddr);

	if (mode == ModeRunningP1 || mode == ModeRunningP2)
	{
		g_PlayerPauseMode = mode;
	}

	if (mode < ModeRunningP1 || mode > 3)
	{
		g_inputAllowed = false;
		if (mode < ModeRunningP1)
			s_index = SelContinue;
	}

	// handle new options in pause menu by tricking the game that we shouldn't quit
	if (s_currentMode != 0 && (s_OptionsRequest || s_AchievementRequest))
	{
		if (OptionsMenu::s_isVisible == false && AchievementMenu::s_isVisible == false)
		{
			s_currentMode = 0;
			X86_STORE_U32(g_pauseModeAddr, g_PlayerPauseMode);
			s_OptionsRequest = false;
			s_AchievementRequest = false;
			s_quitOptionsRequest = false;
			s_isVisible = true;
		}
	}
	else if (!s_currentMode)
	{
		if (s_index == SelOptions && s_OptionsRequest && OptionsMenu::s_isVisible == false)
		{
			s_currentMode = PauseAction_Options;
			OptionsMenu::Open(true, GetPausePadIndex());
		}
		else if (s_index == SelAchievements && s_AchievementRequest && AchievementMenu::s_isVisible == false)
		{
			s_currentMode = PauseAction_Achievements;
			AchievementMenu::Open(GetPausePadIndex());
		}

		if (s_currentMode != 0)
		{
			s_isVisible = false;
			// The original ret 4 pops the return address and the frame-time argument.
			ctx.AddEsp(2);
			return;
		}
	}




	__imp__sub_5AEE0(ctx, base);

	s_isVisible = !flagdisabled && (mode == ModeRunningP1 || mode == ModeRunningP2);
}

void PauseBCheckMidAsmHook(X86Context& __restrict ctx, uint8_t* base)
{
	g_isBPressed = GetPadSlot(ctx.ebp.u32)->justPressed & PJSPeripheral::BTN_B;
}

// This function check input and set cursor for the pause menu
// Because we add a new option not intended, we don't want this to mess with the real cursor pose 
// Thus we patch this function to trick the game as much as possible that cursor pos is fine
X86_FUNC_IMPL(__imp__sub_5AAF0);
X86_OVERRIDE_FUNC(sub_5AAF0)
{

	const uint32_t cursorAddr = ctx.ecx.u32 + 0x38;

	const bool isCustomPause = IsCustomPauseActive();

	if (isCustomPause) // ensure our CSD patch worked so we can allow custom input properly
	{
		g_animDonePlaying = X86_LOAD_U8(ctx.ecx.u32 + 0x4D) != 0;
		if (X86_LOAD_U8(ctx.ecx.u32 + 0x4D) == 0 || s_OptionsRequest || s_AchievementRequest) // pause anim is playing, don't allow input yet
		{
			ctx.eax.u32 = PauseAction_None;
			ctx.AddEsp(1);
			return;
		}


		// Put our index back into vanilla range before handing off
		if (s_index <= SelRestart || s_index == SelQuit)
		{
			X86_STORE_U32(cursorAddr, GetVanillaCursorIndex(s_index));
		}
		else if (X86_LOAD_U32(cursorAddr) == 0)
		{
			// under specific situation, the cursor hack can sometimes lead to having 
			// the validation sfx play twice: first per the game and second per our new option check, 
			// force value 1 so nothing play.
			X86_STORE_U32(cursorAddr, 1);
		}

		g_inputAllowed = g_animDonePlaying;
	}

	auto ecx = ctx.ecx.u32;

	g_isBPressed = false;
	__imp__sub_5AAF0(ctx, base);

	if (isCustomPause)
	{
		auto mode = X86_LOAD_U8(ecx + 0x38);

		uint32_t returnedAction = ctx.eax.u32;


		// Apply navigation delta to our extended cursor

		if (returnedAction != PauseAction_None && returnedAction != PauseAction_StoryRoute)
		{
			if (s_index <= SelRestart || s_index == SelQuit)
			{
				// Vanilla item is selected, we let the return value propagate to caller
				// so it will handle Resume/Restart/Quit normally
				X86_STORE_U32(cursorAddr, GetVanillaCursorIndex(s_index));
			}
			else
			{
				// new option menu is selected set an unused index
				ctx.eax.u32 = (uint32_t)PauseAction_Restart + s_index;
			}
		}
	}

	// B resumes like start on continue
	if (g_isBPressed && ctx.eax.u32 == PauseAction_None)
	{
		ctx.eax.u32 = PauseAction_Resume;
		g_waitBRelease = true;
	}
}

// Story route "Y" press
// adding in "B" press
X86_FUNC_IMPL(__imp__sub_5A240);
X86_OVERRIDE_FUNC(sub_5A240)
{
	const bool isAnimDone = X86_LOAD_U8(ctx.ecx.u32 + 0x4D) != 0;
	__imp__sub_5A240(ctx, base);
	const uint32_t pausePort = GetMultiplayerPausePort();
	const uint32_t port = pausePort < 4 ? pausePort : X86_LOAD_U32(0x5356E4);
	if (ctx.eax.u8_lo == 0 && isAnimDone && ((GetPadSlot(port)->justPressed & PJSPeripheral::BTN_B) || RouteMenuAnyPlayerClose()))
	{
		ctx.eax.u8_lo = 1;
	}
}

// Unpause 'hold'
// A/X/Start are checked normally, adding B
X86_FUNC_IMPL(__imp__sub_2A7F30);
X86_OVERRIDE_FUNC(sub_2A7F30)
{
	const uint32_t port = X86_LOAD_U32(ctx.esp.u32 + 0x8);
	__imp__sub_2A7F30(ctx, base);
	if (g_waitBRelease && ctx.eax.u8_lo == 0)
	{
		g_waitBRelease = GetPadSlot(port)->buttons & PJSPeripheral::BTN_B;
		ctx.eax.u8_lo = g_waitBRelease;
	}
}

// Pause_DrawCurrentMissionText
X86_FUNC_IMPL(__imp__sub_282F80);
X86_OVERRIDE_FUNC(sub_282F80)
{
	g_isDrawingSubtitle = true;
	__imp__sub_282F80(ctx, base);
	g_isDrawingSubtitle = false;
}

// The pause menu draws text (current mission) using a function called DrawMessage (0x1E930). 
// Because we increases the pause options count, the menu now overflows the subtitles and it is drawn on top of the game.
// We patch this function to offset the Y position of the text on the RenderWare level, this avoid messing with directX matrix and corrupt the game.

static constexpr uint32_t drawMessageArgY = 0x14; // ESP parameter offset in the stack of DrawMessage for the Y position of the text to draw.

// Rt2d device state, (handled by Rt2dDeviceSetCamera (0x3081D0) and read by rt2dCTMGet (0x307E10).)
static constexpr uint32_t rt2dCamera = 0x582604;
static constexpr uint32_t rt2dDeviceWidth = 0x5824F4;
static constexpr uint32_t rt2dDeviceHeight = 0x5824F8;
static constexpr uint32_t rt2dFlatDepth = 0x5825D8;
static constexpr uint32_t rwCameraProjectionType = 0x14;
static constexpr uint32_t rwPARALLEL = 2;


static constexpr float subtitleOffsetPerItem = 0.03f;

// Helper function to get the ratio of Rt2d units to world units, 
// so we can scale the offset we measured in world units into Rt2d units.
static float GetRt2dUnitsPerWorldUnit()
{
	auto base = g_memory.base;

	const uint32_t camera = X86_LOAD_U32(rt2dCamera);

	if (camera == 0)
		return 0.0f;

	const float flat = X86_LOAD_U32(camera + rwCameraProjectionType) == rwPARALLEL ? 1.0f : X86_LOAD_F32(rt2dFlatDepth);
	const float worldPerRt2d = std::min(X86_LOAD_F32(rt2dDeviceWidth), X86_LOAD_F32(rt2dDeviceHeight)) * flat;

	return worldPerRt2d != 0.0f ? 1.0f / worldPerRt2d : 0.0f;
}


static float GetRt2dHeight()
{
	auto base = g_memory.base;

	const float width = X86_LOAD_F32(rt2dDeviceWidth);
	const float height = X86_LOAD_F32(rt2dDeviceHeight);
	const float shortSide = std::min(width, height);

	return shortSide != 0.0f ? height / shortSide : 1.0f;
}

// On a tall vertical res, the text only fills the middle of the screen, while the 3D view,
// the pause menu and stretched FMVs cover the whole height. Push the text away from the center
// by the same amount to fix it.
static float PushAwayFromCentreY(float y)
{
	if (g_hudUnstretchY >= 1.0f)
		return y;

	const float centreY = GetRt2dHeight() * 0.5f;

	return centreY + (y - centreY) / g_hudUnstretchY;
}

static constexpr uint32_t messageManager = 0x544F3C;
static constexpr uint32_t messageManagerMovieMode = 0x31; // set by StageMovie while an FMV plays


static bool ShouldPushMessageTaskText()
{
	auto base = g_memory.base;

	const uint32_t manager = X86_LOAD_U32(messageManager);
	const bool isMovie = manager != 0 && X86_LOAD_U8(manager + messageManagerMovieMode) != 0;

	return !(isMovie && Config::FMVAspectRatio.Value == EFMVAspectRatio::Fit);
}

// DrawMessage (used by both cutscene and pause menu)
X86_FUNC_IMPL(__imp__sub_1E930);
X86_OVERRIDE_FUNC(sub_1E930)
{
	const uint32_t argY = ctx.esp.u32 + drawMessageArgY;

	if (g_isDrawingSubtitle)
	{
		const float offset = subtitleOffsetPerItem * GetAddedPauseItemsCount() * GetRt2dUnitsPerWorldUnit();

		X86_STORE_F32(argY, X86_LOAD_F32(argY) - offset);
	}
	else if (g_isDrawingMessageTask && ShouldPushMessageTaskText())
	{
		X86_STORE_F32(argY, PushAwayFromCentreY(X86_LOAD_F32(argY)));
	}

	__imp__sub_1E930(ctx, base);
}

void PausePatchesInit()
{
	g_fntDFPZong = ImFontAtlasSnapshot::GetFont("DFPZongYiW5-B5.ttc");
}
