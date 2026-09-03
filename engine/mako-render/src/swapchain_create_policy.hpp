/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"
#include "profile_update.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

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
    /// not reserve FG images or change the application's present mode.
    [[nodiscard]] inline bool applySwapchainCreateProvisioning(
            const ls::GameConf& profile, const uint32_t maxImages,
            VkSwapchainCreateInfoKHR& createInfo,
            const bool frameGenerationProvisioned,
            const bool spatialScalingActive,
            const bool orderedFrameGenerationTransport,
            const bool swapchainImageCountCompatibility = false) {
        if (!frameGenerationProvisioned && !spatialScalingActive)
            return false;

        createInfo.imageUsage |=
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (!frameGenerationProvisioned)
            return false;

        switch (profile.pacing) {
            case ls::Pacing::None:
                // Preserve the application's requested WSI ownership and add
                // one image per generated output. Ordered FG-only restores
                // the release-3.0 relief image which keeps multi-output FIFO
                // batches off the headroom-tight admission path. Combined
                // scaling deliberately omits that sixth image: some engines
                // reject or churn the enlarged lower replacement pool, while
                // the replacement-prime and native-first paths protect its
                // real frame without double-counting it.
                if (!swapchainImageCountCompatibility) {
                    const uint64_t orderedFrameGenerationReliefImages =
                        orderedFrameGenerationTransport &&
                            !ls::spatialScalingRequested(profile)
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
