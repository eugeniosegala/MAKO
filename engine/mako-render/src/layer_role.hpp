/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "mako-common/configuration/config.hpp"

#include <cstdlib>
#include <string_view>

namespace mako::layer {

#if defined(MAKO_LAYER_ROLE_SPATIAL_SCALING)
    inline constexpr bool frameGenerationLayer = false;
    inline constexpr bool spatialScalingLayer = true;
    inline constexpr std::string_view layerRoleName = "spatial-scaling";
    inline constexpr std::string_view layerIdentity =
        "VK_LAYER_MAKO_spatial_scaling";
#else
    inline constexpr bool frameGenerationLayer = true;
    inline constexpr bool spatialScalingLayer = false;
    inline constexpr std::string_view layerRoleName = "frame-generation";
    inline constexpr std::string_view layerIdentity = "VK_LAYER_MAKO_render";
#endif

    inline constexpr std::string_view splitLayerChainEnvironment =
        "MAKO_SPLIT_LAYER_CHAIN";

    /// The lower spatial role can observe external-memory features enabled by
    /// the upper frame-generation role. Those device features are not proof
    /// that the lower copy owns frame generation, and must never make it force
    /// a second ordered/FIFO transport beneath Gamescope WSI.
    [[nodiscard]] inline constexpr bool frameGenerationInteropForLayer(
            const bool deviceInteropEnabled) {
        return frameGenerationLayer && deviceInteropEnabled;
    }

    /// Decky's scaling path loads two isolated copies of MAKO around
    /// Gamescope WSI. Direct Renderer users keep the established combined
    /// library unless their launcher explicitly opts into that split chain.
    [[nodiscard]] inline bool splitLayerChainEnabled() {
        if constexpr (spatialScalingLayer)
            return true;

        const char* value = std::getenv(splitLayerChainEnvironment.data());
        return value != nullptr && std::string_view(value) != "" &&
            std::string_view(value) != "0";
    }

    /// Direct Renderer launches retain combined frame-generation and spatial
    /// ownership. In Decky's split chain, only the dedicated lower role owns
    /// spatial capability contracts; the upper frame-generation role must
    /// forward the virtual source extent advertised by that lower role.
    [[nodiscard]] inline bool spatialScalingOwnedByLayer() {
        if constexpr (spatialScalingLayer)
            return true;

        return !splitLayerChainEnabled();
    }

    [[nodiscard]] inline bool shouldRejectUnmatchedFixedSpatialCreate(
            const bool fixedVirtualSourceRequest,
            const bool scalingExtentsSelected) {
        return spatialScalingOwnedByLayer() && fixedVirtualSourceRequest &&
            !scalingExtentsSelected;
    }

    /// Project the shared user profile onto one layer's isolated ownership.
    /// The upper layer owns only frame generation; the lower layer owns only
    /// spatial reconstruction. Keeping unrelated settings canonical prevents
    /// either copy from allocating resources or reacting to live changes that
    /// belong to the other side of Gamescope WSI.
    [[nodiscard]] inline ls::GameConf profileForLayer(
            ls::GameConf profile) {
        if constexpr (spatialScalingLayer) {
            profile.frame_generation_enabled = false;
            profile.frame_generation_refresh_threshold =
                ls::GameConfDefaults::frameGenerationRefreshThreshold;
            profile.multiplier = ls::GameConfDefaults::multiplier;
            profile.base_fps_cap = ls::GameConfDefaults::baseFpsCap;
            profile.adaptive = ls::GameConfDefaults::adaptive;
            profile.adaptive_auto_base_fps_cap =
                ls::GameConfDefaults::adaptiveAutoBaseFpsCap;
            profile.target_fps = ls::GameConfDefaults::targetFps;
            profile.adaptive_max_multiplier =
                ls::GameConfDefaults::adaptiveMaxMultiplier;
            profile.adaptive_stable_cadence =
                ls::GameConfDefaults::adaptiveStableCadence;
            profile.dynamic_cadence_recovery =
                ls::GameConfDefaults::dynamicCadenceRecovery;
            profile.dynamic_cadence_probe_interval_seconds =
                ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds;
            profile.ultra_performance =
                ls::GameConfDefaults::ultraPerformance;
            profile.flow_scale = ls::GameConfDefaults::flowScale;
            profile.performance_mode = ls::GameConfDefaults::performanceMode;
            profile.pacing = ls::GameConfDefaults::pacing;
        } else if (splitLayerChainEnabled()) {
            profile.scaling_enabled = false;
            profile.scaling_method = ls::ScalingMethod::Native;
            profile.scaling_factor = ls::GameConfDefaults::scalingFactor;
            profile.scaling_sharpness =
                ls::GameConfDefaults::scalingSharpness;
        }
        return profile;
    }

}
