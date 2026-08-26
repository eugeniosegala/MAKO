/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_policy_limits.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace mako::layer {

    enum class ProfileUpdateAction : uint8_t {
        NoRuntimeChange,
        ApplyLive,
        DeferUntilSwapchainRecreation,
        DeferUntilProcessRestart,
        RequestSwapchainRecreation,
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
        bool spatialScalingChanged{false};
        bool frameGenerationBackendChanged{false};
        bool generatedFrameCapacityExceeded{false};
        bool processRestartRequired{false};
    };

    struct ProcessStaticProfileProjection {
        ls::GameConf runtimeProfile;
        bool gpuSelectionPending{false};
        bool pacingPending{false};
        bool frameGenerationInteropPending{false};
        bool ultraPerformancePending{false};

        [[nodiscard]] bool restartRequired() const {
            return this->gpuSelectionPending || this->pacingPending ||
                this->frameGenerationInteropPending ||
                this->ultraPerformancePending;
        }
    };

    /// Retain settings selected during process/device/backend construction
    /// while allowing unrelated fields from the requested profile to continue
    /// through the normal live-update path.
    [[nodiscard]] inline ProcessStaticProfileProjection
    projectProcessStaticProfileForLiveUpdate(
            const ls::GameConf& current,
            const ls::GameConf& requested,
            const bool frameGenerationConfiguredAtStartup) {
        ProcessStaticProfileProjection projection{
            .runtimeProfile = requested,
            .gpuSelectionPending = requested.gpu != current.gpu,
            .pacingPending = requested.pacing != current.pacing,
            .frameGenerationInteropPending =
                requested.frame_generation_enabled &&
                !frameGenerationConfiguredAtStartup,
            .ultraPerformancePending = requested.ultra_performance !=
                current.ultra_performance,
        };
        if (projection.gpuSelectionPending)
            projection.runtimeProfile.gpu = current.gpu;
        if (projection.pacingPending)
            projection.runtimeProfile.pacing = current.pacing;
        if (projection.frameGenerationInteropPending)
            projection.runtimeProfile.frame_generation_enabled = false;
        if (projection.ultraPerformancePending) {
            projection.runtimeProfile.ultra_performance =
                current.ultra_performance;
            projection.runtimeProfile.flow_scale = current.flow_scale;
            projection.runtimeProfile.performance_mode =
                current.performance_mode;
        }
        return projection;
    }

    /// Private generated outputs are selected by the active Fixed/Adaptive
    /// policy. Initial construction may reserve more for the inactive policy,
    /// but the active requirement is the boundary used for live updates.
    [[nodiscard]] inline size_t generatedFrameCapacityForActivePolicy(
            const ls::GameConf& profile) {
        const size_t activeMultiplier = profile.adaptive
            ? profile.adaptive_max_multiplier
            : profile.multiplier;
        return activeMultiplier - 1;
    }

    /// Project a requested profile onto the GPU resources already owned by a
    /// live swapchain. Root retains the unmodified requested profile for the
    /// requested recreation; the existing context can still apply unrelated
    /// live-safe fields without changing scaler allocation, optical-flow
    /// dimensions, or the selected frame-generation model.
    [[nodiscard]] inline ls::GameConf maskRecreatedProfileResourcesForLiveUpdate(
            const ls::GameConf& current,
            const ls::GameConf& requested,
            const bool generatedFrameCapacityPending = false) {
        auto live = requested;
        live.scaling_enabled = current.scaling_enabled;
        live.scaling_method = current.scaling_method;
        live.scaling_factor = current.scaling_factor;
        live.scaling_sharpness = current.scaling_sharpness;
        live.flow_scale = current.flow_scale;
        live.performance_mode = current.performance_mode;
        if (generatedFrameCapacityPending) {
            live.adaptive = current.adaptive;
            live.multiplier = current.multiplier;
            live.adaptive_max_multiplier = current.adaptive_max_multiplier;
        }
        return live;
    }

    struct RecreatedProfileResourceKey {
        bool scalingEnabled{false};
        ls::ScalingMethod scalingMethod{ls::ScalingMethod::Mako};
        float scalingFactor{1.0F};
        float scalingSharpness{0.5F};
        float effectiveFlowScale{ls::GameConfDefaults::flowScale};
        bool effectivePerformanceMode{false};
        size_t requiredGeneratedFrameCapacity{0};

        friend bool operator==(
            const RecreatedProfileResourceKey&,
            const RecreatedProfileResourceKey&) = default;
    };

    [[nodiscard]] inline RecreatedProfileResourceKey recreatedProfileResourceKey(
            const ls::GameConf& profile) {
        return {
            .scalingEnabled = profile.scaling_enabled,
            .scalingMethod = profile.scaling_method,
            .scalingFactor = profile.scaling_factor,
            .scalingSharpness = profile.scaling_sharpness,
            .effectiveFlowScale = ls::effectiveFlowScale(profile),
            .effectivePerformanceMode = ls::effectivePerformanceMode(profile),
            .requiredGeneratedFrameCapacity =
                generatedFrameCapacityForActivePolicy(profile),
        };
    }

    /// Scaling dimensions and frame-generation context construction cannot be
    /// mutated while a swapchain is active. Arm one spec-defined OUT_OF_DATE
    /// signal after the lower present has consumed the application's wait
    /// semaphores. Repeated polls of the same requested resources do not
    /// signal again, while a distinct request receives one fresh signal.
    class LiveProfileResourceRecreation {
    public:
        using Clock = std::chrono::steady_clock;
        static constexpr auto quietPeriod = std::chrono::milliseconds(500);

        void update(const ls::GameConf& current,
                const ls::GameConf& requested,
                const uint64_t runtimeStateRevision,
                const Clock::time_point now = Clock::now()) {
            const auto currentKey = recreatedProfileResourceKey(current);
            const auto requestedKey = recreatedProfileResourceKey(requested);
            if (currentKey == requestedKey) {
                this->request.reset();
                return;
            }
            if (this->request && this->request->profile == requestedKey)
                return;
            this->request = Request{
                .profile = requestedKey,
                .runtimeStateRevision = runtimeStateRevision,
                .signalAfter = now + quietPeriod,
            };
        }

        [[nodiscard]] bool pending() const {
            return this->request.has_value();
        }

        [[nodiscard]] bool armed() const {
            return this->request && !this->request->signaled;
        }

        [[nodiscard]] std::optional<uint64_t> signalAfterSuccessfulPresent(
                const Clock::time_point now = Clock::now()) {
            if (!this->request || this->request->signaled ||
                    now < this->request->signalAfter)
                return std::nullopt;
            this->request->signaled = true;
            return this->request->runtimeStateRevision;
        }

    private:
        struct Request {
            RecreatedProfileResourceKey profile;
            uint64_t runtimeStateRevision{0};
            Clock::time_point signalAfter{};
            bool signaled{false};
        };
        std::optional<Request> request;
    };

    /// Spatial resources can always be retried during application swapchain
    /// creation. Flow Scale and Lighter FG Model require the process to have
    /// retained its frame-generation device interop and private context.
    [[nodiscard]] inline bool liveProfileResourceRecreationAvailable(
            const ProfileUpdateDecision& decision,
            const bool frameGenerationResourcesAvailable) {
        if (decision.processRestartRequired)
            return false;
        return decision.spatialScalingChanged ||
            ((decision.frameGenerationBackendChanged ||
              decision.generatedFrameCapacityExceeded) &&
             frameGenerationResourcesAvailable);
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
    /// Ultra Performance deliberately keeps only the active policy. A live
    /// mode or multiplier change that needs a different capacity uses the
    /// game-owned recreation boundary before the new policy is selected.
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
        const bool spatialScalingChanged =
            current.scaling_enabled != next.scaling_enabled ||
            current.scaling_method != next.scaling_method ||
            current.scaling_factor != next.scaling_factor ||
            current.scaling_sharpness != next.scaling_sharpness;
        const bool frameGenerationBackendChanged =
            ls::effectiveFlowScale(current) != ls::effectiveFlowScale(next) ||
            ls::effectivePerformanceMode(current) !=
                ls::effectivePerformanceMode(next);

        const bool backendConstructionChanged =
            current.gpu != next.gpu ||
            current.ultra_performance != next.ultra_performance ||
            frameGenerationBackendChanged;
        const bool presentationShapeChanged = current.pacing != next.pacing;
        const bool generatedCapacityExceeded =
            generatedFrameCapacityForActivePolicy(next) >
                generatedFrameCapacity;
        const bool resourcesNeeded = !current.frame_generation_enabled &&
            next.frame_generation_enabled &&
            !frameGenerationResourcesAvailable;
        const bool processRestartRequired =
            current.gpu != next.gpu ||
            current.ultra_performance != next.ultra_performance ||
            presentationShapeChanged || resourcesNeeded;

        if (backendConstructionChanged || presentationShapeChanged ||
                spatialScalingChanged ||
                generatedCapacityExceeded || resourcesNeeded) {
            return {
                .action = processRestartRequired
                    ? ProfileUpdateAction::DeferUntilProcessRestart
                    : ProfileUpdateAction::DeferUntilSwapchainRecreation,
                .frameGenerationChanged = frameGenerationChanged,
                .refreshRateThresholdChanged = refreshRateThresholdChanged,
                .generationPolicyChanged = generationPolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
                .baseFpsCapChanged = baseFpsCapChanged,
                .dynamicCadenceProbeIntervalChanged =
                    dynamicCadenceProbeIntervalChanged,
                .spatialScalingChanged = spatialScalingChanged,
                .frameGenerationBackendChanged =
                    frameGenerationBackendChanged,
                .generatedFrameCapacityExceeded =
                    generatedCapacityExceeded,
                .processRestartRequired = processRestartRequired,
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
                .spatialScalingChanged = spatialScalingChanged,
                .frameGenerationBackendChanged =
                    frameGenerationBackendChanged,
                .generatedFrameCapacityExceeded =
                    generatedCapacityExceeded,
                .processRestartRequired = processRestartRequired,
            };
        }

        return {};
    }

}
