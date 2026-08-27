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
        bool nativePassthrough{false};
        float factor{1.0F};
    };

    /// One coherent policy snapshot used for a fixed-surface capability query.
    /// The revision changes only when scaler activity, Native passthrough,
    /// process support, or factor changes. Sharpness and changes between active
    /// scaler methods can safely reuse the same advertised extent contract.
    struct SpatialScalingPolicySnapshot {
        SpatialScalingPolicy policy{};
        bool processSupported{false};
        uint64_t revision{0};
    };

    /// Result of virtualizing one fixed-surface capability query. Entrypoint
    /// code adds the surface-scoped query generation only after its queue and
    /// format preflight succeeds and the virtual capabilities are returned to
    /// the application.
    struct SpatialScalingCapabilitySelection {
        SpatialScalingExtents extents{};
        float factor{1.0F};
        uint64_t policyRevision{0};
    };

    /// Exact fixed-surface contract observed by the application. Swapchain
    /// creation consumes this record instead of recomputing a source extent
    /// from capabilities that may have changed since the query.
    struct FixedSurfaceScalingContract {
        SpatialScalingExtents extents{};
        float factor{1.0F};
        uint64_t policyRevision{0};
        uint64_t queryGeneration{0};
    };

    enum class SpatialScalingInactiveReason {
        None,
        NativePassthrough,
        ProcessUnsupported,
        InvalidFactor,
        FactorNotUpscaling,
        NoFixedCapabilityContract,
        PolicyChangedAfterCapabilityQuery,
        SurfaceChangedAfterCapabilityQuery,
        ApplicationExtentOverrideNoSplit,
        ApplicationExtentMismatch,
        VariableSurfaceFeedback,
        VariableSurfaceNoHeadroom,
        SwapchainShapeUnsupported,
        SwapchainFormatUnsupported,
        QueuePresentationUnsupported,
        QueueCommandsUnsupported,
    };

    [[nodiscard]] constexpr const char* spatialScalingInactiveReasonName(
            const SpatialScalingInactiveReason reason) noexcept {
        switch (reason) {
            case SpatialScalingInactiveReason::None:
                return "none";
            case SpatialScalingInactiveReason::NativePassthrough:
                return "native-passthrough";
            case SpatialScalingInactiveReason::ProcessUnsupported:
                return "process-unsupported";
            case SpatialScalingInactiveReason::InvalidFactor:
                return "invalid-factor";
            case SpatialScalingInactiveReason::FactorNotUpscaling:
                return "factor-not-upscaling";
            case SpatialScalingInactiveReason::NoFixedCapabilityContract:
                return "no-fixed-capability-contract";
            case SpatialScalingInactiveReason::PolicyChangedAfterCapabilityQuery:
                return "policy-changed-after-capability-query";
            case SpatialScalingInactiveReason::SurfaceChangedAfterCapabilityQuery:
                return "surface-changed-after-capability-query";
            case SpatialScalingInactiveReason::ApplicationExtentOverrideNoSplit:
                return "application-extent-override-no-source-presentation-split";
            case SpatialScalingInactiveReason::ApplicationExtentMismatch:
                return "application-extent-mismatch";
            case SpatialScalingInactiveReason::VariableSurfaceFeedback:
                return "variable-surface-feedback";
            case SpatialScalingInactiveReason::VariableSurfaceNoHeadroom:
                return "variable-surface-no-headroom";
            case SpatialScalingInactiveReason::SwapchainShapeUnsupported:
                return "swapchain-shape-unsupported";
            case SpatialScalingInactiveReason::SwapchainFormatUnsupported:
                return "swapchain-format-unsupported";
            case SpatialScalingInactiveReason::QueuePresentationUnsupported:
                return "queue-presentation-unsupported";
            case SpatialScalingInactiveReason::QueueCommandsUnsupported:
                return "queue-commands-unsupported";
        }
        return "unknown";
    }

    struct SpatialScalingCreateDecision {
        std::optional<SpatialScalingExtents> extents;
        std::optional<FixedSurfaceScalingContract> fixedContract;
        SpatialScalingInactiveReason inactiveReason{
            SpatialScalingInactiveReason::None
        };
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
                .enabled = ls::spatialScalingRequested(profile),
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
                .enabled = ls::spatialScalingRequested(profile),
                .factor = profile.scaling_factor,
            },
            capabilities
        );
    }

    /// Match a create request against the source extent that MAKO advertised.
    /// An application-selected override is left alone rather than silently
    /// changing its requested rendering size.
    [[nodiscard]] inline SpatialScalingCreateDecision
    scalingDecisionForCreate(
            const SpatialScalingPolicy policy,
            const bool processSupported,
            const uint64_t policyRevision,
            const VkSurfaceCapabilitiesKHR& realCapabilities,
            const VkExtent2D requestedExtent,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents = std::nullopt,
            const std::optional<FixedSurfaceScalingContract>&
                fixedContract = std::nullopt) noexcept {
        SpatialScalingCreateDecision decision{
            .fixedContract = fixedContract,
        };
        if (policy.nativePassthrough) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::NativePassthrough;
            return decision;
        }
        if (!processSupported) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::ProcessUnsupported;
            return decision;
        }
        if (!validSpatialScalingFactor(policy.factor)) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::InvalidFactor;
            return decision;
        }
        if (!policy.enabled || policy.factor <= 1.0F) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::FactorNotUpscaling;
            return decision;
        }

        if (fixedSurfaceExtent(realCapabilities.currentExtent)) {
            if (!fixedContract) {
                decision.inactiveReason =
                    SpatialScalingInactiveReason::NoFixedCapabilityContract;
                return decision;
            }
            if (fixedContract->policyRevision != policyRevision ||
                    fixedContract->factor != policy.factor) {
                decision.inactiveReason = SpatialScalingInactiveReason::
                    PolicyChangedAfterCapabilityQuery;
                return decision;
            }
            if (!sameExtent(
                    fixedContract->extents.presentation,
                    realCapabilities.currentExtent)) {
                decision.inactiveReason = SpatialScalingInactiveReason::
                    SurfaceChangedAfterCapabilityQuery;
                return decision;
            }
            if (!sameExtent(fixedContract->extents.source, requestedExtent)) {
                decision.inactiveReason = sameExtent(
                    requestedExtent, realCapabilities.currentExtent
                ) ? SpatialScalingInactiveReason::ApplicationExtentOverrideNoSplit
                  : SpatialScalingInactiveReason::ApplicationExtentMismatch;
                return decision;
            }
            decision.extents = fixedContract->extents;
            return decision;
        }

        // Variable-extent window systems cannot expose a compositor-owned
        // native size through currentExtent. Use MAKO's explicit factor
        // contract instead: retain the application's requested render extent
        // and enlarge the lower WSI image by the configured factor.
        decision.fixedContract.reset();
        if (requestedExtent.width == 0 || requestedExtent.height == 0) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::VariableSurfaceNoHeadroom;
            return decision;
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
            decision.inactiveReason =
                SpatialScalingInactiveReason::VariableSurfaceFeedback;
            return decision;
        }

        const double maximumWidthFactor =
            static_cast<double>(realCapabilities.maxImageExtent.width) /
            static_cast<double>(requestedExtent.width);
        const double maximumHeightFactor =
            static_cast<double>(realCapabilities.maxImageExtent.height) /
            static_cast<double>(requestedExtent.height);
        const double effectiveFactor = std::min({
            static_cast<double>(policy.factor),
            maximumWidthFactor,
            maximumHeightFactor,
        });
        if (effectiveFactor <= 1.0) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::VariableSurfaceNoHeadroom;
            return decision;
        }

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
                presentation.height <= requestedExtent.height) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::VariableSurfaceNoHeadroom;
            return decision;
        }

        decision.extents = SpatialScalingExtents{
            .source = requestedExtent,
            .presentation = presentation,
        };
        return decision;
    }

    [[nodiscard]] inline SpatialScalingCreateDecision
    scalingDecisionForCreate(
            const ls::GameConf& profile,
            const bool processSupported,
            const uint64_t policyRevision,
            const VkSurfaceCapabilitiesKHR& realCapabilities,
            const VkExtent2D requestedExtent,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents = std::nullopt,
            const std::optional<FixedSurfaceScalingContract>&
                fixedContract = std::nullopt) noexcept {
        return scalingDecisionForCreate(
            SpatialScalingPolicy{
                .enabled = ls::spatialScalingRequested(profile),
                .nativePassthrough = profile.scaling_enabled &&
                    profile.scaling_method == ls::ScalingMethod::Native,
                .factor = profile.scaling_factor,
            },
            processSupported,
            policyRevision,
            realCapabilities,
            requestedExtent,
            previousVariableExtents,
            fixedContract
        );
    }

    /// Compatibility wrapper for callers that only need variable-surface
    /// policy. Fixed-surface activation intentionally requires an explicit
    /// capability contract and therefore returns no extent here.
    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    scalingExtentsForCreate(
            const ls::GameConf& profile,
            const VkSurfaceCapabilitiesKHR& realCapabilities,
            const VkExtent2D requestedExtent,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents = std::nullopt) noexcept {
        return scalingDecisionForCreate(
            profile, true, 0, realCapabilities, requestedExtent,
            previousVariableExtents
        ).extents;
    }

    [[nodiscard]] inline bool variableSurfaceScalingFeedbackDetected(
            const ls::GameConf& profile,
            const VkSurfaceCapabilitiesKHR& realCapabilities,
            const VkExtent2D requestedExtent,
            const std::optional<SpatialScalingExtents>&
                previousVariableExtents) noexcept {
        return ls::spatialScalingRequested(profile) &&
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
