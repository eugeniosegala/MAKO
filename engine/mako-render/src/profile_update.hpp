/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_policy_limits.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace mako::layer {

    enum class ProfileUpdateAction : uint8_t {
        NoRuntimeChange,
        ApplyLive,
        DeferUntilSwapchainRecreation,
        DeferUntilProcessRestart,
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
        bool swapchainRecreationDeferred{false};
        bool processRestartDeferred{false};
    };

    struct ProfileUpdatePlan {
        ls::GameConf appliedProfile;
        ProfileUpdateDecision decision;
    };

    /// Ultra Performance deliberately removes profile-file polling from the
    /// running process. Compositor-owned safety feedback is handled separately.
    [[nodiscard]] inline bool liveProfileReloadEnabled(
            const std::optional<ls::GameConf>& activeProfile) {
        return !activeProfile || !activeProfile->ultra_performance;
    }

    /// Normal mode retains idle private resources for live re-enable. An Ultra
    /// Performance profile is process-frozen, so an explicit off state has no
    /// live enable path and can omit those resources entirely.
    [[nodiscard]] inline bool privateGenerationResourcesRequired(
            const ls::GameConf& profile) {
        return !profile.ultra_performance ||
            profile.frame_generation_enabled;
    }

    /// A natural swapchain recreation reuses Root's process-wide backend. Keep
    /// its actual GPU identity in the context profile so a requested GPU change
    /// remains visibly pending until the process restarts.
    [[nodiscard]] inline ls::GameConf profileForExistingBackend(
            const ls::GameConf& requested,
            const ls::GameConf& backendProfile) {
        auto applied = requested;
        applied.gpu = backendProfile.gpu;
        return applied;
    }

    [[nodiscard]] inline bool backendGlobalChangePending(
            const std::optional<ls::GlobalConf>& backendGlobal,
            const ls::GlobalConf& requestedGlobal) {
        return backendGlobal && (
            backendGlobal->dll != requestedGlobal.dll ||
            backendGlobal->allow_fp16 != requestedGlobal.allow_fp16
        );
    }

    [[nodiscard]] inline bool backendProfileChangePending(
            const std::optional<ls::GameConf>& backendProfile,
            const ls::GameConf& requestedProfile) {
        return backendProfile && (
            backendProfile->gpu != requestedProfile.gpu ||
            backendProfile->ultra_performance !=
                requestedProfile.ultra_performance
        );
    }

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

    /// Build the profile that can be applied to an existing context while
    /// retaining requested resource-shape and process-static fields for their
    /// documented boundary. A pending restart-only edit must not prevent an
    /// unrelated cap, mode, target, or live generation switch from applying.
    [[nodiscard]] inline ProfileUpdatePlan planProfileUpdate(
            const ls::GameConf& current, const ls::GameConf& next,
            const size_t generatedFrameCapacity,
            const bool frameGenerationResourcesAvailable) {
        ls::GameConf applied = next;
        bool swapchainRecreationDeferred = false;
        bool processRestartDeferred = false;

        // GPU and Ultra Performance participate in process-wide backend
        // construction. A natural application swapchain recreation reuses that
        // backend and therefore cannot satisfy either change.
        if (current.gpu != next.gpu) {
            applied.gpu = current.gpu;
            processRestartDeferred = true;
        }
        if (current.ultra_performance != next.ultra_performance) {
            applied.ultra_performance = current.ultra_performance;
            processRestartDeferred = true;
        }

        // Flow scale and model selection shape the private context, while
        // pacing shapes the game-owned swapchain. A natural recreation can
        // apply these values without replacing the process-wide backend.
        if (ls::effectiveFlowScale(current) !=
                ls::effectiveFlowScale(applied)) {
            applied.flow_scale = current.flow_scale;
            swapchainRecreationDeferred = true;
        }
        if (ls::effectivePerformanceMode(current) !=
                ls::effectivePerformanceMode(applied)) {
            applied.performance_mode = current.performance_mode;
            swapchainRecreationDeferred = true;
        }
        if (current.pacing != next.pacing) {
            applied.pacing = current.pacing;
            swapchainRecreationDeferred = true;
        }

        // Keep the currently active policy when the requested Fixed/Adaptive
        // selection needs more generated images. Dormant settings can still be
        // retained in the applied profile and become active after recreation.
        if (generatedFrameCapacityForActivePolicy(applied) >
                generatedFrameCapacity) {
            applied.adaptive = current.adaptive;
            if (current.adaptive)
                applied.adaptive_max_multiplier =
                    current.adaptive_max_multiplier;
            else
                applied.multiplier = current.multiplier;
            swapchainRecreationDeferred = true;
        }

        if (!current.frame_generation_enabled &&
                next.frame_generation_enabled &&
                !frameGenerationResourcesAvailable) {
            applied.frame_generation_enabled = false;
            swapchainRecreationDeferred = true;
        }

        const bool frameGenerationChanged =
            current.frame_generation_enabled !=
                applied.frame_generation_enabled;
        const bool refreshRateThresholdChanged =
            current.frame_generation_refresh_threshold !=
                applied.frame_generation_refresh_threshold;
        const bool generationModeChanged = current.adaptive != applied.adaptive;
        const bool fixedMultiplierChanged =
            current.multiplier != applied.multiplier &&
            (!current.adaptive || !applied.adaptive);
        const bool baseFpsCapChanged =
            effectiveBaseFpsCap(current) != effectiveBaseFpsCap(applied);
        const bool dynamicCadenceProbeIntervalChanged =
            current.dynamic_cadence_probe_interval_seconds !=
                applied.dynamic_cadence_probe_interval_seconds &&
            (current.dynamic_cadence_recovery ||
             applied.dynamic_cadence_recovery);
        const bool generationPolicyChanged =
            current.dynamic_cadence_recovery !=
                applied.dynamic_cadence_recovery ||
            (current.adaptive && applied.adaptive && (
                current.target_fps != applied.target_fps ||
                current.adaptive_max_multiplier !=
                    applied.adaptive_max_multiplier ||
                current.adaptive_stable_cadence !=
                    applied.adaptive_stable_cadence
            ));

        if (frameGenerationChanged || refreshRateThresholdChanged ||
                generationPolicyChanged ||
                generationModeChanged || fixedMultiplierChanged ||
                baseFpsCapChanged || dynamicCadenceProbeIntervalChanged) {
            return {
                .appliedProfile = std::move(applied),
                .decision = {
                    .action = ProfileUpdateAction::ApplyLive,
                    .frameGenerationChanged = frameGenerationChanged,
                    .refreshRateThresholdChanged =
                        refreshRateThresholdChanged,
                    .generationPolicyChanged = generationPolicyChanged,
                    .generationModeChanged = generationModeChanged,
                    .fixedMultiplierChanged = fixedMultiplierChanged,
                    .baseFpsCapChanged = baseFpsCapChanged,
                    .dynamicCadenceProbeIntervalChanged =
                        dynamicCadenceProbeIntervalChanged,
                    .swapchainRecreationDeferred =
                        swapchainRecreationDeferred,
                    .processRestartDeferred = processRestartDeferred,
                },
            };
        }

        ProfileUpdateAction action = ProfileUpdateAction::NoRuntimeChange;
        if (processRestartDeferred)
            action = ProfileUpdateAction::DeferUntilProcessRestart;
        else if (swapchainRecreationDeferred)
            action = ProfileUpdateAction::DeferUntilSwapchainRecreation;
        return {
            .appliedProfile = std::move(applied),
            .decision = {
                .action = action,
                .swapchainRecreationDeferred =
                    swapchainRecreationDeferred,
                .processRestartDeferred = processRestartDeferred,
            },
        };
    }

    /// Classify a requested change without exposing the merged applied profile.
    /// Tests and callers that only need transition metadata can use this helper.
    [[nodiscard]] inline ProfileUpdateDecision classifyProfileUpdate(
            const ls::GameConf& current, const ls::GameConf& next,
            const size_t generatedFrameCapacity,
            const bool frameGenerationResourcesAvailable) {
        return planProfileUpdate(
            current, next, generatedFrameCapacity,
            frameGenerationResourcesAvailable
        ).decision;
    }

}
