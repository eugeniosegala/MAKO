/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include "generated_frame_delivery.hpp"
#include "generated_frame_plan.hpp"
#include "mako-common/configuration/config.hpp"

#include <chrono>
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
        [[nodiscard]] inline size_t selectGeneratedFrameCount(
            double desiredOutputsPerRealFrame,
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

            struct FastBurst {
                std::optional<TimePoint> startedAt;
                std::optional<TimePoint> lastDiagnosticAt;
                size_t frames{0};
                size_t framesSinceDiagnostic{0};
            } fastBurst;

            struct OutputPlanner {
                double credit{0.0};
                size_t generationLimit{0};
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
