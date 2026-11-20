#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include "virtual_file_system.h"

using FilePair = std::pair<const char*, uint32_t>;

namespace Installer
{
    struct Progress
    {
        float ratio = 0.0f;
        const char* currentFileName = "";
    };

    struct Sources
    {
        std::unique_ptr<VirtualFileSystem> game;
        uint64_t totalSize = 0;
    };

    using ProgressCallback = std::function<void(const Progress&)>;

    bool ParseSources(const std::filesystem::path& sourcePath, Sources& sources, std::string& errorMessage);
    bool Install(const std::filesystem::path& sourcePath, const std::filesystem::path& outputDir, const ProgressCallback& callback, std::string& errorMessage);
    bool CheckInstall(const std::filesystem::path& gameDir, const ProgressCallback& callback, std::string& errorMessage);
#ifdef SHADOW_RECOMP_UWP
    bool CheckExtractedGame(const std::filesystem::path& gameDir, std::string& errorMessage);
#endif
}
