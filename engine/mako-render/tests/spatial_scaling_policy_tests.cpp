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
    expect(scalingExtentsForCreate(profile, real, {1280, 720}).has_value(),
        "A create request matching the advertised source extent must scale");
    expect(!scalingExtentsForCreate(profile, real, {1600, 900}),
        "An application extent override must remain application-owned");

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
    expect(!scalingExtentsForCreate(
            profile, real, previouslyAdvertisedCapabilities.currentExtent),
        "A stale fixed-surface request must not match a changed scaling policy");
    profile.scaling_enabled = false;
    expect(!scalingExtentsForCreate(
            profile, real, previouslyAdvertisedCapabilities.currentExtent),
        "A fixed-surface request must not scale after the policy is disabled");
    profile.scaling_enabled = true;
    profile.scaling_factor = 1.5F;

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
