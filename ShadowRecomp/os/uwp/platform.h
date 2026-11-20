#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace Uwp
{
    const std::filesystem::path& GetLocalStatePath();
    std::filesystem::path ResolveDataPath(const std::filesystem::path& localState);
    void InitializeStorage();
    std::vector<std::filesystem::path> GetInstallSources(bool extractedOnly, std::string& status);
    void SetExtractedGamePath(std::filesystem::path path);
    void UpdateGamepads();
    void LogStartup(std::string_view message) noexcept;
    bool ConsumeQuickBoot();
    std::string NextInstallSource(std::string_view current, std::string& status);
    void UpdateRenderSize(SDL_Window* window);
    void GetRenderSize(uint32_t& width, uint32_t& height);
}
