#pragma once

struct PreloadContext
{
#if defined(_WIN32) && !defined(SHADOW_RECOMP_UWP)
    HANDLE handle{};
    HANDLE mappingHandle{};
    SIZE_T size{};
    PVOID view{};
    bool preloaded{};
#endif

    ~PreloadContext();
    void PreloadExecutable();
};
