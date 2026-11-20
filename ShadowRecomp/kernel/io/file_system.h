#pragma once

#include "user/paths.h"

static const std::string GameExecutableFile = "default.xbe";
#ifdef SHADOW_RECOMP_UWP
std::filesystem::path GetMainGameFolder();
#else
inline std::filesystem::path GetMainGameFolder() { return GetGamePath() / "game"; }
#endif
inline std::filesystem::path GetGameExecutablePath()
{
    return GetMainGameFolder() / GameExecutableFile;
}

struct FileSystem
{
    static std::filesystem::path ResolvePath(const std::string_view& path, bool checkForMods, bool logModFile = false);
};
