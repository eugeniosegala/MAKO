/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "swapchain_retirement.hpp"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(1);
        }
    }
}

int main() {
    expect(std::string_view(
            selectSwapchainMaintenance1Extension(true, true, true)
        ) == khrSwapchainMaintenance1ExtensionName,
        "the promoted maintenance1 extension was not preferred");
    expect(std::string_view(
            selectSwapchainMaintenance1Extension(false, true, true)
        ) == VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
        "the EXT maintenance1 fallback was not selected");
    expect(!selectSwapchainMaintenance1Extension(true, true, false) &&
            !selectSwapchainMaintenance1Extension(false, false, true),
        "maintenance1 was selected without both extension and feature support");

    const std::array<const char*, 1> swapchainExtensions{
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo presentationDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount =
            static_cast<uint32_t>(swapchainExtensions.size()),
        .ppEnabledExtensionNames = swapchainExtensions.data(),
    };
    expect(swapchainPresentationEnabled(presentationDevice),
        "a swapchain presentation device was not recognized");

    const VkDeviceCreateInfo computeOnlyDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    };
    expect(!swapchainPresentationEnabled(computeOnlyDevice),
        "a compute-only device was treated as a presentation device");

    const VkDeviceCreateInfo malformedPresentationDevice{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .enabledExtensionCount = 1,
    };
    expect(!swapchainPresentationEnabled(malformedPresentationDevice),
        "a malformed presentation extension list was accepted");

    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT maintenance{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT,
        .swapchainMaintenance1 = VK_TRUE,
    };
    const std::array<const char*, 1> extensions{
        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
    };
    const VkDeviceCreateInfo enabled{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    expect(swapchainMaintenance1Enabled(enabled),
        "enabled maintenance1 extension and feature were not recognized");

    maintenance.swapchainMaintenance1 = VK_FALSE;
    expect(!swapchainMaintenance1Enabled(enabled),
        "a disabled maintenance1 feature was accepted");
    maintenance.swapchainMaintenance1 = VK_TRUE;

    const VkDeviceCreateInfo missingExtension{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
    };
    expect(!swapchainMaintenance1Enabled(missingExtension),
        "a maintenance1 feature without its extension was accepted");

    const VkDeviceCreateInfo malformedExtensionList{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
        .enabledExtensionCount = 1,
    };
    expect(!swapchainMaintenance1Enabled(malformedExtensionList),
        "a malformed extension list was dereferenced or accepted");

    const std::array<const char*, 1> khrExtensions{
        khrSwapchainMaintenance1ExtensionName,
    };
    const VkDeviceCreateInfo khrEnabled{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &maintenance,
        .enabledExtensionCount = static_cast<uint32_t>(khrExtensions.size()),
        .ppEnabledExtensionNames = khrExtensions.data(),
    };
    expect(swapchainMaintenance1Enabled(khrEnabled),
        "the promoted KHR maintenance1 extension was not recognized");

    VkFence fence = VK_NULL_HANDLE;
    const VkSwapchainPresentFenceInfoEXT presentFence{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT,
        .swapchainCount = 1,
        .pFences = &fence,
    };
    expect(findSwapchainPresentFenceInfo(&presentFence) == &presentFence,
        "present-fence pNext lookup failed");
    expect(!upstreamPresentFenceProtectsSwapchain(&presentFence),
        "a null upstream present fence was accepted as lifetime proof");
    fence = reinterpret_cast<VkFence>(static_cast<uintptr_t>(1));
    expect(upstreamPresentFenceProtectsSwapchain(&presentFence),
        "a valid upstream present fence was not accepted as lifetime proof");
    expect(!upstreamPresentFenceProtectsSwapchain(&presentFence, 1),
        "an out-of-range upstream present fence was accepted");

    const VkBaseInStructure precedingNode{
        .sType = VK_STRUCTURE_TYPE_PRESENT_ID_KHR,
        .pNext = reinterpret_cast<const VkBaseInStructure*>(&presentFence),
    };
    // Production pNext chains enter through an opaque Vulkan ABI pointer.
    // Keep the fixture opaque too so LTO cannot reason across distinct Vulkan
    // aggregate types in a way that no real loader call permits.
    const void* volatile opaquePrecedingNode = &precedingNode;
    expect(findSwapchainPresentFenceInfo(opaquePrecedingNode) == &presentFence,
        "present-fence lookup failed behind a preceding pNext node");

    expect(swapchainRetirementGracePeriod == std::chrono::milliseconds(50),
        "the compositor retirement grace contract changed unexpectedly");

    const auto surfaceA = reinterpret_cast<VkSurfaceKHR>(1);
    const auto surfaceB = reinterpret_cast<VkSurfaceKHR>(2);
    expect(retiredSwapchainBelongsToSurface(surfaceA, surfaceA),
        "the creating surface did not own terminal retirement");
    expect(!retiredSwapchainBelongsToSurface(surfaceA, surfaceB) &&
            !retiredSwapchainBelongsToSurface(VK_NULL_HANDLE, surfaceA),
        "surface-terminal retirement crossed its exact non-null owner");

    const auto deviceA = reinterpret_cast<VkDevice>(1);
    const auto deviceB = reinterpret_cast<VkDevice>(2);
    const auto swapchainA = reinterpret_cast<VkSwapchainKHR>(1);
    expect(shouldHandoffRetainedSwapchainAsOld(
            VK_NULL_HANDLE, deviceA, surfaceA, deviceA, surfaceA, false),
        "a retained lower swapchain was not selected for null-old replacement");
    expect(!shouldHandoffRetainedSwapchainAsOld(
            swapchainA, deviceA, surfaceA, deviceA, surfaceA, false) &&
            !shouldHandoffRetainedSwapchainAsOld(
                VK_NULL_HANDLE, deviceA, surfaceA, deviceB, surfaceA, false) &&
            !shouldHandoffRetainedSwapchainAsOld(
                VK_NULL_HANDLE, deviceA, surfaceA, deviceA, surfaceB, false) &&
            !shouldHandoffRetainedSwapchainAsOld(
                VK_NULL_HANDLE, deviceA, surfaceA, deviceA, surfaceA, true),
        "retained lower handoff ignored old-swapchain, owner, or one-shot bounds");

    expect(presentFenceWillSignal(VK_SUCCESS) &&
            presentFenceWillSignal(VK_SUBOPTIMAL_KHR) &&
            presentFenceWillSignal(VK_ERROR_OUT_OF_DATE_KHR) &&
            presentFenceWillSignal(VK_ERROR_SURFACE_LOST_KHR) &&
            presentFenceWillSignal(
                VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT
            ),
        "a queued presentation result lost its retirement fence");
    expect(!presentFenceWillSignal(VK_ERROR_OUT_OF_HOST_MEMORY) &&
            !presentFenceWillSignal(VK_ERROR_OUT_OF_DEVICE_MEMORY) &&
            !presentFenceWillSignal(VK_ERROR_DEVICE_LOST),
        "an unqueueable failure retained an unsignalable fence");

    std::cout << "swapchain retirement tests passed\n";
    return 0;
}
