/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
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
        uint32_t targetFps{120};
        size_t maximumMultiplier{3};
        size_t generatedFrameCapacity{0};
        bool stableCadence{false};
        AdaptiveRecoveryPolicy recoveryPolicy{
            AdaptiveRecoveryPolicy::ConservativeHdr
        };
    };

    class AdaptiveScheduler;

    /// Generated-frame timestamps for one real-frame interval.
    ///
    /// Adaptive is capped at 4x, so at most three timestamps can be produced.
    /// Keeping them inline avoids a heap allocation on every generated frame.
    class AdaptiveFramePlan {
    public:
        using const_iterator = std::array<float, 3>::const_iterator;

        [[nodiscard]] size_t size() const { return this->count; }
        [[nodiscard]] bool empty() const { return this->count == 0; }
        [[nodiscard]] float front() const { return this->values.front(); }
        [[nodiscard]] float operator[](const size_t index) const {
            return this->values[index];
        }
        [[nodiscard]] const_iterator begin() const {
            return this->values.begin();
        }
        [[nodiscard]] const_iterator end() const {
            return this->values.begin() + static_cast<std::ptrdiff_t>(this->count);
        }
        [[nodiscard]] std::span<const float> timestamps() const {
            return {this->values.data(), this->count};
        }

        bool operator==(const AdaptiveFramePlan&) const = default;

    private:
        friend class AdaptiveScheduler;

        std::array<float, 3> values{};
        size_t count{0};
    };

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
        virtual void cadenceRefresh(std::string_view, size_t, size_t) {}
        virtual void loadShed(size_t, size_t, double, double,
            std::string_view) {}
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
            return this->adaptiveHistoryWarmupRemaining > 0;
        }
        [[nodiscard]] size_t historyWarmupRemaining() const {
            return this->adaptiveHistoryWarmupRemaining;
        }
        [[nodiscard]] bool historyWarmupIsRecovery() const {
            return this->adaptiveHistoryWarmupIsRecovery;
        }
        void beginHistoryWarmup(size_t frames, bool recovery);
        void ensureHistoryWarmup(size_t frames, bool recovery);
        void cancelHistoryWarmup();
        void consumeHistoryWarmupFrame(TimePoint now);
        void reportGeneratedFrameDelivery(size_t planned, size_t onTime);

        [[nodiscard]] bool discontinuityRecoveryActive() const {
            return this->adaptiveDiscontinuityRecoveryDeadline.has_value();
        }
        [[nodiscard]] size_t discontinuityGenerationLimit() const {
            return this->adaptiveDiscontinuityGenerationLimit;
        }
        [[nodiscard]] double discontinuityBaselineBaseFps() const {
            return this->adaptiveDiscontinuityBaselineBaseFps;
        }
        [[nodiscard]] size_t discontinuityFallbackGenerationLimit() const {
            return this->adaptiveDiscontinuityFallbackGenerationLimit;
        }
        [[nodiscard]] AdaptiveGenerationLoadBaseline generationLoadBaseline()
                const {
            if (this->adaptiveStrictLoadBaselineBaseFps <= 0.0 ||
                    this->validatedGenerationLimit() <=
                        this->adaptiveStrictLoadBaselineLimit) {
                return {};
            }
            return {
                .fallbackGenerationLimit =
                    this->adaptiveStrictLoadBaselineLimit,
                .baseFps = this->adaptiveStrictLoadBaselineBaseFps,
            };
        }
        [[nodiscard]] std::optional<TimePoint> discontinuityDeadline() const {
            return this->adaptiveDiscontinuityRecoveryDeadline;
        }
        [[nodiscard]] bool discontinuitySoftRecoveryAttempted() const {
            return this->adaptiveDiscontinuitySoftRecoveryAttempted;
        }
        void markDiscontinuitySoftRecoveryAttempted() {
            this->adaptiveDiscontinuitySoftRecoveryAttempted = true;
        }

        static size_t historyWarmupFrameCount();
        static Clock::duration rescueMeasurementDuration();
        static Clock::duration rescueCooldown();
        static Clock::duration stableRearmDuration();

    private:
        void beginCadenceRefresh(TimePoint now, std::string_view reason);
        void scheduleRearm(TimePoint now, std::string_view reason,
            size_t fallbackLimit = 0, double baselineBaseFps = 0.0);
        void updateGenerationLimit(TimePoint now, double baseFps);
        [[nodiscard]] size_t configuredGenerationLimit() const;

        AdaptiveSchedulerConfig config;
        AdaptiveSchedulerDiagnostics* diagnostics;
        bool diagnosticsActive;

        size_t adaptiveHistoryWarmupRemaining{0};
        bool adaptiveHistoryWarmupIsRecovery{false};
        std::optional<TimePoint> adaptiveLastRealFrame;
        std::optional<TimePoint> adaptiveLastDiagnostic;
        std::optional<TimePoint> adaptiveFastBurstStartedAt;
        std::optional<TimePoint> adaptiveLastFastBurstDiagnostic;
        size_t adaptiveFastBurstFrames{0};
        size_t adaptiveFastBurstFramesSinceDiagnostic{0};
        double adaptiveSmoothedIntervalSeconds{0.0};
        double adaptiveOutputCredit{0.0};
        std::optional<TimePoint> adaptiveStabilizationUntil;
        std::optional<TimePoint> adaptiveNextRampAt;
        std::optional<TimePoint> adaptiveRampEvaluationAt;
        std::optional<TimePoint> adaptiveTargetDeficitSince;
        size_t adaptiveGenerationLimit{0};
        size_t adaptiveRampPlannedGeneratedFrames{0};
        size_t adaptiveRampOnTimeGeneratedFrames{0};
        size_t adaptiveStableCadencePlannedGeneratedFrames{0};
        size_t adaptiveStableCadenceOnTimeGeneratedFrames{0};
        size_t adaptiveRampPreviousLimit{0};
        size_t adaptiveCadenceDropFrames{0};
        double adaptiveRampBaselineBaseFps{0.0};
        bool adaptiveBridgeActive{false};
        size_t adaptiveBridgeBaselineLimit{0};
        double adaptiveBridgeBaselineBaseFps{0.0};
        bool adaptiveRearmRequired{false};
        std::optional<TimePoint> adaptiveRearmNotBefore;
        std::optional<TimePoint> adaptiveStableRearmSince;
        std::optional<TimePoint> adaptiveRearmImprovementSince;
        std::string adaptiveRearmReason;
        double adaptiveRearmBaselineBaseFps{0.0};
        size_t adaptiveRearmFallbackLimit{0};
        std::optional<size_t> adaptiveStableCadenceLimit;
        std::optional<TimePoint> adaptiveStableCadenceEvaluationAt;
        std::optional<TimePoint> adaptiveStableCadenceOutsideRangeSince;
        std::optional<TimePoint> adaptiveStableCadenceRetryAt;
        double adaptiveStableCadenceBaselineBaseFps{0.0};
        std::optional<size_t> adaptiveStableCadenceCandidateLimit;
        std::optional<TimePoint> adaptiveStableCadenceCandidateSince;
        double adaptiveStableCadenceCandidateMinimumBaseFps{0.0};
        double adaptiveStableCadenceCandidateMaximumBaseFps{0.0};
        std::optional<TimePoint> adaptiveRescueUntil;
        std::optional<TimePoint> adaptiveRescueCooldownUntil;
        size_t adaptiveRescuePreviousLimit{0};
        double adaptiveRescueBaselineBaseFps{0.0};
        bool adaptiveRescueFromStrictLoad{false};
        size_t adaptiveRescueStrictLoadLimit{0};
        size_t adaptiveStrictLoadBaselineLimit{0};
        double adaptiveStrictLoadBaselineBaseFps{0.0};
        std::optional<TimePoint> adaptiveStrictLoadCollapseSince;
        std::optional<TimePoint> adaptiveDiscontinuityRecoveryDeadline;
        std::optional<TimePoint> adaptiveDiscontinuityStableSince;
        size_t adaptiveDiscontinuityGenerationLimit{0};
        size_t adaptiveDiscontinuityFallbackGenerationLimit{0};
        double adaptiveDiscontinuityBaselineBaseFps{0.0};
        bool adaptiveDiscontinuitySoftRecoveryAttempted{false};
        size_t adaptiveConsecutiveProbeFailures{0};
        size_t adaptiveLastFailedRampLimit{0};
        size_t adaptiveConsecutiveRampFailures{0};
        double adaptiveFailedRampBaselineBaseFps{0.0};
    };

}
