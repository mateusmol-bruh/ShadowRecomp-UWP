#include <stdafx.h>
#include "heap.h"
#include "memory.h"
#include "function.h"
#include "x86_context.h"

#ifdef SHADOW_RECOMP_UWP
#include <os/uwp/platform.h>

void Memory::InitForGame()
{
    // Reserve the 32-bit address space only when launching the installed game.
    base = static_cast<uint8_t*>(VirtualAllocFromApp(nullptr, X86_MEMORY_SIZE, MEM_RESERVE, PAGE_NOACCESS));
    if (!base)
    {
        Uwp::LogStartup(fmt::format("Memory: guest address reservation failed (Win32 {}).", GetLastError()));
        return;
    }

    const auto commit = [this](size_t offset, size_t size)
    {
        return VirtualAllocFromApp(base + offset, size, MEM_COMMIT, PAGE_READWRITE) != nullptr;
    };

    constexpr size_t nullPageSize = 4096;
    // The XBE's small-block allocator at 0x425F6A uses the fixed page below 0x80001000.
    constexpr size_t xbeSmallBlockPool = 0x80000000;
    constexpr size_t xbeSmallBlockPoolSize = 0x1000;
    static_assert(X86_IMAGE_BASE + X86_IMAGE_SIZE + X86_CODE_SIZE * 2ull <= UwpGuestHeapBegin);
    if (!commit(nullPageSize, UwpGuestHeapBegin + UwpGuestHeapSize - nullPageSize)
        || !commit(xbeSmallBlockPool, xbeSmallBlockPoolSize)
        || !commit(UwpPhysicalHeapBegin, UwpPhysicalHeapSize)
        || !commit(NV2A_APERTURE_BASE, NV2A_APERTURE_SIZE)
        || !commit(MMIO_APERTURE_BASE, MMIO_APERTURE_SIZE))
    {
        const DWORD error = GetLastError();
        VirtualFree(base, 0, MEM_RELEASE);
        base = nullptr;
        Uwp::LogStartup(fmt::format("Memory: guest commit failed (Win32 {}).", error));
        return;
    }
#else
Memory::Memory()
{
#ifdef _WIN32

	static constexpr uintptr_t kCandidateBases[] = 
	{
		0x100000000ull,  
		0x200000000ull,
		0x300000000ull,
		0x400000000ull,
		0x500000000ull,
	};

	for (uintptr_t candidate : kCandidateBases)
	{
		base = (uint8_t*)VirtualAlloc(
			(void*)candidate,
			X86_MEMORY_SIZE,
			MEM_RESERVE | MEM_COMMIT,
			PAGE_READWRITE
		);

		if (base)
			break;
	}


	if (base == nullptr) 
	{
		DWORD error = GetLastError();
		fprintf(stderr, "Failed to allocate at 0x10000000 (Error: 0x%lX)\n", error);
		return;
	}

	DWORD oldProtect;
	VirtualProtect(base, 4096, PAGE_NOACCESS, &oldProtect);
#else
	base = (uint8_t*)mmap((void*)0x100000000ull, X86_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

	if (base == (uint8_t*)MAP_FAILED)
		base = (uint8_t*)mmap(NULL, X86_MEMORY_SIZE, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);

	mprotect(base, 4096, PROT_NONE);
#endif
#endif


	for (size_t i = 0; X86FuncMappings[i].guest != 0; i++)
	{
		if (X86FuncMappings[i].host != nullptr)
			InsertFunction(X86FuncMappings[i].guest, X86FuncMappings[i].host);
	}
}

void* MmGetHostAddress(uint32_t ptr)
{
	return g_memory.Translate(ptr);
}


