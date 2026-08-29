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
        return profile;
    }
}

int main() {
#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
    const auto lower = profileForLayer(exercisedProfile());
    expect(spatialScalingLayer && !frameGenerationLayer,
        "the lower build must identify only the spatial-scaling role");
    expect(lower.scaling_enabled &&
            lower.scaling_method == ls::ScalingMethod::Mako,
        "the lower layer must retain spatial-scaling configuration");
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
        "the lower layer must enforce spatial capability contracts");
    expect(shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the lower layer must reject an unmatched fixed spatial request");
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
    expect(spatialScalingOwnedByLayer(),
        "the direct combined layer must enforce spatial capability contracts");
    expect(shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the direct combined layer must reject an unmatched fixed request");
    expect(!shouldRejectUnmatchedFixedSpatialCreate(true, true),
        "a selected scaling extent must satisfy the fixed contract");

    setenv(splitLayerChainEnvironment.data(), "1", 1);
    const auto upper = profileForLayer(exercisedProfile());
    expect(upper.frame_generation_enabled && upper.adaptive,
        "the upper split layer must retain frame-generation configuration");
    expect(upper.scaling_enabled &&
            upper.scaling_method == ls::ScalingMethod::Mako,
        "the upper split layer must retain scaling configuration for the "
        "fixed-surface capability relay");
    expect(!spatialScalingOwnedByLayer(),
        "the upper split layer must forward lower-role spatial contracts");
    expect(spatialScalingCapabilityRelayByLayer(),
        "the upper split layer must relay virtual capabilities through WSI");
    const auto upperContext = profileForLayerContext(upper);
    expect(!upperContext.scaling_enabled &&
            upperContext.scaling_method == ls::ScalingMethod::Native,
        "the upper split layer must not allocate spatial-scaling resources");
    expect(!shouldRejectUnmatchedFixedSpatialCreate(true, false),
        "the upper split layer must not reject a lower-role source extent");

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
