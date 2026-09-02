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
        VkFormat format{VK_FORMAT_UNDEFINED};
        VkColorSpaceKHR colorSpace{VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
        VkImageUsageFlags imageUsage{0};
        VkPresentModeKHR presentMode{VK_PRESENT_MODE_FIFO_KHR};
        // Set only when a matching create activates the workaround. Candidate
        // evidence may cross a surface handoff, but the resulting fallback
        // must remain local to the surface that actually proved it.
        VkSurfaceKHR establishedSurface{VK_NULL_HANDLE};
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
            const uint64_t activeLifetimeMs,
            const VkFormat format = VK_FORMAT_UNDEFINED,
            const VkColorSpaceKHR colorSpace =
                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            const VkImageUsageFlags imageUsage = 0,
            const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR,
            const VkSurfaceKHR surface = VK_NULL_HANDLE) {
        const auto signatureMatches = [&](const auto& signature) {
            return signature.requestedMinImages == requestedMinImages &&
                signature.provisionedMinImages == provisionedMinImages &&
                signature.applicationExtent.width == applicationExtent.width &&
                signature.applicationExtent.height == applicationExtent.height &&
                signature.format == format &&
                signature.colorSpace == colorSpace &&
                signature.imageUsage == imageUsage &&
                signature.presentMode == presentMode &&
                signature.establishedSurface == surface;
        };
        const auto established = std::ranges::find_if(
            state.applicationMinimumSignatures, signatureMatches
        );
        if (established != state.applicationMinimumSignatures.end())
            return SwapchainImageCountCompatibilityObservation::Ignored;
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
                        .format = format,
                        .colorSpace = colorSpace,
                        .imageUsage = imageUsage,
                        .presentMode = presentMode,
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
                    .format = format,
                    .colorSpace = colorSpace,
                    .imageUsage = imageUsage,
                    .presentMode = presentMode,
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
            const VkExtent2D applicationExtent,
            const VkFormat format = VK_FORMAT_UNDEFINED,
            const VkColorSpaceKHR colorSpace =
                VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
            const VkImageUsageFlags imageUsage = 0,
            const VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR,
            const VkSurfaceKHR surface = VK_NULL_HANDLE) {
        if (requestedMinImages >= provisionedMinImages)
            return false;
        const auto createSignatureMatches = [&](const auto& signature) {
            return signature.requestedMinImages == requestedMinImages &&
                signature.provisionedMinImages == provisionedMinImages &&
                signature.applicationExtent.width == applicationExtent.width &&
                signature.applicationExtent.height == applicationExtent.height &&
                signature.format == format &&
                signature.colorSpace == colorSpace &&
                signature.imageUsage == imageUsage &&
                signature.presentMode == presentMode;
        };
        if (std::ranges::any_of(
                state.applicationMinimumSignatures,
                [&](const auto& signature) {
                    return createSignatureMatches(signature) &&
                        signature.establishedSurface == surface;
                })) {
            return true;
        }
        if (!state.replacementCandidate)
            return false;

        auto candidate = *state.replacementCandidate;
        state.zeroReturnProbeObserved = false;
        state.replacementCandidate.reset();
        const bool matches = createSignatureMatches(candidate);
        if (!matches)
            return false;

        candidate.establishedSurface = surface;
        state.applicationMinimumSignatures.push_back(candidate);
        return true;
    }

    inline void clearSwapchainImageCountCompatibilityForSurface(
            SwapchainImageCountCompatibilityState& state,
            const VkSurfaceKHR surface) {
        std::erase_if(
            state.applicationMinimumSignatures,
            [surface](const auto& signature) {
                return signature.establishedSurface == surface;
            }
        );
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

    /// Return the largest generated batch that the already-created WSI
    /// swapchain can retain beside MAKO's ordered real-image slot. The
    /// application's ownership requirement is already represented by the
    /// pool returned for max(application minimum, generated capacity + 1);
    /// adding it again here would revive the same overprovisioning model at
    /// the live-growth boundary. A driver returning fewer images than the
    /// application requested is malformed and must fail closed.
    [[nodiscard]] inline size_t
    generatedFrameCapacitySupportedByExistingSwapchain(
            const uint32_t requestedMinImages,
            const size_t returnedImages) noexcept {
        if (returnedImages < requestedMinImages || returnedImages == 0)
            return 0;
        return returnedImages - 1;
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
                // The application and MAKO consume the same WSI pool at
                // different points in the ordered present sequence. Reserve
                // whichever minimum is larger instead of adding both
                // requirements: additive provisioning can turn a normal
                // three-image game swapchain into six images and break
                // engines with fixed image-count assumptions. Acquire
                // recovery retains the real frame if temporary application
                // ownership leaves insufficient generated-image headroom.
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
