#include "platform.h"

#define NOMINMAX
#include <windows.h>
#include <fileapifromapp.h>
#include <objbase.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <vector>

#include <user/paths.h>

namespace fs = std::filesystem;

static std::string ReadStorageSetting(const fs::path& path)
{
    const HANDLE file = CreateFileFromAppW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return {};

    std::array<char, 128> bytes{};
    DWORD count = 0;
    const BOOL read = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr);
    CloseHandle(file);
    if (!read || count == bytes.size())
        return {};

    std::string_view text(bytes.data(), count);
    if (text.starts_with("\xEF\xBB\xBF"))
        text.remove_prefix(3);

    constexpr std::string_view whitespace = " \t\r\n";
    const auto begin = text.find_first_not_of(whitespace);
    if (begin == std::string_view::npos)
        return {};

    return std::string(text.substr(begin, text.find_last_not_of(whitespace) - begin + 1));
}

static bool IsStorageId(std::string_view id)
{
    constexpr std::string_view characters =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-";
    return !id.empty() && id.size() <= 64 && id.find_first_not_of(characters) == std::string_view::npos;
}

static void CheckWritableStorage(const fs::path& root)
{
    GUID guid{};
    wchar_t suffix[39]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, suffix, ARRAYSIZE(suffix)))
        throw std::runtime_error("Could not create a USB write check.");

    const auto probe = root / (std::wstring(L".write-check-") + suffix);
    const HANDLE file = CreateFileFromAppW(probe.c_str(), GENERIC_WRITE | DELETE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        throw std::runtime_error("Cannot write to the selected USB data folder. Check the permissions of shadow05 and its save folder.");

    constexpr char value = '1';
    DWORD bytesWritten = 0;
    const bool writable = WriteFile(file, &value, 1, &bytesWritten, nullptr) && bytesWritten == 1;
    CloseHandle(file);
    if (!writable)
        throw std::runtime_error("Could not write to USB. Check the drive and available space.");
}

fs::path Uwp::ResolveDataPath(const fs::path& localState)
{
    const auto settings = localState / "storage.txt";
    const bool automatic = !fs::exists(settings);
    const auto choice = automatic ? std::string() : ReadStorageSetting(settings);
    if (choice == "local")
    {
        LogStartup("Storage: LocalState selected by storage.txt.");
        return localState;
    }

    if (automatic && fs::is_regular_file(localState / "game" / "default.xbe"))
    {
        LogStartup("Storage: keeping the existing LocalState installation.");
        return localState;
    }

    constexpr std::string_view prefix = "usb:";
    if (!automatic && (!choice.starts_with(prefix) || !IsStorageId(std::string_view(choice).substr(prefix.size()))))
        throw std::runtime_error("Invalid LocalState/storage.txt. Use local or usb:<storage-id>.");

    const auto id = automatic ? std::string_view() : std::string_view(choice).substr(prefix.size());
    LogStartup(automatic ? "Storage: looking for a prepared shadow05 folder." : "Storage: looking for " + choice);
    const DWORD driveMask = GetLogicalDrives();
    if (!driveMask)
        throw std::runtime_error("Windows could not enumerate storage volumes.");

    fs::path selected;
    std::wstring volume = L"A:\\";
    // USB HDDs and SSDs can report DRIVE_FIXED. Identify prepared folders by their marker.
    for (unsigned index = 0; index < 26; ++index)
    {
        if (!(driveMask & (1UL << index)))
            continue;

        volume[0] = static_cast<wchar_t>(L'A' + index);
        const auto type = GetDriveTypeW(volume.c_str());
        if (type != DRIVE_REMOVABLE && type != DRIVE_FIXED)
            continue;

        const auto candidate = fs::path(volume) / "shadow05";
        LogStartup(std::string("Storage: checking ") + static_cast<char>(volume[0]) + ":\\shadow05.");
        const auto candidateId = ReadStorageSetting(candidate / "storage-id.txt");
        if (!IsStorageId(candidateId) || (!automatic && candidateId != id))
            continue;

        if (!selected.empty())
        {
            if (automatic)
                throw std::runtime_error("Multiple shadow05 folders found. Choose a unique usb:<storage-id> in LocalState/storage.txt.");
            throw std::runtime_error("Two connected drives have the same ShadowRecomp USB storage ID. Disconnect one and reopen the app.");
        }

        selected = candidate;
    }

    if (selected.empty())
    {
        if (automatic)
        {
            LogStartup("Storage: no prepared USB folder found. Using LocalState.");
            return localState;
        }
        throw std::runtime_error("USB folder shadow05 not found. Reconnect the selected drive and check shadow05/storage-id.txt and read permissions.");
    }

    LogStartup("Storage: found " + selected.string() + ". Checking write permission.");
    CheckWritableStorage(selected);
    const auto saves = selected / "save";
    if (fs::is_directory(saves))
        CheckWritableStorage(saves);
    return selected;
}

void Uwp::InitializeStorage()
{
    const auto& root = GetUserPath();
    for (const char* directory : { "original", "save", "mods" })
        fs::create_directories(root / directory);
    LogStartup("Storage: data directories ready.");
}

std::vector<fs::path> Uwp::GetInstallSources(bool extractedOnly, std::string& status)
{
    const auto original = GetUserPath() / "original";
    std::vector<fs::path> sources;
    std::error_code ec;
    for (fs::directory_iterator it(original, ec), end; !ec && it != end; it.increment(ec))
    {
        auto path = it->path();
        std::error_code entryError;
        if (it->is_directory(entryError))
            path /= "default.xbe";

        if (!fs::is_regular_file(path, entryError))
            continue;

        const auto filenameText = path.filename().u8string();
        std::string filename(filenameText.begin(), filenameText.end());
        std::transform(filename.begin(), filename.end(), filename.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (filename == "default.xbe" || (!extractedOnly && filename.ends_with(".iso")))
            sources.emplace_back(std::move(path));
    }

    if (ec)
    {
        status = "Could not read the original folder on the selected storage.";
        return {};
    }

    if (sources.empty())
    {
        status = extractedOnly ? "No extracted disc found. Setup is available for ISO sources."
            : "Copy the Xbox ISO or extracted disc to the original folder on the selected storage.";
        return {};
    }

    status.clear();
    std::sort(sources.begin(), sources.end());
    return sources;
}

std::string Uwp::NextInstallSource(std::string_view current, std::string& status)
{
    const auto sources = GetInstallSources(false, status);
    if (sources.empty())
        return {};

    const auto currentPath = current.empty() ? fs::path{}
        : fs::path(std::u8string_view(reinterpret_cast<const char8_t*>(current.data()), current.size()));
    const auto selected = std::find(sources.begin(), sources.end(), currentPath);
    const auto& next = selected == sources.end() || std::next(selected) == sources.end()
        ? sources.front() : *std::next(selected);
    const auto text = next.u8string();
    return { text.begin(), text.end() };
}
