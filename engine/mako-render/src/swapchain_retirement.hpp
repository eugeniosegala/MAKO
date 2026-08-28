/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <chrono>
#include <cstring>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    // VK_KHR_swapchain_maintenance1 promoted the EXT extension without
    // changing its structure layouts or sType values. Freedesktop 23.08's
    // Vulkan headers predate the KHR spelling, so keep the ABI-facing types on
    // the EXT names while still negotiating the promoted runtime name.
    inline constexpr char khrSwapchainMaintenance1ExtensionName[] =
        "VK_KHR_swapchain_maintenance1";

#ifdef VK_KHR_swapchain_maintenance1
    static_assert(
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT ==
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR
    );
    static_assert(
        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT ==
        VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR
    );
    static_assert(
        sizeof(VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT) ==
        sizeof(VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR)
    );
    static_assert(
        sizeof(VkSwapchainPresentFenceInfoEXT) ==
        sizeof(VkSwapchainPresentFenceInfoKHR)
    );
#endif

    // The maintenance fence is the Vulkan lifetime proof. The grace interval
    // additionally lets an upper Gamescope WSI/protocol event loop observe a
    // replacement presentation before the retired lower WSI is destroyed.
    inline constexpr auto swapchainRetirementGracePeriod =
        std::chrono::milliseconds(50);

    /// Surface destruction is a terminal boundary only for lower swapchains
    /// created from that exact application-visible surface. A missing mapping
    /// must remain deferred for device teardown rather than match null handles.
    [[nodiscard]] constexpr bool retiredSwapchainBelongsToSurface(
            const VkSurfaceKHR retiredSurface,
            const VkSurfaceKHR destroyedSurface) noexcept {
        return retiredSurface != VK_NULL_HANDLE &&
            retiredSurface == destroyedSurface;
    }

    /// An upper WSI may destroy its application-visible swapchain before
    /// creating the replacement and consequently pass a null oldSwapchain to
    /// the lower layer. If MAKO retained that exact lower swapchain for
    /// maintenance-fence completion, it is still the non-retired swapchain
    /// associated with the native window. Hand it to the lower create exactly
    /// once so Vulkan can retire it without discarding MAKO's lifetime proof.
    [[nodiscard]] constexpr bool shouldHandoffRetainedSwapchainAsOld(
            const VkSwapchainKHR requestedOldSwapchain,
            const VkDevice createDevice,
            const VkSurfaceKHR createSurface,
            const VkDevice retiredDevice,
            const VkSurfaceKHR retiredSurface,
            const bool handoffConsumed) noexcept {
        return requestedOldSwapchain == VK_NULL_HANDLE &&
            createDevice != VK_NULL_HANDLE &&
            createDevice == retiredDevice &&
            createSurface != VK_NULL_HANDLE &&
            createSurface == retiredSurface &&
            !handoffConsumed;
    }

    /// Prefer the promoted extension name when the driver advertises it, then
    /// fall back to the EXT predecessor used by current Gamescope/RADV stacks.
    /// The feature bit is mandatory for either spelling.
    [[nodiscard]] constexpr const char*
    selectSwapchainMaintenance1Extension(
            const bool khrExtensionSupported,
            const bool extExtensionSupported,
            const bool featureSupported) noexcept {
        if (!featureSupported)
            return nullptr;
        if (khrExtensionSupported)
            return khrSwapchainMaintenance1ExtensionName;
        if (extExtensionSupported)
            return VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME;
        return nullptr;
    }

    // Vulkan guarantees that every extensible structure starts with sType and
    // pNext, but C++ strict-aliasing rules do not make an arbitrary Vulkan
    // structure a VkBaseInStructure object. Copy the common header so optimized
    // builds can traverse a caller-owned chain without type-punning UB.
    [[nodiscard]] inline VkBaseInStructure pNextHeader(
            const void* node) noexcept {
        VkBaseInStructure header{};
        if (node)
            std::memcpy(&header, node, sizeof(header));
        return header;
    }

    /// The Gamescope WSI layer enables swapchain-maintenance1 before calling
    /// MAKO. Preserve that negotiated device contract explicitly rather than
    /// inferring support later from the physical device or environment.
    [[nodiscard]] inline bool swapchainMaintenance1Enabled(
            const VkDeviceCreateInfo& createInfo) noexcept {
        if (createInfo.enabledExtensionCount > 0 &&
                !createInfo.ppEnabledExtensionNames) {
            return false;
        }
        bool extensionEnabled = false;
        for (uint32_t index = 0;
                index < createInfo.enabledExtensionCount; ++index) {
            const char* const extension =
                createInfo.ppEnabledExtensionNames[index];
            if (extension &&
                    (std::strcmp(
                        extension,
                        VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME
                    ) == 0 ||
                     std::strcmp(
                        extension,
                        khrSwapchainMaintenance1ExtensionName
                    ) == 0)) {
                extensionEnabled = true;
                break;
            }
        }
        if (!extensionEnabled)
            return false;

        for (const void* node = createInfo.pNext; node;) {
            const auto header = pNextHeader(node);
            if (header.sType ==
                    VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT) {
                const auto* features = reinterpret_cast<
                    const VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT*>(
                        node
                    );
                return features->swapchainMaintenance1 == VK_TRUE;
            }
            node = header.pNext;
        }
        return false;
    }

    [[nodiscard]] inline const VkSwapchainPresentFenceInfoEXT*
    findSwapchainPresentFenceInfo(const void* chain) noexcept {
        for (const void* node = chain; node;) {
            const auto header = pNextHeader(node);
            if (header.sType ==
                    VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT) {
                return reinterpret_cast<
                    const VkSwapchainPresentFenceInfoEXT*>(node);
            }
            node = header.pNext;
        }
        return nullptr;
    }

    /// A non-null upstream fence for this swapchain is the caller's Vulkan
    /// lifetime proof for the final lower present. MAKO never borrows, resets,
    /// or replaces that fence; it only preserves the chain and records that
    /// the one-shot recreation result followed a retirement-protected present.
    [[nodiscard]] inline bool upstreamPresentFenceProtectsSwapchain(
            const VkSwapchainPresentFenceInfoEXT* const info,
            const uint32_t swapchainIndex = 0) noexcept {
        return info && info->pFences &&
            swapchainIndex < info->swapchainCount &&
            info->pFences[swapchainIndex] != VK_NULL_HANDLE;
    }

    /// These results retain the queued present operation, including its
    /// maintenance1 fence signal. Allocation or device-loss failures do not
    /// provide that guarantee and must not leave an unsignalable fence tracked.
    [[nodiscard]] constexpr bool presentFenceWillSignal(
            const VkResult result) noexcept {
        return result == VK_SUCCESS ||
            result == VK_SUBOPTIMAL_KHR ||
            result == VK_ERROR_OUT_OF_DATE_KHR ||
            result == VK_ERROR_SURFACE_LOST_KHR ||
            result == VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT;
    }

}
