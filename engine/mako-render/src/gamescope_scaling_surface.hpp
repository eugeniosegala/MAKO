/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <memory>
#include <optional>
#include <string_view>
#include <vulkan/vulkan_core.h>

struct VkXcbSurfaceCreateInfoKHR;
struct VkXlibSurfaceCreateInfoKHR;

namespace mako::layer {

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
    /// change the application's surface transport.
    [[nodiscard]] constexpr bool needsGamescopeScalingSurface(
            const bool scalingProvisioned, const bool wsiIsolated,
            const bool spatialRole, const bool splitChain,
            const std::string_view gamescopeDisplay,
            const std::string_view waylandDisplay) noexcept {
        return scalingProvisioned && wsiIsolated && !spatialRole &&
            !splitChain && !gamescopeDisplay.empty() &&
            (waylandDisplay.empty() || waylandDisplay == gamescopeDisplay);
    }

    /// Owns only Gamescope's X11-window -> Wayland-buffer association. Vulkan
    /// still owns acquisition, presentation, synchronization and retirement.
    /// No Gamescope WSI layer or frame-limiter/timing/HDR interface is loaded.
    class GamescopeScalingSurface {
    public:
        GamescopeScalingSurface();
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
        /// Bind on first presentation, so merely preparing a replacement
        /// surface cannot steal an existing window's still-live image.
        [[nodiscard]] bool preparePresent(VkSurfaceKHR surface);
        [[nodiscard]] bool owns(VkSurfaceKHR surface) const;

        /// Preserve the application's X11 extent contract at both public
        /// capability-query entrypoints. Internal driver queries still see
        /// the variable Wayland extent needed for separate scaling output.
        /// nullopt means this adapter does not own the surface.
        [[nodiscard]] std::optional<VkResult> applicationCapabilities(
            VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR& capabilities) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}
