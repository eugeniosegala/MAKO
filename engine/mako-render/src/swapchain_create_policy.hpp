/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"
#include "profile_update.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    inline constexpr uint32_t
        swapchainImageCountShortLivedReturnedPresentMinimum = 2;
    inline constexpr uint32_t
        swapchainImageCountShortLivedReturnedPresentMaximum = 8;
    inline constexpr uint64_t
        swapchainImageCountZeroReturnProbeLifetimeLimitMs = 2'000;
    inline constexpr uint64_t
        swapchainImageCountReplacementLifetimeLimitMs = 10'000;

    struct SwapchainImageCountReplacementCandidate {
        VkExtent2D applicationExtent{};
        uint32_t requestedMinImages{0};
        uint32_t provisionedMinImages{0};
        uint32_t returnedPresentCount{0};
        uint64_t activeLifetimeMs{0};
    };

    struct SwapchainImageCountCompatibilityState {
        bool zeroReturnProbeObserved{false};
        std::optional<SwapchainImageCountReplacementCandidate>
            replacementCandidate;
        std::vector<SwapchainImageCountReplacementCandidate>
            applicationMinimumSignatures;
    };

    enum class SwapchainImageCountCompatibilityObservation : uint32_t {
        Ignored,
        ZeroReturnProbe,
        ShortLivedReplacementCandidate,
        HealthySurfaceCleared,
    };

    [[nodiscard]] inline SwapchainImageCountCompatibilityObservation
    observeDestroyedSwapchainForImageCountCompatibility(
            SwapchainImageCountCompatibilityState& state,
            const uint32_t returnedPresentCount,
            const uint32_t requestedMinImages,
            const uint32_t provisionedMinImages,
            const VkExtent2D applicationExtent,
            const uint64_t activeLifetimeMs) {
        const auto signatureMatches = [&](const auto& signature) {
            return signature.requestedMinImages == requestedMinImages &&
                signature.provisionedMinImages == provisionedMinImages &&
                signature.applicationExtent.width == applicationExtent.width &&
                signature.applicationExtent.height == applicationExtent.height;
        };
        if (std::ranges::any_of(
                state.applicationMinimumSignatures, signatureMatches)) {
            return SwapchainImageCountCompatibilityObservation::Ignored;
        }
        if (requestedMinImages >= provisionedMinImages) {
            state.zeroReturnProbeObserved = false;
            state.replacementCandidate.reset();
            return SwapchainImageCountCompatibilityObservation::Ignored;
        }

        if (returnedPresentCount == 0) {
            if (activeLifetimeMs >
                    swapchainImageCountZeroReturnProbeLifetimeLimitMs) {
                const bool clearedStartupEvidence =
                    state.zeroReturnProbeObserved ||
                    state.replacementCandidate.has_value();
                state.zeroReturnProbeObserved = false;
                state.replacementCandidate.reset();
                return clearedStartupEvidence
                    ? SwapchainImageCountCompatibilityObservation::
                        HealthySurfaceCleared
                    : SwapchainImageCountCompatibilityObservation::Ignored;
            }
            if (state.zeroReturnProbeObserved) {
                state.replacementCandidate =
                    SwapchainImageCountReplacementCandidate{
                        .applicationExtent = applicationExtent,
                        .requestedMinImages = requestedMinImages,
                        .provisionedMinImages = provisionedMinImages,
                        .returnedPresentCount = 0,
                        .activeLifetimeMs = 0,
                    };
                return SwapchainImageCountCompatibilityObservation::
                    ShortLivedReplacementCandidate;
            }
            state.zeroReturnProbeObserved = true;
            state.replacementCandidate.reset();
            return SwapchainImageCountCompatibilityObservation::
                ZeroReturnProbe;
        }

        const bool shortLivedMultiPresent =
            returnedPresentCount >=
                swapchainImageCountShortLivedReturnedPresentMinimum &&
            returnedPresentCount <=
                swapchainImageCountShortLivedReturnedPresentMaximum &&
            activeLifetimeMs <=
                swapchainImageCountReplacementLifetimeLimitMs;
        if (state.zeroReturnProbeObserved && shortLivedMultiPresent) {
            state.replacementCandidate =
                SwapchainImageCountReplacementCandidate{
                    .applicationExtent = applicationExtent,
                    .requestedMinImages = requestedMinImages,
                    .provisionedMinImages = provisionedMinImages,
                    .returnedPresentCount = std::min(
                        returnedPresentCount,
                        swapchainImageCountShortLivedReturnedPresentMinimum
                    ),
                    .activeLifetimeMs = activeLifetimeMs,
                };
            return SwapchainImageCountCompatibilityObservation::
                ShortLivedReplacementCandidate;
        }

        const bool clearedStartupEvidence = state.zeroReturnProbeObserved ||
            state.replacementCandidate.has_value();
        state.zeroReturnProbeObserved = false;
        state.replacementCandidate.reset();
        return clearedStartupEvidence
            ? SwapchainImageCountCompatibilityObservation::
                HealthySurfaceCleared
            : SwapchainImageCountCompatibilityObservation::Ignored;
    }

    [[nodiscard]] inline bool
    activateSwapchainImageCountCompatibilityForCreate(
            SwapchainImageCountCompatibilityState& state,
            const uint32_t requestedMinImages,
            const uint32_t provisionedMinImages,
            const VkExtent2D applicationExtent) {
        if (requestedMinImages >= provisionedMinImages)
            return false;
        const auto signatureMatches = [&](const auto& signature) {
            return signature.requestedMinImages == requestedMinImages &&
                signature.provisionedMinImages == provisionedMinImages &&
                signature.applicationExtent.width == applicationExtent.width &&
                signature.applicationExtent.height == applicationExtent.height;
        };
        if (std::ranges::any_of(
                state.applicationMinimumSignatures, signatureMatches)) {
            return true;
        }
        if (!state.replacementCandidate)
            return false;

        const auto candidate = *state.replacementCandidate;
        state.zeroReturnProbeObserved = false;
        state.replacementCandidate.reset();
        const bool matches = signatureMatches(candidate);
        if (!matches)
            return false;

        state.applicationMinimumSignatures.push_back(candidate);
        return true;
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
                // Preserve the normal healthy transport contract: the
                // application's own in-flight requirement remains intact,
                // while one real image plus the largest generated batch is
                // reserved for MAKO's ordered output. A lower-create failure
                // or a zero-return probe followed by a matching short-lived
                // replacement can select the unmodified application minimum.
                // Healthy swapchains retain the established queue depth and
                // cadence.
                createInfo.minImageCount +=
                    generatedFrameCapacityForProfile(profile) + 1;
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
