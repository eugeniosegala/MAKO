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
    inline constexpr std::string_view legacyPostFrameGenerationSplitValue =
        "1";
    inline constexpr std::string_view combinedPipelineSplitValue = "2";

    enum class SplitLayerChainLayout {
        Disabled,
        LegacyPostFrameGeneration,
        CombinedPipeline,
        Invalid,
    };

    [[nodiscard]] inline SplitLayerChainLayout splitLayerChainLayout() {
        const char* const rawValue =
            std::getenv(splitLayerChainEnvironment.data());
        const std::string_view value = rawValue ? rawValue : "";
        if (value.empty() || value == "0")
            return SplitLayerChainLayout::Disabled;
        if (value == legacyPostFrameGenerationSplitValue)
            return SplitLayerChainLayout::LegacyPostFrameGeneration;
        if (value == combinedPipelineSplitValue)
            return SplitLayerChainLayout::CombinedPipeline;
        return SplitLayerChainLayout::Invalid;
    }

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
        return splitLayerChainLayout() != SplitLayerChainLayout::Disabled;
    }

    /// Direct Renderer launches retain combined frame-generation and spatial
    /// ownership. Renderer 2.2 split chains reconstructed below Gamescope WSI.
    /// Current split chains preserve that loader order but let the upper
    /// frame-generation role own an extent-selected combined pipeline. The
    /// lower spatial role remains capability-only.
    [[nodiscard]] inline bool spatialScalingOwnedByLayer() {
        const auto layout = splitLayerChainLayout();
        if constexpr (spatialScalingLayer) {
            return layout != SplitLayerChainLayout::CombinedPipeline;
        }
        return layout == SplitLayerChainLayout::Disabled ||
            layout == SplitLayerChainLayout::CombinedPipeline;
    }

    /// Capability virtualization remains below Gamescope WSI so the spatial
    /// role observes the compositor-owned presentation surface. The upper
    /// role consumes that immutable result through the relay in either split
    /// layout; direct combined launches own the query without a relay.
    [[nodiscard]] inline bool spatialScalingCapabilityOwnedByLayer() {
        if constexpr (spatialScalingLayer)
            return true;
        return !splitLayerChainEnabled();
    }

    /// Both split layouts relay the lower role's virtual fixed extent through
    /// Gamescope WSI. In the legacy layout the upper role forwards the source
    /// create unchanged. In the current layout it uses the relayed contract to
    /// allocate one combined scaler/FG context at the selected FG extent.
    [[nodiscard]] inline bool spatialScalingCapabilityRelayByLayer() {
        if constexpr (spatialScalingLayer)
            return false;
        const auto layout = splitLayerChainLayout();
        return layout == SplitLayerChainLayout::LegacyPostFrameGeneration ||
            layout == SplitLayerChainLayout::CombinedPipeline;
    }

    /// The direct Renderer and the current upper split role own one combined
    /// scaling/FG context. The immutable swapchain extent policy chooses the
    /// exact pre/post ordering inside that owner.
    [[nodiscard]] inline bool combinedSpatialFramePipelineOwnedByLayer() {
        if constexpr (spatialScalingLayer)
            return false;
        return spatialScalingOwnedByLayer();
    }

    [[nodiscard]] inline bool shouldRejectUnmatchedFixedSpatialCreate(
            const bool fixedVirtualSourceRequest,
            const bool scalingExtentsSelected) {
        return (spatialScalingOwnedByLayer() ||
                spatialScalingCapabilityOwnedByLayer()) &&
            fixedVirtualSourceRequest && !scalingExtentsSelected;
    }

    /// A current split upper role must never construct a transient native-size
    /// FG context while scaling is provisioned but its lower WSI extent relay
    /// is missing or invalid. An explicit lower no-split decision is a valid
    /// native create, not a missing dependency. The lower swapchain is rolled
    /// back only when no coherent lower decision was observed.
    [[nodiscard]] inline bool shouldRejectUncontractedSpatialCreate(
            const bool scalingProvisioned,
            const bool spatialScalingActive,
            const bool lowerCreateDecisionObserved) {
        return scalingProvisioned && !spatialScalingActive &&
            !lowerCreateDecisionObserved &&
            spatialScalingOwnedByLayer() &&
            spatialScalingCapabilityRelayByLayer();
    }

    /// Preserve the process profile used for capability queries and
    /// process-static policy. A legacy split frame-generation role retains
    /// scaling fields only for relay coherence, while the current split role
    /// also applies them to its combined swapchain context.
    [[nodiscard]] inline ls::GameConf profileForLayer(
            ls::GameConf profile) {
        if constexpr (spatialScalingLayer) {
            profile.scaling_method = ls::effectiveScalingMethod(profile);
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
        }
        return profile;
    }

    /// Project a process profile onto the resources that this role is allowed
    /// to own. Legacy upper and current lower roles remain allocation-free;
    /// the current upper role retains both scaling and frame generation.
    [[nodiscard]] inline ls::GameConf profileForLayerContext(
            ls::GameConf profile) {
        if (!spatialScalingOwnedByLayer()) {
            profile.scaling_enabled = false;
            profile.scaling_method = ls::ScalingMethod::Native;
            profile.scaling_factor = ls::GameConfDefaults::scalingFactor;
            profile.scaling_sharpness =
                ls::GameConfDefaults::scalingSharpness;
        }
        return profile;
    }

}
