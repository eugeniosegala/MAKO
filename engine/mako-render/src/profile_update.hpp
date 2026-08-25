/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_policy_limits.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mako::layer {

    enum class ProfileUpdateAction : uint8_t {
        NoRuntimeChange,
        ApplyLive,
        DeferUntilSwapchainRecreation,
    };

    struct ProfileUpdateDecision {
        ProfileUpdateAction action{ProfileUpdateAction::NoRuntimeChange};
        bool frameGenerationChanged{false};
        bool refreshRateThresholdChanged{false};
        bool generationPolicyChanged{false};
        bool generationModeChanged{false};
        bool fixedMultiplierChanged{false};
        bool baseFpsCapChanged{false};
        bool dynamicCadenceProbeIntervalChanged{false};
    };

    /// Auto-cap aligns the common healthy path with an exact 2x cadence.
    /// Adaptive can still raise its multiplier when the game falls below this
    /// ceiling. Keep the engine's 10 FPS policy floor for unusually low targets.
    [[nodiscard]] inline double effectiveBaseFpsCap(
            const ls::GameConf& profile) {
        if (profile.adaptive && profile.adaptive_auto_base_fps_cap) {
            return std::max(
                adaptiveMinimumBaseFps,
                static_cast<double>(profile.target_fps) / 2.0
            );
        }
        return static_cast<double>(profile.base_fps_cap);
    }

    [[nodiscard]] inline bool dynamicCadenceRecoveryEnabled(
            const ls::GameConf& profile) {
        return profile.dynamic_cadence_recovery &&
            effectiveBaseFpsCap(profile) <= 0.0;
    }

    /// Preserve the user's live Frame Generation switch while applying the
    /// optional display guard. Missing Gamescope feedback fails open so an
    /// unsupported compositor cannot silently disable synthesis.
    [[nodiscard]] inline bool effectiveFrameGenerationEnabled(
            const ls::GameConf& profile,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        if (!profile.frame_generation_enabled)
            return false;
        return profile.frame_generation_refresh_threshold == 0 ||
            !gamescopeRefreshHz ||
            *gamescopeRefreshHz > profile.frame_generation_refresh_threshold;
    }

    /// Once Smooth Cadence has validated a constant 2x policy, ordered FIFO can
    /// own the same pacing boundary as Fixed 2x. Handoff is deliberately
    /// restricted to Steady Adaptive with confirmed target-matching refresh;
    /// losing qualification or entering transport recovery restores the
    /// explicit real-frame cap on the next present.
    [[nodiscard]] inline bool smoothCadencePacerHandoffActive(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const AdaptiveSchedulerSnapshot& scheduler) {
        return profile.adaptive &&
            profile.adaptive_auto_base_fps_cap &&
            profile.adaptive_stable_cadence &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            adaptiveTargetMatchesRefresh(
                profile.target_fps, gamescopeRefreshHz
            ) &&
            scheduler.phase == AdaptiveSchedulerPhase::StableCadence &&
            scheduler.stableCadenceLimit == 1 &&
            !scheduler.stableCadenceEvaluationActive &&
            scheduler.smoothedBaseFps * 2.0 >=
                static_cast<double>(profile.target_fps) * 0.98 &&
            scheduler.smoothedBaseFps * 2.0 <=
                static_cast<double>(profile.target_fps) * 1.02;
    }

    struct GenerationSchedulerPolicy {
        uint32_t targetFps{0};
        size_t maximumMultiplier{0};
        bool stableCadence{false};
        bool dynamicCadenceRecovery{false};
        float dynamicCadenceProbeIntervalSeconds{
            ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds
        };
    };

    /// Adaptive owns an explicit output target. Fixed + Dynamic Cadence
    /// Recovery instead follows a confirmed Gamescope refresh rate and treats
    /// the selected Fixed multiplier as a ceiling. Without that external
    /// signal, Fixed remains exact rather than borrowing a hidden target.
    [[nodiscard]] inline std::optional<GenerationSchedulerPolicy>
    generationSchedulerPolicy(const ls::GameConf& profile,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        if (profile.adaptive) {
            return GenerationSchedulerPolicy{
                .targetFps = profile.target_fps,
                .maximumMultiplier = profile.adaptive_max_multiplier,
                .stableCadence = profile.adaptive_stable_cadence,
                .dynamicCadenceRecovery =
                    dynamicCadenceRecoveryEnabled(profile),
                .dynamicCadenceProbeIntervalSeconds =
                    profile.dynamic_cadence_probe_interval_seconds,
            };
        }
        if (!dynamicCadenceRecoveryEnabled(profile) ||
                !gamescopeRefreshHz ||
                *gamescopeRefreshHz < ls::GameConfLimits::minimumTargetFps ||
                *gamescopeRefreshHz > ls::GameConfLimits::maximumTargetFps ||
                profile.multiplier <
                    ls::GameConfLimits::minimumAdaptiveMaxMultiplier ||
                profile.multiplier >
                    ls::GameConfLimits::maximumAdaptiveMaxMultiplier) {
            return std::nullopt;
        }
        return GenerationSchedulerPolicy{
            .targetFps = *gamescopeRefreshHz,
            .maximumMultiplier = profile.multiplier,
            .stableCadence = false,
            .dynamicCadenceRecovery = true,
            .dynamicCadenceProbeIntervalSeconds =
                profile.dynamic_cadence_probe_interval_seconds,
        };
    }

    /// Reserve one private output set that can serve both Fixed and Adaptive.
    /// Ultra Performance deliberately keeps only the active policy because
    /// its profile is frozen for the lifetime of the game process.
    [[nodiscard]] inline size_t generatedFrameCapacityForProfile(
            const ls::GameConf& profile) {
        if (profile.ultra_performance) {
            const size_t activeMultiplier = profile.adaptive
                ? profile.adaptive_max_multiplier
                : profile.multiplier;
            return activeMultiplier - 1;
        }
        return std::max(profile.multiplier, profile.adaptive_max_multiplier) - 1;
    }

    /// Live updates need only the capacity selected by the active policy.
    /// The inactive mode remains reserved at initial creation, but a dormant
    /// larger setting must not block a live switch to a cheaper policy.
    [[nodiscard]] inline size_t generatedFrameCapacityForActivePolicy(
            const ls::GameConf& profile) {
        const size_t activeMultiplier = profile.adaptive
            ? profile.adaptive_max_multiplier
            : profile.multiplier;
        return activeMultiplier - 1;
    }

    [[nodiscard]] inline size_t fixedGeneratedFrameCount(
            const size_t multiplier, const size_t generatedFrameCapacity) {
        return std::min(multiplier - 1, generatedFrameCapacity);
    }

    /// Classify a profile change without touching Vulkan state.
    ///
    /// Only switches that alter CPU-side policy or select the already-created
    /// frame-generation resources are safe during vkQueuePresentKHR. Changes
    /// that alter resource shape or backend model construction are retained by
    /// Root for the next game-owned swapchain creation.
    [[nodiscard]] inline ProfileUpdateDecision classifyProfileUpdate(
            const ls::GameConf& current, const ls::GameConf& next,
            const size_t generatedFrameCapacity,
            const bool frameGenerationResourcesAvailable) {
        const bool frameGenerationChanged =
            current.frame_generation_enabled != next.frame_generation_enabled;
        const bool refreshRateThresholdChanged =
            current.frame_generation_refresh_threshold !=
                next.frame_generation_refresh_threshold;
        const bool generationModeChanged = current.adaptive != next.adaptive;
        const bool fixedMultiplierChanged = current.multiplier != next.multiplier;
        const bool baseFpsCapChanged =
            effectiveBaseFpsCap(current) != effectiveBaseFpsCap(next);
        const bool dynamicCadenceProbeIntervalChanged =
            current.dynamic_cadence_probe_interval_seconds !=
                next.dynamic_cadence_probe_interval_seconds &&
            (current.dynamic_cadence_recovery ||
             next.dynamic_cadence_recovery);
        const bool generationPolicyChanged =
            current.dynamic_cadence_recovery !=
                next.dynamic_cadence_recovery ||
            (current.adaptive && next.adaptive && (
                current.target_fps != next.target_fps ||
                current.adaptive_max_multiplier !=
                    next.adaptive_max_multiplier ||
                current.adaptive_stable_cadence !=
                    next.adaptive_stable_cadence
            ));

        const bool backendConstructionChanged =
            current.gpu != next.gpu ||
            current.ultra_performance != next.ultra_performance ||
            ls::effectiveFlowScale(current) != ls::effectiveFlowScale(next) ||
            ls::effectivePerformanceMode(current) !=
                ls::effectivePerformanceMode(next);
        const bool presentationShapeChanged = current.pacing != next.pacing;
        const bool generatedCapacityExceeded =
            generatedFrameCapacityForActivePolicy(next) >
                generatedFrameCapacity;
        const bool resourcesNeeded = !current.frame_generation_enabled &&
            next.frame_generation_enabled &&
            !frameGenerationResourcesAvailable;

        if (backendConstructionChanged || presentationShapeChanged ||
                generatedCapacityExceeded || resourcesNeeded) {
            return {
                .action = ProfileUpdateAction::DeferUntilSwapchainRecreation,
                .frameGenerationChanged = frameGenerationChanged,
                .refreshRateThresholdChanged = refreshRateThresholdChanged,
                .generationPolicyChanged = generationPolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
                .baseFpsCapChanged = baseFpsCapChanged,
                .dynamicCadenceProbeIntervalChanged =
                    dynamicCadenceProbeIntervalChanged,
            };
        }

        if (frameGenerationChanged || refreshRateThresholdChanged ||
                generationPolicyChanged ||
                generationModeChanged || fixedMultiplierChanged ||
                baseFpsCapChanged || dynamicCadenceProbeIntervalChanged) {
            return {
                .action = ProfileUpdateAction::ApplyLive,
                .frameGenerationChanged = frameGenerationChanged,
                .refreshRateThresholdChanged = refreshRateThresholdChanged,
                .generationPolicyChanged = generationPolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
                .baseFpsCapChanged = baseFpsCapChanged,
                .dynamicCadenceProbeIntervalChanged =
                    dynamicCadenceProbeIntervalChanged,
            };
        }

        return {};
    }

}
