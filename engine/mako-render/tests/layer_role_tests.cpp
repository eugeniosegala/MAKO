/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "layer_role.hpp"
#include "profile_update.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

using namespace mako::layer;

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    ls::GameConf exercisedProfile() {
        ls::GameConf profile{};
        profile.frame_generation_enabled = true;
        profile.adaptive = true;
        profile.multiplier = 4;
        profile.target_fps = 120;
        profile.scaling_enabled = true;
        profile.scaling_method = ls::ScalingMethod::Mako;
        profile.scaling_factor = 2.0F;
        profile.scaling_sharpness = 0.8F;
        profile.ultra_performance = true;
        return profile;
    }
}

int main() {
#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
    unsetenv(splitLayerChainEnvironment.data());
    const auto lower = profileForLayer(exercisedProfile());
    expect(spatialScalingLayer && !frameGenerationLayer,
        "the lower build must identify only the spatial-scaling role");
    expect(lower.scaling_enabled &&
            lower.scaling_method == ls::ScalingMethod::Ls1Performance,
        "the lower layer must retain Ultra Performance's effective scaler");
    expect(!lower.frame_generation_enabled && !lower.adaptive,
        "the lower layer must never own frame generation");
    expect(generatedFrameCapacityForActivePolicy(lower) == 0,
        "the lower layer must not report a generated-image capacity request");
    expect(!frameGenerationInteropForLayer(true),
        "the lower layer must not force a second ordered transport even when "
        "upper-role interop features are visible on the device");
    expect(splitLayerChainEnabled(),
        "the lower layer is always part of an isolated split chain");
    expect(spatialScalingOwnedByLayer(),
        "the legacy lower layer must own spatial reconstruction");
    expect(spatialScalingCapabilityOwnedByLayer(),
        "the lower layer must always own spatial capability contracts");
    expect(shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the dedicated layer must reject an unmatched fixed spatial request");
    expect(!combinedSpatialFramePipelineOwnedByLayer(),
        "an unversioned dedicated role must not own the combined pipeline");
    setenv(
        splitLayerChainEnvironment.data(),
        combinedPipelineSplitValue.data(), 1
    );
    const auto currentLower = profileForLayerContext(lower);
    expect(!spatialScalingOwnedByLayer(),
        "the current lower layer must delegate spatial reconstruction");
    expect(spatialScalingCapabilityOwnedByLayer(),
        "the current lower layer must retain surface capability ownership");
    expect(!currentLower.scaling_enabled &&
            currentLower.scaling_method == ls::ScalingMethod::Native,
        "the current lower layer must remain allocation-free");
    expect(shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the lower extent owner must fail closed without its fixed contract");
    expect(!combinedSpatialFramePipelineOwnedByLayer(),
        "the capability-only lower role must not own the combined pipeline");
    expect(!spatialScalingCapabilityRelayByLayer(),
        "the application-facing dedicated role must not export a relay");
    expect(!shouldRejectUncontractedSpatialCreate(true, false, false),
        "the allocation-free lower role must not consume its own relay");
    unsetenv(splitLayerChainEnvironment.data());
#else
    unsetenv(splitLayerChainEnvironment.data());
    const auto direct = profileForLayer(exercisedProfile());
    expect(frameGenerationLayer && !spatialScalingLayer,
        "the primary build must identify the frame-generation role");
    expect(frameGenerationInteropForLayer(true) &&
            !frameGenerationInteropForLayer(false),
        "the upper layer must follow actual device interop availability");
    expect(direct.frame_generation_enabled && direct.scaling_enabled,
        "direct Renderer launches must retain the established combined library");
    expect(ls::effectiveScalingMethod(direct) ==
            ls::ScalingMethod::Ls1Performance,
        "direct Renderer launches must apply Ultra Performance to scaling");
    expect(spatialScalingOwnedByLayer(),
        "the direct combined layer must enforce spatial capability contracts");
    expect(spatialScalingCapabilityOwnedByLayer(),
        "the direct combined layer must own spatial capability queries");
    expect(combinedSpatialFramePipelineOwnedByLayer(),
        "the direct combined layer must own the extent-selected pipeline");
    expect(shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the direct combined layer must reject an unmatched fixed request");
    expect(!shouldRejectUnmatchedFixedSpatialCreate(true, true),
        "a selected scaling extent must satisfy the fixed contract");

    setenv(
        splitLayerChainEnvironment.data(),
        legacyPostFrameGenerationSplitValue.data(), 1
    );
    const auto legacyUpper = profileForLayer(exercisedProfile());
    expect(legacyUpper.frame_generation_enabled && legacyUpper.adaptive,
        "the legacy upper split layer must retain frame generation");
    expect(!spatialScalingOwnedByLayer(),
        "the legacy upper split layer must delegate reconstruction");
    expect(spatialScalingCapabilityRelayByLayer(),
        "the legacy upper role must relay virtual capabilities through WSI");
    const auto legacyUpperContext = profileForLayerContext(legacyUpper);
    expect(!legacyUpperContext.scaling_enabled &&
            legacyUpperContext.scaling_method == ls::ScalingMethod::Native,
        "the legacy upper role must not allocate spatial resources");
    expect(!shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the legacy upper role must not reject a lower-role source extent");
    expect(!shouldRejectUncontractedSpatialCreate(true, false, false),
        "the legacy allocation-free upper role must preserve its lower owner");

    setenv(
        splitLayerChainEnvironment.data(),
        combinedPipelineSplitValue.data(), 1
    );
    const auto currentFrameGeneration = profileForLayer(exercisedProfile());
    expect(currentFrameGeneration.frame_generation_enabled &&
            currentFrameGeneration.adaptive,
        "the current split frame-generation role must retain generation");
    expect(spatialScalingOwnedByLayer(),
        "the current frame-generation role must own reconstruction");
    expect(!spatialScalingCapabilityOwnedByLayer(),
        "the lower spatial role must retain fixed capability ownership");
    expect(spatialScalingCapabilityRelayByLayer(),
        "the current frame-generation role must consume relayed capabilities");
    const auto currentFrameGenerationContext =
        profileForLayerContext(currentFrameGeneration);
    expect(currentFrameGenerationContext.scaling_enabled &&
            currentFrameGenerationContext.scaling_method ==
                ls::ScalingMethod::Mako,
        "the current frame-generation role must allocate the combined scaler");
    expect(shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the current frame-generation role must fail closed without a relay");
    expect(combinedSpatialFramePipelineOwnedByLayer(),
        "the current upper context must own the extent-selected pipeline");
    expect(shouldRejectUncontractedSpatialCreate(true, false, false) &&
            !shouldRejectUncontractedSpatialCreate(true, true, false) &&
            !shouldRejectUncontractedSpatialCreate(false, false, false) &&
            !shouldRejectUncontractedSpatialCreate(true, false, true),
        "the current combined owner must reject only a provisioned create "
        "that lacks a coherent lower create decision");

    setenv(splitLayerChainEnvironment.data(), "0", 1);
    const auto disabledSplit = profileForLayer(exercisedProfile());
    expect(disabledSplit.scaling_enabled,
        "a zero split-chain value must preserve direct Renderer behavior");
    expect(spatialScalingOwnedByLayer(),
        "a disabled split chain must retain combined spatial ownership");
    expect(!spatialScalingCapabilityRelayByLayer(),
        "a disabled split chain must not enable the capability relay");
    unsetenv(splitLayerChainEnvironment.data());
#endif
}
