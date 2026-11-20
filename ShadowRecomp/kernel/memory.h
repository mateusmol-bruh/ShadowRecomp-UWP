#pragma once

#ifndef _WIN32
#define MEM_COMMIT  0x00001000  
#define MEM_RESERVE 0x00002000  
#endif

// Xbox address space map (the top of the 4 GB region is hardware, not RAM).
// NV2A registers start at 0xFD000000 and the APU at 0xFE800000. 
// XboxRecomp gives access in those ranges through g_nv2aPtr[off] and g_mmio[off].

constexpr uint32_t NV2A_APERTURE_BASE = 0xFD000000u;
constexpr uint32_t NV2A_APERTURE_SIZE = 0x01000000u;  // 16 MiB the real BAR0 size.
constexpr uint32_t MMIO_APERTURE_BASE = 0xFE800000u;
constexpr uint32_t MMIO_APERTURE_SIZE = 0x00800000u;  // 8 MiB

#ifdef SHADOW_RECOMP_UWP
constexpr size_t UwpGuestHeapBegin = 0x016A1000ull;
constexpr size_t UwpPhysicalHeapBegin = 0xA0000000ull;
constexpr size_t UwpGuestHeapSize = 256ull * 1024 * 1024;
constexpr size_t UwpPhysicalHeapSize = 256ull * 1024 * 1024;
#endif

struct Memory
{
    uint8_t* base{};

#ifdef SHADOW_RECOMP_UWP
    void InitForGame();
#else
    Memory();
#endif

    bool IsInMemoryRange(const void* host) const noexcept
    {
        return host >= base && host < (base + X86_MEMORY_SIZE);
    }

    void* Translate(size_t offset) const noexcept
    {
        if (offset)
            assert(offset < 0x100000000ull);

        return base + offset;
    }

    uint32_t MapVirtual(const void* host) const noexcept
    {
        if (host)
            assert(IsInMemoryRange(host));

        return static_cast<uint32_t>(static_cast<const uint8_t*>(host) - base);
    }

    X86Func* FindFunction(uint32_t guest) const noexcept
    {
        return X86_LOOKUP_FUNC(base, guest);
    }

    void InsertFunction(uint32_t guest, X86Func* host)
    {
        X86_LOOKUP_FUNC(base, guest) = host;
    }
};

extern "C" void* MmGetHostAddress(uint32_t ptr);
extern Memory g_memory;
