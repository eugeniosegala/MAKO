/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "pnext_chain.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <vulkan/vulkan_core.h>

struct VkXcbSurfaceCreateInfoKHR;
struct VkXlibSurfaceCreateInfoKHR;

namespace mako::layer {

    /// Negotiate application completion pacing before device creation. The
    /// private generated-output queue adds latency to the final real image;
    /// presentation-wait clients can otherwise feed that delay back into
    /// production of the next real frame. Keep this process-start choice
    /// independent of live multiplier, VRR and 0x changes.
    [[nodiscard]] constexpr bool needsBridgePresentWaitFallback(
            const bool bridgeConnected, const bool applicationFacingRole,
            const bool generationProvisioned) noexcept {
        return bridgeConnected && applicationFacingRole && generationProvisioned;
    }

    inline void disableBridgePresentWaitFeatures(
            VkPhysicalDeviceFeatures2& features) noexcept {
        for (auto* item = static_cast<VkBaseOutStructure*>(features.pNext);
                item; item = item->pNext) {
            if (item->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_FEATURES_KHR)
                reinterpret_cast<VkPhysicalDevicePresentWaitFeaturesKHR*>(item)
                    ->presentWait = VK_FALSE;
#if defined(VK_KHR_present_wait2)
            if (item->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PRESENT_WAIT_2_FEATURES_KHR)
                reinterpret_cast<VkPhysicalDevicePresentWait2FeaturesKHR*>(item)
                    ->presentWait2 = VK_FALSE;
#endif
        }
    }

    /// Headless probes and native Wayland instances need no X11 adapter.
    [[nodiscard]] inline bool requestsGamescopeScalingSurface(
            const VkInstanceCreateInfo& info) noexcept {
        bool surface = false;
        bool x11 = false;
        for (uint32_t i = 0; i < info.enabledExtensionCount; ++i) {
            const std::string_view name = info.ppEnabledExtensionNames[i];
            surface |= name == "VK_KHR_surface";
            x11 |= name == "VK_KHR_xcb_surface" || name == "VK_KHR_xlib_surface";
        }
        return surface && x11;
    }

    /// The surface-only connection is mutually exclusive with full Gamescope
    /// WSI. Both are process-start choices; a live scaler selection must not
    /// change the application's surface transport. At startup, differing socket
    /// names need filesystem identity checks for Steam Runtime aliases and its
    /// missing default display. Unrelated or inherited connections stay isolated.
    [[nodiscard]] bool needsGamescopeScalingSurface(
            bool scalingProvisioned, bool wsiIsolated,
            bool spatialRole, bool splitChain,
            std::string_view gamescopeDisplay, std::string_view waylandDisplay,
            std::string_view runtimeDirectory = {}, bool inheritedSocket = false);

    /// An application-facing layer below MAKO may own the global enumeration
    /// entrypoint but report only its layer-specific extensions. In that case
    /// VK_ERROR_LAYER_NOT_PRESENT means driver support is opaque, not absent.
    /// The bridge remains fail-closed for every other enumeration failure.
    [[nodiscard]] constexpr bool canAttemptGamescopeScalingSurface(
            const VkResult enumerationResult,
            const bool waylandSurfaceAdvertised) noexcept {
        return waylandSurfaceAdvertised ||
            enumerationResult == VK_ERROR_LAYER_NOT_PRESENT;
    }

    /// Owns Gamescope's X11-window -> Wayland-buffer association and protocol
    /// timing. Vulkan owns acquisition, synchronization and retirement.
    /// Ordered private outputs use bounded desired presentation times.
    /// The same surface owner forwards optional HDR colour and metadata. No
    /// Gamescope WSI layer or frame-limiter interface is loaded.
    class GamescopeScalingSurface {
    public:
        using HdrOutputQuery = std::function<std::optional<bool>(uint32_t gamescopePid)>;
        explicit GamescopeScalingSurface(bool allowHdr = false,
            HdrOutputQuery rootHdrOutput = {});
        ~GamescopeScalingSurface();
        GamescopeScalingSurface(const GamescopeScalingSurface&) = delete;
        GamescopeScalingSurface& operator=(const GamescopeScalingSurface&) = delete;

        /// Resolve the optional runtime libraries and connect before instance
        /// creation. False leaves the ordinary application surface untouched.
        [[nodiscard]] bool connect();
        [[nodiscard]] std::optional<VkResult> create(
            VkInstance instance, PFN_vkGetInstanceProcAddr next,
            const VkXcbSurfaceCreateInfoKHR& info,
            const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);
        [[nodiscard]] std::optional<VkResult> create(
            VkInstance instance, PFN_vkGetInstanceProcAddr next,
            const VkXlibSurfaceCreateInfoKHR& info,
            const VkAllocationCallbacks* allocator, VkSurfaceKHR* surface);

        /// Called after Vulkan has destroyed the surface and retired all its
        /// swapchains. Wayland objects must outlive those driver resources.
        void destroy(VkSurfaceKHR surface);
        /// Mirror Gamescope WSI's per-Vulkan-swapchain protocol lifetime. The
        /// compositor needs the real image count before low-latency delivery,
        /// and a replacement must not inherit its predecessor's pacing state.
        /// applicationExtent is the original game request, before expansion.
        [[nodiscard]] bool createSwapchain(VkSurfaceKHR surface,
            VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& info,
            VkExtent2D applicationExtent, uint32_t imageCount, std::string_view engineName,
            std::optional<VkPresentModeKHR> compositorPresentMode,
            uint32_t refreshHz = 0);
        void destroySwapchain(VkSurfaceKHR surface, VkSwapchainKHR swapchain);

        /// Drain already-read protocol events and return retirement or a cached
        /// X11 extent change before acquiring an application image. No socket
        /// read, X11 query, image acquisition or semaphore signal occurs.
        /// The private variable Wayland surface cannot report this mismatch.
        [[nodiscard]] VkResult acquisitionResult(
            VkSurfaceKHR surface, VkSwapchainKHR swapchain) const;

        /// Bind the matching protocol object before every lower presentation,
        /// including each generated output. Association recovers Xwayland and
        /// Steam UI mapping changes; the protocol mode covers one commit only.
        /// Retirement requests swapchain recreation, not surface destruction.
        /// Callers must complete rejected-present queue operations on failure.
        /// Managed outputs share their source frame serial so GPU readiness
        /// observes one source batch, including all generated/real outputs.
        [[nodiscard]] VkResult preparePresent(
            VkSurfaceKHR surface, VkSwapchainKHR swapchain,
            double outputFps = 0.0, uint32_t refreshHz = 0,
            size_t outputBatchSize = 1, bool generationEnabled = false,
            std::optional<uint64_t> sourceFrameSerial = std::nullopt);
        [[nodiscard]] bool owns(VkSurfaceKHR surface) const;
        [[nodiscard]] bool hdrEnabled() const;

        /// Validate HDR against this surface's compositor and lower image
        /// formats, then normalize only the driver-facing colour space.
        [[nodiscard]] VkResult prepareSwapchain(VkPhysicalDevice physicalDevice,
            VkSwapchainCreateInfoKHR& info,
            PFN_vkGetPhysicalDeviceSurfaceFormatsKHR lowerFormats) const;
        /// True means this bridge owns the swapchain, including ignored SDR
        /// metadata. Unowned swapchains must retain lower extension dispatch.
        [[nodiscard]] bool setHdrMetadata(VkSurfaceKHR surface,
            VkSwapchainKHR swapchain, const VkHdrMetadataEXT& metadata);

        /// Preserve the application's X11 extent contract at both public
        /// capability-query entrypoints. Internal driver queries still see
        /// the variable Wayland extent needed for separate scaling output.
        /// nullopt means this adapter does not own the surface.
        [[nodiscard]] std::optional<VkResult> applicationCapabilities(
            VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) const;
        /// Preserve the X11 format order while exposing only format/color-space
        /// pairs also supported by the private Wayland surface. A failed proof
        /// leaves the lower driver's list intact.
        [[nodiscard]] std::optional<std::vector<VkSurfaceFormatKHR>> applicationFormats(
            VkPhysicalDevice physicalDevice, VkSurfaceKHR surface,
            PFN_vkGetPhysicalDeviceSurfaceFormatsKHR lowerFormats) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
