/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "spatial_scaling_policy.hpp"
#include "swapchain_create_policy.hpp"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const char* message) {
        if (condition)
            return;
        std::cerr << message << '\n';
        std::exit(1);
    }

    VkSurfaceCapabilitiesKHR fixedCapabilities(
            const uint32_t width, const uint32_t height) {
        return {
            .currentExtent = {width, height},
            .minImageExtent = {16, 16},
            .maxImageExtent = {width, height},
            .maxImageArrayLayers = 4,
            .supportedCompositeAlpha =
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR |
                VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            .supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        };
    }

    VkPhysicalDeviceMemoryProperties memoryProperties(
            const VkDeviceSize deviceLocalHeapBytes) {
        VkPhysicalDeviceMemoryProperties properties{};
        properties.memoryHeapCount = 1;
        properties.memoryHeaps[0] = {
            .size = deviceLocalHeapBytes,
            .flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT,
        };
        return properties;
    }
}

int main() {
    expect(spatialScalingProcessSupported(false, false, false),
        "An ordinary desktop process must permit spatial scaling");
    expect(!spatialScalingProcessSupported(true, false, false),
        "A Gamescope environment hint must fail closed before X11 feedback");
    expect(!spatialScalingProcessSupported(false, true, false),
        "Confirmed Gamescope must fail closed while HDR exposure is allowed");
    expect(spatialScalingProcessSupported(true, false, true) &&
            spatialScalingProcessSupported(false, true, true),
        "Disabled HDR exposure must permit Gamescope scaling from either identity source");
    expect(liveGamescopeHdrReclassificationAllowed(false),
        "Unscaled swapchains must retain live Gamescope HDR transitions");
    expect(!liveGamescopeHdrReclassificationAllowed(true),
        "Scaled swapchains must remain on their immutable SDR boundary");

    expect(spatialSplitSurfaceScalingSupported(
            false, SpatialSurfaceOrigin::Unknown
        ) && spatialSplitSurfaceScalingSupported(
            false, SpatialSurfaceOrigin::Xlib
        ),
        "The direct combined Renderer must accept its application-owned surface");
    expect(spatialSplitSurfaceScalingSupported(
            true, SpatialSurfaceOrigin::Wayland
        ),
        "The lower split role must accept a Gamescope WSI Wayland surface");
    expect(!spatialSplitSurfaceScalingSupported(
            true, SpatialSurfaceOrigin::Unknown
        ) && !spatialSplitSurfaceScalingSupported(
            true, SpatialSurfaceOrigin::Xcb
        ) && !spatialSplitSurfaceScalingSupported(
            true, SpatialSurfaceOrigin::Xlib
        ),
        "The lower split role must fail legacy or unproven surfaces native");
    expect(std::string_view(spatialSurfaceOriginName(
            SpatialSurfaceOrigin::Wayland
        )) == "wayland" && std::string_view(spatialSurfaceOriginName(
            SpatialSurfaceOrigin::Xlib
        )) == "xlib",
        "Surface provenance diagnostics must remain machine-readable");

    const auto deckPlacement = selectSpatialFramePipelinePlacement(
        {852, 532}, {1280, 800}
    );
    expect(deckPlacement ==
            SpatialFramePipelinePlacement::PreFrameGeneration &&
            sameExtent(frameGenerationExtent(
                deckPlacement, {852, 532}, {1280, 800}
            ), {1280, 800}),
        "Deck-class outputs must reconstruct once before frame generation");
    const auto fullHdPlacement = selectSpatialFramePipelinePlacement(
        {1280, 720}, {1920, 1080}
    );
    expect(fullHdPlacement ==
            SpatialFramePipelinePlacement::PreFrameGeneration,
        "720p-to-1080p must retain the low-resolution pre-FG path");
    expect(selectSpatialFramePipelinePlacement(
            {1280, 800}, {1920, 1200}
        ) == SpatialFramePipelinePlacement::PreFrameGeneration,
        "The 1920x1200 low-resolution budget boundary must remain pre-FG");
    expect(selectSpatialFramePipelinePlacement(
            {1720, 600}, {2560, 900}
        ) == SpatialFramePipelinePlacement::PreFrameGeneration,
        "An ultrawide output at the same 1920x1200 pixel budget must remain pre-FG");
    expect(selectSpatialFramePipelinePlacement(
            {1720, 600}, {2562, 900}
        ) == SpatialFramePipelinePlacement::PostFrameGeneration,
        "An ultrawide output above the presentation pixel budget must use post-FG");
    const auto fourKPlacement = selectSpatialFramePipelinePlacement(
        {1920, 1080}, {3840, 2160}
    );
    expect(fourKPlacement ==
            SpatialFramePipelinePlacement::PostFrameGeneration &&
            sameExtent(frameGenerationExtent(
                fourKPlacement, {1920, 1080}, {3840, 2160}
            ), {1920, 1080}),
        "1080p-to-4K must keep interpolation resources at source resolution");
    expect(spatialScalerTransitionRequiresGeneratedRenderDrain(
            fourKPlacement) &&
            !spatialScalerTransitionRequiresGeneratedRenderDrain(
                SpatialFramePipelinePlacement::PreFrameGeneration),
        "Only post-FG live scaler replacement must drain generated render work");
    expect(selectSpatialFramePipelinePlacement(
            {2560, 1440}, {3840, 2160}
        ) == SpatialFramePipelinePlacement::PostFrameGeneration,
        "1440p-to-4K must keep the high-resolution source-FG path");
    expect(std::string_view(spatialFramePipelinePlacementName(
            fourKPlacement)) == "post-frame-generation" &&
            std::string_view(spatialFramePipelinePlacementReason(
                fourKPlacement)) ==
                "source-resolution-frame-generation-saves-high-resolution-work",
        "Pipeline placement diagnostics must remain stable and machine-readable");
    expect(selectSpatialFramePipelinePlacement(
            {0, 0}, {3840, 2160}
        ) == SpatialFramePipelinePlacement::PreFrameGeneration,
        "Invalid extents must fail to the conservative pre-FG placement");
    const auto directSourceUsage = frameGenerationSourceImageUsage(
        SpatialFramePipelinePlacement::PreFrameGeneration, true
    );
    expect((directSourceUsage & VK_IMAGE_USAGE_STORAGE_BIT) != 0 &&
            (directSourceUsage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0 &&
            (directSourceUsage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) != 0 &&
            (directSourceUsage & VK_IMAGE_USAGE_SAMPLED_BIT) != 0,
        "Pre-FG scaling sources must support direct compute reconstruction and backend sampling");
    expect((frameGenerationSourceImageUsage(
                SpatialFramePipelinePlacement::PostFrameGeneration, true
            ) & VK_IMAGE_USAGE_STORAGE_BIT) == 0 &&
            (frameGenerationSourceImageUsage(
                SpatialFramePipelinePlacement::PreFrameGeneration, false
            ) & VK_IMAGE_USAGE_STORAGE_BIT) == 0,
        "Post-FG and FG-only sources must retain the narrow transport usage contract");
    expect(directSpatialFrameGenerationOutputEligible(
            SpatialFramePipelinePlacement::PreFrameGeneration, true, 2
        ) && !directSpatialFrameGenerationOutputEligible(
            SpatialFramePipelinePlacement::PostFrameGeneration, true, 2
        ) && !directSpatialFrameGenerationOutputEligible(
            SpatialFramePipelinePlacement::PreFrameGeneration, false, 2
        ) && !directSpatialFrameGenerationOutputEligible(
            SpatialFramePipelinePlacement::PreFrameGeneration, true, 1
        ),
        "Direct spatial output eligibility must remain limited to the complete pre-FG source pair");

    VkSurfaceCapabilitiesKHR surfaceCapabilities{
        .supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
            VK_IMAGE_USAGE_TRANSFER_DST_BIT,
    };
    expect(spatialScalingSurfaceCapabilitiesSupported(surfaceCapabilities),
        "An opaque transfer-capable surface must pass scaling preflight");
    surfaceCapabilities.supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    expect(!spatialScalingSurfaceCapabilitiesSupported(surfaceCapabilities),
        "A surface without opaque composition must fail scaling preflight");
    surfaceCapabilities.supportedCompositeAlpha =
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    surfaceCapabilities.supportedUsageFlags = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    expect(!spatialScalingSurfaceCapabilitiesSupported(surfaceCapabilities),
        "A surface without bidirectional transfer usage must fail scaling preflight");

    expect(spatialScalingFixedSurfacePreflightSupported(true, 4, 2),
        "A fixed Gamescope surface with compatible SDR and unrelated HDR formats must remain scalable");
    expect(!spatialScalingFixedSurfacePreflightSupported(true, 4, 0),
        "A fixed surface without any compatible advertised format must fail closed");
    expect(!spatialScalingFixedSurfacePreflightSupported(false, 4, 4),
        "Compatible formats must not bypass the presentation-queue requirement");
    expect(!spatialScalingFixedSurfacePreflightSupported(true, 0, 0),
        "An empty surface-format enumeration must fail closed");

    const VkSwapchainCreateInfoKHR ordinarySwapchain{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .flags = 0,
        .imageArrayLayers = 1,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
    };
    expect(spatialScalingSwapchainShapeSupported(ordinarySwapchain),
        "An opaque single-layer ordinary FIFO swapchain must support scaling");

    auto layeredSwapchain = ordinarySwapchain;
    layeredSwapchain.imageArrayLayers = 2;
    expect(!spatialScalingSwapchainShapeSupported(layeredSwapchain),
        "Array-layer swapchains must fail the initial scaling shape contract");

    auto protectedSwapchain = ordinarySwapchain;
    protectedSwapchain.flags = VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR;
    expect(!spatialScalingSwapchainShapeSupported(protectedSwapchain),
        "Protected swapchains must fail the initial scaling shape contract");

    auto splitInstanceSwapchain = ordinarySwapchain;
    splitInstanceSwapchain.flags =
        VK_SWAPCHAIN_CREATE_SPLIT_INSTANCE_BIND_REGIONS_BIT_KHR;
    expect(!spatialScalingSwapchainShapeSupported(splitInstanceSwapchain),
        "Split-instance device-group swapchains must fail the initial scaling shape contract");

    auto sharedDemandSwapchain = ordinarySwapchain;
    sharedDemandSwapchain.presentMode =
        VK_PRESENT_MODE_SHARED_DEMAND_REFRESH_KHR;
    expect(!spatialScalingSwapchainShapeSupported(sharedDemandSwapchain),
        "Shared-demand swapchains must fail the scaling shape contract");

    auto sharedContinuousSwapchain = ordinarySwapchain;
    sharedContinuousSwapchain.presentMode =
        VK_PRESENT_MODE_SHARED_CONTINUOUS_REFRESH_KHR;
    expect(!spatialScalingSwapchainShapeSupported(sharedContinuousSwapchain),
        "Shared-continuous swapchains must fail the scaling shape contract");

    auto nonOpaqueSwapchain = ordinarySwapchain;
    nonOpaqueSwapchain.compositeAlpha =
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR;
    expect(!spatialScalingSwapchainShapeSupported(nonOpaqueSwapchain),
        "Non-opaque swapchains must fail the initial scaling shape contract");

    ls::GameConf profile{
        .scaling_enabled = true,
        .scaling_method = ls::ScalingMethod::Mako,
        .scaling_factor = 1.5F,
    };

    auto deckCapabilities = fixedCapabilities(1280, 800);
    const auto deck = virtualizeSurfaceCapabilities(
        profile, deckCapabilities
    );
    expect(deck.has_value(), "A fixed Deck output must support scaling");
    expect(deck->source.width == 852 && deck->source.height == 532,
        "The Deck source extent must be deterministically rounded down to even pixels");
    expect(deck->presentation.width == 1280 &&
            deck->presentation.height == 800,
        "The native presentation extent must be preserved");
    expect(sameExtent(deckCapabilities.currentExtent, deck->source),
        "The application must observe the scaled source extent");
    expect(deckCapabilities.minImageExtent.width <= deck->source.width &&
            deckCapabilities.maxImageExtent.width >= deck->source.width,
        "Virtual capabilities must retain a self-consistent extent range");
    expect(deckCapabilities.maxImageArrayLayers == 1 &&
            deckCapabilities.supportedCompositeAlpha ==
                VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        "Virtual capabilities must advertise only the accepted swapchain shape");

    profile.frame_generation_enabled = false;
    const auto scalingOnly = selectSpatialScalingExtents(
        profile, fixedCapabilities(1280, 800)
    );
    expect(scalingOnly && sameExtent(scalingOnly->source, deck->source),
        "Scaling-only mode must not depend on frame-generation enablement");
    profile.frame_generation_enabled = true;
    profile.adaptive = true;
    const auto combinedAdaptive = selectSpatialScalingExtents(
        profile, fixedCapabilities(1280, 800)
    );
    expect(combinedAdaptive &&
            sameExtent(combinedAdaptive->source, deck->source),
        "Adaptive frame generation must not alter spatial extent selection");
    profile.adaptive = false;

    profile.scaling_method = ls::ScalingMethod::Native;
    const auto nativeBaseline = selectSpatialScalingExtents(
        profile, fixedCapabilities(1280, 800)
    );
    expect(ls::spatialScalingRequested(profile) && nativeBaseline &&
            sameExtent(nativeBaseline->source, deck->source),
        "Native Resolution must retain the model-free reconstruction lane for live switching");
    profile.scaling_method = ls::ScalingMethod::Mako;

    profile.scaling_factor = 2.0F;
    const auto odd = selectSpatialScalingExtents(
        profile, fixedCapabilities(321, 181)
    );
    expect(odd && odd->source.width == 160 && odd->source.height == 90,
        "Odd presentation extents must round down safely at 2x");

    auto variable = fixedCapabilities(UINT32_MAX, UINT32_MAX);
    expect(!virtualizeSurfaceCapabilities(profile, variable),
        "Variable desktop surfaces must fail closed");
    expect(variable.currentExtent.width == UINT32_MAX &&
            variable.currentExtent.height == UINT32_MAX,
        "A failed policy must not mutate surface capabilities");

    profile.scaling_enabled = false;
    expect(!selectSpatialScalingExtents(
            profile, fixedCapabilities(1920, 1080)),
        "Disabled scaling must be a zero-work policy");

    profile.scaling_enabled = true;
    profile.scaling_factor = 1.0F;
    expect(!selectSpatialScalingExtents(
            profile, fixedCapabilities(1920, 1080)),
        "A 1x factor must bypass spatial scaling");

    profile.scaling_factor = 1.5F;
    const auto real = fixedCapabilities(1920, 1080);
    const FixedSurfaceScalingContract fixedContract{
        .extents = {
            .source = {1280, 720},
            .presentation = {1920, 1080},
        },
        .factor = 1.5F,
        .policyRevision = 7,
        .queryGeneration = 11,
    };
    FixedSurfaceCapabilityRelaySlot capabilityRelay;
    const auto lowerPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(0x1);
    const auto otherPhysicalDevice = reinterpret_cast<VkPhysicalDevice>(0x2);
    const auto lowerSurface = reinterpret_cast<VkSurfaceKHR>(0x10);
    const auto aliasedUpperSurface = reinterpret_cast<VkSurfaceKHR>(0x20);
    static_cast<void>(aliasedUpperSurface);
    capabilityRelay.begin();
    capabilityRelay.publish(
        lowerPhysicalDevice, lowerSurface, fixedContract
    );
    const auto relayedAcrossAliasedSurface = capabilityRelay.consume(
        lowerPhysicalDevice
    );
    expect(relayedAcrossAliasedSurface &&
            lowerSurface != aliasedUpperSurface &&
            relayedAcrossAliasedSurface->lowerSurface == lowerSurface &&
            relayedAcrossAliasedSurface->contract.queryGeneration == 11,
        "A bracketed capability relay must preserve the lower contract when Gamescope aliases the upper surface handle");
    expect(!capabilityRelay.consume(lowerPhysicalDevice),
        "A capability relay contract must be consumable exactly once");
    capabilityRelay.publish(
        lowerPhysicalDevice, lowerSurface, fixedContract
    );
    expect(!capabilityRelay.consume(otherPhysicalDevice) &&
            !capabilityRelay.consume(lowerPhysicalDevice),
        "A physical-device mismatch must fail closed and clear the capability relay");
    capabilityRelay.publish(
        lowerPhysicalDevice, lowerSurface, fixedContract
    );
    capabilityRelay.begin();
    expect(!capabilityRelay.consume(lowerPhysicalDevice),
        "Beginning a capability query must clear any stale relay contract");
    expect(classifySpatialCreateRelay(
            fixedContract, fixedContract.extents.source
        ) == SpatialCreateRelayDecision::Split,
        "A fixed-surface create relay must accept the lower DSO's nonzero query generation");
    const FixedSurfaceScalingContract nativeCreateDecision{
        .extents = {
            .source = fixedContract.extents.presentation,
            .presentation = fixedContract.extents.presentation,
        },
        .factor = 1.5F,
        .policyRevision = 8,
        .queryGeneration = 12,
        .variableSurface = true,
        .inactiveReason = SpatialScalingInactiveReason::
            GamescopePresentationTargetNoHeadroom,
    };
    expect(classifySpatialCreateRelay(
            nativeCreateDecision, nativeCreateDecision.extents.source
        ) == SpatialCreateRelayDecision::Native,
        "An application native-extent override must remain a valid explicit lower create decision");
    capabilityRelay.publish(
        lowerPhysicalDevice, lowerSurface, nativeCreateDecision
    );
    const auto relayedNativeDecision = capabilityRelay.consume(
        lowerPhysicalDevice
    );
    expect(relayedNativeDecision &&
            relayedNativeDecision->contract.variableSurface &&
            relayedNativeDecision->contract.inactiveReason ==
                SpatialScalingInactiveReason::
                    GamescopePresentationTargetNoHeadroom,
        "A native lower create relay must preserve its variable-surface class and exact inactive reason");
    const FixedSurfaceScalingContract memoryConstrainedRelayContract{
        .extents = {
            .source = {2560, 1440},
            .presentation = {3840, 2160},
        },
        .factor = 2.0F,
        .spatialSurfaceScalingSupported = true,
        .variableSurface = true,
        .memoryBudgetConstrained = true,
    };
    const auto relayMetadata = fixedSurfaceScalingContractRelayMetadata(
        memoryConstrainedRelayContract
    );
    FixedSurfaceScalingContract roundTrippedRelayContract;
    expect(applyFixedSurfaceScalingContractRelayMetadata(
                roundTrippedRelayContract, relayMetadata
            ) &&
            roundTrippedRelayContract.spatialSurfaceScalingSupported &&
            roundTrippedRelayContract.variableSurface &&
            roundTrippedRelayContract.memoryBudgetConstrained &&
            roundTrippedRelayContract.inactiveReason ==
                SpatialScalingInactiveReason::None,
        "The cross-DSO create relay must preserve variable-surface memory-constraint metadata");
    auto invalidRelayMetadata = relayMetadata;
    invalidRelayMetadata.inactiveReason = UINT32_MAX;
    expect(!applyFixedSurfaceScalingContractRelayMetadata(
                roundTrippedRelayContract, invalidRelayMetadata
            ),
        "The cross-DSO create relay must reject an unknown inactive reason");
    const FixedSurfaceScalingContract liveFactorCreateDecision{
        .extents = {
            .source = {1706, 960},
            .presentation = {2560, 1440},
        },
        .factor = 1.5F,
        .policyRevision = 21,
        .queryGeneration = 19,
    };
    expect(classifySpatialCreateRelay(
            liveFactorCreateDecision, {1706, 960}
        ) == SpatialCreateRelayDecision::Split,
        "A guarded live-factor replacement must accept the exact lower fixed-surface decision");
    expect(classifySpatialCreateRelay(
            liveFactorCreateDecision, {1422, 800}
        ) == SpatialCreateRelayDecision::Unavailable,
        "A stale or mismatched lower create decision must still fail closed");
    expect(awaitLowerSpatialCreateRelay(true, true, false, false, true),
        "An upper fixed-surface create without a capability contract must reach the lower one-shot decision");
    expect(!awaitLowerSpatialCreateRelay(true, true, true, false, true) &&
            !awaitLowerSpatialCreateRelay(true, true, false, true, true) &&
            !awaitLowerSpatialCreateRelay(true, false, false, false, true) &&
            !awaitLowerSpatialCreateRelay(true, true, false, false, false),
        "Only the upper owner's unresolved fixed-surface create may await the lower relay");
    auto relayedSourceCapabilities = real;
    relayedSourceCapabilities.currentExtent = fixedContract.extents.source;
    expect(prepareFixedSurfaceCapabilityRelay(
            relayedSourceCapabilities, fixedContract
        ) && sameExtent(
            relayedSourceCapabilities.currentExtent,
            fixedContract.extents.presentation
        ),
        "a split relay must restore an already-virtualized capability only "
        "for its upper policy calculation");
    auto restoredNativeCapabilities = real;
    expect(prepareFixedSurfaceCapabilityRelay(
            restoredNativeCapabilities, fixedContract
        ) && sameExtent(
            restoredNativeCapabilities.currentExtent,
            fixedContract.extents.presentation
        ),
        "a split relay must preserve a WSI-restored native capability");
    auto unrelatedRelayCapabilities = real;
    unrelatedRelayCapabilities.currentExtent = {1600, 900};
    expect(!prepareFixedSurfaceCapabilityRelay(
            unrelatedRelayCapabilities, fixedContract
        ),
        "a split relay must fail closed for an extent outside the lower contract");
    const auto matchingFixed = scalingDecisionForCreate(
        profile, true, 7, real, {1280, 720}, std::nullopt, fixedContract
    );
    expect(matchingFixed.extents.has_value() &&
            matchingFixed.fixedContract &&
            matchingFixed.fixedContract->queryGeneration == 11 &&
            matchingFixed.inactiveReason ==
                SpatialScalingInactiveReason::None,
        "A create request matching the advertised source extent must scale");
    const auto nativeOverride = scalingDecisionForCreate(
        profile, true, 7, real, {1920, 1080}, std::nullopt, fixedContract
    );
    expect(!nativeOverride.extents &&
            nativeOverride.inactiveReason ==
                SpatialScalingInactiveReason::ApplicationExtentOverrideNoSplit,
        "A native application extent override must remain application-owned");
    const auto customOverride = scalingDecisionForCreate(
        profile, true, 7, real, {1600, 900}, std::nullopt, fixedContract
    );
    expect(!customOverride.extents &&
            customOverride.inactiveReason ==
                SpatialScalingInactiveReason::ApplicationExtentMismatch,
        "An application extent override must remain application-owned");
    const FixedSurfaceScalingContract changedFactorContract{
        .extents = {
            .source = {1536, 864},
            .presentation = {1920, 1080},
        },
        .factor = 1.25F,
        .policyRevision = 8,
        .queryGeneration = 12,
    };
    profile.scaling_factor = 1.25F;
    const auto retainedFixedSource = scalingDecisionForCreate(
        profile, true, 8, real, {1280, 720},
        SpatialScalingExtents{
            .source = {1280, 720},
            .presentation = {1920, 1080},
        },
        changedFactorContract
    );
    expect(retainedFixedSource.extents &&
            retainedFixedSource.retainedPreviousFixedSource &&
            sameExtent(retainedFixedSource.extents->source, {1280, 720}) &&
            sameExtent(
                retainedFixedSource.extents->presentation, {1920, 1080}
            ),
        "A live fixed-surface factor change must retain the exact proven prior split while WSI keeps its source");
    const auto unprovenChangedFactorRequest = scalingDecisionForCreate(
        profile, true, 8, real, {1400, 800},
        SpatialScalingExtents{
            .source = {1280, 720},
            .presentation = {1920, 1080},
        },
        changedFactorContract
    );
    expect(!unprovenChangedFactorRequest.extents &&
            !unprovenChangedFactorRequest.retainedPreviousFixedSource &&
            unprovenChangedFactorRequest.inactiveReason ==
                SpatialScalingInactiveReason::ApplicationExtentMismatch,
        "A fixed-surface factor change must reject an unproven source extent");
    auto changedPresentation = real;
    changedPresentation.currentExtent = {2560, 1440};
    const auto changedPresentationRequest = scalingDecisionForCreate(
        profile, true, 8, changedPresentation, {1280, 720},
        SpatialScalingExtents{
            .source = {1280, 720},
            .presentation = {1920, 1080},
        },
        FixedSurfaceScalingContract{
            .extents = {
                .source = {2048, 1152},
                .presentation = {2560, 1440},
            },
            .factor = 1.25F,
            .policyRevision = 8,
            .queryGeneration = 13,
        }
    );
    expect(!changedPresentationRequest.extents &&
            !changedPresentationRequest.retainedPreviousFixedSource,
        "A previous split from a different presentation extent must not be retained");
    profile.scaling_factor = 1.5F;
    const auto missingContract = scalingDecisionForCreate(
        profile, true, 7, real, {1280, 720}
    );
    expect(!missingContract.extents &&
            missingContract.inactiveReason == SpatialScalingInactiveReason::
                NoFixedCapabilityContract,
        "A fixed source request without a capability contract must fail closed");
    const auto blockedProcess = scalingDecisionForCreate(
        profile, false, 7, real, {1280, 720}, std::nullopt, fixedContract
    );
    expect(!blockedProcess.extents && blockedProcess.inactiveReason ==
            SpatialScalingInactiveReason::ProcessUnsupported,
        "A process outside the SDR scaling boundary must fail closed");

    profile.scaling_sharpness = 0.75F;
    profile.scaling_method = ls::ScalingMethod::Ls1;
    expect(scalingDecisionForCreate(
            profile, true, 7, real, {1280, 720}, std::nullopt,
            fixedContract).extents.has_value(),
        "Method and sharpness recreations must reuse an unchanged extent contract");

    auto previouslyAdvertisedCapabilities = real;
    const auto previouslyAdvertised = virtualizeSurfaceCapabilities(
        SpatialScalingPolicy{.enabled = true, .factor = 1.5F},
        previouslyAdvertisedCapabilities
    );
    expect(previouslyAdvertised && sameExtent(
            previouslyAdvertisedCapabilities.currentExtent,
            previouslyAdvertised->source),
        "A fixed-surface query must advertise the selected source extent");
    profile.scaling_factor = 2.0F;
    const auto changedPolicy = scalingDecisionForCreate(
        profile, true, 8, real,
        previouslyAdvertisedCapabilities.currentExtent,
        std::nullopt, fixedContract
    );
    expect(!changedPolicy.extents && changedPolicy.inactiveReason ==
            SpatialScalingInactiveReason::PolicyChangedAfterCapabilityQuery,
        "A stale fixed-surface request must not match a changed scaling policy");
    profile.scaling_enabled = false;
    expect(!scalingDecisionForCreate(
            profile, true, 9, real,
            previouslyAdvertisedCapabilities.currentExtent,
            std::nullopt, fixedContract).extents,
        "A fixed-surface request must not scale after the policy is disabled");
    profile.scaling_enabled = true;
    profile.scaling_factor = 1.5F;
    auto resizedSurface = real;
    resizedSurface.currentExtent = {2560, 1440};
    const auto surfaceDrift = scalingDecisionForCreate(
        profile, true, 7, resizedSurface, {1280, 720}, std::nullopt,
        fixedContract
    );
    expect(!surfaceDrift.extents && surfaceDrift.inactiveReason ==
            SpatialScalingInactiveReason::SurfaceChangedAfterCapabilityQuery,
        "A changed fixed presentation extent must invalidate the old contract");
    expect(std::string_view(spatialScalingInactiveReasonName(
            SpatialScalingInactiveReason::ApplicationExtentOverrideNoSplit)) ==
            "application-extent-override-no-source-presentation-split" &&
            std::string_view(spatialScalingInactiveReasonName(
                SpatialScalingInactiveReason::SwapchainFormatUnsupported)) ==
            "swapchain-format-unsupported" &&
            std::string_view(spatialScalingInactiveReasonName(
                SpatialScalingInactiveReason::GamescopeWsiSurfaceUnproven)) ==
            "gamescope-wsi-surface-unproven" &&
            std::string_view(spatialScalingInactiveReasonName(
                SpatialScalingInactiveReason::
                    GamescopePresentationTargetUnavailable)) ==
            "gamescope-presentation-target-unavailable" &&
            std::string_view(spatialScalingInactiveReasonName(
                SpatialScalingInactiveReason::
                    GamescopePresentationTargetNoHeadroom)) ==
            "gamescope-presentation-target-no-headroom",
        "Inactive reason diagnostics must retain stable machine-readable names");

    VkSurfaceCapabilitiesKHR variableCreate{
        .currentExtent = {UINT32_MAX, UINT32_MAX},
        .minImageExtent = {16, 16},
        .maxImageExtent = {1280, 800},
    };
    const auto customFactor = scalingExtentsForCreate(
        profile, variableCreate, {640, 400}
    );
    expect(customFactor &&
            sameExtent(customFactor->source, {640, 400}) &&
            sameExtent(customFactor->presentation, {960, 600}),
        "Variable surfaces must preserve the requested source and scale the lower WSI extent");
    expect(variableSurfaceScalingFeedbackDetected(
            profile, variableCreate, {960, 600}, customFactor),
        "A compositor-echoed variable presentation extent must be detected");
    expect(!scalingExtentsForCreate(
            profile, variableCreate, {960, 600}, customFactor),
        "A compositor-echoed extent must not compound the scale factor");
    expect(scalingDecisionForCreate(
            profile, true, 7, variableCreate, {960, 600}, customFactor
        ).inactiveReason == SpatialScalingInactiveReason::
            VariableSurfaceFeedback,
        "Variable compositor feedback must expose a stable inactive reason");
    expect(!variableSurfaceScalingFeedbackDetected(
            profile, variableCreate, {800, 500}, customFactor) &&
            scalingExtentsForCreate(
                profile, variableCreate, {800, 500}, customFactor
            ).has_value(),
        "A genuinely different variable source request must remain scalable");
    const auto fixedFeedbackCapabilities = fixedCapabilities(960, 600);
    expect(!variableSurfaceScalingFeedbackDetected(
            profile, fixedFeedbackCapabilities, {960, 600}, customFactor),
        "Fixed surfaces must not enter the variable feedback policy");

    VkSurfaceCapabilitiesKHR managedVariableCreate{
        .currentExtent = {UINT32_MAX, UINT32_MAX},
        .minImageExtent = {16, 16},
        .maxImageExtent = {8192, 8192},
    };
    const SpatialScalingExtents previousManagedExtents{
        .source = {1280, 720},
        .presentation = {1920, 1080},
    };
    expect(!variableSurfaceScalingFeedbackDetected(
            profile, managedVariableCreate, {1920, 1080},
            previousManagedExtents, true),
        "A managed Gamescope create relay must distinguish a real resolution change from uncontracted feedback");
    const auto managedPreviousPresentationRequest = scalingDecisionForCreate(
        profile, true, 7, managedVariableCreate, {1920, 1080},
        previousManagedExtents, std::nullopt, std::nullopt,
        VkExtent2D{3840, 2160}, true
    );
    expect(managedPreviousPresentationRequest.extents &&
            sameExtent(
                managedPreviousPresentationRequest.extents->source,
                {1920, 1080}
            ) &&
            sameExtent(
                managedPreviousPresentationRequest.extents->presentation,
                {2880, 1620}
            ) &&
            managedPreviousPresentationRequest.inactiveReason ==
                SpatialScalingInactiveReason::None,
        "A managed 720p-to-1080p game resolution change must remain scalable when the new source equals the previous presentation");
    const FixedSurfaceScalingContract managedChangedResolutionContract{
        .extents = *managedPreviousPresentationRequest.extents,
        .factor = profile.scaling_factor,
        .policyRevision = 7,
    };
    expect(classifySpatialCreateRelay(
            managedChangedResolutionContract, {1920, 1080}
        ) == SpatialCreateRelayDecision::Split,
        "The upper role must accept the exact managed changed-resolution create relay");
    expect(classifySpatialCreateRelay(
            managedChangedResolutionContract, {1280, 720}
        ) == SpatialCreateRelayDecision::Unavailable,
        "A lower-only Gamescope extent mutation must still fail the upper create relay closed");

    const auto retainedFeedback = committedVariableSurfaceScalingExtents(
        customFactor, true, true, false, true, false,
        {960, 600}, {960, 600}
    );
    expect(retainedFeedback &&
            sameExtent(retainedFeedback->source, customFactor->source) &&
            sameExtent(
                retainedFeedback->presentation, customFactor->presentation
            ),
        "A successful native feedback-guard recreation must retain the prior pair");
    const auto replacement = committedVariableSurfaceScalingExtents(
        retainedFeedback, true, true, true, false, false,
        {800, 500}, {1200, 750}
    );
    expect(replacement && sameExtent(replacement->source, {800, 500}) &&
            sameExtent(replacement->presentation, {1200, 750}),
        "A genuinely different successful source must replace prior feedback state");
    expect(!committedVariableSurfaceScalingExtents(
            replacement, true, true, false, false, false,
            {1200, 750}, {1200, 750}),
        "A successful unsuppressed native recreation must clear variable state");
    expect(!committedVariableSurfaceScalingExtents(
            replacement, true, false, false, false, false,
            {1200, 750}, {1200, 750}),
        "A successful fixed-surface recreation must clear variable state");
    expect(!committedVariableSurfaceScalingExtents(
            replacement, false, true, false, false, false,
            {1200, 750}, {1200, 750}),
        "An inactive profile must clear variable feedback state");

    const SpatialScalingExtents sameSourceFourK{
        .source = {1920, 1080},
        .presentation = {3840, 2160},
    };
    const auto factorDownshiftRollback =
        committedVariableSurfaceRollbackExtents(
            sameSourceFourK, std::nullopt, true, true, true, false, false, false,
            {1920, 1080}, {2880, 1620}
        );
    expect(factorDownshiftRollback && sameExtent(
            factorDownshiftRollback->presentation, {3840, 2160}),
        "A successful same-source factor downshift must retain one rollback envelope");
    const auto duplicateDownshiftCommit =
        committedVariableSurfaceRollbackExtents(
            SpatialScalingExtents{
                .source = {1920, 1080},
                .presentation = {2880, 1620},
            },
            factorDownshiftRollback, true, true, true, false, false, false,
            {1920, 1080}, {2880, 1620}
        );
    expect(duplicateDownshiftCommit && sameExtent(
            duplicateDownshiftCommit->presentation, {3840, 2160}),
        "The second split role's duplicate commit must retain the rollback envelope");
    expect(!committedVariableSurfaceRollbackExtents(
            SpatialScalingExtents{
                .source = {1920, 1080},
                .presentation = {2880, 1620},
            },
            duplicateDownshiftCommit, true, true, true, false, false, false,
            {1920, 1080}, {3840, 2160}),
        "Reaching the proven maximum must consume the rollback envelope");
    expect(!committedVariableSurfaceRollbackExtents(
            sameSourceFourK, std::nullopt, true, true, true, false, false, true,
            {1920, 1080}, {2880, 1620}),
        "A memory-forced shrink must not create a future admission bypass");

    const SpatialScalingExtents sameSourceIntermediate{
        .source = {1920, 1080},
        .presentation = {3072, 1728},
    };
    const auto nativeResolutionCurrent =
        committedVariableSurfaceScalingExtents(
            sameSourceIntermediate, true, true, false, false, true,
            {1920, 1080}, {1920, 1080}
        );
    expect(nativeResolutionCurrent && sameExtent(
            nativeResolutionCurrent->presentation, {3072, 1728}),
        "Native Resolution must retain the same-source current envelope for the next live factor edit");
    const auto nativeResolutionRollback =
        committedVariableSurfaceRollbackExtents(
            sameSourceIntermediate, factorDownshiftRollback,
            true, true, false, false, true, false,
            {1920, 1080}, {1920, 1080}
        );
    expect(nativeResolutionRollback && sameExtent(
            nativeResolutionRollback->presentation, {3840, 2160}),
        "Native Resolution must retain the same-source proven maximum while live memory accounting settles");
    expect(!committedVariableSurfaceScalingExtents(
            sameSourceIntermediate, true, true, false, false, true,
            {2560, 1440}, {2560, 1440}),
        "Native Resolution must not carry an envelope across a source change");
    expect(!committedVariableSurfaceRollbackExtents(
            sameSourceIntermediate, factorDownshiftRollback,
            true, true, false, false, true, false,
            {2560, 1440}, {2560, 1440}),
        "Native Resolution must not carry rollback proof across a source change");
    const auto intermediateUpshiftRollback =
        committedVariableSurfaceRollbackExtents(
            SpatialScalingExtents{
                .source = {1920, 1080},
                .presentation = {2880, 1620},
            },
            factorDownshiftRollback, true, true, true, false, false, false,
            {1920, 1080}, {3072, 1728}
        );
    expect(intermediateUpshiftRollback && sameExtent(
            intermediateUpshiftRollback->presentation, {3840, 2160}),
        "An intermediate factor step must retain the proven maximum until it is reached");

    profile.scaling_factor = 2.0F;
    const auto clampedCustomFactor = scalingExtentsForCreate(
        profile, variableCreate, {800, 600}
    );
    expect(clampedCustomFactor &&
            sameExtent(clampedCustomFactor->presentation, {1066, 800}),
        "Variable-surface scaling must preserve aspect ratio at the WSI maximum");

    expect(!scalingExtentsForCreate(
            profile, variableCreate, {1280, 800}),
        "A variable surface with no enlargement headroom must fail closed");
    expect(scalingDecisionForCreate(
            profile, true, 7, variableCreate, {1280, 800}
        ).inactiveReason == SpatialScalingInactiveReason::
            VariableSurfaceNoHeadroom,
        "Variable maximum-extent exhaustion must expose a stable inactive reason");

    VkSurfaceCapabilitiesKHR largeVariableCreate{
        .currentExtent = {UINT32_MAX, UINT32_MAX},
        .minImageExtent = {16, 16},
        .maxImageExtent = {8192, 8192},
    };
    profile.scaling_factor = 1.8F;
    const auto unboundedVariablePresentation = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1280, 720}
    );
    expect(unboundedVariablePresentation.extents && sameExtent(
            unboundedVariablePresentation.extents->presentation,
            {2302, 1294}
        ),
        "A standalone variable surface without a proven output must retain the requested factor");
    const auto missingRequiredGamescopeTarget = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1280, 720},
        std::nullopt, std::nullopt, std::nullopt, std::nullopt, true
    );
    expect(!missingRequiredGamescopeTarget.extents &&
            missingRequiredGamescopeTarget.inactiveReason ==
                SpatialScalingInactiveReason::
                    GamescopePresentationTargetUnavailable,
        "A managed variable Gamescope surface without a proven target must fail closed");
    const auto deckNoHeadroom = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1280, 720},
        std::nullopt, std::nullopt, std::nullopt,
        VkExtent2D{1280, 800}, true
    );
    expect(!deckNoHeadroom.extents &&
            deckNoHeadroom.gamescopePresentationTargetConstrained &&
            deckNoHeadroom.inactiveReason == SpatialScalingInactiveReason::
                GamescopePresentationTargetNoHeadroom,
        "A Deck 720p source must not supersample past its 1280x800 output");
    const auto deckConstrained = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {960, 540},
        std::nullopt, std::nullopt, std::nullopt,
        VkExtent2D{1280, 800}, true
    );
    expect(deckConstrained.extents &&
            deckConstrained.gamescopePresentationTargetConstrained &&
            sameExtent(deckConstrained.extents->source, {960, 540}) &&
            sameExtent(deckConstrained.extents->presentation, {1280, 720}),
        "A Deck variable surface must aspect-fit scaling into the real output ceiling");
    profile.scaling_supersampling = true;
    const auto deckSupersampled = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {960, 540},
        std::nullopt, std::nullopt, std::nullopt,
        VkExtent2D{1280, 800}, true
    );
    expect(deckSupersampled.extents &&
            !deckSupersampled.gamescopePresentationTargetConstrained &&
            deckSupersampled.gamescopePresentationTargetBypassed &&
            sameExtent(
                deckSupersampled.extents->presentation, {1726, 970}
            ),
        "Quality supersampling must deliberately exceed a proven Deck output target");
    expect(!variableSurfaceSupersamplingChangePreservesEffectiveExtents(
            true, true, {960, 540}, {1280, 720}, 1.8F,
            false, true, VkExtent2D{1280, 800}),
        "Enabling supersampling above a clamped target must require an extent transition");
    expect(!variableSurfaceSupersamplingChangePreservesEffectiveExtents(
            true, false, {1280, 720}, {1280, 720}, 1.8F,
            false, true, VkExtent2D{1280, 800}),
        "Enabling supersampling on a native variable surface must request the extent transition which can activate scaling");
    expect(variableSurfaceSupersamplingChangePreservesEffectiveExtents(
            false, true, {960, 540}, {1280, 720}, 1.8F,
            false, true, VkExtent2D{1280, 800}),
        "A fixed surface must treat supersampling as a live no-op");
    expect(std::string_view(spatialScalingRuntimeInactiveReason(
            false,
            true,
            1.5F,
            SpatialScalingInactiveReason::VariableSurfaceMemoryBudget
        )) == "variable-surface-memory-budget" &&
            spatialScalingRuntimeInactiveReason(
                true,
                true,
                1.5F,
                SpatialScalingInactiveReason::VariableSurfaceMemoryBudget
            ) == nullptr &&
            spatialScalingRuntimeInactiveReason(
                false, true, 1.5F, SpatialScalingInactiveReason::None
            ) == nullptr &&
            spatialScalingRuntimeInactiveReason(
                false, false,
                1.5F,
                SpatialScalingInactiveReason::VariableSurfaceMemoryBudget
            ) == nullptr &&
            std::string_view(spatialScalingRuntimeInactiveReason(
                false, true, 1.0F,
                SpatialScalingInactiveReason::VariableSurfaceMemoryBudget
            )) == "factor-not-upscaling",
        "Runtime status must expose an inactive policy reason only while spatial scaling is requested and inactive");
    profile.scaling_supersampling = false;
    const auto deckBelowCeiling = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {640, 360},
        std::nullopt, std::nullopt, std::nullopt,
        VkExtent2D{1280, 800}, true
    );
    expect(deckBelowCeiling.extents &&
            !deckBelowCeiling.gamescopePresentationTargetConstrained &&
            sameExtent(deckBelowCeiling.extents->presentation, {1150, 646}),
        "A requested factor below the Deck output ceiling must remain unchanged");
    const auto dockedConstrained = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1280, 720},
        std::nullopt, std::nullopt, std::nullopt,
        VkExtent2D{1920, 1080}, true
    );
    expect(dockedConstrained.extents &&
            dockedConstrained.gamescopePresentationTargetConstrained &&
            sameExtent(
                dockedConstrained.extents->presentation, {1920, 1080}
            ),
        "A docked target must cap scaling without changing source aspect ratio");
    expect(variableSurfaceFactorChangePreservesEffectiveExtents(
            true, true, {960, 540}, {1280, 720}, 1.8F, 1.5F),
        "A live factor edit that still reaches the Gamescope ceiling must avoid redundant reconstruction");
    profile.scaling_factor = 2.0F;
    constexpr VkDeviceSize gibibyte = uint64_t{1024} * 1024 * 1024;
    const auto twoGiB = memoryProperties(2 * gibibyte);
    const auto eightGiB = memoryProperties(8 * gibibyte);
    const auto twentyFourGiB = memoryProperties(24 * gibibyte);
    expect(largestDeviceLocalHeapBytes(eightGiB) == 8 * gibibyte,
        "The allocation envelope must use the largest device-local heap");
    expect(variablePresentationPixelBudget(twoGiB) ==
            minimumVariablePresentationPixels,
        "Low-memory devices must retain the baseline 4K presentation tier");
    expect(variablePresentationPixelBudget(eightGiB) == 11'184'810,
        "An 8 GiB heap must receive the conservative variable-presentation budget");
    expect(variablePresentationPixelBudget(twentyFourGiB) == 33'554'432,
        "A 24 GiB heap must admit the 8K presentation tier");
    expect(variablePresentationPixelBudget(
            VkPhysicalDeviceMemoryProperties{}) == 0,
        "A device without a local heap must fail the allocation envelope closed");

    VkPhysicalDeviceMemoryBudgetPropertiesEXT ampleLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    ampleLiveBudget.heapBudget[0] = 7 * gibibyte;
    ampleLiveBudget.heapUsage[0] = 1 * gibibyte;
    const auto ampleAdmission = variablePresentationMemoryAdmission(
        eightGiB, &ampleLiveBudget
    );
    expect(ampleAdmission.livePixelBudget &&
            ampleAdmission.effectivePixelBudget ==
                ampleAdmission.staticPixelBudget &&
            ampleAdmission.heapBudgetBytes == 7 * gibibyte &&
            ampleAdmission.heapUsageBytes == 1 * gibibyte,
        "Ample live headroom must retain the conservative static ceiling");

    VkPhysicalDeviceMemoryBudgetPropertiesEXT fourGiBLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    fourGiBLiveBudget.heapBudget[0] = 4 * gibibyte;
    fourGiBLiveBudget.heapUsage[0] = 1 * gibibyte;
    const auto fourGiBAdmission = variablePresentationMemoryAdmission(
        eightGiB, &fourGiBLiveBudget
    );
    VkPhysicalDeviceMemoryBudgetPropertiesEXT fiveGiBLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    fiveGiBLiveBudget.heapBudget[0] = 5 * gibibyte;
    fiveGiBLiveBudget.heapUsage[0] = 1 * gibibyte;
    const auto fiveGiBAdmission = variablePresentationMemoryAdmission(
        eightGiB, &fiveGiBLiveBudget
    );
    VkPhysicalDeviceMemoryBudgetPropertiesEXT deckObservedLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    deckObservedLiveBudget.heapBudget[0] = 6561 * uint64_t{1024} * 1024;
    deckObservedLiveBudget.heapUsage[0] = 1 * gibibyte;
    const auto deckObservedAdmission = variablePresentationMemoryAdmission(
        eightGiB, &deckObservedLiveBudget
    );
    VkPhysicalDeviceMemoryBudgetPropertiesEXT eightGiBLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    eightGiBLiveBudget.heapBudget[0] = 8 * gibibyte;
    eightGiBLiveBudget.heapUsage[0] = 1 * gibibyte;
    const auto eightGiBAdmission = variablePresentationMemoryAdmission(
        eightGiB, &eightGiBLiveBudget
    );
    expect(fourGiBAdmission.reservedHeadroomBytes ==
                minimumVariablePresentationLiveReserve &&
            fiveGiBAdmission.reservedHeadroomBytes ==
                minimumVariablePresentationLiveReserve &&
            deckObservedAdmission.reservedHeadroomBytes ==
                deckObservedLiveBudget.heapBudget[0] /
                    variablePresentationLiveReserveDivisor &&
            eightGiBAdmission.reservedHeadroomBytes ==
                eightGiBLiveBudget.heapBudget[0] /
                    variablePresentationLiveReserveDivisor,
        "Deck-shaped live budgets must use the 512 MiB floor through 5 GiB and the 10% reserve above it");
    expect(fourGiBAdmission.livePixelBudget &&
            fiveGiBAdmission.livePixelBudget &&
            deckObservedAdmission.livePixelBudget &&
            eightGiBAdmission.livePixelBudget &&
            *fourGiBAdmission.livePixelBudget <
                *fiveGiBAdmission.livePixelBudget &&
            *fiveGiBAdmission.livePixelBudget <
                *deckObservedAdmission.livePixelBudget &&
            *deckObservedAdmission.livePixelBudget <
                *eightGiBAdmission.livePixelBudget,
        "Increasing Deck-shaped driver budgets must monotonically increase live admission at equal usage");

    VkPhysicalDeviceMemoryBudgetPropertiesEXT pressuredLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    pressuredLiveBudget.heapBudget[0] = 7 * gibibyte;
    pressuredLiveBudget.heapUsage[0] = 4 * gibibyte;
    const auto pressuredAdmission = variablePresentationMemoryAdmission(
        eightGiB, &pressuredLiveBudget
    );
    expect(pressuredAdmission.livePixelBudget &&
            pressuredAdmission.effectivePixelBudget ==
                *pressuredAdmission.livePixelBudget &&
            pressuredAdmission.effectivePixelBudget <
                pressuredAdmission.staticPixelBudget &&
            pressuredAdmission.effectivePixelBudget >=
                minimumVariablePresentationPixels,
        "Current driver pressure must tighten the static allocation ceiling without discarding a safe 4K envelope");

    VkPhysicalDeviceMemoryBudgetPropertiesEXT exhaustedLiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    exhaustedLiveBudget.heapBudget[0] = 7 * gibibyte;
    exhaustedLiveBudget.heapUsage[0] = 6 * gibibyte;
    const auto exhaustedAdmission = variablePresentationMemoryAdmission(
        eightGiB, &exhaustedLiveBudget
    );
    expect(exhaustedAdmission.livePixelBudget &&
            exhaustedAdmission.effectivePixelBudget <
                minimumVariablePresentationPixels,
        "A live budget under pressure must not inherit the static 4K floor");
    const auto exhaustedResourceAdmission =
        variablePresentationResourceAdmission(
            exhaustedAdmission,
            {1920, 1080},
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            6,
            2
        );
    expect(exhaustedResourceAdmission.livePixelBudget &&
            exhaustedResourceAdmission.effectivePixelBudget <
                minimumVariablePresentationPixels,
        "The resource-graph admission must still reject 4K when the post-reserve heap cannot hold its WSI, scaler, and FG resources");
    const auto fourKUnderPressure = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1920, 1080},
        std::nullopt, std::nullopt,
        exhaustedResourceAdmission.effectivePixelBudget
    );
    expect(!fourKUnderPressure.extents &&
            fourKUnderPressure.inactiveReason ==
                SpatialScalingInactiveReason::VariableSurfaceMemoryBudget,
        "A normally supported 4K envelope must fail closed when the live driver budget cannot admit it");

    constexpr VkDeviceSize mebibyte = uint64_t{1024} * 1024;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT observedRe4LiveBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    observedRe4LiveBudget.heapBudget[0] = 6561 * mebibyte;
    observedRe4LiveBudget.heapUsage[0] = 5086 * mebibyte;
    const auto observedRe4MemoryAdmission =
        variablePresentationMemoryAdmission(
            eightGiB, &observedRe4LiveBudget
        );
    const auto observedRe4ResourceAdmission =
        variablePresentationResourceAdmission(
            observedRe4MemoryAdmission,
            {2560, 1440},
            VK_FORMAT_A2B10G10R10_UNORM_PACK32,
            6,
            2
        );
    expect(observedRe4ResourceAdmission.fixedSourceBytes ==
            variablePresentationFixedAllocationBytes +
                uint64_t{2560} * 1440 * 96 &&
            observedRe4ResourceAdmission.presentationBytesPerPixel == 32 &&
            observedRe4ResourceAdmission.effectivePixelBudget >=
                minimumVariablePresentationPixels,
        "The measured RE4 1440p resource graph must admit a 4K packed-10-bit WSI replacement inside the driver's post-reserve headroom");
    profile.scaling_factor = 1.5F;
    const auto observedRe4FourK = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {2560, 1440},
        std::nullopt, std::nullopt,
        observedRe4ResourceAdmission.effectivePixelBudget,
        VkExtent2D{3840, 2160}, true,
        observedRe4MemoryAdmission.staticPixelBudget
    );
    expect(observedRe4FourK.extents &&
            sameExtent(
                observedRe4FourK.extents->presentation, {3840, 2160}
            ) &&
            !observedRe4FourK.memoryBudgetConstrained,
        "A late 1440p-to-4K activation must not depend on whether the game allocated its steady-state memory before the factor changed");
    const auto expensiveRe4ResourceAdmission =
        variablePresentationResourceAdmission(
            observedRe4MemoryAdmission,
            {2560, 1440},
            VK_FORMAT_R16G16B16A16_SFLOAT,
            6,
            4
        );
    expect(expensiveRe4ResourceAdmission.livePixelBudget &&
            expensiveRe4ResourceAdmission.effectivePixelBudget <
                minimumVariablePresentationPixels,
        "The same live headroom must remain sensitive to a 64-bit WSI and the maximum generated-frame capacity");
    profile.scaling_factor = 2.0F;
    const SpatialScalingExtents provenFourK{
        .source = {2560, 1440},
        .presentation = {3840, 2160},
    };
    const auto raisedFactorRetainsProvenFourK = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {2560, 1440},
        provenFourK, std::nullopt,
        exhaustedAdmission.effectivePixelBudget,
        std::nullopt, false, exhaustedAdmission.staticPixelBudget
    );
    expect(raisedFactorRetainsProvenFourK.extents &&
            raisedFactorRetainsProvenFourK.reusedPreviousPresentationBudget &&
            raisedFactorRetainsProvenFourK.memoryBudgetConstrained &&
            sameExtent(
                raisedFactorRetainsProvenFourK.extents->presentation,
                provenFourK.presentation
            ),
        "A tighter live reading must constrain a raised factor to the proven running envelope instead of disabling scaling");
    profile.scaling_factor = 1.2F;
    const auto reducedFactorShrinksInsideProvenFourK = scalingDecisionForCreate(
        profile, true, 8, largeVariableCreate, {2560, 1440},
        provenFourK, std::nullopt,
        exhaustedAdmission.effectivePixelBudget,
        std::nullopt, false, exhaustedAdmission.staticPixelBudget
    );
    expect(reducedFactorShrinksInsideProvenFourK.extents &&
            reducedFactorShrinksInsideProvenFourK.
                reusedPreviousPresentationBudget &&
            !reducedFactorShrinksInsideProvenFourK.
                reusedPreviousDownshiftEnvelope &&
            !reducedFactorShrinksInsideProvenFourK.
                memoryBudgetConstrained &&
            sameExtent(
                reducedFactorShrinksInsideProvenFourK.extents->presentation,
                {3072, 1728}
            ),
        "Reducing a proven live extent must remain admitted even when the driver's released-allocation accounting lags");
    profile.scaling_factor = 2.0F;
    const SpatialScalingExtents currentSameSourceDownshift{
        .source = {1920, 1080},
        .presentation = {2880, 1620},
    };
    const auto factorRoundTripRestoresProvenFourK = scalingDecisionForCreate(
        profile, true, 9, largeVariableCreate, {1920, 1080},
        currentSameSourceDownshift, std::nullopt,
        exhaustedAdmission.effectivePixelBudget,
        std::nullopt, false, exhaustedAdmission.staticPixelBudget,
        sameSourceFourK
    );
    expect(factorRoundTripRestoresProvenFourK.extents &&
            factorRoundTripRestoresProvenFourK.
                reusedPreviousPresentationBudget &&
            factorRoundTripRestoresProvenFourK.
                reusedRollbackPresentationBudget &&
            !factorRoundTripRestoresProvenFourK.memoryBudgetConstrained &&
            sameExtent(
                factorRoundTripRestoresProvenFourK.extents->presentation,
                {3840, 2160}
            ),
        "A 2x to 1.5x to 2x same-source round trip must restore its one-step proven 4K envelope while live accounting lags");
    const auto lowerSourceReusesProvenFourK = scalingDecisionForCreate(
        profile, true, 9, largeVariableCreate, {1920, 1080},
        provenFourK, std::nullopt, exhaustedAdmission.effectivePixelBudget,
        std::nullopt, false, exhaustedAdmission.staticPixelBudget
    );
    expect(lowerSourceReusesProvenFourK.extents &&
            lowerSourceReusesProvenFourK.reusedPreviousPresentationBudget &&
            lowerSourceReusesProvenFourK.reusedPreviousDownshiftEnvelope &&
            !lowerSourceReusesProvenFourK.memoryBudgetConstrained &&
            sameExtent(
                lowerSourceReusesProvenFourK.extents->source, {1920, 1080}
            ) &&
            sameExtent(
                lowerSourceReusesProvenFourK.extents->presentation,
                provenFourK.presentation
            ),
        "A lower source must reuse an already-proven presentation envelope while released-allocation accounting lags");
    const auto ultrawideDownshiftFitsInsideProvenFourK =
        scalingDecisionForCreate(
            profile, true, 9, largeVariableCreate, {2560, 1080},
            provenFourK, std::nullopt,
            exhaustedAdmission.effectivePixelBudget,
            std::nullopt, false, exhaustedAdmission.staticPixelBudget
        );
    expect(ultrawideDownshiftFitsInsideProvenFourK.extents &&
            ultrawideDownshiftFitsInsideProvenFourK.
                reusedPreviousPresentationBudget &&
            ultrawideDownshiftFitsInsideProvenFourK.
                reusedPreviousDownshiftEnvelope &&
            ultrawideDownshiftFitsInsideProvenFourK.
                memoryBudgetConstrained &&
            sameExtent(
                ultrawideDownshiftFitsInsideProvenFourK.extents->presentation,
                {3840, 1620}
            ),
        "A lower ultrawide source must preserve aspect ratio while remaining inside the proven presentation envelope");
    const auto largerSourceRequiresFreshAdmission = scalingDecisionForCreate(
        profile, true, 9, largeVariableCreate, {2560, 1440},
        SpatialScalingExtents{
            .source = {1920, 1080},
            .presentation = {3840, 2160},
        },
        std::nullopt, exhaustedAdmission.effectivePixelBudget,
        std::nullopt, false, exhaustedAdmission.staticPixelBudget
    );
    expect(!largerSourceRequiresFreshAdmission.extents &&
            !largerSourceRequiresFreshAdmission.
                reusedPreviousPresentationBudget &&
            !largerSourceRequiresFreshAdmission.
                reusedPreviousDownshiftEnvelope &&
            largerSourceRequiresFreshAdmission.inactiveReason ==
                SpatialScalingInactiveReason::VariableSurfaceMemoryBudget,
        "A larger source must not borrow a smaller source's live allocation proof");

    constexpr uint64_t measuredTransitionLivePixelBudget = 2'523'785;
    const auto largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom =
        scalingDecisionForCreate(
            profile, true, 10, largeVariableCreate, {2560, 1440},
            sameSourceFourK, std::nullopt,
            measuredTransitionLivePixelBudget,
            VkExtent2D{3840, 2160}, true,
            variablePresentationPixelBudget(eightGiB)
        );
    expect(largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.extents &&
            largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.
                reusedPreviousPresentationBudget &&
            largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.
                reusedPreviousSourceGrowthHeadroom &&
            !largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.
                reusedPreviousDownshiftEnvelope &&
            !largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.
                memoryBudgetConstrained &&
            sameExtent(
                largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.
                    extents->source,
                {2560, 1440}
            ) &&
            sameExtent(
                largerSourceKeepsProvenFourKWithinLiveGrowthHeadroom.
                    extents->presentation,
                {3840, 2160}
            ),
        "A larger source may retain a proven 4K output only when its incremental area fits live headroom");
    const auto coldLargerSourceStillRequiresFullAdmission =
        scalingDecisionForCreate(
            profile, true, 10, largeVariableCreate, {2560, 1440},
            std::nullopt, std::nullopt,
            measuredTransitionLivePixelBudget,
            VkExtent2D{3840, 2160}, true,
            variablePresentationPixelBudget(eightGiB)
        );
    expect(!coldLargerSourceStillRequiresFullAdmission.extents &&
            !coldLargerSourceStillRequiresFullAdmission.
                reusedPreviousSourceGrowthHeadroom &&
            coldLargerSourceStillRequiresFullAdmission.inactiveReason ==
                SpatialScalingInactiveReason::VariableSurfaceMemoryBudget,
        "A cold larger source must not receive transition-only presentation proof");

    const SpatialScalingExtents measuredCurrentDownshift{
        .source = {2560, 1440},
        .presentation = {3072, 1728},
    };
    profile.scaling_factor = 1.2F;
    const auto measuredNativeToPreviousFactor = scalingDecisionForCreate(
        profile, true, 11, largeVariableCreate, {2560, 1440},
        measuredCurrentDownshift, std::nullopt,
        measuredTransitionLivePixelBudget,
        VkExtent2D{3840, 2160}, true,
        variablePresentationPixelBudget(eightGiB), provenFourK
    );
    expect(measuredNativeToPreviousFactor.extents &&
            measuredNativeToPreviousFactor.
                reusedRollbackPresentationBudget &&
            sameExtent(
                measuredNativeToPreviousFactor.extents->presentation,
                {3072, 1728}
            ),
        "The observed Native Resolution to 1.2x step must restore its exact previously running extent");
    profile.scaling_factor = 1.3F;
    const auto measuredIntermediateFactor = scalingDecisionForCreate(
        profile, true, 12, largeVariableCreate, {2560, 1440},
        measuredCurrentDownshift, std::nullopt,
        measuredTransitionLivePixelBudget,
        VkExtent2D{3840, 2160}, true,
        variablePresentationPixelBudget(eightGiB), provenFourK
    );
    expect(measuredIntermediateFactor.extents &&
            measuredIntermediateFactor.reusedRollbackPresentationBudget &&
            sameExtent(
                measuredIntermediateFactor.extents->presentation,
                {3326, 1870}
            ),
        "An intermediate 1.3x step must remain inside the retained proven 4K envelope");
    const auto measuredRetainedMaximum =
        committedVariableSurfaceRollbackExtents(
            measuredCurrentDownshift, provenFourK,
            true, true, true, false, false, false,
            {2560, 1440}, {3326, 1870}
        );
    expect(measuredRetainedMaximum && sameExtent(
            measuredRetainedMaximum->presentation, {3840, 2160}),
        "An intermediate successful upshift must retain the measured 4K maximum");
    profile.scaling_factor = 1.5F;
    const auto measuredReturnToFourK = scalingDecisionForCreate(
        profile, true, 13, largeVariableCreate, {2560, 1440},
        SpatialScalingExtents{
            .source = {2560, 1440},
            .presentation = {3326, 1870},
        },
        std::nullopt, measuredTransitionLivePixelBudget,
        VkExtent2D{3840, 2160}, true,
        variablePresentationPixelBudget(eightGiB), measuredRetainedMaximum
    );
    expect(measuredReturnToFourK.extents &&
            measuredReturnToFourK.reusedRollbackPresentationBudget &&
            sameExtent(
                measuredReturnToFourK.extents->presentation,
                {3840, 2160}
            ),
        "The observed 1.2x to 1.3x to 1.5x sequence must recover its proven 4K output under lagging live accounting");

    profile.scaling_factor = 2.0F;
    const auto staticOnlyAdmission = variablePresentationMemoryAdmission(
        eightGiB
    );
    expect(!staticOnlyAdmission.livePixelBudget &&
            staticOnlyAdmission.effectivePixelBudget ==
                variablePresentationPixelBudget(eightGiB),
        "A driver without VK_EXT_memory_budget must retain the deterministic static fallback");

    const auto fourKPresentation = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1920, 1080},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(fourKPresentation.extents && sameExtent(
            fourKPresentation.extents->presentation, {3840, 2160}),
        "An 8 GiB device must retain exact 1080p-to-4K scaling");
    const auto coldFiveKUsesBaseline = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {2560, 1440},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(coldFiveKUsesBaseline.extents &&
            coldFiveKUsesBaseline.usedBaselinePresentationBudget &&
            coldFiveKUsesBaseline.memoryBudgetConstrained &&
            !coldFiveKUsesBaseline.reusedPreviousPresentationBudget &&
            sameExtent(
                coldFiveKUsesBaseline.extents->source, {2560, 1440}
            ) &&
            sameExtent(
                coldFiveKUsesBaseline.extents->presentation, {3840, 2160}
            ),
        "A cold 1440p launch must use the deterministic 4K presentation envelope");
    expect(variableSurfaceFactorChangePreservesEffectiveExtents(
            true, true, {2560, 1440}, {3840, 2160}, 2.0F, 1.8F),
        "A factor edit above an active variable-surface ceiling must preserve the effective extents");
    expect(variableSurfaceFactorChangePreservesEffectiveExtents(
            true, true, {2560, 1440}, {3840, 2160}, 2.0F, 1.5F),
        "A factor edit exactly matching an active variable-surface ceiling must preserve the effective extents");
    expect(!variableSurfaceFactorChangePreservesEffectiveExtents(
            true, true, {2560, 1440}, {3840, 2160}, 2.0F, 1.4F),
        "A factor edit below an active variable-surface ceiling must require new extents");
    expect(!variableSurfaceFactorChangePreservesEffectiveExtents(
            false, true, {2560, 1440}, {3840, 2160}, 2.0F, 1.8F),
        "A fixed surface must retain its factor-bound capability contract");
    expect(!variableSurfaceFactorChangePreservesEffectiveExtents(
            true, true, {1280, 720}, {1920, 1080}, 1.5F, 1.8F),
        "An unconstrained variable surface must recreate when its factor changes");
    const auto fiveKAfterFourKIsIdentical = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {2560, 1440},
        SpatialScalingExtents{
            .source = {1920, 1080},
            .presentation = {3840, 2160},
        },
        std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(fiveKAfterFourKIsIdentical.extents &&
            fiveKAfterFourKIsIdentical.usedBaselinePresentationBudget &&
            !fiveKAfterFourKIsIdentical.reusedPreviousPresentationBudget &&
            sameExtent(
                fiveKAfterFourKIsIdentical.extents->source, {2560, 1440}
            ) &&
            sameExtent(
                fiveKAfterFourKIsIdentical.extents->presentation,
                coldFiveKUsesBaseline.extents->presentation
            ),
        "A prior 1080p swapchain must not change the 1440p allocation decision");
    const auto eightKRejected = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {3840, 2160},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(!eightKRejected.extents && eightKRejected.inactiveReason ==
            SpatialScalingInactiveReason::VariableSurfaceMemoryBudget,
        "An 8 GiB device must not allocate an 8K lower swapchain");
    const auto nonWidescreen = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1280, 1024},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(nonWidescreen.extents && sameExtent(
            nonWidescreen.extents->presentation, {2560, 2048}),
        "Allocation safety must retain valid 5:4 source scaling");
    const auto eightKPresentation = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {3840, 2160},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(twentyFourGiB)
    );
    expect(eightKPresentation.extents && sameExtent(
            eightKPresentation.extents->presentation, {7680, 4320}),
        "A 24 GiB device must retain exact 4K-to-8K scaling");

    profile.scaling_factor = std::numeric_limits<float>::infinity();
    expect(!selectSpatialScalingExtents(
            profile, fixedCapabilities(1920, 1080)),
        "A non-finite factor must fail closed");

    profile.scaling_factor = 2.01F;
    expect(!selectSpatialScalingExtents(
            profile, fixedCapabilities(1920, 1080)),
        "An invalid factor must fail closed even after parser validation");

    ls::GameConf scalingOnlyProfile{
        .frame_generation_enabled = false,
        .scaling_enabled = true,
    };
    VkSwapchainCreateInfoKHR scalingOnlyCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    const bool scalingOnlyPrivateTransport =
        applySwapchainCreateProvisioning(
            scalingOnlyProfile, 0, scalingOnlyCreate,
            false, true, false
        );
    expect(!scalingOnlyPrivateTransport &&
            scalingOnlyCreate.minImageCount == 3 &&
            scalingOnlyCreate.presentMode == VK_PRESENT_MODE_MAILBOX_KHR,
        "Standalone scaling must preserve WSI image count and present mode");
    expect((scalingOnlyCreate.imageUsage &
            (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
             VK_IMAGE_USAGE_TRANSFER_DST_BIT)) ==
            (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
             VK_IMAGE_USAGE_TRANSFER_DST_BIT),
        "Standalone scaling must request only its required transfer usage");

    auto combinedProfile = scalingOnlyProfile;
    combinedProfile.frame_generation_enabled = true;
    VkSwapchainCreateInfoKHR combinedCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    const bool combinedPrivateTransport =
        applySwapchainCreateProvisioning(
            combinedProfile, 0, combinedCreate,
            true, false, true
        );
    expect(combinedPrivateTransport &&
            combinedCreate.minImageCount == 5 &&
            combinedCreate.presentMode == VK_PRESENT_MODE_FIFO_KHR,
        "A configured combined profile must reserve two generated outputs without a duplicate real-frame slot before the lower scaling relay arrives");

    auto frameGenerationOnlyProfile = combinedProfile;
    frameGenerationOnlyProfile.scaling_enabled = false;
    VkSwapchainCreateInfoKHR frameGenerationOnlyCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    const bool frameGenerationOnlyPrivateTransport =
        applySwapchainCreateProvisioning(
            frameGenerationOnlyProfile, 0, frameGenerationOnlyCreate,
            true, false, true
        );
    expect(frameGenerationOnlyPrivateTransport &&
            frameGenerationOnlyCreate.minImageCount == 6 &&
            frameGenerationOnlyCreate.presentMode == VK_PRESENT_MODE_FIFO_KHR,
        "Ordered FG-only must restore the release-3.0 three-image plus two-output plus one-relief contract");

    VkSwapchainCreateInfoKHR compatibleFrameGenerationOnlyCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    const bool compatibleFrameGenerationOnlyTransport =
        applySwapchainCreateProvisioning(
            frameGenerationOnlyProfile, 0,
            compatibleFrameGenerationOnlyCreate,
            true, false, true, true
        );
    expect(compatibleFrameGenerationOnlyTransport &&
            compatibleFrameGenerationOnlyCreate.minImageCount == 3 &&
            compatibleFrameGenerationOnlyCreate.presentMode ==
                VK_PRESENT_MODE_FIFO_KHR &&
            (compatibleFrameGenerationOnlyCreate.imageUsage &
                (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT)) ==
                (VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT),
        "Explicit startup compatibility must preserve an FG-only application's image count without removing ordered transport or transfer usage");

    VkSwapchainCreateInfoKHR compatibleCombinedCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    expect(applySwapchainCreateProvisioning(
            combinedProfile, 0, compatibleCombinedCreate,
            true, true, true, true
        ) && compatibleCombinedCreate.minImageCount == 3 &&
            compatibleCombinedCreate.presentMode == VK_PRESENT_MODE_FIFO_KHR,
        "Explicit startup compatibility must preserve the same application image contract for combined FG and scaling");

    VkSwapchainCreateInfoKHR compatibleNonOrderedCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    expect(!applySwapchainCreateProvisioning(
            frameGenerationOnlyProfile, 0, compatibleNonOrderedCreate,
            true, false, false, true
        ) && compatibleNonOrderedCreate.minImageCount == 3 &&
            compatibleNonOrderedCreate.presentMode ==
                VK_PRESENT_MODE_MAILBOX_KHR,
        "Explicit startup compatibility must not invent ordered transport on a non-ordered path");

    VkPhysicalDeviceMemoryBudgetPropertiesEXT imageCountPricingBudget{
        .sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT,
    };
    imageCountPricingBudget.heapBudget[0] = 7 * gibibyte;
    imageCountPricingBudget.heapUsage[0] = 5'947 * mebibyte;
    const auto imageCountPricingMemory = variablePresentationMemoryAdmission(
        eightGiB, &imageCountPricingBudget
    );
    const auto applicationMinimumPricing =
        variablePresentationResourceAdmission(
            imageCountPricingMemory, {1920, 1080},
            VK_FORMAT_B8G8R8A8_UNORM, 3, 2
        );
    const auto provisionedMinimumPricing =
        variablePresentationResourceAdmission(
            imageCountPricingMemory, {1920, 1080},
            VK_FORMAT_B8G8R8A8_UNORM, combinedCreate.minImageCount, 2
        );
    expect(applicationMinimumPricing.presentationBytesPerPixel == 20 &&
            provisionedMinimumPricing.presentationBytesPerPixel == 28 &&
            provisionedMinimumPricing.effectivePixelBudget <
                applicationMinimumPricing.effectivePixelBudget,
        "Scaling admission must price the exact five-image application-plus-generated WSI graph");
    expect(
        (provisionedMinimumPricing.presentationBytesPerPixel -
            applicationMinimumPricing.presentationBytesPerPixel) *
                minimumVariablePresentationPixels == 66'355'200,
        "The RE4 4K admission regression must charge two generated images without a duplicate real image");

    auto fiveXProfile = frameGenerationOnlyProfile;
    fiveXProfile.multiplier = 5;
    fiveXProfile.adaptive_max_multiplier = 5;
    VkSwapchainCreateInfoKHR fiveXCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 2,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    static_cast<void>(applySwapchainCreateProvisioning(
        fiveXProfile, 0, fiveXCreate, true, false, true
    ));
    expect(fiveXCreate.minImageCount == 7,
        "Ordered FG-only must retain the release-3.0 FIFO relief image");

    VkSwapchainCreateInfoKHR applicationDominatedCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 4,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    static_cast<void>(applySwapchainCreateProvisioning(
        frameGenerationOnlyProfile, 0, applicationDominatedCreate,
        true, false, true
    ));
    expect(applicationDominatedCreate.minImageCount == 7,
        "A larger FG-only application minimum must retain generated outputs and FIFO relief");

    VkSwapchainCreateInfoKHR nonOrderedFrameGenerationCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    static_cast<void>(applySwapchainCreateProvisioning(
        frameGenerationOnlyProfile, 0, nonOrderedFrameGenerationCreate,
        true, false, false
    ));
    expect(nonOrderedFrameGenerationCreate.minImageCount == 5,
        "Non-ordered Frame Generation must not reserve ordered FIFO relief");

    VkSwapchainCreateInfoKHR boundedFiveXCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 2,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    static_cast<void>(applySwapchainCreateProvisioning(
        fiveXProfile, 4, boundedFiveXCreate, true, false, true
    ));
    expect(boundedFiveXCreate.minImageCount == 4,
        "The surface maximum must still bound ordered-queue image provisioning");

    VkSwapchainCreateInfoKHR saturatedCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = std::numeric_limits<uint32_t>::max(),
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    static_cast<void>(applySwapchainCreateProvisioning(
        fiveXProfile, 0, saturatedCreate, true, false, true
    ));
    expect(saturatedCreate.minImageCount ==
            std::numeric_limits<uint32_t>::max(),
        "Generated-output provisioning must saturate instead of wrapping an extreme application minimum");

    expect(shouldRetrySwapchainWithApplicationMinimum(
            VK_ERROR_INITIALIZATION_FAILED, 3, 6),
        "An implementation rejection must retry an inflated minimum once");
    expect(shouldRetrySwapchainWithApplicationMinimum(
            VK_ERROR_OUT_OF_DEVICE_MEMORY, 3, 6),
        "A device-memory rejection must retry an inflated minimum once");
    expect(shouldRetrySwapchainWithApplicationMinimum(
            VK_ERROR_OUT_OF_HOST_MEMORY, 3, 6),
        "A host-memory rejection must retry an inflated minimum once");
    expect(generatedFrameCapacitySupportedByExistingSwapchain(3, 5) == 2,
        "A five-image swapchain must expose two outputs beyond a three-image application minimum");
    expect(generatedFrameCapacitySupportedByExistingSwapchain(3, 6) == 3,
        "A six-image swapchain must expose three outputs beyond a three-image application minimum");
    expect(generatedFrameCapacitySupportedByExistingSwapchain(3, 3) == 0,
        "An application-only pool must expose no generated-output headroom");
    expect(generatedFrameCapacitySupportedByExistingSwapchain(3, 2) == 0,
        "A malformed returned-image count must fail closed without unsigned underflow");
    expect(!shouldRetrySwapchainWithApplicationMinimum(
            VK_ERROR_SURFACE_LOST_KHR, 3, 6),
        "A lost surface must not be hidden by an image-count retry");
    expect(!shouldRetrySwapchainWithApplicationMinimum(
            VK_ERROR_INITIALIZATION_FAILED, 3, 3),
        "An unmodified minimum must not be retried identically");
    auto liveDisabledProfile = combinedProfile;
    liveDisabledProfile.frame_generation_enabled = false;
    VkSwapchainCreateInfoKHR retainedInteropCreate{
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .minImageCount = 3,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .presentMode = VK_PRESENT_MODE_MAILBOX_KHR,
    };
    const bool retainedPrivateTransport =
        applySwapchainCreateProvisioning(
            liveDisabledProfile, 0, retainedInteropCreate,
            true, true, true
        );
    expect(retainedPrivateTransport &&
            retainedInteropCreate.minImageCount ==
                combinedCreate.minImageCount &&
            retainedInteropCreate.presentMode == VK_PRESENT_MODE_FIFO_KHR,
        "An FG-provisioned device must retain live re-enable resources through an off-state recreation");
}
