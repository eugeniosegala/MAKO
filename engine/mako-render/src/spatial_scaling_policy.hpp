/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Source and presentation extents selected for one spatially-scaled
    /// swapchain. The application renders only the source rectangle; the
    /// lower WSI image retains the presentation extent.
    struct SpatialScalingExtents {
        VkExtent2D source{};
        VkExtent2D presentation{};
    };

    /// Immutable subset needed by instance-level fixed-surface queries. Root
    /// publishes this as one coherent snapshot so Vulkan capability queries
    /// never race the mutable configuration/profile state.
    struct SpatialScalingPolicy {
        bool enabled{false};
        float factor{1.0F};
    };

    [[nodiscard]] constexpr bool spatialScalingProcessSupported(
            const bool gamescopeEnvironmentHint,
            const bool gamescopeFeedbackDetected,
            const bool hdrExposureDisabled) noexcept {
        return !(gamescopeEnvironmentHint || gamescopeFeedbackDetected) ||
            hdrExposureDisabled;
    }

    /// A scaled swapchain is admitted only on the immutable SDR boundary.
    /// Gamescope feedback may continue to change for other swapchains, but it
    /// must never reclassify an existing scaler into an HDR colour pipeline.
    [[nodiscard]] constexpr bool liveGamescopeHdrReclassificationAllowed(
            const bool spatialScalingActive) noexcept {
        return !spatialScalingActive;
    }

    [[nodiscard]] constexpr bool sameExtent(
            const VkExtent2D left, const VkExtent2D right) noexcept {
        return left.width == right.width && left.height == right.height;
    }

    [[nodiscard]] constexpr bool fixedSurfaceExtent(
            const VkExtent2D extent) noexcept {
        return extent.width != UINT32_MAX && extent.height != UINT32_MAX &&
            extent.width != 0 && extent.height != 0;
    }

    /// Capability requirements that are knowable before the application
    /// chooses its swapchain shape. Opaque composition is part of the initial
    /// scaler contract, so do not advertise a virtual source extent on a
    /// surface that cannot satisfy it.
    [[nodiscard]] constexpr bool spatialScalingSurfaceCapabilitiesSupported(
            const VkSurfaceCapabilitiesKHR& capabilities) noexcept {
        constexpr VkImageUsageFlags transferUsage =
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return (capabilities.supportedUsageFlags & transferUsage) ==
                transferUsage &&
            (capabilities.supportedCompositeAlpha &
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) != 0;
    }

    [[nodiscard]] inline bool validSpatialScalingFactor(
            const float factor) noexcept {
        return std::isfinite(factor) &&
            factor >= ls::GameConfLimits::minimumScalingFactor &&
            factor <= ls::GameConfLimits::maximumScalingFactor;
    }

    /// The initial scaler contract is a conventional, unprotected,
    /// single-layer WSI image. Shared-present swapchains have different image
    /// layouts, usage capabilities, and acquire semantics and therefore fail
    /// closed until they receive a dedicated implementation.
    [[nodiscard]] constexpr bool spatialScalingSwapchainShapeSupported(
            const VkSwapchainCreateInfoKHR& createInfo) noexcept {
        const bool sharedPresentMode =
            createInfo.presentMode ==
                VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR ||
            createInfo.presentMode ==
                VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
        constexpr VkSwapchainCreateFlagsKHR unsupportedFlags =
            VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR |
            VK_SWAPCHAIN_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR;
        return createInfo.imageArrayLayers == 1 &&
            (createInfo.flags & unsupportedFlags) == 0 &&
            !sharedPresentMode &&
            createInfo.compositeAlpha == VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    [[nodiscard]] inline uint32_t scaledSourceDimension(
            const uint32_t presentation, const float factor) noexcept {
        const auto divided = static_cast<uint32_t>(std::max(
            1.0, std::floor(static_cast<double>(presentation) / factor)
        ));
        // Even source dimensions avoid half-pixel asymmetry in the
        // frame-generation and spatial-scaling workgroups. Preserve a valid
        // one-pixel extent for pathological test surfaces.
        return divided > 1 ? divided & ~uint32_t{1} : divided;
    }

    /// Resolve a fixed-size WSI surface into the low-resolution application
    /// extent and native presentation extent. Variable-size desktop surfaces
    /// remain untouched until a compositor-owned output extent is available.
    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    selectSpatialScalingExtents(
            const SpatialScalingPolicy policy,
            const VkSurfaceCapabilitiesKHR& capabilities) noexcept {
        if (!policy.enabled ||
                !validSpatialScalingFactor(policy.factor) ||
                policy.factor <= 1.0F ||
                !fixedSurfaceExtent(capabilities.currentExtent)) {
            return std::nullopt;
        }

        const VkExtent2D source{
            .width = scaledSourceDimension(
                capabilities.currentExtent.width, policy.factor
            ),
            .height = scaledSourceDimension(
                capabilities.currentExtent.height, policy.factor
            ),
        };
        if (sameExtent(source, capabilities.currentExtent))
            return std::nullopt;

        return SpatialScalingExtents{
            .source = source,
            .presentation = capabilities.currentExtent,
        };
    }

    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    selectSpatialScalingExtents(
            const ls::GameConf& profile,
            const VkSurfaceCapabilitiesKHR& capabilities) noexcept {
        return selectSpatialScalingExtents(
            SpatialScalingPolicy{
                .enabled = profile.scaling_enabled,
                .factor = profile.scaling_factor,
            },
            capabilities
        );
    }

    /// Advertise the source extent while retaining a self-consistent extent
    /// range. The unmodified capabilities are queried again at swapchain
    /// creation and remain authoritative for the lower WSI object.
    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    virtualizeSurfaceCapabilities(
            const SpatialScalingPolicy policy,
            VkSurfaceCapabilitiesKHR& capabilities) noexcept {
        const auto extents = selectSpatialScalingExtents(
            policy, capabilities
        );
        if (!extents)
            return std::nullopt;

        capabilities.currentExtent = extents->source;
        capabilities.minImageExtent.width = std::min(
            capabilities.minImageExtent.width, extents->source.width
        );
        capabilities.minImageExtent.height = std::min(
            capabilities.minImageExtent.height, extents->source.height
        );
        capabilities.maxImageExtent.width = std::max(
            capabilities.maxImageExtent.width, extents->source.width
        );
        capabilities.maxImageExtent.height = std::max(
            capabilities.maxImageExtent.height, extents->source.height
        );
        // Keep the advertised fixed-surface contract aligned with the shape
        // accepted at swapchain creation. Otherwise a conforming application
        // could select an advertised multi-layer or non-opaque shape after it
        // has already observed MAKO's virtual source extent, leaving no safe
        // native fallback for the lower presentation-sized WSI image.
        capabilities.maxImageArrayLayers = 1;
        capabilities.supportedCompositeAlpha =
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        return extents;
    }


    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    virtualizeSurfaceCapabilities(
            const ls::GameConf& profile,
            VkSurfaceCapabilitiesKHR& capabilities) noexcept {
        return virtualizeSurfaceCapabilities(
            SpatialScalingPolicy{
                .enabled = profile.scaling_enabled,
                .factor = profile.scaling_factor,
            },
            capabilities
        );
    }

    /// Match a create request against the source extent that MAKO advertised.
    /// An application-selected override is left alone rather than silently
    /// changing its requested rendering size.
    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    scalingExtentsForCreate(
            const ls::GameConf& profile,
            const VkSurfaceCapabilitiesKHR& realCapabilities,
            const VkExtent2D requestedExtent,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents = std::nullopt) noexcept {
        const auto extents = selectSpatialScalingExtents(
            profile, realCapabilities
        );
        if (extents) {
            if (!sameExtent(extents->source, requestedExtent))
                return std::nullopt;
            return extents;
        }

        // Variable-extent window systems cannot expose a compositor-owned
        // native size through currentExtent. Use MAKO's explicit factor
        // contract instead: retain the application's requested render extent
        // and enlarge the lower WSI image by the configured factor.
        if (!profile.scaling_enabled ||
                !validSpatialScalingFactor(profile.scaling_factor) ||
                profile.scaling_factor <= 1.0F ||
                fixedSurfaceExtent(realCapabilities.currentExtent) ||
                requestedExtent.width == 0 || requestedExtent.height == 0) {
            return std::nullopt;
        }

        // Some variable Wayland surfaces echo the enlarged lower WSI extent
        // back to the application as its next logical size. Treat an exact
        // previous presentation extent as compositor feedback, not a new
        // source to enlarge again. This safely falls back to native rendering
        // for the recreated swapchain instead of compounding the factor until
        // the surface maximum is reached.
        if (previousVariableExtents &&
                sameExtent(
                    requestedExtent,
                    previousVariableExtents->presentation
                ) &&
                !sameExtent(
                    previousVariableExtents->source,
                    previousVariableExtents->presentation
                )) {
            return std::nullopt;
        }

        const double maximumWidthFactor =
            static_cast<double>(realCapabilities.maxImageExtent.width) /
            static_cast<double>(requestedExtent.width);
        const double maximumHeightFactor =
            static_cast<double>(realCapabilities.maxImageExtent.height) /
            static_cast<double>(requestedExtent.height);
        const double effectiveFactor = std::min({
            static_cast<double>(profile.scaling_factor),
            maximumWidthFactor,
            maximumHeightFactor,
        });
        if (effectiveFactor <= 1.0)
            return std::nullopt;

        VkExtent2D presentation{
            .width = static_cast<uint32_t>(std::floor(
                static_cast<double>(requestedExtent.width) * effectiveFactor
            )),
            .height = static_cast<uint32_t>(std::floor(
                static_cast<double>(requestedExtent.height) * effectiveFactor
            )),
        };
        if (presentation.width > 1)
            presentation.width &= ~uint32_t{1};
        if (presentation.height > 1)
            presentation.height &= ~uint32_t{1};
        if (presentation.width <= requestedExtent.width ||
                presentation.height <= requestedExtent.height)
            return std::nullopt;

        return SpatialScalingExtents{
            .source = requestedExtent,
            .presentation = presentation,
        };
    }

    [[nodiscard]] inline bool variableSurfaceScalingFeedbackDetected(
            const ls::GameConf& profile,
            const VkSurfaceCapabilitiesKHR& realCapabilities,
            const VkExtent2D requestedExtent,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents) noexcept {
        return profile.scaling_enabled &&
            validSpatialScalingFactor(profile.scaling_factor) &&
            profile.scaling_factor > 1.0F &&
            !fixedSurfaceExtent(realCapabilities.currentExtent) &&
            previousVariableExtents &&
            sameExtent(
                requestedExtent, previousVariableExtents->presentation
            ) &&
            !sameExtent(
                previousVariableExtents->source,
                previousVariableExtents->presentation
            );
    }

    /// Commit the surface-scoped feedback state only after the replacement
    /// swapchain and its MAKO context have both been created successfully.
    /// A compositor echo retains the previous pair so repeated recreations
    /// cannot compound the factor; every other native or fixed-surface result
    /// clears stale variable state.
    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    committedVariableSurfaceScalingExtents(
            const std::optional<SpatialScalingExtents>& previous,
            const bool profileActive,
            const bool variableSurface,
            const bool spatialScalingActive,
            const bool feedbackSuppressed,
            const VkExtent2D applicationExtent,
            const VkExtent2D presentationExtent) noexcept {
        if (!profileActive || !variableSurface)
            return std::nullopt;
        if (spatialScalingActive) {
            return SpatialScalingExtents{
                .source = applicationExtent,
                .presentation = presentationExtent,
            };
        }
        if (feedbackSuppressed)
            return previous;
        return std::nullopt;
    }

}
