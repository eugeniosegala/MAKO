/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"
#include "profile_update.hpp"

#include <algorithm>
#include <cstdint>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Apply only the WSI mutations justified by resources provisioned on the
    /// application device. Standalone scaling needs transfer usage, but must
    /// not reserve FG images or change the application's present mode.
    [[nodiscard]] inline bool applySwapchainCreateProvisioning(
            const ls::GameConf& profile, const uint32_t maxImages,
            VkSwapchainCreateInfoKHR& createInfo,
            const bool frameGenerationProvisioned,
            const bool spatialScalingActive,
            const bool orderedFrameGenerationTransport) {
        if (!frameGenerationProvisioned && !spatialScalingActive)
            return false;

        createInfo.imageUsage |=
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (!frameGenerationProvisioned)
            return false;

        switch (profile.pacing) {
            case ls::Pacing::None:
                // One intercepted application present keeps the real image
                // acquired while MAKO acquires at most one image per
                // generated output. Reserve that total only when the
                // application's own minimum is smaller. Adding the two
                // requirements needlessly changes a three-image application
                // swapchain into six images for a 3x-capable profile and can
                // break engines with otherwise avoidable WSI image-count
                // assumptions. If other application frames temporarily hold
                // the remaining images, generated-image acquire recovery
                // already retains a native present instead of requiring
                // permanent app-visible headroom.
                createInfo.minImageCount = std::max(
                    createInfo.minImageCount,
                    static_cast<uint32_t>(
                        generatedFrameCapacityForProfile(profile) + 1
                    )
                );
                if (maxImages && createInfo.minImageCount > maxImages)
                    createInfo.minImageCount = maxImages;
                if (orderedFrameGenerationTransport) {
                    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
                    return true;
                }
                return false;
        }
        return false;
    }

}
