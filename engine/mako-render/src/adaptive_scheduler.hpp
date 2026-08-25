/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "generated_frame_delivery.hpp"
#include "generated_frame_plan.hpp"
#include "mako-common/configuration/config.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace mako::layer {

    struct AdaptiveGenerationLoadBaseline {
        size_t fallbackGenerationLimit{0};
        double baseFps{0.0};
    };

    /// Cadence recovery follows the presentation transport selected when the
    /// swapchain was created; it is not selected from live HDR feedback.
    ///
    /// Ordered SDR has FIFO ordering/backpressure under MAKO's control, so a
    /// discontinuity only needs a short temporal-history refresh and can keep
    /// the last multiplier that was proven stable. The Gamescope HDR bridge
    /// preserves Gamescope's lower transport and admits synthetic images
    /// opportunistically. A discontinuity there can also mean compositor or
    /// colour-pipeline pressure, so it deliberately remeasures more cautiously.
    enum class AdaptiveRecoveryPolicy : uint8_t {
        OrderedSdr,
        ConservativeHdr,
    };

    struct AdaptiveSchedulerConfig {
        uint32_t targetFps{ls::GameConfDefaults::targetFps};
        size_t maximumMultiplier{ls::GameConfDefaults::adaptiveMaxMultiplier};
        size_t generatedFrameCapacity{0};
        bool stableCadence{ls::GameConfDefaults::adaptiveStableCadence};
        bool dynamicCadenceRecovery{
            ls::GameConfDefaults::dynamicCadenceRecovery
        };
        std::chrono::milliseconds dynamicCadenceProbeInterval{
            ls::dynamicCadenceProbeIntervalDuration(
                ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds
            )
        };
        AdaptiveRecoveryPolicy recoveryPolicy{
            AdaptiveRecoveryPolicy::ConservativeHdr
        };
    };

    using AdaptiveFramePlan = GeneratedFramePlan;

    enum class AdaptiveSchedulerPhase : uint8_t {
        Uninitialized,
        HistoryWarmup,
        Stabilizing,
        DiscontinuityRecovery,
        RescueMeasurement,
        RearmCooldown,
        RampEvaluation,
        StableCadence,
        Active,
        NativeCadenceProbe,
    };

    struct AdaptiveSchedulerSnapshot {
        AdaptiveSchedulerPhase phase{AdaptiveSchedulerPhase::Uninitialized};
        size_t generationLimit{0};
        size_t validatedGenerationLimit{0};
        std::optional<size_t> stableCadenceLimit;
        size_t historyWarmupRemaining{0};
        double smoothedBaseFps{0.0};
        bool rampEvaluationActive{false};
        bool rearmRequired{false};
        bool discontinuityRecoveryActive{false};
        bool nativeCadenceProbeActive{false};
        bool targetOutputClockActive{false};
        double targetOutputBudgetCreditOutputs{0.0};
        double targetOutputPhaseErrorOutputs{0.0};
        bool targetOutputDeferredBudgetOutput{false};
    };

    struct AdaptivePlanDiagnostic {
        double baseFps{0.0};
        uint32_t targetFps{0};
        size_t generatedFrames{0};
        size_t maximumGeneratedFrames{0};
        size_t configuredMaximumGeneratedFrames{0};
        bool stableCadence{false};
        std::string_view phase;
        size_t recoveryGenerationLimit{0};
        size_t consecutiveFailures{0};
        std::string_view rearmReason;
        std::chrono::steady_clock::duration rearmCooldownRemaining{};
        double rearmBaselineBaseFps{0.0};
        bool targetOutputClockActive{false};
        double targetOutputBudgetCreditOutputs{0.0};
        double targetOutputPhaseErrorMilliseconds{0.0};
        bool targetOutputDeferredBudgetOutput{false};
        size_t pacingSourceSamples{0};
        double sourceIntervalMeanMilliseconds{0.0};
        double sourceIntervalStdDevMilliseconds{0.0};
        double sourceIntervalP95Milliseconds{0.0};
        double sourceIntervalP99Milliseconds{0.0};
        size_t generatedCountChanges{0};
        size_t requestedIntervalSamples{0};
        double requestedIntervalMeanMilliseconds{0.0};
        double requestedIntervalStdDevMilliseconds{0.0};
        double requestedIntervalP95Milliseconds{0.0};
        double requestedIntervalP99Milliseconds{0.0};
        size_t targetPhaseErrorSamples{0};
        double targetPhaseErrorRmsMilliseconds{0.0};
        double targetPhaseErrorMaximumMilliseconds{0.0};
    };

    class AdaptiveSchedulerDiagnostics {
    public:
        virtual ~AdaptiveSchedulerDiagnostics() = default;

        [[nodiscard]] virtual bool enabled() const { return false; }
        virtual void plan(const AdaptivePlanDiagnostic&) {}
        virtual void stabilization(std::string_view,
            std::chrono::steady_clock::duration) {}
        virtual void ramp(size_t, size_t, double) {}
        virtual void rampResult(bool, size_t, size_t, double, double,
            double, double) {}
        virtual void bridge(size_t, size_t, size_t, double, double,
            double, double) {}
        virtual void bridgeResult(bool, size_t, size_t, double, double,
            double, double) {}
        virtual void probeAborted(std::string_view, size_t) {}
        virtual void rearm(std::string_view, std::string_view, size_t, size_t,
            std::chrono::steady_clock::duration, double, double = 0.0,
            std::string_view = {}) {}
        virtual void fastCadenceBurst(double, double, double, size_t, size_t,
            std::chrono::steady_clock::duration) {}
        virtual void fastCadenceBurstComplete(size_t,
            std::chrono::steady_clock::duration) {}
        virtual void rampBackoff(size_t, size_t, double,
            std::chrono::steady_clock::duration) {}
        virtual void rampEarlyRetry(size_t, double, double) {}
        virtual void recoveryResume(size_t,
            std::chrono::steady_clock::duration, std::string_view) {}
        virtual void stableCadence(std::string_view, size_t, double, double,
            std::string_view = {}) {}
        virtual void rescueStart(size_t, double, double, double,
            std::string_view = "stable-cadence-collapse") {}
        virtual void rescueComplete(size_t, size_t, size_t, size_t, double,
            double, std::string_view) {}
        virtual void discontinuityRecoveryStart(size_t, double,
            std::string_view, std::chrono::steady_clock::duration) {}
        virtual void discontinuityRecoveryComplete(size_t, double, double,
            std::string_view) {}
        virtual void twoXGameplayHitchRecovery(size_t, double,
            std::chrono::steady_clock::duration) {}
        virtual void sdrGameplayHitchBridge(size_t, double,
            std::chrono::steady_clock::duration) {}
        virtual void cadenceRefresh(std::string_view, size_t, size_t) {}
        virtual void loadShed(size_t, size_t, double, double,
            std::string_view) {}
        virtual void nativeCadenceProbe(std::string_view, size_t, double,
            double, size_t) {}
    };

    /// Pure adaptive frame-generation policy.
    ///
    /// All time enters through method arguments. The scheduler owns no Vulkan
    /// resources and performs no wall-clock reads, which makes a recorded frame
    /// cadence exactly replayable in unit tests.
    class AdaptiveScheduler {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        explicit AdaptiveScheduler(AdaptiveSchedulerConfig config,
            AdaptiveSchedulerDiagnostics* diagnostics = nullptr);

        [[nodiscard]] AdaptiveFramePlan planFrame(TimePoint now,
            bool generatedImageAcquireBackoff);

        void resetTiming(TimePoint now);
        void updateDynamicCadenceProbeInterval(
            TimePoint now, std::chrono::milliseconds interval);
        void beginStabilization(TimePoint now, std::string_view reason);
        void restoreGenerationLimit(TimePoint now, size_t generationLimit,
            std::string_view reason,
            std::optional<size_t> monitoredFallbackLimit = std::nullopt,
            double monitoredBaselineBaseFps = 0.0);
        void beginDiscontinuityRecovery(TimePoint now, size_t generationLimit,
            size_t fallbackGenerationLimit, double baselineBaseFps,
            std::optional<TimePoint> deadline,
            bool softRecoveryAttempted, std::string_view reason);

        [[nodiscard]] size_t validatedGenerationLimit() const;
        [[nodiscard]] AdaptiveSchedulerSnapshot snapshot() const;

        [[nodiscard]] bool historyWarmupActive() const {
            return this->state.historyWarmup.remaining > 0;
        }
        [[nodiscard]] size_t historyWarmupRemaining() const {
            return this->state.historyWarmup.remaining;
        }
        [[nodiscard]] bool historyWarmupIsRecovery() const {
            return this->state.historyWarmup.recovery;
        }
        void beginHistoryWarmup(size_t frames, bool recovery);
        void ensureHistoryWarmup(size_t frames, bool recovery);
        void cancelHistoryWarmup();
        void consumeHistoryWarmupFrame(TimePoint now);
        void reportGeneratedFrameDelivery(GeneratedFrameDelivery delivery);

        [[nodiscard]] bool discontinuityRecoveryActive() const {
            return this->state.discontinuityRecovery.deadline.has_value();
        }
        [[nodiscard]] size_t discontinuityGenerationLimit() const {
            return this->state.discontinuityRecovery.generationLimit;
        }
        [[nodiscard]] double discontinuityBaselineBaseFps() const {
            return this->state.discontinuityRecovery.baselineBaseFps;
        }
        [[nodiscard]] size_t discontinuityFallbackGenerationLimit() const {
            return this->state.discontinuityRecovery.fallbackGenerationLimit;
        }
        [[nodiscard]] AdaptiveGenerationLoadBaseline generationLoadBaseline()
                const {
            if (this->state.strictLoad.baselineBaseFps <= 0.0 ||
                    this->validatedGenerationLimit() <=
                        this->state.strictLoad.baselineLimit) {
                return {};
            }
            return {
                .fallbackGenerationLimit =
                    this->state.strictLoad.baselineLimit,
                .baseFps = this->state.strictLoad.baselineBaseFps,
            };
        }
        [[nodiscard]] std::optional<TimePoint> discontinuityDeadline() const {
            return this->state.discontinuityRecovery.deadline;
        }
        [[nodiscard]] bool discontinuitySoftRecoveryAttempted() const {
            return this->state.discontinuityRecovery.softRecoveryAttempted;
        }
        void markDiscontinuitySoftRecoveryAttempted() {
            this->state.discontinuityRecovery.softRecoveryAttempted = true;
        }

        static size_t historyWarmupFrameCount();
        static Clock::duration rescueMeasurementDuration();
        static Clock::duration rescueCooldown();
        static Clock::duration stableRearmDuration();

    private:
        struct CadenceObservation {
            bool planningReady{false};
            AdaptiveFramePlan terminalPlan;
            double baseFps{0.0};
            double instantaneousBaseFps{0.0};
            double rawIntervalSeconds{0.0};
        };

        struct PlanningStageResult {
            bool planningReady{false};
            AdaptiveFramePlan terminalPlan;
        };

        // These named stages are still one per-present hot path. Their
        // definitions remain inline in adaptive_scheduler.cpp so optimized
        // flattening does not retain duplicate out-of-line implementations.
        [[nodiscard]] inline CadenceObservation observeCadence(
            TimePoint now, bool generatedImageAcquireBackoff);
        [[nodiscard]] inline PlanningStageResult advanceDiscontinuityRecovery(
            TimePoint now, double baseFps);
        [[nodiscard]] inline PlanningStageResult advanceRescueMeasurement(
            TimePoint now, double baseFps);
        [[nodiscard]] inline PlanningStageResult advanceStableCadence(
            TimePoint now, double baseFps,
            double desiredOutputsPerRealFrame,
            size_t maximumGeneratedFrameCount);
        inline void advanceEfficiencyProbe(TimePoint now, double baseFps);
        [[nodiscard]] inline size_t selectGeneratedFrameCount(
            double desiredOutputsPerRealFrame,
            double rawIntervalSeconds,
            size_t maximumGeneratedFrameCount);
        [[nodiscard]] inline PlanningStageResult advanceNativeCadenceProbe(
            TimePoint now, double baseFps, double instantaneousBaseFps,
            double desiredOutputsPerRealFrame,
            size_t maximumGeneratedFrameCount,
            size_t& generatedFrameCount);
        [[nodiscard]] inline PlanningStageResult applyStrictLoadGuard(
            TimePoint now, double baseFps, size_t& generatedFrameCount);
        void beginCadenceRefresh(TimePoint now, std::string_view reason);
        void scheduleRearm(TimePoint now, std::string_view reason,
            size_t fallbackLimit = 0, double baselineBaseFps = 0.0);
        // This larger multiplier-validation stage follows the same inline
        // contract as the per-present stages above.
        inline void updateGenerationLimit(TimePoint now, double baseFps);
        [[nodiscard]] size_t configuredGenerationLimit() const;

        struct SchedulerState {
            struct HistoryWarmup {
                size_t remaining{0};
                bool recovery{false};
            } historyWarmup;

            struct Cadence {
                std::optional<TimePoint> lastRealFrame;
                double smoothedIntervalSeconds{0.0};
                size_t dropFrames{0};
                // Ordered SDR keeps updating temporal history while native
                // frames are presented. Once a hard cadence stall has already
                // requested that refresh, do not restart the same warm-up on
                // every sub-10-FPS frame. Resume normal policy only after
                // several clearly viable intervals.
                bool sdrStallBypass{false};
                size_t sdrResumeFrames{0};
                // An accepted Smooth Cadence 2x policy can bridge one short
                // gameplay hitch. A second consecutive stall must use normal
                // history recovery so loading screens and genuinely collapsed
                // cadence remain safe.
                bool sdrIsolatedHitchBridged{false};
            } cadence;

            struct DiagnosticThrottle {
                std::optional<TimePoint> lastPlanAt;
            } diagnosticThrottle;

            struct PacingWindow {
                struct Distribution {
                    static constexpr size_t histogramBinCount = 101;
                    static constexpr double histogramBinWidthMilliseconds = 1.0;

                    std::array<uint32_t, histogramBinCount> histogram{};
                    size_t samples{0};
                    double mean{0.0};
                    double squaredDeviation{0.0};

                    void record(const double milliseconds) {
                        const double bounded = std::max(0.0, milliseconds);
                        const size_t bin = std::min(
                            histogramBinCount - 1,
                            static_cast<size_t>(
                                bounded / histogramBinWidthMilliseconds
                            )
                        );
                        this->histogram[bin]++;
                        this->samples++;
                        const double delta = bounded - this->mean;
                        this->mean += delta / static_cast<double>(this->samples);
                        this->squaredDeviation +=
                            delta * (bounded - this->mean);
                    }

                    [[nodiscard]] double standardDeviation() const {
                        return this->samples > 1
                            ? std::sqrt(
                                std::max(0.0, this->squaredDeviation) /
                                static_cast<double>(this->samples)
                            )
                            : 0.0;
                    }

                    [[nodiscard]] double percentile(
                            const double quantile) const {
                        if (this->samples == 0)
                            return 0.0;
                        const size_t rank = static_cast<size_t>(std::ceil(
                            std::clamp(quantile, 0.0, 1.0) *
                            static_cast<double>(this->samples)
                        ));
                        size_t cumulative = 0;
                        for (size_t bin = 0;
                                bin < this->histogram.size(); ++bin) {
                            cumulative += this->histogram[bin];
                            if (cumulative >= std::max<size_t>(rank, 1)) {
                                return (static_cast<double>(bin) + 0.5) *
                                    histogramBinWidthMilliseconds;
                            }
                        }
                        return static_cast<double>(histogramBinCount) *
                            histogramBinWidthMilliseconds;
                    }

                    void reset() {
                        this->histogram.fill(0);
                        this->samples = 0;
                        this->mean = 0.0;
                        this->squaredDeviation = 0.0;
                    }
                } sourceIntervals, requestedIntervals;

                std::optional<size_t> previousGeneratedFrameCount;
                size_t generatedCountChanges{0};
                size_t phaseErrorSamples{0};
                double phaseErrorSquaredMilliseconds{0.0};
                double maximumAbsolutePhaseErrorMilliseconds{0.0};
                size_t frameSequence{0};
                std::optional<size_t> lastRecordedFrameSequence;
                std::optional<bool> recordedTargetClockActive;
                std::optional<bool> recordedStableCadence;
                std::optional<size_t> recordedGenerationLimit;
                std::optional<size_t> recordedTargetClockEpoch;

                void beginFrame() {
                    this->frameSequence++;
                }

                void record(const double rawIntervalSeconds,
                        const size_t generatedFrameCount,
                        const bool targetClockActive,
                        const double targetPhaseErrorOutputs,
                        const uint32_t targetFps,
                        const bool stableCadence,
                        const size_t generationLimit,
                        const size_t targetClockEpoch) {
                    const bool skippedPolicyFrame =
                        this->lastRecordedFrameSequence &&
                        this->frameSequence !=
                            *this->lastRecordedFrameSequence + 1;
                    const bool policyChanged =
                        (this->recordedTargetClockActive &&
                         *this->recordedTargetClockActive !=
                            targetClockActive) ||
                        (this->recordedStableCadence &&
                         *this->recordedStableCadence != stableCadence) ||
                        (this->recordedGenerationLimit &&
                         *this->recordedGenerationLimit != generationLimit) ||
                        (this->recordedTargetClockEpoch &&
                         *this->recordedTargetClockEpoch != targetClockEpoch);
                    if (skippedPolicyFrame || policyChanged) {
                        this->resetWindow();
                        this->previousGeneratedFrameCount.reset();
                    }
                    this->lastRecordedFrameSequence = this->frameSequence;
                    this->recordedTargetClockActive = targetClockActive;
                    this->recordedStableCadence = stableCadence;
                    this->recordedGenerationLimit = generationLimit;
                    this->recordedTargetClockEpoch = targetClockEpoch;

                    const double sourceIntervalMilliseconds =
                        rawIntervalSeconds * 1000.0;
                    this->sourceIntervals.record(sourceIntervalMilliseconds);
                    const size_t outputCount = generatedFrameCount + 1;
                    const double requestedIntervalMilliseconds =
                        sourceIntervalMilliseconds /
                        static_cast<double>(outputCount);
                    for (size_t output = 0; output < outputCount; ++output)
                        this->requestedIntervals.record(
                            requestedIntervalMilliseconds
                        );
                    if (this->previousGeneratedFrameCount &&
                            *this->previousGeneratedFrameCount !=
                                generatedFrameCount) {
                        this->generatedCountChanges++;
                    }
                    this->previousGeneratedFrameCount = generatedFrameCount;

                    if (!targetClockActive || targetFps == 0)
                        return;
                    const double phaseErrorMilliseconds =
                        targetPhaseErrorOutputs * 1000.0 /
                        static_cast<double>(targetFps);
                    this->phaseErrorSamples++;
                    this->phaseErrorSquaredMilliseconds +=
                        phaseErrorMilliseconds * phaseErrorMilliseconds;
                    this->maximumAbsolutePhaseErrorMilliseconds = std::max(
                        this->maximumAbsolutePhaseErrorMilliseconds,
                        std::abs(phaseErrorMilliseconds)
                    );
                }

                void resetWindow() {
                    this->sourceIntervals.reset();
                    this->requestedIntervals.reset();
                    this->generatedCountChanges = 0;
                    this->phaseErrorSamples = 0;
                    this->phaseErrorSquaredMilliseconds = 0.0;
                    this->maximumAbsolutePhaseErrorMilliseconds = 0.0;
                }

                void reset() {
                    this->resetWindow();
                    this->previousGeneratedFrameCount.reset();
                    this->frameSequence = 0;
                    this->lastRecordedFrameSequence.reset();
                    this->recordedTargetClockActive.reset();
                    this->recordedStableCadence.reset();
                    this->recordedGenerationLimit.reset();
                    this->recordedTargetClockEpoch.reset();
                }
            } pacingWindow;

            struct FastBurst {
                std::optional<TimePoint> startedAt;
                std::optional<TimePoint> lastDiagnosticAt;
                size_t frames{0};
                size_t framesSinceDiagnostic{0};
            } fastBurst;

            struct OutputPlanner {
                // Smoothed cadence owns the generated-work budget, preserving
                // the established load envelope. Raw cadence owns only the
                // bounded placement phase and may defer one already-budgeted
                // output from a clearly short interval.
                double budgetCreditOutputs{0.0};
                double targetPhaseErrorOutputs{0.0};
                bool deferredBudgetOutput{false};
                double deferredPlacementBenefit{0.0};
                double maximumBaselineSpacingOutputs{0.0};
                bool targetClockActive{false};
                size_t targetClockEpoch{0};
                size_t generationLimit{0};

                void resetTargetClock() {
                    if (this->budgetCreditOutputs != 0.0 ||
                            this->targetPhaseErrorOutputs != 0.0 ||
                            this->deferredBudgetOutput ||
                            this->deferredPlacementBenefit != 0.0 ||
                            this->maximumBaselineSpacingOutputs != 0.0 ||
                            this->targetClockActive) {
                        this->targetClockEpoch++;
                    }
                    this->budgetCreditOutputs = 0.0;
                    this->targetPhaseErrorOutputs = 0.0;
                    this->deferredBudgetOutput = false;
                    this->deferredPlacementBenefit = 0.0;
                    this->maximumBaselineSpacingOutputs = 0.0;
                    this->targetClockActive = false;
                }
            } outputPlanner;

            struct NativeCadenceProbe {
                std::optional<TimePoint> nextAt;
                bool active{false};
                double baselineBaseFps{0.0};
                double minimumMeasuredBaseFps{0.0};
                size_t confirmedSamples{0};

                void reset() {
                    this->nextAt.reset();
                    this->active = false;
                    this->baselineBaseFps = 0.0;
                    this->minimumMeasuredBaseFps = 0.0;
                    this->confirmedSamples = 0;
                }
            } nativeCadenceProbe;

            struct Stabilization {
                std::optional<TimePoint> until;
            } stabilization;

            struct Ramp {
                std::optional<TimePoint> nextAt;
                std::optional<TimePoint> evaluationAt;
                std::optional<TimePoint> targetDeficitSince;
                GeneratedDeliveryWindow delivery;
                size_t previousLimit{0};
                double baselineBaseFps{0.0};
                bool bridgeActive{false};
                size_t bridgeBaselineLimit{0};
                double bridgeBaselineBaseFps{0.0};
                size_t lastFailedLimit{0};
                size_t consecutiveFailures{0};
                double failedBaselineBaseFps{0.0};
            } ramp;

            struct Rearm {
                bool required{false};
                std::optional<TimePoint> notBefore;
                std::optional<TimePoint> stableSince;
                std::optional<TimePoint> improvementSince;
                std::string reason;
                double baselineBaseFps{0.0};
                size_t fallbackLimit{0};
                size_t consecutiveProbeFailures{0};
            } rearm;

            struct StableCadence {
                struct Candidate {
                    std::optional<size_t> limit;
                    std::optional<TimePoint> since;
                    double minimumBaseFps{0.0};
                    double maximumBaseFps{0.0};

                    void reset() {
                        this->limit.reset();
                        this->since.reset();
                        this->minimumBaseFps = 0.0;
                        this->maximumBaseFps = 0.0;
                    }
                } candidate;

                std::optional<size_t> limit;
                std::optional<TimePoint> evaluationAt;
                std::optional<TimePoint> outsideRangeSince;
                std::optional<TimePoint> retryAt;
                double baselineBaseFps{0.0};
                GeneratedDeliveryWindow delivery;
            } stableCadence;

            struct EfficiencyProbe {
                std::optional<TimePoint> eligibleSince;
                std::optional<TimePoint> evaluationAt;
                std::optional<TimePoint> retryAt;
                GeneratedDeliveryWindow delivery;
                size_t testedLimit{0};
                double baselineBaseFps{0.0};

                void reset() {
                    this->eligibleSince.reset();
                    this->evaluationAt.reset();
                    this->retryAt.reset();
                    this->delivery.reset();
                    this->testedLimit = 0;
                    this->baselineBaseFps = 0.0;
                }
            } efficiencyProbe;

            struct Rescue {
                std::optional<TimePoint> until;
                std::optional<TimePoint> cooldownUntil;
                size_t previousLimit{0};
                double baselineBaseFps{0.0};
                bool fromStrictLoad{false};
                size_t strictLoadLimit{0};
            } rescue;

            struct StrictLoad {
                size_t baselineLimit{0};
                double baselineBaseFps{0.0};
                std::optional<TimePoint> collapseSince;
                std::optional<TimePoint> healthySince;
                std::optional<TimePoint> recoverySince;
                size_t failedLimit{0};
                size_t consecutiveFailures{0};
                double failedBaselineBaseFps{0.0};
            } strictLoad;

            struct DiscontinuityRecovery {
                std::optional<TimePoint> deadline;
                std::optional<TimePoint> stableSince;
                size_t generationLimit{0};
                size_t fallbackGenerationLimit{0};
                double baselineBaseFps{0.0};
                bool softRecoveryAttempted{false};

                void reset() {
                    this->deadline.reset();
                    this->stableSince.reset();
                    this->generationLimit = 0;
                    this->fallbackGenerationLimit = 0;
                    this->baselineBaseFps = 0.0;
                    this->softRecoveryAttempted = false;
                }
            } discontinuityRecovery;
        };

        AdaptiveSchedulerConfig config;
        AdaptiveSchedulerDiagnostics* diagnostics;
        bool diagnosticsActive;
        SchedulerState state;
    };

}
