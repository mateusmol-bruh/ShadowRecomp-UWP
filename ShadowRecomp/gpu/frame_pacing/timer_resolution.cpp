#include "timer_resolution.h"

#if defined(_WIN32) && !defined(SHADOW_RECOMP_UWP)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

/*
 * NtQueryTimerResolution/NtSetTimerResolution are technically internal APIs
 * but have been used in many pieces of software for years, including the
 * Chrome browser.
 */
typedef LONG NTSTATUS;
typedef NTSTATUS(NTAPI* QueryTimerResolutionFn)(PULONG, PULONG, PULONG);
typedef NTSTATUS(NTAPI* SetTimerResolutionFn)(ULONG, BOOLEAN, PULONG);
#elif defined(__linux__)
#include <sys/prctl.h>
#endif

TIMER_RESOLUTION_THREAD_LOCAL double TimerResolution::s_granularityMs = 0.5;
TIMER_RESOLUTION_THREAD_LOCAL bool TimerResolution::s_isActive = false;
TIMER_RESOLUTION_THREAD_LOCAL bool TimerResolution::s_isResolved = false;

bool TimerResolution::SetHighestResolution()
{
    if (s_isActive)
        return true;

#if defined(_WIN32) && !defined(SHADOW_RECOMP_UWP)
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr)
        return false;

    const auto query = reinterpret_cast<QueryTimerResolutionFn>(
        GetProcAddress(ntdll, "NtQueryTimerResolution"));
    const auto set = reinterpret_cast<SetTimerResolutionFn>(
        GetProcAddress(ntdll, "NtSetTimerResolution"));
    if (query == nullptr || set == nullptr)
        return false;

    // The maximum resolution value is the finest tick the OS supports.
    ULONG minResolution, maxResolution, actualResolution;
    query(&minResolution, &maxResolution, &actualResolution);

    if (set(maxResolution, TRUE, &actualResolution) == 0)
    {
        s_granularityMs = actualResolution / 10000.0; // 100ns ticks to ms
        s_isActive = true;
        return true;
    }
#elif defined(__linux__)
    // Slack tells how late a wake may land. 50us is the kernel default.
    // Set it in case it was overriden externally.
    prctl(PR_SET_TIMERSLACK, 50000UL);
    s_granularityMs = static_cast<double>(prctl(PR_GET_TIMERSLACK)) / 1000000.0;
    s_isActive = true;
    return true;
#else
    return false;
#endif
}

double TimerResolution::GetGranularityMs()
{
    if (!s_isResolved)
    {
        s_isResolved = true;
        SetHighestResolution();
    }

    return s_granularityMs;
}

void TimerResolution::Release()
{
#if defined(_WIN32) && !defined(SHADOW_RECOMP_UWP)
    if (!s_isActive)
        return;

    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll != nullptr)
    {
        const auto set = reinterpret_cast<SetTimerResolutionFn>(
            GetProcAddress(ntdll, "NtSetTimerResolution"));
        if (set != nullptr)
        {
            ULONG actualResolution;
            set(0, FALSE, &actualResolution);
        }
    }

    s_isActive = false;
    s_granularityMs = 0.5;
    s_isResolved = false;
#elif defined(__linux__)
    // Slack is per-thread; nothing process/system-wide to undo.
#endif
}
