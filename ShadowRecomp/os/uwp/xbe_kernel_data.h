#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace Uwp
{
    struct XbeKernelData
    {
        struct HardwareInfo
        {
            uint32_t flags = 0;
            uint8_t gpuRevision = 0;
            uint8_t mcpRevision = 0;
            uint16_t reserved = 0;
        } hardware;

        struct KernelVersion
        {
            uint16_t major = 1;
            uint16_t minor = 0;
            uint16_t build = 5838;
            uint16_t qfe = 1;
        } version;

        struct AnsiString
        {
            uint16_t length = 0;
            uint16_t maximumLength = 0;
            uint32_t buffer = 0;
        } imageFileName;

        uint32_t cachePartitions = 3;
        uint32_t launchDataPage = 0;
        uint32_t tickCount = 0; // The title's tick-count routine is already hooked.
        uint32_t eventType = 0;
        uint32_t threadType = 0;
        uint32_t bootVideoMode = 0;
        char imageName[sizeof("D:\\default.xbe")] = "D:\\default.xbe";
        // Signature calculations use the port's existing crypto hooks.
        std::array<uint8_t, 16> hdKey{};
        std::array<uint8_t, 16> signatureKey{};
        std::array<uint8_t, 16 * 16> alternateSignatureKeys{};
        std::array<uint32_t, 128> ideChannel{};
    };

    static_assert(sizeof(XbeKernelData::HardwareInfo) == 8);
    static_assert(sizeof(XbeKernelData::KernelVersion) == 8);
    static_assert(sizeof(XbeKernelData::AnsiString) == 8);

    inline size_t BindXbeKernelData(std::span<uint32_t> thunks, uint32_t guestAddress, XbeKernelData& data)
    {
        data.imageFileName = { sizeof(data.imageName) - 1, sizeof(data.imageName),
            guestAddress + static_cast<uint32_t>(offsetof(XbeKernelData, imageName)) };

        // Callable imports are resolved by the recompiler. Only replace the
        // data ordinals used by this title with addresses in guest memory.
        constexpr std::array offsets = {
            std::pair{ 16u, offsetof(XbeKernelData, eventType) },
            std::pair{ 40u, offsetof(XbeKernelData, cachePartitions) },
            std::pair{ 156u, offsetof(XbeKernelData, tickCount) },
            std::pair{ 164u, offsetof(XbeKernelData, launchDataPage) },
            std::pair{ 259u, offsetof(XbeKernelData, threadType) },
            std::pair{ 322u, offsetof(XbeKernelData, hardware) },
            std::pair{ 323u, offsetof(XbeKernelData, hdKey) },
            std::pair{ 324u, offsetof(XbeKernelData, version) },
            std::pair{ 325u, offsetof(XbeKernelData, signatureKey) },
            std::pair{ 326u, offsetof(XbeKernelData, imageFileName) },
            std::pair{ 354u, offsetof(XbeKernelData, alternateSignatureKeys) },
            std::pair{ 356u, offsetof(XbeKernelData, bootVideoMode) },
            std::pair{ 357u, offsetof(XbeKernelData, ideChannel) },
        };
        size_t count = 0;
        for (auto& thunk : thunks)
        {
            for (const auto& [ordinal, offset] : offsets)
            {
                if (thunk != (0x80000000u | ordinal))
                    continue;
                thunk = guestAddress + static_cast<uint32_t>(offset);
                ++count;
                break;
            }
        }
        return count;
    }
}
