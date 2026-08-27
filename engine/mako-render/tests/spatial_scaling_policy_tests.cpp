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
    expect(!ls::spatialScalingRequested(profile) &&
            !selectSpatialScalingExtents(
                profile, fixedCapabilities(1280, 800)),
        "Native must bypass spatial extent virtualization while FG remains enabled");
    const auto nativeDecision = scalingDecisionForCreate(
        profile, true, 0, fixedCapabilities(1280, 800), {1280, 800}
    );
    expect(!nativeDecision.extents && nativeDecision.inactiveReason ==
            SpatialScalingInactiveReason::NativePassthrough,
        "Native must expose a distinct diagnostic passthrough reason");
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
            "swapchain-format-unsupported",
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

    const auto retainedFeedback = committedVariableSurfaceScalingExtents(
        customFactor, true, true, false, true, {960, 600}, {960, 600}
    );
    expect(retainedFeedback &&
            sameExtent(retainedFeedback->source, customFactor->source) &&
            sameExtent(
                retainedFeedback->presentation, customFactor->presentation
            ),
        "A successful native feedback-guard recreation must retain the prior pair");
    const auto replacement = committedVariableSurfaceScalingExtents(
        retainedFeedback, true, true, true, false, {800, 500}, {1200, 750}
    );
    expect(replacement && sameExtent(replacement->source, {800, 500}) &&
            sameExtent(replacement->presentation, {1200, 750}),
        "A genuinely different successful source must replace prior feedback state");
    expect(!committedVariableSurfaceScalingExtents(
            replacement, true, true, false, false, {1200, 750}, {1200, 750}),
        "A successful unsuppressed native recreation must clear variable state");
    expect(!committedVariableSurfaceScalingExtents(
            replacement, true, false, false, false, {1200, 750}, {1200, 750}),
        "A successful fixed-surface recreation must clear variable state");
    expect(!committedVariableSurfaceScalingExtents(
            replacement, false, true, false, false, {1200, 750}, {1200, 750}),
        "An inactive profile must clear variable feedback state");

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

    const auto fourKPresentation = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {1920, 1080},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(fourKPresentation.extents && sameExtent(
            fourKPresentation.extents->presentation, {3840, 2160}),
        "An 8 GiB device must retain exact 1080p-to-4K scaling");
    const auto fiveKRejected = scalingDecisionForCreate(
        profile, true, 7, largeVariableCreate, {2560, 1440},
        std::nullopt, std::nullopt,
        variablePresentationPixelBudget(eightGiB)
    );
    expect(!fiveKRejected.extents && fiveKRejected.inactiveReason ==
            SpatialScalingInactiveReason::VariableSurfaceMemoryBudget,
        "An 8 GiB device must reject an exact 5K presentation over its envelope");
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
            true, true, true
        );
    expect(combinedPrivateTransport &&
            combinedCreate.minImageCount > 3 &&
            combinedCreate.presentMode == VK_PRESENT_MODE_FIFO_KHR,
        "Provisioned frame generation must retain its image-capacity and ordered transport policy");

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
