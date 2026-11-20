#include "stdafx.h"
#include <kernel/memory.h>
#include <kernel/heap.h>
#include <kernel/function.h>
#include <gpu/video.h>
#include <cpu/guest_thread.h>
#include <input/input.h>
#include <file.h>
#include <image.h>
#include <user/paths.h>
#include <kernel/io/file_system.h>
#include <audio/audio.h>
#include <ui/game_window.h>
#include <os/logger.h>
#include <os/process.h>
#include <os/registry.h>

#include <xbe.h>
#include <iostream>
#include <filesystem>
#include <mod/mod_loader.h>
#include <preload_executable.h>
#include <patches/unix_patches.h>
#include <patches/reloaded_mod_patches.h>
#include <patches/code_patches.h>
#include <patches/multiplayer_mod_patches.h>
#include <audio/embedded_player.h>
#include <ui/installer_wizard.h>
#include <install/installer.h>
#include <install/update_checker.h>
#include <gpu/frame_pacing/timer_resolution.h>
#include <patches/pause_patches.h>
#include <patches/bloom_patches.h>
#ifndef SHADOW_RECOMP_UWP
#include <misc/rich_presence/shadow_rpc.h>
#endif
#ifdef SHADOW_RECOMP_UWP
#include <os/uwp/platform.h>
#include <os/uwp/xbe_kernel_data.h>

uint32_t decode_kernel_thunk_addr(uint32_t encodedAddress, uint32_t imageSize);
#endif


#if defined(_WIN32) && defined(SHADOW_RECOMP_D3D12) && !defined(SHADOW_RECOMP_UWP)
static std::array<std::string_view, 3> g_D3D12RequiredModules =
{
    "D3D12/D3D12Core.dll",
    "dxcompiler.dll",
    "dxil.dll"
};
#endif

Memory g_memory;
Heap g_userHeap;

#ifdef SHADOW_RECOMP_UWP
static bool InitializeXbeKernelData(const XbeHeader& header, std::span<const XbeSectionHeader> sections)
{
    const uint32_t table = decode_kernel_thunk_addr(header.m_kernel_image_thunk_addr, header.sizeOfImage);
    if (table < header.baseAddress || table % sizeof(uint32_t) != 0)
        return false;

    for (const auto& section : sections)
    {
        const uint64_t end = uint64_t(section.virtualAddress) + std::min(section.virtualSize, section.rawSize);
        if (table < section.virtualAddress || uint64_t(table) + sizeof(uint32_t) > end)
            continue;

        auto* entries = static_cast<uint32_t*>(g_memory.Translate(table));
        const std::span words(entries, (end - table) / sizeof(uint32_t));
        const auto terminator = std::ranges::find(words, 0u);
        if (terminator == words.end())
            return false;

        auto* data = static_cast<Uwp::XbeKernelData*>(g_userHeap.Alloc(sizeof(Uwp::XbeKernelData)));
        if (!data)
            return false;
        std::construct_at(data);
        const size_t count = Uwp::BindXbeKernelData(
            words.first(static_cast<size_t>(terminator - words.begin())), g_memory.MapVirtual(data), *data);
        Uwp::LogStartup(fmt::format("Kernel: bound {} XBE data imports.", count));
        return true;
    }
    return false;
}
#endif

// return a modifiable pointer inside this structure that corresponds to a virtual address
uint8_t* GetAddr(uint32_t xbeVA, const XbeHeader& header, const XbeSectionHeader* sectionHeaders)
{
    for (uint32_t i = 0; i < header.numSections; i++)
    {
        uint32_t virtStart = sectionHeaders[i].virtualAddress;
        uint32_t virtSize = sectionHeaders[i].virtualSize;

        if (xbeVA >= virtStart && xbeVA < (virtStart + virtSize))
        {
            uint32_t offsetInSection = xbeVA - virtStart;
            uint8_t* sectionBaseHost = reinterpret_cast<uint8_t*>(g_memory.Translate(virtStart));
            return sectionBaseHost + offsetInSection;
        }
    }

    return nullptr; // Not found
}

TLS* m_TLS;

uint32_t LoadXbeModule(const std::filesystem::path& path)
{
    auto module = LoadFile(path);

    if (module.empty())
    {
        LOGN_ERROR("Failed to load XBE file, please make sure game files are present.");
        assert("Failed to load XBE file, please make sure game files are present." && false);
        return 0;
    }


    const auto* header = reinterpret_cast<const XbeHeader*>(module.data());
    const uint32_t sectionHeadersOffset = header->sectionHeadersAddress - header->baseAddress;
    const auto* sectionHeaders = reinterpret_cast<const XbeSectionHeader*>(module.data() + sectionHeadersOffset);

    uint32_t firstSectionVA = sectionHeaders[0].virtualAddress;
    uint32_t headersSize = firstSectionVA - header->baseAddress;

    //Copy headers to their expected VA 
    void* headersDest = g_memory.Translate(header->baseAddress);
    memcpy(headersDest, module.data(), headersSize);

    for (uint32_t i = 0; i < header->numSections; i++)
    {
        auto srcData = module.data() + sectionHeaders[i].rawAddress;
        auto destData = g_memory.Translate(sectionHeaders[i].virtualAddress);
        memcpy(destData, srcData, sectionHeaders[i].rawSize);
    }

#ifdef SHADOW_RECOMP_UWP
    if (!InitializeXbeKernelData(*header, { sectionHeaders, header->numSections }))
        throw std::runtime_error("Could not bind kernel data imports from the selected XBE.");
#endif

    void* addr = GetAddr(header->tlsAddress, *header, sectionHeaders);
    if (addr)
    {
        m_TLS = new TLS;
        memcpy(m_TLS, addr, sizeof(*m_TLS));
    }

    return decode_entry_point(header->m_entry, header->sizeOfImage);
}

void HostStartup()
{
#ifdef _WIN32
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif

#ifndef SHADOW_RECOMP_UWP
    input::Init();
#endif

#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Memory: initializing guest address space and heaps.");
    g_memory.InitForGame();
#endif
    if (g_memory.base == nullptr)
    {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("System_MemoryAllocationFailed").c_str(), GameWindow::s_pWindow);
        std::_Exit(1);
    }


    g_userHeap.Init();
#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup(fmt::format("Memory: guest heaps ready at host base {}.", fmt::ptr(g_memory.base)));
#endif
}

extern thread_local bool isHostThread;


bool QuickBootConfig = false;
int Ts_StageID = -1; // test spawn stage select
std::vector<int> Ts_EventIDs; // events/cutscenes to play directly on boot
bool Ts_EventAll = false; // will play all events

static constexpr size_t EventListMaxSize = 256;

static void ParseEventList(const char* arg)
{
    std::string list = arg;

    for (char* token = strtok(list.data(), ","); token; token = strtok(nullptr, ","))
    {
        int first, last;
        int read = sscanf(token, "%d-%d", &first, &last);

        if (read < 1)
            break;

        if (read == 1)
            last = first;

        if (last < first)
            std::swap(first, last);

        for (int id = first; id <= last && Ts_EventIDs.size() < EventListMaxSize; id++)
            Ts_EventIDs.push_back(id);
    }
}

#ifdef SHADOW_RECOMP_UWP
int ShadowRecompMain(int argc, char* argv[])
#else
int main(int argc, char* argv[])
#endif
{
#ifdef SHADOW_RECOMP_UWP
    Uwp::InitializeStorage();
    QuickBootConfig = Uwp::ConsumeQuickBoot();
#elif defined(_WIN32)
    //SetProcessAffinityMask(GetCurrentProcess(), 1);
    TimerResolution::SetHighestResolution();

    // Higher priority == faster wake-up during frame pacing.
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
#elif defined(__linux__)
    //cpu_set_t cpuset;
    //CPU_ZERO(&cpuset);
    //CPU_SET(0, &cpuset);
    //sched_setaffinity(0, sizeof(cpuset), &cpuset);
#endif

    os::process::CheckConsole();

    if (!os::registry::Init())
        LOGN_WARNING("OS does not support registry");


    os::logger::Init();

    PreloadContext preloadContext;
    preloadContext.PreloadExecutable();

    LOGN("Starting Shadow Recomp");

    std::filesystem::path xbePath;
    xbePath = GetGameExecutablePath();

    bool forceInstaller = false;
    bool useDefaultWorkingDirectory = false;
    bool forceInstallationCheck = false;
    bool graphicsApiRetry = false;
    const char* sdlVideoDriver = nullptr;

    for (uint32_t i = 1; i < argc; i++)
    {
        forceInstaller = forceInstaller || (strcmp(argv[i], "--install") == 0);
        useDefaultWorkingDirectory = useDefaultWorkingDirectory || (strcmp(argv[i], "--use-cwd") == 0);
        forceInstallationCheck = forceInstallationCheck || (strcmp(argv[i], "--install-check") == 0);
        graphicsApiRetry = graphicsApiRetry || (strcmp(argv[i], "--graphics-api-retry") == 0);
        QuickBootConfig = QuickBootConfig || (strcmp(argv[i], "--quickboot") == 0);
        Video::s_dumpPipelineCacheHeader = Video::s_dumpPipelineCacheHeader || (strcmp(argv[i], "--dump-pipeline-cache") == 0);

        if (strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0)
        {
            if ((i + 1) < argc)
                Ts_StageID = atoi(argv[++i]);
            else
                LOGN_WARNING("No argument was specified for --level. Option will be ignored.");
        }

        if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--event") == 0)
        {
            if ((i + 1) < argc)
            {
                const char* arg = argv[++i];

                if (_stricmp(arg, "all") == 0)
                {
                    Ts_EventAll = true;
                }
                else
                {
                    ParseEventList(arg);

                    if (Ts_EventIDs.empty())
                        LOGN_WARNING("No valid event id was specified for --event. Option will be ignored.");
                }
            }
            else
            {
                LOGN_WARNING("No argument was specified for --event. Option will be ignored.");
            }
        }

        if (strcmp(argv[i], "--sdl-video-driver") == 0)
        {
            if ((i + 1) < argc)
                sdlVideoDriver = argv[++i];
            else
                LOGN_WARNING("No argument was specified for --sdl-video-driver. Option will be ignored.");
        }
    }

    if (Ts_StageID != -1 && (Ts_EventAll || !Ts_EventIDs.empty()))
    {
        Ts_EventAll = false;
        Ts_EventIDs.clear();
    }

#ifndef SHADOW_RECOMP_UWP
    if (!useDefaultWorkingDirectory)
    {
        // Set the current working directory to the executable's path.
        std::error_code ec;
        std::filesystem::current_path(os::process::GetExecutableRoot(), ec);
    }

#else
    const auto installRequest = Uwp::GetLocalStatePath() / "install.txt";
    forceInstaller = forceInstaller || std::filesystem::is_regular_file(installRequest);
#endif

#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Loading configuration.");
#endif
    Config::Load();

#ifdef SHADOW_RECOMP_UWP
    if (!forceInstaller && !std::filesystem::is_regular_file(GetGameExecutablePath()))
    {
        Uwp::LogStartup("Media: looking for an extracted disc to launch directly.");
        std::string status;
        const auto sources = Uwp::GetInstallSources(true, status);
        for (const auto& executable : sources)
        {
            const auto root = executable.parent_path();
            if (Installer::CheckExtractedGame(root, status))
            {
                Uwp::SetExtractedGamePath(root);
                Uwp::LogStartup("Media: verified extracted disc. Launching in place.");
                break;
            }
            Uwp::LogStartup("Media: extracted disc rejected. " + status);
        }
        if (sources.empty())
            Uwp::LogStartup("Media: " + status);
    }
#endif

    bool isGameInstalled = std::filesystem::exists(GetGameExecutablePath());
    bool runInstallerWizard = forceInstaller || !isGameInstalled;

    if (forceInstallationCheck)
    {
        os::process::ShowConsole();
        int lastDecile = -1;
        std::string errorMessage;
        bool isInstallValid = Installer::CheckInstall(GetMainGameFolder(), [&](const Installer::Progress& progress)
        {
            int decile = int(progress.ratio * 10.0f);
            if (decile != lastDecile)
            {
                LOGFN("Checking files: {}%", decile * 10);
                lastDecile = decile;
            }
        }, errorMessage);

        char resultText[512];
        snprintf(resultText, sizeof(resultText), Localise(isInstallValid ? "IntegrityCheck_Success" : "IntegrityCheck_Failed").c_str(), errorMessage.c_str());
        fprintf(isInstallValid ? stdout : stderr, "%s\n", resultText);
        SDL_ShowSimpleMessageBox(isInstallValid ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), resultText, GameWindow::s_pWindow);
        return isInstallValid ? 0 : 1;
    }

    if (runInstallerWizard)
    {
#ifdef SHADOW_RECOMP_UWP
        Uwp::LogStartup("Installed game unavailable or setup explicitly requested.");
#endif
        if (!Video::CreateHostDevice(sdlVideoDriver, graphicsApiRetry))
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("Video_BackendError").c_str(), GameWindow::s_pWindow);
            std::_Exit(1);
        }

#ifndef SHADOW_RECOMP_UWP
        g_userHeap.Init();
#else
        Uwp::LogStartup("Installer: opening setup wizard.");
#endif

        if (!InstallerWizard::Run(GetGamePath()))
        {
#ifdef SHADOW_RECOMP_UWP
            Uwp::LogStartup("Installer: wizard closed before installation completed.");
#endif
            std::_Exit(0);
        }
#ifdef SHADOW_RECOMP_UWP
        Uwp::LogStartup("Installer: installation completed.");
        std::error_code ec;
        std::filesystem::remove(installRequest, ec);
#endif
    }

#if defined(_WIN32) && defined(SHADOW_RECOMP_D3D12) && !defined(SHADOW_RECOMP_UWP)
    for (auto& dll : g_D3D12RequiredModules)
    {
        if (!std::filesystem::exists(g_executableRoot / dll))
        {
            char text[512];
            snprintf(text, sizeof(text), "missing graphics DLL", dll.data());
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), text, GameWindow::s_pWindow);
            std::_Exit(1);
        }
    }
#endif

    constexpr double TimeBetweenUpdateChecksInSeconds = 24 * 60 * 60;
    time_t timeNow = std::time(nullptr);
    if (Config::CheckForUpdates && difftime(timeNow, Config::LastChecked) > TimeBetweenUpdateChecksInSeconds)
    {
        UpdateChecker::initialize();
        UpdateChecker::start();
        Config::LastChecked = timeNow;
        Config::Save();
    }

    if (Config::ShowConsole)
        os::process::ShowConsole();

#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Starting host subsystems.");
#endif
    HostStartup();

#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Loading mods.");
#endif
    ModLoader::Init();

    //init mutexes for multi-core patch
    InitMultiCoreMutexesPatches();


    isGameInstalled = std::filesystem::exists(GetGameExecutablePath());
    if (!isGameInstalled)
    {
#ifdef SHADOW_RECOMP_UWP
        Uwp::LogStartup("The selected game/default.xbe is missing.");
#endif
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), "Game files not found. Please reinstall.", GameWindow::s_pWindow);
        std::_Exit(1);
    }

#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Loading default.xbe from the selected game folder.");
#endif
    xbePath = GetGameExecutablePath();
    uint32_t entry = LoadXbeModule(xbePath);
#ifdef SHADOW_RECOMP_UWP
    if (entry < X86_CODE_BASE || entry - X86_CODE_BASE >= X86_CODE_SIZE || !g_memory.FindFunction(entry))
        throw std::runtime_error("The XBE entry point does not match this recompilation. Use supported original media.");
    Uwp::LogStartup("Game executable loaded.");
#endif

    if (!runInstallerWizard)
    {
        if (!Video::CreateHostDevice(sdlVideoDriver, graphicsApiRetry))
        {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GameWindow::GetTitle(), Localise("Video_BackendError").c_str(), GameWindow::s_pWindow);
            std::_Exit(1);
        }
    }

#ifdef SHADOW_RECOMP_UWP
    input::Init();
#endif


#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Preparing shader and pipeline caches.");
#endif
    Video::PrecompileShadersAndPipelines();

    PatchUnixStrings();
    ReloadedValuePatches();
    CodeValuePatches();
    MultiplayerPatchesInit();
    PausePatchesInit();
    BloomConfigValueChangedCallback(&Config::BloomIntensity);
#ifndef SHADOW_RECOMP_UWP
    RichPresence::ShadowRpc::Start();
#endif

    //launch the Start function of the game
    isHostThread = true;
#ifdef SHADOW_RECOMP_UWP
    Uwp::LogStartup("Starting recompiled game thread.");
#endif
    GuestThreadParams params = { entry, 0, 0, 0, 0 };
    GuestThread::Start(params);


    return 0;
}
