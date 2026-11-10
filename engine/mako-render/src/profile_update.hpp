/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "adaptive_policy_limits.hpp"
#include "adaptive_scheduler.hpp"
#include "mako-common/configuration/config.hpp"
#include "presentation_policy.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
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
        bool gpuHeadroomChanged{false};
        bool dynamicCadenceProbeIntervalChanged{false};
        bool spatialScalingChanged{false};
        bool spatialScalingDormantUpdate{false};
        bool spatialScalingEffectiveExtentUnchanged{false};
        bool spatialScalingLiveRebuild{false};
        bool frameGenerationBackendChanged{false};
        bool generatedFrameCapacityExceeded{false};
        bool frameGenerationPrivateRebuild{false};
        bool swapchainRecreationDeferred{false};
        bool processRestartDeferred{false};
        bool swapchainRecreationRequested{false};
    };

    struct ProfileUpdatePlan {
        ls::GameConf appliedProfile;
        ProfileUpdateDecision decision;
    };

    /// A confirmed Steam-menu suspension is already a scheduling boundary.
    /// Coalesce any number of live policy writes behind that boundary and
    /// construct only the final scheduler policy when gameplay resumes.
    class GenerationPolicyResetGate {
    public:
        [[nodiscard]] bool request(const bool generationSuspended) noexcept {
            if (generationSuspended) {
                this->deferred = true;
                return false;
            }
            this->deferred = false;
            return true;
        }

        [[nodiscard]] bool consumeOnResume() noexcept {
            return std::exchange(this->deferred, false);
        }

        void cancel() noexcept {
            this->deferred = false;
        }

        [[nodiscard]] bool pending() const noexcept {
            return this->deferred;
        }

    private:
        bool deferred{false};
    };

    /// A live change which reshapes generated work starts a new transport
    /// policy episode. Incomplete evidence from the old Fixed/Adaptive plan
    /// cannot classify the new plan as unhealthy. Resource-retirement and
    /// per-context recreation one-shot state remain independently owned.
    [[nodiscard]] constexpr bool
    profileUpdateInvalidatesTransientGenerationRecovery(
            const ProfileUpdateDecision& decision) noexcept {
        return decision.generationPolicyChanged ||
            decision.generationModeChanged ||
            decision.fixedMultiplierChanged ||
            decision.baseFpsCapChanged || decision.gpuHeadroomChanged;
    }

    [[nodiscard]] inline std::chrono::milliseconds
    spatialScalerRebuildQuietPeriod(
            const ls::GameConf& current,
            const ls::GameConf& requested) noexcept {
        if (ls::effectiveScalingMethod(current) !=
                ls::effectiveScalingMethod(requested) ||
                current.hdr_quality != requested.hdr_quality)
            return std::chrono::milliseconds::zero();
        return std::chrono::milliseconds(500);
    }

    struct ProcessStaticProfileProjection {
        ls::GameConf runtimeProfile;
        bool gpuSelectionPending{false};
        bool frameGenerationModelPending{false};
        bool frameGenerationProvisioningPending{false};
        bool ultraPerformancePending{false};
        bool scalingEnginePending{false};
        bool swapchainImageCountCompatibilityPending{false};
        bool mailboxPresentationCompatibilityPending{false};

        [[nodiscard]] bool restartRequired() const {
            return this->gpuSelectionPending || this->frameGenerationModelPending ||
                this->frameGenerationProvisioningPending ||
                this->ultraPerformancePending ||
                this->scalingEnginePending ||
                this->swapchainImageCountCompatibilityPending ||
                this->mailboxPresentationCompatibilityPending;
        }
    };

    /// Retain settings selected during process/device/backend construction
    /// while allowing unrelated fields from the requested profile to continue
    /// through the normal live-update path.
    [[nodiscard]] inline ProcessStaticProfileProjection
    projectProcessStaticProfileForLiveUpdate(
            const ls::GameConf& current,
            const ls::GameConf& requested,
            const bool frameGenerationProvisionedAtStartup,
            const bool scalingEngineConfiguredAtStartup,
            const bool swapchainImageCountCompatibilityConfiguredAtStartup,
            const bool mailboxPresentationCompatibilityConfiguredAtStartup) {
        ProcessStaticProfileProjection projection{
            .runtimeProfile = requested,
            .gpuSelectionPending = requested.gpu != current.gpu,
            .frameGenerationModelPending = requested.frame_generation_model != current.frame_generation_model,
            .frameGenerationProvisioningPending =
                requested.frame_generation_provisioned !=
                    frameGenerationProvisionedAtStartup,
            .ultraPerformancePending = requested.ultra_performance !=
                current.ultra_performance,
            .scalingEnginePending = requested.scaling_enabled !=
                scalingEngineConfiguredAtStartup,
            .swapchainImageCountCompatibilityPending =
                requested.swapchain_image_count_compatibility !=
                    swapchainImageCountCompatibilityConfiguredAtStartup,
            .mailboxPresentationCompatibilityPending =
                requested.mailbox_presentation_compatibility !=
                    mailboxPresentationCompatibilityConfiguredAtStartup,
        };
        if (projection.frameGenerationModelPending)
            projection.runtimeProfile.frame_generation_model = current.frame_generation_model;
        if (projection.gpuSelectionPending)
            projection.runtimeProfile.gpu = current.gpu;
        if (projection.frameGenerationProvisioningPending) {
            projection.runtimeProfile.frame_generation_provisioned =
                frameGenerationProvisionedAtStartup;
        }
        if (projection.ultraPerformancePending) {
            projection.runtimeProfile.ultra_performance =
                current.ultra_performance;
            projection.runtimeProfile.flow_scale = current.flow_scale;
            projection.runtimeProfile.mako_fg_mode = current.mako_fg_mode;
            projection.runtimeProfile.performance_mode =
                current.performance_mode;
        }
        if (projection.scalingEnginePending) {
            projection.runtimeProfile.scaling_enabled =
                scalingEngineConfiguredAtStartup;
        }
        if (projection.swapchainImageCountCompatibilityPending) {
            projection.runtimeProfile.swapchain_image_count_compatibility =
                swapchainImageCountCompatibilityConfiguredAtStartup;
        }
        if (projection.mailboxPresentationCompatibilityPending) {
            projection.runtimeProfile.mailbox_presentation_compatibility =
                mailboxPresentationCompatibilityConfiguredAtStartup;
        }
        return projection;
    }

    /// Private generated outputs are selected by the active Fixed/Adaptive
    /// policy. Initial construction may reserve more for the inactive policy,
    /// but the active requirement is the boundary used for live updates. A
    /// context with Frame Generation Off has no active generated-output
    /// requirement. This is important for the isolated spatial role: it
    /// deliberately receives a Frame-Generation-Off profile and must never
    /// report an impossible generated-image-capacity transition of its own.
    [[nodiscard]] inline size_t generatedFrameCapacityForActivePolicy(
            const ls::GameConf& profile) {
        if (!profile.frame_generation_provisioned ||
                !profile.frame_generation_enabled)
            return 0;
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
        live.swapchain_image_count_compatibility =
            current.swapchain_image_count_compatibility;
        live.mailbox_presentation_compatibility =
            current.mailbox_presentation_compatibility;
        live.scaling_method = current.scaling_method;
        live.scaling_factor = current.scaling_factor;
        live.scaling_supersampling = current.scaling_supersampling;
        live.scaling_sharpness = current.scaling_sharpness;
        live.hdr_quality = current.hdr_quality;
        live.flow_scale = current.flow_scale;
        live.mako_fg_mode = current.mako_fg_mode;
        live.performance_mode = current.performance_mode;
        if (generatedFrameCapacityPending) {
            live.adaptive = current.adaptive;
            live.multiplier = current.multiplier;
            live.adaptive_max_multiplier = current.adaptive_max_multiplier;
        }
        return live;
    }

    /// Model settings a private FG context opens with. Swapchain creation and
    /// live replacement share this, so neither can drop a setting and every
    /// setting joins the comparison that coalesces replacement requests.
    struct FrameGenerationContextSettings {
        float flow{1.0F};
        bool performanceMode{false};
        bool reducedHdrPrecision{false};
        int motionPeriod{1};

        friend bool operator==(
            const FrameGenerationContextSettings&,
            const FrameGenerationContextSettings&) = default;
    };

    [[nodiscard]] inline FrameGenerationContextSettings
    frameGenerationContextSettings(const ls::GameConf& profile) {
        return {
            .flow = 1.0F / ls::effectiveFlowScale(profile),
            .performanceMode = ls::effectivePerformanceMode(profile),
            .reducedHdrPrecision = ls::hdrApproximateGeneration(profile.hdr_quality),
            .motionPeriod = ls::makoFgMotionPeriod(ls::effectiveMakoFgMode(profile)),
        };
    }

    struct RecreatedProfileResourceKey {
        bool scalingEnabled{false};
        ls::ScalingMethod scalingMethod{ls::ScalingMethod::Native};
        float scalingFactor{1.0F};
        bool scalingSupersampling{false};
        float scalingSharpness{0.5F};
        ls::HdrQuality hdrQuality{ls::HdrQuality::Quality};
        float effectiveFlowScale{ls::GameConfDefaults::flowScale};
        bool effectivePerformanceMode{false};
        ls::MakoFgMode effectiveMakoFgMode{ls::MakoFgMode::Quality};
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
                ? ls::effectiveScalingMethod(profile)
                : ls::ScalingMethod::Native,
            .scalingFactor = scalingActive ? profile.scaling_factor : 1.0F,
            .scalingSupersampling = scalingActive
                ? profile.scaling_supersampling : false,
            .scalingSharpness = scalingActive
                ? profile.scaling_sharpness : 0.5F,
            .hdrQuality = (scalingActive || profile.frame_generation_provisioned)
                ? profile.hdr_quality : ls::HdrQuality::Quality,
            .effectiveFlowScale = ls::effectiveFlowScale(profile),
            .effectivePerformanceMode = ls::effectivePerformanceMode(profile),
            .effectiveMakoFgMode = ls::effectiveMakoFgMode(profile),
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
                current.scalingMethod != requested.scalingMethod ||
                current.scalingSupersampling !=
                    requested.scalingSupersampling ||
                current.hdrQuality != requested.hdrQuality)
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
             !decision.frameGenerationPrivateRebuild &&
             frameGenerationResourcesAvailable);
    }

    /// A recreation request is safe only when the resource owner's final
    /// present is protected by swapchain-maintenance1 retirement proof.
    /// Managed Gamescope may use that path only for the role that owns the
    /// active scaler's extent change; frame-generation-only resource changes
    /// remain private-context transitions.
    [[nodiscard]] inline bool guardedLiveProfileResourceRecreationAvailable(
            const ProfileUpdateDecision& decision,
            const bool frameGenerationResourcesAvailable,
            const bool presentRetirementEnabled,
            const bool gamescopeDetected,
            const bool spatialResourceOwner) {
        if (!presentRetirementEnabled ||
                !liveProfileResourceRecreationAvailable(
                    decision, frameGenerationResourcesAvailable
                )) {
            return false;
        }
        return !gamescopeDetected ||
            (spatialResourceOwner && decision.spatialScalingChanged);
    }

    /// A natural swapchain recreation reuses Root's process-wide backend. Keep
    /// its actual GPU identity in the context profile so a requested GPU change
    /// remains visibly pending until the process restarts.
    [[nodiscard]] inline ls::GameConf profileForExistingBackend(
            const ls::GameConf& requested,
            const ls::GameConf& backendProfile) {
        auto applied = requested;
        applied.gpu = backendProfile.gpu;
        applied.frame_generation_model = backendProfile.frame_generation_model;
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
            const ls::GameConf& requestedProfile,
            const std::optional<ls::FrameGenerationModel> modelFallbackFrom = std::nullopt) {
        return backendProfile && (
            backendProfile->gpu != requestedProfile.gpu ||
            (backendProfile->frame_generation_model != requestedProfile.frame_generation_model &&
                requestedProfile.frame_generation_model != modelFallbackFrom) ||
            backendProfile->ultra_performance !=
                requestedProfile.ultra_performance
        );
    }

    /// Return the Fractional Adaptive cap selected from short, stable
    /// real/generated ratios. Auto leaves the saved manual cap authoritative.
    [[nodiscard]] inline double fractionalRealFramePriorityCap(
            const ls::GameConf& profile) {
        return ls::adaptiveFractionalRealFramePriorityCap(
            profile.adaptive_fractional_real_frame_priority,
            profile.target_fps
        );
    }

    [[nodiscard]] inline bool fractionalRealFramePriorityActive(
            const ls::GameConf& profile) {
        return profile.adaptive && !profile.adaptive_auto_base_fps_cap &&
            profile.adaptive_fractional_real_frame_priority !=
                ls::AdaptiveFractionalRealFramePriority::Auto;
    }

    /// Preserve the user's live Frame Generation switch while applying the
    /// optional display guard. Missing Gamescope feedback fails open so an
    /// unsupported compositor cannot silently disable synthesis.
    [[nodiscard]] inline bool effectiveFrameGenerationEnabled(
            const ls::GameConf& profile,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        if (!profile.frame_generation_provisioned ||
                !profile.frame_generation_enabled)
            return false;
        return profile.frame_generation_refresh_threshold == 0 ||
            !gamescopeRefreshHz ||
            *gamescopeRefreshHz > profile.frame_generation_refresh_threshold;
    }

    /// Use the existing cap interval for Fractional, below-refresh automatic
    /// caps, and exact Fixed batches that fit the known refresh. A compositor
    /// output clock can also prepare inside a target-matched automatic cap;
    /// direct FIFO retains its existing target-matched source pacing.
    [[nodiscard]] inline bool baseCapPacesOutputs(
            const ls::GameConf& profile, const double effectiveCap,
            const bool untimedOrderedTransport,
            const std::optional<uint32_t> gamescopeRefreshHz = std::nullopt,
            const bool compositorOutputTiming = false) {
        if (!effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) ||
                !untimedOrderedTransport || !std::isfinite(effectiveCap) ||
                effectiveCap <= 0.0)
            return false;
        if (profile.adaptive)
            return static_cast<double>(profile.target_fps) > effectiveCap &&
                (!profile.adaptive_auto_base_fps_cap ||
                    (gamescopeRefreshHz &&
                        (profile.target_fps < *gamescopeRefreshHz ||
                         (compositorOutputTiming &&
                          profile.target_fps == *gamescopeRefreshHz))));
        return !profile.dynamic_cadence_recovery &&
            (profile.base_fps_cap > 0 || profile.gpu_headroom_percent > 0) &&
            profile.multiplier >= ls::GameConfLimits::minimumMultiplier &&
            profile.multiplier <= ls::GameConfLimits::maximumMultiplier &&
            gamescopeRefreshHz &&
            effectiveCap * profile.multiplier <= *gamescopeRefreshHz;
    }

    /// Auto-cap aligns the common healthy path with an exact 2x cadence.
    /// Explicit Fractional priority instead selects its target-relative cap.
    /// Adaptive can still raise its multiplier when the game falls below this
    /// ceiling. Keep the engine's 10 FPS policy floor for unusually low targets.
    /// The display guard releases both target-derived Adaptive caps while
    /// preserving the saved manual cap. Explicit Off keeps all caps dormant.
    [[nodiscard]] inline double effectiveBaseFpsCap(
            const ls::GameConf& profile,
            const std::optional<uint32_t> gamescopeRefreshHz = std::nullopt) {
        if (!profile.frame_generation_provisioned ||
                !profile.frame_generation_enabled)
            return 0.0;
        if (!effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz))
            return static_cast<double>(profile.base_fps_cap);
        if (profile.adaptive && profile.adaptive_auto_base_fps_cap) {
            return std::max(
                adaptiveMinimumBaseFps,
                static_cast<double>(profile.target_fps) / 2.0
            );
        }
        if (fractionalRealFramePriorityActive(profile))
            return fractionalRealFramePriorityCap(profile);
        return static_cast<double>(profile.base_fps_cap);
    }

    /// A proven Ordered-SDR collapse may ask the presentation pacer to release
    /// only Adaptive's automatic half-target cap. Manual and Fixed caps remain
    /// authoritative. The release lasts only for the bounded rescue sample;
    /// verification runs after the configured cap has been restored.
    [[nodiscard]] inline double effectiveBaseFpsCap(
            const ls::GameConf& profile,
            const AdaptiveSchedulerSnapshot& scheduler,
            const std::optional<uint32_t> gamescopeRefreshHz = std::nullopt) {
        if (!effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz))
            return effectiveBaseFpsCap(profile, gamescopeRefreshHz);
        if (profile.adaptive && profile.adaptive_auto_base_fps_cap &&
                scheduler.automaticBaseCapSuppressed) {
            return 0.0;
        }
        return effectiveBaseFpsCap(profile, gamescopeRefreshHz);
    }

    /// Ordered-SDR rescue can release an automatic cap after FIFO masks source
    /// headroom. Under explicit VRR, retain the configured automatic cap
    /// outside a qualified FIFO handoff, without resetting the
    /// scheduler or discarding its validated multiplier.
    [[nodiscard]] inline double effectiveBaseFpsCap(
            const ls::GameConf& profile,
            const AdaptiveSchedulerSnapshot& scheduler,
            const GamescopePresentationFeedback& presentationFeedback,
            const std::optional<uint32_t> gamescopeRefreshHz = std::nullopt) {
        if (presentationFeedback.variableRefreshRequested())
            return effectiveBaseFpsCap(profile, gamescopeRefreshHz);
        return effectiveBaseFpsCap(profile, scheduler, gamescopeRefreshHz);
    }

    /// Without a Base FPS Cap, Fixed leaves a game paced by FIFO backpressure,
    /// which keeps the queue full. A Zink swapchain's acquire window then
    /// makes every frame wait for Zink's present worker, and that hand-off
    /// misses refreshes: real frames alternate about 25 ms and 8 ms at an exact
    /// rate. Pace such a swapchain at the refresh divided by the multiplier,
    /// the rate FIFO already allows, so the capped path and its backlog drain
    /// own it. Dynamic Cadence Recovery clears base caps to measure the native
    /// rate, so it keeps FIFO pacing. Zero keeps the configured pacing.
    [[nodiscard]] inline double zinkRefreshLimitedBaseFpsCap(
            const ls::GameConf& profile,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        if (profile.adaptive || profile.base_fps_cap > 0 ||
                profile.dynamic_cadence_recovery || profile.multiplier < 2 ||
                !gamescopeRefreshHz || *gamescopeRefreshHz == 0 ||
                !effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz))
            return 0.0;
        return static_cast<double>(*gamescopeRefreshHz) /
            static_cast<double>(profile.multiplier);
    }

    [[nodiscard]] inline bool dynamicCadenceRecoveryEnabled(
            const ls::GameConf& profile) {
        return profile.dynamic_cadence_recovery &&
            effectiveBaseFpsCap(profile) <= 0.0;
    }

    /// A desktop compositor gives MAKO neither refresh feedback nor an output
    /// clock, so ordered FIFO shows a generated batch on consecutive refreshes.
    /// Fixed and Adaptive spread it across the measured source interval there
    /// instead. Any Gamescope signal keeps the Gamescope pacing paths, and
    /// Dynamic Cadence Recovery keeps its unspaced native measurement.
    [[nodiscard]] inline bool sourceIntervalSpacingApplies(
            const ls::GameConf& profile, const bool orderedFifoTransport,
            const bool gamescopeSession,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const bool compositorOutputTiming) {
        return orderedFifoTransport && !gamescopeSession && !gamescopeRefreshHz &&
            !compositorOutputTiming && !dynamicCadenceRecoveryEnabled(profile) &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz);
    }

    /// A proven Steady 2x cadence keeps its established FIFO handoff. Under
    /// VRR, Steady may request a validated higher full batch, while Fractional
    /// may hand off only an already accepted constant integer cadence with no
    /// competing real-frame cap. Probe and transport guards restore pacing.
    [[nodiscard]] inline std::optional<size_t>
    smoothCadencePacerHandoffGenerationLimit(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const AdaptiveSchedulerSnapshot& scheduler,
            const GamescopePresentationFeedback& presentationFeedback = {},
            const std::optional<size_t> activeGenerationLimit = std::nullopt) {
        const size_t generationLimit = scheduler.stableCadenceLimit.value_or(
            scheduler.validatedGenerationLimit
        );
        if (generationLimit == 0 ||
                generationLimit >= profile.adaptive_max_multiplier)
            return std::nullopt;
        const double targetFps = static_cast<double>(profile.target_fps);
        const double projectedOutputFps = scheduler.smoothedBaseFps *
            static_cast<double>(generationLimit + 1);
        const bool higherVrrRung = generationLimit > 1 &&
            presentationFeedback.variableRefreshRequested() &&
            scheduler.validatedGenerationLimit >= generationLimit &&
            (scheduler.phase == AdaptiveSchedulerPhase::Active ||
             scheduler.phase == AdaptiveSchedulerPhase::StableCadence) &&
            !scheduler.efficiencyProbeGenerationLimit &&
            (!scheduler.stableCadenceEvaluationActive ||
             activeGenerationLimit == generationLimit);
        const bool provenTwoX = generationLimit == 1 &&
            scheduler.phase == AdaptiveSchedulerPhase::StableCadence &&
            !scheduler.stableCadenceEvaluationActive;
        const bool acceptedFractionalVrrInteger =
            !profile.adaptive_auto_base_fps_cap &&
            effectiveBaseFpsCap(profile, gamescopeRefreshHz) <= 0.0 &&
            !profile.dynamic_cadence_recovery &&
            presentationFeedback.variableRefreshRequested() &&
            scheduler.phase == AdaptiveSchedulerPhase::StableCadence &&
            scheduler.stableCadenceLimit == generationLimit &&
            scheduler.validatedGenerationLimit >= generationLimit &&
            scheduler.generationLimit >= generationLimit &&
            !scheduler.stableCadenceEvaluationActive &&
            !scheduler.efficiencyProbeGenerationLimit &&
            !scheduler.targetOutputClockActive;
        const bool entryCadenceMatched =
            projectedOutputFps >= targetFps * 0.98 &&
            projectedOutputFps <= targetFps *
                (generationLimit == 1 ? 1.02 : 1.25);
        // Entry needs a close target match. Once accepted, the scheduler's
        // sustained cadence/delivery qualification owns retention; applying
        // entry thresholds again would turn one noisy estimate into a failed
        // handoff and a minute-long cooldown. All eligibility guards still apply.
        const bool retainActiveHandoff =
            activeGenerationLimit == generationLimit;
        const bool eligible = profile.adaptive &&
            profile.adaptive_stable_cadence &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            adaptiveTargetMatchesRefresh(
                profile.target_fps, gamescopeRefreshHz
            ) &&
            ((profile.adaptive_auto_base_fps_cap &&
              (provenTwoX || higherVrrRung)) ||
             acceptedFractionalVrrInteger) &&
            (entryCadenceMatched || retainActiveHandoff);
        return eligible ? std::optional<size_t>{generationLimit} : std::nullopt;
    }

    /// A lower-load probe deliberately releases a higher Steady VRR FIFO
    /// handoff. Preserve its retry eligibility unless a transport or scheduler
    /// safety guard also failed during the probe.
    [[nodiscard]] inline bool smoothCadencePacerHandoffPlannedProbe(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const AdaptiveSchedulerSnapshot& scheduler,
            const GamescopePresentationFeedback& presentationFeedback,
            const std::optional<size_t> activeGenerationLimit) {
        return profile.adaptive &&
            (profile.adaptive_auto_base_fps_cap ||
             (effectiveBaseFpsCap(profile, gamescopeRefreshHz) <= 0.0 &&
              scheduler.pacedLoadValidationPending &&
              scheduler.efficiencyProbeGenerationLimit) ||
             (activeGenerationLimit &&
              scheduler.pacedLoadRollbackGenerationLimit ==
                  activeGenerationLimit)) &&
            profile.adaptive_stable_cadence &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            adaptiveTargetMatchesRefresh(
                profile.target_fps, gamescopeRefreshHz
            ) &&
            presentationFeedback.variableRefreshRequested() &&
            activeGenerationLimit && *activeGenerationLimit > 1 &&
            (scheduler.phase == AdaptiveSchedulerPhase::Active ||
             scheduler.phase == AdaptiveSchedulerPhase::StableCadence) &&
            ((scheduler.efficiencyProbeGenerationLimit &&
              *scheduler.efficiencyProbeGenerationLimit <
                  *activeGenerationLimit) ||
             scheduler.pacedLoadRollbackGenerationLimit ==
                 activeGenerationLimit) &&
            (scheduler.validatedGenerationLimit >= *activeGenerationLimit ||
             scheduler.pacedLoadRollbackGenerationLimit ==
                 activeGenerationLimit) &&
            !scheduler.rampEvaluationActive &&
            !scheduler.stableCadenceEvaluationActive &&
            !scheduler.nativeCadenceProbeActive &&
            !scheduler.rearmRequired &&
            !scheduler.discontinuityRecoveryActive;
    }

    /// Integer-cadence base-cap refinement is limited to the same ordered,
    /// target-matched Steady path as the FIFO handoff. Fractional Adaptive,
    /// HDR transport, recovery, and unmatched displays retain their existing
    /// pacing policy.
    [[nodiscard]] inline bool smoothCadenceBaseCapEligible(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const GamescopePresentationFeedback& presentationFeedback = {}) {
        return profile.adaptive &&
            profile.adaptive_auto_base_fps_cap &&
            profile.adaptive_stable_cadence &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            presentationFeedback.fixedRefreshPacingEligible() &&
            adaptiveTargetMatchesRefresh(
                profile.target_fps, gamescopeRefreshHz
            );
    }

    /// Fixed Smooth Cadence lets ordered FIFO pace a full multiplier instead
    /// of sleeping in the application's present call, including when
    /// Gamescope requests VRR. Explicit caps and recovery retain their
    /// existing output budget; the caller also verifies that the private
    /// output pool can deliver the selected multiplier.
    [[nodiscard]] inline bool fixedSmoothCadenceFifoEligible(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz) {
        return !profile.adaptive &&
            profile.adaptive_stable_cadence &&
            profile.base_fps_cap == 0 &&
            !profile.dynamic_cadence_recovery &&
            profile.multiplier >= 2 &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            gamescopeRefreshHz.has_value() && *gamescopeRefreshHz > 0;
    }

    /// A compositor presentation update resets pacing only when it changes
    /// Steady Adaptive's fixed-refresh cap eligibility. Eligible Steady or
    /// Fractional FIFO handoffs and Fixed Smooth Cadence retain their owner.
    [[nodiscard]] inline bool gamescopePresentationPacingOwnerChanged(
            const ls::GameConf& profile,
            const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const AdaptiveSchedulerSnapshot& scheduler,
            const std::optional<size_t> activeGenerationLimit,
            const GamescopePresentationFeedback& previous,
            const GamescopePresentationFeedback& current) {
        if (smoothCadencePacerHandoffGenerationLimit(
                    profile, privateOrderedTransport,
                    orderedAcquireRecoveryActive, gamescopeRefreshHz,
                    scheduler, current, activeGenerationLimit))
            return false;
        return smoothCadenceBaseCapEligible(
                    profile, privateOrderedTransport,
                    orderedAcquireRecoveryActive, gamescopeRefreshHz,
                    previous) !=
                smoothCadenceBaseCapEligible(
                    profile, privateOrderedTransport,
                    orderedAcquireRecoveryActive, gamescopeRefreshHz,
                    current);
    }

    /// An explicit Steam VRR preference change permits a fresh higher-load
    /// trial. Active feedback alone can flap under overlays; initial/missing
    /// feedback and incapable connectors cannot invalidate workload evidence.
    [[nodiscard]] inline bool gamescopeHigherLoadRetryChanged(
            const ls::GameConf& profile, const bool privateOrderedTransport,
            const bool orderedAcquireRecoveryActive,
            const std::optional<uint32_t> gamescopeRefreshHz,
            const GamescopePresentationFeedback& previous,
            const GamescopePresentationFeedback& current) {
        return profile.adaptive && privateOrderedTransport &&
            !orderedAcquireRecoveryActive &&
            effectiveFrameGenerationEnabled(profile, gamescopeRefreshHz) &&
            previous.vrrEnabled.has_value() && current.vrrEnabled.has_value() &&
            previous.vrrEnabled != current.vrrEnabled &&
            previous.vrrCapable != false && current.vrrCapable != false;
    }

    /// Only Fractional Automatic can release the generated presentation ceiling.
    /// A target-capable manual cap remains enforced by the source pacer.
    [[nodiscard]] inline bool fractionalNativePresentationEligible(
            const ls::GameConf& profile) {
        return profile.adaptive && !profile.adaptive_auto_base_fps_cap &&
            profile.adaptive_fractional_real_frame_priority ==
                ls::AdaptiveFractionalRealFramePriority::Auto &&
            (profile.base_fps_cap == 0 || profile.base_fps_cap >= profile.target_fps);
    }

    struct GenerationSchedulerPolicy {
        uint32_t targetFps{0};
        size_t maximumMultiplier{0};
        bool stableCadence{false};
        bool nearTargetNativePreference{false};
        bool qualifyTargetNativeCadence{false};
        bool dynamicCadenceRecovery{false};
        float dynamicCadenceProbeIntervalSeconds{
            ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds
        };
    };

    /// Resolve only an opted-in Adaptive target. The saved fallback is never
    /// overwritten by display feedback; the scheduler retains its supported range.
    [[nodiscard]] constexpr uint32_t adaptiveTargetFpsForDisplay(
            const uint32_t fallbackTargetFps, const bool adaptive,
            const bool matchDisplayRefresh,
            const std::optional<uint32_t> displayRefreshHz) noexcept {
        if (!adaptive || !matchDisplayRefresh || !displayRefreshHz ||
                *displayRefreshHz == 0)
            return fallbackTargetFps;
        return std::clamp(*displayRefreshHz,
            ls::GameConfLimits::minimumTargetFps,
            ls::GameConfLimits::maximumTargetFps);
    }

    /// Apply only the policy established by a committed private FG resource set.
    /// Live target edits and compositor feedback may have changed during preparation.
    inline void applyFrameGenerationResourceProfile(ls::GameConf& applied,
            const ls::GameConf& resources, const ls::GameConf& requested,
            const std::optional<uint32_t> displayRefreshHz) {
        applied.flow_scale = resources.flow_scale;
        applied.mako_fg_mode = resources.mako_fg_mode;
        applied.performance_mode = resources.performance_mode;
        applied.adaptive = resources.adaptive;
        applied.multiplier = resources.multiplier;
        applied.adaptive_max_multiplier = resources.adaptive_max_multiplier;
        applied.target_fps = adaptiveTargetFpsForDisplay(
            requested.target_fps, applied.adaptive,
            applied.adaptive_target_refresh_rate, displayRefreshHz
        );
    }

    /// Adaptive owns the resolved applied output target. Fixed + Dynamic Cadence
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
                .qualifyTargetNativeCadence =
                    fractionalNativePresentationEligible(profile),
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

    struct GamescopeBridgeOutputPolicy {
        double outputFps{0.0};
        bool generatedTimeline{false};
    };

    /// Qualified native Automatic presentation releases both the Adaptive
    /// ceiling and generated-work lead. A sparse Fractional turn or temporary
    /// native probe still belongs to the generated timeline. A manual cap at
    /// or above target remains enforced by the source pacer. Below-target caps,
    /// explicit priorities and Steady retain their existing output policy.
    [[nodiscard]] inline GamescopeBridgeOutputPolicy gamescopeBridgeOutputPolicy(
            const ls::GameConf& profile, const std::optional<uint32_t> refreshHz,
            const AdaptiveSchedulerSnapshot& scheduler = {},
            const double gpuHeadroomCap = 0.0) {
        double baseFpsCap = effectiveBaseFpsCap(profile, refreshHz);
        if (gpuHeadroomCap > 0.0)
            baseFpsCap = baseFpsCap > 0.0 ? std::min(baseFpsCap, gpuHeadroomCap) : gpuHeadroomCap;
        const bool nativeAutomatic = fractionalNativePresentationEligible(profile) &&
            scheduler.phase == AdaptiveSchedulerPhase::NearTargetNative &&
            scheduler.generationLimit == 0;
        const bool generatedTimeline =
            effectiveFrameGenerationEnabled(profile, refreshHz) && !nativeAutomatic;
        double outputFps = static_cast<double>(refreshHz.value_or(0));
        if (generatedTimeline) {
            if (profile.adaptive)
                outputFps = std::min(outputFps,
                    static_cast<double>(profile.target_fps));
            else if (baseFpsCap > 0.0)
                outputFps = std::min(outputFps,
                    baseFpsCap * static_cast<double>(profile.multiplier));
        }
        return {outputFps, generatedTimeline};
    }

    /// Reserve one private output set that can serve both Fixed and Adaptive.
    /// Ultra Performance deliberately keeps only the active policy. A live
    /// mode or multiplier change that needs a different capacity prepares and
    /// atomically replaces MAKO's private frame-generation context.
    [[nodiscard]] inline size_t generatedFrameCapacityForProfile(
            const ls::GameConf& profile) {
        if (!profile.frame_generation_provisioned)
            return 0;
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
            const bool spatialScalerActive = false,
            const bool frameGenerationPrivateRebuildAvailable = false,
            const bool spatialScalingActivationSupported = true,
            const bool spatialScalingEffectiveExtentUnchanged = false,
            const bool spatialSupersamplingEffectiveExtentUnchanged = false,
            const std::optional<uint32_t> displayRefreshHz = std::nullopt,
            const bool hdrScalingActive = false,
            const bool hdrFrameGenerationPrecisionChanged = false,
            const bool internalNativeSpatialResize = false) {
        ls::GameConf applied = next;
        bool swapchainRecreationDeferred = false;
        bool processRestartDeferred = false;

        // GPU, Frame Generation provisioning, Ultra Performance, and Scaling
        // Engine participate in
        // process-wide backend or Vulkan-layer construction. A natural
        // application swapchain recreation reuses those process owners and
        // therefore cannot satisfy any of these changes.
        if (current.frame_generation_model != next.frame_generation_model) {
            applied.frame_generation_model = current.frame_generation_model;
            processRestartDeferred = true;
        }
        if (current.gpu != next.gpu) {
            applied.gpu = current.gpu;
            processRestartDeferred = true;
        }
        if (current.frame_generation_provisioned !=
                next.frame_generation_provisioned) {
            applied.frame_generation_provisioned =
                current.frame_generation_provisioned;
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

        const bool frameGenerationBackendChanged = hdrFrameGenerationPrecisionChanged ||
            (current.ultra_performance == next.ultra_performance && (
            ls::effectiveFlowScale(current) != ls::effectiveFlowScale(applied) ||
            ls::effectivePerformanceMode(current) !=
                ls::effectivePerformanceMode(applied) ||
            ls::effectiveMakoFgMode(current) != ls::effectiveMakoFgMode(applied)));
        const bool generatedCapacityExceeded =
            generatedFrameCapacityForActivePolicy(next) >
                generatedFrameCapacity;
        const bool frameGenerationPrivateRebuild =
            frameGenerationPrivateRebuildAvailable &&
            frameGenerationResourcesAvailable &&
            !processRestartDeferred &&
            (frameGenerationBackendChanged || generatedCapacityExceeded);

        // Scaler selection/tuning, Flow Scale, and model selection shape
        // private resources, while pacing shapes the game-owned swapchain.
        // They remain at their actually applied values until recreation
        // completes inside an already compatible process.
        const bool scalingMethodChanged =
            ls::effectiveScalingMethod(current) !=
                ls::effectiveScalingMethod(applied);
        const bool scalingFactorChanged =
            !internalNativeSpatialResize && current.scaling_factor != next.scaling_factor;
        const bool scalingSupersamplingChanged =
            !internalNativeSpatialResize && current.scaling_supersampling != next.scaling_supersampling;
        const bool scalingSharpnessChanged =
            current.scaling_sharpness != next.scaling_sharpness;
        const bool hdrPrecisionChanged = hdrScalingActive &&
            ls::hdrCompactBuffers(current.hdr_quality) != ls::hdrCompactBuffers(next.hdr_quality);
        const bool scalerSettingsChanged = scalingMethodChanged ||
            scalingFactorChanged || scalingSupersamplingChanged ||
            scalingSharpnessChanged || hdrPrecisionChanged;
        const bool spatialScalingResourcesChanged =
            // The internal Native output resize owns only precision. Saved
            // scaler choices remain dormant while the Scaling toggle is off.
            internalNativeSpatialResize ? spatialScalerActive && hdrPrecisionChanged :
            current.scaling_enabled == next.scaling_enabled &&
            (ls::spatialScalingRequested(current) ||
             ls::spatialScalingRequested(next)) &&
            scalerSettingsChanged;
        // Method and sharpness are dormant profile choices when this
        // swapchain has no scaler (for example an extent that cannot upscale
        // inside the allocation envelope). Save them immediately so the next
        // eligible natural recreation uses the latest choice, but do not
        // disturb the WSI or frame-generation contexts merely to rebuild a
        // resource that does not exist. Factor remains extent-bound because
        // changing it can make the current source eligible.
        const bool spatialScalingDormantUpdate =
            spatialScalingResourcesChanged && !spatialScalerActive &&
            (!(scalingFactorChanged || scalingSupersamplingChanged) ||
             !spatialScalingActivationSupported);
        const bool spatialScalingExtentNoOp =
            spatialScalingResourcesChanged &&
            !hdrPrecisionChanged &&
            (scalingFactorChanged || scalingSupersamplingChanged) &&
            (!scalingFactorChanged ||
             spatialScalingEffectiveExtentUnchanged) &&
            (!scalingSupersamplingChanged ||
             spatialSupersamplingEffectiveExtentUnchanged);
        const bool spatialScalingLiveRebuild =
            spatialScalingResourcesChanged &&
            spatialScalerActive &&
            (!scalingFactorChanged ||
                (hdrPrecisionChanged && spatialScalingEffectiveExtentUnchanged)) &&
            (!scalingSupersamplingChanged ||
                (hdrPrecisionChanged && spatialSupersamplingEffectiveExtentUnchanged));
        if (spatialScalingResourcesChanged &&
                !spatialScalingDormantUpdate &&
                !spatialScalingExtentNoOp) {
            if (!internalNativeSpatialResize)
                applied.scaling_method = current.scaling_method;
            // A precision rebuild can accompany extent-neutral profile edits.
            // Those values need no resource commit: the private scaler owns
            // only method, sharpness and precision, not the saved geometry.
            if (!spatialScalingLiveRebuild) {
                applied.scaling_factor = current.scaling_factor;
                applied.scaling_supersampling = current.scaling_supersampling;
            }
            if (!internalNativeSpatialResize)
                applied.scaling_sharpness = current.scaling_sharpness;
            if (hdrPrecisionChanged)
                applied.hdr_quality = current.hdr_quality;
            swapchainRecreationDeferred = !spatialScalingLiveRebuild;
        }
        if (ls::effectiveFlowScale(current) !=
                ls::effectiveFlowScale(applied)) {
            applied.flow_scale = current.flow_scale;
            swapchainRecreationDeferred |= !frameGenerationPrivateRebuild;
        }
        if (ls::effectiveMakoFgMode(current) !=
                ls::effectiveMakoFgMode(applied)) {
            applied.mako_fg_mode = current.mako_fg_mode;
            swapchainRecreationDeferred |= !frameGenerationPrivateRebuild;
        }
        if (ls::effectivePerformanceMode(current) !=
                ls::effectivePerformanceMode(applied)) {
            applied.performance_mode = current.performance_mode;
            swapchainRecreationDeferred |= !frameGenerationPrivateRebuild;
        }
        if (hdrFrameGenerationPrecisionChanged) {
            applied.hdr_quality = current.hdr_quality;
            swapchainRecreationDeferred |= !frameGenerationPrivateRebuild;
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
            swapchainRecreationDeferred |= !frameGenerationPrivateRebuild;
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
        applied.target_fps = adaptiveTargetFpsForDisplay(
            next.target_fps, applied.adaptive,
            applied.adaptive_target_refresh_rate, displayRefreshHz
        );
        const bool refreshRateThresholdChanged =
            current.frame_generation_refresh_threshold !=
                applied.frame_generation_refresh_threshold;
        const bool generationModeChanged = current.adaptive != applied.adaptive;
        const bool fixedMultiplierChanged =
            current.multiplier != applied.multiplier &&
            (!current.adaptive || !applied.adaptive);
        const bool baseFpsCapChanged =
            effectiveBaseFpsCap(current, displayRefreshHz) !=
                effectiveBaseFpsCap(applied, displayRefreshHz);
        const bool gpuHeadroomChanged = current.gpu_headroom_percent != applied.gpu_headroom_percent &&
            (effectiveFrameGenerationEnabled(current, displayRefreshHz) ||
             effectiveFrameGenerationEnabled(applied, displayRefreshHz));
        const bool dynamicCadenceProbeIntervalChanged =
            current.dynamic_cadence_probe_interval_seconds !=
                applied.dynamic_cadence_probe_interval_seconds &&
            (current.dynamic_cadence_recovery ||
             applied.dynamic_cadence_recovery);
        const bool generationPolicyChanged =
            current.dynamic_cadence_recovery !=
                applied.dynamic_cadence_recovery ||
            current.adaptive_stable_cadence !=
                applied.adaptive_stable_cadence ||
            (current.adaptive && applied.adaptive && (
                // Equal numeric caps can still change scheduler and VRR
                // ownership between Fractional and Steady.
                (current.adaptive_auto_base_fps_cap !=
                    applied.adaptive_auto_base_fps_cap &&
                 (effectiveFrameGenerationEnabled(current, displayRefreshHz) ||
                  effectiveFrameGenerationEnabled(applied, displayRefreshHz))) ||
                current.target_fps != applied.target_fps ||
                current.adaptive_max_multiplier !=
                    applied.adaptive_max_multiplier
            ));
        const bool spatialScalingChanged =
            current.scaling_enabled != next.scaling_enabled ||
            (spatialScalingResourcesChanged &&
             !spatialScalingDormantUpdate &&
             !spatialScalingExtentNoOp);
        const bool liveChange = frameGenerationChanged ||
            refreshRateThresholdChanged ||
                generationPolicyChanged ||
                generationModeChanged || fixedMultiplierChanged ||
                baseFpsCapChanged || gpuHeadroomChanged || dynamicCadenceProbeIntervalChanged ||
                current.hdr_quality != applied.hdr_quality ||
                spatialScalingLiveRebuild || frameGenerationPrivateRebuild;

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
                .gpuHeadroomChanged = gpuHeadroomChanged,
                .dynamicCadenceProbeIntervalChanged =
                    dynamicCadenceProbeIntervalChanged,
                .spatialScalingChanged = spatialScalingChanged,
                .spatialScalingDormantUpdate =
                    spatialScalingDormantUpdate,
                .spatialScalingEffectiveExtentUnchanged =
                    spatialScalingExtentNoOp,
                .spatialScalingLiveRebuild =
                    spatialScalingLiveRebuild,
                .frameGenerationBackendChanged =
                    frameGenerationBackendChanged,
                .generatedFrameCapacityExceeded =
                    generatedCapacityExceeded,
                .frameGenerationPrivateRebuild =
                    frameGenerationPrivateRebuild,
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
