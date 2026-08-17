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

    std::cout << "pNext chain tests passed\n";
    return 0;
}
