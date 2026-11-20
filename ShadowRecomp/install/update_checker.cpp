#include "update_checker.h"

#ifdef SHADOW_RECOMP_UWP
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>
#include <winrt/Windows.Web.Http.Headers.h>
#else
#include <curl/curl.h>
#endif
#include <nlohmann/json.hpp>

#include "version.h"

#if defined(WIN32) && !defined(SHADOW_RECOMP_UWP)
#include <shellapi.h>
#endif

#include <os/logger.h>

// UpdateChecker

using json = nlohmann::json;

static const char *CHECK_URL = "https://api.github.com/repos/FoxWorkshop/ShadowRecomp/releases/latest";
static const char *VISIT_URL = "https://github.com/FoxWorkshop/ShadowRecomp/releases/latest";
static const char *USER_AGENT = "ShadowRecomp-Agent";

static std::atomic<bool> g_updateCheckerInProgress = false;
static std::atomic<bool> g_updateCheckerFinished = false;
static UpdateChecker::Result g_updateCheckerResult = UpdateChecker::Result::NotStarted;

#ifndef SHADOW_RECOMP_UWP
static size_t updateCheckerWriteCallback(void *contents, size_t size, size_t nmemb, std::string *output)
{
    size_t totalSize = size * nmemb;
    output->append((char *)contents, totalSize);
    return totalSize;
}
#endif

static bool parseVersion(const std::string &versionStr, int &major, int &minor, int &revision)
{
    size_t start = 0;
    if (versionStr[0] == 'v')
    {
        start = 1;
    }

    size_t firstDot = versionStr.find('.', start);
    size_t secondDot = versionStr.find('.', firstDot + 1);

    if (firstDot == std::string::npos || secondDot == std::string::npos)
    {
        return false;
    }

    try
    {
        major = std::stoi(versionStr.substr(start, firstDot - start));
        minor = std::stoi(versionStr.substr(firstDot + 1, secondDot - firstDot - 1));
        revision = std::stoi(versionStr.substr(secondDot + 1));
    }
    catch (const std::exception &e)
    {
        LOGF_ERROR("Error while parsing version: {}.", e.what());
        return false;
    }

    return true;
}

static bool downloadLatestRelease(std::string& response)
{
#ifdef SHADOW_RECOMP_UWP
    try
    {
        struct Apartment
        {
            Apartment() { winrt::init_apartment(); }
            ~Apartment() { winrt::uninit_apartment(); }
        } apartment;

        using winrt::Windows::Foundation::Uri;
        winrt::Windows::Web::Http::HttpClient client;
        client.DefaultRequestHeaders().UserAgent().ParseAdd(winrt::to_hstring(USER_AGENT));
        response = winrt::to_string(client.GetStringAsync(Uri(winrt::to_hstring(CHECK_URL))).get());
        return true;
    }
    catch (const winrt::hresult_error& error)
    {
        LOGF_ERROR("Error while performing request: {}", winrt::to_string(error.message()));
        return false;
    }
#else
    CURL* curl = curl_easy_init();
    if (!curl)
        return false;

    curl_easy_setopt(curl, CURLOPT_URL, CHECK_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, updateCheckerWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, USER_AGENT);
    const auto result = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (result != CURLE_OK)
    {
        LOGF_ERROR("Error while performing request: {}", curl_easy_strerror(result));
        return false;
    }
    return true;
#endif
}

void updateCheckerThread()
{
    int major, minor, revision;
    std::string response;
    if (downloadLatestRelease(response))
    {
        try
        {
            json root = json::parse(response);
            auto tag_name_element = root.find("tag_name");
            if (tag_name_element != root.end() && tag_name_element->is_string())
            {
                if (parseVersion(*tag_name_element, major, minor, revision))
                {
                    if ((g_versionMajor < major) || (g_versionMajor == major  && g_versionMinor < minor) || (g_versionMajor == major && g_versionMinor == minor && g_versionRevision < revision))
                    {
                        g_updateCheckerResult = UpdateChecker::Result::UpdateAvailable;
                    }
                    else
                    {
                        g_updateCheckerResult = UpdateChecker::Result::AlreadyUpToDate;
                    }
                }
                else
                {
                    LOG_ERROR("Error while parsing response: tag_name does not contain a valid version string.");
                    g_updateCheckerResult = UpdateChecker::Result::Failed;
                }
            }
            else
            {
                LOG_ERROR("Error while parsing response: tag_name not found or not the right type.");
                g_updateCheckerResult = UpdateChecker::Result::Failed;
            }
        }
        catch (const json::exception &e)
        {
            LOGF_ERROR("Error while parsing response: {}", e.what());
            g_updateCheckerResult = UpdateChecker::Result::Failed;
        }
    }
    else
    {
        g_updateCheckerResult = UpdateChecker::Result::Failed;
    }

    g_updateCheckerFinished = true;
    g_updateCheckerInProgress = false;
}

void UpdateChecker::initialize()
{
#ifndef SHADOW_RECOMP_UWP
    curl_global_init(CURL_GLOBAL_DEFAULT);
#endif
}

bool UpdateChecker::start()
{
    if (g_updateCheckerInProgress)
    {
        return false;
    }

    g_updateCheckerInProgress = true;
    g_updateCheckerFinished = false;
    std::thread thread(&updateCheckerThread);
    thread.detach();

    return true;
}

UpdateChecker::Result UpdateChecker::check()
{
    if (g_updateCheckerFinished)
    {
        return g_updateCheckerResult;
    }
    else if (g_updateCheckerInProgress)
    {
        return UpdateChecker::Result::InProgress;
    }
    else
    {
        return UpdateChecker::Result::NotStarted;
    }
}

void UpdateChecker::visitWebsite()
{
#if defined(SHADOW_RECOMP_UWP)
    SDL_OpenURL(VISIT_URL);
#elif defined(WIN32)
    ShellExecuteA(0, 0, VISIT_URL, 0, 0, SW_SHOW);
#elif defined(__linux__)
    std::string command = "xdg-open " + std::string(VISIT_URL) + " &";
    std::system(command.c_str());
#elif defined(__APPLE__)
    std::string command = "open " + std::string(VISIT_URL) + " &";
    std::system(command.c_str());
#else
    static_assert(false, "Visit website not implemented for this platform.");
#endif
}
