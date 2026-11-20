#include "platform.h"

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <windows.h>
#include <fileapifromapp.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.ApplicationModel.Core.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.ViewManagement.h>
#include <winrt/Windows.UI.Core.h>

#include <os/logger.h>
#include <os/media.h>
#include <os/process.h>
#include <os/user.h>
#include <user/paths.h>

int ShadowRecompMain(int argc, char* argv[]);

static std::atomic<uint64_t> renderSize;
static winrt::agile_ref<winrt::Windows::UI::Core::CoreDispatcher> dispatcher;
static std::filesystem::path startupLogPath;

static void LogNativeAppFrames(HANDLE file, const CONTEXT& fault) noexcept
{
    const auto appBase = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    ULONG_PTR stackBottom = 0;
    ULONG_PTR stackTop = 0;
    GetCurrentThreadStackLimits(&stackBottom, &stackTop);
    CONTEXT frame = fault;

    __try
    {
        for (int depth = 0; depth < 20 && frame.Rip
            && frame.Rsp >= stackBottom && frame.Rsp <= stackTop - sizeof(uint64_t); ++depth)
        {
            MEMORY_BASIC_INFORMATION region{};
            if (VirtualQuery(reinterpret_cast<void*>(frame.Rip), &region, sizeof(region))
                && reinterpret_cast<uintptr_t>(region.AllocationBase) == appBase)
            {
                char line[96];
                const int size = std::snprintf(line, sizeof(line), "Native stack frame %d: ShadowRecomp.exe + 0x%llX.\r\n",
                    depth, static_cast<unsigned long long>(frame.Rip - appBase));
                DWORD written;
                if (size > 0 && static_cast<size_t>(size) < sizeof(line))
                    WriteFile(file, line, static_cast<DWORD>(size), &written, nullptr);
            }

            const auto previousStack = frame.Rsp;
            DWORD64 imageBase = 0;
            if (const auto function = RtlLookupFunctionEntry(frame.Rip, &imageBase, nullptr))
            {
                void* handlerData = nullptr;
                DWORD64 establisherFrame = 0;
                RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, frame.Rip, function, &frame, &handlerData,
                    &establisherFrame, nullptr);
            }
            else
            {
                frame.Rip = *reinterpret_cast<const uint64_t*>(frame.Rsp);
                frame.Rsp += sizeof(uint64_t);
            }
            if (frame.Rsp <= previousStack)
                break;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        // Keep the original crash report if the stack cannot be unwound.
    }
}

static LONG WINAPI LogUnhandledNativeException(EXCEPTION_POINTERS* exception) noexcept
{
    const auto* record = exception->ExceptionRecord;
    MEMORY_BASIC_INFORMATION region{};
    VirtualQuery(record->ExceptionAddress, &region, sizeof(region));
    const auto offset = reinterpret_cast<uintptr_t>(record->ExceptionAddress)
        - reinterpret_cast<uintptr_t>(region.AllocationBase);
    const bool accessViolation = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
        && record->NumberParameters >= 2;
    char line[256];
    const int size = std::snprintf(line, sizeof(line),
        "Unhandled native exception 0x%08lX at %p (thread %lu, allocation %p + 0x%llX, %s %p).\r\n",
        record->ExceptionCode, record->ExceptionAddress, GetCurrentThreadId(), region.AllocationBase,
        static_cast<unsigned long long>(offset),
        accessViolation ? (record->ExceptionInformation[0] == 1 ? "write" :
            record->ExceptionInformation[0] == 8 ? "execute" : "read") : "access address unavailable",
        accessViolation ? reinterpret_cast<void*>(record->ExceptionInformation[1]) : nullptr);
    if (size > 0 && static_cast<size_t>(size) < sizeof(line))
    {
        const HANDLE file = CreateFileFromAppW(startupLogPath.c_str(), FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file != INVALID_HANDLE_VALUE)
        {
            DWORD written;
            WriteFile(file, line, static_cast<DWORD>(size), &written, nullptr);
            if (accessViolation && exception->ContextRecord)
                LogNativeAppFrames(file, *exception->ContextRecord);
            CloseHandle(file);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void Uwp::LogStartup(std::string_view message) noexcept
{
    try
    {
        static std::mutex mutex;
        const std::lock_guard lock(mutex);
        std::ofstream log(GetLocalStatePath() / "startup.log", std::ios::app);
        log << message << '\n';
    }
    catch (...)
    {
        // Diagnostics must not prevent the app from starting.
    }
}

static int RunShadowRecomp(int argc, char* argv[])
{
    Uwp::LogStartup("WinRT application callback entered.");
    try
    {
        dispatcher = winrt::Windows::UI::Core::CoreWindow::GetForCurrentThread().Dispatcher();
        Uwp::LogStartup("CoreWindow dispatcher acquired.");
        g_executableRoot = os::process::GetExecutableRoot();
        const int result = ShadowRecompMain(argc, argv);
        Uwp::LogStartup("Game main returned " + std::to_string(result) + ".");
        return result;
    }
    catch (const winrt::hresult_error& error)
    {
        const auto message = winrt::to_string(error.message());
        char code[32];
        std::snprintf(code, sizeof(code), "WinRT error 0x%08lX: ", static_cast<unsigned long>(error.code().value));
        Uwp::LogStartup(code + message);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shadow Recompiled", message.c_str(), nullptr);
    }
    catch (const std::exception& error)
    {
        Uwp::LogStartup(error.what());
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", error.what());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Shadow Recompiled", error.what(), nullptr);
    }
    return 1;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    OutputDebugStringA("Shadow Recompiled entered wWinMain.\n");
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try
        {
            startupLogPath = Uwp::GetLocalStatePath() / "startup.log";
            std::ofstream log(startupLogPath, std::ios::trunc);
            log << "Executable entered wWinMain. Starting WinRT SDL.\n";
            SetUnhandledExceptionFilter(LogUnhandledNativeException);
        }
        catch (...)
        {
            OutputDebugStringA("Shadow Recompiled could not initialize startup.log.\n");
        }
        winrt::uninit_apartment();
    }
    catch (...)
    {
        OutputDebugStringA("Shadow Recompiled could not initialize WinRT logging.\n");
    }

    // The WinRT SDL backend supplies CoreWindow, activation, and the event pump.
    try
    {
        const int result = SDL_RunApp(0, nullptr, RunShadowRecomp, nullptr);
        Uwp::LogStartup("SDL_RunApp returned " + std::to_string(result) + ".");
        return result;
    }
    catch (...)
    {
        Uwp::LogStartup("Unhandled exception during WinRT SDL startup or shutdown.");
        return 1;
    }
}

void Uwp::UpdateRenderSize(SDL_Window* window)
{
    int width = 0;
    int height = 0;
    if (SDL_GetWindowSizeInPixels(window, &width, &height) && width > 0 && height > 0)
        renderSize.store((uint64_t(width) << 32) | uint32_t(height), std::memory_order_relaxed);
}

void Uwp::GetRenderSize(uint32_t& width, uint32_t& height)
{
    const uint64_t size = renderSize.load(std::memory_order_relaxed);
    width = static_cast<uint32_t>(size >> 32);
    height = static_cast<uint32_t>(size);
}

void os::logger::Init() {}

void os::logger::Log(std::string_view text, ELogType type, const char* function)
{
    const auto priority = type == ELogType::Error ? SDL_LOG_PRIORITY_ERROR
        : type == ELogType::Warning ? SDL_LOG_PRIORITY_WARN : SDL_LOG_PRIORITY_INFO;
    SDL_LogMessage(SDL_LOG_CATEGORY_APPLICATION, priority, "%s%s%.*s",
        function ? function : "", function ? ": " : "", static_cast<int>(text.size()), text.data());
}

void os::logger::LogFile(std::string_view text, std::filesystem::path path)
{
#ifndef NDEBUG
    Log(std::string(text) + " '" + path.string() + "'", ELogType::Utility, nullptr);
#endif
}

std::filesystem::path os::process::GetExecutablePath()
{
    return GetExecutableRoot() / "ShadowRecomp.exe";
}

std::filesystem::path os::process::GetExecutableRoot()
{
    return winrt::Windows::ApplicationModel::Package::Current().InstalledLocation().Path().c_str();
}

std::filesystem::path os::process::GetWorkingDirectory() { return GetUserPath(); }
bool os::process::SetWorkingDirectory(const std::filesystem::path&) { return false; }
void os::process::CheckConsole() {}
void os::process::ShowConsole() {}

bool Uwp::ConsumeQuickBoot()
{
    std::error_code ec;
    return std::filesystem::remove(Uwp::GetLocalStatePath() / "quickboot.txt", ec);
}

static winrt::fire_and_forget RequestRestart(bool quickBoot)
{
    using winrt::Windows::ApplicationModel::Core::CoreApplication;
    try
    {
        co_await winrt::resume_foreground(dispatcher.get());
        if (quickBoot)
        {
            std::ofstream marker(Uwp::GetLocalStatePath() / "quickboot.txt");
            if (!marker)
                throw std::runtime_error("Could not save restart options.");
        }
        const auto result = co_await CoreApplication::RequestRestartAsync(L"");
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Restart failed (%d).", static_cast<int>(result));
    }
    catch (const winrt::hresult_error& error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Restart failed: %s",
            winrt::to_string(error.message()).c_str());
    }
    catch (const std::exception& error)
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Restart failed: %s", error.what());
    }

    if (quickBoot)
        Uwp::ConsumeQuickBoot();
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Shadow Recompiled",
        "Close and reopen the app to apply the new settings.", nullptr);
}

bool os::process::StartProcess(const std::filesystem::path& path, const std::vector<std::string>& args,
    std::filesystem::path)
{
    const bool quickBoot = args.size() == 1 && args.front() == "--quickboot";
    if (path != GetExecutablePath() || (!args.empty() && !quickBoot))
        return false;
    RequestRestart(quickBoot);
    return true;
}

bool os::media::IsSupported() { return false; }
bool os::media::IsExternalMediaPlaying() { return false; }

bool os::user::IsDarkTheme()
{
    const auto color = winrt::Windows::UI::ViewManagement::UISettings().GetColorValue(
        winrt::Windows::UI::ViewManagement::UIColorType::Background);
    return color.R + color.G + color.B < 384;
}
