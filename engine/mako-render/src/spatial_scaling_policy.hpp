/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

#include <vulkan/vulkan_core.h>

namespace mako::layer {

    /// Source and presentation extents selected for one spatially-scaled
    /// swapchain. The application renders only the source rectangle; the
    /// lower WSI image retains the presentation extent.
    struct SpatialScalingExtents {
        VkExtent2D source{};
        VkExtent2D presentation{};
    };

    /// Immutable ordering selected for a combined spatial-scaling and
    /// frame-generation swapchain. Deck/1080p-class presentation extents pay
    /// for reconstruction once before interpolation. Larger outputs retain
    /// source-resolution interpolation resources and reconstruct each
    /// delivered image immediately before WSI presentation.
    enum class SpatialFramePipelinePlacement {
        PreFrameGeneration,
        PostFrameGeneration,
    };

    /// Keep the placement decision aspect-ratio independent. The boundary is
    /// a presentation-pixel budget equivalent to 1920x1200, not a rectangular
    /// width/height clamp.
    inline constexpr uint64_t preFrameGenerationPresentationPixelBudget =
        uint64_t{1920} * 1200;

    [[nodiscard]] constexpr uint64_t extentPixelCount(
            const VkExtent2D extent) noexcept {
        return static_cast<uint64_t>(extent.width) * extent.height;
    }

    [[nodiscard]] constexpr SpatialFramePipelinePlacement
    selectSpatialFramePipelinePlacement(
            const VkExtent2D source,
            const VkExtent2D presentation) noexcept {
        if (source.width == 0 || source.height == 0 ||
                presentation.width == 0 || presentation.height == 0 ||
                extentPixelCount(presentation) <=
                    preFrameGenerationPresentationPixelBudget) {
            return SpatialFramePipelinePlacement::PreFrameGeneration;
        }
        return SpatialFramePipelinePlacement::PostFrameGeneration;
    }

    [[nodiscard]] constexpr VkExtent2D frameGenerationExtent(
            const SpatialFramePipelinePlacement placement,
            const VkExtent2D source,
            const VkExtent2D presentation) noexcept {
        return placement == SpatialFramePipelinePlacement::PreFrameGeneration
            ? presentation : source;
    }

    /// Post-FG reconstruction is recorded into the generated-image command
    /// buffers guarded by the main render fence. Replacing its private scaler
    /// therefore requires that fence in addition to the per-WSI-image spatial
    /// fences. Pre-FG reconstruction is owned entirely by the latter.
    [[nodiscard]] constexpr bool
    spatialScalerTransitionRequiresGeneratedRenderDrain(
            const SpatialFramePipelinePlacement placement) noexcept {
        return placement ==
            SpatialFramePipelinePlacement::PostFrameGeneration;
    }

    /// The pre-FG spatial graph may write reconstruction straight into the
    /// exported FG source. Post-FG reconstruction and FG-only swapchains keep
    /// the narrower transfer/sampled contract.
    [[nodiscard]] constexpr VkImageUsageFlags frameGenerationSourceImageUsage(
            const SpatialFramePipelinePlacement placement,
            const bool directSpatialOutputEnabled) noexcept {
        const auto baseUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT;
        return directSpatialOutputEnabled &&
                placement == SpatialFramePipelinePlacement::PreFrameGeneration
            ? baseUsage | VK_IMAGE_USAGE_STORAGE_BIT |
                VK_IMAGE_USAGE_TRANSFER_SRC_BIT
            : baseUsage;
    }

    [[nodiscard]] constexpr bool directSpatialFrameGenerationOutputEligible(
            const SpatialFramePipelinePlacement placement,
            const bool spatialScalingActive,
            const size_t sourceImageCount) noexcept {
        return spatialScalingActive &&
            placement == SpatialFramePipelinePlacement::PreFrameGeneration &&
            sourceImageCount == 2;
    }

    [[nodiscard]] constexpr const char* spatialFramePipelinePlacementName(
            const SpatialFramePipelinePlacement placement) noexcept {
        return placement == SpatialFramePipelinePlacement::PreFrameGeneration
            ? "pre-frame-generation" : "post-frame-generation";
    }

    [[nodiscard]] constexpr const char* spatialFramePipelinePlacementReason(
            const SpatialFramePipelinePlacement placement) noexcept {
        return placement == SpatialFramePipelinePlacement::PreFrameGeneration
            ? "presentation-within-low-resolution-budget"
            : "source-resolution-frame-generation-saves-high-resolution-work";
    }

    /// Immutable subset needed by instance-level fixed-surface queries. Root
    /// publishes this as one coherent snapshot so Vulkan capability queries
    /// never race the mutable configuration/profile state.
    struct SpatialScalingPolicy {
        bool enabled{false};
        float factor{1.0F};
        bool supersampling{false};
    };

    /// One coherent policy snapshot used for a fixed-surface capability query.
    /// The revision changes only when scaler activity, process support, or the
    /// factor changes. Native Resolution and changes between active scaler
    /// methods safely reuse the same advertised extent contract.
    struct SpatialScalingPolicySnapshot {
        SpatialScalingPolicy policy{};
        bool processSupported{false};
        uint64_t revision{0};
    };

    /// Window-system provenance observed by one layer DSO. In a managed split
    /// chain the lower spatial role must see the Wayland surface created by
    /// Gamescope WSI; an X11 surface at that boundary means WSI passed the
    /// application's physical window through and source/presentation geometry
    /// cannot be separated safely. The direct combined Renderer does not
    /// require this proof because it owns the application's surface itself.
    enum class SpatialSurfaceOrigin {
        Unknown,
        Wayland,
        Xcb,
        Xlib,
    };

    [[nodiscard]] constexpr const char* spatialSurfaceOriginName(
            const SpatialSurfaceOrigin origin) noexcept {
        switch (origin) {
            case SpatialSurfaceOrigin::Unknown:
                return "unknown";
            case SpatialSurfaceOrigin::Wayland:
                return "wayland";
            case SpatialSurfaceOrigin::Xcb:
                return "xcb";
            case SpatialSurfaceOrigin::Xlib:
                return "xlib";
        }
        return "unknown";
    }

    [[nodiscard]] constexpr bool spatialSplitSurfaceScalingSupported(
            const bool lowerSplitSurfaceProofRequired,
            const SpatialSurfaceOrigin origin) noexcept {
        return !lowerSplitSurfaceProofRequired ||
            origin == SpatialSurfaceOrigin::Wayland;
    }

    /// Result of virtualizing one fixed-surface capability query. Entrypoint
    /// code adds the surface-scoped query generation only after its queue and
    /// format preflight succeeds and the virtual capabilities are returned to
    /// the application.
    struct SpatialScalingCapabilitySelection {
        SpatialScalingExtents extents{};
        float factor{1.0F};
        uint64_t policyRevision{0};
    };

    enum class SpatialScalingInactiveReason {
        None,
        ProcessUnsupported,
        InvalidFactor,
        FactorNotUpscaling,
        NoFixedCapabilityContract,
        PolicyChangedAfterCapabilityQuery,
        SurfaceChangedAfterCapabilityQuery,
        ApplicationExtentOverrideNoSplit,
        ApplicationExtentMismatch,
        GamescopeWsiSurfaceUnproven,
        GamescopePresentationTargetUnavailable,
        GamescopePresentationTargetNoHeadroom,
        VariableSurfaceFeedback,
        VariableSurfaceNoHeadroom,
        VariableSurfaceMemoryBudget,
        SwapchainShapeUnsupported,
        SwapchainFormatUnsupported,
        QueuePresentationUnsupported,
        QueueCommandsUnsupported,
    };

    /// Exact fixed-surface contract observed by the application. Swapchain
    /// creation consumes this record instead of recomputing a source extent
    /// from capabilities that may have changed since the query. The same
    /// one-shot record carries a lower split role's create-time surface class
    /// and exact native decision back to the upper resource owner.
    struct FixedSurfaceScalingContract {
        SpatialScalingExtents extents{};
        float factor{1.0F};
        uint64_t policyRevision{0};
        uint64_t queryGeneration{0};
        bool spatialSurfaceScalingSupported{true};
        bool variableSurface{false};
        bool memoryBudgetConstrained{false};
        SpatialScalingInactiveReason inactiveReason{
            SpatialScalingInactiveReason::None
        };
    };

    struct FixedSurfaceScalingContractRelayMetadata {
        VkBool32 spatialSurfaceScalingSupported{VK_TRUE};
        VkBool32 variableSurface{VK_FALSE};
        VkBool32 memoryBudgetConstrained{VK_FALSE};
        uint32_t inactiveReason{
            static_cast<uint32_t>(SpatialScalingInactiveReason::None)
        };
    };

    [[nodiscard]] constexpr FixedSurfaceScalingContractRelayMetadata
    fixedSurfaceScalingContractRelayMetadata(
            const FixedSurfaceScalingContract& contract) noexcept {
        return {
            .spatialSurfaceScalingSupported =
                contract.spatialSurfaceScalingSupported ? VK_TRUE : VK_FALSE,
            .variableSurface = contract.variableSurface ? VK_TRUE : VK_FALSE,
            .memoryBudgetConstrained = contract.memoryBudgetConstrained
                ? VK_TRUE : VK_FALSE,
            .inactiveReason = static_cast<uint32_t>(contract.inactiveReason),
        };
    }

    [[nodiscard]] constexpr bool applyFixedSurfaceScalingContractRelayMetadata(
            FixedSurfaceScalingContract& contract,
            const FixedSurfaceScalingContractRelayMetadata& metadata) noexcept {
        if (metadata.inactiveReason > static_cast<uint32_t>(
                SpatialScalingInactiveReason::QueueCommandsUnsupported
            )) {
            return false;
        }
        contract.spatialSurfaceScalingSupported =
            metadata.spatialSurfaceScalingSupported == VK_TRUE;
        contract.variableSurface = metadata.variableSurface == VK_TRUE;
        contract.memoryBudgetConstrained =
            metadata.memoryBudgetConstrained == VK_TRUE;
        contract.inactiveReason = static_cast<SpatialScalingInactiveReason>(
            metadata.inactiveReason
        );
        return true;
    }

    /// One capability contract published by the lower spatial-scaling DSO
    /// while an upper split-role query is on the same thread. Gamescope WSI
    /// may replace VkSurfaceKHR between the two layers, so the cross-DSO relay
    /// deliberately records (but does not match on) the lower surface. Exact
    /// surface matching remains mandatory in the lower role's create-time
    /// contract map.
    struct FixedSurfaceCapabilityRelayRecord {
        VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
        VkSurfaceKHR lowerSurface{VK_NULL_HANDLE};
        FixedSurfaceScalingContract contract{};
    };

    /// Same-thread, one-shot slot used only to hand a capability-query result
    /// from the lower split-role DSO back to the upper DSO that bracketed that
    /// query. Beginning a query clears stale state, and every consume attempt
    /// clears the slot even when the physical device does not match.
    class FixedSurfaceCapabilityRelaySlot {
    public:
        void begin() noexcept {
            record_.reset();
        }

        void publish(
                const VkPhysicalDevice physicalDevice,
                const VkSurfaceKHR lowerSurface,
                const FixedSurfaceScalingContract& contract) noexcept {
            record_ = FixedSurfaceCapabilityRelayRecord{
                .physicalDevice = physicalDevice,
                .lowerSurface = lowerSurface,
                .contract = contract,
            };
        }

        [[nodiscard]] std::optional<FixedSurfaceCapabilityRelayRecord> consume(
                const VkPhysicalDevice physicalDevice) noexcept {
            auto record = std::exchange(record_, std::nullopt);
            if (!record || record->physicalDevice != physicalDevice)
                return std::nullopt;
            return record;
        }

    private:
        std::optional<FixedSurfaceCapabilityRelayRecord> record_;
    };

    [[nodiscard]] constexpr const char* spatialScalingInactiveReasonName(
            const SpatialScalingInactiveReason reason) noexcept {
        switch (reason) {
            case SpatialScalingInactiveReason::None:
                return "none";
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
            case SpatialScalingInactiveReason::GamescopeWsiSurfaceUnproven:
                return "gamescope-wsi-surface-unproven";
            case SpatialScalingInactiveReason::GamescopePresentationTargetUnavailable:
                return "gamescope-presentation-target-unavailable";
            case SpatialScalingInactiveReason::GamescopePresentationTargetNoHeadroom:
                return "gamescope-presentation-target-no-headroom";
            case SpatialScalingInactiveReason::VariableSurfaceFeedback:
                return "variable-surface-feedback";
            case SpatialScalingInactiveReason::VariableSurfaceNoHeadroom:
                return "variable-surface-no-headroom";
            case SpatialScalingInactiveReason::VariableSurfaceMemoryBudget:
                return "variable-surface-memory-budget";
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

    [[nodiscard]] constexpr const char* spatialScalingRuntimeInactiveReason(
            const bool active,
            const bool scalingRequested,
            const float requestedFactor,
            const SpatialScalingInactiveReason reason) noexcept {
        if (active || !scalingRequested)
            return nullptr;
        if (requestedFactor <= 1.0F) {
            return spatialScalingInactiveReasonName(
                SpatialScalingInactiveReason::FactorNotUpscaling
            );
        }
        return reason == SpatialScalingInactiveReason::None
            ? nullptr : spatialScalingInactiveReasonName(reason);
    }

    struct SpatialScalingCreateDecision {
        std::optional<SpatialScalingExtents> extents;
        std::optional<FixedSurfaceScalingContract> fixedContract;
        bool retainedPreviousFixedSource{false};
        bool reusedPreviousPresentationBudget{false};
        bool reusedRollbackPresentationBudget{false};
        bool reusedPreviousDownshiftEnvelope{false};
        bool reusedPreviousSourceGrowthHeadroom{false};
        bool usedBaselinePresentationBudget{false};
        bool memoryBudgetConstrained{false};
        bool gamescopePresentationTargetConstrained{false};
        bool gamescopePresentationTargetBypassed{false};
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

    [[nodiscard]] inline bool validSpatialScalingFactor(
        float factor) noexcept;

    [[nodiscard]] constexpr bool sameExtent(
            const VkExtent2D left, const VkExtent2D right) noexcept {
        return left.width == right.width && left.height == right.height;
    }

    /// Resolve the unconstrained variable-surface presentation extent for a
    /// source/factor pair using the same floor-and-even rule as swapchain
    /// admission. Surface and memory ceilings are intentionally excluded.
    [[nodiscard]] inline VkExtent2D unconstrainedVariablePresentationExtent(
            const VkExtent2D source, const float factor) noexcept {
        if (!validSpatialScalingFactor(factor) || factor <= 1.0F)
            return source;
        VkExtent2D presentation{
            .width = static_cast<uint32_t>(std::floor(
                static_cast<double>(source.width) * factor
            )),
            .height = static_cast<uint32_t>(std::floor(
                static_cast<double>(source.height) * factor
            )),
        };
        if (presentation.width > 1)
            presentation.width &= ~uint32_t{1};
        if (presentation.height > 1)
            presentation.height &= ~uint32_t{1};
        return presentation;
    }

    /// A variable-surface factor edit is WSI-neutral when the active
    /// presentation was already clamped below the old unconstrained request
    /// and the new factor still reaches that exact presentation envelope.
    /// Fixed surfaces are excluded because their virtual source advertisement
    /// is factor-bound even when the lower presentation extent is unchanged.
    [[nodiscard]] inline bool
    variableSurfaceFactorChangePreservesEffectiveExtents(
            const bool variableSurface,
            const bool spatialScalingContractActive,
            const VkExtent2D source,
            const VkExtent2D presentation,
            const float currentFactor,
            const float requestedFactor) noexcept {
        if (!variableSurface || !spatialScalingContractActive ||
                currentFactor == requestedFactor ||
                !validSpatialScalingFactor(currentFactor) ||
                !validSpatialScalingFactor(requestedFactor) ||
                requestedFactor <= 1.0F ||
                sameExtent(source, presentation)) {
            return false;
        }
        const auto unconstrainedCurrent =
            unconstrainedVariablePresentationExtent(source, currentFactor);
        const bool currentWasConstrained =
            presentation.width <= unconstrainedCurrent.width &&
            presentation.height <= unconstrainedCurrent.height &&
            !sameExtent(presentation, unconstrainedCurrent);
        if (!currentWasConstrained)
            return false;
        const auto unconstrainedRequested =
            unconstrainedVariablePresentationExtent(source, requestedFactor);
        return unconstrainedRequested.width >= presentation.width &&
            unconstrainedRequested.height >= presentation.height;
    }

    /// Supersampling changes only the variable Gamescope output envelope. It
    /// is WSI-neutral on fixed/direct surfaces and when the requested policy
    /// still resolves to the already active presentation extent.
    [[nodiscard]] inline bool
    variableSurfaceSupersamplingChangePreservesEffectiveExtents(
            const bool variableSurface,
            const bool spatialScalingContractActive,
            const VkExtent2D source,
            const VkExtent2D presentation,
            const float factor,
            const bool currentSupersampling,
            const bool requestedSupersampling,
            const std::optional<VkExtent2D>& gamescopeTarget) noexcept {
        if (currentSupersampling == requestedSupersampling)
            return false;
        if (!variableSurface || !gamescopeTarget ||
                gamescopeTarget->width == 0 ||
                gamescopeTarget->height == 0 ||
                gamescopeTarget->width == UINT32_MAX ||
                gamescopeTarget->height == UINT32_MAX) {
            return true;
        }
        if (!spatialScalingContractActive)
            return false;

        auto requestedPresentation =
            unconstrainedVariablePresentationExtent(source, factor);
        if (!requestedSupersampling) {
            const double targetFactor = std::min(
                static_cast<double>(gamescopeTarget->width) / source.width,
                static_cast<double>(gamescopeTarget->height) / source.height
            );
            requestedPresentation = unconstrainedVariablePresentationExtent(
                source,
                static_cast<float>(std::min(
                    static_cast<double>(factor), targetFactor
                ))
            );
        }
        return sameExtent(requestedPresentation, presentation);
    }

    enum class SpatialCreateRelayDecision {
        Unavailable,
        Native,
        Split,
    };

    /// Classify the lower role's decision for the create call bracketed by the
    /// upper role. The slot already proves same-thread, one-shot and physical-
    /// device provenance. Query generations are deliberately DSO-local and
    /// may be nonzero for fixed surfaces, so request/extent coherence—not a
    /// synthetic generation value—distinguishes a valid decision from a
    /// missing or mismatched relay.
    [[nodiscard]] constexpr SpatialCreateRelayDecision
    classifySpatialCreateRelay(
            const FixedSurfaceScalingContract& contract,
            const VkExtent2D requestedExtent) noexcept {
        if (!sameExtent(contract.extents.source, requestedExtent))
            return SpatialCreateRelayDecision::Unavailable;
        return sameExtent(
            contract.extents.source, contract.extents.presentation
        ) ? SpatialCreateRelayDecision::Native
          : SpatialCreateRelayDecision::Split;
    }

    /// The upper combined role cannot validate an application override until
    /// its downstream create reaches the lower extent owner. Defer only a
    /// fixed-surface decision for which neither the upper capability cache nor
    /// its local policy selected extents; the post-create one-shot relay still
    /// rejects a missing or mismatched lower decision.
    [[nodiscard]] constexpr bool awaitLowerSpatialCreateRelay(
            const bool capabilityRelay,
            const bool spatialResourceOwner,
            const bool fixedSurfaceContractAvailable,
            const bool scalingExtentsSelected,
            const bool fixedSurface) noexcept {
        return capabilityRelay && spatialResourceOwner &&
            !fixedSurfaceContractAvailable && !scalingExtentsSelected &&
            fixedSurface;
    }

    /// The upper role in a split chain may receive either the lower role's
    /// virtual source extent or a native extent restored by the intervening
    /// WSI layer. Normalize the former back to the lower contract's
    /// presentation extent before applying the upper relay policy; this keeps
    /// a shared capability from being scaled twice while still restoring a
    /// source extent that WSI replaced with native dimensions.
    [[nodiscard]] constexpr bool prepareFixedSurfaceCapabilityRelay(
            VkSurfaceCapabilitiesKHR& capabilities,
            const FixedSurfaceScalingContract& lowerContract) noexcept {
        if (sameExtent(
                capabilities.currentExtent, lowerContract.extents.presentation
            )) {
            return true;
        }
        if (!sameExtent(
                capabilities.currentExtent, lowerContract.extents.source
            )) {
            return false;
        }
        capabilities.currentExtent = lowerContract.extents.presentation;
        return true;
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

    /// Fixed-surface virtualization needs one presentation-capable
    /// graphics/compute queue and at least one advertised SDR format that can
    /// execute the spatial graph. Gamescope may advertise additional HDR or
    /// otherwise unsupported formats even when the managed launch is SDR;
    /// those unrelated choices must not disable a supported SDR path. The
    /// application's selected format is checked again at swapchain creation
    /// and an unsupported fixed-surface selection is rejected fail-closed.
    [[nodiscard]] constexpr bool spatialScalingFixedSurfacePreflightSupported(
            const bool queueSupported,
            const uint32_t advertisedFormatCount,
            const uint32_t compatibleFormatCount) noexcept {
        return queueSupported && advertisedFormatCount != 0 &&
            compatibleFormatCount != 0 &&
            compatibleFormatCount <= advertisedFormatCount;
    }

    [[nodiscard]] inline bool validSpatialScalingFactor(
            const float factor) noexcept {
        return std::isfinite(factor) &&
            factor >= ls::GameConfLimits::minimumScalingFactor &&
            factor <= ls::GameConfLimits::maximumScalingFactor;
    }

    /// Variable WSI surfaces do not expose a compositor-owned presentation
    /// extent. Bound their requested lower swapchain using a conservative
    /// fraction of the largest device-local heap so a high source resolution
    /// cannot multiply into a pathological allocation. The fixed 4K floor
    /// retains the baseline display tier on unified-memory devices whose
    /// driver exposes only a small device-local aperture. The 768-byte ratio
    /// reserves one third of the heap against a 256-byte-per-presentation-
    /// pixel combined active/retired spatial + frame-generation envelope.
    /// When VK_EXT_memory_budget is available, additionally require the
    /// candidate resource graph to fit inside the driver's current process
    /// budget after a 10% (at least 512 MiB) non-MAKO reserve. The graph
    /// accounts separately for the lower WSI image format/count, spatial
    /// source/output images, exported FG transport, and backend-private source
    /// resources. The dynamic limit deliberately has no 4K floor: current
    /// pressure must be allowed to reject a normally supported allocation
    /// instead of risking paging or device loss.
    inline constexpr uint64_t minimumVariablePresentationPixels =
        uint64_t{3840} * uint64_t{2160};
    inline constexpr VkDeviceSize
        variablePresentationHeapBytesPerPixel = 768;
    inline constexpr VkDeviceSize
        variablePresentationAllocationBytesPerPixel = 256;
    inline constexpr VkDeviceSize
        variablePresentationSpatialSourceBytesPerPixel = 24;
    inline constexpr VkDeviceSize
        variablePresentationSpatialOutputBytesPerPixel = 8;
    inline constexpr VkDeviceSize
        variablePresentationFrameTransportBytesPerPixel = 8;
    inline constexpr VkDeviceSize
        variablePresentationBackendSourceBytesPerPixel = 40;
    inline constexpr VkDeviceSize variablePresentationFixedAllocationBytes =
        VkDeviceSize{16} * 1024 * 1024;
    inline constexpr VkDeviceSize minimumVariablePresentationLiveReserve =
        VkDeviceSize{512} * 1024 * 1024;
    inline constexpr VkDeviceSize variablePresentationLiveReserveDivisor = 10;

    struct VariablePresentationMemoryAdmission {
        VkDeviceSize heapBytes{0};
        VkDeviceSize heapBudgetBytes{0};
        VkDeviceSize heapUsageBytes{0};
        VkDeviceSize reservedHeadroomBytes{0};
        uint64_t staticPixelBudget{0};
        std::optional<uint64_t> livePixelBudget;
        uint64_t effectivePixelBudget{0};
    };

    struct VariablePresentationResourceAdmission {
        VkDeviceSize fixedSourceBytes{0};
        VkDeviceSize presentationBytesPerPixel{0};
        std::optional<uint64_t> livePixelBudget;
        uint64_t effectivePixelBudget{0};
    };

    [[nodiscard]] constexpr VkDeviceSize saturatingDeviceSizeMultiply(
            const VkDeviceSize left, const VkDeviceSize right) noexcept {
        if (left == 0 || right == 0)
            return 0;
        if (left > std::numeric_limits<VkDeviceSize>::max() / right)
            return std::numeric_limits<VkDeviceSize>::max();
        return left * right;
    }

    [[nodiscard]] constexpr VkDeviceSize saturatingDeviceSizeAdd(
            const VkDeviceSize left, const VkDeviceSize right) noexcept {
        if (left > std::numeric_limits<VkDeviceSize>::max() - right)
            return std::numeric_limits<VkDeviceSize>::max();
        return left + right;
    }

    [[nodiscard]] constexpr VkDeviceSize
    variablePresentationSwapchainBytesPerPixel(
            const VkFormat format) noexcept {
        return format == VK_FORMAT_R16G16B16A16_SFLOAT ? 8 : 4;
    }

    [[nodiscard]] constexpr std::optional<uint32_t>
    largestDeviceLocalHeapIndex(
            const VkPhysicalDeviceMemoryProperties& properties) noexcept {
        std::optional<uint32_t> largestIndex;
        for (uint32_t index = 0; index < properties.memoryHeapCount; ++index) {
            if ((properties.memoryHeaps[index].flags &
                    VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0) {
                continue;
            }
            if (!largestIndex || properties.memoryHeaps[index].size >
                    properties.memoryHeaps[*largestIndex].size) {
                largestIndex = index;
            }
        }
        return largestIndex;
    }

    [[nodiscard]] constexpr VkDeviceSize largestDeviceLocalHeapBytes(
            const VkPhysicalDeviceMemoryProperties& properties) noexcept {
        const auto index = largestDeviceLocalHeapIndex(properties);
        return index ? properties.memoryHeaps[*index].size : 0;
    }

    [[nodiscard]] constexpr uint64_t variablePresentationPixelBudget(
            const VkPhysicalDeviceMemoryProperties& properties) noexcept {
        const auto heapBytes = largestDeviceLocalHeapBytes(properties);
        if (heapBytes == 0)
            return 0;
        return std::max(
            minimumVariablePresentationPixels,
            static_cast<uint64_t>(
                heapBytes / variablePresentationHeapBytesPerPixel
            )
        );
    }

    [[nodiscard]] constexpr VariablePresentationMemoryAdmission
    variablePresentationMemoryAdmission(
            const VkPhysicalDeviceMemoryProperties& properties,
            const VkPhysicalDeviceMemoryBudgetPropertiesEXT*
                liveBudget = nullptr) noexcept {
        VariablePresentationMemoryAdmission admission{
            .heapBytes = largestDeviceLocalHeapBytes(properties),
            .staticPixelBudget = variablePresentationPixelBudget(properties),
        };
        admission.effectivePixelBudget = admission.staticPixelBudget;

        const auto heapIndex = largestDeviceLocalHeapIndex(properties);
        if (!heapIndex || !liveBudget ||
                liveBudget->heapBudget[*heapIndex] == 0) {
            return admission;
        }

        admission.heapBudgetBytes = liveBudget->heapBudget[*heapIndex];
        admission.heapUsageBytes = liveBudget->heapUsage[*heapIndex];
        admission.reservedHeadroomBytes = std::max(
            minimumVariablePresentationLiveReserve,
            admission.heapBudgetBytes /
                variablePresentationLiveReserveDivisor
        );
        const VkDeviceSize availableBytes =
            admission.heapBudgetBytes > admission.heapUsageBytes
            ? admission.heapBudgetBytes - admission.heapUsageBytes : 0;
        const VkDeviceSize admissibleBytes =
            availableBytes > admission.reservedHeadroomBytes
            ? availableBytes - admission.reservedHeadroomBytes : 0;
        admission.livePixelBudget = static_cast<uint64_t>(
            admissibleBytes /
                variablePresentationAllocationBytesPerPixel
        );
        admission.effectivePixelBudget = std::min(
            admission.staticPixelBudget, *admission.livePixelBudget
        );
        return admission;
    }

    /// Convert live heap headroom into a presentation-pixel limit using the
    /// resources the replacement will actually allocate. Source-sized costs
    /// are paid once, while presentation-sized costs scale with the candidate
    /// lower WSI extent. Constants intentionally round above the measured FP16
    /// LS1/FG allocations; the separate non-MAKO reserve remains untouched.
    [[nodiscard]] constexpr VariablePresentationResourceAdmission
    variablePresentationResourceAdmission(
            const VariablePresentationMemoryAdmission& memory,
            const VkExtent2D sourceExtent,
            const VkFormat swapchainFormat,
            const uint32_t swapchainImageCount,
            const size_t generatedFrameCapacity) noexcept {
        const VkDeviceSize sourcePixels = saturatingDeviceSizeMultiply(
            sourceExtent.width, sourceExtent.height
        );
        const VkDeviceSize transportImageCount = saturatingDeviceSizeAdd(
            2, generatedFrameCapacity
        );
        const VkDeviceSize sourceBytesPerPixel = saturatingDeviceSizeAdd(
            saturatingDeviceSizeAdd(
                variablePresentationSpatialSourceBytesPerPixel,
                variablePresentationBackendSourceBytesPerPixel
            ),
            saturatingDeviceSizeMultiply(
                variablePresentationFrameTransportBytesPerPixel,
                transportImageCount
            )
        );
        const VkDeviceSize fixedSourceBytes = saturatingDeviceSizeAdd(
            variablePresentationFixedAllocationBytes,
            saturatingDeviceSizeMultiply(
                sourcePixels, sourceBytesPerPixel
            )
        );
        const VkDeviceSize presentationBytesPerPixel =
            saturatingDeviceSizeAdd(
                variablePresentationSpatialOutputBytesPerPixel,
                saturatingDeviceSizeMultiply(
                    variablePresentationSwapchainBytesPerPixel(
                        swapchainFormat
                    ),
                    std::max(uint32_t{1}, swapchainImageCount)
                )
            );
        VariablePresentationResourceAdmission admission{
            .fixedSourceBytes = fixedSourceBytes,
            .presentationBytesPerPixel = presentationBytesPerPixel,
            .effectivePixelBudget = memory.staticPixelBudget,
        };
        if (!memory.livePixelBudget)
            return admission;

        const VkDeviceSize availableBytes =
            memory.heapBudgetBytes > memory.heapUsageBytes
            ? memory.heapBudgetBytes - memory.heapUsageBytes : 0;
        const VkDeviceSize admissibleBytes =
            availableBytes > memory.reservedHeadroomBytes
            ? availableBytes - memory.reservedHeadroomBytes : 0;
        const VkDeviceSize presentationBytes =
            admissibleBytes > fixedSourceBytes
            ? admissibleBytes - fixedSourceBytes : 0;
        admission.livePixelBudget = presentationBytesPerPixel != 0
            ? static_cast<uint64_t>(
                presentationBytes / presentationBytesPerPixel
            )
            : 0;
        admission.effectivePixelBudget = std::min(
            memory.staticPixelBudget, *admission.livePixelBudget
        );
        return admission;
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
                .supersampling = profile.scaling_supersampling,
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
                .supersampling = profile.scaling_supersampling,
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
                fixedContract = std::nullopt,
            const std::optional<uint64_t>
                variablePresentationPixels = std::nullopt,
            const std::optional<VkExtent2D>&
                gamescopePresentationTarget = std::nullopt,
            const bool gamescopePresentationTargetRequired = false,
            const std::optional<uint64_t>
                variablePresentationStaticPixels = std::nullopt,
            const std::optional<SpatialScalingExtents>&
                variableSurfaceRollbackExtents = std::nullopt) noexcept {
        SpatialScalingCreateDecision decision{
            .fixedContract = fixedContract,
        };
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
                // A fixed Gamescope surface may keep the application's
                // existing virtual window size while a live factor update
                // advertises a different source. Retain only the exact split
                // proven by the preceding swapchain on this same surface and
                // presentation. This prevents an OUT_OF_DATE recreation loop
                // without treating an arbitrary stale request as valid.
                if (previousVariableExtents &&
                        sameExtent(
                            previousVariableExtents->source,
                            requestedExtent
                        ) &&
                        sameExtent(
                            previousVariableExtents->presentation,
                            realCapabilities.currentExtent
                        ) &&
                        !sameExtent(
                            previousVariableExtents->source,
                            previousVariableExtents->presentation
                        )) {
                    decision.extents = *previousVariableExtents;
                    decision.retainedPreviousFixedSource = true;
                    return decision;
                }
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
        if (gamescopePresentationTargetRequired &&
                (!gamescopePresentationTarget ||
                    !fixedSurfaceExtent(*gamescopePresentationTarget))) {
            decision.inactiveReason = SpatialScalingInactiveReason::
                GamescopePresentationTargetUnavailable;
            return decision;
        }
        if (requestedExtent.width == 0 || requestedExtent.height == 0) {
            decision.inactiveReason =
                SpatialScalingInactiveReason::VariableSurfaceNoHeadroom;
            return decision;
        }

        // Some uncontracted variable Wayland surfaces echo the enlarged lower
        // WSI extent back to the application as its next logical size. Treat
        // an exact previous presentation extent as compositor feedback, not a
        // new source to enlarge again. A managed Gamescope split chain instead
        // proves the application's request through the lower create relay: a
        // real resolution change matches that source, while a lower-only WSI
        // mutation fails the upper relay's exact requested-extent check.
        if (!gamescopePresentationTargetRequired &&
                previousVariableExtents &&
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
        const double unconstrainedFactor = std::min({
            static_cast<double>(policy.factor),
            maximumWidthFactor,
            maximumHeightFactor,
        });
        double effectiveFactor = unconstrainedFactor;
        if (!policy.supersampling && gamescopePresentationTarget &&
                fixedSurfaceExtent(*gamescopePresentationTarget)) {
            const double targetWidthFactor =
                static_cast<double>(gamescopePresentationTarget->width) /
                static_cast<double>(requestedExtent.width);
            const double targetHeightFactor =
                static_cast<double>(gamescopePresentationTarget->height) /
                static_cast<double>(requestedExtent.height);
            const double targetFactor = std::min(
                targetWidthFactor, targetHeightFactor
            );
            effectiveFactor = std::min(effectiveFactor, targetFactor);
            decision.gamescopePresentationTargetConstrained =
                targetFactor < unconstrainedFactor;
        } else if (policy.supersampling && gamescopePresentationTarget &&
                fixedSurfaceExtent(*gamescopePresentationTarget)) {
            decision.gamescopePresentationTargetBypassed =
                requestedExtent.width * effectiveFactor >
                    gamescopePresentationTarget->width ||
                requestedExtent.height * effectiveFactor >
                    gamescopePresentationTarget->height;
        }
        if (effectiveFactor <= 1.0) {
            decision.inactiveReason = gamescopePresentationTarget
                ? SpatialScalingInactiveReason::
                    GamescopePresentationTargetNoHeadroom
                : SpatialScalingInactiveReason::VariableSurfaceNoHeadroom;
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
        const VkExtent2D requestedPresentation = presentation;
        const uint64_t presentationPixels =
            static_cast<uint64_t>(presentation.width) *
            static_cast<uint64_t>(presentation.height);
        if (variablePresentationPixels &&
                presentationPixels > *variablePresentationPixels) {
            // VK_EXT_memory_budget can lag immediately released WSI and
            // private allocations. When a split extent was already running
            // on this surface, it is safe to retain that proven envelope or
            // shrink inside it, including when the game lowers its source
            // resolution: neither the new source nor presentation resources
            // can exceed the context they supersede. Apply this only when the
            // live budget tightened the deterministic ceiling; static
            // admission remains independent of launch history.
            const bool liveBudgetTightenedStaticCeiling =
                variablePresentationStaticPixels &&
                *variablePresentationPixels <
                    *variablePresentationStaticPixels;
            // A deliberate same-source factor downshift may be followed by
            // an immediate return to the larger extent which was already
            // allocated successfully on this surface. Reuse that one-step
            // rollback proof before consulting the smaller current extent;
            // it is cleared after the next successful upshift and can never
            // enlarge the proven source or presentation envelope.
            if (liveBudgetTightenedStaticCeiling &&
                    variableSurfaceRollbackExtents &&
                    sameExtent(
                        requestedExtent,
                        variableSurfaceRollbackExtents->source
                    ) &&
                    !sameExtent(
                        variableSurfaceRollbackExtents->source,
                        variableSurfaceRollbackExtents->presentation
                    )) {
                const double rollbackWidthFactor =
                    static_cast<double>(
                        variableSurfaceRollbackExtents->presentation.width
                    ) / requestedExtent.width;
                const double rollbackHeightFactor =
                    static_cast<double>(
                        variableSurfaceRollbackExtents->presentation.height
                    ) / requestedExtent.height;
                const double retainedFactor = std::min({
                    effectiveFactor,
                    rollbackWidthFactor,
                    rollbackHeightFactor,
                });
                if (retainedFactor > 1.0) {
                    VkExtent2D retainedPresentation{
                        .width = static_cast<uint32_t>(std::floor(
                            static_cast<double>(requestedExtent.width) *
                                retainedFactor
                        )),
                        .height = static_cast<uint32_t>(std::floor(
                            static_cast<double>(requestedExtent.height) *
                                retainedFactor
                        )),
                    };
                    if (retainedPresentation.width > 1)
                        retainedPresentation.width &= ~uint32_t{1};
                    if (retainedPresentation.height > 1)
                        retainedPresentation.height &= ~uint32_t{1};
                    if (retainedPresentation.width > requestedExtent.width &&
                            retainedPresentation.height >
                                requestedExtent.height) {
                        presentation = retainedPresentation;
                        decision.reusedPreviousPresentationBudget = true;
                        decision.reusedRollbackPresentationBudget = true;
                        decision.memoryBudgetConstrained = !sameExtent(
                            retainedPresentation, requestedPresentation
                        );
                    }
                }
            }
            const uint64_t requestedSourcePixels =
                static_cast<uint64_t>(requestedExtent.width) *
                static_cast<uint64_t>(requestedExtent.height);
            const uint64_t previousSourcePixels = previousVariableExtents
                ? static_cast<uint64_t>(
                    previousVariableExtents->source.width
                  ) * static_cast<uint64_t>(
                    previousVariableExtents->source.height
                  )
                : 0;
            const uint64_t additionalSourcePixels =
                requestedSourcePixels > previousSourcePixels
                ? requestedSourcePixels - previousSourcePixels : 0;
            const bool sourceFitsPreviousPresentation =
                previousVariableExtents &&
                requestedExtent.width <=
                    previousVariableExtents->presentation.width &&
                requestedExtent.height <=
                    previousVariableExtents->presentation.height;
            const bool sourceDidNotGrow = previousVariableExtents &&
                requestedExtent.width <=
                    previousVariableExtents->source.width &&
                requestedExtent.height <=
                    previousVariableExtents->source.height;
            // A source-resolution increase can keep an equal-or-smaller
            // presentation envelope only when its incremental source area
            // fits the driver's remaining live headroom. Charging the growth
            // at the same conservative bytes-per-pixel rate leaves the full
            // non-MAKO reserve untouched and never refunds unattributed heap
            // usage. Cold creates and any presentation growth still require
            // ordinary admission.
            const bool sourceTransitionFitsLiveHeadroom =
                sourceDidNotGrow ||
                (sourceFitsPreviousPresentation &&
                    additionalSourcePixels <= *variablePresentationPixels);
            if (!decision.reusedPreviousPresentationBudget &&
                    liveBudgetTightenedStaticCeiling &&
                    previousVariableExtents &&
                    sourceTransitionFitsLiveHeadroom &&
                    !sameExtent(
                        previousVariableExtents->source,
                        previousVariableExtents->presentation
                    )) {
                const double previousWidthFactor =
                    static_cast<double>(
                        previousVariableExtents->presentation.width
                    ) / requestedExtent.width;
                const double previousHeightFactor =
                    static_cast<double>(
                        previousVariableExtents->presentation.height
                    ) / requestedExtent.height;
                const double retainedFactor = std::min({
                    effectiveFactor,
                    previousWidthFactor,
                    previousHeightFactor,
                });
                if (retainedFactor > 1.0) {
                    VkExtent2D retainedPresentation{
                        .width = static_cast<uint32_t>(std::floor(
                            static_cast<double>(requestedExtent.width) *
                                retainedFactor
                        )),
                        .height = static_cast<uint32_t>(std::floor(
                            static_cast<double>(requestedExtent.height) *
                                retainedFactor
                        )),
                    };
                    if (retainedPresentation.width > 1)
                        retainedPresentation.width &= ~uint32_t{1};
                    if (retainedPresentation.height > 1)
                        retainedPresentation.height &= ~uint32_t{1};
                    if (retainedPresentation.width > requestedExtent.width &&
                            retainedPresentation.height >
                                requestedExtent.height) {
                        presentation = retainedPresentation;
                        decision.reusedPreviousPresentationBudget = true;
                        decision.reusedPreviousDownshiftEnvelope =
                            sourceDidNotGrow && !sameExtent(
                                previousVariableExtents->source,
                                requestedExtent
                            );
                        decision.reusedPreviousSourceGrowthHeadroom =
                            additionalSourcePixels != 0;
                        decision.memoryBudgetConstrained = !sameExtent(
                            retainedPresentation, requestedPresentation
                        );
                    }
                }
            }

            // The fallback must not depend on which resolution happened to be
            // active first. Aspect-fit every over-budget request into the
            // deterministic 4K baseline envelope, which is already reserved
            // by variablePresentationPixelBudget(), instead of borrowing a
            // previous swapchain's presentation extent. This makes a cold
            // 1440p launch select the same 3840x2160 contract as a 1080p ->
            // 1440p transition on an 8 GiB unified-memory device.
            if (!decision.reusedPreviousPresentationBudget) {
                constexpr VkExtent2D baselinePresentation{3840, 2160};
                const double baselineWidthFactor =
                    static_cast<double>(baselinePresentation.width) /
                    static_cast<double>(requestedExtent.width);
                const double baselineHeightFactor =
                    static_cast<double>(baselinePresentation.height) /
                    static_cast<double>(requestedExtent.height);
                const double baselineFactor = std::min({
                    effectiveFactor,
                    baselineWidthFactor,
                    baselineHeightFactor,
                });
                if (baselineFactor > 1.0) {
                    VkExtent2D baselineFit{
                        .width = static_cast<uint32_t>(std::floor(
                            static_cast<double>(requestedExtent.width) *
                                baselineFactor
                        )),
                        .height = static_cast<uint32_t>(std::floor(
                            static_cast<double>(requestedExtent.height) *
                                baselineFactor
                        )),
                    };
                    if (baselineFit.width > 1)
                        baselineFit.width &= ~uint32_t{1};
                    if (baselineFit.height > 1)
                        baselineFit.height &= ~uint32_t{1};
                    const uint64_t baselinePixels =
                        static_cast<uint64_t>(baselineFit.width) *
                        static_cast<uint64_t>(baselineFit.height);
                    if (baselineFit.width > requestedExtent.width &&
                            baselineFit.height > requestedExtent.height &&
                            baselinePixels <= *variablePresentationPixels) {
                        presentation = baselineFit;
                        decision.usedBaselinePresentationBudget = true;
                        decision.memoryBudgetConstrained = !sameExtent(
                            baselineFit, requestedPresentation
                        );
                    }
                }
            }
            if (!decision.reusedPreviousPresentationBudget &&
                    !decision.usedBaselinePresentationBudget) {
                decision.inactiveReason = SpatialScalingInactiveReason::
                    VariableSurfaceMemoryBudget;
                return decision;
            }
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
                fixedContract = std::nullopt,
            const std::optional<uint64_t>
                variablePresentationPixels = std::nullopt,
            const std::optional<VkExtent2D>&
                gamescopePresentationTarget = std::nullopt,
            const bool gamescopePresentationTargetRequired = false,
            const std::optional<uint64_t>
                variablePresentationStaticPixels = std::nullopt,
            const std::optional<SpatialScalingExtents>&
                variableSurfaceRollbackExtents = std::nullopt) noexcept {
        return scalingDecisionForCreate(
            SpatialScalingPolicy{
                .enabled = ls::spatialScalingRequested(profile),
                .factor = profile.scaling_factor,
                .supersampling = profile.scaling_supersampling,
            },
            processSupported,
            policyRevision,
            realCapabilities,
            requestedExtent,
            previousVariableExtents,
            fixedContract,
            variablePresentationPixels,
            gamescopePresentationTarget,
            gamescopePresentationTargetRequired,
            variablePresentationStaticPixels,
            variableSurfaceRollbackExtents
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
                previousVariableExtents,
            const bool managedGamescopeCreateRelay = false) noexcept {
        return ls::spatialScalingRequested(profile) &&
            validSpatialScalingFactor(profile.scaling_factor) &&
            profile.scaling_factor > 1.0F &&
            !fixedSurfaceExtent(realCapabilities.currentExtent) &&
            !managedGamescopeCreateRelay &&
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
            const bool retainInactiveProof,
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
        if (retainInactiveProof && previous &&
                sameExtent(previous->source, applicationExtent)) {
            return previous;
        }
        return std::nullopt;
    }

    /// Retain a successfully allocated same-source presentation envelope
    /// across deliberate factor downshifts, including Native Resolution. An
    /// intermediate upshift inside the envelope keeps the proof; reaching the
    /// proven maximum consumes it. Memory-forced changes and source changes
    /// clear it, so cold and unrelated allocations remain subject to ordinary
    /// live admission.
    [[nodiscard]] inline std::optional<SpatialScalingExtents>
    committedVariableSurfaceRollbackExtents(
            const std::optional<SpatialScalingExtents>& previousCurrent,
            const std::optional<SpatialScalingExtents>& previousRollback,
            const bool profileActive,
            const bool variableSurface,
            const bool spatialScalingActive,
            const bool feedbackSuppressed,
            const bool retainInactiveProof,
            const bool memoryBudgetConstrained,
            const VkExtent2D applicationExtent,
            const VkExtent2D presentationExtent) noexcept {
        if (!profileActive || !variableSurface)
            return std::nullopt;
        if (!spatialScalingActive) {
            if (feedbackSuppressed)
                return previousRollback;
            if (retainInactiveProof && previousRollback &&
                    sameExtent(
                        previousRollback->source, applicationExtent
                    )) {
                return previousRollback;
            }
            return std::nullopt;
        }
        if (!previousCurrent ||
                !sameExtent(previousCurrent->source, applicationExtent) ||
                memoryBudgetConstrained) {
            return std::nullopt;
        }

        if (previousRollback) {
            const bool matchingSource = sameExtent(
                previousRollback->source, applicationExtent
            );
            const bool remainsInsideRollback =
                presentationExtent.width <=
                    previousRollback->presentation.width &&
                presentationExtent.height <=
                    previousRollback->presentation.height;
            if (!matchingSource || !remainsInsideRollback)
                return std::nullopt;
            if (sameExtent(
                    presentationExtent,
                    previousRollback->presentation
                )) {
                return std::nullopt;
            }
            return previousRollback;
        }

        const bool presentationUnchanged = sameExtent(
            previousCurrent->presentation, presentationExtent
        );
        if (presentationUnchanged)
            return previousRollback;

        const bool deliberateDownshift =
            presentationExtent.width <
                previousCurrent->presentation.width &&
            presentationExtent.height <
                previousCurrent->presentation.height;
        if (deliberateDownshift)
            return previousCurrent;

        return std::nullopt;
    }

}
