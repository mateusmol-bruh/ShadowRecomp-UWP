#include "paths.h"
#include <os/process.h>

#ifdef SHADOW_RECOMP_UWP
#include <kernel/io/file_system.h>
#include <os/uwp/platform.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.h>

std::filesystem::path g_executableRoot;
static std::filesystem::path extractedGamePath;

void Uwp::SetExtractedGamePath(std::filesystem::path path)
{
    extractedGamePath = std::move(path);
}

std::filesystem::path GetMainGameFolder()
{
    return extractedGamePath.empty() ? GetGamePath() / "game" : extractedGamePath;
}

const std::filesystem::path& Uwp::GetLocalStatePath()
{
    static const std::filesystem::path localState =
        winrt::Windows::Storage::ApplicationData::Current().LocalFolder().Path().c_str();
    return localState;
}

std::filesystem::path GetGamePath()
{
    return GetUserPath();
}
#else
std::filesystem::path g_executableRoot = os::process::GetExecutablePath().remove_filename();
std::filesystem::path g_userPath = BuildUserPath();
#endif

bool CheckPortable()
{
#ifdef SHADOW_RECOMP_UWP
    return false;
#else
    return std::filesystem::exists(g_executableRoot / "portable.txt");
#endif
}

std::filesystem::path BuildUserPath()
{
#ifdef SHADOW_RECOMP_UWP
    return Uwp::ResolveDataPath(Uwp::GetLocalStatePath());
#else
    if (CheckPortable())
        return g_executableRoot;

    std::filesystem::path userPath;

#if defined(_WIN32)
    PWSTR knownPath = NULL;
    if (SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, &knownPath) == S_OK)
        userPath = std::filesystem::path{ knownPath } / USER_DIRECTORY;

    CoTaskMemFree(knownPath);
#elif defined(__linux__)
    const char* homeDir = getenv("HOME");
    if (homeDir == nullptr)
    {
        homeDir = getpwuid(getuid())->pw_dir;
    }

    if (homeDir != nullptr)
    {
        // Prefer to store in the .config directory if it exists. Use the home directory otherwise.
        std::filesystem::path homePath = homeDir;
        std::filesystem::path configPath = homePath / ".config";
        if (std::filesystem::exists(configPath))
            userPath = configPath / USER_DIRECTORY;
        else
            userPath = homePath / ("." USER_DIRECTORY);
    }
#else
    static_assert(false, "GetUserPath() not implemented for this platform.");
#endif

    return userPath;
#endif
}

const std::filesystem::path& GetUserPath()
{
#ifdef SHADOW_RECOMP_UWP
    static const auto userPath = BuildUserPath();
    return userPath;
#else
    return g_userPath;
#endif
}
