/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"
#include "profile_update.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    struct GamescopeScalingPresentContract {
        VkPresentModeKHR lowerPresentMode{VK_PRESENT_MODE_FIFO_KHR};
        std::optional<VkPresentModeKHR> compositorPresentMode;
    };

    /// Proton 9's Wine renders a Vulkan window that has Win32 child windows
    /// into an offscreen client window under the 1x1 top-level window it keeps
    /// for that purpose, and copies it to the visible window once per game
    /// frame, so generated frames presented in between can never be shown.
    /// FIFO to that window also completes on Xwayland's fallback timer of about
    /// 17 ms whatever the refresh. Only that 1x1 top-level identifies it: a
    /// game window can exceed its top-level by any amount while the window
    /// manager settles a switch to fullscreen, and a visible window never has
    /// a 1x1 top-level. Proton 7 and 8 redirect the window in place instead,
    /// which this cannot see.
    [[nodiscard]] constexpr bool x11WindowPresentsOffscreen(
            const uint32_t width, const uint32_t height,
            const uint32_t topLevelWidth,
            const uint32_t topLevelHeight) noexcept {
        return topLevelWidth == 1 && topLevelHeight == 1 &&
            (width > 1 || height > 1);
    }

    /// Every private bridge output needs FIFO plus a distinct bounded future
    /// deadline: FIFO alone can still lose outputs that become ready together.
    /// MAILBOX avoids the second Wayland FIFO callback on the game's thread;
    /// retain lower FIFO unless both that mode and a timing clock are proven.
    /// Mailbox Presentation is a per-game compatibility opt-out from that
    /// ordering: an advertised MAILBOX owns both the lower swapchain and the
    /// bridge, so presents never wait for FIFO slots and a newer output may
    /// replace one not yet displayed.
    [[nodiscard]] constexpr GamescopeScalingPresentContract
    gamescopeScalingPresentContract(const VkPresentModeKHR intendedPresentMode,
            const bool gamescopeScalingSurface,
            const bool privateOrderedTransport,
            const bool lowerMailboxSupported,
            const bool refreshKnown,
            const bool mailboxPresentation = false) noexcept {
        if (mailboxPresentation && privateOrderedTransport &&
                lowerMailboxSupported) {
            return {
                .lowerPresentMode = VK_PRESENT_MODE_MAILBOX_KHR,
                .compositorPresentMode = gamescopeScalingSurface
                    ? std::optional{VK_PRESENT_MODE_MAILBOX_KHR}
                    : std::nullopt,
            };
        }
        if (gamescopeScalingSurface && privateOrderedTransport &&
                lowerMailboxSupported && refreshKnown) {
            return {
                .lowerPresentMode = VK_PRESENT_MODE_MAILBOX_KHR,
                .compositorPresentMode = VK_PRESENT_MODE_FIFO_KHR,
            };
        }
        return {.lowerPresentMode = intendedPresentMode};
    }

    [[nodiscard]] inline bool shouldRetrySwapchainWithApplicationMinimum(
            const VkResult result, const uint32_t requestedMinImages,
            const uint32_t provisionedMinImages) {
        if (requestedMinImages >= provisionedMinImages)
            return false;
        return result == VK_ERROR_INITIALIZATION_FAILED ||
            result == VK_ERROR_OUT_OF_DEVICE_MEMORY ||
            result == VK_ERROR_OUT_OF_HOST_MEMORY;
    }

    /// Return the largest generated batch that fits beyond the application's
    /// requested WSI ownership. The intercepted real frame already occupies
    /// one of those application images, so it must not be reserved a second
    /// time. A driver returning fewer images than requested is malformed and
    /// must fail closed.
    [[nodiscard]] inline size_t
    generatedFrameCapacitySupportedByExistingSwapchain(
            const uint32_t requestedMinImages,
            const size_t returnedImages) noexcept {
        if (returnedImages < requestedMinImages)
            return 0;
        return returnedImages - requestedMinImages;
    }

    /// Apply only the WSI mutations justified by resources provisioned on the
    /// application device. Standalone scaling needs transfer usage, but must
    /// not reserve FG images or change the application's present mode. A
    /// swapchain the reconstruction writes directly also needs storage usage.
    [[nodiscard]] inline bool applySwapchainCreateProvisioning(
            const ls::GameConf& profile, const uint32_t maxImages,
            VkSwapchainCreateInfoKHR& createInfo,
            const bool frameGenerationProvisioned,
            const bool spatialScalingActive,
            const bool orderedFrameGenerationTransport,
            const bool swapchainImageCountCompatibility = false,
            const bool directSpatialOutput = false) {
        if (!frameGenerationProvisioned && !spatialScalingActive)
            return false;

        createInfo.imageUsage |=
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (spatialScalingActive && directSpatialOutput)
            createInfo.imageUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
        if (!frameGenerationProvisioned)
            return false;

        switch (profile.pacing) {
            case ls::Pacing::None:
                // Preserve the application's requested WSI ownership and add
                // one image per generated output. Ordered FG-only restores
                // the release-3.0 relief image which keeps multi-output FIFO
                // batches off the headroom-tight admission path. Potentially
                // active combined scaling deliberately omits that sixth image:
                // some engines reject or churn the enlarged lower replacement
                // pool, while the replacement-prime and native-first paths
                // protect its real frame without double-counting it. An enabled
                // 1.0 scaler remains inactive and must preserve FG-only relief.
                if (!swapchainImageCountCompatibility) {
                    const bool combinedScalingTopology =
                        spatialScalingActive ||
                        ls::spatialScalingMayActivate(profile);
                    const uint64_t orderedFrameGenerationReliefImages =
                        orderedFrameGenerationTransport &&
                            !combinedScalingTopology
                        ? 1 : 0;
                    createInfo.minImageCount = static_cast<uint32_t>(std::min(
                        static_cast<uint64_t>(
                            std::numeric_limits<uint32_t>::max()
                        ),
                        static_cast<uint64_t>(createInfo.minImageCount) +
                            generatedFrameCapacityForProfile(profile) +
                            orderedFrameGenerationReliefImages
                    ));
                    if (maxImages && createInfo.minImageCount > maxImages)
                        createInfo.minImageCount = maxImages;
                }
                if (orderedFrameGenerationTransport) {
                    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                    return true;
                }
                return false;
        }
        return false;
    }

}
