/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "adaptive_scheduler.hpp"
#include "generated_frame_delivery.hpp"
#include "presentation_policy.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {
    using TimePoint = AdaptiveScheduler::TimePoint;

    struct TestFailure {
        std::string message;
    };

    void require(const bool condition, std::string message) {
        if (!condition)
            throw TestFailure{std::move(message)};
    }

    void requireNear(const float actual, const float expected,
            const float tolerance, std::string message) {
        if (std::abs(actual - expected) > tolerance) {
            message += ": expected " + std::to_string(expected) +
                ", got " + std::to_string(actual);
            throw TestFailure{std::move(message)};
        }
    }

    struct RecordingDiagnostics final : AdaptiveSchedulerDiagnostics {
        struct Event {
            std::string operation;
            std::string reason;
            bool accepted{false};
            size_t previousLimit{0};
            size_t testedLimit{0};
        };

        std::vector<Event> events;
        std::vector<AdaptivePlanDiagnostic> plans;
        std::optional<AdaptivePlanDiagnostic> latestPlan;

        [[nodiscard]] bool enabled() const override { return true; }

        void plan(const AdaptivePlanDiagnostic& plan) override {
            this->plans.push_back(plan);
            this->latestPlan = plan;
        }

        void stabilization(const std::string_view reason,
                std::chrono::steady_clock::duration) override {
            this->events.push_back({
                .operation = "stabilization",
                .reason = std::string(reason),
            });
        }

        void ramp(const size_t previousLimit, const size_t testedLimit,
                double) override {
            this->events.push_back({
                .operation = "ramp",
                .reason = {},
                .previousLimit = previousLimit,
                .testedLimit = testedLimit,
            });
        }

        void rampResult(const bool accepted, const size_t previousLimit,
                const size_t testedLimit, double, double, double,
                double) override {
            this->events.push_back({
                .operation = "ramp-result",
                .reason = {},
                .accepted = accepted,
                .previousLimit = previousLimit,
                .testedLimit = testedLimit,
            });
        }

        void probeAborted(std::string_view reason, size_t testedLimit) override {
            this->events.push_back({
                .operation = "probe-aborted",
                .reason = std::string(reason),
                .testedLimit = testedLimit,
            });
        }

        void bridge(size_t previousLimit, size_t testedLimit,
                size_t bridgeLimit, double, double, double, double) override {
            this->events.push_back({
                .operation = "bridge",
                .reason = {},
                .previousLimit = previousLimit,
                .testedLimit = bridgeLimit,
            });
            static_cast<void>(testedLimit);
        }

        void bridgeResult(bool accepted, size_t baselineLimit,
                size_t testedLimit, double, double, double, double) override {
            this->events.push_back({
                .operation = "bridge-result",
                .reason = {},
                .accepted = accepted,
                .previousLimit = baselineLimit,
                .testedLimit = testedLimit,
            });
        }

        void rearm(std::string_view operation, std::string_view reason,
                size_t, size_t, std::chrono::steady_clock::duration,
                double, double, std::string_view) override {
            this->events.push_back({
                .operation = std::string(operation),
                .reason = std::string(reason),
            });
        }

        void rampBackoff(size_t testedLimit, size_t failures, double,
                std::chrono::steady_clock::duration) override {
            this->events.push_back({
                .operation = "ramp-backoff",
                .reason = {},
                .previousLimit = failures,
                .testedLimit = testedLimit,
            });
        }

        void rampEarlyRetry(size_t testedLimit, double, double) override {
            this->events.push_back({
                .operation = "ramp-early-retry",
                .reason = {},
                .testedLimit = testedLimit,
            });
        }

        void fastCadenceBurst(double, double, double, size_t, size_t,
                std::chrono::steady_clock::duration) override {
            this->events.push_back({
                .operation = "fast-burst",
                .reason = {},
            });
        }

        void fastCadenceBurstComplete(size_t,
                std::chrono::steady_clock::duration) override {
            this->events.push_back({
                .operation = "fast-burst-complete",
                .reason = {},
            });
        }

        void twoXGameplayHitchRecovery(size_t, double,
                std::chrono::steady_clock::duration) override {
            this->events.push_back({
                .operation = "2x-hitch-recovery",
                .reason = {},
            });
        }

        void sdrGameplayHitchBridge(size_t generationLimit, double,
                std::chrono::steady_clock::duration) override {
            this->events.push_back({
                .operation = "sdr-hitch-bridge",
                .reason = {},
                .testedLimit = generationLimit,
            });
        }

        void cadenceRefresh(std::string_view reason, size_t retainedLimit,
                size_t historyFrames) override {
            this->events.push_back({
                .operation = "cadence-refresh",
                .reason = std::string(reason),
                .previousLimit = retainedLimit,
                .testedLimit = historyFrames,
            });
        }

        void loadShed(size_t previousLimit, size_t resumedLimit,
                double, double, std::string_view reason) override {
            this->events.push_back({
                .operation = "load-shed",
                .reason = std::string(reason),
                .previousLimit = previousLimit,
                .testedLimit = resumedLimit,
            });
        }

        void nativeCadenceProbe(std::string_view operation, size_t,
                double, double, size_t) override {
            this->events.push_back({
                .operation = std::string(operation),
            });
        }

        void stableCadence(const std::string_view operation, size_t, double,
                double, const std::string_view reason) override {
            this->events.push_back({
                .operation = std::string(operation),
                .reason = std::string(reason),
            });
        }

        void rescueStart(size_t previousLimit, double, double, double,
                std::string_view reason) override {
            this->events.push_back({
                .operation = "rescue-start",
                .reason = std::string(reason),
                .previousLimit = previousLimit,
            });
        }

        void rescueComplete(size_t previousLimit, size_t resumedLimit,
                size_t, size_t, double, double,
                std::string_view decision) override {
            this->events.push_back({
                .operation = "rescue-complete",
                .reason = std::string(decision),
                .previousLimit = previousLimit,
                .testedLimit = resumedLimit,
            });
        }

        void discontinuityRecoveryComplete(size_t previousLimit, double,
                double, std::string_view decision) override {
            this->events.push_back({
                .operation = "discontinuity-complete",
                .reason = std::string(decision),
                .previousLimit = previousLimit,
            });
        }

        [[nodiscard]] bool contains(const std::string_view operation) const {
            for (const auto& event : this->events) {
                if (event.operation == operation)
                    return true;
            }
            return false;
        }

        [[nodiscard]] const Event* last(const std::string_view operation) const {
            for (auto event = this->events.rbegin();
                    event != this->events.rend(); ++event) {
                if (event->operation == operation)
                    return &*event;
            }
            return nullptr;
        }

        [[nodiscard]] size_t count(const std::string_view operation) const {
            size_t result = 0;
            for (const auto& event : this->events) {
                if (event.operation == operation)
                    result++;
            }
            return result;
        }
    };

    struct Harness {
        RecordingDiagnostics diagnostics;
        AdaptiveScheduler scheduler;
        TimePoint now{};

        Harness(const uint32_t targetFps, const size_t maximumMultiplier,
                const bool stableCadence = false,
                const AdaptiveRecoveryPolicy recoveryPolicy =
                    AdaptiveRecoveryPolicy::ConservativeHdr,
                const bool dynamicCadenceRecovery = false,
                const std::chrono::seconds dynamicCadenceProbeInterval =
                    std::chrono::seconds(
                        ls::GameConfDefaults::
                            dynamicCadenceProbeIntervalSeconds
                    )) :
            scheduler(
                AdaptiveSchedulerConfig{
                    .targetFps = targetFps,
                    .maximumMultiplier = maximumMultiplier,
                    .generatedFrameCapacity = 3,
                    .stableCadence = stableCadence,
                    .dynamicCadenceRecovery = dynamicCadenceRecovery,
                    .dynamicCadenceProbeInterval =
                        dynamicCadenceProbeInterval,
                    .recoveryPolicy = recoveryPolicy,
                },
                &this->diagnostics
            ) {}

        void start() {
            this->scheduler.beginStabilization(this->now, "startup");
            for (size_t i = 0;
                    i < AdaptiveScheduler::historyWarmupFrameCount(); ++i) {
                this->now += 16ms;
                this->scheduler.consumeHistoryWarmupFrame(this->now);
            }
        }

        AdaptiveFramePlan frame(const std::chrono::nanoseconds interval,
                const bool acquireBackoff = false) {
            this->now += interval;
            return this->scheduler.planFrame(this->now, acquireBackoff);
        }

        AdaptiveFramePlan frameAtFps(const double fps,
                const bool acquireBackoff = false) {
            return this->frame(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(1.0 / fps)
            ), acquireBackoff);
        }

        AdaptiveFramePlan runAtFps(const double fps,
                const std::chrono::seconds duration) {
            AdaptiveFramePlan result;
            const size_t frames = static_cast<size_t>(
                std::ceil(fps * static_cast<double>(duration.count()))
            );
            for (size_t i = 0; i < frames; ++i)
                result = this->frameAtFps(fps);
            return result;
        }
    };

    void requireValidTimestamps(const AdaptiveFramePlan& timestamps,
            const size_t capacity) {
        require(timestamps.size() <= capacity,
            "generated timestamps exceeded destination capacity");
        float previous = 0.0F;
        for (const float timestamp : timestamps) {
            require(timestamp > previous,
                "generated timestamps were not strictly increasing");
            require(timestamp < 1.0F,
                "generated timestamp reached or exceeded the real frame");
            previous = timestamp;
        }
    }

    class TraceFingerprint {
    public:
        void mix(const uint64_t value) {
            this->value ^= value;
            this->value *= 1099511628211ULL;
        }

        void mix(const std::string_view text) {
            for (const char character : text)
                this->mix(static_cast<unsigned char>(character));
            this->mix(0xffU);
        }

        [[nodiscard]] uint64_t get() const { return this->value; }

    private:
        uint64_t value{1469598103934665603ULL};
    };

    uint64_t characterizeTrace(const uint32_t targetFps,
            const size_t maximumMultiplier, const bool stableCadence,
            const AdaptiveRecoveryPolicy recoveryPolicy,
            const std::vector<std::chrono::nanoseconds>& intervals) {
        Harness harness(
            targetFps, maximumMultiplier, stableCadence, recoveryPolicy
        );
        harness.start();
        TraceFingerprint fingerprint;
        for (const auto interval : intervals) {
            const auto plan = harness.frame(interval);
            harness.scheduler.reportGeneratedFrameDelivery({
                .requested = plan.size(),
                .acceptedForPresentation = plan.size(),
            });
            fingerprint.mix(plan.size());
            for (const float timestamp : plan)
                fingerprint.mix(std::bit_cast<uint32_t>(timestamp));

            const auto snapshot = harness.scheduler.snapshot();
            fingerprint.mix(static_cast<uint8_t>(snapshot.phase));
            fingerprint.mix(snapshot.generationLimit);
            fingerprint.mix(snapshot.validatedGenerationLimit);
            fingerprint.mix(snapshot.stableCadenceLimit.has_value());
            fingerprint.mix(snapshot.stableCadenceLimit.value_or(0));
            fingerprint.mix(snapshot.historyWarmupRemaining);
            fingerprint.mix(snapshot.rampEvaluationActive);
            fingerprint.mix(snapshot.rearmRequired);
            fingerprint.mix(snapshot.discontinuityRecoveryActive);
        }
        for (const auto& event : harness.diagnostics.events) {
            fingerprint.mix(event.operation);
            fingerprint.mix(event.reason);
            fingerprint.mix(event.accepted);
            fingerprint.mix(event.previousLimit);
            fingerprint.mix(event.testedLimit);
        }
        return fingerprint.get();
    }

    std::vector<std::chrono::nanoseconds> constantTrace(
            const std::chrono::nanoseconds interval, const size_t frames) {
        return std::vector<std::chrono::nanoseconds>(frames, interval);
    }

    void testCharacterizationTraceCorpus() {
        const auto steady45 = characterizeTrace(
            90, 3, true, AdaptiveRecoveryPolicy::OrderedSdr,
            constantTrace(22'222'222ns, 540)
        );
        const auto boundary425 = characterizeTrace(
            90, 3, true, AdaptiveRecoveryPolicy::OrderedSdr,
            constantTrace(23'529'411ns, 510)
        );
        const auto boundary4275 = characterizeTrace(
            90, 3, true, AdaptiveRecoveryPolicy::OrderedSdr,
            constantTrace(23'391'812ns, 513)
        );
        const auto boundary43 = characterizeTrace(
            90, 3, true, AdaptiveRecoveryPolicy::OrderedSdr,
            constantTrace(23'255'813ns, 516)
        );

        std::vector<std::chrono::nanoseconds> disruptionTrace;
        disruptionTrace.reserve(640);
        constexpr std::array cadence{
            17ms, 16ms, 17ms, 16ms, 50ms, 16ms, 17ms, 200ms,
            16ms, 16ms, 17ms, 1ms, 1ms, 16ms, 33ms, 34ms,
        };
        for (size_t replay = 0; replay < 40; ++replay) {
            disruptionTrace.insert(
                disruptionTrace.end(), cadence.begin(), cadence.end()
            );
        }
        const auto disruptions = characterizeTrace(
            120, 4, true, AdaptiveRecoveryPolicy::ConservativeHdr,
            disruptionTrace
        );

        require(steady45 == 12777908042654023899ULL,
            "45-to-90 characterization changed: " +
                std::to_string(steady45));
        require(boundary425 == 8941383692458771123ULL,
            "42.5-to-90 characterization changed: " +
                std::to_string(boundary425));
        require(boundary4275 == 15981559191712361698ULL,
            "42.75-to-90 characterization changed: " +
                std::to_string(boundary4275));
        require(boundary43 == 8557601161360105282ULL,
            "43-to-90 characterization changed: " +
                std::to_string(boundary43));
        require(disruptions == 1580889166940170359ULL,
            "disruption characterization changed: " +
                std::to_string(disruptions));
    }

    void testStartupWarmupIsExplicit() {
        Harness harness(120, 3);
        require(harness.scheduler.historyWarmupRemaining() == 3,
            "Adaptive must start with three temporal-history frames");
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::HistoryWarmup,
            "startup history warm-up was not exposed as scheduler state");
        harness.start();
        require(!harness.scheduler.historyWarmupActive(),
            "startup history warm-up did not complete deterministically");
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::Stabilizing,
            "scheduler left startup stabilization too early");
    }

    void testBusyWarmupNotificationIsIdempotent() {
        Harness harness(120, 3);
        harness.scheduler.consumeHistoryWarmupFrame(harness.now + 16ms);
        require(harness.scheduler.historyWarmupRemaining() == 2,
            "precondition failed: initial warm-up did not advance");
        harness.scheduler.ensureHistoryWarmup(3, true);
        require(harness.scheduler.historyWarmupRemaining() == 2,
            "a repeated busy notification restarted active warm-up");
        harness.scheduler.consumeHistoryWarmupFrame(harness.now + 32ms);
        require(harness.scheduler.historyWarmupRemaining() == 1,
            "warm-up did not continue after an intermittent busy frame");
    }

    void testTransientBusyFrameDoesNotRearmCompletedWarmup() {
        Harness harness(120, 3);
        PipelineBusyRecovery pipelineBusy;

        for (size_t frame = 0;
                frame < AdaptiveScheduler::historyWarmupFrameCount(); ++frame) {
            harness.now += 16ms;
            harness.scheduler.consumeHistoryWarmupFrame(harness.now);

            const auto busy = pipelineBusy.reportBusy(harness.now + 8ms);
            if (busy.requestHistoryWarmup) {
                harness.scheduler.ensureHistoryWarmup(
                    AdaptiveScheduler::historyWarmupFrameCount(), true
                );
            }
            static_cast<void>(pipelineBusy.reportReady(harness.now + 16ms));
        }

        require(!harness.scheduler.historyWarmupActive(),
            "normal GPU overlap rearmed a completed Adaptive warm-up");
        require(harness.scheduler.historyWarmupRemaining() == 0,
            "Adaptive warm-up did not reach a generation-eligible state");
    }

    void testInvalidConfigurationIsRejectedAtBoundary() {
        bool invalidTargetRejected = false;
        try {
            AdaptiveScheduler scheduler({
                .targetFps = 0,
                .maximumMultiplier = 3,
                .generatedFrameCapacity = 3,
            });
            static_cast<void>(scheduler);
        } catch (const std::invalid_argument&) {
            invalidTargetRejected = true;
        }
        require(invalidTargetRejected,
            "scheduler accepted an invalid target FPS");

        bool invalidMultiplierRejected = false;
        try {
            AdaptiveScheduler scheduler({
                .targetFps = 120,
                .maximumMultiplier = 5,
                .generatedFrameCapacity = 3,
            });
            static_cast<void>(scheduler);
        } catch (const std::invalid_argument&) {
            invalidMultiplierRejected = true;
        }
        require(invalidMultiplierRejected,
            "scheduler accepted an invalid maximum multiplier");

        bool invalidCapacityRejected = false;
        try {
            AdaptiveScheduler scheduler({
                .targetFps = 120,
                .maximumMultiplier = 4,
                .generatedFrameCapacity = 4,
            });
            static_cast<void>(scheduler);
        } catch (const std::invalid_argument&) {
            invalidCapacityRejected = true;
        }
        require(invalidCapacityRejected,
            "scheduler accepted more than three generated-frame slots");
    }

    void testGeneratedDeliveryWindowContract() {
        GeneratedDeliveryWindow window;
        require(window.healthy(),
            "an empty delivery window should be healthy");

        window.record({.requested = 1, .acceptedForPresentation = 0});
        require(!window.healthy(),
            "complete loss in a short window was incorrectly tolerated");

        window.reset();
        window.record({.requested = 19, .acceptedForPresentation = 18});
        require(!window.healthy(),
            "short evaluation windows must require complete delivery");

        window.reset();
        window.record({.requested = 20, .acceptedForPresentation = 19});
        require(window.healthy(),
            "one isolated miss in a full window should be tolerated");
        window.record({.requested = 0, .acceptedForPresentation = 20});
        require(window.healthy(),
            "accepted-frame clamping changed delivery health");

        window.reset();
        window.record({.requested = 1, .acceptedForPresentation = 2});
        window.record({.requested = 1, .acceptedForPresentation = 0});
        require(window.acceptedForPresentation() == 1 && !window.healthy(),
            "surplus acceptance from one sample hid a later miss");

        window.reset();
        window.record({.requested = 20, .acceptedForPresentation = 18});
        require(!window.healthy(),
            "persistent delivery pressure was incorrectly tolerated");
    }

    void testSteadySixtyRampsToTwoXFor120Target() {
        Harness harness(120, 3);
        harness.start();
        const auto timestamps = harness.runAtFps(60.0, 7s);
        requireValidTimestamps(timestamps, 3);
        require(timestamps.size() == 1,
            "60 FPS toward 120 FPS should settle at one generated frame");
        requireNear(timestamps.front(), 0.5F, 0.0001F,
            "2x interpolation timestamp changed");
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.validatedGenerationLimit == 1,
            "2x level was not validated after its evaluation window");
        require(!snapshot.rampEvaluationActive,
            "steady 2x policy remained in a transient probe");
        require(snapshot.targetOutputClockActive &&
                std::abs(snapshot.targetOutputPhaseErrorOutputs) <= 0.5,
            "steady 2x output was not phase-bounded by the target clock");
    }

    void testFractionalTargetClockAssignsWorkToLongIntervals() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(60.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: fractional clock had no generated capacity");

        harness.scheduler.resetTiming(harness.now);
        constexpr auto shortInterval = 13ms;
        constexpr auto longInterval = 20'333'333ns;
        for (size_t pair = 0; pair < 20; ++pair) {
            harness.frame(shortInterval);
            harness.frame(longInterval);
        }
        size_t scheduledOutputs = 0;
        double elapsedSeconds = 0.0;
        std::vector<double> plannedIntervalsMilliseconds;
        std::vector<double> legacyPlannedIntervalsMilliseconds;
        plannedIntervalsMilliseconds.reserve(540);
        legacyPlannedIntervalsMilliseconds.reserve(540);

        for (size_t pair = 0; pair < 180; ++pair) {
            const auto shortPlan = harness.frame(shortInterval);
            const auto longPlan = harness.frame(longInterval);
            require(shortPlan.empty(),
                "fractional clock assigned the extra output to a short interval");
            require(longPlan.size() == 1,
                "fractional clock did not subdivide the longer interval");
            requireNear(longPlan.front(), 0.5F, 0.0001F,
                "fractional target clock changed the minimum-variance timestamp");

            scheduledOutputs += shortPlan.size() + longPlan.size() + 2;
            elapsedSeconds += std::chrono::duration<double>(
                shortInterval + longInterval
            ).count();
            plannedIntervalsMilliseconds.push_back(13.0);
            plannedIntervalsMilliseconds.push_back(20.333333 / 2.0);
            plannedIntervalsMilliseconds.push_back(20.333333 / 2.0);
            legacyPlannedIntervalsMilliseconds.push_back(13.0 / 2.0);
            legacyPlannedIntervalsMilliseconds.push_back(13.0 / 2.0);
            legacyPlannedIntervalsMilliseconds.push_back(20.333333);

            const auto snapshot = harness.scheduler.snapshot();
            require(snapshot.targetOutputClockActive &&
                    std::abs(snapshot.targetOutputPhaseErrorOutputs) <=
                        0.5 + 1e-9,
                "fractional target-clock phase escaped its half-output bound");
        }

        const double outputFps = static_cast<double>(scheduledOutputs) /
            elapsedSeconds;
        require(std::abs(outputFps - 90.0) < 0.001,
            "fractional target clock lost the requested long-term output rate");

        const auto populationStandardDeviation = [](const auto& samples) {
            double mean = 0.0;
            for (const double sample : samples)
                mean += sample;
            mean /= static_cast<double>(samples.size());
            double squaredDeviation = 0.0;
            for (const double sample : samples) {
                const double delta = sample - mean;
                squaredDeviation += delta * delta;
            }
            return std::sqrt(
                squaredDeviation / static_cast<double>(samples.size())
            );
        };
        const double standardDeviation = populationStandardDeviation(
            plannedIntervalsMilliseconds
        );
        const double legacyStandardDeviation = populationStandardDeviation(
            legacyPlannedIntervalsMilliseconds
        );
        require(standardDeviation < 1.5 &&
                standardDeviation < legacyStandardDeviation * 0.25,
            "noisy fractional plan retained excessive temporal-spacing variance");

        require(harness.diagnostics.latestPlan.has_value(),
            "fractional pacing aggregate was not reported");
        const auto& pacing = *harness.diagnostics.latestPlan;
        require(pacing.targetOutputClockActive &&
                pacing.pacingSourceSamples > 0 &&
                pacing.generatedCountChanges > 0,
            "fractional pacing aggregate omitted target-clock activity");
        require(pacing.requestedIntervalStdDevMilliseconds < 1.5 &&
                std::abs(pacing.requestedIntervalP99Milliseconds - 13.5) <
                    1e-6,
            "fractional pacing aggregate did not expose the improved outliers");
        require(pacing.targetPhaseErrorSamples == pacing.pacingSourceSamples,
            "fractional pacing aggregate omitted phase-error sample coverage");
        require(pacing.targetPhaseErrorMaximumMilliseconds <=
                1000.0 / 90.0 / 2.0 + 1e-6,
            "reported target-clock error exceeded half an output interval");
    }

    void testTargetClockCoversSteadyAndNoisyCadenceMatrix() {
        struct PacingCase {
            double baseFps;
            uint32_t targetFps;
        };
        constexpr std::array cases{
            PacingCase{45.0, 90},
            PacingCase{60.0, 90},
            PacingCase{60.0, 120},
            PacingCase{80.0, 120},
            PacingCase{90.0, 120},
        };

        for (const auto pacingCase : cases) {
            Harness harness(pacingCase.targetFps, 2);
            harness.start();
            harness.runAtFps(pacingCase.baseFps, 7s);
            require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
                "pacing matrix did not validate 2x capacity");

            size_t steadyOutputs = 0;
            bool steadySawRealOnly = false;
            bool steadySawGenerated = false;
            constexpr size_t steadyFrames = 240;
            for (size_t frame = 0; frame < steadyFrames; ++frame) {
                const auto plan = harness.frameAtFps(pacingCase.baseFps);
                requireValidTimestamps(plan, 1);
                steadyOutputs += plan.size() + 1;
                steadySawRealOnly |= plan.empty();
                steadySawGenerated |= !plan.empty();
            }
            const double steadyOutputFps =
                static_cast<double>(steadyOutputs) /
                (static_cast<double>(steadyFrames) / pacingCase.baseFps);
            require(std::abs(
                    steadyOutputFps -
                        static_cast<double>(pacingCase.targetFps)
                ) < 0.2,
                "steady pacing matrix lost its requested target average");
            const bool integerRelationship =
                (pacingCase.baseFps == 45.0 &&
                 pacingCase.targetFps == 90) ||
                (pacingCase.baseFps == 60.0 &&
                 pacingCase.targetFps == 120);
            if (integerRelationship) {
                require(!steadySawRealOnly && steadySawGenerated,
                    "integer pacing matrix did not retain constant 2x");
            } else {
                require(steadySawRealOnly && steadySawGenerated,
                    "fractional steady pacing did not exercise both counts");
            }

            harness.scheduler.resetTiming(harness.now);
            const auto meanInterval = std::chrono::duration<double>(
                1.0 / pacingCase.baseFps
            );
            const auto shortInterval =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    meanInterval * 0.9
                );
            const auto longInterval =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    meanInterval * 1.1
                );
            size_t scheduledOutputs = 0;
            double elapsedSeconds = 0.0;

            for (size_t pair = 0; pair < 240; ++pair) {
                for (const auto interval : {shortInterval, longInterval}) {
                    const auto plan = harness.frame(interval);
                    requireValidTimestamps(plan, 1);
                    scheduledOutputs += plan.size() + 1;
                    elapsedSeconds += std::chrono::duration<double>(
                        interval
                    ).count();
                    const auto snapshot = harness.scheduler.snapshot();
                    require(snapshot.targetOutputClockActive &&
                            std::abs(snapshot.targetOutputPhaseErrorOutputs) <=
                                0.5 + 1e-9,
                        "noisy pacing matrix escaped the target-clock phase bound");
                }
            }

            const double outputFps =
                static_cast<double>(scheduledOutputs) / elapsedSeconds;
            require(std::abs(
                    outputFps - static_cast<double>(pacingCase.targetFps)
                ) < 0.2,
                "noisy pacing matrix lost its requested target average");
        }
    }

    void testTargetClockHandlesMultiLevelFractionalCadence() {
        Harness harness(120, 3);
        harness.start();
        harness.runAtFps(45.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: multi-level test had no 3x capacity");

        constexpr std::chrono::nanoseconds shortInterval = 18'888'889ns;
        constexpr std::chrono::nanoseconds longInterval = 25'555'555ns;
        size_t scheduledOutputs = 0;
        double elapsedSeconds = 0.0;
        bool sawOneGeneratedFrame = false;
        bool sawTwoGeneratedFrames = false;
        bool sawDeferredOutput = false;
        for (size_t pair = 0; pair < 600; ++pair) {
            for (const auto interval : {shortInterval, longInterval}) {
                const auto plan = harness.frame(interval);
                requireValidTimestamps(plan, 2);
                sawOneGeneratedFrame |= plan.size() == 1;
                sawTwoGeneratedFrames |= plan.size() == 2;
                scheduledOutputs += plan.size() + 1;
                elapsedSeconds +=
                    std::chrono::duration<double>(interval).count();
                const auto snapshot = harness.scheduler.snapshot();
                sawDeferredOutput |=
                    snapshot.targetOutputDeferredBudgetOutput;
                require(snapshot.targetOutputClockActive &&
                        std::abs(snapshot.targetOutputPhaseErrorOutputs) <=
                            0.5 + 1e-9,
                    "multi-level fractional phase escaped its bound");
            }
        }

        require(sawOneGeneratedFrame && sawTwoGeneratedFrames,
            "multi-level fractional cadence did not exercise both counts");
        require(sawDeferredOutput,
            "multi-level fractional cadence did not exercise placement");
        require(std::abs(
                static_cast<double>(scheduledOutputs) / elapsedSeconds - 120.0
            ) < 0.2,
            "multi-level fractional cadence lost its target average");

        constexpr std::array<std::chrono::nanoseconds, 4> tailTrace{
            11ms, 18'333'333ns, 20'833'333ns, 20'833'334ns,
        };
        const auto tailTraceDuration =
            tailTrace[0] + tailTrace[1] + tailTrace[2] + tailTrace[3];
        const double tailBaseFps = 4.0 /
            std::chrono::duration<double>(tailTraceDuration).count();
        Harness tailHarness(120, 3);
        tailHarness.start();
        tailHarness.runAtFps(tailBaseFps, 10s);
        for (size_t frame = 0;
                frame < 120 && tailHarness.scheduler.snapshot().
                    targetOutputDeferredBudgetOutput;
                ++frame) {
            tailHarness.frameAtFps(tailBaseFps);
        }
        const auto initial = tailHarness.scheduler.snapshot();
        require(initial.validatedGenerationLimit == 2 &&
                !initial.targetOutputDeferredBudgetOutput,
            "precondition failed: tail guard had no 3x capacity");

        double legacyCredit = initial.targetOutputBudgetCreditOutputs;
        size_t actualOutputCount = 0;
        size_t legacyOutputCount = 0;
        double actualMaximumInterval = 0.0;
        double legacyMaximumInterval = 0.0;
        std::vector<double> actualIntervals;
        std::vector<double> legacyIntervals;
        actualIntervals.reserve(7'200);
        legacyIntervals.reserve(7'200);

        for (size_t cycle = 0; cycle < 600; ++cycle) {
            for (const auto interval : tailTrace) {
                const auto plan = tailHarness.frame(interval);
                requireValidTimestamps(plan, 2);
                const auto snapshot = tailHarness.scheduler.snapshot();
                legacyCredit += 120.0 / snapshot.smoothedBaseFps;
                const size_t requestedOutputs = std::max<size_t>(
                    1,
                    static_cast<size_t>(std::floor(legacyCredit + 1e-9))
                );
                const size_t baselineOutputs = std::min<size_t>(
                    requestedOutputs, 3
                );
                legacyCredit -= static_cast<double>(baselineOutputs);
                if (legacyCredit < 0.0)
                    legacyCredit = 0.0;
                if (baselineOutputs == 3 && legacyCredit >= 1.0)
                    legacyCredit = std::fmod(legacyCredit, 1.0);

                const size_t actualOutputs = plan.size() + 1;
                actualOutputCount += actualOutputs;
                legacyOutputCount += baselineOutputs;
                require(actualOutputCount <= legacyOutputCount &&
                        legacyOutputCount - actualOutputCount <= 1,
                    "multi-level placement changed the workload envelope");

                const double intervalMilliseconds =
                    std::chrono::duration<double, std::milli>(interval).count();
                const double actualSpacing = intervalMilliseconds /
                    static_cast<double>(actualOutputs);
                const double legacySpacing = intervalMilliseconds /
                    static_cast<double>(baselineOutputs);
                actualMaximumInterval = std::max(
                    actualMaximumInterval, actualSpacing
                );
                legacyMaximumInterval = std::max(
                    legacyMaximumInterval, legacySpacing
                );
                require(actualMaximumInterval <=
                        legacyMaximumInterval + 1e-9,
                    "multi-level placement worsened the requested maximum");
                for (size_t output = 0; output < actualOutputs; ++output)
                    actualIntervals.push_back(actualSpacing);
                for (size_t output = 0; output < baselineOutputs; ++output)
                    legacyIntervals.push_back(legacySpacing);
            }
        }

        std::sort(actualIntervals.begin(), actualIntervals.end());
        std::sort(legacyIntervals.begin(), legacyIntervals.end());
        const auto percentile = [](const std::vector<double>& samples,
                const double quantile) {
            const size_t rank = std::max<size_t>(
                1,
                static_cast<size_t>(std::ceil(
                    quantile * static_cast<double>(samples.size())
                ))
            );
            return samples[rank - 1];
        };
        require(percentile(actualIntervals, 0.95) <=
                percentile(legacyIntervals, 0.95) + 1e-9 &&
                percentile(actualIntervals, 0.99) <=
                    percentile(legacyIntervals, 0.99) + 1e-9,
            "multi-level placement worsened requested tail intervals");
    }

    void testTargetClockPreservesNoisyIntegerCeiling() {
        Harness harness(120, 2);
        harness.start();
        harness.runAtFps(60.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: noisy integer test had no 2x capacity");

        constexpr std::chrono::nanoseconds shortInterval = 10ms;
        constexpr std::chrono::nanoseconds longInterval = 23'333'333ns;
        for (size_t pair = 0; pair < 20; ++pair) {
            harness.frame(shortInterval);
            harness.frame(longInterval);
        }

        size_t scheduledOutputs = 0;
        double elapsedSeconds = 0.0;
        for (size_t pair = 0; pair < 240; ++pair) {
            for (const auto interval : {shortInterval, longInterval}) {
                const auto plan = harness.frame(interval);
                require(plan.size() == 1,
                    "raw placement reduced an achievable noisy 2x cadence");
                scheduledOutputs += plan.size() + 1;
                elapsedSeconds +=
                    std::chrono::duration<double>(interval).count();
                require(std::abs(
                        harness.scheduler.snapshot().
                            targetOutputPhaseErrorOutputs
                    ) <= 0.5 + 1e-9,
                    "noisy integer target phase escaped its bounded residual");
            }
        }
        require(std::abs(
                static_cast<double>(scheduledOutputs) / elapsedSeconds - 120.0
            ) < 0.001,
            "noisy achievable 2x cadence lost the target average");
    }

    void testDeferredOutputRepaymentPreservesPacingBenefit() {
        constexpr std::array traces{
            std::array<std::chrono::nanoseconds, 3>{
                27'777'778ns, 13'333'333ns, 8'888'889ns,
            },
            std::array<std::chrono::nanoseconds, 3>{
                23'333'333ns, 23'333'333ns, 8'888'889ns,
            },
        };

        for (const auto& trace : traces) {
            const auto traceDuration = trace[0] + trace[1] + trace[2];
            const double baseFps = 3.0 /
                std::chrono::duration<double>(traceDuration).count();
            Harness harness(90, 2);
            harness.start();
            harness.runAtFps(baseFps, 7s);
            const auto initial = harness.scheduler.snapshot();
            require(initial.validatedGenerationLimit == 1 &&
                    !initial.targetOutputDeferredBudgetOutput,
                "precondition failed: irregular trace had stale placement debt");

            double legacyCredit = initial.targetOutputBudgetCreditOutputs;
            double actualSquaredError = 0.0;
            double legacySquaredError = 0.0;
            size_t actualOutputs = 0;
            size_t legacyOutputs = 0;
            bool sawDeferredOutput = false;
            bool sawRepaidOutput = false;
            bool deferredOutputPending = false;
            constexpr double targetIntervalMilliseconds = 1000.0 / 90.0;

            for (size_t cycle = 0; cycle < 600; ++cycle) {
                for (const auto interval : trace) {
                    const auto plan = harness.frame(interval);
                    const auto snapshot = harness.scheduler.snapshot();
                    require(snapshot.targetOutputClockActive &&
                            snapshot.smoothedBaseFps > 0.0,
                        "irregular trace left active fractional scheduling");

                    legacyCredit += 90.0 / snapshot.smoothedBaseFps;
                    const size_t requestedOutputs = std::max<size_t>(
                        1,
                        static_cast<size_t>(std::floor(
                            legacyCredit + 1e-9
                        ))
                    );
                    const size_t baselineOutputs = std::min<size_t>(
                        requestedOutputs, 2
                    );
                    legacyCredit -= static_cast<double>(baselineOutputs);
                    if (legacyCredit < 0.0)
                        legacyCredit = 0.0;
                    if (baselineOutputs == 2 && legacyCredit >= 1.0)
                        legacyCredit = std::fmod(legacyCredit, 1.0);

                    const size_t scheduledOutputs = plan.size() + 1;
                    actualOutputs += scheduledOutputs;
                    legacyOutputs += baselineOutputs;
                    require(actualOutputs <= legacyOutputs &&
                            legacyOutputs - actualOutputs <= 1,
                        "raw placement changed the smoothed workload envelope");
                    sawDeferredOutput |=
                        snapshot.targetOutputDeferredBudgetOutput;
                    const bool repaidOutput = deferredOutputPending &&
                        !snapshot.targetOutputDeferredBudgetOutput;
                    sawRepaidOutput |= repaidOutput;
                    if (repaidOutput) {
                        require(actualOutputs == legacyOutputs,
                            "deferred repayment did not restore baseline work");
                    }
                    deferredOutputPending =
                        snapshot.targetOutputDeferredBudgetOutput;

                    const double intervalMilliseconds =
                        std::chrono::duration<double, std::milli>(
                            interval
                        ).count();
                    const auto squaredError = [intervalMilliseconds](
                            const size_t outputCount) {
                        const double error = intervalMilliseconds /
                            static_cast<double>(outputCount) -
                            targetIntervalMilliseconds;
                        return static_cast<double>(outputCount) *
                            error * error;
                    };
                    actualSquaredError += squaredError(scheduledOutputs);
                    legacySquaredError += squaredError(baselineOutputs);
                    require(actualSquaredError <=
                            legacySquaredError + 1e-6,
                        "placement worsened a prefix of requested pacing");
                }
            }

            require(sawDeferredOutput,
                "irregular trace did not exercise deferred placement");
            require(sawRepaidOutput,
                "irregular trace never repaid a deferred output");
            require(legacyOutputs - actualOutputs <= 1,
                "deferred placement lost more than one bounded output");
            require(actualSquaredError <= legacySquaredError + 1e-6,
                "deferred repayment made requested pacing worse than baseline");
        }
    }

    void testTargetClockResetClearsDeferredOutput() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(60.0, 7s);

        constexpr std::array<std::chrono::nanoseconds, 3> trace{
            27'777'778ns, 13'333'333ns, 8'888'889ns,
        };
        bool createdDeferredOutput = false;
        for (size_t cycle = 0;
                cycle < 120 && !createdDeferredOutput; ++cycle) {
            for (const auto interval : trace) {
                harness.frame(interval);
                if (harness.scheduler.snapshot().
                        targetOutputDeferredBudgetOutput) {
                    createdDeferredOutput = true;
                    break;
                }
            }
        }
        require(createdDeferredOutput,
            "precondition failed: reset test created no deferred output");

        harness.scheduler.resetTiming(harness.now);
        const auto reset = harness.scheduler.snapshot();
        require(!reset.targetOutputClockActive &&
                reset.targetOutputBudgetCreditOutputs == 0.0 &&
                reset.targetOutputPhaseErrorOutputs == 0.0 &&
                !reset.targetOutputDeferredBudgetOutput,
            "timing reset retained fractional placement state");
    }

    void testRawPlacementCannotMintGeneratedWork() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(60.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: placement guard had no generated capacity");

        harness.scheduler.resetTiming(harness.now);
        require(harness.frame(10ms).empty(),
            "above-target source interval unexpectedly generated a frame");
        const auto delayedIntervalPlan = harness.frame(19ms);
        require(delayedIntervalPlan.empty(),
            "raw interval minted work before the smoothed budget earned it");
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.targetOutputClockActive &&
                snapshot.targetOutputBudgetCreditOutputs > 0.0 &&
                snapshot.targetOutputBudgetCreditOutputs < 1.0,
            "fractional placement clock did not activate for the delayed interval");
    }

    void testTargetClockDiscardsUnreachableCeilingDebt() {
        Harness harness(240, 3);
        harness.start();
        const auto saturatedPlan = harness.runAtFps(60.0, 10s);
        require(saturatedPlan.size() == 2 &&
                harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: target clock did not reach its 3x ceiling");
        require(harness.scheduler.snapshot().
                    targetOutputBudgetCreditOutputs < 1.0,
            "unreachable ceiling accumulated whole-output budget debt");

        harness.scheduler.resetTiming(harness.now);
        for (size_t frame = 0; frame < 120; ++frame) {
            const auto recoveredPlan = harness.frameAtFps(120.0);
            require(recoveredPlan.size() <= 1,
                "unreachable target debt produced a catch-up output after recovery");
            require(std::abs(
                    harness.scheduler.snapshot().targetOutputPhaseErrorOutputs
                ) <= 0.5 + 1e-9,
                "ceiling saturation retained whole-output target debt");
        }
    }

    void testPacingDiagnosticsRestartAfterPolicyGap() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(60.0, 7s);

        const size_t initialPlanCount = harness.diagnostics.plans.size();
        for (size_t frame = 0;
                frame < 120 &&
                    harness.diagnostics.plans.size() == initialPlanCount;
                ++frame) {
            harness.frameAtFps(60.0);
        }
        require(harness.diagnostics.plans.size() > initialPlanCount,
            "precondition failed: no baseline pacing aggregate was emitted");

        for (size_t frame = 0; frame < 30; ++frame)
            harness.frameAtFps(60.0);
        harness.diagnostics.plans.clear();
        harness.frameAtFps(60.0, true);

        size_t postGapFrames = 0;
        while (harness.diagnostics.plans.empty() && postGapFrames < 120) {
            harness.frameAtFps(60.0);
            postGapFrames++;
        }
        require(!harness.diagnostics.plans.empty(),
            "no pacing aggregate was emitted after acquisition bypass");
        const auto& postGap = harness.diagnostics.plans.front();
        require(postGap.pacingSourceSamples > 0 &&
                postGap.pacingSourceSamples <= postGapFrames,
            "post-bypass aggregate retained pre-bypass pacing samples");
        require(postGap.targetPhaseErrorSamples ==
                postGap.pacingSourceSamples,
            "post-bypass phase RMS mixed a different policy window");
    }

    void testIsolatedGeneratedFrameMissDoesNotRejectRamp() {
        Harness harness(120, 2);
        harness.start();
        bool reportedMiss = false;
        for (size_t frame = 0; frame < 600; ++frame) {
            const auto plan = harness.frameAtFps(60.0);
            if (harness.scheduler.snapshot().rampEvaluationActive &&
                    !reportedMiss && !plan.empty()) {
                harness.scheduler.reportGeneratedFrameDelivery({
                    .requested = plan.size(),
                    .acceptedForPresentation = plan.size() - 1,
                });
                reportedMiss = true;
            } else {
                harness.scheduler.reportGeneratedFrameDelivery({
                    .requested = plan.size(),
                    .acceptedForPresentation = plan.size(),
                });
            }
            if (reportedMiss && harness.diagnostics.contains("ramp-result"))
                break;
        }
        const auto* result = harness.diagnostics.last("ramp-result");
        require(reportedMiss && result && result->accepted,
            "one isolated compositor admission miss rejected a healthy ramp");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "healthy delivery with one isolated miss was not validated");
    }

    void testPersistentGeneratedFrameMissesRejectRamp() {
        Harness harness(120, 2);
        harness.start();
        bool reportedPressure = false;
        for (size_t frame = 0; frame < 600; ++frame) {
            const auto plan = harness.frameAtFps(60.0);
            if (harness.scheduler.snapshot().rampEvaluationActive &&
                    !plan.empty()) {
                harness.scheduler.reportGeneratedFrameDelivery({
                    .requested = plan.size(),
                    .acceptedForPresentation = 0,
                });
                reportedPressure = true;
            } else {
                harness.scheduler.reportGeneratedFrameDelivery({
                    .requested = plan.size(),
                    .acceptedForPresentation = plan.size(),
                });
            }
            if (reportedPressure && harness.diagnostics.contains("ramp-result"))
                break;
        }
        const auto* result = harness.diagnostics.last("ramp-result");
        require(reportedPressure && result && !result->accepted,
            "persistent compositor admission loss did not reject the ramp");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 0,
            "persistent delivery loss was retained as a validated multiplier");
    }

    void testFourXPlanUsesEvenInterpolationTimestamps() {
        Harness harness(120, 4);
        harness.start();
        const auto timestamps = harness.runAtFps(30.0, 10s);
        requireValidTimestamps(timestamps, 3);
        require(timestamps.size() == 3,
            "30 FPS toward 120 FPS did not settle at the 4x ceiling");
        requireNear(timestamps[0], 0.25F, 0.0001F,
            "first 4x interpolation timestamp changed");
        requireNear(timestamps[1], 0.50F, 0.0001F,
            "second 4x interpolation timestamp changed");
        requireNear(timestamps[2], 0.75F, 0.0001F,
            "third 4x interpolation timestamp changed");
    }

    void testSchedulerCannotReduceAboveTargetCadence() {
        Harness harness(120, 4);
        harness.start();
        const auto timestamps = harness.runAtFps(144.0, 6s);
        require(timestamps.empty(),
            "scheduler generated frames when real cadence exceeded target");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 0,
            "above-target cadence raised the validated generation level");
        require(!harness.scheduler.snapshot().targetOutputClockActive,
            "above-target cadence retained fractional target-clock debt");
    }

    void testAcquireBackoffDoesNotAdvancePolicy() {
        Harness harness(120, 3);
        harness.start();
        harness.runAtFps(60.0, 7s);
        const auto before = harness.scheduler.snapshot();
        for (size_t i = 0; i < 120; ++i) {
            const auto timestamps = harness.frameAtFps(60.0, true);
            require(timestamps.size() == 1,
                "acquire backoff must perform exactly one availability probe");
            requireNear(timestamps.front(), 0.5F, 0.0001F,
                "acquire-backoff probe timestamp changed");
        }
        const auto after = harness.scheduler.snapshot();
        require(after.generationLimit == before.generationLimit,
            "acquire backoff advanced generation policy");
        require(after.validatedGenerationLimit ==
                before.validatedGenerationLimit,
            "acquire backoff changed the validated level");
        require(!after.targetOutputClockActive,
            "acquire backoff retained a stale fractional target phase");
    }

    void testValidatedTwoXSurvivesShortGameplayHitch() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(45.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: 2x was not validated");

        const auto hitchPlan = harness.frame(200ms);
        require(hitchPlan.empty(),
            "short hitch recovery generated from stale temporal history");
        require(harness.scheduler.historyWarmupRemaining() == 3,
            "short 2x hitch did not request a full history refresh");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "short 2x hitch discarded its validated policy");
        require(harness.diagnostics.contains("2x-hitch-recovery"),
            "short 2x hitch recovery was not observable");
    }

    // On the Gamescope HDR transport a long gap is ambiguous: it can be a
    // menu/focus transition, a colour-pipeline transition, or compositor
    // admission pressure. Do not resume generated work until cadence is proven.
    void testHdrLongHitchUsesConservativeDiscontinuityRecovery() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(45.0, 7s);
        const auto hitchPlan = harness.frame(400ms);
        require(hitchPlan.empty(),
            "long hitch unexpectedly generated a frame");
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.discontinuityRecoveryActive,
            "long hitch bypassed menu/focus discontinuity recovery");
        require(snapshot.generationLimit == 0,
            "long hitch retained generated load during stabilization");
    }

    void testRecoveredCadenceRestoresValidatedLevel() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(45.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: 2x was not validated");
        harness.frame(400ms);
        require(harness.scheduler.discontinuityRecoveryActive(),
            "precondition failed: long hitch did not start recovery");

        harness.runAtFps(45.0, 3s);
        const auto snapshot = harness.scheduler.snapshot();
        require(!snapshot.discontinuityRecoveryActive,
            "healthy cadence did not complete discontinuity recovery");
        require(snapshot.validatedGenerationLimit == 1,
            "healthy cadence did not restore the proven 2x level");
        require(harness.frameAtFps(45.0).size() == 1,
            "restored 2x policy did not resume generation");
    }

    void testDiscontinuityRecoveryTimesOutToFreshRamp() {
        Harness harness(90, 2);
        harness.start();
        harness.runAtFps(45.0, 7s);
        harness.frame(400ms);
        require(harness.scheduler.discontinuityRecoveryActive(),
            "precondition failed: long hitch did not start recovery");

        for (size_t frame = 0;
                frame < 160 &&
                    !harness.diagnostics.contains("discontinuity-complete");
                ++frame) {
            harness.frameAtFps(20.0);
        }
        const auto* completion = harness.diagnostics.last(
            "discontinuity-complete"
        );
        require(completion && completion->reason == "timeout-ramp-from-zero",
            "unrecovered cadence did not take the bounded timeout path");
        const auto snapshot = harness.scheduler.snapshot();
        require(!snapshot.discontinuityRecoveryActive,
            "expired discontinuity recovery remained active");
        require(snapshot.generationLimit == 0,
            "expired discontinuity recovery retained stale generated load");
    }

    void testSustainedCadenceDropRebasesWithoutMenuRecovery() {
        Harness harness(120, 3);
        harness.start();
        harness.runAtFps(60.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: 2x was not validated");

        harness.frameAtFps(30.0);
        harness.frameAtFps(30.0);
        harness.frameAtFps(30.0);
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.phase == AdaptiveSchedulerPhase::Stabilizing,
            "sustained gameplay cadence drop did not rebase through stabilization");
        require(!snapshot.discontinuityRecoveryActive,
            "ordinary gameplay cadence drop was mistaken for a menu discontinuity");
        require(snapshot.generationLimit == 0,
            "cadence rebase retained load before measuring the new scene");
    }

    void testSdrLongHitchRefreshesHistoryWithoutDroppingValidatedLevel() {
        // Ordered FIFO delivery makes an SDR hitch a temporal-history problem,
        // not evidence that the previously validated multiplier is unsafe.
        Harness harness(90, 2, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(45.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: SDR 2x was not validated");

        require(harness.frame(400ms).empty(),
            "SDR cadence refresh generated from stale history");
        auto snapshot = harness.scheduler.snapshot();
        require(!snapshot.discontinuityRecoveryActive,
            "ordinary SDR hitch entered HDR-style discontinuity recovery");
        require(snapshot.validatedGenerationLimit == 1,
            "ordinary SDR hitch discarded the validated 2x level");
        require(snapshot.historyWarmupRemaining == 2,
            "ordinary SDR hitch did not request the short history refresh");
        const auto* refresh = harness.diagnostics.last("cadence-refresh");
        require(refresh && refresh->reason == "cadence-stall" &&
                refresh->previousLimit == 1 && refresh->testedLimit == 2,
            "SDR cadence refresh was not reported accurately");

        harness.now += 22ms;
        harness.scheduler.consumeHistoryWarmupFrame(harness.now);
        harness.now += 22ms;
        harness.scheduler.consumeHistoryWarmupFrame(harness.now);
        require(harness.frameAtFps(45.0).empty(),
            "SDR resumed before recovered cadence was confirmed");
        require(harness.frameAtFps(45.0).empty(),
            "SDR resumed before three healthy cadence samples");
        const auto thirdResumedPlan = harness.frameAtFps(45.0);
        const auto fourthResumedPlan = harness.frameAtFps(45.0);
        require(thirdResumedPlan.size() == 1 || fourthResumedPlan.size() == 1,
            "SDR did not resume its validated 2x level after confirmed recovery");
    }

    void testSdrStableTwoXBridgesIsolatedGameplayHitch() {
        Harness harness(
            120, 3, true, AdaptiveRecoveryPolicy::OrderedSdr
        );
        harness.start();
        harness.runAtFps(60.0, 12s);
        const auto before = harness.scheduler.snapshot();
        require(before.phase == AdaptiveSchedulerPhase::StableCadence &&
                before.stableCadenceLimit == 1 &&
                before.validatedGenerationLimit == 1,
            "precondition failed: SDR Smooth Cadence 2x was not accepted");

        const auto hitchPlan = harness.frame(150ms);
        require(hitchPlan.size() == 1,
            "isolated SDR gameplay hitch dropped the generated midpoint");
        requireNear(hitchPlan.front(), 0.5F, 0.0001F,
            "isolated SDR gameplay hitch changed the midpoint timestamp");
        require(!harness.scheduler.historyWarmupActive(),
            "isolated SDR gameplay hitch unnecessarily refreshed history");
        const auto* bridge = harness.diagnostics.last("sdr-hitch-bridge");
        require(bridge && bridge->testedLimit == 1,
            "isolated SDR gameplay hitch bridge was not observable");

        require(harness.frameAtFps(60.0).size() == 1,
            "healthy frame after an isolated SDR hitch lost interpolation");
        require(harness.frame(150ms).size() == 1,
            "a later isolated SDR hitch did not rearm after healthy cadence");
        require(harness.diagnostics.count("cadence-refresh") == 0,
            "isolated SDR gameplay hitches entered cadence recovery");
    }

    void testSdrCadenceDropRefreshesHistoryWithoutFullStabilization() {
        Harness harness(120, 3, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(60.0, 7s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: SDR 2x was not validated");

        harness.frameAtFps(30.0);
        harness.frameAtFps(30.0);
        harness.frameAtFps(30.0);
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.phase == AdaptiveSchedulerPhase::HistoryWarmup,
            "SDR cadence drop did not use a short history refresh");
        require(snapshot.validatedGenerationLimit == 1,
            "SDR cadence drop discarded the validated multiplier");
        require(!snapshot.discontinuityRecoveryActive,
            "SDR cadence drop entered discontinuity recovery");
        const auto* refresh = harness.diagnostics.last("cadence-refresh");
        require(refresh && refresh->reason == "cadence-drop",
            "SDR cadence-drop refresh was not observable");
    }

    void testSdrSustainedHardStallDoesNotRestartHistoryRefresh() {
        Harness harness(120, 2, true, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(60.0, 12s);
        require(harness.scheduler.snapshot().phase ==
                    AdaptiveSchedulerPhase::StableCadence &&
                harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: SDR Smooth Cadence 2x was not accepted");

        // A hard interval starts the short SDR refresh, then the following real
        // frames remain too slow for generation. They must remain
        // native/history-only rather than scheduling another two-frame refresh
        // indefinitely.
        require(harness.frame(150ms).size() == 1,
            "first short SDR stall did not use the isolated-hitch bridge");
        require(harness.frame(150ms).empty(),
            "consecutive SDR stall bypassed history recovery");
        require(harness.diagnostics.count("sdr-hitch-bridge") == 1,
            "consecutive SDR stalls used more than one isolated-hitch bridge");
        for (size_t frame = 0; frame < 2; ++frame) {
            harness.now += 150ms;
            harness.scheduler.consumeHistoryWarmupFrame(harness.now);
        }
        for (size_t frame = 0; frame < 8; ++frame) {
            require(harness.frame(150ms).empty(),
                "sustained hard SDR stall generated output");
        }
        require(harness.diagnostics.count("cadence-refresh") == 1,
            "sustained hard SDR stall restarted the history refresh");
        require(!harness.scheduler.historyWarmupActive(),
            "sustained hard SDR stall left history warm-up active");

        // Hovering around the old 10-FPS boundary must neither restart the
        // refresh nor count as a confirmed recovery. This is the remaining
        // boundary chatter that remains after the refresh-loop fix.
        for (size_t frame = 0; frame < 4; ++frame) {
            require(harness.frame(90ms).empty(),
                "near-boundary SDR cadence resumed generation");
            require(harness.frame(110ms).empty(),
                "near-boundary SDR cadence generated output");
        }
        require(harness.diagnostics.count("cadence-refresh") == 1,
            "near-boundary SDR cadence restarted the history refresh");

        require(harness.frameAtFps(60.0).empty(),
            "SDR resumed on the first healthy recovery sample");
        require(harness.frameAtFps(60.0).empty(),
            "SDR resumed before recovery hysteresis completed");
        const auto thirdRecoveredPlan = harness.frameAtFps(60.0);
        const auto fourthRecoveredPlan = harness.frameAtFps(60.0);
        require(thirdRecoveredPlan.size() == 1 ||
                fourthRecoveredPlan.size() == 1,
            "SDR did not resume its retained 2x level after confirmed recovery");
    }

    void testImpossibleFastBurstDoesNotCorruptCadence() {
        Harness harness(120, 3);
        harness.start();
        harness.runAtFps(60.0, 7s);
        const auto before = harness.scheduler.snapshot();
        for (size_t i = 0; i < 30; ++i)
            require(harness.frame(1ms).empty(),
                "impossible fast-present burst generated interpolation work");
        harness.frameAtFps(60.0);
        const auto after = harness.scheduler.snapshot();
        require(after.validatedGenerationLimit ==
                before.validatedGenerationLimit,
            "fast-present burst corrupted the proven generation level");
        require(after.smoothedBaseFps < 80.0,
            "fast-present burst polluted the gameplay cadence estimate");
        require(harness.diagnostics.contains("fast-burst-complete"),
            "fast-present burst completion was not observable");
    }

    void testRejectedFirstProbeEntersBoundedRearm() {
        Harness harness(180, 2);
        harness.start();
        for (size_t frame = 0;
                frame < 600 &&
                    !harness.scheduler.snapshot().rampEvaluationActive;
                ++frame) {
            harness.frameAtFps(60.0);
        }
        require(harness.scheduler.snapshot().rampEvaluationActive,
            "precondition failed: initial multiplier probe did not begin");
        // 34 FPS is slow enough to make 2x counterproductive against the
        // 60 FPS baseline, but not slow enough to trip the separate 2x raw
        // cadence-discontinuity detector before the one-second probe ends.
        harness.runAtFps(34.0, 2s);
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.rearmRequired,
            "harmful first multiplier probe did not enter rearm cooldown");
        require(snapshot.validatedGenerationLimit == 0,
            "rejected first probe was incorrectly treated as validated");
        const auto* result = harness.diagnostics.last("ramp-result");
        require(result && !result->accepted,
            "rejected probe result was not emitted deterministically");
    }

    void testInterruptedProbeRearmsWithoutFailurePenalty() {
        Harness harness(180, 3);
        harness.start();
        for (size_t frame = 0;
                frame < 600 &&
                    !harness.scheduler.snapshot().rampEvaluationActive;
                ++frame) {
            harness.frameAtFps(60.0);
        }
        require(harness.scheduler.snapshot().rampEvaluationActive,
            "precondition failed: multiplier probe did not begin");

        harness.frame(400ms);
        auto snapshot = harness.scheduler.snapshot();
        require(snapshot.rearmRequired,
            "interrupted probe did not enter bounded rearm");
        const auto* scheduled = harness.diagnostics.last(
            "adaptive-rearm-scheduled"
        );
        require(scheduled && scheduled->reason == "probe-interrupted",
            "interrupted probe was recorded as a throughput rejection");

        harness.runAtFps(60.0, 4s);
        snapshot = harness.scheduler.snapshot();
        require(!snapshot.rearmRequired,
            "stable cadence did not rearm an interrupted probe promptly");
        const auto* ready = harness.diagnostics.last("adaptive-rearm-ready");
        require(ready && ready->reason == "probe-interrupted",
            "interrupted probe rearm decision was not observable");
    }

    void testBridgeProbeCanRecoverMisleadingFirstStep() {
        Harness harness(180, 3);
        harness.start();
        for (size_t frame = 0;
                frame < 600 &&
                    !harness.scheduler.snapshot().rampEvaluationActive;
                ++frame) {
            harness.frameAtFps(60.0);
        }
        require(harness.scheduler.snapshot().rampEvaluationActive,
            "precondition failed: initial multiplier probe did not begin");

        harness.runAtFps(31.0, 3s);
        require(harness.diagnostics.contains("bridge"),
            "counterproductive-looking first step did not start a bridge probe");
        const auto* result = harness.diagnostics.last("bridge-result");
        require(result && result->accepted,
            "useful bridge multiplier was not accepted");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "accepted bridge multiplier was not retained as validated");
    }

    void testRejectedHigherLevelRetainsProvenLoadAndBacksOff() {
        Harness harness(180, 3);
        harness.start();
        for (size_t frame = 0;
                frame < 900;
                ++frame) {
            harness.frameAtFps(60.0);
            const auto snapshot = harness.scheduler.snapshot();
            if (snapshot.rampEvaluationActive &&
                    snapshot.generationLimit == 2 &&
                    snapshot.validatedGenerationLimit == 1) {
                break;
            }
        }
        auto snapshot = harness.scheduler.snapshot();
        require(snapshot.rampEvaluationActive &&
                snapshot.generationLimit == 2 &&
                snapshot.validatedGenerationLimit == 1,
            "precondition failed: higher-level probe did not begin from proven 2x");

        harness.runAtFps(35.0, 2s);
        snapshot = harness.scheduler.snapshot();
        require(!snapshot.rampEvaluationActive,
            "counterproductive higher-level probe did not finish");
        require(snapshot.validatedGenerationLimit == 1,
            "rejected higher-level probe discarded the proven 2x load");
        const auto* backoff = harness.diagnostics.last("ramp-backoff");
        require(backoff && backoff->testedLimit == 2 &&
                backoff->previousLimit == 1,
            "higher-level rejection did not schedule first bounded retry delay");
    }

    void testSmoothCadenceSettlesNearIntegerDemand() {
        Harness harness(90, 2, true);
        harness.start();
        const auto timestamps = harness.runAtFps(47.0, 12s);
        require(timestamps.size() == 1,
            "Smooth Cadence did not settle on constant 2x output");
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::StableCadence,
            "accepted Smooth Cadence was not exposed as scheduler state");
        require(harness.diagnostics.contains(
                "adaptive-stable-cadence-accepted"),
            "Smooth Cadence acceptance was not observable");
    }

    void testSmoothCadenceRetainsValidatedTwoXThroughMildDip() {
        Harness harness(120, 2, true);
        harness.start();
        harness.runAtFps(60.0, 12s);
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::StableCadence,
            "precondition failed: 60 FPS did not settle on 2x for a 120 FPS target");

        const auto plan = harness.runAtFps(57.1, 3s);
        const auto snapshot = harness.scheduler.snapshot();
        require(snapshot.phase == AdaptiveSchedulerPhase::StableCadence &&
                snapshot.stableCadenceLimit == 1,
            "validated 2x cadence was discarded during a mild base-rate dip");
        require(plan.size() == 1,
            "mild base-rate dip resumed fractional generated-frame counts");
    }

    void testSmoothCadenceExitsBelowRetentionRange() {
        Harness harness(120, 2, true);
        harness.start();
        harness.runAtFps(60.0, 12s);
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::StableCadence,
            "precondition failed: 60 FPS did not settle on 2x for a 120 FPS target");

        harness.runAtFps(56.5, 3s);
        require(harness.scheduler.snapshot().phase !=
                AdaptiveSchedulerPhase::StableCadence,
            "Smooth Cadence remained locked below its retention range");
        const auto* disabled = harness.diagnostics.last(
            "adaptive-stable-cadence-disabled"
        );
        require(disabled && disabled->reason == "outside-useful-range",
            "retention-range exit did not report the expected reason");
    }

    void testPersistentDeliveryLossRejectsSmoothCadenceProbe() {
        Harness harness(90, 2, true);
        harness.start();
        bool reportedPressure = false;
        for (size_t frame = 0; frame < 1000; ++frame) {
            const auto plan = harness.frameAtFps(47.0);
            const bool probeStarted = harness.diagnostics.contains(
                "adaptive-stable-cadence-probe"
            );
            if (probeStarted && !plan.empty()) {
                harness.scheduler.reportGeneratedFrameDelivery({
                    .requested = plan.size(),
                    .acceptedForPresentation = 0,
                });
                reportedPressure = true;
            } else {
                harness.scheduler.reportGeneratedFrameDelivery({
                    .requested = plan.size(),
                    .acceptedForPresentation = plan.size(),
                });
            }
            if (reportedPressure && harness.diagnostics.contains(
                    "adaptive-stable-cadence-rejected"))
                break;
        }
        require(reportedPressure && harness.diagnostics.contains(
                "adaptive-stable-cadence-rejected"),
            "persistent display admission loss did not reject Smooth Cadence");
        require(harness.scheduler.snapshot().phase !=
                AdaptiveSchedulerPhase::StableCadence,
            "Smooth Cadence retained a persistently delivery-late multiplier");
    }

    void testSmoothCadenceReturnsToTargetAfterBaseRecovery() {
        Harness harness(100, 2, true);
        harness.start();
        harness.runAtFps(50.0, 12s);
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::StableCadence,
            "precondition failed: 50 FPS did not settle on 2x for a 100 FPS target");

        harness.runAtFps(60.0, 3s);
        require(harness.scheduler.snapshot().phase !=
                AdaptiveSchedulerPhase::StableCadence,
            "Smooth Cadence retained stale 2x output after native cadence recovered");
        const auto* disabled = harness.diagnostics.last(
            "adaptive-stable-cadence-disabled"
        );
        require(disabled && disabled->reason == "outside-useful-range",
            "native cadence recovery did not report a bounded Smooth Cadence exit");

        size_t outputs = 0;
        constexpr size_t sampleFrames = 600;
        for (size_t frame = 0; frame < sampleFrames; ++frame)
            outputs += 1 + harness.frameAtFps(60.0).size();
        const double estimatedOutputFps =
            static_cast<double>(outputs) / 10.0;
        require(std::abs(estimatedOutputFps - 100.0) <= 0.2,
            "strict scheduling did not return recovered cadence to the 100 FPS target");
    }

    void testDynamicCadenceRecoversSelfHiddenNativeRateIncrease() {
        Harness harness(
            60, 2, false, AdaptiveRecoveryPolicy::OrderedSdr, true
        );
        harness.start();
        AdaptiveFramePlan previousPlan = harness.runAtFps(30.0, 8s);
        require(previousPlan.size() == 1,
            "precondition failed: 30 FPS gameplay did not settle at 2x");

        for (size_t frame = 0;
                frame < 600 &&
                    !harness.diagnostics.contains(
                        "dynamic-cadence-recovered"
                    );
                ++frame) {
            const double observedFps = previousPlan.empty() ? 60.0 : 30.0;
            previousPlan = harness.frameAtFps(observedFps);
            harness.scheduler.reportGeneratedFrameDelivery({
                .requested = previousPlan.size(),
                .acceptedForPresentation = previousPlan.size(),
            });
        }

        require(harness.diagnostics.contains(
                "dynamic-cadence-probe-start"),
            "dynamic cadence recovery never exposed native cadence");
        require(harness.diagnostics.contains(
                "dynamic-cadence-recovered"),
            "native 60 FPS menu cadence remained hidden behind generated FIFO work");
        require(harness.frameAtFps(60.0).empty(),
            "recovered native target cadence continued generating frames");
    }

    void testDynamicCadenceBoundsSelfHiddenRecoveryLatency() {
        Harness harness(
            60, 2, false, AdaptiveRecoveryPolicy::OrderedSdr, true
        );
        harness.start();
        constexpr auto recoveryLatencyBound = std::chrono::seconds(
            ls::GameConfDefaults::dynamicCadenceProbeIntervalSeconds
        ) + 100ms;

        while (!harness.diagnostics.contains(
                "dynamic-cadence-probe-rejected")) {
            harness.frameAtFps(30.0);
        }

        const auto transitionAt = harness.now;
        AdaptiveFramePlan previousPlan =
            AdaptiveFramePlan::evenlySpaced(1);
        while (!harness.diagnostics.contains("dynamic-cadence-recovered")) {
            const double observedFps = previousPlan.empty() ? 60.0 : 30.0;
            previousPlan = harness.frameAtFps(observedFps);
            harness.scheduler.reportGeneratedFrameDelivery({
                .requested = previousPlan.size(),
                .acceptedForPresentation = previousPlan.size(),
            });
            require(harness.now - transitionAt <= recoveryLatencyBound,
                "self-hidden native cadence recovery exceeded its latency bound");
        }

        require(harness.now - transitionAt <= recoveryLatencyBound,
            "self-hidden native cadence recovery did not complete promptly");
    }

    void testDynamicCadenceProbeIntervalUpdatesLive() {
        Harness harness(
            60, 2, false, AdaptiveRecoveryPolicy::OrderedSdr, true, 3s
        );
        harness.start();

        while (!harness.diagnostics.contains(
                "dynamic-cadence-probe-rejected")) {
            harness.frameAtFps(30.0);
        }

        const auto generationLimit =
            harness.scheduler.snapshot().generationLimit;
        const size_t stabilizationEvents =
            harness.diagnostics.count("stabilization");
        harness.scheduler.updateDynamicCadenceProbeInterval(harness.now, 1s);
        require(harness.scheduler.snapshot().generationLimit == generationLimit,
            "live probe interval update reset the validated generation limit");
        require(harness.diagnostics.count("stabilization") ==
                stabilizationEvents,
            "live probe interval update restarted scheduler stabilization");

        const auto transitionAt = harness.now;
        AdaptiveFramePlan previousPlan =
            AdaptiveFramePlan::evenlySpaced(1);
        while (!harness.diagnostics.contains("dynamic-cadence-recovered")) {
            const double observedFps = previousPlan.empty() ? 60.0 : 30.0;
            previousPlan = harness.frameAtFps(observedFps);
            harness.scheduler.reportGeneratedFrameDelivery({
                .requested = previousPlan.size(),
                .acceptedForPresentation = previousPlan.size(),
            });
            require(harness.now - transitionAt <= 1100ms,
                "live probe interval update did not reschedule the next probe");
        }
    }

    void testDynamicCadenceProbeRejectsTrueThirtyFpsCadence() {
        Harness harness(
            60, 2, false, AdaptiveRecoveryPolicy::OrderedSdr, true
        );
        harness.start();
        require(harness.runAtFps(30.0, 7s).size() == 1,
            "precondition failed: true 30 FPS cadence did not settle at 2x");

        size_t consecutiveNativeFrames = 0;
        size_t maximumConsecutiveNativeFrames = 0;
        for (size_t frame = 0; frame < 420; ++frame) {
            const auto plan = harness.frameAtFps(30.0);
            consecutiveNativeFrames = plan.empty()
                ? consecutiveNativeFrames + 1
                : 0;
            maximumConsecutiveNativeFrames = std::max(
                maximumConsecutiveNativeFrames, consecutiveNativeFrames
            );
        }

        require(harness.diagnostics.contains(
                "dynamic-cadence-probe-rejected"),
            "a true 30 FPS cadence did not reject the native-only probe");
        require(!harness.diagnostics.contains(
                "dynamic-cadence-recovered"),
            "a true 30 FPS cadence was mistaken for native recovery");
        require(maximumConsecutiveNativeFrames == 1,
            "a rejected native-cadence probe suppressed generation for multiple frames");
        require(harness.frameAtFps(30.0).size() == 1,
            "a rejected native-cadence probe did not resume 2x generation");
    }

    void testDynamicCadenceRecoveryIsOptInAndSdrOnly() {
        Harness disabled(
            60, 2, false, AdaptiveRecoveryPolicy::OrderedSdr, false
        );
        disabled.start();
        disabled.runAtFps(30.0, 12s);
        require(!disabled.diagnostics.contains(
                "dynamic-cadence-probe-start"),
            "default Adaptive policy performed a native-cadence probe");

        Harness hdr(
            60, 2, false, AdaptiveRecoveryPolicy::ConservativeHdr, true
        );
        hdr.start();
        hdr.runAtFps(30.0, 12s);
        require(!hdr.diagnostics.contains(
                "dynamic-cadence-probe-start"),
            "nonblocking HDR transport performed an ordered-cadence probe");
    }

    void testSmoothCadenceDoesNotChatterOnOscillatingLoad() {
        Harness harness(110, 3, true);
        harness.start();
        harness.runAtFps(55.0, 10s);
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::StableCadence,
            "precondition failed: stable 55 FPS did not settle on Smooth Cadence");

        harness.runAtFps(68.0, 2s);
        require(harness.scheduler.snapshot().phase !=
                AdaptiveSchedulerPhase::StableCadence,
            "native cadence recovery did not leave Smooth Cadence");

        const size_t probesBeforeOscillation = harness.diagnostics.count(
            "adaptive-stable-cadence-probe"
        );
        for (size_t cycle = 0; cycle < 6; ++cycle) {
            harness.runAtFps(55.0, 4s);
            harness.runAtFps(68.0, 2s);
        }
        const size_t probesAfterOscillation = harness.diagnostics.count(
            "adaptive-stable-cadence-probe"
        );
        require(probesAfterOscillation - probesBeforeOscillation <= 2,
            "oscillating load repeatedly toggled Smooth Cadence workload");
    }

    // The HDR bridge measures real-only cadence before deciding whether a
    // collapse was model load or compositor/colour-pipeline pressure.
    void testHdrStrictLoadCollapseMeasuresBeforeFallback() {
        Harness harness(180, 3);
        harness.start();
        harness.runAtFps(60.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: 3x was not validated");

        for (size_t frame = 0;
                frame < 160 && !harness.diagnostics.contains("rescue-start");
                ++frame) {
            harness.frameAtFps(40.0);
        }
        const auto* rescueStart = harness.diagnostics.last("rescue-start");
        require(rescueStart && rescueStart->reason == "strict-load-collapse",
            "sustained high-multiplier throughput collapse did not start rescue");
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::RescueMeasurement,
            "strict-load rescue was not exposed as scheduler state");

        harness.runAtFps(60.0, 2s);
        const auto* rescueComplete = harness.diagnostics.last("rescue-complete");
        require(rescueComplete &&
                rescueComplete->reason == "strict-load-restored",
            "real-only throughput recovery did not select the cheaper proven level");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "strict-load rescue did not restore the validated lower load");
    }

    void testSdrStrictLoadCollapseKeepsGeneratedFallbackActive() {
        // SDR's ordered transport can shed directly to the lower proven load;
        // a real-only second would be the visible 120-to-60 FPS regression.
        Harness harness(180, 3, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(60.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: SDR 3x was not validated");

        AdaptiveFramePlan plan;
        for (size_t frame = 0;
                frame < 160 && !harness.diagnostics.contains("load-shed");
                ++frame) {
            plan = harness.frameAtFps(40.0);
        }
        const auto* loadShed = harness.diagnostics.last("load-shed");
        require(loadShed && loadShed->reason == "sdr-direct-fallback" &&
                loadShed->previousLimit == 2 && loadShed->testedLimit == 1,
            "SDR collapse did not select its cheaper proven multiplier");
        require(harness.scheduler.snapshot().phase !=
                AdaptiveSchedulerPhase::RescueMeasurement,
            "SDR collapse entered a disruptive real-only measurement");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "SDR collapse did not retain the proven generated fallback");
        require(plan.size() == 1,
            "SDR load shed dropped the transition frame to native-only");
    }

    void testSdrStrictLoadRetainsHigherOutputLevel() {
        // A higher multiplier that still improves displayed throughput is not
        // a useful load-shed candidate, even when its real cadence falls far
        // enough to satisfy the strict collapse threshold.
        Harness harness(120, 3, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(50.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: target-positive SDR 3x was not validated");

        AdaptiveFramePlan plan;
        for (size_t frame = 0; frame < 160; ++frame)
            plan = harness.frameAtFps(36.0);

        require(!harness.diagnostics.contains("load-shed"),
            "SDR shed a higher multiplier that still improved displayed throughput");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "SDR did not retain the target-positive higher multiplier");
        require(plan.size() == 2,
            "SDR stopped requesting the target-positive generated workload");
    }

    void testSdrModerateDeficitDoesNotTrustHistoricalFallback() {
        // Reproduce the Resident Evil 4 trace: the earlier 2x sample could
        // theoretically provide about 104 FPS, while the current 3x sample
        // provides about 103 FPS. That stale cross-scene comparison is not
        // enough reason to shed load while output remains above 75% of target.
        Harness harness(120, 3, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(52.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: moderate-deficit SDR 3x was not validated");

        AdaptiveFramePlan plan;
        for (size_t frame = 0; frame < 160; ++frame)
            plan = harness.frameAtFps(34.5);

        require(!harness.diagnostics.contains("load-shed"),
            "SDR trusted a stale lower-level estimate during a moderate deficit");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "SDR discarded usable 3x output during a moderate target deficit");
        require(plan.size() == 2,
            "SDR stopped requesting 3x work during a moderate target deficit");
    }

    void testSdrSevereMarginalGainFallsBackAndBacksOff() {
        // Reproduce the ordered-FIFO collapse observed while the Decky overlay
        // was open: 2x provides roughly 50 FPS, while 3x lowers the base rate
        // enough to provide only a marginal displayed-rate improvement.
        Harness harness(120, 3, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(50.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: severe-deficit SDR 3x was not validated");

        for (size_t frame = 0;
                frame < 160 && harness.diagnostics.count("load-shed") < 1;
                ++frame) {
            harness.frameAtFps(29.0);
        }
        require(harness.diagnostics.count("load-shed") == 1 &&
                harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: initial SDR collapse did not establish 2x fallback");

        for (size_t frame = 0;
                frame < 600 &&
                    !harness.scheduler.snapshot().rampEvaluationActive;
                ++frame) {
            harness.frameAtFps(25.0);
        }
        require(harness.scheduler.snapshot().rampEvaluationActive,
            "SDR fallback never retried its higher level");

        for (size_t frame = 0;
                frame < 160 &&
                    harness.scheduler.snapshot().rampEvaluationActive;
                ++frame) {
            harness.frameAtFps(18.0);
        }
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: marginal 3x probe was not initially accepted");

        for (size_t frame = 0;
                frame < 160 && harness.diagnostics.count("load-shed") < 2;
                ++frame) {
            harness.frameAtFps(18.0);
        }
        require(harness.diagnostics.count("load-shed") == 2 &&
                harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "severe below-target marginal gain pinned the saturated 3x level");

        const size_t rampsBeforeBackoff = harness.diagnostics.count("ramp");
        harness.runAtFps(25.0, 20s);
        require(harness.diagnostics.count("ramp") == rampsBeforeBackoff &&
                harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "repeated saturated 3x probe did not extend its retry backoff");

        const size_t earlyRetries = harness.diagnostics.count(
            "ramp-early-retry"
        );
        for (size_t frame = 0;
                frame < 150 &&
                    harness.diagnostics.count("ramp-early-retry") == earlyRetries;
                ++frame) {
            harness.frameAtFps(30.0);
        }
        require(harness.diagnostics.count("ramp-early-retry") > earlyRetries,
            "sustained fallback-cadence recovery did not retry 3x early");
    }

    void testSdrTwoXCollapseRetainsMinimumGeneratedPolicy() {
        Harness harness(180, 2, false, AdaptiveRecoveryPolicy::OrderedSdr);
        harness.start();
        harness.runAtFps(60.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "precondition failed: SDR 2x was not validated");

        AdaptiveFramePlan plan;
        for (size_t frame = 0;
                frame < 160 && !harness.diagnostics.contains("load-shed");
                ++frame) {
            plan = harness.frameAtFps(30.0);
        }
        const auto* loadShed = harness.diagnostics.last("load-shed");
        require(loadShed && loadShed->reason == "sdr-retain-2x" &&
                loadShed->previousLimit == 1 && loadShed->testedLimit == 1,
            "SDR 2x collapse did not retain its minimum generated policy");
        require(harness.scheduler.snapshot().phase !=
                AdaptiveSchedulerPhase::RescueMeasurement,
            "SDR 2x collapse entered a disruptive real-only measurement");
        require(plan.size() == 1,
            "SDR 2x collapse dropped the transition frame to native-only");
    }

    void testSmoothCadenceCollapseUsesRealOnlyMeasurement() {
        Harness harness(90, 2, true);
        harness.start();
        harness.runAtFps(47.0, 12s);
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::StableCadence,
            "precondition failed: Smooth Cadence did not settle");

        for (size_t frame = 0;
                frame < 120 && !harness.diagnostics.contains("rescue-start");
                ++frame) {
            harness.frameAtFps(30.0);
        }
        const auto* rescueStart = harness.diagnostics.last("rescue-start");
        require(rescueStart &&
                rescueStart->reason == "stable-cadence-collapse",
            "collapsed Smooth Cadence did not start real-only measurement");
        require(harness.scheduler.snapshot().phase ==
                AdaptiveSchedulerPhase::RescueMeasurement,
            "Smooth Cadence rescue was not exposed as scheduler state");
        require(harness.frameAtFps(30.0).empty(),
            "Smooth Cadence rescue generated during real-only measurement");
    }

    void testRestoredDiscontinuityLoadRetainsCollapseGuard() {
        Harness harness(110, 3);
        harness.start();
        harness.runAtFps(50.0, 10s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "precondition failed: 3x was not validated before interruption");

        harness.frame(400ms);
        require(harness.scheduler.discontinuityRecoveryActive(),
            "precondition failed: menu-like interruption did not start recovery");
        harness.runAtFps(64.0, 3s);
        require(!harness.scheduler.discontinuityRecoveryActive() &&
                harness.scheduler.snapshot().validatedGenerationLimit == 2,
            "healthy real-only cadence did not restore the validated 3x level");

        for (size_t frame = 0;
                frame < 160 && !harness.diagnostics.contains("rescue-start");
                ++frame) {
            harness.frameAtFps(35.0);
        }
        const auto* rescueStart = harness.diagnostics.last("rescue-start");
        require(rescueStart && rescueStart->reason == "strict-load-collapse",
            "restored 3x load lost its delayed-collapse guard");

        harness.runAtFps(64.0, 2s);
        const auto* rescueComplete = harness.diagnostics.last("rescue-complete");
        require(rescueComplete &&
                rescueComplete->reason == "strict-load-restored",
            "real-only recovery did not back off the harmful restored level");
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "harmful restored 3x load did not fall back to proven 2x");
    }

    void testGeneratedImageRecoveryRetainsCollapseGuard() {
        Harness harness(110, 3);
        harness.start();
        harness.runAtFps(50.0, 10s);
        const size_t recoveredLimit =
            harness.scheduler.snapshot().validatedGenerationLimit;
        const auto loadBaseline =
            harness.scheduler.generationLoadBaseline();
        require(recoveredLimit == 2 &&
                loadBaseline.fallbackGenerationLimit == 1 &&
                loadBaseline.baseFps > 0.0,
            "precondition failed: higher load had no lower-level baseline");

        harness.scheduler.beginStabilization(
            harness.now, "generated-image-recovery"
        );
        harness.scheduler.restoreGenerationLimit(
            harness.now,
            recoveredLimit,
            "generated-image-recovery",
            loadBaseline.fallbackGenerationLimit,
            loadBaseline.baseFps
        );

        for (size_t frame = 0;
                frame < 160 && !harness.diagnostics.contains("rescue-start");
                ++frame) {
            harness.frameAtFps(35.0);
        }
        const auto* rescueStart = harness.diagnostics.last("rescue-start");
        require(rescueStart && rescueStart->reason == "strict-load-collapse",
            "in-place image recovery cleared the delayed-collapse guard");

        harness.runAtFps(64.0, 2s);
        require(harness.scheduler.snapshot().validatedGenerationLimit == 1,
            "image recovery did not return harmful 3x load to proven 2x");
    }

    void testDeterministicReplay() {
        Harness first(120, 4, true);
        Harness second(120, 4, true);
        first.start();
        second.start();

        const std::vector<std::chrono::nanoseconds> trace{
            17ms, 16ms, 17ms, 16ms, 50ms, 16ms, 17ms, 200ms,
            16ms, 16ms, 17ms, 1ms, 1ms, 16ms, 33ms, 34ms,
        };
        for (size_t replay = 0; replay < 40; ++replay) {
            for (const auto interval : trace) {
                const auto firstPlan = first.frame(interval);
                const auto secondPlan = second.frame(interval);
                require(firstPlan == secondPlan,
                    "identical cadence trace produced different plans");
                requireValidTimestamps(firstPlan, 3);
            }
        }

        const auto firstSnapshot = first.scheduler.snapshot();
        const auto secondSnapshot = second.scheduler.snapshot();
        require(firstSnapshot.phase == secondSnapshot.phase &&
                firstSnapshot.generationLimit ==
                    secondSnapshot.generationLimit &&
                firstSnapshot.validatedGenerationLimit ==
                    secondSnapshot.validatedGenerationLimit &&
                firstSnapshot.historyWarmupRemaining ==
                    secondSnapshot.historyWarmupRemaining,
            "identical cadence trace produced different final state");
    }

    struct TestCase {
        std::string_view name;
        void (*run)();
    };
}

int main() {
    const std::vector<TestCase> tests{
        {"startup warm-up is explicit", testStartupWarmupIsExplicit},
        {"busy warm-up notification is idempotent", testBusyWarmupNotificationIsIdempotent},
        {"transient busy frame does not rearm warm-up", testTransientBusyFrameDoesNotRearmCompletedWarmup},
        {"invalid configuration is rejected", testInvalidConfigurationIsRejectedAtBoundary},
        {"generated delivery window contract", testGeneratedDeliveryWindowContract},
        {"scheduler characterization corpus", testCharacterizationTraceCorpus},
        {"60 to 120 settles at 2x", testSteadySixtyRampsToTwoXFor120Target},
        {"fractional target clock follows raw intervals", testFractionalTargetClockAssignsWorkToLongIntervals},
        {"target clock covers steady and noisy cadence matrix", testTargetClockCoversSteadyAndNoisyCadenceMatrix},
        {"target clock handles multi-level fractional cadence", testTargetClockHandlesMultiLevelFractionalCadence},
        {"target clock preserves noisy integer ceiling", testTargetClockPreservesNoisyIntegerCeiling},
        {"deferred output repayment preserves pacing benefit", testDeferredOutputRepaymentPreservesPacingBenefit},
        {"target clock reset clears deferred output", testTargetClockResetClearsDeferredOutput},
        {"raw placement cannot mint generated work", testRawPlacementCannotMintGeneratedWork},
        {"target clock discards unreachable ceiling debt", testTargetClockDiscardsUnreachableCeilingDebt},
        {"pacing diagnostics restart after policy gap", testPacingDiagnosticsRestartAfterPolicyGap},
        {"isolated delivery miss keeps ramp", testIsolatedGeneratedFrameMissDoesNotRejectRamp},
        {"persistent delivery loss rejects ramp", testPersistentGeneratedFrameMissesRejectRamp},
        {"4x timestamps remain evenly spaced", testFourXPlanUsesEvenInterpolationTimestamps},
        {"above-target cadence remains real-only", testSchedulerCannotReduceAboveTargetCadence},
        {"acquire backoff freezes policy", testAcquireBackoffDoesNotAdvancePolicy},
        {"validated 2x survives short hitch", testValidatedTwoXSurvivesShortGameplayHitch},
        {"HDR long hitch uses conservative recovery", testHdrLongHitchUsesConservativeDiscontinuityRecovery},
        {"healthy cadence restores validated level", testRecoveredCadenceRestoresValidatedLevel},
        {"discontinuity timeout restarts from zero", testDiscontinuityRecoveryTimesOutToFreshRamp},
        {"gameplay cadence drop rebases", testSustainedCadenceDropRebasesWithoutMenuRecovery},
        {"SDR long hitch keeps validated level", testSdrLongHitchRefreshesHistoryWithoutDroppingValidatedLevel},
        {"SDR stable 2x bridges isolated hitch", testSdrStableTwoXBridgesIsolatedGameplayHitch},
        {"SDR cadence drop uses short refresh", testSdrCadenceDropRefreshesHistoryWithoutFullStabilization},
        {"SDR hard stall avoids refresh loop", testSdrSustainedHardStallDoesNotRestartHistoryRefresh},
        {"fast-present burst preserves cadence", testImpossibleFastBurstDoesNotCorruptCadence},
        {"harmful first probe enters rearm", testRejectedFirstProbeEntersBoundedRearm},
        {"interrupted probe rearms promptly", testInterruptedProbeRearmsWithoutFailurePenalty},
        {"bridge probe handles misleading first step", testBridgeProbeCanRecoverMisleadingFirstStep},
        {"rejected higher level backs off", testRejectedHigherLevelRetainsProvenLoadAndBacksOff},
        {"Smooth Cadence settles near integer demand", testSmoothCadenceSettlesNearIntegerDemand},
        {"Smooth Cadence retains validated 2x through mild dip", testSmoothCadenceRetainsValidatedTwoXThroughMildDip},
        {"Smooth Cadence exits below retention range", testSmoothCadenceExitsBelowRetentionRange},
        {"persistent loss rejects Smooth Cadence", testPersistentDeliveryLossRejectsSmoothCadenceProbe},
        {"Smooth Cadence exits after native recovery", testSmoothCadenceReturnsToTargetAfterBaseRecovery},
        {"dynamic cadence recovers a self-hidden native rate", testDynamicCadenceRecoversSelfHiddenNativeRateIncrease},
        {"dynamic cadence bounds self-hidden recovery latency", testDynamicCadenceBoundsSelfHiddenRecoveryLatency},
        {"dynamic cadence probe interval updates live", testDynamicCadenceProbeIntervalUpdatesLive},
        {"dynamic cadence rejects true 30 FPS", testDynamicCadenceProbeRejectsTrueThirtyFpsCadence},
        {"dynamic cadence remains opt-in and SDR-only", testDynamicCadenceRecoveryIsOptInAndSdrOnly},
        {"Smooth Cadence resists oscillating-load chatter", testSmoothCadenceDoesNotChatterOnOscillatingLoad},
        {"HDR strict load collapse measures real-only", testHdrStrictLoadCollapseMeasuresBeforeFallback},
        {"SDR load shed keeps generated fallback", testSdrStrictLoadCollapseKeepsGeneratedFallbackActive},
        {"SDR load shed retains higher output", testSdrStrictLoadRetainsHigherOutputLevel},
        {"SDR moderate deficit ignores stale fallback", testSdrModerateDeficitDoesNotTrustHistoricalFallback},
        {"SDR severe marginal gain backs off", testSdrSevereMarginalGainFallsBackAndBacksOff},
        {"SDR 2x load shed stays generated", testSdrTwoXCollapseRetainsMinimumGeneratedPolicy},
        {"Smooth Cadence collapse measures real-only", testSmoothCadenceCollapseUsesRealOnlyMeasurement},
        {"restored load keeps collapse guard", testRestoredDiscontinuityLoadRetainsCollapseGuard},
        {"image recovery keeps collapse guard", testGeneratedImageRecoveryRetainsCollapseGuard},
        {"cadence replay is deterministic", testDeterministicReplay},
    };

    size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "PASS: " << test.name << '\n';
        } catch (const TestFailure& failure) {
            failures++;
            std::cerr << "FAIL: " << test.name << ": "
                      << failure.message << '\n';
        } catch (const std::exception& error) {
            failures++;
            std::cerr << "FAIL: " << test.name
                      << ": unexpected exception: " << error.what() << '\n';
        }
    }

    if (failures) {
        std::cerr << failures << " adaptive scheduler test(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << tests.size() << " adaptive scheduler tests passed\n";
    return EXIT_SUCCESS;
}
