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
#include <utility>

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
        bool spatialScalingLiveRebuild{false};
        bool frameGenerationBackendChanged{false};
        bool generatedFrameCapacityExceeded{false};
        bool swapchainRecreationDeferred{false};
        bool processRestartDeferred{false};
        bool swapchainRecreationRequested{false};
    };

    struct ProfileUpdatePlan {
        ls::GameConf appliedProfile;
        ProfileUpdateDecision decision;
    };

    [[nodiscard]] inline std::chrono::milliseconds
    spatialScalerRebuildQuietPeriod(
            const ls::GameConf& current,
            const ls::GameConf& requested) noexcept {
        if (current.scaling_method != requested.scaling_method)
            return std::chrono::milliseconds::zero();
        return std::chrono::milliseconds(500);
    }

    struct ProcessStaticProfileProjection {
        ls::GameConf runtimeProfile;
        bool gpuSelectionPending{false};
        bool ultraPerformancePending{false};
        bool scalingEnginePending{false};

        [[nodiscard]] bool restartRequired() const {
            return this->gpuSelectionPending ||
                this->ultraPerformancePending ||
                this->scalingEnginePending;
        }
    };

    /// Retain settings selected during process/device/backend construction
    /// while allowing unrelated fields from the requested profile to continue
    /// through the normal live-update path.
    [[nodiscard]] inline ProcessStaticProfileProjection
    projectProcessStaticProfileForLiveUpdate(
            const ls::GameConf& current,
            const ls::GameConf& requested,
            const bool scalingEngineConfiguredAtStartup) {
        ProcessStaticProfileProjection projection{
            .runtimeProfile = requested,
            .gpuSelectionPending = requested.gpu != current.gpu,
            .ultraPerformancePending = requested.ultra_performance !=
                current.ultra_performance,
            .scalingEnginePending = requested.scaling_enabled !=
                scalingEngineConfiguredAtStartup,
        };
        if (projection.gpuSelectionPending)
            projection.runtimeProfile.gpu = current.gpu;
        if (projection.ultraPerformancePending) {
            projection.runtimeProfile.ultra_performance =
                current.ultra_performance;
            projection.runtimeProfile.flow_scale = current.flow_scale;
            projection.runtimeProfile.performance_mode =
                current.performance_mode;
        }
        if (projection.scalingEnginePending) {
            projection.runtimeProfile.scaling_enabled =
                scalingEngineConfiguredAtStartup;
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
        ls::ScalingMethod scalingMethod{ls::ScalingMethod::Native};
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
        const bool scalingActive = ls::spatialScalingRequested(profile);
        return {
            .scalingEnabled = scalingActive,
            .scalingMethod = scalingActive
                ? profile.scaling_method : ls::ScalingMethod::Native,
            .scalingFactor = scalingActive ? profile.scaling_factor : 1.0F,
            .scalingSharpness = scalingActive
                ? profile.scaling_sharpness : 0.5F,
            .effectiveFlowScale = ls::effectiveFlowScale(profile),
            .effectivePerformanceMode = ls::effectivePerformanceMode(profile),
            .requiredGeneratedFrameCapacity =
                generatedFrameCapacityForActivePolicy(profile),
        };
    }

    /// Discrete scaler topology selections are complete user actions rather
    /// than continuous controls. Let the next successful present apply them
    /// immediately so a short return to gameplay cannot be overtaken by the
    /// next Quick Access edit. Numeric resource controls retain a quiet period
    /// to coalesce slider movement and avoid recreation storms.
    [[nodiscard]] inline std::chrono::milliseconds
    recreatedProfileResourceQuietPeriod(
            const RecreatedProfileResourceKey& current,
            const RecreatedProfileResourceKey& requested) noexcept {
        if (current.scalingEnabled != requested.scalingEnabled ||
                current.scalingMethod != requested.scalingMethod)
            return std::chrono::milliseconds::zero();
        return std::chrono::milliseconds(500);
    }

    /// Scaling dimensions and frame-generation context construction cannot be
    /// mutated while a swapchain is active. Arm one spec-defined OUT_OF_DATE
    /// signal after the final lower present has accepted MAKO's maintenance1
    /// retirement fence. Repeated polls of the same requested resources do not
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
                .signalAfter = now + recreatedProfileResourceQuietPeriod(
                    currentKey, requestedKey
                ),
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
        if (decision.processRestartDeferred)
            return false;
        return (decision.spatialScalingChanged &&
                !decision.spatialScalingLiveRebuild) ||
            ((decision.frameGenerationBackendChanged ||
              decision.generatedFrameCapacityExceeded) &&
             frameGenerationResourcesAvailable);
    }

    /// A natural swapchain recreation reuses Root's process-wide backend. Keep
    /// its actual GPU identity in the context profile so a requested GPU change
    /// remains visibly pending until the process restarts.
    [[nodiscard]] inline ls::GameConf profileForExistingBackend(
            const ls::GameConf& requested,
            const ls::GameConf& backendProfile) {
        auto applied = requested;
        applied.gpu = backendProfile.gpu;
        applied.ultra_performance = backendProfile.ultra_performance;
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

    /// Integer-cadence base-cap refinement is limited to the same ordered,
    /// target-matched Steady path as the 2x pacer handoff. Fractional Adaptive,
    /// HDR transport, recovery, and unmatched displays retain their existing
    /// pacing policy.
    [[nodiscard]] inline bool smoothCadenceBaseCapEligible(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        return profile.adaptive &&
            profile.adaptive_auto_base_fps_cap &&
            profile.adaptive_stable_cadence &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            adaptiveTargetMatchesRefresh(
                profile.target_fps, gamescopeRefreshHz
            );
    }

    struct GenerationSchedulerPolicy {
        uint32_t targetFps{0};
        size_t maximumMultiplier{0};
        bool stableCadence{false};
        bool nearTargetNativePreference{false};
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
                .nearTargetNativePreference = true,
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
            .nearTargetNativePreference = false,
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

    /// Build the profile that can be applied to an existing context while
    /// retaining requested resource-shape and process-static fields for their
    /// documented boundary. A pending restart-only edit must not prevent an
    /// unrelated cap, mode, target, or live generation switch from applying.
    [[nodiscard]] inline ProfileUpdatePlan planProfileUpdate(
            const ls::GameConf& current, const ls::GameConf& next,
            const size_t generatedFrameCapacity,
            const bool frameGenerationResourcesAvailable,
            const bool spatialScalerLiveRebuildAvailable = false) {
        ls::GameConf applied = next;
        bool swapchainRecreationDeferred = false;
        bool processRestartDeferred = false;

        // GPU, Ultra Performance, and Scaling Engine participate in
        // process-wide backend or Vulkan-layer construction. A natural
        // application swapchain recreation reuses those process owners and
        // therefore cannot satisfy any of these changes.
        if (current.gpu != next.gpu) {
            applied.gpu = current.gpu;
            processRestartDeferred = true;
        }
        if (current.ultra_performance != next.ultra_performance) {
            applied.ultra_performance = current.ultra_performance;
            applied.flow_scale = current.flow_scale;
            applied.performance_mode = current.performance_mode;
            processRestartDeferred = true;
        }
        if (current.scaling_enabled != next.scaling_enabled) {
            applied.scaling_enabled = current.scaling_enabled;
            processRestartDeferred = true;
        }

        // Scaler selection/tuning, Flow Scale, and model selection shape
        // private resources, while pacing shapes the game-owned swapchain.
        // They remain at their actually applied values until recreation
        // completes inside an already compatible process.
        const bool scalingMethodChanged =
            current.scaling_method != next.scaling_method;
        const bool scalingFactorChanged =
            current.scaling_factor != next.scaling_factor;
        const bool scalingSharpnessChanged =
            current.scaling_sharpness != next.scaling_sharpness;
        const bool scalerSettingsChanged = scalingMethodChanged ||
            scalingFactorChanged || scalingSharpnessChanged;
        const bool spatialScalingResourcesChanged =
            current.scaling_enabled == next.scaling_enabled &&
            (ls::spatialScalingRequested(current) ||
             ls::spatialScalingRequested(next)) &&
            scalerSettingsChanged;
        const bool spatialScalingLiveRebuild =
            spatialScalingResourcesChanged &&
            spatialScalerLiveRebuildAvailable &&
            !scalingFactorChanged;
        if (spatialScalingResourcesChanged) {
            applied.scaling_method = current.scaling_method;
            applied.scaling_factor = current.scaling_factor;
            applied.scaling_sharpness = current.scaling_sharpness;
            swapchainRecreationDeferred = !spatialScalingLiveRebuild;
        }
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
            processRestartDeferred = true;
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
        const bool spatialScalingChanged =
            current.scaling_enabled != next.scaling_enabled ||
            spatialScalingResourcesChanged;
        const bool frameGenerationBackendChanged =
            current.ultra_performance == next.ultra_performance && (
            ls::effectiveFlowScale(current) != ls::effectiveFlowScale(next) ||
            ls::effectivePerformanceMode(current) !=
                ls::effectivePerformanceMode(next));
        const bool generatedCapacityExceeded =
            generatedFrameCapacityForActivePolicy(next) >
                generatedFrameCapacity;
        const bool liveChange = frameGenerationChanged ||
            refreshRateThresholdChanged ||
                generationPolicyChanged ||
                generationModeChanged || fixedMultiplierChanged ||
                baseFpsCapChanged || dynamicCadenceProbeIntervalChanged ||
                spatialScalingLiveRebuild;

        ProfileUpdateAction action = ProfileUpdateAction::NoRuntimeChange;
        if (liveChange)
            action = ProfileUpdateAction::ApplyLive;
        else if (processRestartDeferred)
            action = ProfileUpdateAction::DeferUntilProcessRestart;
        else if (swapchainRecreationDeferred)
            action = ProfileUpdateAction::DeferUntilSwapchainRecreation;
        return {
            .appliedProfile = std::move(applied),
            .decision = {
                .action = action,
                .frameGenerationChanged = frameGenerationChanged,
                .refreshRateThresholdChanged =
                    refreshRateThresholdChanged,
                .generationPolicyChanged = generationPolicyChanged,
                .generationModeChanged = generationModeChanged,
                .fixedMultiplierChanged = fixedMultiplierChanged,
                .baseFpsCapChanged = baseFpsCapChanged,
                .dynamicCadenceProbeIntervalChanged =
                    dynamicCadenceProbeIntervalChanged,
                .spatialScalingChanged = spatialScalingChanged,
                .spatialScalingLiveRebuild =
                    spatialScalingLiveRebuild,
                .frameGenerationBackendChanged =
                    frameGenerationBackendChanged,
                .generatedFrameCapacityExceeded =
                    generatedCapacityExceeded,
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
