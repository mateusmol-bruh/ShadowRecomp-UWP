#include "installer_wizard.h"

#ifdef SHADOW_RECOMP_UWP
#include <os/uwp/platform.h>
#else
#include <nfd.h>
#endif
#include <audio/embedded_player.h>
#include <install/installer.h>
#include <gpu/video.h>
#include <gpu/frame_pacing/frame_pacer.h>
#include <locale/locale.h>
#include <user/config.h>
#include <ui/game_window.h>
#include <os/logger.h>
#include <input/input.h>
#include <thread>
#include <atomic>
#include <exports.h>
#include <gpu/imgui/imgui_snapshot.h>
#include <ui/imgui_utils.h>
#include <ui/fader.h>
#include <res/images/installer/installer_bg.png.h>
#include <res/images/common/TitleBg.png.h>
#include <res/images/common/Options2.png.h>
#include <res/images/installer/FoxLogo.png.h>
#include <decompressor.h>
#include <kernel/io/file_system.h>
#include "options_menu.h"
#include <res/credits.h>


static std::unique_ptr<GuestTexture> g_upTexInstallBG{};
static std::unique_ptr<GuestTexture> g_upTexOptionsBG2{};
static std::unique_ptr<GuestTexture> g_upTexFoxLogo{};

enum class WizardPage
{
	SelectLanguage,
	Introduction,
	SelectGame,
	CheckSpace,
	Installing,
	InstallSucceeded,
	InstallFailed,
};

static ImFont* g_fntSeurat;

// TODO: Probably nuke all this since this is just unleashed stuff
// Keeping for now incase we add the blue 'advertise' animation
static constexpr double SCANLINES_ANIMATION_TIME = 0.0;
static constexpr double SCANLINES_ANIMATION_DURATION = 15.0;

static constexpr double MILES_ICON_ANIMATION_TIME = SCANLINES_ANIMATION_TIME + 10.0;
static constexpr double MILES_ICON_ANIMATION_DURATION = 15.0;

static constexpr double IMAGE_ANIMATION_TIME = MILES_ICON_ANIMATION_TIME + MILES_ICON_ANIMATION_DURATION;
static constexpr double IMAGE_ANIMATION_DURATION = 15.0;

static constexpr double TITLE_ANIMATION_TIME = SCANLINES_ANIMATION_DURATION;
static constexpr double TITLE_ANIMATION_DURATION = 30.0;

static constexpr double CONTAINER_LINE_ANIMATION_TIME = SCANLINES_ANIMATION_DURATION;
static constexpr double CONTAINER_LINE_ANIMATION_DURATION = 23.0;

static constexpr double CONTAINER_OUTER_TIME = SCANLINES_ANIMATION_DURATION + CONTAINER_LINE_ANIMATION_DURATION;
static constexpr double CONTAINER_OUTER_DURATION = 23.0;

static constexpr double CONTAINER_INNER_TIME = SCANLINES_ANIMATION_DURATION + CONTAINER_LINE_ANIMATION_DURATION + 8.0;
static constexpr double CONTAINER_INNER_DURATION = 10.f;

static constexpr double ALL_ANIMATIONS_FULL_DURATION = CONTAINER_INNER_TIME + CONTAINER_INNER_DURATION;
static constexpr double QUITTING_EXTRA_DURATION = 60.0;

static float fontSize = 24.0f;
static float smallFontSize = 18.0f;
static float fileFontSize = 16.0f;
static float descFontSize = 18.0f;

// Shadow's orig res is 640x480, use this as a base for window scaling
static const float baseWidth = 640.0f;
static const float baseHeight = 480.0f;

constexpr float CONTAINER_X = 153.0f;
constexpr float CONTAINER_Y = 100.f;
constexpr float CONTAINER_WIDTH = 526.5f;
constexpr float CONTAINER_HEIGHT = 246.0f;
constexpr float SIDE_CONTAINER_WIDTH = CONTAINER_WIDTH / 2.0f;
constexpr float CONTAINER_BUTTON_WIDTH = 75.0f;
constexpr float CONTAINER_BUTTON_GAP = 10.0f;
constexpr float BUTTON_HEIGHT = 22.0f;
constexpr float BUTTON_TEXT_GAP = 28.0f;

static int g_selectedBtn = -1;

static WizardPage g_currentPage = WizardPage::SelectLanguage;
static std::filesystem::path g_installPath;
static std::string g_sourcePath;
static std::string g_statusMessage;

static std::unique_ptr<std::thread> g_installerThread;
static std::atomic<bool> g_installerFinished{ false };
static std::atomic<bool> g_installerFailed{ false };
static std::string g_installerErrorMessage;
static float g_installerProgress = 0.0f;
static std::atomic<float> g_installerProgressTarget{ 0.0f };
static std::atomic<const char*> g_installerCurrentFileName{ "" };
static std::atomic<bool> g_installerCompiling{ false };
static Installer::Sources g_installerSources;
static uint64_t g_installerAvailableSize = 0;
static std::unique_ptr<std::thread> g_parseThread;
static std::atomic<bool> g_parseFinished{ false };
static bool g_parseSucceeded = false;
static Installer::Sources g_parsedSources;
static std::string g_creditsStr;
static ELanguage g_creditsLanguage;
static double g_creditsScrollStart = -1.0;
static double g_bgFadeStart = -1.0;
static constexpr float BG_FADE_IN_DURATION = 1.0f;
static double g_bgFadeOutStart = -1.0;
static constexpr float BG_FADE_OUT_DURATION = 1.0f;
static constexpr float INSTALL_EXTRACT_RATIO_END = 0.8f;

static bool g_currentCursorAccepted = false;
static bool g_currentCursorBack = false;
static int g_currentCursorIndex = -1;
static int g_currentCursorDefault = 0;
static int g_lastCursorIndex = -1;
static WizardPage g_cursorPage = WizardPage::SelectLanguage;
static bool g_cursorPickDefault = false;
static bool g_mouseMoved = false;
static bool g_mouseOverButton = false;
static ImVec2 g_joypadAxis = {};
static bool g_isQuitting = false;
static bool g_browseClicked = false;
static double g_sourceDropTime = -1.0;
static float g_openProgress = 0.0f;
static bool g_isClosing = false;

static std::vector<std::pair<ImVec2, ImVec2>> g_currentCursorRects;

static void Quit()
{
	if (g_isQuitting)
	{
		return;
	}

	g_isQuitting = true;
	g_isClosing = true;

	EmbeddedPlayer::FadeOutMusic();
	Fader::FadeOut(1, []() { InstallerWizard::s_isVisible = false; });
}

static bool IsParsingSources()
{
	return g_parseThread != nullptr;
}

static void JoinParseThread()
{
	if (g_parseThread && g_parseThread->joinable())
	{
		g_parseThread->join();
	}
	g_parseThread.reset();
}

static void StartParseSources()
{
	g_parseFinished.store(false);
	g_parseThread = std::make_unique<std::thread>([sourcePath = g_sourcePath]()
	{
		std::string parseError;
		g_parseSucceeded = Installer::ParseSources(sourcePath, g_parsedSources, parseError);
		g_parseFinished.store(true);
	});
}

static std::string& GetWizardText(WizardPage page)
{
	switch (page)
	{
	case WizardPage::SelectLanguage: return Localise("Installer_Page_SelectLanguage");
	case WizardPage::Introduction: return Localise("Installer_Page_Introduction");
	case WizardPage::SelectGame: return Localise("Installer_Page_SelectGame");
	case WizardPage::CheckSpace: return Localise("Installer_Page_CheckSpace");
	case WizardPage::Installing: return Localise("Installer_Page_Installing");
	case WizardPage::InstallSucceeded: return Localise("Installer_Page_InstallSucceeded");
	}

	return g_localeMissing;
}

// These are ordered from bottom to top in a 3x2 grid.
const char* LANGUAGE_TEXT[] =
{
	"Français", // French
	"Deutsch", // German
	"English", // English
	"Español", // Spanish
	"Italiano", // Italian
	"日本語", // Japanese
};

const ELanguage LANGUAGE_ENUM[] =
{
	ELanguage::French,
	ELanguage::German,
	ELanguage::English,
	ELanguage::Spanish,
	ELanguage::Italian,
	ELanguage::Japanese,
};

static ImVec2 ComputeTextSize(ImFont* font, const char* text, float size, float& squashRatio, float maxTextWidth = FLT_MAX)
{
	ImVec2 textSize = font->CalcTextSizeA(size, FLT_MAX, 0.0f, text);
	if (textSize.x > maxTextWidth)
	{
		squashRatio = maxTextWidth / textSize.x;
	}
	else
	{
		squashRatio = 1.0f;
	}
	return textSize;
}


static bool IsPressedButton(ImVec2 min, ImVec2 max, bool& hovered, bool makeDefault = false)
{
	auto& io = ImGui::GetIO();

	int index = int(g_currentCursorRects.size());
	g_currentCursorRects.emplace_back(min, max);

	if (makeDefault)
		g_currentCursorDefault = index;

	bool pressed = false;
	bool mouseOver = io.MousePos.x >= min.x && io.MousePos.x <= max.x &&
		io.MousePos.y >= min.y && io.MousePos.y <= max.y;

	if (mouseOver && g_mouseMoved)
	{
		g_currentCursorIndex = index;
		g_mouseOverButton = true;
	}

	if (mouseOver && io.MouseClicked[0])
	{
		g_currentCursorIndex = index;
		pressed = true;
	}

	hovered = g_currentCursorIndex == index;

	if (hovered && g_currentCursorAccepted)
	{
		g_currentCursorAccepted = false;
		pressed = true;
	}

	return pressed;
}

static bool DrawTextButton(X86Context* ctx, ImVec2 min, ImVec2 max, const char* text, float textFontSize, bool enabled, const char* pressSound, bool makeDefault = false)
{
	auto drawList = ImGui::GetBackgroundDrawList();
	auto* font = g_fntSeurat;

	bool isHovered = false;
	bool isPressed = false;

	if (enabled)
		isPressed = IsPressedButton(min, max, isHovered, makeDefault);

	auto textSize = font->CalcTextSizeA(textFontSize, FLT_MAX, 0, text);
	ImVec2 textPos(min.x + ((max.x - min.x) - textSize.x) * 0.5f, min.y + ((max.y - min.y) - textSize.y) * 0.5f);

	if (isHovered)
	{
		drawList->AddText(font, textFontSize, textPos, IM_COL32(255, 255, 0, 255), text);
	}
	else
	{
		DrawTextWithShadow(font, textFontSize, textPos, IM_COL32(255, 255, 255, enabled ? 127 : 50), text, 1.5, 1, IM_COL32(0, 0, 0, 127));
	}

	if (isPressed)
		Game_PlaySound(ctx, pressSound);

	return isPressed;
}

static void DrawButton(X86Context* ctx, ImVec2 min, ImVec2 max, const char* buttonText, bool sourceButton, int i, bool btnEnabled, bool& btnPressed, float maxTextWidth = FLT_MAX, bool makeDefault = false)
{
	btnPressed = false;

	// default to english
	if (i == 2 && g_selectedBtn == -1)
	{
		btnPressed = true;
		btnEnabled = true;
	}

	auto drawList = ImGui::GetBackgroundDrawList();
	bool btnHovered = false;

	if (IsPressedButton(min, max, btnHovered, makeDefault) && !btnEnabled)
	{
		Game_PlaySound(ctx, "decide");
		btnPressed = true;
		g_selectedBtn = i;
	}

	float alpha = 255;
	if (!btnEnabled && !btnHovered)
	{
		alpha *= 0.5f;
	}

	ImFont* font = g_fntSeurat;
	float size = Scale(18.0f);
	float squashRatio;
	ImVec2 textSize = ComputeTextSize(font, buttonText, size, squashRatio, Scale(maxTextWidth));
	min.x += ((max.x - min.x) - textSize.x) / 2.0f;
	min.y += ((max.y - min.y) - textSize.y) / 2.0f;

	if (!sourceButton)
	{
		// Fixes slight misalignment caused by this particular font.
		min.y -= Scale(1.0f);
	}

	SetOrigin({ min.x + textSize.x / 2.0f, min.y });
	SetScale({ squashRatio, 1.0f });

	ImU32 textCol = IM_COL32(255, 255, 255, alpha);
	if (btnPressed || btnHovered || btnEnabled)
	{
		textCol = IM_COL32(255, 255, 0, alpha);
		drawList->AddText(font,
						  size,
						  min,
						  textCol,
						  buttonText);
	}
	else
	{
		DrawTextWithShadow(
			font,
			size,
			min,
			textCol,
			buttonText,
			1.5,
			1,
			IM_COL32(0, 0, 0, alpha)
		);
	}

	ResetGradient();
	SetScale({ 1.0f, 1.0f });
	SetOrigin({ 0.0f, 0.0f });
}

static void InstallerThread()
{
	auto progressCallback = [](const Installer::Progress& progress)
		{
			g_installerCurrentFileName.store(progress.currentFileName);
			g_installerProgressTarget.store(progress.ratio * INSTALL_EXTRACT_RATIO_END);
		};

	std::filesystem::path sourcePath(std::u8string_view(reinterpret_cast<const char8_t*>(g_sourcePath.c_str())));
	std::string errorMessage;
#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Installer: validating and copying game files.");
#endif
	bool success = Installer::Install(sourcePath, g_installPath, progressCallback, errorMessage);
	if (success)
	{
#ifdef SHADOW_RECOMP_UWP
        Uwp::LogStartup("Installer: files installed. Preparing shader and pipeline caches.");
#endif
		g_installerCompiling.store(true);

		Video::PrecompileShadersAndPipelinesForInstall([](float progress)
			{
				g_installerProgressTarget.store(INSTALL_EXTRACT_RATIO_END + (1.0f - INSTALL_EXTRACT_RATIO_END) * progress);
			});

		g_installerCompiling.store(false);
		g_installerProgressTarget.store(1.0f);
	}
	else
	{
#ifdef SHADOW_RECOMP_UWP
        Uwp::LogStartup("Installer: " + errorMessage);
#endif
		LOGF_ERROR("Installing from {} failed: {}", g_sourcePath, errorMessage);

		g_installerProgressTarget.store(1.0f);
		g_installerFailed.store(true);
		g_installerErrorMessage = Localise("Installer_Message_InvalidFileSelected");
	}
	g_installerFinished.store(true);
}

static void DrawLanguagePicker(X86Context* ctx, ImVec2 panelMin, ImVec2 panelMax)
{
	bool buttonPressed = false;
	float panelWidth = panelMax.x - panelMin.x;
	float panelHeight = panelMax.y - panelMin.y;

	float buttonWidth = std::min(panelWidth * 0.38f, Scale(CONTAINER_BUTTON_WIDTH));
	float buttonH = Scale(BUTTON_HEIGHT);
	float gap = panelWidth * 0.3f;
	float bottomMargin = panelHeight * 0.375f;
	float rowStride = buttonH + Scale(CONTAINER_BUTTON_GAP);

	float leftMinX = panelMin.x + gap;
	float leftMaxX = leftMinX + buttonWidth;
	float rightMinX = panelMax.x - gap - buttonWidth;
	float rightMaxX = panelMax.x - gap;

	for (int i = 0; i < 6; i++)
	{
		float minX = (i < 3) ? leftMinX : rightMinX;
		float maxX = (i < 3) ? leftMaxX : rightMaxX;
		float minusY = rowStride * float(i % 3);
		float btnMaxY = panelMax.y - bottomMargin - minusY;
		float btnMinY = btnMaxY - buttonH;

		ImVec2 min = { minX, btnMinY };
		ImVec2 max = { maxX, btnMaxY };

		const bool isCurrent = i == g_selectedBtn || (g_selectedBtn == -1 && i == 2);

		DrawButton(ctx, min, max, LANGUAGE_TEXT[i], false, i,
			i == g_selectedBtn, buttonPressed, FLT_MAX, isCurrent);

		if (buttonPressed)
		{
			Config::Language = LANGUAGE_ENUM[i];
		}
	}
}


static void GetMainButtonRect(const ImVec2& panelMin, const ImVec2& panelMax, float scale, float width, ImVec2& min, ImVec2& max)
{
	min = { panelMin.x + ((panelMax.x - panelMin.x) - width * scale) * 0.5f, panelMax.y - (20.0f * scale) - (70.0f * scale) };
	max = { min.x + width * scale, min.y + 20.0f * scale };
}

static void DrawNextButton(X86Context* ctx, ImVec2& panelMin, ImVec2& panelMax, float scale, bool makeDefault = true)
{
	ImVec2 min, max;
	GetMainButtonRect(panelMin, panelMax, scale, 45.0f, min, max);

	if (DrawTextButton(ctx, min, max, Localise("Installer_Button_Next").c_str(), smallFontSize, true, "decide", makeDefault))
		g_currentPage = (WizardPage)((int)g_currentPage + 1);
}

static void DrawQuitButton(X86Context* ctx, ImVec2& panelMin, ImVec2& panelMax, float scale)
{
	ImVec2 min = { panelMin.x + ((panelMax.x - panelMin.x) - 45.0f * scale) * 0.5f, panelMax.y - (20.0f * scale) - (35.0f * scale) };
	ImVec2 max = { min.x + 45.0f * scale, min.y + 20.0f * scale };

	if (DrawTextButton(ctx, min, max, Localise("Installer_Button_Quit").c_str(), smallFontSize, true, "cancel"))
		Quit();
}

static void DrawPrevButton(X86Context* ctx, ImVec2& panelMin, ImVec2& panelMax, float scale, bool enabled = true)
{
	ImVec2 min = { panelMin.x + ((panelMax.x - panelMin.x) - 45.0f * scale) * 0.5f, panelMax.y - (20.0f * scale) - (35.0f * scale) };
	ImVec2 max = { min.x + 45.0f * scale, min.y + 20.0f * scale };

	if (DrawTextButton(ctx, min, max, Localise("Installer_Button_Back").c_str(), smallFontSize, enabled, "cancel"))
		g_currentPage = (WizardPage)((int)g_currentPage - 1);
}



static void DrawBackground(ImGuiViewport* viewport, ImVec2 winPos, ImVec2 winSize, float scale, float offsetX, float offsetY)
{
	auto drawList = ImGui::GetBackgroundDrawList();

	// Bevel at edges
	ImVec2 min = { winPos.x + offsetX + 21.0f * scale, winPos.y + offsetY + 54.0f * scale };
	ImVec2 max = { min.x + 598.0f * scale, min.y + 362.0f * scale };

	DrawShadowWindow(drawList, min, max, scale, g_openProgress);
}

static void DrawShadowImage()
{
	auto& res = ImGui::GetIO().DisplaySize;
	auto drawList = ImGui::GetBackgroundDrawList();

	// fade the background on the intro
	if (g_bgFadeStart < 0.0)
		g_bgFadeStart = ImGui::GetTime();

	float fade = std::clamp(float(ImGui::GetTime() - g_bgFadeStart) / BG_FADE_IN_DURATION, 0.0f, 1.0f);


	if (g_isClosing)
	{
		if (g_bgFadeOutStart < 0.0)
			g_bgFadeOutStart = ImGui::GetTime();

		fade *= 1.0f - std::clamp(float(ImGui::GetTime() - g_bgFadeOutStart) / BG_FADE_OUT_DURATION, 0.0f, 1.0f);
	}

	float borderPixels = 32.f; // Options BG has an extra black border at the bottom, we don't want that.
	float uvMaxY = 1.0f - (borderPixels / 512.0f);

	ImVec2 centre = { res.x / 2.0f, res.y / 2.0f };
	ImVec2 Min = { centre.x - centre.x, centre.y - centre.y };
	ImVec2 Max = { centre.x + centre.x, centre.y + centre.y  };

	drawList->AddImage
	(
		g_upTexInstallBG.get(),
		Min,
		Max,
		{ 0, 0 },
		{ 1, uvMaxY },
		IM_COL32(255, 255, 255, (int)(255 * fade))
	);


	// draw fire and white lines under the menu box
	drawList->PushClipRect(Min, Max, true);
	DrawAnimatedFire(Min, Max, fade);
	DrawLoopLines(Min, Max, fade);

	drawList->PopClipRect();
}


static void InitCreditsString()
{
	g_creditsLanguage = Config::Language.Value;
	g_creditsStr = Localise("Installer_Credits_Message");

	for (int i = 0; i < g_credits.size(); i++)
	{
		g_creditsStr += g_credits[i];
		if (i < g_credits.size() - 1)
			g_creditsStr += ",  ";
	}
}

static void DrawScrollingCredits(const ImVec2& min, const ImVec2& max, float scale)
{
	auto drawList = ImGui::GetBackgroundDrawList();
	auto* font = g_fntSeurat;

	if (g_creditsLanguage != Config::Language.Value)
		InitCreditsString();

	if (g_creditsScrollStart < 0.0)
		g_creditsScrollStart = ImGui::GetTime() + 1.0;

	auto textSize = font->CalcTextSizeA(fileFontSize, FLT_MAX, 0, g_creditsStr.c_str());
	float bandWidth = max.x - min.x;
	float textY = min.y + ((max.y - min.y) - textSize.y) * 0.5f;

	drawList->PushClipRect(min, max, true);

	auto drawText = [&](float x) { DrawTextWithShadow(font, fileFontSize, ImVec2(x, textY), IM_COL32(255, 255, 255, 255), g_creditsStr.c_str(), 1.5, 1, IM_COL32(0, 0, 0, 127)); };

	if (textSize.x <= bandWidth)
	{
		drawText(min.x + (bandWidth - textSize.x) * 0.5f);
	}
	else
	{
		float totalWidth = textSize.x + 80.0f * scale;
		double elapsed = ImGui::GetTime() - g_creditsScrollStart;
		float offset = elapsed > 0.0 ? (float)fmod(elapsed * 50.0f * scale, totalWidth) : 0.0f;

		drawText(min.x - offset);
		drawText(min.x - offset + totalWidth);
	}

	drawList->PopClipRect();
}

static void DrawWizardParagraph(const char* text, float maxWidth, float lineMargin, const ImVec2& pos)
{
	auto draw = [](const char* str, float size, ImVec2 p) { DrawTextWithShadow(g_fntSeurat, size, p, IM_COL32(255, 255, 255, 255), str, 1.5, 1, IM_COL32(0, 0, 0, 127)); };

	DrawRubyAnnotatedText(g_fntSeurat, descFontSize, maxWidth, pos, lineMargin, text,
		[&](const char* str, ImVec2 p) { draw(str, descFontSize, p); },
		draw);
}


static void ResetCursorRects()
{
	g_currentCursorDefault = 0;
	g_currentCursorRects.clear();
}

static void CheckCancelAction(X86Context* ctx)
{
	if (!g_currentCursorBack)
		return;

	g_currentCursorBack = false;

	if (g_isClosing || g_openProgress < 1.0f)
		return;

	switch (g_currentPage)
	{
	case WizardPage::SelectLanguage:
		Game_PlaySound(ctx, "cancel");
		Quit();
		break;

	case WizardPage::Introduction:
	case WizardPage::CheckSpace:
		Game_PlaySound(ctx, "cancel");
		g_currentPage = (WizardPage)((int)g_currentPage - 1);
		break;

	case WizardPage::SelectGame:
		if (!IsParsingSources())
		{
			Game_PlaySound(ctx, "cancel");
			g_currentPage = WizardPage::Introduction;
		}
		break;
	default:
		break;
	}
}

static void FinishCursorFrame(X86Context* ctx)
{
	if (g_mouseMoved && !g_mouseOverButton)
		g_currentCursorIndex = -1;


	if (g_cursorPickDefault && !g_currentCursorRects.empty())
	{
		g_cursorPickDefault = false;

		if (input::g_inputDevice != input::EInputDevice::Mouse)
		{
			g_currentCursorIndex = g_currentCursorDefault;
			g_lastCursorIndex = g_currentCursorIndex;
		}
	}

	if (g_currentCursorIndex != g_lastCursorIndex)
	{
		if (g_currentCursorIndex >= 0)
			Game_PlaySound(ctx, "cursor");

		g_lastCursorIndex = g_currentCursorIndex;
	}

	CheckCancelAction(ctx);
	g_currentCursorAccepted = false;
}



void InstallerWizard::Shutdown()
{
	JoinParseThread();

	if (g_installerThread && g_installerThread->joinable())
		g_installerThread->join();
	g_installerThread.reset();

	g_currentPage = WizardPage::SelectLanguage;
	g_sourcePath.clear();
	g_statusMessage.clear();
	g_sourceDropTime = -1.0;
	g_installerFinished.store(false);
	g_installerFailed.store(false);
	g_installerProgress = 0.0f;
}


void InstallerWizard::Draw(X86Context* ctx)
{
	if (!s_isVisible)
		return;

	auto& io = ImGui::GetIO();

	ResetCursorRects();
	g_mouseMoved = io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f;
	g_mouseOverButton = false;


	if (g_cursorPage != g_currentPage)
	{
		g_cursorPage = g_currentPage;
		g_currentCursorIndex = -1;
		g_lastCursorIndex = -1;
		g_cursorPickDefault = true;
	}


	auto drawList = ImGui::GetBackgroundDrawList();
	auto& displaySize = io.DisplaySize;
	ImGuiViewport* viewport = ImGui::GetMainViewport();

	// Scale using the actual whole viewport instead of the many other ways googling will give you
	ImVec2 winPos = viewport->Pos;
	ImVec2 winSize = viewport->Size;

	// Scale values based on window size
	float scaleX = winSize.x > 1.0f ? (winSize.x / baseWidth) : 1.0f;
	float scaleY = winSize.y > 1.0f ? (winSize.y / baseHeight) : 1.0f;
	float scale = fminf(scaleX, scaleY);


	// Center to screen based on Shadow res
	float canvasW = baseWidth * scale;
	float canvasH = baseHeight * scale;
	float offsetX = (winSize.x - canvasW) * 0.5f;
	float offsetY = (winSize.y - canvasH) * 0.5f;

	drawList->AddRectFilled(ImVec2(0, 0), displaySize, IM_COL32(0, 0, 0, 255));

	auto* font = g_fntSeurat;

	float verticalOffset = 40.0f * scale;

	ImVec2 panelMin = ImVec2(
		winPos.x + offsetX + (26.0f * scale),
		winPos.y + offsetY + (59.0f * scale)
	);
	ImVec2 panelMax = ImVec2(
		panelMin.x + (587.0f * scale),
		panelMin.y + (351.0f * scale)
	);

	float panelWidth = panelMax.x - panelMin.x;
	float centerX = (panelMin.x + panelMax.x) * 0.5f;
	float centerY = (panelMin.y + panelMax.y) * 0.5f;

	float baseFontSize = 18.0f * scale;
	fontSize = baseFontSize;
	descFontSize = baseFontSize + std::floor(baseFontSize * 0.2f);
	smallFontSize = std::floor(baseFontSize * 0.9f);
	fileFontSize = std::floor(baseFontSize * 0.7f);

	// prevent the anim stutter
	if (ImGui::GetTime() < 1.0)
	{
		return;
	}

	// recreate the message box opening and closing anim
	g_openProgress = StepOpenProgress(g_openProgress, g_isClosing);

	DrawShadowImage();

	if (g_isClosing && g_openProgress <= 0.0f)
	{
		bool bgFadedOut = g_bgFadeOutStart >= 0.0 && ImGui::GetTime() - g_bgFadeOutStart >= BG_FADE_OUT_DURATION;

		if (!g_isQuitting && bgFadedOut && !EmbeddedPlayer::IsPlayingSound() && !EmbeddedPlayer::IsPlayingMusic())
		{
			s_isVisible = false;
		}
		return;
	}


	if (g_openProgress > 0.0f)
		DrawBackground(viewport, winPos, winSize, scale, offsetX, offsetY);

	if (g_openProgress < 1.0f)
	{
		return;
	}


	std::string wizardText = GetWizardText(g_currentPage);
	char descriptionText[512];
	strncpy(descriptionText, wizardText.c_str(), sizeof(descriptionText) - 1);
	auto originMargin = 65.0f * scale;
	auto originMin = ImVec2(panelMin.x + originMargin, panelMin.y + originMargin);
	auto originMax = ImVec2(panelMax.x - originMargin, panelMax.y - originMargin);
	auto textMargin = 5.0f;

	auto textMaxWidth = originMax.x - (descFontSize / 2.0f) - originMin.x;
	auto textSize = MeasureCentredParagraph(g_fntSeurat, descFontSize, textMaxWidth, textMargin, wizardText.c_str());
	ImVec2 textPosition = ImVec2(panelMin.x + (panelWidth - textSize.x) * 0.5f, originMin.y);

	if (g_currentPage != WizardPage::Installing && g_currentPage != WizardPage::InstallFailed)
	{
		DrawWizardParagraph(wizardText.c_str(), textMaxWidth, textMargin, textPosition);
	}

	switch (g_currentPage)
	{
	case WizardPage::SelectLanguage:
	{
		DrawLanguagePicker(ctx, panelMin, panelMax);

		DrawNextButton(ctx, panelMin, panelMax, scale, false);

		DrawQuitButton(ctx, panelMin, panelMax, scale);
	}
	break;
	case WizardPage::Introduction:
	{
		DrawPrevButton(ctx, panelMin, panelMax, scale);

		DrawNextButton(ctx, panelMin, panelMax, scale);
	}
	break;
	case WizardPage::SelectGame:
	{
		if (IsParsingSources() && g_parseFinished.load())
		{
			JoinParseThread();

			if (g_parseSucceeded)
			{
				g_installerSources = std::move(g_parsedSources);
				g_statusMessage.clear();
				g_currentPage = WizardPage::CheckSpace;
			}
			else
			{
				g_statusMessage = Localise("Installer_Message_InvalidFileSelected");
			}
		}

		const bool isParsing = IsParsingSources();

		ImVec2 inputMin = ImVec2(
			panelMin.x + (74.0f * scale),
			panelMin.y + (141.0f * scale)
		);
		float fieldBaseHeight = 32.0f;
		ImVec2 inputMax = ImVec2(
			inputMin.x + (350.0f * scale),
			inputMin.y + (fieldBaseHeight * scale)
		);

		float pathClipWidth = (inputMax.x - (8.0f * scale)) - (inputMin.x + (8.0f * scale));

		const bool isNoFileSelected = g_sourcePath.empty();
		const std::string displayPathStorage = isNoFileSelected ? Localise("Installer_Page_NoFileSelected") : std::string(g_sourcePath);
		const char* displayPath = displayPathStorage.c_str();

		// expand the field if the text is too big (different languages moment)
		auto displayTextSize = font->CalcTextSizeA(fileFontSize, FLT_MAX, isNoFileSelected ? pathClipWidth : 0.0f, displayPath);
		if (isNoFileSelected)
		{
			float fieldNeededHeight = (displayTextSize.y / scale) + 12.0f;
			fieldBaseHeight = std::max(fieldBaseHeight, fieldNeededHeight);
			inputMax.y = inputMin.y + (fieldBaseHeight * scale);
		}

		DrawScaledRect(drawList, winPos, scale, offsetX, offsetY, 100.0f, 200.0f, 350.0f, fieldBaseHeight, IM_COL32(0, 0, 40, 255));

		bool pathOverflows = !isNoFileSelected && displayTextSize.x > pathClipWidth;
		std::string overflowRender;
		const char* pathToShow = displayPath;

		if (pathOverflows)
		{
			// Find the longest tail of displayPath that, when prefixed with
			// "...", still fits within pathClipWidth.
			const std::string ellipsis = "...";
			size_t dlen = strlen(displayPath);
			size_t tailLen = dlen;
			while (tailLen > 1)
			{
				std::string candidate = ellipsis + std::string(displayPath + (dlen - tailLen), tailLen);
				auto candSize = font->CalcTextSizeA(fileFontSize, FLT_MAX, 0.0f, candidate.c_str());
				if (candSize.x <= pathClipWidth)
				{
					overflowRender = std::move(candidate);
					break;
				}
				tailLen--;
			}
			if (overflowRender.empty())
				overflowRender = ellipsis;
			pathToShow = overflowRender.c_str();
		}

		drawList->AddText(font, fileFontSize, ImVec2(inputMin.x + (10.0f * scale), inputMin.y + ((inputMax.y - inputMin.y) - displayTextSize.y) * 0.5f), IM_COL32(255, 255, 255, 127), pathToShow, nullptr, pathClipWidth);

		// Create a short yellow flash on the field to confirm the drop worked
		if (g_sourceDropTime >= 0.0)
		{
			float flash = 1.0f - float(ImGui::GetTime() - g_sourceDropTime) / 0.5f;
			if (flash > 0.0f)
				drawList->AddRect(inputMin, inputMax, IM_COL32(255, 255, 0, int(255 * flash)), 0.0f, 0, 2.0f * scale);
			else
				g_sourceDropTime = -1.0;
		}

		const std::string browseTextStorage = Localise("Installer_Header_Browse");
		const char* browseText = browseTextStorage.c_str();
		float browseFontSize = smallFontSize;
		auto browsetextSize = font->CalcTextSizeA(browseFontSize, FLT_MAX, 0, browseText);

		// Ensure "browse" button doesn't overflow since some language have way longer text.
		float btnX = inputMax.x + (15.0f * scale);
		float btnMaxWidth = (panelMax.x - (20.0f * scale)) - btnX;
		if (browsetextSize.x > btnMaxWidth)
		{
			browseFontSize = std::floor(browseFontSize * (btnMaxWidth / browsetextSize.x));
			browsetextSize = font->CalcTextSizeA(browseFontSize, FLT_MAX, 0, browseText);
		}

		float btnWidth = std::max(80.0f * scale, browsetextSize.x);
		float btnHeight = 20.0f * scale;
		float btnY = inputMin.y + ((inputMax.y - inputMin.y) - btnHeight) * 0.5f;


		if (DrawTextButton(ctx, ImVec2(btnX, btnY), ImVec2(btnX + btnWidth, btnY + btnHeight), browseText, browseFontSize, !isParsing, "decide", g_sourcePath.empty()))
			g_browseClicked = true;

		DrawPrevButton(ctx, panelMin, panelMax, scale, !isParsing);

		if (!g_statusMessage.empty())
		{
			float statusPadding = 6.0f * scale;
			float statusMaxWidth = panelWidth - (statusPadding * 2.0f);
			float statusFontSize = smallFontSize;
			auto statusSize = font->CalcTextSizeA(statusFontSize, FLT_MAX, 0.0f, g_statusMessage.c_str());
			if (statusSize.x > statusMaxWidth)
			{
				statusFontSize = std::floor(statusFontSize * (statusMaxWidth / statusSize.x));
			}
			drawList->AddText(font, statusFontSize, ImVec2(panelMin.x + statusPadding, panelMin.y + statusPadding), IM_COL32(255, 100, 100, 255), g_statusMessage.c_str(), nullptr, statusMaxWidth);
		}

		if (!g_sourcePath.empty())
		{
			ImVec2 nextMin, nextMax;
			GetMainButtonRect(panelMin, panelMax, scale, 45.0f, nextMin, nextMax);

			if (DrawTextButton(ctx, nextMin, nextMax, Localise("Installer_Button_Next").c_str(), smallFontSize, !isParsing, "decide", true))
			{
				std::error_code spaceErrorCode;
				std::filesystem::path spacePath = g_installPath;
				while (spacePath.has_parent_path() && spacePath != spacePath.parent_path() && !std::filesystem::exists(spacePath, spaceErrorCode))
				{
					spacePath = spacePath.parent_path();
				}

				g_installerAvailableSize = 0;
				std::filesystem::space_info spaceInfo = std::filesystem::space(spacePath, spaceErrorCode);
				if (!spaceErrorCode)
				{
					g_installerAvailableSize = spaceInfo.available;
				}

				g_statusMessage.clear();
				StartParseSources();
			}
		}
	}
	break;
	case WizardPage::CheckSpace:
	{
		constexpr double DivisorGiB = (1024.0 * 1024.0 * 1024.0);
		double requiredGiB = double(g_installerSources.totalSize) / DivisorGiB;
		double availableGiB = double(g_installerAvailableSize) / DivisorGiB;

		char requiredSpaceText[128];
		char availableSpaceText[128];
		snprintf(requiredSpaceText, sizeof(requiredSpaceText), Localise("Installer_Step_RequiredSpace").c_str(), requiredGiB);
		snprintf(availableSpaceText, sizeof(availableSpaceText), (g_installerAvailableSize > 0) ? Localise("Installer_Step_AvailableSpace").c_str() : "", availableGiB);
		char spaceText[512];
		snprintf(spaceText, sizeof(spaceText), "%s\n%s", requiredSpaceText, availableSpaceText);

		auto spaceTextSize = MeasureCentredParagraph(g_fntSeurat, descFontSize, textMaxWidth, textMargin, spaceText);
		ImVec2 spaceTextPosition = ImVec2(panelMin.x + (panelWidth - spaceTextSize.x) * 0.5f, textPosition.y + textSize.y + descFontSize);

		DrawWizardParagraph(spaceText, textMaxWidth, textMargin, spaceTextPosition);

		DrawPrevButton(ctx, panelMin, panelMax, scale);

		ImVec2 installMin, installMax;
		GetMainButtonRect(panelMin, panelMax, scale, 45.0f, installMin, installMax);

		if (DrawTextButton(ctx, installMin, installMax, Localise("Common_Install").c_str(), smallFontSize, true, "decide", true))
		{
			g_currentPage = WizardPage::Installing;
			g_installerFinished.store(false);
			g_installerFailed.store(false);
			g_installerCompiling.store(false);
			g_installerProgress = 0.0f;
			g_installerProgressTarget.store(0.0f);
			g_installerThread = std::make_unique<std::thread>(InstallerThread);
		}
	}
	break;
	case WizardPage::Installing:
	{
		const char* headerText;
		if (g_installerCompiling.load())
		{
			headerText = Localise("Installer_Page_Compiling").c_str();
		}
		else
		{
			headerText = Localise("Installer_Page_Installing").c_str();
		}

		auto installTextSize = MeasureCentredParagraph(g_fntSeurat, descFontSize, textMaxWidth, textMargin, headerText);
		ImVec2 installTextPosition = ImVec2(panelMin.x + (panelWidth - installTextSize.x) * 0.5f, originMin.y);

		DrawWizardParagraph(headerText, textMaxWidth, textMargin, installTextPosition);

		float barPadding = 20.0f * scale;
		float barX = panelMin.x + barPadding;
		float barWidth = panelWidth - barPadding * 2.0f;
		float barHeight = 24.0f * scale;

		if (!g_installerCompiling.load())
		{
			const char* currentFileName = g_installerCurrentFileName.load();
			if (*currentFileName != '\0')
			{
				char fileText[512];
				auto fileTxt = Localise("Common_File") + ": %s";
				snprintf(fileText, sizeof(fileText), fileTxt.c_str(), currentFileName);
				ImVec2 fileTextPosition = ImVec2(barX, panelMin.y + (10.0f * scale));

				DrawTextParagraph(font, fileFontSize, barWidth, fileTextPosition, textMargin, fileText,
					[&](const char* str, ImVec2 pos) { DrawTextWithShadow(g_fntSeurat, fileFontSize, pos, IM_COL32(255, 255, 255, 127), str, 1.5, 1, IM_COL32(0, 0, 0, 127)); });
			}
		}

		ImVec2 barMin(barX, centerY - barHeight * 0.5f);
		ImVec2 barMax(barX + barWidth, centerY + barHeight * 0.5f);

		g_installerProgress = g_installerProgressTarget.load();

		drawList->AddRectFilled(barMin, barMax, IM_COL32(0, 0, 40, 255));

		float fillWidth = barWidth * std::min(g_installerProgress, 1.0f);
		if (fillWidth > 0)
		{
			ImVec2 fillMax(barX + fillWidth, centerY + barHeight * 0.5f);
			drawList->AddRectFilled(barMin, fillMax, IM_COL32(255, 255, 0, 255));
		}

		char percentText[32];
		snprintf(percentText, sizeof(percentText), "%.1f%%", g_installerProgress * 100.0f);
		auto percentSize = font->CalcTextSizeA(smallFontSize, FLT_MAX, 0, percentText);

		DrawTextWithShadow(font, smallFontSize, ImVec2(barX + (barWidth - percentSize.x) * 0.5f, (centerY - percentSize.y * 0.5f) + barHeight), IM_COL32(255, 255, 255, 255), percentText, 1.5, 1, IM_COL32(0, 0, 0, 127));

		if (g_installerFinished.load())
		{
			if (g_installerFailed.load())
			{
				g_currentPage = WizardPage::InstallFailed;
			}
			else
			{
				g_currentPage = WizardPage::InstallSucceeded;
			}
		}

		break;
	}

	case WizardPage::InstallSucceeded:
	{
		auto foxDev = "FoxWorkshop";

		auto foxSize = font->CalcTextSizeA(descFontSize, FLT_MAX, 0, foxDev);
		auto foxPositionY = textSize.y + textPosition.y + (25.0f * scale);


		float logoHeight = foxSize.y * 1.6f;
		float logoWidth = logoHeight * (520.0f / 482.0f);
		float logoGap = 8.0f * scale;
		float foxGroupX = centerX - (logoWidth + logoGap + foxSize.x) * 0.5f;
		float logoY = foxPositionY + (foxSize.y - logoHeight) * 0.5f;

		drawList->AddImage(g_upTexFoxLogo.get(), ImVec2(foxGroupX, logoY), ImVec2(foxGroupX + logoWidth, logoY + logoHeight),
			ImVec2(71.0f / 640.0f, 17.0f / 512.0f), ImVec2(591.0f / 640.0f, 499.0f / 512.0f));

		DrawTextWithShadow(font, descFontSize, ImVec2(foxGroupX + logoWidth + logoGap, foxPositionY), IM_COL32(255, 255, 255, 255), foxDev, 1.5, 1, IM_COL32(0, 0, 0, 127));

		float creditsPadding = 40.0f * scale;
		float creditsY = logoY + logoHeight + (10.0f * scale);
		DrawScrollingCredits(ImVec2(panelMin.x + creditsPadding, creditsY), ImVec2(panelMax.x - creditsPadding, creditsY + fileFontSize * 1.5f), scale);

		DrawQuitButton(ctx, panelMin, panelMax, scale);

		ImVec2 btnplayMin, btnplayMax;
		GetMainButtonRect(panelMin, panelMax, scale, 75.0f, btnplayMin, btnplayMax);

		if (DrawTextButton(ctx, btnplayMin, btnplayMax, Localise("Installer_Button_PlayGame").c_str(), smallFontSize, true, "decide", true))
		{
			g_isClosing = true;
			EmbeddedPlayer::FadeOutMusic();
		}
		break;
	}

	case WizardPage::InstallFailed:
	{
		const char* errorMsg = g_installerErrorMessage.empty() ? Localise("Installer_Page_InstallFailed").c_str() : g_installerErrorMessage.c_str();

		ImVec2 errorTextSize = MeasureCentredParagraph(g_fntSeurat, descFontSize, textMaxWidth, textMargin, errorMsg);
		ImVec2 errorTextPosition = ImVec2(panelMin.x + (panelWidth - errorTextSize.x) * 0.5f, originMin.y);

		DrawWizardParagraph(errorMsg, textMaxWidth, textMargin, errorTextPosition);

		ImVec2 btnretryMin, btnretryMax;
		GetMainButtonRect(panelMin, panelMax, scale, 50.0f, btnretryMin, btnretryMax);

		if (DrawTextButton(ctx, btnretryMin, btnretryMax, Localise("Installer_Button_Retry").c_str(), smallFontSize, true, "decide", true))
		{
			if (g_installerThread && g_installerThread->joinable())
				g_installerThread->join();
			g_installerThread.reset();

			g_currentPage = WizardPage::SelectGame;
			g_statusMessage.clear();
			g_installerFinished.store(false);
			g_installerFailed.store(false);
			g_installerCompiling.store(false);
			g_installerProgress = 0.0f;
		}

		DrawQuitButton(ctx, panelMin, panelMax, scale);

		break;
	}
	}


	DrawVersionString(g_fntSeurat, IM_COL32(255, 255, 255, (int)(70)));
	FinishCursorFrame(ctx);
}
// Dropping a file on the window works the same as Browse and a dropped folder
// is taken as the extracted game.
static void OnSourceDropped(X86Context* ctx, const char* file)
{
	if (g_currentPage != WizardPage::SelectGame || g_isQuitting || IsParsingSources())
		return;

	std::error_code ec;
	std::filesystem::path path(std::u8string_view(reinterpret_cast<const char8_t*>(file)));

	if (std::filesystem::is_directory(path, ec))
		path /= GameExecutableFile;

	std::u8string extensionStr = path.extension().u8string();
	std::string lowerExtension(extensionStr.begin(), extensionStr.end());
	std::transform(lowerExtension.begin(), lowerExtension.end(), lowerExtension.begin(), [](unsigned char c) { return std::tolower(c); });

	if ((lowerExtension != ".iso" && lowerExtension != ".xbe") || !std::filesystem::is_regular_file(path, ec))
	{
		Game_PlaySound(ctx, "cannot_decide");
		g_statusMessage = Localise("Installer_Message_InvalidFileSelected");
		return;
	}

	std::u8string pathString = path.u8string();
	g_sourcePath.assign(pathString.begin(), pathString.end());
	g_statusMessage.clear();
	g_sourceDropTime = ImGui::GetTime();
	Game_PlaySound(ctx, "decide");
}


static void OnCursorEvent(const SDL_Event& event)
{
	if (g_isClosing || !IsInputAllowed())
		return;

	constexpr float axisValueRange = 32767.0f;
	constexpr float axisTapRange = 0.5f;

	ImVec2 tapDirection = {};
	// re used from unleashed recomp
	switch (event.type)
	{
	case SDL_EVENT_KEY_DOWN:
	{
		switch (event.key.scancode)
		{
		case SDL_SCANCODE_LEFT:
		case SDL_SCANCODE_RIGHT:
			tapDirection.x = (event.key.scancode == SDL_SCANCODE_RIGHT) ? 1.0f : -1.0f;
			break;

		case SDL_SCANCODE_UP:
		case SDL_SCANCODE_DOWN:
			tapDirection.y = (event.key.scancode == SDL_SCANCODE_DOWN) ? 1.0f : -1.0f;
			break;

		case SDL_SCANCODE_RETURN:
		case SDL_SCANCODE_KP_ENTER:
			if (!event.key.repeat)
				g_currentCursorAccepted = g_currentCursorIndex >= 0;
			break;

		case SDL_SCANCODE_ESCAPE:
			if (!event.key.repeat)
				g_currentCursorBack = true;
			break;
		}

		break;
	}

	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	{
		switch (event.gbutton.button)
		{
		case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
			tapDirection = { -1.0f, 0.0f };
			break;

		case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
			tapDirection = { 1.0f, 0.0f };
			break;

		case SDL_GAMEPAD_BUTTON_DPAD_UP:
			tapDirection = { 0.0f, -1.0f };
			break;

		case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
			tapDirection = { 0.0f, 1.0f };
			break;

		case SDL_GAMEPAD_BUTTON_SOUTH:
			g_currentCursorAccepted = g_currentCursorIndex >= 0;
			break;

		case SDL_GAMEPAD_BUTTON_EAST:
			g_currentCursorBack = true;
			break;
		}

		break;
	}

	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
	{
		if (event.gaxis.axis < 2)
		{
			float newAxisValue = event.gaxis.value / axisValueRange;
			bool sameDirection = (newAxisValue * g_joypadAxis[event.gaxis.axis]) > 0.0f;
			bool wasInRange = abs(g_joypadAxis[event.gaxis.axis]) > axisTapRange;
			bool isInRange = abs(newAxisValue) > axisTapRange;

			if (sameDirection && !wasInRange && isInRange)
				tapDirection[event.gaxis.axis] = newAxisValue;

			g_joypadAxis[event.gaxis.axis] = newAxisValue;
		}

		break;
	}
	}

	if (tapDirection.x == 0.0f && tapDirection.y == 0.0f)
		return;

	if (g_currentCursorIndex < 0 || g_currentCursorIndex >= int(g_currentCursorRects.size()))
	{
		if (!g_currentCursorRects.empty())
			g_currentCursorIndex = g_currentCursorDefault;

		return;
	}

	auto& currentRect = g_currentCursorRects[g_currentCursorIndex];
	ImVec2 currentPoint = ImVec2
	(
		(currentRect.first.x + currentRect.second.x) / 2.0f + tapDirection.x * (currentRect.second.x - currentRect.first.x) / 2.0f,
		(currentRect.first.y + currentRect.second.y) / 2.0f + tapDirection.y * (currentRect.second.y - currentRect.first.y) / 2.0f
	);

	int newCursorIndex = g_currentCursorIndex;
	float closestDistance = FLT_MAX;
	for (int i = 0; i < int(g_currentCursorRects.size()); i++)
	{
		if (i == g_currentCursorIndex)
			continue;

		auto& targetRect = g_currentCursorRects[i];
		ImVec2 targetPoint = ImVec2
		(
			(targetRect.first.x + targetRect.second.x) / 2.0f + tapDirection.x * (targetRect.first.x - targetRect.second.x) / 2.0f,
			(targetRect.first.y + targetRect.second.y) / 2.0f + tapDirection.y * (targetRect.first.y - targetRect.second.y) / 2.0f
		);

		ImVec2 delta = ImVec2(targetPoint.x - currentPoint.x, targetPoint.y - currentPoint.y);
		float projectedDistance = delta.x * tapDirection.x + delta.y * tapDirection.y;
		float manhattanDistance = abs(delta.x) + abs(delta.y);

		if (projectedDistance > 0.0f && manhattanDistance < closestDistance)
		{
			newCursorIndex = i;
			closestDistance = manhattanDistance;
		}
	}

	g_currentCursorIndex = newCursorIndex;
}

bool InstallerWizard::Run(std::filesystem::path installPath)
{
	g_installPath = installPath / "game";
	g_isQuitting = false;
	g_isClosing = false;
	g_openProgress = 0.0f;
	g_bgFadeOutStart = -1.0;
	g_currentPage = WizardPage::SelectLanguage;
	g_cursorPage = g_currentPage;
	g_cursorPickDefault = true;
	g_currentCursorIndex = -1;
	g_lastCursorIndex = -1;
	g_currentCursorAccepted = false;
	g_currentCursorBack = false;
	g_joypadAxis = {};

#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Installer: finding original media.");
    g_sourcePath = Uwp::NextInstallSource({}, g_statusMessage);
    Uwp::LogStartup(g_sourcePath.empty() ? g_statusMessage : "Installer: original media detected.");
    Uwp::LogStartup("Installer: initializing input.");
#endif
	input::Init();

#ifndef SHADOW_RECOMP_UWP
	EmbeddedPlayer::Init();
	NFD_Init();
#else
    bool initializeAudioAfterFirstFrame = true;
    Uwp::LogStartup("Installer: opening the first screen.");
#endif

	GameWindow::SetFullscreenCursorVisibility(true);
	s_isVisible = true;

	FramePacer framePacer;
	framePacer.SetFpsLimit(60.0);

	while (s_isVisible)
	{
		if (!g_isClosing)
		{
			EmbeddedPlayer::PlayMusic();
		}

		SDL_PumpEvents();

		if (g_browseClicked)
		{
			g_browseClicked = false;

#ifdef SHADOW_RECOMP_UWP
            g_sourcePath = Uwp::NextInstallSource(g_sourcePath, g_statusMessage);
            g_cursorPickDefault = true;
#else
			nfdu8filteritem_t filters[] = { {"Xbox ISO or default.xbe", "iso,xbe"} };
			nfdu8char_t* outPath = nullptr;
			nfdresult_t result = NFD_OpenDialogU8(&outPath, filters, 1, nullptr);

			if (result == NFD_OKAY && outPath != nullptr)
			{
				g_sourcePath = outPath;
				g_statusMessage.clear();
				NFD_FreePathU8(outPath);
				g_cursorPickDefault = true;
			}
			else if (result == NFD_ERROR)
			{
				g_statusMessage = NFD_GetError();
			}
#endif
		}

		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				if (g_currentPage == WizardPage::Installing)
					continue;

				if (g_isQuitting)
				{
					s_isVisible = false;
				}
				else
				{
					Quit();
				}
			}
			else if (event.type == SDL_EVENT_DROP_FILE)
			{
				OnSourceDropped(nullptr, event.drop.data);
			}
			else
			{
				OnCursorEvent(event);
			}
		}

		GameWindow::Update();
		input::Update();
		Video::Present(nullptr); // host thread: no guest context

#ifdef SHADOW_RECOMP_UWP
        if (initializeAudioAfterFirstFrame)
        {
            initializeAudioAfterFirstFrame = false;
            Uwp::LogStartup("Installer: initializing audio after the first frame.");
            if (EmbeddedPlayer::Init())
                Uwp::LogStartup("Installer: audio ready.");
            else
                Uwp::LogStartup(fmt::format("Installer: audio unavailable. Continuing setup. {}", SDL_GetError()));
        }
#endif
		framePacer.EndFrame(FramePacer::WaitMode::SleepOnly);
	}

	SDL_CaptureMouse(false);

	SDL_PumpEvents();
	SDL_FlushEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST);

	GameWindow::SetFullscreenCursorVisibility(false);

	Config::Fullscreen = GameWindow::IsFullscreen();
	if (!GameWindow::IsFullscreen() && !GameWindow::IsMaximised())
	{
		Config::WindowX = GameWindow::s_x;
		Config::WindowY = GameWindow::s_y;
		Config::WindowWidth = GameWindow::s_width;
		Config::WindowHeight = GameWindow::s_height;
	}
	Config::Save();

#ifndef SHADOW_RECOMP_UWP
	NFD_Quit();
#endif
	InstallerWizard::Shutdown();
	EmbeddedPlayer::Shutdown();

	return !g_isQuitting;
}

void InstallerWizard::Init()
{
	g_fntSeurat = ImFontAtlasSnapshot::GetFont("FOT-SeuratPro-DB.otf");
	g_upTexInstallBG = LOAD_ZSTD_TEXTURE(g_installer_bg);
	g_upTexOptionsBG2 = LOAD_ZSTD_TEXTURE(g_options_bg2);
	g_upTexFoxLogo = LOAD_ZSTD_TEXTURE(g_foxLogo);

	InitCreditsString();
}
