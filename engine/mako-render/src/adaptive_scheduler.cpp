/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "adaptive_scheduler.hpp"

#include "adaptive_policy_limits.hpp"
#include "generated_frame_delivery.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>

using namespace mako::layer;

#if defined(__OPTIMIZE__) && (defined(__GNUC__) || defined(__clang__))
#define MAKO_ADAPTIVE_STAGE_INLINE inline __attribute__((always_inline))
#else
#define MAKO_ADAPTIVE_STAGE_INLINE inline
#endif

namespace {
    static_assert(
        ls::GameConfLimits::maximumAdaptiveMaxMultiplier ==
            GeneratedFramePlan::capacity + 1,
        "Adaptive multiplier limit must fit the inline generated-frame plan"
    );

    constexpr size_t adaptiveHistoryWarmupFrames = 3;
    constexpr size_t adaptiveSdrCadenceRefreshFrames = 2;
    constexpr double adaptiveSdrCadenceResumeBaseFps = 12.0;
    constexpr size_t adaptiveSdrCadenceResumeFrameCount = 3;
    constexpr double adaptiveIntervalSmoothing = 0.25;
    constexpr double adaptiveCadenceDropRatio = 2.0;
    constexpr size_t adaptiveCadenceDropFrameCount = 3;
    constexpr double adaptiveTransientFastBurstCadenceRatio = 3.0;
    constexpr double adaptiveTransientFastBurstTargetRatio = 2.0;
    constexpr double adaptiveRampThroughputTolerance = 0.95;
    constexpr double adaptiveRampBaseCollapseRatio = 0.70;
    constexpr double adaptiveRampMarginalGain = 1.15;
    constexpr double adaptiveRampTargetSatisfiedRatio = 0.95;
    constexpr double adaptiveStrictLoadCollapseRatio = 0.80;
    constexpr double adaptiveStrictLoadSevereTargetDeficitRatio = 0.75;
    constexpr double adaptiveBridgeMinimumOutputRetention = 0.85;
    constexpr double adaptiveBridgeMinimumBaseRetention = 0.40;
    constexpr double adaptiveBridgeTargetDeficitRatio = 0.90;
    constexpr double adaptiveStableCadenceMaximumProbeOvershootRatio = 1.40;
    constexpr double adaptiveStableCadenceMaximumRetainedOvershootRatio = 1.50;
    constexpr double adaptiveStableCadenceMinimumQualificationTargetRatio =
        0.98;
    constexpr double adaptiveStableCadenceMinimumRetentionTargetRatio = 0.95;
    constexpr double adaptiveStableCadenceMinimumBaseRetention = 0.74;
    constexpr double adaptiveStableCadenceMinimumDemandRatio = 0.95;
    constexpr double adaptiveStableCadenceMinimumConvergenceDemandRatio =
        0.875;
    constexpr double adaptiveStableCadenceMaximumConvergenceOutputRatio =
        1.02;
    constexpr double adaptiveNativeCadenceMinimumRiseRatio = 1.25;
    constexpr size_t adaptiveNativeCadenceConfirmationFrames = 3;
    constexpr auto adaptiveStabilizationDuration = std::chrono::seconds(1);
    constexpr auto adaptiveRecoveryStabilizationDuration = std::chrono::seconds(3);
    constexpr auto adaptiveRampEvaluationDuration = std::chrono::seconds(1);
    constexpr auto adaptiveTargetDeficitDuration = std::chrono::seconds(1);
    constexpr auto adaptiveNearTargetNativeHoldDuration =
        std::chrono::seconds(1);
    // Opposite evidence decays twice as quickly as qualifying evidence grows.
    // A one-frame boundary excursion therefore cannot reset a real cadence
    // drop, while evenly oscillating measurements cannot accumulate enough
    // one-sided evidence to chatter the policy.
    constexpr int adaptiveNearTargetOppositeEvidenceDecay = 2;
    constexpr auto adaptiveNearTargetMaximumEvidenceSample =
        std::chrono::milliseconds(100);
    // Fractional Adaptive promises to approach the requested output target.
    // Native preference is therefore only eligible when the real cadence is
    // already within Adaptive's established 95% retention envelope. The
    // interval-quality comparison still avoids sparse generated work at a
    // genuinely near-target cadence, without stranding a 95-110 FPS source
    // below a 120 FPS target indefinitely.
    constexpr double adaptiveNearTargetNativeMinimumOutputRatio = 0.95;
    // Inside the final five-percent output envelope, measured cadence can
    // naturally wander around a close quality decision. Admit a half-percent
    // target-interval RMS tolerance so a nominal near-target cadence cannot
    // chatter back into an equally irregular sparse Fractional sequence.
    // Expressing the margin as target time keeps it proportional at 60, 90,
    // 120, 144 Hz, and other configured targets.
    constexpr double adaptiveNearTargetEntryTargetIntervalTolerance = 0.005;
    constexpr auto adaptiveRampStepDelay = std::chrono::milliseconds(250);
    constexpr auto adaptiveRampFirstRetryDelay = std::chrono::seconds(5);
    constexpr auto adaptiveRampSecondRetryDelay = std::chrono::seconds(15);
    constexpr auto adaptiveRampThirdRetryDelay = std::chrono::seconds(30);
    constexpr auto adaptiveRampMaximumRetryDelay = std::chrono::seconds(60);
    constexpr double adaptiveRampEarlyRetryBaseImprovement = 1.15;
    constexpr auto adaptiveRecoveryHigherProbeDelay = std::chrono::seconds(5);
    constexpr auto adaptiveStableCadenceEvaluationDuration = std::chrono::seconds(1);
    constexpr auto adaptiveStableCadenceExitGraceDuration = std::chrono::milliseconds(500);
    constexpr auto adaptiveStableCadenceRetryDelay = std::chrono::seconds(15);
    constexpr auto adaptiveStableCadenceConvergenceRetryDelay =
        std::chrono::seconds(60);
    constexpr auto adaptiveStableCadenceStrictSettlingDuration = std::chrono::seconds(2);
    constexpr auto adaptiveStableCadenceCandidateDuration = std::chrono::seconds(2);
    constexpr double adaptiveStableCadenceMaximumCandidateSpreadRatio = 1.15;
    constexpr auto adaptiveEfficiencyProbeHoldDuration = std::chrono::seconds(5);
    constexpr auto adaptiveEfficiencyProbeEvaluationDuration = std::chrono::milliseconds(250);
    constexpr auto adaptiveEfficiencyProbeSettlingGraceDuration =
        std::chrono::milliseconds(250);
    constexpr auto adaptiveEfficiencyProbeRetryDelay = std::chrono::seconds(60);
    constexpr double adaptiveEfficiencyProbeMinimumTargetRatio = 0.98;
    constexpr double adaptiveEfficiencyProbeSettlingMinimumTargetRatio = 0.90;
    constexpr double adaptiveEfficiencyProbeSettlingMinimumBaseRiseRatio = 1.20;
    constexpr double adaptiveRescueBaseCollapseRatio = 0.78;
    constexpr double adaptiveRescueOutputCollapseRatio = 0.80;
    constexpr double adaptiveRescueRecoveredBaseRatio = 0.90;
    constexpr auto adaptiveRescueMeasurementDuration = std::chrono::seconds(1);
    constexpr auto adaptiveRescueCooldown = std::chrono::seconds(15);
    constexpr auto adaptiveStrictLoadCollapseDuration = std::chrono::seconds(1);
    constexpr auto adaptiveStrictLoadHealthyDuration = std::chrono::seconds(2);
    constexpr double adaptiveDiscontinuityRecoveredBaseRatio = 0.90;
    constexpr auto adaptiveDiscontinuityStableDuration = std::chrono::seconds(1);
    constexpr auto adaptiveDiscontinuityMaximumDuration = std::chrono::seconds(5);
    constexpr auto adaptiveTwoXGameplayHitchMaximumDuration =
        std::chrono::milliseconds(250);
    constexpr auto adaptiveFailedProbeCooldown = std::chrono::seconds(15);
    constexpr auto adaptiveInterruptedProbeCooldown = std::chrono::seconds(2);
    constexpr auto adaptiveStableRearmDuration = std::chrono::seconds(2);
    constexpr auto adaptivePlanDiagnosticInterval = std::chrono::seconds(1);
    constexpr auto adaptiveFastBurstDiagnosticInterval = std::chrono::seconds(1);
    constexpr double adaptiveTargetClockPlacementDeadbandRatio = 0.01;
    constexpr double adaptiveTargetClockMinimumRepaymentHeadroomOutputs = 0.25;
    constexpr std::array adaptiveOutputCountReciprocals{
        0.0, 1.0, 0.5, 1.0 / 3.0, 0.25,
    };
    static_assert(
        adaptiveOutputCountReciprocals.size() ==
            GeneratedFramePlan::capacity + 2
    );

    AdaptiveSchedulerDiagnostics nullDiagnostics;

    struct NearTargetCadenceQualityPrediction {
        bool valid{false};
        double nativeIntervalSquaredErrorMilliseconds{0.0};
        double fractionalIntervalSquaredErrorMilliseconds{0.0};

        [[nodiscard]] double nativeIntervalRmsMilliseconds() const {
            return std::sqrt(std::max(
                0.0, this->nativeIntervalSquaredErrorMilliseconds
            ));
        }

        [[nodiscard]] double fractionalIntervalRmsMilliseconds() const {
            return std::sqrt(std::max(
                0.0, this->fractionalIntervalSquaredErrorMilliseconds
            ));
        }
    };

    [[nodiscard]] NearTargetCadenceQualityPrediction
    predictNearTargetCadenceQuality(const double baseFps,
            const uint32_t targetFps) {
        const double target = static_cast<double>(targetFps);
        if (baseFps <= target * 0.5 || baseFps >= target)
            return {};

        const double desiredOutputsPerRealFrame = target / baseFps;
        const double generatedSourceShare = desiredOutputsPerRealFrame - 1.0;
        const double sourceIntervalMilliseconds = 1000.0 / baseFps;
        const double targetIntervalMilliseconds = 1000.0 / target;
        const double nativeError =
            sourceIntervalMilliseconds - targetIntervalMilliseconds;
        const double dividedError =
            sourceIntervalMilliseconds * 0.5 - targetIntervalMilliseconds;
        const double fractionalSquaredError = (
            (1.0 - generatedSourceShare) * nativeError * nativeError +
            2.0 * generatedSourceShare * dividedError * dividedError
        ) / desiredOutputsPerRealFrame;
        return {
            .valid = true,
            .nativeIntervalSquaredErrorMilliseconds =
                nativeError * nativeError,
            .fractionalIntervalSquaredErrorMilliseconds =
                std::max(0.0, fractionalSquaredError),
        };
    }

    std::chrono::steady_clock::duration adaptiveRampRetryDelayForFailures(
            const size_t failures) {
        if (failures <= 1)
            return adaptiveRampFirstRetryDelay;
        if (failures == 2)
            return adaptiveRampSecondRetryDelay;
        if (failures == 3)
            return adaptiveRampThirdRetryDelay;
        return adaptiveRampMaximumRetryDelay;
    }
}

AdaptiveScheduler::AdaptiveScheduler(AdaptiveSchedulerConfig config,
        AdaptiveSchedulerDiagnostics* diagnostics) :
        config(config),
        diagnostics(diagnostics ? diagnostics : &nullDiagnostics),
        diagnosticsActive(this->diagnostics->enabled()) {
    this->state.historyWarmup.remaining = adaptiveHistoryWarmupFrames;
    if (this->config.targetFps < ls::GameConfLimits::minimumTargetFps ||
            this->config.targetFps > ls::GameConfLimits::maximumTargetFps) {
        throw std::invalid_argument(
            "Adaptive target FPS must be between " + std::to_string(
                ls::GameConfLimits::minimumTargetFps
            ) + " and " + std::to_string(
                ls::GameConfLimits::maximumTargetFps
            )
        );
    }
    if (this->config.dynamicCadenceProbeInterval <
                ls::dynamicCadenceProbeIntervalDuration(
                    ls::GameConfLimits::minimumDynamicCadenceProbeIntervalSeconds
                ) ||
            this->config.dynamicCadenceProbeInterval >
                ls::dynamicCadenceProbeIntervalDuration(
                    ls::GameConfLimits::maximumDynamicCadenceProbeIntervalSeconds
                )) {
        throw std::invalid_argument(
            "Dynamic cadence probe interval is outside the accepted range"
        );
    }
    if (this->config.maximumMultiplier <
            ls::GameConfLimits::minimumAdaptiveMaxMultiplier ||
            this->config.maximumMultiplier >
                ls::GameConfLimits::maximumAdaptiveMaxMultiplier) {
        throw std::invalid_argument(
            "Adaptive maximum multiplier must be between " + std::to_string(
                ls::GameConfLimits::minimumAdaptiveMaxMultiplier
            ) + " and " + std::to_string(
                ls::GameConfLimits::maximumAdaptiveMaxMultiplier
            )
        );
    }
    if (this->config.generatedFrameCapacity > GeneratedFramePlan::capacity) {
        throw std::invalid_argument(
            "Adaptive generated-frame capacity exceeds plan capacity"
        );
    }
}

void AdaptiveScheduler::updateDynamicCadenceProbeInterval(
        const TimePoint now, const std::chrono::milliseconds interval) {
    if (interval < ls::dynamicCadenceProbeIntervalDuration(
                ls::GameConfLimits::minimumDynamicCadenceProbeIntervalSeconds) ||
            interval > ls::dynamicCadenceProbeIntervalDuration(
                ls::GameConfLimits::maximumDynamicCadenceProbeIntervalSeconds)) {
        throw std::invalid_argument(
            "Dynamic cadence probe interval is outside the accepted range"
        );
    }

    this->config.dynamicCadenceProbeInterval = interval;
    auto& probe = this->state.nativeCadenceProbe;
    if (!probe.active && probe.nextAt)
        probe.nextAt = now + interval;
}

void AdaptiveScheduler::beginHistoryWarmup(const size_t frames,
        const bool recovery) {
    this->state.historyWarmup.remaining = frames;
    this->state.historyWarmup.recovery = recovery && frames > 0;
}

void AdaptiveScheduler::ensureHistoryWarmup(const size_t frames,
        const bool recovery) {
    if (this->state.historyWarmup.remaining > 0)
        return;
    this->beginHistoryWarmup(frames, recovery);
}

void AdaptiveScheduler::cancelHistoryWarmup() {
    this->state.historyWarmup.remaining = 0;
    this->state.historyWarmup.recovery = false;
}

void AdaptiveScheduler::consumeHistoryWarmupFrame(const TimePoint now) {
    if (this->state.historyWarmup.remaining == 0)
        return;

    this->state.historyWarmup.remaining--;
    if (this->state.historyWarmup.remaining == 0)
        this->state.historyWarmup.recovery = false;
    this->resetTiming(now);
}

void AdaptiveScheduler::reportGeneratedFrameDelivery(
        const GeneratedFrameDelivery delivery) {
    if (delivery.requested == 0)
        return;
    if (this->state.ramp.evaluationAt) {
        this->state.ramp.delivery.record(delivery);
    }
    if (this->state.stableCadence.evaluationAt) {
        this->state.stableCadence.delivery.record(delivery);
    }
    if (this->state.efficiencyProbe.evaluationAt) {
        this->state.efficiencyProbe.delivery.record(delivery);
    }
}

AdaptiveSchedulerSnapshot AdaptiveScheduler::snapshot() const {
    AdaptiveSchedulerPhase phase = AdaptiveSchedulerPhase::Active;
    if (this->state.historyWarmup.remaining > 0) {
        phase = AdaptiveSchedulerPhase::HistoryWarmup;
    } else if (!this->state.cadence.lastRealFrame) {
        phase = AdaptiveSchedulerPhase::Uninitialized;
    } else if (this->state.discontinuityRecovery.deadline) {
        phase = AdaptiveSchedulerPhase::DiscontinuityRecovery;
    } else if (this->state.rescue.until) {
        phase = AdaptiveSchedulerPhase::RescueMeasurement;
    } else if (this->state.stabilization.until) {
        phase = AdaptiveSchedulerPhase::Stabilizing;
    } else if (this->state.rearm.required) {
        phase = AdaptiveSchedulerPhase::RearmCooldown;
    } else if (this->state.ramp.evaluationAt) {
        phase = AdaptiveSchedulerPhase::RampEvaluation;
    } else if (this->state.nativeCadenceProbe.active) {
        phase = AdaptiveSchedulerPhase::NativeCadenceProbe;
    } else if (this->state.stableCadence.limit) {
        phase = AdaptiveSchedulerPhase::StableCadence;
    } else if (this->state.nearTargetNativePreference.active) {
        phase = AdaptiveSchedulerPhase::NearTargetNative;
    }

    return {
        .phase = phase,
        .generationLimit = this->state.outputPlanner.generationLimit,
        .validatedGenerationLimit = this->validatedGenerationLimit(),
        .stableCadenceLimit = this->state.stableCadence.limit,
        .stableCadenceEvaluationActive =
            this->state.stableCadence.evaluationAt.has_value(),
        .historyWarmupRemaining = this->state.historyWarmup.remaining,
        .smoothedBaseFps = this->state.cadence.smoothedIntervalSeconds > 0.0
            ? 1.0 / this->state.cadence.smoothedIntervalSeconds
            : 0.0,
        .rampEvaluationActive = this->state.ramp.evaluationAt.has_value(),
        .rearmRequired = this->state.rearm.required,
        .discontinuityRecoveryActive =
            this->state.discontinuityRecovery.deadline.has_value(),
        .nativeCadenceProbeActive = this->state.nativeCadenceProbe.active,
        .nearTargetNativePreference =
            this->state.nearTargetNativePreference.active,
        .targetOutputClockActive =
            this->state.outputPlanner.targetClockActive,
        .targetOutputBudgetCreditOutputs =
            this->state.outputPlanner.budgetCreditOutputs,
        .targetOutputPhaseErrorOutputs =
            this->state.outputPlanner.targetPhaseErrorOutputs,
        .targetOutputDeferredBudgetOutput =
            this->state.outputPlanner.deferredBudgetOutput,
    };
}

size_t AdaptiveScheduler::configuredGenerationLimit() const {
    if (this->config.maximumMultiplier <
            ls::GameConfLimits::minimumAdaptiveMaxMultiplier) {
        return 0;
    }
    return std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
}

MAKO_ADAPTIVE_STAGE_INLINE AdaptiveScheduler::CadenceObservation
AdaptiveScheduler::observeCadence(
        const std::chrono::steady_clock::time_point now,
        const bool generatedImageAcquireBackoff) {
    if (generatedImageAcquireBackoff) {
        // Keep probing for one generated-image slot without advancing ramp,
        // stable-cadence, or load-shed policy while the generated workload is
        // deliberately bypassed. Evaluating a multiplier during this phase
        // would measure only the real-frame path and could falsely accept it.
        // Pressure belongs to the compositor admission path, not the game's
        // cadence. Keep the real-frame clock current so clearing pressure does
        // not manufacture a cadence stall from the bypass interval.
        if (this->state.cadence.lastRealFrame) {
            const auto bypassDuration = now - *this->state.cadence.lastRealFrame;
            if (this->state.efficiencyProbe.eligibleSince)
                *this->state.efficiencyProbe.eligibleSince += bypassDuration;
            if (this->state.efficiencyProbe.evaluationAt)
                *this->state.efficiencyProbe.evaluationAt += bypassDuration;
        }
        this->state.cadence.lastRealFrame = now;
        this->state.outputPlanner.resetTargetClock();
        return {
            .terminalPlan = AdaptiveFramePlan::evenlySpaced(1),
        };
    }

    if (!this->state.cadence.lastRealFrame) {
        this->state.cadence.lastRealFrame = now;
        return {};
    }

    const auto rawInterval = now - *this->state.cadence.lastRealFrame;
    const double rawIntervalSeconds = std::chrono::duration<double>(
        rawInterval
    ).count();
    this->state.cadence.lastRealFrame = now;

    const auto finishFastCadenceBurst = [&] {
        if (!this->state.fastBurst.startedAt)
            return;

        this->diagnostics->fastCadenceBurstComplete(
            this->state.fastBurst.frames,
            now - *this->state.fastBurst.startedAt
        );
        this->state.fastBurst.startedAt.reset();
        this->state.fastBurst.lastDiagnosticAt.reset();
        this->state.fastBurst.frames = 0;
        this->state.fastBurst.framesSinceDiagnostic = 0;
    };

    // Loading screens, suspension and base rates below 10 FPS do not provide
    // useful motion history. Present real frames until cadence has been stable
    // for a bounded interval instead of immediately reapplying model load.
    if (rawIntervalSeconds <= 0.0) {
        finishFastCadenceBurst();
        if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
            if (this->state.cadence.sdrStallBypass) {
                this->state.cadence.sdrResumeFrames = 0;
                this->state.outputPlanner.resetTargetClock();
                return {};
            }
            this->beginCadenceRefresh(now, "cadence-stall");
            return {};
        }
        this->beginStabilization(now, "cadence-stall");
        return {};
    }

    const double baselineBaseFps = this->state.cadence.smoothedIntervalSeconds > 0.0
        ? 1.0 / this->state.cadence.smoothedIntervalSeconds
        : 0.0;
    const double instantaneousBaseFps = 1.0 / rawIntervalSeconds;
    if (rawIntervalSeconds <= 1.0 / adaptiveMinimumBaseFps)
        this->state.cadence.sdrIsolatedHitchBridged = false;
    const double fastBurstThresholdFps = std::max(
        baselineBaseFps * adaptiveTransientFastBurstCadenceRatio,
        static_cast<double>(this->config.targetFps) *
            adaptiveTransientFastBurstTargetRatio
    );
    if (instantaneousBaseFps > fastBurstThresholdFps) {
        // Do not manufacture generated work for a transient that is already
        // faster than the requested output. Preserve the proven gameplay
        // baseline and pause evaluation windows which require real generated
        // workload instead of letting wall-clock time validate an untested
        // multiplier while DX12 is submitting the burst.
        if (!this->state.fastBurst.startedAt)
            this->state.fastBurst.startedAt = now;
        this->state.fastBurst.frames++;
        this->state.fastBurst.framesSinceDiagnostic++;

        const auto pauseEvaluation = [&rawInterval](auto& deadline) {
            if (deadline)
                *deadline += rawInterval;
        };
        pauseEvaluation(this->state.stabilization.until);
        pauseEvaluation(this->state.ramp.evaluationAt);
        pauseEvaluation(this->state.stableCadence.evaluationAt);
        pauseEvaluation(this->state.efficiencyProbe.eligibleSince);
        pauseEvaluation(this->state.efficiencyProbe.evaluationAt);
        pauseEvaluation(this->state.rescue.until);

        this->state.outputPlanner.resetTargetClock();
        this->state.cadence.dropFrames = 0;
        this->state.ramp.targetDeficitSince.reset();
        this->state.rearm.stableSince.reset();
        this->state.rearm.improvementSince.reset();
        this->state.strictLoad.collapseSince.reset();
        this->state.strictLoad.healthySince.reset();
        this->state.strictLoad.recoverySince.reset();
        this->state.stableCadence.outsideRangeSince.reset();
        this->state.discontinuityRecovery.stableSince.reset();
        if (this->state.cadence.sdrStallBypass)
            this->state.cadence.sdrResumeFrames = 0;

        if (!this->state.fastBurst.lastDiagnosticAt ||
                now - *this->state.fastBurst.lastDiagnosticAt >=
                    adaptiveFastBurstDiagnosticInterval) {
            this->diagnostics->fastCadenceBurst(
                baselineBaseFps,
                instantaneousBaseFps,
                fastBurstThresholdFps,
                this->state.fastBurst.framesSinceDiagnostic,
                this->state.fastBurst.frames,
                now - *this->state.fastBurst.startedAt
            );
            this->state.fastBurst.lastDiagnosticAt = now;
            this->state.fastBurst.framesSinceDiagnostic = 0;
        }
        return {};
    }
    finishFastCadenceBurst();

    if (rawIntervalSeconds > 1.0 / adaptiveMinimumBaseFps) {
        if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
            const size_t validatedGenerationLimit =
                this->validatedGenerationLimit();
            const bool acceptedStableTwoX =
                this->state.stableCadence.limit == 1 &&
                !this->state.stableCadence.evaluationAt;
            const bool isolatedGameplayHitch =
                acceptedStableTwoX &&
                validatedGenerationLimit == 1 &&
                !this->state.cadence.sdrStallBypass &&
                !this->state.cadence.sdrIsolatedHitchBridged &&
                rawInterval <= adaptiveTwoXGameplayHitchMaximumDuration;
            if (isolatedGameplayHitch) {
                // Fixed 2x already demonstrates that one midpoint remains
                // useful across an isolated source hitch.
                // Suppressing interpolation here amplified one long source
                // interval into four native-only frames. Preserve the proven
                // cadence once; a consecutive stall takes the refresh path.
                this->state.cadence.sdrIsolatedHitchBridged = true;
                this->state.outputPlanner.resetTargetClock();
                this->state.cadence.dropFrames = 0;
                this->diagnostics->sdrGameplayHitchBridge(
                    validatedGenerationLimit,
                    baselineBaseFps,
                    rawInterval
                );
                return {
                    .terminalPlan = AdaptiveFramePlan::evenlySpaced(1),
                };
            }
            if (this->state.cadence.sdrStallBypass) {
                // The initial refresh already left the backend receiving a
                // history-only update for every real frame. Repeating that
                // two-frame warm-up while a game is temporarily below the
                // useful cadence threshold turns one stall into a visible
                // recovery loop. Keep the real frames flowing until timing is
                // viable again, then re-establish the normal policy below.
                this->state.cadence.smoothedIntervalSeconds = rawIntervalSeconds;
                this->state.cadence.dropFrames = 0;
                this->state.outputPlanner.resetTargetClock();
                this->state.cadence.sdrResumeFrames = 0;
                return {};
            }
            this->beginCadenceRefresh(now, "cadence-stall");
            return {};
        }

        const size_t configuredGenerationLimit = std::min(
            this->config.generatedFrameCapacity,
            this->config.maximumMultiplier - 1
        );
        const size_t validatedGenerationLimit =
            this->validatedGenerationLimit();
        const bool shortTwoXGameplayHitch =
            configuredGenerationLimit == 1 &&
            validatedGenerationLimit == 1 &&
            rawInterval <= adaptiveTwoXGameplayHitchMaximumDuration;
        if (shortTwoXGameplayHitch) {
            // Keep the proven 2x policy, but feed the model fresh real-frame
            // history before generating again. If Gamescope is actually
            // withholding generated images, the existing bounded acquire and
            // swapchain recovery path will still take over after this warmup.
            this->state.historyWarmup.remaining =
                adaptiveHistoryWarmupFrames;
            this->state.historyWarmup.recovery = true;
            this->diagnostics->twoXGameplayHitchRecovery(
                validatedGenerationLimit,
                baselineBaseFps,
                rawInterval
            );
            this->resetTiming(now);
            return {};
        }

        this->beginStabilization(now, "cadence-stall");
        return {};
    }

    if (this->state.cadence.sdrStallBypass) {
        // A cadence hovering around the 10-FPS stall boundary is not a real
        // recovery. Require a small run above a separate resume threshold so
        // one short interval cannot resume generation and immediately start
        // another history refresh on the next long interval.
        this->state.cadence.smoothedIntervalSeconds = rawIntervalSeconds;
        this->state.cadence.dropFrames = 0;
        this->state.outputPlanner.resetTargetClock();
        if (instantaneousBaseFps < adaptiveSdrCadenceResumeBaseFps) {
            this->state.cadence.sdrResumeFrames = 0;
            return {};
        }

        this->state.cadence.sdrResumeFrames++;
        if (this->state.cadence.sdrResumeFrames <
                adaptiveSdrCadenceResumeFrameCount) {
            return {};
        }

        // Discard the pre-stall estimate. The confirmed recovered cadence is
        // now the baseline, so a recovered 60-FPS game cannot be mistaken for
        // a fresh cadence drop.
        this->state.cadence.sdrStallBypass = false;
        this->state.cadence.sdrResumeFrames = 0;
    }

    // A sustained interval jump can be a menu/focus transition, but it can
    // also be a legitimate heavier gameplay scene. Three samples avoid
    // treating an isolated hitch as a cadence change. Unlike a hard stall,
    // this path performs only the ordinary one-second stabilization and then
    // rebases Adaptive at the new measured rate.
    const bool cadenceDropCandidate =
        this->state.cadence.smoothedIntervalSeconds > 0.0 &&
            rawIntervalSeconds >=
                this->state.cadence.smoothedIntervalSeconds * adaptiveCadenceDropRatio;
    if (cadenceDropCandidate) {
        this->state.rearm.stableSince.reset();
        this->state.rearm.improvementSince.reset();
        this->state.cadence.dropFrames++;
    } else {
        this->state.cadence.dropFrames = 0;
    }
    if (this->state.cadence.dropFrames >= adaptiveCadenceDropFrameCount) {
        if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
            this->beginCadenceRefresh(now, "cadence-drop");
            return {};
        }
        this->beginStabilization(now, "cadence-drop");
        return {};
    }

    if (this->state.cadence.smoothedIntervalSeconds == 0.0) {
        this->state.cadence.smoothedIntervalSeconds = rawIntervalSeconds;
    } else if (!cadenceDropCandidate) {
        // Keep the pre-disruption baseline while confirming a sustained drop.
        // Otherwise smoothing the first slow samples raises the comparison
        // threshold and can hide the third confirming frame.
        this->state.cadence.smoothedIntervalSeconds =
            (1.0 - adaptiveIntervalSmoothing) * this->state.cadence.smoothedIntervalSeconds +
            adaptiveIntervalSmoothing * rawIntervalSeconds;
    }

    return {
        .planningReady = true,
        .baseFps = 1.0 / this->state.cadence.smoothedIntervalSeconds,
        .instantaneousBaseFps = instantaneousBaseFps,
        .rawIntervalSeconds = rawIntervalSeconds,
    };
}

MAKO_ADAPTIVE_STAGE_INLINE AdaptiveScheduler::PlanningStageResult
AdaptiveScheduler::advanceDiscontinuityRecovery(
        const TimePoint now, const double baseFps) {
    if (this->state.discontinuityRecovery.deadline) {
        const size_t recoveryLimit = this->state.discontinuityRecovery.generationLimit;
        const size_t recoveryFallbackLimit =
            this->state.discontinuityRecovery.fallbackGenerationLimit;
        const double recoveryBaselineBaseFps =
            this->state.discontinuityRecovery.baselineBaseFps;
        const bool initialStabilizationComplete =
            !this->state.stabilization.until ||
            now >= *this->state.stabilization.until;
        const bool baseRecovered = recoveryBaselineBaseFps > 0.0 &&
            baseFps >= recoveryBaselineBaseFps *
                adaptiveDiscontinuityRecoveredBaseRatio;

        if (baseRecovered) {
            if (!this->state.discontinuityRecovery.stableSince)
                this->state.discontinuityRecovery.stableSince = now;
        } else {
            this->state.discontinuityRecovery.stableSince.reset();
        }

        const bool recoveredCadenceStable = initialStabilizationComplete &&
            this->state.discontinuityRecovery.stableSince &&
            now - *this->state.discontinuityRecovery.stableSince >=
                adaptiveDiscontinuityStableDuration;
        const bool recoveryExpired =
            now >= *this->state.discontinuityRecovery.deadline;

        if (recoveredCadenceStable || recoveryExpired) {
            const std::string_view decision = recoveredCadenceStable
                ? "restore-validated-level"
                : "timeout-ramp-from-zero";
            this->state.discontinuityRecovery.reset();
            this->state.stabilization.until.reset();
            this->state.outputPlanner.resetTargetClock();
            if (recoveredCadenceStable) {
                this->restoreGenerationLimit(
                    now,
                    recoveryLimit,
                    "cadence-discontinuity",
                    recoveryFallbackLimit,
                    baseFps
                );
                if (this->config.stableCadence) {
                    this->state.stableCadence.retryAt =
                        now + adaptiveStableCadenceStrictSettlingDuration;
                }
            } else {
                this->state.outputPlanner.generationLimit = 0;
                this->state.ramp.evaluationAt.reset();
                this->state.ramp.targetDeficitSince.reset();
                this->state.ramp.bridgeActive = false;
                this->state.ramp.bridgeBaselineLimit = 0;
                this->state.ramp.bridgeBaselineBaseFps = 0.0;
                this->state.rearm.required = false;
                this->state.rearm.notBefore.reset();
                this->state.rearm.stableSince.reset();
                this->state.rearm.improvementSince.reset();
                this->state.rearm.reason.clear();
                this->state.rearm.baselineBaseFps = 0.0;
                this->state.rearm.fallbackLimit = 0;
                this->state.rearm.consecutiveProbeFailures = 0;
                this->state.ramp.lastFailedLimit = 0;
                this->state.ramp.consecutiveFailures = 0;
                this->state.ramp.failedBaselineBaseFps = 0.0;
                this->state.ramp.nextAt = now + adaptiveRampStepDelay;
            }
            this->diagnostics->discontinuityRecoveryComplete(
                recoveryLimit,
                recoveryBaselineBaseFps,
                baseFps,
                decision
            );
            // Keep this transition frame real-only. The restored or freshly
            // ramped policy starts on the following real frame.
            return {};
        }

        this->state.outputPlanner.resetTargetClock();
        if (this->diagnosticsActive &&
                (!this->state.diagnosticThrottle.lastPlanAt ||
                 now - *this->state.diagnosticThrottle.lastPlanAt >=
                    adaptivePlanDiagnosticInterval)) {
            this->state.diagnosticThrottle.lastPlanAt = now;
            AdaptivePlanDiagnostic plan;
            plan.baseFps = baseFps;
            plan.targetFps = this->config.targetFps;
            plan.phase = "discontinuity-recovery";
            plan.recoveryGenerationLimit = recoveryLimit;
            this->diagnostics->plan(plan);
        }
        return {};
    }

    return {.planningReady = true};
}

MAKO_ADAPTIVE_STAGE_INLINE AdaptiveScheduler::PlanningStageResult
AdaptiveScheduler::advanceRescueMeasurement(
        const TimePoint now, const double baseFps) {
    if (this->state.rescue.until) {
        if (now < *this->state.rescue.until) {
            this->state.outputPlanner.resetTargetClock();
            if (this->diagnosticsActive &&
                    (!this->state.diagnosticThrottle.lastPlanAt ||
                     now - *this->state.diagnosticThrottle.lastPlanAt >=
                        adaptivePlanDiagnosticInterval)) {
                this->state.diagnosticThrottle.lastPlanAt = now;
                AdaptivePlanDiagnostic plan;
                plan.baseFps = baseFps;
                plan.targetFps = this->config.targetFps;
                plan.phase = this->state.rescue.fromStrictLoad
                    ? "strict-load-rescue"
                    : "rescue";
                this->diagnostics->plan(plan);
            }
            return {};
        }

        const size_t configuredLimit = std::min(
            this->config.generatedFrameCapacity,
            this->config.maximumMultiplier - 1
        );
        const size_t previousLimit = std::min(
            this->state.rescue.previousLimit, configuredLimit
        );
        const double rescueBaselineBaseFps =
            this->state.rescue.baselineBaseFps;
        const size_t requiredOutputs = std::max<size_t>(
            1,
            static_cast<size_t>(std::ceil(
                static_cast<double>(this->config.targetFps) / baseFps - 1e-9
            ))
        );
        const size_t requiredLimit = requiredOutputs - 1;
        const size_t requestedLimit = std::min(requiredLimit, configuredLimit);
        const bool baseRecovered = rescueBaselineBaseFps > 0.0 &&
            baseFps >= rescueBaselineBaseFps * adaptiveRescueRecoveredBaseRatio;
        const bool strictLoadRescue = this->state.rescue.fromStrictLoad;
        const size_t strictLoadLimit = std::min(
            this->state.rescue.strictLoadLimit, configuredLimit
        );

        std::string_view decision = "resume-strict";
        this->state.outputPlanner.generationLimit = previousLimit;
        this->state.ramp.evaluationAt.reset();
        this->state.ramp.bridgeActive = false;
        this->state.ramp.bridgeBaselineLimit = 0;
        this->state.ramp.bridgeBaselineBaseFps = 0.0;
        this->state.ramp.nextAt.reset();
        if (strictLoadRescue) {
            // A real-only measurement distinguishes inference pressure from a
            // genuinely heavier game scene. Restore the lower proven level
            // only when cadence recovers without generated-frame work;
            // otherwise retain the higher level that the scene still needs.
            this->state.ramp.nextAt = this->state.rescue.cooldownUntil;
            if (baseRecovered) {
                decision = "strict-load-restored";
            } else {
                this->state.outputPlanner.generationLimit = std::max(
                    previousLimit, strictLoadLimit
                );
                decision = "strict-load-retained";
            }
        } else if (baseRecovered) {
            decision = "recovered-strict";
        } else if (requestedLimit > previousLimit) {
            // updateGenerationLimit() below will probe only the next
            // allowed level and apply its existing throughput checks.
            this->state.ramp.nextAt = now;
            decision = "probe-higher-limit";
        } else if (requiredLimit > configuredLimit) {
            decision = "ceiling-limited";
        }

        this->state.rescue.until.reset();
        this->state.rescue.previousLimit = 0;
        this->state.rescue.baselineBaseFps = 0.0;
        this->state.rescue.fromStrictLoad = false;
        this->state.rescue.strictLoadLimit = 0;
        this->state.ramp.targetDeficitSince.reset();
        this->state.outputPlanner.resetTargetClock();
        if (this->state.rescue.cooldownUntil)
            this->state.stableCadence.retryAt = this->state.rescue.cooldownUntil;
        this->diagnostics->rescueComplete(
            previousLimit,
            this->state.outputPlanner.generationLimit,
            requestedLimit,
            configuredLimit,
            rescueBaselineBaseFps,
            baseFps,
            decision
        );
    }

    return {.planningReady = true};
}

MAKO_ADAPTIVE_STAGE_INLINE AdaptiveScheduler::PlanningStageResult
AdaptiveScheduler::advanceStableCadence(
        const TimePoint now, const double baseFps,
        const double desiredOutputsPerRealFrame,
        const size_t maximumGeneratedFrameCount) {
    // A downward efficiency probe deliberately runs one cheaper constant
    // cadence while retaining the previously qualified policy for immediate
    // rollback. Do not let normal retention interpret that temporary workload
    // change as a reason to disable or requalify Smooth Cadence.
    if (this->state.efficiencyProbe.evaluationAt)
        return {.planningReady = true};

    struct StableCadenceCandidate {
        size_t generatedFrames{0};
        bool convergenceProbe{false};
    };
    const auto stableCadenceCandidate =
            [&]() -> std::optional<StableCadenceCandidate> {
        if (!this->config.stableCadence)
            return std::nullopt;
        if (desiredOutputsPerRealFrame <= 1.0)
            return std::nullopt;

        const double minimumUsefulOutputFps =
            static_cast<double>(this->config.targetFps) *
                adaptiveStableCadenceMinimumQualificationTargetRatio;
        const size_t candidateOutputs = static_cast<size_t>(std::ceil(
            minimumUsefulOutputFps / baseFps - 1e-9
        ));
        if (candidateOutputs <= 1)
            return std::nullopt;

        const size_t candidateGenerated = candidateOutputs - 1;
        if (candidateGenerated > maximumGeneratedFrameCount)
            return std::nullopt;

        const double cadenceDemandRatio =
            desiredOutputsPerRealFrame /
                static_cast<double>(candidateOutputs);
        const bool normalCandidate = cadenceDemandRatio >=
            adaptiveStableCadenceMinimumDemandRatio;
        const bool twoXConvergenceCandidate = !normalCandidate &&
            candidateGenerated == 1 &&
            cadenceDemandRatio >=
                adaptiveStableCadenceMinimumConvergenceDemandRatio &&
            this->config.recoveryPolicy ==
                AdaptiveRecoveryPolicy::OrderedSdr &&
            adaptiveTargetMatchesRefresh(
                this->config.targetFps, this->config.displayRefreshFps
            );
        if (!normalCandidate && !twoXConvergenceCandidate)
            return std::nullopt;

        const double projectedOutputFps = baseFps *
            static_cast<double>(candidateOutputs);
        if (projectedOutputFps >
                static_cast<double>(this->config.targetFps) *
                    adaptiveStableCadenceMaximumProbeOvershootRatio)
            return std::nullopt;

        return StableCadenceCandidate{
            .generatedFrames = candidateGenerated,
            .convergenceProbe = twoXConvergenceCandidate,
        };
    }();

    if (this->state.stableCadence.limit) {
        const size_t generatedLimit = *this->state.stableCadence.limit;
        const double targetFps = static_cast<double>(this->config.targetFps);
        const double projectedOutputFps = baseFps *
            static_cast<double>(generatedLimit + 1);
        const double cadenceDemandRatio =
            desiredOutputsPerRealFrame /
            static_cast<double>(generatedLimit + 1);
        const bool capacityAvailable =
            generatedLimit <= maximumGeneratedFrameCount;
        const bool convergenceEvaluation =
            this->state.stableCadence.convergenceProbe &&
            this->state.stableCadence.evaluationAt.has_value();
        const bool cadenceStillUseful =
            desiredOutputsPerRealFrame > 1.0 &&
            cadenceDemandRatio >= adaptiveStableCadenceMinimumDemandRatio &&
            projectedOutputFps >=
                targetFps * adaptiveStableCadenceMinimumRetentionTargetRatio &&
            projectedOutputFps <=
                targetFps * adaptiveStableCadenceMaximumRetainedOvershootRatio;

        if (!capacityAvailable) {
            this->diagnostics->stableCadence(
                "adaptive-stable-cadence-disabled",
                generatedLimit,
                this->state.stableCadence.baselineBaseFps,
                baseFps,
                "capacity-changed"
            );
            this->state.stableCadence.limit.reset();
            this->state.stableCadence.evaluationAt.reset();
            this->state.stableCadence.convergenceProbe = false;
            this->state.stableCadence.convergedTwoX = false;
            this->state.stableCadence.delivery.reset();
            this->state.stableCadence.outsideRangeSince.reset();
            this->state.stableCadence.retryAt =
                now + adaptiveStableCadenceRetryDelay;
        } else if (!cadenceStillUseful && !convergenceEvaluation) {
            if (!this->state.stableCadence.outsideRangeSince)
                this->state.stableCadence.outsideRangeSince = now;
            if (now - *this->state.stableCadence.outsideRangeSince >=
                    adaptiveStableCadenceExitGraceDuration) {
                const bool rescueCooldownElapsed =
                    !this->state.rescue.cooldownUntil ||
                    now >= *this->state.rescue.cooldownUntil;
                const bool severeCollapse =
                    rescueCooldownElapsed &&
                    this->state.stableCadence.baselineBaseFps > 0.0 &&
                    baseFps <= this->state.stableCadence.baselineBaseFps *
                        adaptiveRescueBaseCollapseRatio &&
                    projectedOutputFps <= targetFps *
                        adaptiveRescueOutputCollapseRatio;
                this->diagnostics->stableCadence(
                    "adaptive-stable-cadence-disabled",
                    generatedLimit,
                    this->state.stableCadence.baselineBaseFps,
                    baseFps,
                    severeCollapse ? "collapse-rescue" : "outside-useful-range"
                );
                const double rescueBaselineBaseFps =
                    this->state.stableCadence.baselineBaseFps;
                const bool convergedTwoX =
                    this->state.stableCadence.convergedTwoX;
                this->state.stableCadence.limit.reset();
                this->state.stableCadence.evaluationAt.reset();
                this->state.stableCadence.convergenceProbe = false;
                this->state.stableCadence.convergedTwoX = false;
                this->state.stableCadence.delivery.reset();
                this->state.stableCadence.outsideRangeSince.reset();
                this->state.stableCadence.retryAt =
                    now + (convergedTwoX
                        ? adaptiveStableCadenceConvergenceRetryDelay
                        : adaptiveStableCadenceRetryDelay);
                if (severeCollapse) {
                    this->state.rescue.previousLimit = generatedLimit;
                    this->state.rescue.baselineBaseFps = rescueBaselineBaseFps;
                    this->state.rescue.until =
                        now + adaptiveRescueMeasurementDuration;
                    this->state.rescue.cooldownUntil =
                        now + adaptiveRescueCooldown;
                    this->state.ramp.targetDeficitSince.reset();
                    this->state.outputPlanner.resetTargetClock();
                    this->diagnostics->rescueStart(
                        generatedLimit,
                        rescueBaselineBaseFps,
                        baseFps,
                        projectedOutputFps
                    );
                    return {};
                }
            }
        } else {
            this->state.stableCadence.outsideRangeSince.reset();
        }

        if (this->state.stableCadence.limit &&
                this->state.stableCadence.evaluationAt &&
                now >= *this->state.stableCadence.evaluationAt) {
            const size_t evaluatedGeneratedLimit =
                *this->state.stableCadence.limit;
            const double evaluatedProjectedOutputFps = baseFps *
                static_cast<double>(evaluatedGeneratedLimit + 1);
            const double evaluatedDemandRatio =
                desiredOutputsPerRealFrame /
                static_cast<double>(evaluatedGeneratedLimit + 1);
            const bool deliveryHealthy =
                this->state.stableCadence.delivery.healthy();
            const bool convergenceProbe =
                this->state.stableCadence.convergenceProbe;
            const double maximumOutputRatio = convergenceProbe
                ? adaptiveStableCadenceMaximumConvergenceOutputRatio
                : adaptiveStableCadenceMaximumRetainedOvershootRatio;
            const bool accepted = deliveryHealthy &&
                evaluatedDemandRatio >=
                    adaptiveStableCadenceMinimumDemandRatio &&
                evaluatedProjectedOutputFps >=
                    targetFps *
                        adaptiveStableCadenceMinimumQualificationTargetRatio &&
                evaluatedProjectedOutputFps <=
                    targetFps * maximumOutputRatio &&
                baseFps >= this->state.stableCadence.baselineBaseFps *
                    adaptiveStableCadenceMinimumBaseRetention;
            this->diagnostics->stableCadence(
                accepted ? "adaptive-stable-cadence-accepted"
                         : "adaptive-stable-cadence-rejected",
                evaluatedGeneratedLimit,
                this->state.stableCadence.baselineBaseFps,
                baseFps,
                convergenceProbe ? "ordered-2x-convergence" : ""
            );
            this->state.stableCadence.evaluationAt.reset();
            this->state.stableCadence.convergenceProbe = false;
            this->state.stableCadence.convergedTwoX =
                accepted && convergenceProbe;
            this->state.stableCadence.delivery.reset();
            if (!accepted) {
                this->state.stableCadence.limit.reset();
                this->state.stableCadence.outsideRangeSince.reset();
                this->state.stableCadence.retryAt = now + (
                    convergenceProbe
                        ? adaptiveStableCadenceConvergenceRetryDelay
                        : adaptiveStableCadenceRetryDelay
                );
            }
        }
    }

    const bool stableCadenceProbePermitted =
        !this->state.stableCadence.limit && stableCadenceCandidate &&
            !this->state.ramp.evaluationAt &&
            !this->state.rearm.required &&
            (!this->state.rescue.cooldownUntil ||
             now >= *this->state.rescue.cooldownUntil) &&
            (!this->state.stableCadence.retryAt ||
             now >= *this->state.stableCadence.retryAt);
    bool stableCadenceCandidateQualified = false;
    if (!stableCadenceProbePermitted) {
        this->state.stableCadence.candidate.reset();
    } else if (this->state.stableCadence.candidate.limit !=
                stableCadenceCandidate->generatedFrames ||
            this->state.stableCadence.candidate.convergenceProbe !=
                stableCadenceCandidate->convergenceProbe) {
        this->state.stableCadence.candidate.limit =
            stableCadenceCandidate->generatedFrames;
        this->state.stableCadence.candidate.convergenceProbe =
            stableCadenceCandidate->convergenceProbe;
        this->state.stableCadence.candidate.since = now;
        this->state.stableCadence.candidate.minimumBaseFps = baseFps;
        this->state.stableCadence.candidate.maximumBaseFps = baseFps;
    } else {
        this->state.stableCadence.candidate.minimumBaseFps = std::min(
            this->state.stableCadence.candidate.minimumBaseFps, baseFps
        );
        this->state.stableCadence.candidate.maximumBaseFps = std::max(
            this->state.stableCadence.candidate.maximumBaseFps, baseFps
        );
        const bool spreadTooWide =
            this->state.stableCadence.candidate.minimumBaseFps > 0.0 &&
            this->state.stableCadence.candidate.maximumBaseFps >
                this->state.stableCadence.candidate.minimumBaseFps *
                    adaptiveStableCadenceMaximumCandidateSpreadRatio;
        if (spreadTooWide) {
            // Begin a fresh qualification window at the new cadence instead
            // of allowing old low/high samples to trigger a workload switch.
            this->state.stableCadence.candidate.since = now;
            this->state.stableCadence.candidate.minimumBaseFps = baseFps;
            this->state.stableCadence.candidate.maximumBaseFps = baseFps;
        } else {
            stableCadenceCandidateQualified =
                this->state.stableCadence.candidate.since &&
                now - *this->state.stableCadence.candidate.since >=
                    adaptiveStableCadenceCandidateDuration;
        }
    }

    if (stableCadenceCandidateQualified) {
        this->state.stableCadence.limit =
            this->state.stableCadence.candidate.limit;
        this->state.stableCadence.convergenceProbe =
            this->state.stableCadence.candidate.convergenceProbe;
        this->state.stableCadence.convergedTwoX = false;
        this->state.stableCadence.baselineBaseFps = baseFps;
        this->state.stableCadence.evaluationAt =
            now + adaptiveStableCadenceEvaluationDuration;
        this->state.stableCadence.delivery.reset();
        this->state.stableCadence.outsideRangeSince.reset();
        this->state.stableCadence.retryAt.reset();
        this->state.stableCadence.candidate.reset();
        this->state.outputPlanner.resetTargetClock();
        this->diagnostics->stableCadence(
            "adaptive-stable-cadence-probe",
            *this->state.stableCadence.limit,
            baseFps,
            baseFps,
            this->state.stableCadence.convergenceProbe
                ? "ordered-2x-convergence" : ""
        );
    }

    return {.planningReady = true};
}

MAKO_ADAPTIVE_STAGE_INLINE void AdaptiveScheduler::advanceEfficiencyProbe(
        const TimePoint now, const double baseFps) {
    auto& probe = this->state.efficiencyProbe;
    const double targetFps = static_cast<double>(this->config.targetFps);

    if (probe.evaluationAt) {
        if (now < *probe.evaluationAt)
            return;

        const size_t testedLimit = probe.testedLimit;
        const double projectedOutputFps = baseFps *
            static_cast<double>(testedLimit + 1);
        const bool deliveryHealthy = probe.delivery.healthy();
        const bool accepted = deliveryHealthy &&
            baseFps >= adaptiveMinimumBaseFps &&
            projectedOutputFps >=
                targetFps * adaptiveEfficiencyProbeMinimumTargetRatio;
        const bool stillSettling = !accepted && deliveryHealthy &&
            !probe.settlingGraceUsed && probe.baselineBaseFps > 0.0 &&
            baseFps >= probe.baselineBaseFps *
                adaptiveEfficiencyProbeSettlingMinimumBaseRiseRatio &&
            projectedOutputFps >= targetFps *
                adaptiveEfficiencyProbeSettlingMinimumTargetRatio;
        if (stillSettling) {
            // Ordered FIFO backpressure can take longer than the first 250 ms
            // to release after reducing generated work. Extend only a probe
            // that is already close to target and recovering strongly, then
            // retain the original 98% acceptance requirement at the new
            // deadline. A true fixed-rate deficit still rolls back promptly.
            probe.settlingGraceUsed = true;
            probe.evaluationAt =
                now + adaptiveEfficiencyProbeSettlingGraceDuration;
            this->diagnostics->stableCadence(
                "adaptive-efficiency-probe-extended",
                testedLimit,
                probe.baselineBaseFps,
                baseFps,
                "base-recovering"
            );
            return;
        }
        this->diagnostics->stableCadence(
            accepted
                ? "adaptive-efficiency-probe-accepted"
                : "adaptive-efficiency-probe-rejected",
            testedLimit,
            probe.baselineBaseFps,
            baseFps,
            accepted
                ? "target-preserved"
                : deliveryHealthy ? "target-deficit" : "delivery-pressure"
        );

        probe.evaluationAt.reset();
        probe.eligibleSince.reset();
        probe.delivery.reset();
        if (accepted) {
            // The cheaper constant cadence preserved the requested output, so
            // make it the qualified policy rather than immediately ramping
            // back into the more expensive local optimum.
            this->state.stableCadence.limit = testedLimit;
            this->state.stableCadence.baselineBaseFps = baseFps;
            this->state.stableCadence.evaluationAt.reset();
            this->state.stableCadence.convergenceProbe = false;
            this->state.stableCadence.convergedTwoX = false;
            this->state.stableCadence.delivery.reset();
            this->state.stableCadence.outsideRangeSince.reset();
            this->state.stableCadence.retryAt.reset();
            this->state.stableCadence.candidate.reset();
            this->state.outputPlanner.generationLimit = testedLimit;
            this->state.ramp.previousLimit = testedLimit;
            this->state.ramp.targetDeficitSince.reset();
            this->state.strictLoad.baselineLimit = 0;
            this->state.strictLoad.baselineBaseFps = 0.0;
            this->state.strictLoad.collapseSince.reset();
            this->state.strictLoad.healthySince.reset();
            this->state.strictLoad.recoverySince.reset();
            probe.retryAt.reset();
        } else {
            // Resume the retained qualified multiplier on this same decision
            // and keep failed probes rare enough that an unreachable lower
            // cadence cannot create periodic gameplay stutter.
            probe.retryAt = now + adaptiveEfficiencyProbeRetryDelay;
        }
        probe.testedLimit = 0;
        probe.baselineBaseFps = 0.0;
        probe.settlingGraceUsed = false;
        this->state.outputPlanner.resetTargetClock();
        return;
    }

    const bool eligible =
        this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr &&
        this->state.stableCadence.limit &&
        *this->state.stableCadence.limit > 1 &&
        !this->state.stableCadence.evaluationAt &&
        !this->state.stableCadence.outsideRangeSince &&
        !this->state.ramp.evaluationAt &&
        !this->state.rearm.required &&
        !this->state.rescue.until &&
        !this->state.discontinuityRecovery.deadline &&
        !this->state.stabilization.until &&
        !this->state.nativeCadenceProbe.active;
    if (!eligible) {
        probe.eligibleSince.reset();
        if (!this->state.stableCadence.limit)
            probe.reset();
        return;
    }
    if (probe.retryAt && now < *probe.retryAt) {
        probe.eligibleSince.reset();
        return;
    }

    const size_t currentLimit = *this->state.stableCadence.limit;
    const double currentOutputFps = baseFps *
        static_cast<double>(currentLimit + 1);
    if (currentOutputFps <
            targetFps * adaptiveEfficiencyProbeMinimumTargetRatio) {
        probe.eligibleSince.reset();
        return;
    }
    if (!probe.eligibleSince) {
        probe.eligibleSince = now;
        return;
    }
    if (now - *probe.eligibleSince < adaptiveEfficiencyProbeHoldDuration)
        return;

    probe.testedLimit = currentLimit - 1;
    probe.baselineBaseFps = baseFps;
    probe.evaluationAt = now + adaptiveEfficiencyProbeEvaluationDuration;
    probe.eligibleSince.reset();
    probe.retryAt.reset();
    probe.delivery.reset();
    probe.settlingGraceUsed = false;
    this->state.nativeCadenceProbe.reset();
    this->state.outputPlanner.resetTargetClock();
    this->diagnostics->stableCadence(
        "adaptive-efficiency-probe",
        probe.testedLimit,
        probe.baselineBaseFps,
        baseFps,
        "lower-generated-load"
    );
}

MAKO_ADAPTIVE_STAGE_INLINE size_t AdaptiveScheduler::selectGeneratedFrameCount(
        const double desiredOutputsPerRealFrame,
        const double rawIntervalSeconds,
        const size_t maximumGeneratedFrameCount) {
    size_t generatedFrameCount = 0;
    if (this->state.efficiencyProbe.evaluationAt) {
        generatedFrameCount = this->state.efficiencyProbe.testedLimit;
        this->state.outputPlanner.resetTargetClock();
    } else if (this->state.stableCadence.limit) {
        generatedFrameCount = *this->state.stableCadence.limit;
        this->state.outputPlanner.resetTargetClock();
    } else if (desiredOutputsPerRealFrame > 1.0 &&
            maximumGeneratedFrameCount > 0) {
        auto& targetClock = this->state.outputPlanner;
        targetClock.targetClockActive = true;
        targetClock.budgetCreditOutputs += desiredOutputsPerRealFrame;
        const size_t requestedOutputs = std::max<size_t>(
            1,
            static_cast<size_t>(std::floor(
                targetClock.budgetCreditOutputs + 1e-9
            ))
        );
        const size_t maximumOutputs = maximumGeneratedFrameCount + 1;
        const size_t baselineOutputs = std::min(
            requestedOutputs, maximumOutputs
        );
        targetClock.budgetCreditOutputs -=
            static_cast<double>(baselineOutputs);
        if (targetClock.budgetCreditOutputs < 0.0)
            targetClock.budgetCreditOutputs = 0.0;
        if (baselineOutputs == maximumOutputs &&
                targetClock.budgetCreditOutputs >= 1.0) {
            targetClock.budgetCreditOutputs = std::fmod(
                targetClock.budgetCreditOutputs, 1.0
            );
        }
        size_t scheduledOutputs = baselineOutputs;

        const double rawIntervalOutputs =
            rawIntervalSeconds * static_cast<double>(this->config.targetFps);
        const double rawAccumulatedOutputs =
            targetClock.targetPhaseErrorOutputs +
            rawIntervalOutputs;
        const size_t rawPreferredOutputs = std::clamp<size_t>(
            static_cast<size_t>(std::max(
                1.0,
                std::floor(rawAccumulatedOutputs + 0.5 + 1e-9)
            )),
            1,
            maximumOutputs
        );
        const auto placementSquaredError = [rawIntervalOutputs](
                const size_t outputCount) {
            const double spacingError =
                rawIntervalOutputs *
                    adaptiveOutputCountReciprocals[outputCount] -
                1.0;
            return static_cast<double>(outputCount) *
                spacingError * spacingError;
        };
        const double baselineSpacingOutputs =
            rawIntervalOutputs *
                adaptiveOutputCountReciprocals[baselineOutputs];
        const double maximumBaselineSpacingOutputs = std::max(
            targetClock.maximumBaselineSpacingOutputs,
            baselineSpacingOutputs
        );

        // Raw timing is a placement signal, never a source of additional
        // generated work. It may defer one output already earned by the
        // smoothed workload budget when the current interval is materially
        // better left undivided. A separate one-output ledger then makes that
        // earned work available to a later interval without confusing it with
        // impossible ceiling debt. A deferral may not exceed the running worst
        // requested spacing of the unmodified workload plan.
        if (targetClock.deferredBudgetOutput &&
                scheduledOutputs < maximumOutputs &&
                placementSquaredError(scheduledOutputs + 1) -
                    placementSquaredError(scheduledOutputs) <=
                    targetClock.deferredPlacementBenefit + 1e-12) {
            // Repay exactly one output previously earned by the baseline
            // workload ledger. Keeping it separate from fractional credit
            // prevents ceiling normalization from discarding intentional
            // placement work or confusing it with impossible target debt.
            // The saved squared-spacing benefit is the repayment budget, so
            // a still-short interval cannot undo the pacing improvement.
            scheduledOutputs++;
            targetClock.deferredBudgetOutput = false;
            targetClock.deferredPlacementBenefit = 0.0;
        } else if (!targetClock.deferredBudgetOutput &&
                requestedOutputs <= maximumOutputs &&
                baselineOutputs > 1 &&
                desiredOutputsPerRealFrame <=
                    static_cast<double>(maximumOutputs) -
                        adaptiveTargetClockMinimumRepaymentHeadroomOutputs &&
                rawPreferredOutputs < baselineOutputs) {
            const size_t deferredOutputs = baselineOutputs - 1;
            const double deferredSpacingOutputs =
                rawIntervalOutputs *
                    adaptiveOutputCountReciprocals[deferredOutputs];
            const double deferredSpacingError = std::abs(
                deferredSpacingOutputs - 1.0
            );
            const double budgetedSpacingError = std::abs(
                rawIntervalOutputs *
                    adaptiveOutputCountReciprocals[baselineOutputs] -
                1.0
            );
            const double placementBenefit =
                placementSquaredError(baselineOutputs) -
                placementSquaredError(deferredOutputs);
            if (deferredSpacingError +
                    adaptiveTargetClockPlacementDeadbandRatio <
                    budgetedSpacingError &&
                    deferredSpacingOutputs <=
                        maximumBaselineSpacingOutputs + 1e-12 &&
                    placementBenefit > 0.0) {
                scheduledOutputs = deferredOutputs;
                targetClock.deferredBudgetOutput = true;
                targetClock.deferredPlacementBenefit = placementBenefit;
            }
        }
        targetClock.maximumBaselineSpacingOutputs =
            maximumBaselineSpacingOutputs;

        generatedFrameCount = scheduledOutputs - 1;
        targetClock.targetPhaseErrorOutputs =
            rawAccumulatedOutputs - static_cast<double>(scheduledOutputs);
        targetClock.targetPhaseErrorOutputs -= std::floor(
            targetClock.targetPhaseErrorOutputs + 0.5
        );
        if (std::abs(targetClock.targetPhaseErrorOutputs) < 1e-12)
            targetClock.targetPhaseErrorOutputs = 0.0;
    } else {
        // A Vulkan layer cannot present fewer real frames than the application
        // submits. Do not carry phase debt when the base rate is already at or
        // above the target, or while no generated-frame capacity is admitted.
        this->state.outputPlanner.resetTargetClock();
    }
    return generatedFrameCount;
}

MAKO_ADAPTIVE_STAGE_INLINE AdaptiveScheduler::PlanningStageResult
AdaptiveScheduler::advanceNativeCadenceProbe(
        const TimePoint now, const double baseFps,
        const double instantaneousBaseFps,
        const double desiredOutputsPerRealFrame,
        const size_t maximumGeneratedFrameCount,
        size_t& generatedFrameCount) {
    // Ordered FIFO work makes a genuine 30-FPS source observationally
    // identical to a native 60-FPS source held at 30 by MAKO's previous
    // generated-plus-original present. The opt-in probe removes generated
    // work for one frame, then requires a short faster run before rebasing.
    // A rejected probe immediately resumes the proven generated policy.
    const bool eligible =
        this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr &&
        desiredOutputsPerRealFrame > 1.0 &&
        maximumGeneratedFrameCount > 0 &&
        !this->state.ramp.evaluationAt &&
        !this->state.rearm.required &&
        !this->state.rescue.until &&
        !this->state.discontinuityRecovery.deadline &&
        !this->state.stableCadence.evaluationAt &&
        !this->state.efficiencyProbe.evaluationAt;
    if (!eligible) {
        this->state.nativeCadenceProbe.reset();
        return {.planningReady = true};
    }

    auto& probe = this->state.nativeCadenceProbe;
    if (probe.active) {
        const bool fasterCadence = instantaneousBaseFps >=
            probe.baselineBaseFps * adaptiveNativeCadenceMinimumRiseRatio;
        if (!fasterCadence) {
            this->diagnostics->nativeCadenceProbe(
                "dynamic-cadence-probe-rejected",
                this->state.outputPlanner.generationLimit,
                probe.baselineBaseFps,
                instantaneousBaseFps,
                probe.confirmedSamples
            );
            probe.active = false;
            probe.baselineBaseFps = 0.0;
            probe.minimumMeasuredBaseFps = 0.0;
            probe.confirmedSamples = 0;
            probe.nextAt = now + this->config.dynamicCadenceProbeInterval;
            this->state.outputPlanner.resetTargetClock();
            generatedFrameCount = std::max<size_t>(generatedFrameCount, 1);
            return {.planningReady = true};
        }

        probe.confirmedSamples++;
        if (probe.minimumMeasuredBaseFps == 0.0) {
            probe.minimumMeasuredBaseFps = instantaneousBaseFps;
        } else {
            probe.minimumMeasuredBaseFps = std::min(
                probe.minimumMeasuredBaseFps, instantaneousBaseFps
            );
        }
        this->state.outputPlanner.resetTargetClock();
        generatedFrameCount = 0;
        if (probe.confirmedSamples <
                adaptiveNativeCadenceConfirmationFrames) {
            return {};
        }

        const double recoveredBaseFps = probe.minimumMeasuredBaseFps;
        this->state.cadence.smoothedIntervalSeconds = 1.0 / recoveredBaseFps;
        this->state.cadence.dropFrames = 0;
        this->state.ramp.targetDeficitSince.reset();
        this->state.stableCadence.limit.reset();
        this->state.stableCadence.evaluationAt.reset();
        this->state.stableCadence.convergenceProbe = false;
        this->state.stableCadence.convergedTwoX = false;
        this->state.stableCadence.delivery.reset();
        this->state.stableCadence.outsideRangeSince.reset();
        this->state.stableCadence.retryAt.reset();
        this->state.stableCadence.baselineBaseFps = 0.0;
        this->state.stableCadence.candidate.reset();
        this->state.efficiencyProbe.reset();
        this->diagnostics->nativeCadenceProbe(
            "dynamic-cadence-recovered",
            this->state.outputPlanner.generationLimit,
            probe.baselineBaseFps,
            recoveredBaseFps,
            probe.confirmedSamples
        );
        probe.active = false;
        probe.baselineBaseFps = 0.0;
        probe.minimumMeasuredBaseFps = 0.0;
        probe.confirmedSamples = 0;
        probe.nextAt = now + this->config.dynamicCadenceProbeInterval;
        return {};
    }

    if (!probe.nextAt) {
        probe.nextAt = now + this->config.dynamicCadenceProbeInterval;
        return {.planningReady = true};
    }
    if (now < *probe.nextAt)
        return {.planningReady = true};

    probe.active = true;
    probe.nextAt.reset();
    probe.baselineBaseFps = baseFps;
    probe.minimumMeasuredBaseFps = 0.0;
    probe.confirmedSamples = 0;
    this->state.outputPlanner.resetTargetClock();
    generatedFrameCount = 0;
    this->diagnostics->nativeCadenceProbe(
        "dynamic-cadence-probe-start",
        this->state.outputPlanner.generationLimit,
        baseFps,
        0.0,
        0
    );
    return {};
}

MAKO_ADAPTIVE_STAGE_INLINE bool
AdaptiveScheduler::advanceNearTargetNativePreference(
        const TimePoint now, const double baseFps,
        const size_t configuredGenerationLimit) {
    auto& preference = this->state.nearTargetNativePreference;
    if (!this->config.nearTargetNativePreference ||
            configuredGenerationLimit == 0) {
        preference.reset();
        return false;
    }

    // Recovery, delivery evaluation, and qualified Smooth Cadence retain their
    // existing ownership. Near-target preference is only an Active Fractional
    // policy and never changes a transition already being measured.
    if (this->state.discontinuityRecovery.deadline ||
            this->state.rescue.until || this->state.rearm.required ||
            this->state.ramp.evaluationAt ||
            this->state.stableCadence.limit ||
            this->state.stableCadence.evaluationAt ||
            this->state.efficiencyProbe.evaluationAt ||
            this->state.nativeCadenceProbe.active) {
        preference.resetCandidate();
        return preference.active;
    }

    const double targetFps = static_cast<double>(this->config.targetFps);
    const bool nativeOutputMeetsTarget =
        baseFps >= targetFps * adaptiveNearTargetNativeMinimumOutputRatio;
    const bool nativeOutputBelowRetentionFloor =
        preference.active && !nativeOutputMeetsTarget;
    const bool couldPreferNative = nativeOutputMeetsTarget &&
        baseFps < targetFps;
    const auto quality = preference.active || couldPreferNative
        ? predictNearTargetCadenceQuality(baseFps, this->config.targetFps)
        : NearTargetCadenceQualityPrediction{};
    bool requestedActive = false;
    if (preference.active) {
        if (baseFps >= targetFps) {
            requestedActive = true;
        } else if (!nativeOutputMeetsTarget) {
            requestedActive = false;
        } else if (quality.valid) {
            const double exitToleranceMilliseconds =
                1000.0 / targetFps *
                adaptiveNearTargetEntryTargetIntervalTolerance;
            requestedActive =
                quality.fractionalIntervalRmsMilliseconds() +
                    exitToleranceMilliseconds >=
                quality.nativeIntervalRmsMilliseconds();
        }
    } else if (baseFps >= targetFps &&
            preference.candidateActive.value_or(false)) {
        // Preserve an already-started near-target qualification through a
        // noisy sample that briefly crosses the target. Do not start the state
        // from a genuinely above-target stream, where native output is already
        // mandatory and no preference transition is needed.
        requestedActive = true;
    } else if (quality.valid) {
        // The output-retention floor has already rejected material target
        // deficits. Compare the two derived RMS values with a small target-
        // interval tolerance so measurement noise inside the final five
        // percent does not retain sparse Fractional placement unnecessarily.
        const double entryToleranceMilliseconds =
            1000.0 / targetFps *
            adaptiveNearTargetEntryTargetIntervalTolerance;
        requestedActive =
            quality.fractionalIntervalRmsMilliseconds() +
                entryToleranceMilliseconds >=
            quality.nativeIntervalRmsMilliseconds();
    }

    auto evidenceSample = AdaptiveScheduler::Clock::duration{};
    if (preference.lastEvaluationAt) {
        evidenceSample = std::clamp(
            now - *preference.lastEvaluationAt,
            AdaptiveScheduler::Clock::duration{},
            std::chrono::duration_cast<AdaptiveScheduler::Clock::duration>(
                adaptiveNearTargetMaximumEvidenceSample
            )
        );
    }
    preference.lastEvaluationAt = now;

    if (requestedActive == preference.active) {
        const auto decay = evidenceSample *
            adaptiveNearTargetOppositeEvidenceDecay;
        preference.candidateEvidence = decay >= preference.candidateEvidence
            ? AdaptiveScheduler::Clock::duration{}
            : preference.candidateEvidence - decay;
        if (preference.candidateEvidence ==
                AdaptiveScheduler::Clock::duration{})
            preference.candidateActive.reset();
        return preference.active;
    }
    if (preference.candidateActive != requestedActive) {
        preference.candidateActive = requestedActive;
        preference.candidateEvidence = AdaptiveScheduler::Clock::duration{};
    }
    preference.candidateEvidence += evidenceSample;
    if (preference.candidateEvidence < adaptiveNearTargetNativeHoldDuration) {
        return preference.active;
    }

    preference.active = requestedActive;
    preference.resetCandidate();
    this->state.outputPlanner.resetTargetClock();
    if (preference.active) {
        this->state.outputPlanner.generationLimit = 0;
        this->state.ramp.nextAt.reset();
        this->state.ramp.evaluationAt.reset();
        this->state.ramp.targetDeficitSince.reset();
        this->state.ramp.delivery.reset();
        this->state.ramp.previousLimit = 0;
        this->state.ramp.baselineBaseFps = 0.0;
        this->state.ramp.bridgeActive = false;
        this->state.ramp.bridgeBaselineLimit = 0;
        this->state.ramp.bridgeBaselineBaseFps = 0.0;
        this->state.strictLoad.baselineLimit = 0;
        this->state.strictLoad.baselineBaseFps = 0.0;
        this->state.strictLoad.collapseSince.reset();
        this->state.strictLoad.healthySince.reset();
        this->state.strictLoad.recoverySince.reset();
        this->state.efficiencyProbe.reset();
        this->state.nativeCadenceProbe.reset();
    } else {
        // The one-second quality hold already proved a sustained Fractional
        // advantage. Credit that interval to the normal ramp deficit hold so
        // generation can resume without imposing a second policy delay.
        this->state.ramp.targetDeficitSince =
            now - adaptiveTargetDeficitDuration;
    }
    this->diagnostics->nearTargetNativePreference(
        preference.active
            ? "adaptive-near-target-native-enabled"
            : "adaptive-near-target-native-disabled",
        this->config.targetFps,
        baseFps,
        quality.nativeIntervalRmsMilliseconds(),
        quality.fractionalIntervalRmsMilliseconds(),
        adaptiveNearTargetNativeHoldDuration,
        preference.active
            ? "native-predicted-rms-not-worse"
            : (nativeOutputBelowRetentionFloor
                ? "native-output-below-retention-floor"
                : "fractional-predicted-rms-advantage")
    );
    return preference.active;
}

MAKO_ADAPTIVE_STAGE_INLINE AdaptiveScheduler::PlanningStageResult
AdaptiveScheduler::applyStrictLoadGuard(
        const TimePoint now, const double baseFps,
        size_t& generatedFrameCount) {
    // A ramp can pass its one-second evaluation and still settle into a slower
    // compositor divisor afterwards. Ordered SDR can immediately restore its
    // previous proven generated level because FIFO delivery is deterministic.
    // The Gamescope HDR bridge first measures real-only cadence because a
    // collapse there may instead be colour-transition or admission pressure.
    const double strictBaselineOutputFps = std::min(
        static_cast<double>(this->config.targetFps),
        this->state.strictLoad.baselineBaseFps *
            static_cast<double>(this->state.strictLoad.baselineLimit + 1)
    );
    const double strictCurrentOutputFps = std::min(
        static_cast<double>(this->config.targetFps),
        baseFps * static_cast<double>(this->state.outputPlanner.generationLimit + 1)
    );
    const double severeTargetDeficitFps =
        static_cast<double>(this->config.targetFps) *
            adaptiveStrictLoadSevereTargetDeficitRatio;
    const bool fallbackPreservesOutput =
        strictCurrentOutputFps <= strictBaselineOutputFps;
    const bool severeCurrentOutputDeficit =
        strictCurrentOutputFps <= severeTargetDeficitFps;
    const bool severeMarginalOutputGain =
        strictBaselineOutputFps < severeTargetDeficitFps &&
        severeCurrentOutputDeficit &&
        strictCurrentOutputFps < strictBaselineOutputFps *
            adaptiveRampMarginalGain;
    const bool strictOutputCollapse =
        this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr
        ? severeCurrentOutputDeficit &&
            (fallbackPreservesOutput || severeMarginalOutputGain)
        : strictCurrentOutputFps < strictBaselineOutputFps *
            adaptiveRampMarginalGain;
    const bool strictLoadMonitored =
        !this->state.stableCadence.limit &&
        !this->state.ramp.evaluationAt &&
        !this->state.rearm.required &&
        !this->state.rescue.until &&
        this->state.strictLoad.baselineBaseFps > 0.0 &&
        this->state.outputPlanner.generationLimit > this->state.strictLoad.baselineLimit &&
        generatedFrameCount == this->state.outputPlanner.generationLimit;
    const bool strictLoadCollapse =
        strictLoadMonitored &&
        baseFps < this->state.strictLoad.baselineBaseFps *
            adaptiveStrictLoadCollapseRatio &&
        strictOutputCollapse &&
        (!this->state.rescue.cooldownUntil ||
         now >= *this->state.rescue.cooldownUntil);
    if (strictLoadCollapse) {
        this->state.strictLoad.healthySince.reset();
        this->state.strictLoad.recoverySince.reset();
        if (!this->state.strictLoad.collapseSince)
            this->state.strictLoad.collapseSince = now;
        if (now - *this->state.strictLoad.collapseSince >=
                adaptiveStrictLoadCollapseDuration) {
            const size_t collapsedLimit = this->state.outputPlanner.generationLimit;
            if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
                // The ordered SDR path does not need a disruptive one-second
                // real-only measurement. A higher multiplier can immediately
                // fall back to its cheaper proven level during a severe target
                // deficit when that level can preserve the current displayed
                // rate; 2x is retained because there is no cheaper generated
                // policy to compare against.
                const size_t fallbackLimit =
                    this->state.strictLoad.baselineLimit;
                const size_t resumedLimit = fallbackLimit > 0
                    ? fallbackLimit : collapsedLimit;
                const double baselineBaseFps =
                    this->state.strictLoad.baselineBaseFps;
                if (this->state.strictLoad.failedLimit != collapsedLimit) {
                    this->state.strictLoad.failedLimit = collapsedLimit;
                    this->state.strictLoad.consecutiveFailures = 0;
                }
                this->state.strictLoad.consecutiveFailures++;
                this->state.strictLoad.failedBaselineBaseFps =
                    baselineBaseFps;
                const auto retryDelay = adaptiveRampRetryDelayForFailures(
                    this->state.strictLoad.consecutiveFailures + 1
                );
                this->state.outputPlanner.generationLimit = resumedLimit;
                this->state.ramp.previousLimit = resumedLimit;
                this->state.ramp.nextAt = now + retryDelay;
                this->state.rescue.cooldownUntil =
                    now + retryDelay;
                this->state.ramp.targetDeficitSince.reset();
                this->state.strictLoad.baselineLimit = 0;
                this->state.strictLoad.baselineBaseFps = 0.0;
                this->state.strictLoad.collapseSince.reset();
                this->state.strictLoad.healthySince.reset();
                this->state.strictLoad.recoverySince.reset();
                this->state.outputPlanner.resetTargetClock();
                generatedFrameCount = std::min(
                    generatedFrameCount, resumedLimit
                );
                this->diagnostics->loadShed(
                    collapsedLimit,
                    resumedLimit,
                    baselineBaseFps,
                    baseFps,
                    fallbackLimit > 0
                        ? "sdr-direct-fallback"
                        : "sdr-retain-2x"
                );
                this->diagnostics->rampBackoff(
                    collapsedLimit,
                    this->state.strictLoad.consecutiveFailures,
                    baselineBaseFps,
                    retryDelay
                );
            } else {
                this->state.rescue.previousLimit =
                    this->state.strictLoad.baselineLimit;
                this->state.rescue.baselineBaseFps =
                    this->state.strictLoad.baselineBaseFps;
                this->state.rescue.fromStrictLoad = true;
                this->state.rescue.strictLoadLimit = collapsedLimit;
                this->state.rescue.until =
                    now + adaptiveRescueMeasurementDuration;
                this->state.rescue.cooldownUntil = now + adaptiveRescueCooldown;
                this->state.ramp.targetDeficitSince.reset();
                this->state.strictLoad.baselineLimit = 0;
                this->state.strictLoad.baselineBaseFps = 0.0;
                this->state.strictLoad.collapseSince.reset();
                this->state.strictLoad.healthySince.reset();
                this->state.strictLoad.recoverySince.reset();
                this->state.outputPlanner.resetTargetClock();
                this->diagnostics->rescueStart(
                    collapsedLimit,
                    this->state.rescue.baselineBaseFps,
                    baseFps,
                    strictCurrentOutputFps,
                    "strict-load-collapse"
                );
                return {};
            }
        }
    } else {
        this->state.strictLoad.collapseSince.reset();
        if (strictLoadMonitored &&
                this->state.strictLoad.failedLimit ==
                    this->state.outputPlanner.generationLimit &&
                this->state.strictLoad.consecutiveFailures > 0) {
            if (!this->state.strictLoad.healthySince)
                this->state.strictLoad.healthySince = now;
            if (now - *this->state.strictLoad.healthySince >=
                    adaptiveStrictLoadHealthyDuration) {
                this->state.strictLoad.failedLimit = 0;
                this->state.strictLoad.consecutiveFailures = 0;
                this->state.strictLoad.failedBaselineBaseFps = 0.0;
                this->state.strictLoad.healthySince.reset();
                this->state.strictLoad.recoverySince.reset();
            }
        } else {
            this->state.strictLoad.healthySince.reset();
        }
    }

    return {.planningReady = true};
}

#if defined(__OPTIMIZE__) && (defined(__GNUC__) || defined(__clang__))
// The stage methods are ownership boundaries on the per-present hot path.
// Flatten them in optimized builds so that modularity adds no call overhead.
__attribute__((flatten))
#endif
AdaptiveFramePlan AdaptiveScheduler::planFrame(
        const std::chrono::steady_clock::time_point now,
        const bool generatedImageAcquireBackoff) {
    if (this->diagnosticsActive)
        this->state.pacingWindow.beginFrame();
    const auto cadence = this->observeCadence(
        now, generatedImageAcquireBackoff
    );
    if (!cadence.planningReady)
        return cadence.terminalPlan;
    const double baseFps = cadence.baseFps;

    const auto discontinuity = this->advanceDiscontinuityRecovery(
        now, baseFps
    );
    if (!discontinuity.planningReady)
        return discontinuity.terminalPlan;

    const auto rescue = this->advanceRescueMeasurement(now, baseFps);
    if (!rescue.planningReady)
        return rescue.terminalPlan;
    if (this->state.stabilization.until &&
            now < *this->state.stabilization.until) {
        this->state.outputPlanner.resetTargetClock();
        if (this->diagnosticsActive &&
                (!this->state.diagnosticThrottle.lastPlanAt ||
                 now - *this->state.diagnosticThrottle.lastPlanAt >=
                    adaptivePlanDiagnosticInterval)) {
            this->state.diagnosticThrottle.lastPlanAt = now;
            AdaptivePlanDiagnostic plan;
            plan.baseFps = baseFps;
            plan.targetFps = this->config.targetFps;
            plan.phase = "stabilizing";
            this->diagnostics->plan(plan);
        }
        return {};
    }
    this->state.stabilization.until.reset();
    this->updateGenerationLimit(now, baseFps);

    const double desiredOutputsPerRealFrame =
        this->state.cadence.smoothedIntervalSeconds *
        static_cast<double>(this->config.targetFps);

    const size_t maximumGeneratedFrameCount = std::min(
        {
            this->config.generatedFrameCapacity,
            this->config.maximumMultiplier - 1,
            this->state.outputPlanner.generationLimit,
        }
    );

    const auto stableCadence = this->advanceStableCadence(
        now, baseFps, desiredOutputsPerRealFrame,
        maximumGeneratedFrameCount
    );
    if (!stableCadence.planningReady)
        return stableCadence.terminalPlan;

    this->advanceEfficiencyProbe(now, baseFps);

    size_t generatedFrameCount = this->selectGeneratedFrameCount(
        desiredOutputsPerRealFrame, cadence.rawIntervalSeconds,
        maximumGeneratedFrameCount
    );
    if (this->config.dynamicCadenceRecovery) {
        const auto nativeCadenceProbe = this->advanceNativeCadenceProbe(
            now,
            baseFps,
            cadence.instantaneousBaseFps,
            desiredOutputsPerRealFrame,
            maximumGeneratedFrameCount,
            generatedFrameCount
        );
        if (!nativeCadenceProbe.planningReady)
            return nativeCadenceProbe.terminalPlan;
    }
    const auto strictLoad = this->applyStrictLoadGuard(
        now, baseFps, generatedFrameCount
    );
    if (!strictLoad.planningReady)
        return strictLoad.terminalPlan;
    const size_t policyGenerationLimit =
        this->state.efficiencyProbe.evaluationAt
        ? this->state.efficiencyProbe.testedLimit
        : this->state.outputPlanner.generationLimit;
    const size_t effectiveMaximumGeneratedFrameCount = std::min(
        {
            this->config.generatedFrameCapacity,
            this->config.maximumMultiplier - 1,
            policyGenerationLimit,
        }
    );
    if (this->diagnosticsActive) {
        this->state.pacingWindow.record(
            cadence.rawIntervalSeconds,
            generatedFrameCount,
            this->state.outputPlanner.targetClockActive,
            this->state.outputPlanner.targetPhaseErrorOutputs,
            this->config.targetFps,
            this->state.stableCadence.limit.has_value(),
            effectiveMaximumGeneratedFrameCount,
            this->state.outputPlanner.targetClockEpoch
        );
    }
    if (this->diagnosticsActive &&
            (!this->state.diagnosticThrottle.lastPlanAt ||
             now - *this->state.diagnosticThrottle.lastPlanAt >=
                adaptivePlanDiagnosticInterval)) {
        this->state.diagnosticThrottle.lastPlanAt = now;
        const auto& pacing = this->state.pacingWindow;
        const auto rearmRemaining = this->state.rearm.required &&
                this->state.rearm.notBefore &&
                now < *this->state.rearm.notBefore
            ? *this->state.rearm.notBefore - now
            : AdaptiveScheduler::Clock::duration::zero();
        const auto nearTargetQuality = predictNearTargetCadenceQuality(
            baseFps, this->config.targetFps
        );
        this->diagnostics->plan({
            .baseFps = baseFps,
            .targetFps = this->config.targetFps,
            .generatedFrames = generatedFrameCount,
            .maximumGeneratedFrames = effectiveMaximumGeneratedFrameCount,
            .configuredMaximumGeneratedFrames =
                this->configuredGenerationLimit(),
            .stableCadence = this->state.stableCadence.limit.has_value(),
            .nearTargetNativePreference =
                this->state.nearTargetNativePreference.active,
            .predictedNativeIntervalRmsMilliseconds =
                nearTargetQuality.nativeIntervalRmsMilliseconds(),
            .predictedFractionalIntervalRmsMilliseconds =
                nearTargetQuality.fractionalIntervalRmsMilliseconds(),
            .phase = this->state.rearm.required
                ? "rearm-cooldown"
                : this->state.nearTargetNativePreference.active
                    ? "near-target-native"
                    : "",
            .consecutiveFailures = this->state.rearm.consecutiveProbeFailures,
            .rearmReason = this->state.rearm.reason,
            .rearmCooldownRemaining = rearmRemaining,
            .rearmBaselineBaseFps = this->state.rearm.baselineBaseFps,
            .targetOutputClockActive =
                this->state.outputPlanner.targetClockActive,
            .targetOutputBudgetCreditOutputs =
                this->state.outputPlanner.budgetCreditOutputs,
            .targetOutputPhaseErrorMilliseconds =
                this->state.outputPlanner.targetPhaseErrorOutputs * 1000.0 /
                static_cast<double>(this->config.targetFps),
            .targetOutputDeferredBudgetOutput =
                this->state.outputPlanner.deferredBudgetOutput,
            .pacingSourceSamples = pacing.sourceIntervals.samples,
            .sourceIntervalMeanMilliseconds = pacing.sourceIntervals.mean,
            .sourceIntervalStdDevMilliseconds =
                pacing.sourceIntervals.standardDeviation(),
            .sourceIntervalP95Milliseconds =
                pacing.sourceIntervals.percentile(0.95),
            .sourceIntervalP99Milliseconds =
                pacing.sourceIntervals.percentile(0.99),
            .generatedCountChanges = pacing.generatedCountChanges,
            .requestedIntervalSamples = pacing.requestedIntervals.samples,
            .requestedIntervalMeanMilliseconds =
                pacing.requestedIntervals.mean,
            .requestedIntervalStdDevMilliseconds =
                pacing.requestedIntervals.standardDeviation(),
            .requestedIntervalP95Milliseconds =
                pacing.requestedIntervals.percentile(0.95),
            .requestedIntervalP99Milliseconds =
                pacing.requestedIntervals.percentile(0.99),
            .targetPhaseErrorSamples = pacing.phaseErrorSamples,
            .targetPhaseErrorRmsMilliseconds = pacing.phaseErrorSamples > 0
                ? std::sqrt(
                    pacing.phaseErrorSquaredMilliseconds /
                    static_cast<double>(pacing.phaseErrorSamples)
                )
                : 0.0,
            .targetPhaseErrorMaximumMilliseconds =
                pacing.maximumAbsolutePhaseErrorMilliseconds,
        });
        this->state.pacingWindow.resetWindow();
    }

    return AdaptiveFramePlan::evenlySpaced(generatedFrameCount);
}

size_t AdaptiveScheduler::historyWarmupFrameCount() {
    return adaptiveHistoryWarmupFrames;
}

AdaptiveScheduler::Clock::duration
AdaptiveScheduler::rescueMeasurementDuration() {
    return adaptiveRescueMeasurementDuration;
}

AdaptiveScheduler::Clock::duration AdaptiveScheduler::rescueCooldown() {
    return adaptiveRescueCooldown;
}

AdaptiveScheduler::Clock::duration AdaptiveScheduler::stableRearmDuration() {
    return adaptiveStableRearmDuration;
}

void AdaptiveScheduler::resetTiming(
        const std::chrono::steady_clock::time_point now) {
    this->state.cadence.lastRealFrame = now;
    this->state.cadence.smoothedIntervalSeconds = 0.0;
    if (this->state.fastBurst.startedAt) {
        this->diagnostics->fastCadenceBurstComplete(
            this->state.fastBurst.frames,
            now - *this->state.fastBurst.startedAt
        );
    }
    this->state.fastBurst.startedAt.reset();
    this->state.fastBurst.lastDiagnosticAt.reset();
    this->state.fastBurst.frames = 0;
    this->state.fastBurst.framesSinceDiagnostic = 0;
    this->state.ramp.targetDeficitSince.reset();
    this->state.outputPlanner.resetTargetClock();
    this->state.nativeCadenceProbe.reset();
    this->state.nearTargetNativePreference.resetCandidate();
    this->state.efficiencyProbe.reset();
    this->state.pacingWindow.reset();
}

size_t AdaptiveScheduler::validatedGenerationLimit() const {
    const size_t configuredLimit = std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
    size_t generationLimit = this->state.outputPlanner.generationLimit;
    if (this->state.rearm.required) {
        generationLimit = this->state.rearm.fallbackLimit;
    } else if (this->state.ramp.evaluationAt) {
        generationLimit = this->state.ramp.bridgeActive
            ? this->state.ramp.bridgeBaselineLimit
            : this->state.ramp.previousLimit;
    }
    return std::min(generationLimit, configuredLimit);
}

void AdaptiveScheduler::restoreGenerationLimit(
        const std::chrono::steady_clock::time_point now,
        const size_t generationLimit,
        const std::string_view reason,
        const std::optional<size_t> monitoredFallbackLimit,
        const double monitoredBaselineBaseFps) {
    const size_t configuredLimit = std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
    const size_t restoredLimit = std::min(generationLimit, configuredLimit);

    this->state.outputPlanner.generationLimit = restoredLimit;
    this->state.ramp.previousLimit = restoredLimit;
    this->state.ramp.evaluationAt.reset();
    this->state.ramp.targetDeficitSince.reset();
    this->state.ramp.baselineBaseFps = 0.0;
    this->state.ramp.bridgeActive = false;
    this->state.ramp.bridgeBaselineLimit = 0;
    this->state.ramp.bridgeBaselineBaseFps = 0.0;
    this->state.rearm.required = false;
    this->state.rearm.notBefore.reset();
    this->state.rearm.stableSince.reset();
    this->state.rearm.improvementSince.reset();
    this->state.rearm.reason.clear();
    this->state.rearm.baselineBaseFps = 0.0;
    this->state.rearm.fallbackLimit = 0;
    this->state.rearm.consecutiveProbeFailures = 0;
    this->state.ramp.lastFailedLimit = 0;
    this->state.ramp.consecutiveFailures = 0;
    this->state.ramp.failedBaselineBaseFps = 0.0;
    const size_t fallbackLimit = std::min(
        monitoredFallbackLimit.value_or(restoredLimit), configuredLimit
    );
    const bool monitorRestoredLoad = restoredLimit > fallbackLimit &&
        monitoredBaselineBaseFps > 0.0;
    this->state.strictLoad.baselineLimit = monitorRestoredLoad
        ? fallbackLimit
        : 0;
    this->state.strictLoad.baselineBaseFps = monitorRestoredLoad
        ? monitoredBaselineBaseFps
        : 0.0;
    this->state.strictLoad.collapseSince.reset();
    this->state.strictLoad.healthySince.reset();
    this->state.strictLoad.recoverySince.reset();
    this->state.efficiencyProbe.reset();
    this->state.nearTargetNativePreference.reset();
    this->state.outputPlanner.resetTargetClock();

    const auto stabilizationEnd = this->state.stabilization.until.value_or(now);
    const auto higherProbeDelay = restoredLimit > 0 && restoredLimit < configuredLimit
        ? adaptiveRecoveryHigherProbeDelay
        : AdaptiveScheduler::Clock::duration::zero();
    this->state.ramp.nextAt = stabilizationEnd + higherProbeDelay;
    this->diagnostics->recoveryResume(restoredLimit, higherProbeDelay, reason);
}

void AdaptiveScheduler::beginCadenceRefresh(
        const std::chrono::steady_clock::time_point now,
        const std::string_view reason) {
    // Ordinary SDR menu/game cadence changes invalidate temporal history, but
    // they do not prove that the already validated multiplier is unsafe. Drop
    // only an unvalidated probe, refresh two real frames, and resume the proven
    // level immediately instead of entering a 1-3 second real-only recovery.
    const size_t retainedGenerationLimit = this->validatedGenerationLimit();
    const AdaptiveGenerationLoadBaseline loadBaseline =
        this->generationLoadBaseline();
    if (this->state.ramp.evaluationAt) {
        this->diagnostics->probeAborted(reason, this->state.outputPlanner.generationLimit);
    }

    this->state.stabilization.until.reset();
    this->state.discontinuityRecovery.reset();
    this->state.ramp.delivery.reset();
    this->state.cadence.dropFrames = 0;
    this->state.cadence.sdrStallBypass = reason == "cadence-stall";
    this->state.cadence.sdrResumeFrames = 0;
    this->state.cadence.sdrIsolatedHitchBridged = false;

    this->restoreGenerationLimit(
        now,
        retainedGenerationLimit,
        "sdr-cadence-refresh",
        loadBaseline.baseFps > 0.0
            ? std::optional<size_t>{loadBaseline.fallbackGenerationLimit}
            : std::nullopt,
        loadBaseline.baseFps
    );
    this->beginHistoryWarmup(adaptiveSdrCadenceRefreshFrames, false);
    this->resetTiming(now);
    this->diagnostics->cadenceRefresh(
        reason, retainedGenerationLimit, adaptiveSdrCadenceRefreshFrames
    );
}

void AdaptiveScheduler::beginDiscontinuityRecovery(
        const std::chrono::steady_clock::time_point now,
        const size_t generationLimit,
        const size_t fallbackGenerationLimit,
        const double baselineBaseFps,
        const std::optional<std::chrono::steady_clock::time_point> deadline,
        const bool softRecoveryAttempted,
        const std::string_view reason) {
    if (generationLimit == 0 || baselineBaseFps <= 0.0)
        return;

    const size_t configuredLimit = std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
    this->state.discontinuityRecovery.generationLimit = std::min(
        generationLimit, configuredLimit
    );
    if (this->state.discontinuityRecovery.generationLimit == 0)
        return;

    this->state.discontinuityRecovery.fallbackGenerationLimit = std::min(
        fallbackGenerationLimit,
        this->state.discontinuityRecovery.generationLimit
    );
    this->state.discontinuityRecovery.baselineBaseFps = baselineBaseFps;
    const auto minimumDeadline = now + adaptiveDiscontinuityStableDuration;
    this->state.discontinuityRecovery.deadline = deadline
        ? std::max(*deadline, minimumDeadline)
        : now + adaptiveDiscontinuityMaximumDuration;
    this->state.discontinuityRecovery.stableSince.reset();
    this->state.discontinuityRecovery.softRecoveryAttempted = softRecoveryAttempted;
    this->state.nearTargetNativePreference.reset();
    this->state.ramp.targetDeficitSince.reset();
    this->state.outputPlanner.resetTargetClock();

    const auto remainingDuration =
        *this->state.discontinuityRecovery.deadline > now
        ? *this->state.discontinuityRecovery.deadline - now
        : AdaptiveScheduler::Clock::duration::zero();
    this->diagnostics->discontinuityRecoveryStart(
        this->state.discontinuityRecovery.generationLimit,
        baselineBaseFps,
        reason,
        remainingDuration
    );
}

void AdaptiveScheduler::scheduleRearm(
        const std::chrono::steady_clock::time_point now,
        const std::string_view reason,
        const size_t fallbackLimit,
        const double baselineBaseFps) {
    const bool interrupted = reason == "probe-interrupted";
    const auto cooldown = interrupted
        ? adaptiveInterruptedProbeCooldown
        : adaptiveFailedProbeCooldown;
    if (!interrupted)
        this->state.rearm.consecutiveProbeFailures++;
    this->state.rearm.required = true;
    this->state.rearm.notBefore = now + cooldown;
    this->state.rearm.stableSince.reset();
    this->state.rearm.improvementSince.reset();
    this->state.rearm.reason = reason;
    this->state.rearm.baselineBaseFps = baselineBaseFps;
    this->state.rearm.fallbackLimit = fallbackLimit;
    this->state.ramp.targetDeficitSince.reset();
    this->state.ramp.nextAt = this->state.rearm.notBefore;
    this->diagnostics->rearm(
        "adaptive-rearm-scheduled",
        reason,
        this->state.rearm.consecutiveProbeFailures,
        this->state.rearm.fallbackLimit,
        cooldown,
        this->state.rearm.baselineBaseFps
    );
}

void AdaptiveScheduler::beginStabilization(
        const std::chrono::steady_clock::time_point now,
        const std::string_view reason) {
    const bool cadenceChange =
        reason == "cadence-stall" || reason == "cadence-drop";
    const bool hardDiscontinuity = reason == "cadence-stall";
    if (cadenceChange)
        this->state.discontinuityRecovery.stableSince.reset();
    if (hardDiscontinuity &&
            !this->state.discontinuityRecovery.deadline &&
            this->state.cadence.smoothedIntervalSeconds > 0.0) {
        size_t recoveryLimit = this->validatedGenerationLimit();
        if (this->state.stableCadence.limit) {
            recoveryLimit = std::max(
                recoveryLimit, *this->state.stableCadence.limit
            );
        }
        size_t recoveryFallbackLimit = recoveryLimit > 0
            ? recoveryLimit - 1
            : 0;
        if (this->state.strictLoad.baselineBaseFps > 0.0 &&
                this->state.strictLoad.baselineLimit < recoveryLimit) {
            recoveryFallbackLimit = this->state.strictLoad.baselineLimit;
        }
        this->beginDiscontinuityRecovery(
            now,
            recoveryLimit,
            recoveryFallbackLimit,
            1.0 / this->state.cadence.smoothedIntervalSeconds,
            std::nullopt,
            false,
            reason
        );
    }

    const bool alreadyStabilizing = this->state.stabilization.until &&
        now < *this->state.stabilization.until;
    size_t rearmFallbackLimit = 0;
    if (this->state.ramp.evaluationAt) {
        rearmFallbackLimit = this->state.ramp.bridgeActive
            ? this->state.ramp.bridgeBaselineLimit
            : this->state.ramp.previousLimit;
        const double rearmBaselineBaseFps = this->state.ramp.bridgeActive
            ? this->state.ramp.bridgeBaselineBaseFps
            : this->state.ramp.baselineBaseFps;
        this->diagnostics->probeAborted(reason, this->state.outputPlanner.generationLimit);
        this->scheduleRearm(
            now,
            "probe-interrupted",
            rearmFallbackLimit,
            rearmBaselineBaseFps
        );
    } else if (this->state.rearm.required) {
        // A fresh cadence disruption restarts the stable-cadence requirement,
        // but it does not extend the already bounded cooldown indefinitely.
        this->state.rearm.stableSince.reset();
        this->state.rearm.improvementSince.reset();
    }
    // Cold startup can include an uncapped splash screen or launcher followed
    // by normal gameplay. Do not let those first samples start a probe that
    // the gameplay transition immediately interrupts and penalizes. A Vulkan
    // oldSwapchain replacement is already inside a running game: its new
    // backend still receives the normal temporal-history warm-up, while one
    // second of fresh source cadence is enough before the measured ramp.
    const auto stabilizationDuration = reason == "startup"
        ? adaptiveRecoveryStabilizationDuration
        : adaptiveStabilizationDuration;
    this->state.stabilization.until = now + stabilizationDuration;
    this->state.ramp.nextAt = this->state.stabilization.until;
    if (this->state.rearm.notBefore &&
            *this->state.rearm.notBefore > *this->state.ramp.nextAt) {
        this->state.ramp.nextAt = this->state.rearm.notBefore;
    }
    this->state.ramp.evaluationAt.reset();
    this->state.ramp.delivery.reset();
    this->state.outputPlanner.generationLimit = 0;
    this->state.nearTargetNativePreference.reset();
    this->state.ramp.previousLimit = 0;
    this->state.ramp.baselineBaseFps = 0.0;
    this->state.ramp.bridgeActive = false;
    this->state.ramp.bridgeBaselineLimit = 0;
    this->state.ramp.bridgeBaselineBaseFps = 0.0;
    this->state.stableCadence.limit.reset();
    this->state.stableCadence.evaluationAt.reset();
    this->state.stableCadence.convergenceProbe = false;
    this->state.stableCadence.convergedTwoX = false;
    this->state.stableCadence.delivery.reset();
    this->state.stableCadence.outsideRangeSince.reset();
    this->state.stableCadence.retryAt.reset();
    this->state.stableCadence.baselineBaseFps = 0.0;
    this->state.stableCadence.candidate.reset();
    this->state.rescue.until.reset();
    this->state.rescue.previousLimit = 0;
    this->state.rescue.baselineBaseFps = 0.0;
    this->state.rescue.fromStrictLoad = false;
    this->state.rescue.strictLoadLimit = 0;
    this->state.strictLoad.baselineLimit = 0;
    this->state.strictLoad.baselineBaseFps = 0.0;
    this->state.strictLoad.collapseSince.reset();
    this->state.strictLoad.healthySince.reset();
    this->state.strictLoad.recoverySince.reset();
    this->state.strictLoad.failedLimit = 0;
    this->state.strictLoad.consecutiveFailures = 0;
    this->state.strictLoad.failedBaselineBaseFps = 0.0;
    this->state.cadence.dropFrames = 0;
    this->state.cadence.sdrStallBypass = false;
    this->state.cadence.sdrResumeFrames = 0;
    this->state.cadence.sdrIsolatedHitchBridged = false;
    this->state.diagnosticThrottle.lastPlanAt.reset();
    this->resetTiming(now);
    if (!alreadyStabilizing)
        this->diagnostics->stabilization(reason, stabilizationDuration);
}

MAKO_ADAPTIVE_STAGE_INLINE void AdaptiveScheduler::updateGenerationLimit(
        const std::chrono::steady_clock::time_point now,
        const double baseFps) {
    const size_t configuredLimit = std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
    this->state.outputPlanner.generationLimit = std::min(
        this->state.outputPlanner.generationLimit, configuredLimit
    );

    if (this->state.rearm.required) {
        // The stabilization phase itself remains real-frame-only. Once it has
        // completed, retain the last proven level while the failed higher
        // probe cools down instead of dropping frame generation altogether.
        this->state.outputPlanner.generationLimit = std::min(
            this->state.rearm.fallbackLimit, configuredLimit
        );
        if (!this->state.rearm.stableSince)
            this->state.rearm.stableSince = now;

        const bool cooldownElapsed =
            !this->state.rearm.notBefore || now >= *this->state.rearm.notBefore;
        const bool cadenceStable =
            now - *this->state.rearm.stableSince >= adaptiveStableRearmDuration;
        const bool interrupted =
            this->state.rearm.reason == "probe-interrupted";
        const bool baseImproved = !interrupted &&
            this->state.rearm.baselineBaseFps > 0.0 &&
            baseFps >= this->state.rearm.baselineBaseFps *
                adaptiveRampEarlyRetryBaseImprovement;
        if (baseImproved) {
            if (!this->state.rearm.improvementSince)
                this->state.rearm.improvementSince = now;
        } else {
            this->state.rearm.improvementSince.reset();
        }
        const bool performanceRecovered =
            this->state.rearm.improvementSince &&
            now - *this->state.rearm.improvementSince >=
                adaptiveStableRearmDuration;
        if (!cadenceStable || (!cooldownElapsed && !performanceRecovered))
            return;

        const std::string_view decision = interrupted
            ? "interruption-settled"
            : performanceRecovered && !cooldownElapsed
                ? "performance-recovered"
                : "cooldown-elapsed";
        this->diagnostics->rearm(
            "adaptive-rearm-ready",
            this->state.rearm.reason,
            this->state.rearm.consecutiveProbeFailures,
            this->state.rearm.fallbackLimit,
            AdaptiveScheduler::Clock::duration::zero(),
            this->state.rearm.baselineBaseFps,
            baseFps,
            decision
        );
        this->state.rearm.required = false;
        this->state.rearm.notBefore.reset();
        this->state.rearm.stableSince.reset();
        this->state.rearm.improvementSince.reset();
        this->state.rearm.reason.clear();
        this->state.rearm.baselineBaseFps = 0.0;
        this->state.rearm.fallbackLimit = 0;
        this->state.ramp.nextAt.reset();
    }

    // A validated constant cadence already supplies the desired smoothness.
    // Do not probe a higher generated-frame level until it becomes unsuitable.
    if (this->state.stableCadence.limit) {
        this->state.ramp.targetDeficitSince.reset();
        return;
    }

    if (this->state.ramp.evaluationAt) {
        if (now < *this->state.ramp.evaluationAt)
            return;

        const size_t testedLimit = this->state.outputPlanner.generationLimit;
        if (this->state.ramp.bridgeActive) {
            const double targetFps = static_cast<double>(this->config.targetFps);
            const double baselineOutputFps = std::min(
                targetFps,
                this->state.ramp.bridgeBaselineBaseFps *
                    static_cast<double>(this->state.ramp.bridgeBaselineLimit + 1)
            );
            const double currentOutputFps = std::min(
                targetFps,
                baseFps * static_cast<double>(testedLimit + 1)
            );
            const bool deliveryHealthy = this->state.ramp.delivery.healthy();
            const bool accepted = deliveryHealthy &&
                baseFps >= adaptiveMinimumBaseFps &&
                baseFps >= this->state.ramp.bridgeBaselineBaseFps *
                    adaptiveBridgeMinimumBaseRetention &&
                currentOutputFps >= baselineOutputFps * adaptiveRampMarginalGain;
            this->diagnostics->bridgeResult(
                accepted,
                this->state.ramp.bridgeBaselineLimit,
                testedLimit,
                this->state.ramp.bridgeBaselineBaseFps,
                baseFps,
                baselineOutputFps,
                currentOutputFps
            );

            this->state.ramp.evaluationAt.reset();
            this->state.ramp.delivery.reset();
            this->state.ramp.bridgeActive = false;
            this->state.outputPlanner.resetTargetClock();
            if (!accepted) {
                this->state.outputPlanner.generationLimit = this->state.ramp.bridgeBaselineLimit;
                this->scheduleRearm(
                    now,
                    "bridge-rejected",
                    this->state.ramp.bridgeBaselineLimit,
                    this->state.ramp.bridgeBaselineBaseFps
                );
                return;
            }

            this->state.rearm.consecutiveProbeFailures = 0;
            this->state.strictLoad.baselineLimit =
                this->state.ramp.bridgeBaselineLimit;
            this->state.strictLoad.baselineBaseFps =
                this->state.ramp.bridgeBaselineBaseFps;
            this->state.strictLoad.collapseSince.reset();
            this->state.strictLoad.healthySince.reset();
            this->state.strictLoad.recoverySince.reset();
            this->state.ramp.lastFailedLimit = 0;
            this->state.ramp.consecutiveFailures = 0;
            this->state.ramp.failedBaselineBaseFps = 0.0;
            this->state.ramp.nextAt = now + adaptiveRampStepDelay;
            if (this->config.stableCadence) {
                this->state.stableCadence.retryAt =
                    now + adaptiveStableCadenceStrictSettlingDuration;
            }
            return;
        }

        const double previousOutputFps = std::min(
            static_cast<double>(this->config.targetFps),
            this->state.ramp.baselineBaseFps *
                static_cast<double>(this->state.ramp.previousLimit + 1)
        );
        const double currentOutputFps = std::min(
            static_cast<double>(this->config.targetFps),
            baseFps * static_cast<double>(testedLimit + 1)
        );
        const bool throughputRegressed =
            currentOutputFps < previousOutputFps * adaptiveRampThroughputTolerance;
        const bool baseCollapsedForMarginalGain =
            baseFps < this->state.ramp.baselineBaseFps * adaptiveRampBaseCollapseRatio &&
            currentOutputFps < previousOutputFps * adaptiveRampMarginalGain;
        const bool deliveryHealthy = this->state.ramp.delivery.healthy();
        const bool accepted = deliveryHealthy && !throughputRegressed &&
            !baseCollapsedForMarginalGain;
        const size_t bridgeLimit = std::min(configuredLimit, testedLimit + 1);
        const bool canBridge =
            !accepted &&
            this->state.ramp.previousLimit == 0 &&
            testedLimit == 1 &&
            bridgeLimit >= 2 &&
            baseFps >= adaptiveMinimumBaseFps &&
            currentOutputFps >=
                previousOutputFps * adaptiveBridgeMinimumOutputRetention &&
            previousOutputFps <
                static_cast<double>(this->config.targetFps) *
                    adaptiveBridgeTargetDeficitRatio;
        if (canBridge) {
            this->diagnostics->bridge(
                this->state.ramp.previousLimit,
                testedLimit,
                bridgeLimit,
                this->state.ramp.baselineBaseFps,
                baseFps,
                previousOutputFps,
                currentOutputFps
            );
            this->state.ramp.bridgeActive = true;
            this->state.ramp.bridgeBaselineLimit = this->state.ramp.previousLimit;
            this->state.ramp.bridgeBaselineBaseFps =
                this->state.ramp.baselineBaseFps;
            this->state.outputPlanner.generationLimit = bridgeLimit;
            this->state.ramp.evaluationAt = now + adaptiveRampEvaluationDuration;
            this->state.ramp.delivery.reset();
            this->state.outputPlanner.resetTargetClock();
            return;
        }

        this->diagnostics->rampResult(
            accepted,
            this->state.ramp.previousLimit,
            testedLimit,
            this->state.ramp.baselineBaseFps,
            baseFps,
            previousOutputFps,
            currentOutputFps
        );

        this->state.ramp.evaluationAt.reset();
        this->state.ramp.delivery.reset();
        this->state.outputPlanner.resetTargetClock();
        if (!accepted) {
            this->state.outputPlanner.generationLimit = this->state.ramp.previousLimit;
            if (this->state.ramp.previousLimit == 0) {
                this->scheduleRearm(
                    now,
                    "ramp-rejected",
                    this->state.ramp.previousLimit,
                    this->state.ramp.baselineBaseFps
                );
            } else {
                if (this->state.ramp.lastFailedLimit != testedLimit) {
                    this->state.ramp.lastFailedLimit = testedLimit;
                    this->state.ramp.consecutiveFailures = 0;
                }
                this->state.ramp.consecutiveFailures++;
                this->state.ramp.failedBaselineBaseFps =
                    this->state.ramp.baselineBaseFps;
                const auto retryDelay = adaptiveRampRetryDelayForFailures(
                    this->state.ramp.consecutiveFailures
                );
                this->state.ramp.nextAt = now + retryDelay;
                this->diagnostics->rampBackoff(
                    testedLimit,
                    this->state.ramp.consecutiveFailures,
                    this->state.ramp.failedBaselineBaseFps,
                    retryDelay
                );
            }
            return;
        }
        this->state.rearm.consecutiveProbeFailures = 0;
        this->state.strictLoad.baselineLimit =
            this->state.ramp.previousLimit;
        this->state.strictLoad.baselineBaseFps =
            this->state.ramp.baselineBaseFps;
        this->state.strictLoad.collapseSince.reset();
        this->state.strictLoad.healthySince.reset();
        this->state.strictLoad.recoverySince.reset();
        this->state.ramp.lastFailedLimit = 0;
        this->state.ramp.consecutiveFailures = 0;
        this->state.ramp.failedBaselineBaseFps = 0.0;
        this->state.ramp.nextAt = now + adaptiveRampStepDelay;
        if (this->config.stableCadence) {
            this->state.stableCadence.retryAt =
                now + adaptiveStableCadenceStrictSettlingDuration;
        }
    }

    if (this->advanceNearTargetNativePreference(
            now, baseFps, configuredLimit)) {
        this->state.ramp.targetDeficitSince.reset();
        return;
    }

    // Once the current proven ceiling can already supply the requested target,
    // a higher multiplier adds inference load without useful output. Keep the
    // lower level and reconsider automatically if the measured base rate later
    // falls far enough that this capacity is no longer sufficient.
    const double validatedOutputFps = baseFps *
        static_cast<double>(this->state.outputPlanner.generationLimit + 1);
    const bool targetSatisfied = validatedOutputFps >=
        static_cast<double>(this->config.targetFps) *
            adaptiveRampTargetSatisfiedRatio;
    if (targetSatisfied) {
        this->state.ramp.targetDeficitSince.reset();
        this->state.strictLoad.recoverySince.reset();
        return;
    }

    if (this->state.outputPlanner.generationLimit >= configuredLimit) {
        this->state.ramp.targetDeficitSince.reset();
        return;
    }
    if (!this->state.ramp.targetDeficitSince) {
        this->state.ramp.targetDeficitSince = now;
        return;
    }
    if (now - *this->state.ramp.targetDeficitSince <
            adaptiveTargetDeficitDuration)
        return;

    if (this->state.ramp.nextAt && now < *this->state.ramp.nextAt) {
        const size_t nextLimit = this->state.outputPlanner.generationLimit + 1;
        const bool failedRampRecovered =
            this->state.ramp.consecutiveFailures > 0 &&
            this->state.ramp.lastFailedLimit == nextLimit &&
            this->state.ramp.failedBaselineBaseFps > 0.0 &&
            baseFps >= this->state.ramp.failedBaselineBaseFps *
                adaptiveRampEarlyRetryBaseImprovement;
        const bool failedStrictLoadRecovered =
            this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr &&
            this->state.strictLoad.failedLimit == nextLimit &&
            this->state.strictLoad.consecutiveFailures > 0 &&
            this->state.strictLoad.failedBaselineBaseFps > 0.0 &&
            baseFps >= this->state.strictLoad.failedBaselineBaseFps *
                adaptiveRampEarlyRetryBaseImprovement;
        if (failedStrictLoadRecovered) {
            if (!this->state.strictLoad.recoverySince)
                this->state.strictLoad.recoverySince = now;
        } else {
            this->state.strictLoad.recoverySince.reset();
        }
        const bool strictLoadRecoveryConfirmed =
            this->state.strictLoad.recoverySince &&
            now - *this->state.strictLoad.recoverySince >=
                adaptiveStrictLoadHealthyDuration;
        if (!failedRampRecovered && !strictLoadRecoveryConfirmed)
            return;

        this->diagnostics->rampEarlyRetry(
            nextLimit,
            strictLoadRecoveryConfirmed
                ? this->state.strictLoad.failedBaselineBaseFps
                : this->state.ramp.failedBaselineBaseFps,
            baseFps
        );
        this->state.ramp.nextAt.reset();
        if (strictLoadRecoveryConfirmed)
            this->state.rescue.cooldownUntil.reset();
        this->state.strictLoad.recoverySince.reset();
    }

    this->state.ramp.previousLimit = this->state.outputPlanner.generationLimit;
    this->state.ramp.baselineBaseFps = baseFps;
    this->state.outputPlanner.generationLimit++;
    this->state.ramp.evaluationAt = now + adaptiveRampEvaluationDuration;
    this->state.ramp.delivery.reset();
    this->state.ramp.targetDeficitSince.reset();
    this->state.outputPlanner.resetTargetClock();
    this->diagnostics->ramp(
        this->state.ramp.previousLimit,
        this->state.outputPlanner.generationLimit,
        baseFps
    );
}

#undef MAKO_ADAPTIVE_STAGE_INLINE
