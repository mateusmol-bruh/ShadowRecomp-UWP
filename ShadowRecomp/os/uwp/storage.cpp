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

static fs::path MakeStorageTempPath(const fs::path& root, const wchar_t* prefix)
{
    GUID guid{};
    wchar_t suffix[39]{};
    if (FAILED(CoCreateGuid(&guid)) || !StringFromGUID2(guid, suffix, ARRAYSIZE(suffix)))
        throw std::runtime_error("Could not create a temporary storage filename.");

    return root / (std::wstring(prefix) + suffix);
}

static void UpdateStorageSetting(const fs::path& settings, std::string_view previous, const std::string& value)
{
    if (previous == value)
        return;

    const auto temporary = MakeStorageTempPath(settings.parent_path(), L".storage-");
    const auto line = value + '\n';
    const auto size = static_cast<DWORD>(line.size());
    const HANDLE file = CreateFileFromAppW(temporary.c_str(), GENERIC_WRITE, 0,
        nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        const auto error = GetLastError();
        Uwp::LogStartup("Storage: could not create a temporary file for LocalState/storage.txt (" + std::to_string(error) + ").");
        return;
    }

    DWORD bytesWritten = 0;
    const auto result = WriteFile(file, line.data(), size, &bytesWritten, nullptr);
    DWORD error = result ? ERROR_SUCCESS : GetLastError();
    CloseHandle(file);
    if (result && bytesWritten != size)
        error = ERROR_WRITE_FAULT;

    if (error == ERROR_SUCCESS && !ReplaceFileFromAppW(settings.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
    {
        error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND)
            error = MoveFileFromAppW(temporary.c_str(), settings.c_str()) ? ERROR_SUCCESS : GetLastError();
    }

    if (error == ERROR_SUCCESS)
        Uwp::LogStartup("Storage: updated LocalState/storage.txt to " + value + ".");
    else
    {
        DeleteFileFromAppW(temporary.c_str());
        Uwp::LogStartup("Storage: could not update LocalState/storage.txt (" + std::to_string(error) + ").");
    }
}

static void CheckWritableStorage(const fs::path& root)
{
    const auto probe = MakeStorageTempPath(root, L".write-check-");
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
    const auto choice = ReadStorageSetting(settings);
    constexpr std::string_view prefix = "usb:";
    LogStartup("Storage: looking for a prepared shadow05 folder.");
    const DWORD driveMask = GetLogicalDrives();
    if (!driveMask)
        throw std::runtime_error("Windows could not enumerate storage volumes.");

    struct PreparedStorage
    {
        fs::path root;
        std::string id;
    };
    std::vector<PreparedStorage> candidates;
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
        if (IsStorageId(candidateId))
            candidates.push_back({ candidate, candidateId });
    }

    if (candidates.empty())
    {
        UpdateStorageSetting(settings, choice, "local");
        LogStartup("Storage: no prepared USB folder found. Using LocalState.");
        return localState;
    }

    auto selected = candidates.begin();
    if (candidates.size() > 1)
    {
        // The previous selection can disambiguate drives with distinct marker IDs.
        const auto id = choice.starts_with(prefix) ? std::string_view(choice).substr(prefix.size()) : std::string_view{};
        selected = std::ranges::find(candidates, id, &PreparedStorage::id);
        if (selected == candidates.end())
            throw std::runtime_error("Multiple shadow05 folders found. Disconnect the extra drives or choose a unique usb:<storage-id> in LocalState/storage.txt.");
        if (std::ranges::find(std::next(selected), candidates.end(), id, &PreparedStorage::id) != candidates.end())
            throw std::runtime_error("Two connected drives have the same ShadowRecomp USB storage ID. Disconnect one and reopen the app.");
    }

    LogStartup("Storage: found " + selected->root.string() + ". Checking write permission.");
    CheckWritableStorage(selected->root);
    const auto saves = selected->root / "save";
    if (fs::is_directory(saves))
        CheckWritableStorage(saves);
    UpdateStorageSetting(settings, choice, std::string(prefix) + selected->id);
    return selected->root;
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
