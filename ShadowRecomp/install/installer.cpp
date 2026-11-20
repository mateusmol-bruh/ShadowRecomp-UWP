#include "installer.h"

#include <fstream>
#include <unordered_set>
#include <xxhash.h>
#include <kernel/io/file_system.h>

#include "directory_file_system.h"
#include "xiso_file_system.h"
#include "hashes/game.h"

namespace Installer
{
    struct FileEntry
    {
        const char* path;
        std::span<const uint64_t> hashes;
        const VirtualFileSystem::File* source;
    };

    struct Journal
    {
        std::filesystem::path outputDir;
        std::vector<std::filesystem::path> files;
        std::unordered_set<std::string> directories;
    };

    static std::filesystem::path ToPath(std::string_view path)
    {
        return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t*>(path.data()), path.size()));
    }

    // look ups only
    static bool MatchSourceFiles(const VirtualFileSystem& source, std::vector<FileEntry>& fileEntries, std::string& errorMessage)
    {
        const uint64_t* hashes = GameHashes;
        for (size_t i = 0; i < GameFilesSize; i++)
        {
            auto [path, hashCount] = GameFiles[i];
            const VirtualFileSystem::File* sourceFile = source.Find(path);
            if (sourceFile == nullptr)
            {
                errorMessage = fmt::format("File {} is missing.", path);
                return false;
            }
            fileEntries.push_back({ path, { hashes, hashCount }, sourceFile });
            hashes += hashCount;
        }
        std::stable_sort(fileEntries.begin(), fileEntries.end(), [](const FileEntry& a, const FileEntry& b) { return a.source->offset < b.source->offset; });
        std::stable_partition(fileEntries.begin(), fileEntries.end(), [](const FileEntry& fileEntry) { return fileEntry.path != GameExecutableFile; });
        return true;
    }

    static bool CreateParentDirectory(std::string_view path, Journal& journal)
    {
        size_t slash = path.rfind('/');
        if (slash == std::string_view::npos)
        {
            return true;
        }

        auto [directory, inserted] = journal.directories.emplace(path.substr(0, slash));
        if (!inserted)
        {
            return true;
        }

        std::error_code ec;
        std::filesystem::create_directories(journal.outputDir / ToPath(*directory), ec);
        return !ec;
    }

    // picking .xbe = extracted folder
    static std::filesystem::path GetSourceRoot(const std::filesystem::path& sourcePath)
    {
        std::u8string extension = sourcePath.extension().u8string();
        if (VirtualFileSystem::ToLower(std::string(extension.begin(), extension.end())) == ".xbe")
        {
            return sourcePath.parent_path();
        }
        return sourcePath;
    }

    static std::unique_ptr<VirtualFileSystem> CreateFileSystemFromPath(const std::filesystem::path& sourceRoot, std::string& errorMessage)
    {
        std::error_code ec;
        std::unique_ptr<VirtualFileSystem> source;
        if (std::filesystem::is_directory(sourceRoot, ec))
        {
            if (!std::filesystem::is_regular_file(sourceRoot / GameExecutableFile, ec))
            {
                errorMessage = fmt::format("File {} is missing.", GameExecutableFile);
                return nullptr;
            }
            source = DirectoryFileSystem::Create(sourceRoot);
        }
        else
        {
            source = XisoFileSystem::Create(sourceRoot);
        }

        if (source == nullptr)
        {
            errorMessage = "Unable to read the game source.";
        }
        return source;
    }

    static bool ComputeTotalSize(std::span<const FilePair> filePairs, const VirtualFileSystem& source, uint64_t& totalSize, std::string& errorMessage)
    {
        for (FilePair pair : filePairs)
        {
            const std::string filename(pair.first);
            if (!source.exists(filename))
            {
                errorMessage = fmt::format("File {} is missing.", filename);
                return false;
            }

            totalSize += source.getSize(filename);
        }

        return true;
    }

    bool ParseSources(const std::filesystem::path& sourcePath, Sources& sources, std::string& errorMessage)
    {
        sources = Sources();
        sources.game = CreateFileSystemFromPath(GetSourceRoot(sourcePath), errorMessage);
        if (sources.game == nullptr)
        {
            return false;
        }

        return ComputeTotalSize({ GameFiles, GameFilesSize }, *sources.game, sources.totalSize, errorMessage);
    }

    static bool CheckFiles(const VirtualFileSystem& source, std::span<const FileEntry> fileEntries, Journal* journal, const ProgressCallback& callback, std::string& errorMessage)
    {
        uint64_t totalSize = 0;
        for (const FileEntry& fileEntry : fileEntries)
        {
            totalSize += fileEntry.source->size;
        }

        std::vector<uint8_t> buffer(0x200000);
        std::unique_ptr<XXH3_state_t, decltype(&XXH3_freeState)> hashState(XXH3_createState(), XXH3_freeState);
        uint64_t checkedSize = 0;
        uint64_t reportedSize = 0;
        auto reportProgress = [&](const char* fileName)
        {
            reportedSize = checkedSize;
            callback({ float(double(checkedSize) / double(totalSize)), fileName });
        };

        for (const FileEntry& fileEntry : fileEntries)
        {
            std::ofstream outStream;
            if (journal != nullptr)
            {
                if (!CreateParentDirectory(fileEntry.path, *journal))
                {
                    errorMessage = fmt::format("Unable to create the folder for {}.", fileEntry.path);
                    return false;
                }

                std::filesystem::path targetPath = journal->outputDir / ToPath(fileEntry.path);
                outStream.open(targetPath, std::ios::binary);
                if (!outStream.is_open())
                {
                    errorMessage = fmt::format("Unable to create file {}.", fileEntry.path);
                    return false;
                }

                journal->files.push_back(targetPath);
            }
            reportProgress(fileEntry.path);
            XXH3_64bits_reset(hashState.get());
            bool readSucceeded = source.Read(*fileEntry.source, buffer, [&](const uint8_t* data, size_t size)
            {
                XXH3_64bits_update(hashState.get(), data, size);

                if (outStream.is_open() && !outStream.write(reinterpret_cast<const char*>(data), std::streamsize(size)))
                {
                    return false;
                }

                checkedSize += size;

                // every 16 MiB
                if (checkedSize - reportedSize >= 0x1000000)
                {
                    reportProgress(fileEntry.path);
                }
                return true;
            });

            if (outStream.is_open())
            {
                outStream.close();
                if (outStream.fail())
                {
                    errorMessage = fmt::format("Failed to write file {}.", fileEntry.path);
                    return false;
                }
            }

            if (!readSucceeded)
            {
                errorMessage = fmt::format("Failed to read file {}.", fileEntry.path);
                return false;
            }

            if (std::ranges::find(fileEntry.hashes, XXH3_64bits_digest(hashState.get())) == fileEntry.hashes.end())
            {
                errorMessage = fmt::format("File {} does not match any known version.", fileEntry.path);
                return false;
            }
        }
        callback({ 1.0f, "" });
        return true;
    }

    static void Rollback(const Journal& journal)
    {
        std::error_code ec;
        for (const auto& path : journal.files)
        {
            std::filesystem::remove(path, ec);
        }

        for (const auto& directory : journal.directories)
        {
            auto path = journal.outputDir / ToPath(directory);
            while (path != journal.outputDir && std::filesystem::remove(path, ec))
            {
                path = path.parent_path();
            }
        }
    }

    bool Install(const std::filesystem::path& sourcePath, const std::filesystem::path& outputDir, const ProgressCallback& callback, std::string& errorMessage)
    {
        std::error_code ec;
        std::filesystem::path sourceRoot = GetSourceRoot(sourcePath);
        if (std::filesystem::equivalent(sourceRoot, outputDir, ec))
        {
            errorMessage = "Very funny -_- pick a different folder.";
            return false;
        }

        std::unique_ptr<VirtualFileSystem> source = CreateFileSystemFromPath(sourceRoot, errorMessage);
        if (source == nullptr)
        {
            return false;
        }

        std::vector<FileEntry> fileEntries;
        if (!MatchSourceFiles(*source, fileEntries, errorMessage))
        {
            return false;
        }

        std::filesystem::create_directories(outputDir, ec);
        if (ec)
        {
            errorMessage = "Unable to create the install folder.";
            return false;
        }

        std::filesystem::remove(outputDir / GameExecutableFile, ec);
        Journal journal;
        journal.outputDir = outputDir;
        if (!CheckFiles(*source, fileEntries, &journal, callback, errorMessage))
        {
            Rollback(journal);
            return false;
        }
        return true;
    }

    bool CheckInstall(const std::filesystem::path& gameDir, const ProgressCallback& callback, std::string& errorMessage)
    {
        auto source = DirectoryFileSystem::Create(gameDir);
        if (source == nullptr)
        {
            errorMessage = "Unable to read the install folder.";
            return false;
        }
        std::vector<FileEntry> fileEntries;
        return MatchSourceFiles(*source, fileEntries, errorMessage) && CheckFiles(*source, fileEntries, nullptr, callback, errorMessage);
    }

#ifdef SHADOW_RECOMP_UWP
    bool CheckExtractedGame(const std::filesystem::path& gameDir, std::string& errorMessage)
    {
        auto source = DirectoryFileSystem::Create(gameDir);
        if (!source)
        {
            errorMessage = "Unable to read the extracted disc folder.";
            return false;
        }

        std::vector<FileEntry> files;
        if (!MatchSourceFiles(*source, files, errorMessage))
            return false;

        const auto executable = std::find_if(files.begin(), files.end(),
            [](const FileEntry& file) { return GameExecutableFile == file.path; });
        if (executable == files.end())
        {
            errorMessage = "The game manifest does not contain default.xbe.";
            return false;
        }

        return CheckFiles(*source, std::span(&*executable, 1), nullptr, [](const Progress&) {}, errorMessage);
    }
#endif
}
