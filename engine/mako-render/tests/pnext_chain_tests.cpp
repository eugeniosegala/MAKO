/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pnext_chain.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    void testPresentTimingPrefix() {
        // Issue #48: vkd3d-proton prepends EXT timing ahead of the present ID,
        // maintenance1 mode override and fence. Keep borrowed payloads opaque:
        // filtering a link must not drop, rewrite or dereference their data.
        constexpr uint64_t timingPayload[]{0x1122334455667788ULL, 90'000'000};
        constexpr uint64_t presentIds[]{0x123456789abcdef0ULL};
        constexpr VkPresentModeKHR modes[]{VK_PRESENT_MODE_MAILBOX_KHR};
        const VkFence fences[]{VK_NULL_HANDLE};
        const VkSwapchainPresentFenceInfoEXT fence{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
            .pNext = nullptr,
            .swapchainCount = 1,
            .pFences = fences,
        };
        const VkSwapchainPresentModeInfoEXT mode{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
            .pNext = &fence,
            .swapchainCount = 1,
            .pPresentModes = modes,
        };
        const VkPresentIdKHR id{
            .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
            .pNext = &mode,
            .swapchainCount = 1,
            .pPresentIds = presentIds,
        };
        const detail::PresentTimingsInfoExtLayout timings{
            .sType = detail::presentTimingsInfoExtType,
            .pNext = &id,
            .swapchainCount = 1,
            .pTimingInfos = timingPayload,
        };
        const FilteredPresentPNextChain filtered(&timings, true, false);
        expect(filtered.valid(),
            "EXT present timing prefix rejected the first Proton present");
        const auto* copied = static_cast<
            const detail::PresentTimingsInfoExtLayout*>(filtered.head());
        expect(copied != &timings && copied->sType == timings.sType &&
                copied->swapchainCount == 1 &&
                copied->pTimingInfos == timingPayload,
            "copied timing node lost its type, count or borrowed payload");
        const auto* copiedId = static_cast<const VkPresentIdKHR*>(copied->pNext);
        expect(copiedId != &id && copiedId->sType == id.sType &&
                copiedId->swapchainCount == 1 &&
                copiedId->pPresentIds == presentIds &&
                copiedId->pNext == &fence,
            "timing filter lost the present ID or its unchanged fence suffix");
        expect(timings.pNext == &id && id.pNext == &mode &&
                mode.pNext == &fence && modes[0] == VK_PRESENT_MODE_MAILBOX_KHR &&
                timingPayload[0] == 0x1122334455667788ULL &&
                presentIds[0] == 0x123456789abcdef0ULL,
            "present filtering modified caller-owned links or payloads");

        const VkPresentRegionsKHR regions{
            .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
            .pNext = &fence,
            .swapchainCount = 1,
            .pRegions = nullptr,
        };
        // Null pTimingInfos is legal. Exercise timing between two removals,
        // and a scaling-only removal that must retain the dynamic mode node.
        const detail::PresentTimingsInfoExtLayout nullTimings{
            .sType = detail::presentTimingsInfoExtType,
            .pNext = &regions,
            .swapchainCount = 1,
            .pTimingInfos = nullptr,
        };
        auto modeHead = mode;
        modeHead.pNext = &nullTimings;
        for (const bool removeMode : {false, true}) {
            const FilteredPresentPNextChain spatial(
                &modeHead, removeMode, true
            );
            expect(spatial.valid(),
                "EXT timing between filtered present nodes was rejected");
            const void* timingHead = spatial.head();
            if (!removeMode) {
                const auto* keptMode = static_cast<
                    const VkSwapchainPresentModeInfoEXT*>(timingHead);
                expect(keptMode != &modeHead &&
                        keptMode->sType == modeHead.sType &&
                        keptMode->pPresentModes == modes,
                    "scaling-only filter removed the retained mode override");
                timingHead = keptMode->pNext;
            }
            const auto* keptTiming = static_cast<
                const detail::PresentTimingsInfoExtLayout*>(timingHead);
            expect(keptTiming != &nullTimings &&
                    keptTiming->sType == nullTimings.sType &&
                    keptTiming->swapchainCount == 1 &&
                    keptTiming->pTimingInfos == nullptr &&
                    keptTiming->pNext == &fence,
                "region filtering changed a null timing payload or suffix");
        }
        const FilteredPresentPNextChain suffix(&modeHead, true, false);
        expect(suffix.valid() && suffix.head() == &nullTimings,
            "timing in an untouched suffix was unnecessarily copied");
        const FilteredPresentPNextChain absent(&timings, false, true);
        const FilteredPresentPNextChain disabled(&timings, false, false);
        expect(absent.valid() && absent.head() == &timings &&
                disabled.valid() && disabled.head() == &timings,
            "passthrough timing chain changed without a removal target");

        const VkBaseInStructure unknown{
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = reinterpret_cast<const VkBaseInStructure*>(&mode),
        };
        auto unknownTimings = timings;
        unknownTimings.pNext = &unknown;
        const FilteredPresentPNextChain rejected(&unknownTimings, true, false);
        expect(!rejected.valid() && rejected.head() == &unknownTimings &&
                rejected.unsupportedStructureType() == unknown.sType &&
                unknownTimings.pNext == &unknown &&
                unknown.pNext == reinterpret_cast<const VkBaseInStructure*>(&mode),
            "timing support weakened unknown-prefix rejection or input ownership");
    }
}

int main() {
    testPresentTimingPrefix();
    const VkBaseInStructure tail{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
    };
    // Model Gamescope's lower-facing maintenance1 node: its mode list can live
    // in immutable storage and advertises MAILBOX only. Ordered SDR filters the
    // node; it must never const-cast and rewrite this array to FIFO.
    constexpr VkPresentModeKHR immutableModes[]{VK_PRESENT_MODE_MAILBOX_KHR};
    const VkSwapchainPresentModesCreateInfoEXT createModes{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT,
        .pNext = &tail,
        .presentModeCount = 1,
        .pPresentModes = immutableModes,
    };

    const FilteredSwapchainCreatePNextChain filteredHead(
        &createModes, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT
    );
    expect(filteredHead.valid() && filteredHead.head() == &tail,
        "head present-mode node was not filtered");
    expect(immutableModes[0] == VK_PRESENT_MODE_MAILBOX_KHR,
        "immutable Gamescope mode storage was modified");

    const FilteredSwapchainCreatePNextChain disabledCreateFilter(
        &createModes, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT,
        false
    );
    expect(disabledCreateFilter.valid() &&
            disabledCreateFilter.head() == &createModes,
        "disabled filtering changed a native passthrough chain");

    const VkDeviceGroupSwapchainCreateInfoKHR prefix{
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = &createModes,
        .modes = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR,
    };
    const auto* const originalCreateLink = prefix.pNext;
    const FilteredSwapchainCreatePNextChain filteredNestedCreate(
        &prefix, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT
    );
    expect(filteredNestedCreate.valid(),
        "known immutable swapchain-create prefix could not be copied");
    expect(prefix.pNext == originalCreateLink,
        "create filter rewrote caller-owned immutable storage");
    const auto* copiedCreatePrefix = reinterpret_cast<
        const VkDeviceGroupSwapchainCreateInfoKHR*>(
            filteredNestedCreate.head()
        );
    expect(copiedCreatePrefix != &prefix &&
            copiedCreatePrefix->sType == prefix.sType &&
            copiedCreatePrefix->modes == prefix.modes &&
            copiedCreatePrefix->pNext == &tail,
        "nested create filtering did not preserve the known prefix payload");

    const VkBaseInStructure unknownCreatePrefix{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = reinterpret_cast<const VkBaseInStructure*>(&createModes),
    };
    const FilteredSwapchainCreatePNextChain unknownCreateFilter(
        &unknownCreatePrefix,
        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT
    );
    expect(!unknownCreateFilter.valid() &&
            unknownCreateFilter.unsupportedStructureType() ==
                VK_STRUCTURE_TYPE_APPLICATION_INFO &&
            unknownCreatePrefix.pNext == reinterpret_cast<
                const VkBaseInStructure*>(&createModes),
        "unknown immutable create prefix was modified or truncated");

    VkBaseInStructure presentTail{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .pNext = nullptr,
    };
    VkPresentRegionsKHR regions{
        .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
        .pNext = &presentTail,
        .swapchainCount = 0,
        .pRegions = nullptr,
    };
    const VkDeviceGroupPresentInfoKHR immutablePresentPrefix{
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR,
        .pNext = &regions,
        .swapchainCount = 0,
        .pDeviceMasks = nullptr,
        .mode = VK_DEVICE_GROUP_PRESENT_MODE_LOCAL_BIT_KHR,
    };
    const auto* const originalPresentLink = immutablePresentPrefix.pNext;
    const FilteredPresentPNextChain filteredRegions(
        &immutablePresentPrefix, false, true
    );
    expect(filteredRegions.valid(),
        "known immutable present prefix could not be copied");
    expect(immutablePresentPrefix.pNext == originalPresentLink,
        "filter rewrote caller-owned immutable present storage");
    const auto* copiedPrefix = reinterpret_cast<
        const VkDeviceGroupPresentInfoKHR*>(filteredRegions.head());
    expect(copiedPrefix != &immutablePresentPrefix,
        "nested filtering retained the caller-owned prefix node");
    expect(copiedPrefix->sType == immutablePresentPrefix.sType &&
            copiedPrefix->mode == immutablePresentPrefix.mode,
        "nested filtering did not preserve the known prefix payload");
    expect(copiedPrefix->pNext == &presentTail,
        "nested present-region node was not safely filtered");

    VkSwapchainPresentModeInfoEXT presentModeInfo{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODE_INFO_EXT,
        .pNext = &regions,
        .swapchainCount = 0,
        .pPresentModes = nullptr,
    };
    const FilteredPresentPNextChain filteredBoth(
        &presentModeInfo, true, true
    );
    expect(filteredBoth.valid() && filteredBoth.head() == &presentTail,
        "two present-chain targets were not filtered together");
    expect(presentModeInfo.pNext == &regions && regions.pNext == &presentTail,
        "multi-target filtering rewrote the application chain");

    const FilteredPresentPNextChain disabledPresentFilter(
        &immutablePresentPrefix, false, false
    );
    expect(disabledPresentFilter.valid() &&
            disabledPresentFilter.head() == &immutablePresentPrefix,
        "disabled present filtering copied or changed the input chain");

    VkBaseInStructure unknownPrefix{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = reinterpret_cast<const VkBaseInStructure*>(&regions),
    };
    const FilteredPresentPNextChain unknownFilter(
        &unknownPrefix, false, true
    );
    expect(!unknownFilter.valid() &&
            unknownFilter.unsupportedStructureType() ==
                VK_STRUCTURE_TYPE_APPLICATION_INFO,
        "unknown prefix was truncated instead of failing closed");
    expect(unknownPrefix.pNext ==
            reinterpret_cast<const VkBaseInStructure*>(&regions),
        "failed filtering changed the unknown input prefix");

    std::cout << "pNext chain tests passed\n";
    return 0;
}
