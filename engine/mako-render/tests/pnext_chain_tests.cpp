/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "pnext_chain.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    VkBaseOutStructure tail{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
    };
    // Model Gamescope's lower-facing maintenance1 node: its mode list can live
    // in immutable storage and advertises MAILBOX only. Ordered SDR filters the
    // node; it must never const-cast and rewrite this array to FIFO.
    constexpr VkPresentModeKHR immutableModes[]{VK_PRESENT_MODE_MAILBOX_KHR};
    VkSwapchainPresentModesCreateInfoEXT createModes{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT,
        .pNext = &tail,
        .presentModeCount = 1,
        .pPresentModes = immutableModes,
    };

    const void* head = &createModes;
    {
        ScopedPNextRemoval removal(
            head, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT
        );
        expect(head == &tail, "head present-mode node was not filtered");
        expect(immutableModes[0] == VK_PRESENT_MODE_MAILBOX_KHR,
            "immutable Gamescope mode storage was modified");
    }
    expect(head == &createModes, "head pNext chain was not restored");

    {
        // HDR/passthrough transport: disabled filtering must preserve the full
        // Gamescope node exactly, including its immutable MAILBOX list.
        ScopedPNextRemoval disabled(
            head, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT,
            false
        );
        expect(head == &createModes,
            "disabled filtering changed a native passthrough chain");
    }

    VkBaseOutStructure prefix{
        .sType = VK_STRUCTURE_TYPE_DEVICE_GROUP_PRESENT_INFO_KHR,
        .pNext = reinterpret_cast<VkBaseOutStructure*>(&createModes),
    };
    head = &prefix;
    {
        ScopedPNextRemoval removal(
            head, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT
        );
        expect(head == &prefix, "nested removal changed the chain head");
        expect(prefix.pNext == &tail, "nested present-mode node was not filtered");
    }
    expect(prefix.pNext == reinterpret_cast<VkBaseOutStructure*>(&createModes),
        "nested pNext chain was not restored");

    try {
        ScopedPNextRemoval removal(
            head, VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_MODES_CREATE_INFO_EXT
        );
        throw std::runtime_error("test");
    } catch (const std::runtime_error&) {
    }
    expect(prefix.pNext == reinterpret_cast<VkBaseOutStructure*>(&createModes),
        "exception path did not restore the pNext chain");

    VkBaseInStructure presentTail{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .pNext = nullptr,
    };
    VkPresentRegionsKHR regions{
        .sType = VK_STRUCTURE_TYPE_PRESENT_REGIONS_KHR,
        .pNext = &presentTail,
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
