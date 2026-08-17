/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "adaptive_scheduler.hpp"
#include "presentation_policy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

using namespace mako::layer;

namespace {
    constexpr size_t adaptiveHistoryWarmupFrames = 3;
    constexpr size_t adaptiveSdrCadenceRefreshFrames = 2;
    constexpr double adaptiveMinimumBaseFps = 10.0;
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
    constexpr double adaptiveBridgeMinimumOutputRetention = 0.85;
    constexpr double adaptiveBridgeMinimumBaseRetention = 0.40;
    constexpr double adaptiveBridgeTargetDeficitRatio = 0.90;
    constexpr double adaptiveStableCadenceMaximumProbeOvershootRatio = 1.40;
    constexpr double adaptiveStableCadenceMaximumRetainedOvershootRatio = 1.50;
    constexpr double adaptiveStableCadenceMinimumTargetRatio = 0.98;
    constexpr double adaptiveStableCadenceMinimumBaseRetention = 0.74;
    constexpr double adaptiveStableCadenceMinimumDemandRatio = 0.95;
    constexpr auto adaptiveStabilizationDuration = std::chrono::seconds(1);
    constexpr auto adaptiveRecoveryStabilizationDuration = std::chrono::seconds(3);
    constexpr auto adaptiveRampEvaluationDuration = std::chrono::seconds(1);
    constexpr auto adaptiveTargetDeficitDuration = std::chrono::seconds(1);
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
    constexpr auto adaptiveStableCadenceStrictSettlingDuration = std::chrono::seconds(2);
    constexpr auto adaptiveStableCadenceCandidateDuration = std::chrono::seconds(2);
    constexpr double adaptiveStableCadenceMaximumCandidateSpreadRatio = 1.15;
    constexpr double adaptiveRescueBaseCollapseRatio = 0.78;
    constexpr double adaptiveRescueOutputCollapseRatio = 0.80;
    constexpr double adaptiveRescueRecoveredBaseRatio = 0.90;
    constexpr auto adaptiveRescueMeasurementDuration = std::chrono::seconds(1);
    constexpr auto adaptiveRescueCooldown = std::chrono::seconds(15);
    constexpr auto adaptiveStrictLoadCollapseDuration = std::chrono::seconds(1);
    constexpr double adaptiveDiscontinuityRecoveredBaseRatio = 0.90;
    constexpr auto adaptiveDiscontinuityStableDuration = std::chrono::seconds(1);
    constexpr auto adaptiveDiscontinuityMaximumDuration = std::chrono::seconds(5);
    constexpr auto adaptiveTwoXGameplayHitchMaximumDuration =
        std::chrono::milliseconds(250);
    constexpr auto adaptiveFailedProbeCooldown = std::chrono::seconds(15);
    constexpr auto adaptiveInterruptedProbeCooldown = std::chrono::seconds(2);
    constexpr auto adaptiveStableRearmDuration = std::chrono::seconds(2);
    constexpr auto adaptiveFastBurstDiagnosticInterval = std::chrono::seconds(1);

    AdaptiveSchedulerDiagnostics nullDiagnostics;

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
        diagnosticsActive(this->diagnostics->enabled()),
        adaptiveHistoryWarmupRemaining(adaptiveHistoryWarmupFrames) {
    if (this->config.targetFps < 10 || this->config.targetFps > 1000)
        throw std::invalid_argument("Adaptive target FPS must be between 10 and 1000");
    if (this->config.maximumMultiplier < 2 ||
            this->config.maximumMultiplier > 4) {
        throw std::invalid_argument("Adaptive maximum multiplier must be between 2 and 4");
    }
    if (this->config.generatedFrameCapacity > 3) {
        throw std::invalid_argument("Adaptive generated-frame capacity cannot exceed 3");
    }
}

void AdaptiveScheduler::beginHistoryWarmup(const size_t frames,
        const bool recovery) {
    this->adaptiveHistoryWarmupRemaining = frames;
    this->adaptiveHistoryWarmupIsRecovery = recovery && frames > 0;
}

void AdaptiveScheduler::ensureHistoryWarmup(const size_t frames,
        const bool recovery) {
    if (this->adaptiveHistoryWarmupRemaining > 0)
        return;
    this->beginHistoryWarmup(frames, recovery);
}

void AdaptiveScheduler::cancelHistoryWarmup() {
    this->adaptiveHistoryWarmupRemaining = 0;
    this->adaptiveHistoryWarmupIsRecovery = false;
}

void AdaptiveScheduler::consumeHistoryWarmupFrame(const TimePoint now) {
    if (this->adaptiveHistoryWarmupRemaining == 0)
        return;

    this->adaptiveHistoryWarmupRemaining--;
    if (this->adaptiveHistoryWarmupRemaining == 0)
        this->adaptiveHistoryWarmupIsRecovery = false;
    this->resetTiming(now);
}

void AdaptiveScheduler::reportGeneratedFrameDelivery(
        const size_t planned, const size_t onTime) {
    if (planned == 0)
        return;
    const size_t delivered = std::min(planned, onTime);
    if (this->adaptiveRampEvaluationAt) {
        this->adaptiveRampPlannedGeneratedFrames += planned;
        this->adaptiveRampOnTimeGeneratedFrames += delivered;
    }
    if (this->adaptiveStableCadenceEvaluationAt) {
        this->adaptiveStableCadencePlannedGeneratedFrames += planned;
        this->adaptiveStableCadenceOnTimeGeneratedFrames += delivered;
    }
}

AdaptiveSchedulerSnapshot AdaptiveScheduler::snapshot() const {
    AdaptiveSchedulerPhase phase = AdaptiveSchedulerPhase::Active;
    if (this->adaptiveHistoryWarmupRemaining > 0) {
        phase = AdaptiveSchedulerPhase::HistoryWarmup;
    } else if (!this->adaptiveLastRealFrame) {
        phase = AdaptiveSchedulerPhase::Uninitialized;
    } else if (this->adaptiveDiscontinuityRecoveryDeadline) {
        phase = AdaptiveSchedulerPhase::DiscontinuityRecovery;
    } else if (this->adaptiveRescueUntil) {
        phase = AdaptiveSchedulerPhase::RescueMeasurement;
    } else if (this->adaptiveStabilizationUntil) {
        phase = AdaptiveSchedulerPhase::Stabilizing;
    } else if (this->adaptiveRearmRequired) {
        phase = AdaptiveSchedulerPhase::RearmCooldown;
    } else if (this->adaptiveRampEvaluationAt) {
        phase = AdaptiveSchedulerPhase::RampEvaluation;
    } else if (this->adaptiveStableCadenceLimit) {
        phase = AdaptiveSchedulerPhase::StableCadence;
    }

    return {
        .phase = phase,
        .generationLimit = this->adaptiveGenerationLimit,
        .validatedGenerationLimit = this->validatedGenerationLimit(),
        .stableCadenceLimit = this->adaptiveStableCadenceLimit,
        .historyWarmupRemaining = this->adaptiveHistoryWarmupRemaining,
        .smoothedBaseFps = this->adaptiveSmoothedIntervalSeconds > 0.0
            ? 1.0 / this->adaptiveSmoothedIntervalSeconds
            : 0.0,
        .rampEvaluationActive = this->adaptiveRampEvaluationAt.has_value(),
        .rearmRequired = this->adaptiveRearmRequired,
        .discontinuityRecoveryActive =
            this->adaptiveDiscontinuityRecoveryDeadline.has_value(),
    };
}

size_t AdaptiveScheduler::configuredGenerationLimit() const {
    if (this->config.maximumMultiplier < 2)
        return 0;
    return std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
}

AdaptiveFramePlan AdaptiveScheduler::planFrame(
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
        this->adaptiveLastRealFrame = now;
        AdaptiveFramePlan plan;
        plan.values.front() = 0.5F;
        plan.count = 1;
        return plan;
    }

    if (!this->adaptiveLastRealFrame) {
        this->adaptiveLastRealFrame = now;
        return {};
    }

    const auto rawInterval = now - *this->adaptiveLastRealFrame;
    const double rawIntervalSeconds = std::chrono::duration<double>(
        rawInterval
    ).count();
    this->adaptiveLastRealFrame = now;

    const auto finishFastCadenceBurst = [&] {
        if (!this->adaptiveFastBurstStartedAt)
            return;

        this->diagnostics->fastCadenceBurstComplete(
            this->adaptiveFastBurstFrames,
            now - *this->adaptiveFastBurstStartedAt
        );
        this->adaptiveFastBurstStartedAt.reset();
        this->adaptiveLastFastBurstDiagnostic.reset();
        this->adaptiveFastBurstFrames = 0;
        this->adaptiveFastBurstFramesSinceDiagnostic = 0;
    };

    // Loading screens, suspension and base rates below 10 FPS do not provide
    // useful motion history. Present real frames until cadence has been stable
    // for a bounded interval instead of immediately reapplying model load.
    if (rawIntervalSeconds <= 0.0) {
        finishFastCadenceBurst();
        if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
            this->beginCadenceRefresh(now, "cadence-stall");
            return {};
        }
        this->beginStabilization(now, "cadence-stall");
        return {};
    }

    const double baselineBaseFps = this->adaptiveSmoothedIntervalSeconds > 0.0
        ? 1.0 / this->adaptiveSmoothedIntervalSeconds
        : 0.0;
    const double instantaneousBaseFps = 1.0 / rawIntervalSeconds;
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
        if (!this->adaptiveFastBurstStartedAt)
            this->adaptiveFastBurstStartedAt = now;
        this->adaptiveFastBurstFrames++;
        this->adaptiveFastBurstFramesSinceDiagnostic++;

        const auto pauseEvaluation = [&rawInterval](auto& deadline) {
            if (deadline)
                *deadline += rawInterval;
        };
        pauseEvaluation(this->adaptiveStabilizationUntil);
        pauseEvaluation(this->adaptiveRampEvaluationAt);
        pauseEvaluation(this->adaptiveStableCadenceEvaluationAt);
        pauseEvaluation(this->adaptiveRescueUntil);

        this->adaptiveOutputCredit = 0.0;
        this->adaptiveCadenceDropFrames = 0;
        this->adaptiveTargetDeficitSince.reset();
        this->adaptiveStableRearmSince.reset();
        this->adaptiveRearmImprovementSince.reset();
        this->adaptiveStrictLoadCollapseSince.reset();
        this->adaptiveStableCadenceOutsideRangeSince.reset();
        this->adaptiveDiscontinuityStableSince.reset();

        if (!this->adaptiveLastFastBurstDiagnostic ||
                now - *this->adaptiveLastFastBurstDiagnostic >=
                    adaptiveFastBurstDiagnosticInterval) {
            this->diagnostics->fastCadenceBurst(
                baselineBaseFps,
                instantaneousBaseFps,
                fastBurstThresholdFps,
                this->adaptiveFastBurstFramesSinceDiagnostic,
                this->adaptiveFastBurstFrames,
                now - *this->adaptiveFastBurstStartedAt
            );
            this->adaptiveLastFastBurstDiagnostic = now;
            this->adaptiveFastBurstFramesSinceDiagnostic = 0;
        }
        return {};
    }
    finishFastCadenceBurst();

    if (rawIntervalSeconds > 1.0 / adaptiveMinimumBaseFps) {
        if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
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
            this->adaptiveHistoryWarmupRemaining =
                adaptiveHistoryWarmupFrames;
            this->adaptiveHistoryWarmupIsRecovery = true;
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

    // A sustained interval jump can be a menu/focus transition, but it can
    // also be a legitimate heavier gameplay scene. Three samples avoid
    // treating an isolated hitch as a cadence change. Unlike a hard stall,
    // this path performs only the ordinary one-second stabilization and then
    // rebases Adaptive at the new measured rate.
    const bool cadenceDropCandidate =
        this->adaptiveSmoothedIntervalSeconds > 0.0 &&
            rawIntervalSeconds >=
                this->adaptiveSmoothedIntervalSeconds * adaptiveCadenceDropRatio;
    if (cadenceDropCandidate) {
        this->adaptiveStableRearmSince.reset();
        this->adaptiveRearmImprovementSince.reset();
        this->adaptiveCadenceDropFrames++;
    } else {
        this->adaptiveCadenceDropFrames = 0;
    }
    if (this->adaptiveCadenceDropFrames >= adaptiveCadenceDropFrameCount) {
        if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
            this->beginCadenceRefresh(now, "cadence-drop");
            return {};
        }
        this->beginStabilization(now, "cadence-drop");
        return {};
    }

    if (this->adaptiveSmoothedIntervalSeconds == 0.0) {
        this->adaptiveSmoothedIntervalSeconds = rawIntervalSeconds;
    } else if (!cadenceDropCandidate) {
        // Keep the pre-disruption baseline while confirming a sustained drop.
        // Otherwise smoothing the first slow samples raises the comparison
        // threshold and can hide the third confirming frame.
        this->adaptiveSmoothedIntervalSeconds =
            (1.0 - adaptiveIntervalSmoothing) * this->adaptiveSmoothedIntervalSeconds +
            adaptiveIntervalSmoothing * rawIntervalSeconds;
    }

    const double baseFps = 1.0 / this->adaptiveSmoothedIntervalSeconds;
    if (this->adaptiveDiscontinuityRecoveryDeadline) {
        const size_t recoveryLimit = this->adaptiveDiscontinuityGenerationLimit;
        const size_t recoveryFallbackLimit =
            this->adaptiveDiscontinuityFallbackGenerationLimit;
        const double recoveryBaselineBaseFps =
            this->adaptiveDiscontinuityBaselineBaseFps;
        const bool initialStabilizationComplete =
            !this->adaptiveStabilizationUntil ||
            now >= *this->adaptiveStabilizationUntil;
        const bool baseRecovered = recoveryBaselineBaseFps > 0.0 &&
            baseFps >= recoveryBaselineBaseFps *
                adaptiveDiscontinuityRecoveredBaseRatio;

        if (baseRecovered) {
            if (!this->adaptiveDiscontinuityStableSince)
                this->adaptiveDiscontinuityStableSince = now;
        } else {
            this->adaptiveDiscontinuityStableSince.reset();
        }

        const bool recoveredCadenceStable = initialStabilizationComplete &&
            this->adaptiveDiscontinuityStableSince &&
            now - *this->adaptiveDiscontinuityStableSince >=
                adaptiveDiscontinuityStableDuration;
        const bool recoveryExpired =
            now >= *this->adaptiveDiscontinuityRecoveryDeadline;

        if (recoveredCadenceStable || recoveryExpired) {
            const std::string_view decision = recoveredCadenceStable
                ? "restore-validated-level"
                : "timeout-ramp-from-zero";
            this->adaptiveDiscontinuityRecoveryDeadline.reset();
            this->adaptiveDiscontinuityStableSince.reset();
            this->adaptiveDiscontinuityGenerationLimit = 0;
            this->adaptiveDiscontinuityFallbackGenerationLimit = 0;
            this->adaptiveDiscontinuityBaselineBaseFps = 0.0;
            this->adaptiveDiscontinuitySoftRecoveryAttempted = false;
            this->adaptiveStabilizationUntil.reset();
            this->adaptiveOutputCredit = 0.0;
            if (recoveredCadenceStable) {
                this->restoreGenerationLimit(
                    now,
                    recoveryLimit,
                    "cadence-discontinuity",
                    recoveryFallbackLimit,
                    baseFps
                );
                if (this->config.stableCadence) {
                    this->adaptiveStableCadenceRetryAt =
                        now + adaptiveStableCadenceStrictSettlingDuration;
                }
            } else {
                this->adaptiveGenerationLimit = 0;
                this->adaptiveRampEvaluationAt.reset();
                this->adaptiveTargetDeficitSince.reset();
                this->adaptiveBridgeActive = false;
                this->adaptiveBridgeBaselineLimit = 0;
                this->adaptiveBridgeBaselineBaseFps = 0.0;
                this->adaptiveRearmRequired = false;
                this->adaptiveRearmNotBefore.reset();
                this->adaptiveStableRearmSince.reset();
                this->adaptiveRearmImprovementSince.reset();
                this->adaptiveRearmReason.clear();
                this->adaptiveRearmBaselineBaseFps = 0.0;
                this->adaptiveRearmFallbackLimit = 0;
                this->adaptiveConsecutiveProbeFailures = 0;
                this->adaptiveLastFailedRampLimit = 0;
                this->adaptiveConsecutiveRampFailures = 0;
                this->adaptiveFailedRampBaselineBaseFps = 0.0;
                this->adaptiveNextRampAt = now + adaptiveRampStepDelay;
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

        this->adaptiveOutputCredit = 0.0;
        if (this->diagnosticsActive &&
                (!this->adaptiveLastDiagnostic ||
                 now - *this->adaptiveLastDiagnostic >= std::chrono::seconds(1))) {
            this->adaptiveLastDiagnostic = now;
            AdaptivePlanDiagnostic plan;
            plan.baseFps = baseFps;
            plan.targetFps = this->config.targetFps;
            plan.phase = "discontinuity-recovery";
            plan.recoveryGenerationLimit = recoveryLimit;
            this->diagnostics->plan(plan);
        }
        return {};
    }

    if (this->adaptiveRescueUntil) {
        if (now < *this->adaptiveRescueUntil) {
            this->adaptiveOutputCredit = 0.0;
            if (this->diagnosticsActive &&
                    (!this->adaptiveLastDiagnostic ||
                     now - *this->adaptiveLastDiagnostic >= std::chrono::seconds(1))) {
                this->adaptiveLastDiagnostic = now;
                AdaptivePlanDiagnostic plan;
                plan.baseFps = baseFps;
                plan.targetFps = this->config.targetFps;
                plan.phase = this->adaptiveRescueFromStrictLoad
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
            this->adaptiveRescuePreviousLimit, configuredLimit
        );
        const double rescueBaselineBaseFps =
            this->adaptiveRescueBaselineBaseFps;
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
        const bool strictLoadRescue = this->adaptiveRescueFromStrictLoad;
        const size_t strictLoadLimit = std::min(
            this->adaptiveRescueStrictLoadLimit, configuredLimit
        );

        std::string_view decision = "resume-strict";
        this->adaptiveGenerationLimit = previousLimit;
        this->adaptiveRampEvaluationAt.reset();
        this->adaptiveBridgeActive = false;
        this->adaptiveBridgeBaselineLimit = 0;
        this->adaptiveBridgeBaselineBaseFps = 0.0;
        this->adaptiveNextRampAt.reset();
        if (strictLoadRescue) {
            // A real-only measurement distinguishes inference pressure from a
            // genuinely heavier game scene. Restore the lower proven level
            // only when cadence recovers without generated-frame work;
            // otherwise retain the higher level that the scene still needs.
            this->adaptiveNextRampAt = this->adaptiveRescueCooldownUntil;
            if (baseRecovered) {
                decision = "strict-load-restored";
            } else {
                this->adaptiveGenerationLimit = std::max(
                    previousLimit, strictLoadLimit
                );
                decision = "strict-load-retained";
            }
        } else if (baseRecovered) {
            decision = "recovered-strict";
        } else if (requestedLimit > previousLimit) {
            // updateGenerationLimit() below will probe only the next
            // allowed level and apply its existing throughput checks.
            this->adaptiveNextRampAt = now;
            decision = "probe-higher-limit";
        } else if (requiredLimit > configuredLimit) {
            decision = "ceiling-limited";
        }

        this->adaptiveRescueUntil.reset();
        this->adaptiveRescuePreviousLimit = 0;
        this->adaptiveRescueBaselineBaseFps = 0.0;
        this->adaptiveRescueFromStrictLoad = false;
        this->adaptiveRescueStrictLoadLimit = 0;
        this->adaptiveTargetDeficitSince.reset();
        this->adaptiveOutputCredit = 0.0;
        if (this->adaptiveRescueCooldownUntil)
            this->adaptiveStableCadenceRetryAt = this->adaptiveRescueCooldownUntil;
        this->diagnostics->rescueComplete(
            previousLimit,
            this->adaptiveGenerationLimit,
            requestedLimit,
            configuredLimit,
            rescueBaselineBaseFps,
            baseFps,
            decision
        );
    }

    if (this->adaptiveStabilizationUntil &&
            now < *this->adaptiveStabilizationUntil) {
        this->adaptiveOutputCredit = 0.0;
        if (this->diagnosticsActive &&
                (!this->adaptiveLastDiagnostic ||
                 now - *this->adaptiveLastDiagnostic >= std::chrono::seconds(1))) {
            this->adaptiveLastDiagnostic = now;
            AdaptivePlanDiagnostic plan;
            plan.baseFps = baseFps;
            plan.targetFps = this->config.targetFps;
            plan.phase = "stabilizing";
            this->diagnostics->plan(plan);
        }
        return {};
    }
    this->adaptiveStabilizationUntil.reset();
    this->updateGenerationLimit(now, baseFps);

    const double desiredOutputsPerRealFrame =
        this->adaptiveSmoothedIntervalSeconds *
        static_cast<double>(this->config.targetFps);

    const size_t maximumGeneratedFrameCount = std::min(
        {
            this->config.generatedFrameCapacity,
            this->config.maximumMultiplier - 1,
            this->adaptiveGenerationLimit,
        }
    );

    const auto stableCadenceCandidate = [&]() -> std::optional<size_t> {
        if (!this->config.stableCadence)
            return std::nullopt;
        if (desiredOutputsPerRealFrame <= 1.0)
            return std::nullopt;

        const double minimumUsefulOutputFps =
            static_cast<double>(this->config.targetFps) *
                adaptiveStableCadenceMinimumTargetRatio;
        const size_t candidateOutputs = static_cast<size_t>(std::ceil(
            minimumUsefulOutputFps / baseFps - 1e-9
        ));
        if (candidateOutputs <= 1)
            return std::nullopt;
        if (desiredOutputsPerRealFrame /
                static_cast<double>(candidateOutputs) <
                    adaptiveStableCadenceMinimumDemandRatio)
            return std::nullopt;

        const size_t candidateGenerated = candidateOutputs - 1;
        if (candidateGenerated > maximumGeneratedFrameCount)
            return std::nullopt;

        const double projectedOutputFps = baseFps *
            static_cast<double>(candidateOutputs);
        if (projectedOutputFps >
                static_cast<double>(this->config.targetFps) *
                    adaptiveStableCadenceMaximumProbeOvershootRatio)
            return std::nullopt;

        return candidateGenerated;
    }();

    if (this->adaptiveStableCadenceLimit) {
        const size_t generatedLimit = *this->adaptiveStableCadenceLimit;
        const double targetFps = static_cast<double>(this->config.targetFps);
        const double projectedOutputFps = baseFps *
            static_cast<double>(generatedLimit + 1);
        const double cadenceDemandRatio =
            desiredOutputsPerRealFrame /
            static_cast<double>(generatedLimit + 1);
        const bool capacityAvailable =
            generatedLimit <= maximumGeneratedFrameCount;
        const bool cadenceStillUseful =
            desiredOutputsPerRealFrame > 1.0 &&
            cadenceDemandRatio >= adaptiveStableCadenceMinimumDemandRatio &&
            projectedOutputFps >=
                targetFps * adaptiveStableCadenceMinimumTargetRatio &&
            projectedOutputFps <=
                targetFps * adaptiveStableCadenceMaximumRetainedOvershootRatio;

        if (!capacityAvailable) {
            this->diagnostics->stableCadence(
                "adaptive-stable-cadence-disabled",
                generatedLimit,
                this->adaptiveStableCadenceBaselineBaseFps,
                baseFps,
                "capacity-changed"
            );
            this->adaptiveStableCadenceLimit.reset();
            this->adaptiveStableCadenceEvaluationAt.reset();
            this->adaptiveStableCadencePlannedGeneratedFrames = 0;
            this->adaptiveStableCadenceOnTimeGeneratedFrames = 0;
            this->adaptiveStableCadenceOutsideRangeSince.reset();
            this->adaptiveStableCadenceRetryAt =
                now + adaptiveStableCadenceRetryDelay;
        } else if (!cadenceStillUseful) {
            if (!this->adaptiveStableCadenceOutsideRangeSince)
                this->adaptiveStableCadenceOutsideRangeSince = now;
            if (now - *this->adaptiveStableCadenceOutsideRangeSince >=
                    adaptiveStableCadenceExitGraceDuration) {
                const bool rescueCooldownElapsed =
                    !this->adaptiveRescueCooldownUntil ||
                    now >= *this->adaptiveRescueCooldownUntil;
                const bool severeCollapse =
                    rescueCooldownElapsed &&
                    this->adaptiveStableCadenceBaselineBaseFps > 0.0 &&
                    baseFps <= this->adaptiveStableCadenceBaselineBaseFps *
                        adaptiveRescueBaseCollapseRatio &&
                    projectedOutputFps <= targetFps *
                        adaptiveRescueOutputCollapseRatio;
                this->diagnostics->stableCadence(
                    "adaptive-stable-cadence-disabled",
                    generatedLimit,
                    this->adaptiveStableCadenceBaselineBaseFps,
                    baseFps,
                    severeCollapse ? "collapse-rescue" : "outside-useful-range"
                );
                const double rescueBaselineBaseFps =
                    this->adaptiveStableCadenceBaselineBaseFps;
                this->adaptiveStableCadenceLimit.reset();
                this->adaptiveStableCadenceEvaluationAt.reset();
                this->adaptiveStableCadencePlannedGeneratedFrames = 0;
                this->adaptiveStableCadenceOnTimeGeneratedFrames = 0;
                this->adaptiveStableCadenceOutsideRangeSince.reset();
                this->adaptiveStableCadenceRetryAt =
                    now + adaptiveStableCadenceRetryDelay;
                if (severeCollapse) {
                    this->adaptiveRescuePreviousLimit = generatedLimit;
                    this->adaptiveRescueBaselineBaseFps = rescueBaselineBaseFps;
                    this->adaptiveRescueUntil =
                        now + adaptiveRescueMeasurementDuration;
                    this->adaptiveRescueCooldownUntil =
                        now + adaptiveRescueCooldown;
                    this->adaptiveTargetDeficitSince.reset();
                    this->adaptiveOutputCredit = 0.0;
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
            this->adaptiveStableCadenceOutsideRangeSince.reset();
        }

        if (this->adaptiveStableCadenceLimit &&
                this->adaptiveStableCadenceEvaluationAt &&
                now >= *this->adaptiveStableCadenceEvaluationAt) {
            const size_t evaluatedGeneratedLimit =
                *this->adaptiveStableCadenceLimit;
            const double evaluatedProjectedOutputFps = baseFps *
                static_cast<double>(evaluatedGeneratedLimit + 1);
            const double evaluatedDemandRatio =
                desiredOutputsPerRealFrame /
                static_cast<double>(evaluatedGeneratedLimit + 1);
            const bool deliveryHealthy = generatedDeliveryHealthy(
                this->adaptiveStableCadencePlannedGeneratedFrames,
                this->adaptiveStableCadenceOnTimeGeneratedFrames
            );
            const bool accepted = deliveryHealthy &&
                evaluatedDemandRatio >=
                    adaptiveStableCadenceMinimumDemandRatio &&
                evaluatedProjectedOutputFps >=
                    targetFps * adaptiveStableCadenceMinimumTargetRatio &&
                evaluatedProjectedOutputFps <=
                    targetFps *
                        adaptiveStableCadenceMaximumRetainedOvershootRatio &&
                baseFps >= this->adaptiveStableCadenceBaselineBaseFps *
                    adaptiveStableCadenceMinimumBaseRetention;
            this->diagnostics->stableCadence(
                accepted ? "adaptive-stable-cadence-accepted"
                         : "adaptive-stable-cadence-rejected",
                evaluatedGeneratedLimit,
                this->adaptiveStableCadenceBaselineBaseFps,
                baseFps
            );
            this->adaptiveStableCadenceEvaluationAt.reset();
            this->adaptiveStableCadencePlannedGeneratedFrames = 0;
            this->adaptiveStableCadenceOnTimeGeneratedFrames = 0;
            if (!accepted) {
                this->adaptiveStableCadenceLimit.reset();
                this->adaptiveStableCadenceOutsideRangeSince.reset();
                this->adaptiveStableCadenceRetryAt =
                    now + adaptiveStableCadenceRetryDelay;
            }
        }
    }

    const bool stableCadenceProbePermitted =
        !this->adaptiveStableCadenceLimit && stableCadenceCandidate &&
            !this->adaptiveRampEvaluationAt &&
            !this->adaptiveRearmRequired &&
            (!this->adaptiveRescueCooldownUntil ||
             now >= *this->adaptiveRescueCooldownUntil) &&
            (!this->adaptiveStableCadenceRetryAt ||
             now >= *this->adaptiveStableCadenceRetryAt);
    bool stableCadenceCandidateQualified = false;
    if (!stableCadenceProbePermitted) {
        this->adaptiveStableCadenceCandidateLimit.reset();
        this->adaptiveStableCadenceCandidateSince.reset();
        this->adaptiveStableCadenceCandidateMinimumBaseFps = 0.0;
        this->adaptiveStableCadenceCandidateMaximumBaseFps = 0.0;
    } else if (this->adaptiveStableCadenceCandidateLimit !=
            stableCadenceCandidate) {
        this->adaptiveStableCadenceCandidateLimit = stableCadenceCandidate;
        this->adaptiveStableCadenceCandidateSince = now;
        this->adaptiveStableCadenceCandidateMinimumBaseFps = baseFps;
        this->adaptiveStableCadenceCandidateMaximumBaseFps = baseFps;
    } else {
        this->adaptiveStableCadenceCandidateMinimumBaseFps = std::min(
            this->adaptiveStableCadenceCandidateMinimumBaseFps, baseFps
        );
        this->adaptiveStableCadenceCandidateMaximumBaseFps = std::max(
            this->adaptiveStableCadenceCandidateMaximumBaseFps, baseFps
        );
        const bool spreadTooWide =
            this->adaptiveStableCadenceCandidateMinimumBaseFps > 0.0 &&
            this->adaptiveStableCadenceCandidateMaximumBaseFps >
                this->adaptiveStableCadenceCandidateMinimumBaseFps *
                    adaptiveStableCadenceMaximumCandidateSpreadRatio;
        if (spreadTooWide) {
            // Begin a fresh qualification window at the new cadence instead
            // of allowing old low/high samples to trigger a workload switch.
            this->adaptiveStableCadenceCandidateSince = now;
            this->adaptiveStableCadenceCandidateMinimumBaseFps = baseFps;
            this->adaptiveStableCadenceCandidateMaximumBaseFps = baseFps;
        } else {
            stableCadenceCandidateQualified =
                this->adaptiveStableCadenceCandidateSince &&
                now - *this->adaptiveStableCadenceCandidateSince >=
                    adaptiveStableCadenceCandidateDuration;
        }
    }

    if (stableCadenceCandidateQualified) {
        this->adaptiveStableCadenceLimit =
            this->adaptiveStableCadenceCandidateLimit;
        this->adaptiveStableCadenceBaselineBaseFps = baseFps;
        this->adaptiveStableCadenceEvaluationAt =
            now + adaptiveStableCadenceEvaluationDuration;
        this->adaptiveStableCadencePlannedGeneratedFrames = 0;
        this->adaptiveStableCadenceOnTimeGeneratedFrames = 0;
        this->adaptiveStableCadenceOutsideRangeSince.reset();
        this->adaptiveStableCadenceRetryAt.reset();
        this->adaptiveStableCadenceCandidateLimit.reset();
        this->adaptiveStableCadenceCandidateSince.reset();
        this->adaptiveStableCadenceCandidateMinimumBaseFps = 0.0;
        this->adaptiveStableCadenceCandidateMaximumBaseFps = 0.0;
        this->adaptiveOutputCredit = 0.0;
        this->diagnostics->stableCadence(
            "adaptive-stable-cadence-probe",
            *this->adaptiveStableCadenceLimit,
            baseFps,
            baseFps
        );
    }

    size_t generatedFrameCount = 0;
    if (this->adaptiveStableCadenceLimit) {
        generatedFrameCount = *this->adaptiveStableCadenceLimit;
        this->adaptiveOutputCredit = 0.0;
    } else if (desiredOutputsPerRealFrame > 1.0) {
        this->adaptiveOutputCredit += desiredOutputsPerRealFrame;
        const size_t requestedOutputs = std::max<size_t>(
            1,
            static_cast<size_t>(std::floor(this->adaptiveOutputCredit + 1e-9))
        );
        generatedFrameCount = std::min(
            requestedOutputs - 1,
            maximumGeneratedFrameCount
        );
        this->adaptiveOutputCredit -= static_cast<double>(generatedFrameCount + 1);
        if (this->adaptiveOutputCredit < 0.0)
            this->adaptiveOutputCredit = 0.0;
        if (generatedFrameCount == maximumGeneratedFrameCount &&
                this->adaptiveOutputCredit >= 1.0) {
            // The requested target is currently above the configured ceiling. Keep
            // only the fractional phase instead of accumulating an impossible
            // backlog that would delay adaptation when the base rate recovers.
            this->adaptiveOutputCredit = std::fmod(this->adaptiveOutputCredit, 1.0);
        }
    } else {
        // A Vulkan layer cannot present fewer real frames than the application
        // submits. Do not carry debt when the base rate is already at or above
        // the requested target.
        this->adaptiveOutputCredit = 0.0;
    }

    // A ramp can pass its one-second evaluation and still settle into a slower
    // compositor divisor afterwards. Ordered SDR can immediately restore its
    // previous proven generated level because FIFO delivery is deterministic.
    // The Gamescope HDR bridge first measures real-only cadence because a
    // collapse there may instead be colour-transition or admission pressure.
    const double strictBaselineOutputFps = std::min(
        static_cast<double>(this->config.targetFps),
        this->adaptiveStrictLoadBaselineBaseFps *
            static_cast<double>(this->adaptiveStrictLoadBaselineLimit + 1)
    );
    const double strictCurrentOutputFps = std::min(
        static_cast<double>(this->config.targetFps),
        baseFps * static_cast<double>(this->adaptiveGenerationLimit + 1)
    );
    const bool strictLoadCollapse =
        !this->adaptiveStableCadenceLimit &&
        !this->adaptiveRampEvaluationAt &&
        !this->adaptiveRearmRequired &&
        !this->adaptiveRescueUntil &&
        this->adaptiveStrictLoadBaselineBaseFps > 0.0 &&
        this->adaptiveGenerationLimit > this->adaptiveStrictLoadBaselineLimit &&
        generatedFrameCount == this->adaptiveGenerationLimit &&
        baseFps < this->adaptiveStrictLoadBaselineBaseFps *
            adaptiveStrictLoadCollapseRatio &&
        strictCurrentOutputFps < strictBaselineOutputFps *
            adaptiveRampMarginalGain &&
        (!this->adaptiveRescueCooldownUntil ||
         now >= *this->adaptiveRescueCooldownUntil);
    if (strictLoadCollapse) {
        if (!this->adaptiveStrictLoadCollapseSince)
            this->adaptiveStrictLoadCollapseSince = now;
        if (now - *this->adaptiveStrictLoadCollapseSince >=
                adaptiveStrictLoadCollapseDuration) {
            const size_t collapsedLimit = this->adaptiveGenerationLimit;
            if (this->config.recoveryPolicy == AdaptiveRecoveryPolicy::OrderedSdr) {
                // The ordered SDR path does not need a disruptive one-second
                // real-only measurement. A higher multiplier can immediately
                // fall back to its cheaper proven level; 2x is retained because
                // there is no cheaper generated policy to compare against.
                const size_t fallbackLimit =
                    this->adaptiveStrictLoadBaselineLimit;
                const size_t resumedLimit = fallbackLimit > 0
                    ? fallbackLimit : collapsedLimit;
                const double baselineBaseFps =
                    this->adaptiveStrictLoadBaselineBaseFps;
                this->adaptiveGenerationLimit = resumedLimit;
                this->adaptiveRampPreviousLimit = resumedLimit;
                this->adaptiveNextRampAt = now + adaptiveRescueCooldown;
                this->adaptiveRescueCooldownUntil =
                    now + adaptiveRescueCooldown;
                this->adaptiveTargetDeficitSince.reset();
                this->adaptiveStrictLoadBaselineLimit = 0;
                this->adaptiveStrictLoadBaselineBaseFps = 0.0;
                this->adaptiveStrictLoadCollapseSince.reset();
                this->adaptiveOutputCredit = 0.0;
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
            } else {
                this->adaptiveRescuePreviousLimit =
                    this->adaptiveStrictLoadBaselineLimit;
                this->adaptiveRescueBaselineBaseFps =
                    this->adaptiveStrictLoadBaselineBaseFps;
                this->adaptiveRescueFromStrictLoad = true;
                this->adaptiveRescueStrictLoadLimit = collapsedLimit;
                this->adaptiveRescueUntil =
                    now + adaptiveRescueMeasurementDuration;
                this->adaptiveRescueCooldownUntil = now + adaptiveRescueCooldown;
                this->adaptiveTargetDeficitSince.reset();
                this->adaptiveStrictLoadBaselineLimit = 0;
                this->adaptiveStrictLoadBaselineBaseFps = 0.0;
                this->adaptiveStrictLoadCollapseSince.reset();
                this->adaptiveOutputCredit = 0.0;
                this->diagnostics->rescueStart(
                    collapsedLimit,
                    this->adaptiveRescueBaselineBaseFps,
                    baseFps,
                    strictCurrentOutputFps,
                    "strict-load-collapse"
                );
                return {};
            }
        }
    } else {
        this->adaptiveStrictLoadCollapseSince.reset();
    }

    if (this->diagnosticsActive &&
            (!this->adaptiveLastDiagnostic ||
             now - *this->adaptiveLastDiagnostic >= std::chrono::seconds(1))) {
        this->adaptiveLastDiagnostic = now;
        const auto rearmRemaining = this->adaptiveRearmRequired &&
                this->adaptiveRearmNotBefore &&
                now < *this->adaptiveRearmNotBefore
            ? *this->adaptiveRearmNotBefore - now
            : AdaptiveScheduler::Clock::duration::zero();
        this->diagnostics->plan({
            .baseFps = baseFps,
            .targetFps = this->config.targetFps,
            .generatedFrames = generatedFrameCount,
            .maximumGeneratedFrames = maximumGeneratedFrameCount,
            .configuredMaximumGeneratedFrames =
                this->configuredGenerationLimit(),
            .stableCadence = this->adaptiveStableCadenceLimit.has_value(),
            .phase = this->adaptiveRearmRequired ? "rearm-cooldown" : "",
            .consecutiveFailures = this->adaptiveConsecutiveProbeFailures,
            .rearmReason = this->adaptiveRearmReason,
            .rearmCooldownRemaining = rearmRemaining,
            .rearmBaselineBaseFps = this->adaptiveRearmBaselineBaseFps,
        });
    }

    AdaptiveFramePlan plan;
    plan.count = generatedFrameCount;
    for (size_t i = 0; i < generatedFrameCount; ++i) {
        plan.values.at(i) =
            static_cast<float>(i + 1) /
            static_cast<float>(generatedFrameCount + 1);
    }
    return plan;
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
    this->adaptiveLastRealFrame = now;
    this->adaptiveSmoothedIntervalSeconds = 0.0;
    if (this->adaptiveFastBurstStartedAt) {
        this->diagnostics->fastCadenceBurstComplete(
            this->adaptiveFastBurstFrames,
            now - *this->adaptiveFastBurstStartedAt
        );
    }
    this->adaptiveFastBurstStartedAt.reset();
    this->adaptiveLastFastBurstDiagnostic.reset();
    this->adaptiveFastBurstFrames = 0;
    this->adaptiveFastBurstFramesSinceDiagnostic = 0;
    this->adaptiveTargetDeficitSince.reset();
    this->adaptiveOutputCredit = 0.0;
}

size_t AdaptiveScheduler::validatedGenerationLimit() const {
    const size_t configuredLimit = std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
    size_t generationLimit = this->adaptiveGenerationLimit;
    if (this->adaptiveRearmRequired) {
        generationLimit = this->adaptiveRearmFallbackLimit;
    } else if (this->adaptiveRampEvaluationAt) {
        generationLimit = this->adaptiveBridgeActive
            ? this->adaptiveBridgeBaselineLimit
            : this->adaptiveRampPreviousLimit;
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

    this->adaptiveGenerationLimit = restoredLimit;
    this->adaptiveRampPreviousLimit = restoredLimit;
    this->adaptiveRampEvaluationAt.reset();
    this->adaptiveTargetDeficitSince.reset();
    this->adaptiveRampBaselineBaseFps = 0.0;
    this->adaptiveBridgeActive = false;
    this->adaptiveBridgeBaselineLimit = 0;
    this->adaptiveBridgeBaselineBaseFps = 0.0;
    this->adaptiveRearmRequired = false;
    this->adaptiveRearmNotBefore.reset();
    this->adaptiveStableRearmSince.reset();
    this->adaptiveRearmImprovementSince.reset();
    this->adaptiveRearmReason.clear();
    this->adaptiveRearmBaselineBaseFps = 0.0;
    this->adaptiveRearmFallbackLimit = 0;
    this->adaptiveConsecutiveProbeFailures = 0;
    this->adaptiveLastFailedRampLimit = 0;
    this->adaptiveConsecutiveRampFailures = 0;
    this->adaptiveFailedRampBaselineBaseFps = 0.0;
    const size_t fallbackLimit = std::min(
        monitoredFallbackLimit.value_or(restoredLimit), configuredLimit
    );
    const bool monitorRestoredLoad = restoredLimit > fallbackLimit &&
        monitoredBaselineBaseFps > 0.0;
    this->adaptiveStrictLoadBaselineLimit = monitorRestoredLoad
        ? fallbackLimit
        : 0;
    this->adaptiveStrictLoadBaselineBaseFps = monitorRestoredLoad
        ? monitoredBaselineBaseFps
        : 0.0;
    this->adaptiveStrictLoadCollapseSince.reset();
    this->adaptiveOutputCredit = 0.0;

    const auto stabilizationEnd = this->adaptiveStabilizationUntil.value_or(now);
    const auto higherProbeDelay = restoredLimit > 0 && restoredLimit < configuredLimit
        ? adaptiveRecoveryHigherProbeDelay
        : AdaptiveScheduler::Clock::duration::zero();
    this->adaptiveNextRampAt = stabilizationEnd + higherProbeDelay;
    this->diagnostics->recoveryResume(restoredLimit, higherProbeDelay, reason);
}

void AdaptiveScheduler::beginCadenceRefresh(
        const std::chrono::steady_clock::time_point now,
        const std::string_view reason) {
    // Ordinary SDR menu/game cadence changes invalidate temporal history, but
    // they do not prove that the already validated multiplier is unsafe. Drop
    // only an unvalidated probe, refresh two real frames, and resume the proven
    // level immediately instead of entering a 1-5 second real-only recovery.
    const size_t retainedGenerationLimit = this->validatedGenerationLimit();
    const AdaptiveGenerationLoadBaseline loadBaseline =
        this->generationLoadBaseline();
    if (this->adaptiveRampEvaluationAt) {
        this->diagnostics->probeAborted(reason, this->adaptiveGenerationLimit);
    }

    this->adaptiveStabilizationUntil.reset();
    this->adaptiveDiscontinuityRecoveryDeadline.reset();
    this->adaptiveDiscontinuityStableSince.reset();
    this->adaptiveDiscontinuityGenerationLimit = 0;
    this->adaptiveDiscontinuityFallbackGenerationLimit = 0;
    this->adaptiveDiscontinuityBaselineBaseFps = 0.0;
    this->adaptiveDiscontinuitySoftRecoveryAttempted = false;
    this->adaptiveRampPlannedGeneratedFrames = 0;
    this->adaptiveRampOnTimeGeneratedFrames = 0;
    this->adaptiveCadenceDropFrames = 0;

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
    this->adaptiveDiscontinuityGenerationLimit = std::min(
        generationLimit, configuredLimit
    );
    if (this->adaptiveDiscontinuityGenerationLimit == 0)
        return;

    this->adaptiveDiscontinuityFallbackGenerationLimit = std::min(
        fallbackGenerationLimit,
        this->adaptiveDiscontinuityGenerationLimit
    );
    this->adaptiveDiscontinuityBaselineBaseFps = baselineBaseFps;
    const auto minimumDeadline = now + adaptiveDiscontinuityStableDuration;
    this->adaptiveDiscontinuityRecoveryDeadline = deadline
        ? std::max(*deadline, minimumDeadline)
        : now + adaptiveDiscontinuityMaximumDuration;
    this->adaptiveDiscontinuityStableSince.reset();
    this->adaptiveDiscontinuitySoftRecoveryAttempted = softRecoveryAttempted;
    this->adaptiveTargetDeficitSince.reset();
    this->adaptiveOutputCredit = 0.0;

    const auto remainingDuration =
        *this->adaptiveDiscontinuityRecoveryDeadline > now
        ? *this->adaptiveDiscontinuityRecoveryDeadline - now
        : AdaptiveScheduler::Clock::duration::zero();
    this->diagnostics->discontinuityRecoveryStart(
        this->adaptiveDiscontinuityGenerationLimit,
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
        this->adaptiveConsecutiveProbeFailures++;
    this->adaptiveRearmRequired = true;
    this->adaptiveRearmNotBefore = now + cooldown;
    this->adaptiveStableRearmSince.reset();
    this->adaptiveRearmImprovementSince.reset();
    this->adaptiveRearmReason = reason;
    this->adaptiveRearmBaselineBaseFps = baselineBaseFps;
    this->adaptiveRearmFallbackLimit = fallbackLimit;
    this->adaptiveTargetDeficitSince.reset();
    this->adaptiveNextRampAt = this->adaptiveRearmNotBefore;
    this->diagnostics->rearm(
        "adaptive-rearm-scheduled",
        reason,
        this->adaptiveConsecutiveProbeFailures,
        this->adaptiveRearmFallbackLimit,
        cooldown,
        this->adaptiveRearmBaselineBaseFps
    );
}

void AdaptiveScheduler::beginStabilization(
        const std::chrono::steady_clock::time_point now,
        const std::string_view reason) {
    const bool cadenceChange =
        reason == "cadence-stall" || reason == "cadence-drop";
    const bool hardDiscontinuity = reason == "cadence-stall";
    if (cadenceChange)
        this->adaptiveDiscontinuityStableSince.reset();
    if (hardDiscontinuity &&
            !this->adaptiveDiscontinuityRecoveryDeadline &&
            this->adaptiveSmoothedIntervalSeconds > 0.0) {
        size_t recoveryLimit = this->validatedGenerationLimit();
        if (this->adaptiveStableCadenceLimit) {
            recoveryLimit = std::max(
                recoveryLimit, *this->adaptiveStableCadenceLimit
            );
        }
        size_t recoveryFallbackLimit = recoveryLimit > 0
            ? recoveryLimit - 1
            : 0;
        if (this->adaptiveStrictLoadBaselineBaseFps > 0.0 &&
                this->adaptiveStrictLoadBaselineLimit < recoveryLimit) {
            recoveryFallbackLimit = this->adaptiveStrictLoadBaselineLimit;
        }
        this->beginDiscontinuityRecovery(
            now,
            recoveryLimit,
            recoveryFallbackLimit,
            1.0 / this->adaptiveSmoothedIntervalSeconds,
            std::nullopt,
            false,
            reason
        );
    }

    const bool alreadyStabilizing = this->adaptiveStabilizationUntil &&
        now < *this->adaptiveStabilizationUntil;
    size_t rearmFallbackLimit = 0;
    if (this->adaptiveRampEvaluationAt) {
        rearmFallbackLimit = this->adaptiveBridgeActive
            ? this->adaptiveBridgeBaselineLimit
            : this->adaptiveRampPreviousLimit;
        const double rearmBaselineBaseFps = this->adaptiveBridgeActive
            ? this->adaptiveBridgeBaselineBaseFps
            : this->adaptiveRampBaselineBaseFps;
        this->diagnostics->probeAborted(reason, this->adaptiveGenerationLimit);
        this->scheduleRearm(
            now,
            "probe-interrupted",
            rearmFallbackLimit,
            rearmBaselineBaseFps
        );
    } else if (this->adaptiveRearmRequired) {
        // A fresh cadence disruption restarts the stable-cadence requirement,
        // but it does not extend the already bounded cooldown indefinitely.
        this->adaptiveStableRearmSince.reset();
        this->adaptiveRearmImprovementSince.reset();
    }
    // Startup can include an uncapped splash screen or launcher followed by
    // normal gameplay. Do not let those first samples start a probe that the
    // gameplay transition immediately interrupts and unnecessarily penalizes.
    const auto stabilizationDuration =
        reason == "swapchain-recreation" || reason == "startup"
        ? adaptiveRecoveryStabilizationDuration
        : adaptiveStabilizationDuration;
    this->adaptiveStabilizationUntil = now + stabilizationDuration;
    this->adaptiveNextRampAt = this->adaptiveStabilizationUntil;
    if (this->adaptiveRearmNotBefore &&
            *this->adaptiveRearmNotBefore > *this->adaptiveNextRampAt) {
        this->adaptiveNextRampAt = this->adaptiveRearmNotBefore;
    }
    this->adaptiveRampEvaluationAt.reset();
    this->adaptiveRampPlannedGeneratedFrames = 0;
    this->adaptiveRampOnTimeGeneratedFrames = 0;
    this->adaptiveGenerationLimit = 0;
    this->adaptiveRampPreviousLimit = 0;
    this->adaptiveRampBaselineBaseFps = 0.0;
    this->adaptiveBridgeActive = false;
    this->adaptiveBridgeBaselineLimit = 0;
    this->adaptiveBridgeBaselineBaseFps = 0.0;
    this->adaptiveStableCadenceLimit.reset();
    this->adaptiveStableCadenceEvaluationAt.reset();
    this->adaptiveStableCadencePlannedGeneratedFrames = 0;
    this->adaptiveStableCadenceOnTimeGeneratedFrames = 0;
    this->adaptiveStableCadenceOutsideRangeSince.reset();
    this->adaptiveStableCadenceRetryAt.reset();
    this->adaptiveStableCadenceBaselineBaseFps = 0.0;
    this->adaptiveStableCadenceCandidateLimit.reset();
    this->adaptiveStableCadenceCandidateSince.reset();
    this->adaptiveStableCadenceCandidateMinimumBaseFps = 0.0;
    this->adaptiveStableCadenceCandidateMaximumBaseFps = 0.0;
    this->adaptiveRescueUntil.reset();
    this->adaptiveRescuePreviousLimit = 0;
    this->adaptiveRescueBaselineBaseFps = 0.0;
    this->adaptiveRescueFromStrictLoad = false;
    this->adaptiveRescueStrictLoadLimit = 0;
    this->adaptiveStrictLoadBaselineLimit = 0;
    this->adaptiveStrictLoadBaselineBaseFps = 0.0;
    this->adaptiveStrictLoadCollapseSince.reset();
    this->adaptiveCadenceDropFrames = 0;
    this->adaptiveLastDiagnostic.reset();
    this->resetTiming(now);
    if (!alreadyStabilizing)
        this->diagnostics->stabilization(reason, stabilizationDuration);
}

void AdaptiveScheduler::updateGenerationLimit(
        const std::chrono::steady_clock::time_point now,
        const double baseFps) {
    const size_t configuredLimit = std::min(
        this->config.generatedFrameCapacity,
        this->config.maximumMultiplier - 1
    );
    this->adaptiveGenerationLimit = std::min(
        this->adaptiveGenerationLimit, configuredLimit
    );

    if (this->adaptiveRearmRequired) {
        // The stabilization phase itself remains real-frame-only. Once it has
        // completed, retain the last proven level while the failed higher
        // probe cools down instead of dropping frame generation altogether.
        this->adaptiveGenerationLimit = std::min(
            this->adaptiveRearmFallbackLimit, configuredLimit
        );
        if (!this->adaptiveStableRearmSince)
            this->adaptiveStableRearmSince = now;

        const bool cooldownElapsed =
            !this->adaptiveRearmNotBefore || now >= *this->adaptiveRearmNotBefore;
        const bool cadenceStable =
            now - *this->adaptiveStableRearmSince >= adaptiveStableRearmDuration;
        const bool interrupted =
            this->adaptiveRearmReason == "probe-interrupted";
        const bool baseImproved = !interrupted &&
            this->adaptiveRearmBaselineBaseFps > 0.0 &&
            baseFps >= this->adaptiveRearmBaselineBaseFps *
                adaptiveRampEarlyRetryBaseImprovement;
        if (baseImproved) {
            if (!this->adaptiveRearmImprovementSince)
                this->adaptiveRearmImprovementSince = now;
        } else {
            this->adaptiveRearmImprovementSince.reset();
        }
        const bool performanceRecovered =
            this->adaptiveRearmImprovementSince &&
            now - *this->adaptiveRearmImprovementSince >=
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
            this->adaptiveRearmReason,
            this->adaptiveConsecutiveProbeFailures,
            this->adaptiveRearmFallbackLimit,
            AdaptiveScheduler::Clock::duration::zero(),
            this->adaptiveRearmBaselineBaseFps,
            baseFps,
            decision
        );
        this->adaptiveRearmRequired = false;
        this->adaptiveRearmNotBefore.reset();
        this->adaptiveStableRearmSince.reset();
        this->adaptiveRearmImprovementSince.reset();
        this->adaptiveRearmReason.clear();
        this->adaptiveRearmBaselineBaseFps = 0.0;
        this->adaptiveRearmFallbackLimit = 0;
        this->adaptiveNextRampAt.reset();
    }

    // A validated constant cadence already supplies the desired smoothness.
    // Do not probe a higher generated-frame level until it becomes unsuitable.
    if (this->adaptiveStableCadenceLimit) {
        this->adaptiveTargetDeficitSince.reset();
        return;
    }

    if (this->adaptiveRampEvaluationAt) {
        if (now < *this->adaptiveRampEvaluationAt)
            return;

        const size_t testedLimit = this->adaptiveGenerationLimit;
        if (this->adaptiveBridgeActive) {
            const double targetFps = static_cast<double>(this->config.targetFps);
            const double baselineOutputFps = std::min(
                targetFps,
                this->adaptiveBridgeBaselineBaseFps *
                    static_cast<double>(this->adaptiveBridgeBaselineLimit + 1)
            );
            const double currentOutputFps = std::min(
                targetFps,
                baseFps * static_cast<double>(testedLimit + 1)
            );
            const bool deliveryHealthy = generatedDeliveryHealthy(
                this->adaptiveRampPlannedGeneratedFrames,
                this->adaptiveRampOnTimeGeneratedFrames
            );
            const bool accepted = deliveryHealthy &&
                baseFps >= adaptiveMinimumBaseFps &&
                baseFps >= this->adaptiveBridgeBaselineBaseFps *
                    adaptiveBridgeMinimumBaseRetention &&
                currentOutputFps >= baselineOutputFps * adaptiveRampMarginalGain;
            this->diagnostics->bridgeResult(
                accepted,
                this->adaptiveBridgeBaselineLimit,
                testedLimit,
                this->adaptiveBridgeBaselineBaseFps,
                baseFps,
                baselineOutputFps,
                currentOutputFps
            );

            this->adaptiveRampEvaluationAt.reset();
            this->adaptiveRampPlannedGeneratedFrames = 0;
            this->adaptiveRampOnTimeGeneratedFrames = 0;
            this->adaptiveBridgeActive = false;
            this->adaptiveOutputCredit = 0.0;
            if (!accepted) {
                this->adaptiveGenerationLimit = this->adaptiveBridgeBaselineLimit;
                this->scheduleRearm(
                    now,
                    "bridge-rejected",
                    this->adaptiveBridgeBaselineLimit,
                    this->adaptiveBridgeBaselineBaseFps
                );
                return;
            }

            this->adaptiveConsecutiveProbeFailures = 0;
            this->adaptiveStrictLoadBaselineLimit =
                this->adaptiveBridgeBaselineLimit;
            this->adaptiveStrictLoadBaselineBaseFps =
                this->adaptiveBridgeBaselineBaseFps;
            this->adaptiveStrictLoadCollapseSince.reset();
            this->adaptiveLastFailedRampLimit = 0;
            this->adaptiveConsecutiveRampFailures = 0;
            this->adaptiveFailedRampBaselineBaseFps = 0.0;
            this->adaptiveNextRampAt = now + adaptiveRampStepDelay;
            if (this->config.stableCadence) {
                this->adaptiveStableCadenceRetryAt =
                    now + adaptiveStableCadenceStrictSettlingDuration;
            }
            return;
        }

        const double previousOutputFps = std::min(
            static_cast<double>(this->config.targetFps),
            this->adaptiveRampBaselineBaseFps *
                static_cast<double>(this->adaptiveRampPreviousLimit + 1)
        );
        const double currentOutputFps = std::min(
            static_cast<double>(this->config.targetFps),
            baseFps * static_cast<double>(testedLimit + 1)
        );
        const bool throughputRegressed =
            currentOutputFps < previousOutputFps * adaptiveRampThroughputTolerance;
        const bool baseCollapsedForMarginalGain =
            baseFps < this->adaptiveRampBaselineBaseFps * adaptiveRampBaseCollapseRatio &&
            currentOutputFps < previousOutputFps * adaptiveRampMarginalGain;
        const bool deliveryHealthy = generatedDeliveryHealthy(
            this->adaptiveRampPlannedGeneratedFrames,
            this->adaptiveRampOnTimeGeneratedFrames
        );
        const bool accepted = deliveryHealthy && !throughputRegressed &&
            !baseCollapsedForMarginalGain;
        const size_t bridgeLimit = std::min(configuredLimit, testedLimit + 1);
        const bool canBridge =
            !accepted &&
            this->adaptiveRampPreviousLimit == 0 &&
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
                this->adaptiveRampPreviousLimit,
                testedLimit,
                bridgeLimit,
                this->adaptiveRampBaselineBaseFps,
                baseFps,
                previousOutputFps,
                currentOutputFps
            );
            this->adaptiveBridgeActive = true;
            this->adaptiveBridgeBaselineLimit = this->adaptiveRampPreviousLimit;
            this->adaptiveBridgeBaselineBaseFps =
                this->adaptiveRampBaselineBaseFps;
            this->adaptiveGenerationLimit = bridgeLimit;
            this->adaptiveRampEvaluationAt = now + adaptiveRampEvaluationDuration;
            this->adaptiveRampPlannedGeneratedFrames = 0;
            this->adaptiveRampOnTimeGeneratedFrames = 0;
            this->adaptiveOutputCredit = 0.0;
            return;
        }

        this->diagnostics->rampResult(
            accepted,
            this->adaptiveRampPreviousLimit,
            testedLimit,
            this->adaptiveRampBaselineBaseFps,
            baseFps,
            previousOutputFps,
            currentOutputFps
        );

        this->adaptiveRampEvaluationAt.reset();
        this->adaptiveRampPlannedGeneratedFrames = 0;
        this->adaptiveRampOnTimeGeneratedFrames = 0;
        this->adaptiveOutputCredit = 0.0;
        if (!accepted) {
            this->adaptiveGenerationLimit = this->adaptiveRampPreviousLimit;
            if (this->adaptiveRampPreviousLimit == 0) {
                this->scheduleRearm(
                    now,
                    "ramp-rejected",
                    this->adaptiveRampPreviousLimit,
                    this->adaptiveRampBaselineBaseFps
                );
            } else {
                if (this->adaptiveLastFailedRampLimit != testedLimit) {
                    this->adaptiveLastFailedRampLimit = testedLimit;
                    this->adaptiveConsecutiveRampFailures = 0;
                }
                this->adaptiveConsecutiveRampFailures++;
                this->adaptiveFailedRampBaselineBaseFps =
                    this->adaptiveRampBaselineBaseFps;
                const auto retryDelay = adaptiveRampRetryDelayForFailures(
                    this->adaptiveConsecutiveRampFailures
                );
                this->adaptiveNextRampAt = now + retryDelay;
                this->diagnostics->rampBackoff(
                    testedLimit,
                    this->adaptiveConsecutiveRampFailures,
                    this->adaptiveFailedRampBaselineBaseFps,
                    retryDelay
                );
            }
            return;
        }
        this->adaptiveConsecutiveProbeFailures = 0;
        this->adaptiveStrictLoadBaselineLimit =
            this->adaptiveRampPreviousLimit;
        this->adaptiveStrictLoadBaselineBaseFps =
            this->adaptiveRampBaselineBaseFps;
        this->adaptiveStrictLoadCollapseSince.reset();
        this->adaptiveLastFailedRampLimit = 0;
        this->adaptiveConsecutiveRampFailures = 0;
        this->adaptiveFailedRampBaselineBaseFps = 0.0;
        this->adaptiveNextRampAt = now + adaptiveRampStepDelay;
        if (this->config.stableCadence) {
            this->adaptiveStableCadenceRetryAt =
                now + adaptiveStableCadenceStrictSettlingDuration;
        }
    }

    // Once the current proven ceiling can already supply the requested target,
    // a higher multiplier adds inference load without useful output. Keep the
    // lower level and reconsider automatically if the measured base rate later
    // falls far enough that this capacity is no longer sufficient.
    const double validatedOutputFps = baseFps *
        static_cast<double>(this->adaptiveGenerationLimit + 1);
    const bool targetSatisfied = validatedOutputFps >=
        static_cast<double>(this->config.targetFps) *
            adaptiveRampTargetSatisfiedRatio;
    if (targetSatisfied) {
        this->adaptiveTargetDeficitSince.reset();
        return;
    }

    if (this->adaptiveGenerationLimit >= configuredLimit) {
        this->adaptiveTargetDeficitSince.reset();
        return;
    }
    if (!this->adaptiveTargetDeficitSince) {
        this->adaptiveTargetDeficitSince = now;
        return;
    }
    if (now - *this->adaptiveTargetDeficitSince <
            adaptiveTargetDeficitDuration)
        return;

    if (this->adaptiveNextRampAt && now < *this->adaptiveNextRampAt) {
        const size_t nextLimit = this->adaptiveGenerationLimit + 1;
        const bool failedRampRecovered =
            this->adaptiveConsecutiveRampFailures > 0 &&
            this->adaptiveLastFailedRampLimit == nextLimit &&
            this->adaptiveFailedRampBaselineBaseFps > 0.0 &&
            baseFps >= this->adaptiveFailedRampBaselineBaseFps *
                adaptiveRampEarlyRetryBaseImprovement;
        if (!failedRampRecovered)
            return;

        this->diagnostics->rampEarlyRetry(
            nextLimit,
            this->adaptiveFailedRampBaselineBaseFps,
            baseFps
        );
        this->adaptiveNextRampAt.reset();
    }

    this->adaptiveRampPreviousLimit = this->adaptiveGenerationLimit;
    this->adaptiveRampBaselineBaseFps = baseFps;
    this->adaptiveGenerationLimit++;
    this->adaptiveRampEvaluationAt = now + adaptiveRampEvaluationDuration;
    this->adaptiveRampPlannedGeneratedFrames = 0;
    this->adaptiveRampOnTimeGeneratedFrames = 0;
    this->adaptiveTargetDeficitSince.reset();
    this->adaptiveOutputCredit = 0.0;
    this->diagnostics->ramp(
        this->adaptiveRampPreviousLimit,
        this->adaptiveGenerationLimit,
        baseFps
    );
}
