/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "presentation_policy.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {

    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    const PresentationEnvironmentPolicy normalEnvironment{};
    const PresentationEnvironmentPolicy isolatedEnvironment{
        .gamescopeWsiDisabled = true,
        .hdrExposureDisabled = true,
    };
    // Regression boundary: only an HDR-capable swapchain managed by Gamescope
    // may use the compositor bridge. In particular, merely discovering
    // Gamescope must not route an ordinary SDR game away from ordered FIFO.
    expect(selectPresentationTransport(false, false, normalEnvironment) ==
            PresentationTransport::OrderedSdr,
        "ordinary SDR did not retain the ordered fork transport");
    expect(selectPresentationTransport(false, true, normalEnvironment) ==
            PresentationTransport::OrderedSdr,
        "non-Gamescope HDR unexpectedly selected the Gamescope bridge");
    expect(selectPresentationTransport(true, false, normalEnvironment) ==
            PresentationTransport::OrderedSdr,
        "Gamescope SDR was routed through the HDR transport");
    expect(selectPresentationTransport(true, true, normalEnvironment) ==
            PresentationTransport::GamescopeHdr,
        "HDR-capable Gamescope swapchain did not select the HDR bridge");
    expect(selectPresentationTransport(true, true, isolatedEnvironment) ==
            PresentationTransport::OrderedSdr,
        "WSI-isolated launch selected the unavailable Gamescope HDR bridge");

    // The HDR/Gamescope path must never block a real frame waiting for a
    // synthetic image. Legacy/ordered paths keep their historical contract.
    expect(generatedImageAcquireTimeout(true, 50'000'000) == 0,
        "Gamescope admission must remain nonblocking");
    expect(generatedImageAcquireTimeout(true, std::nullopt) == 0,
        "Gamescope admission became unbounded without a configured ceiling");
    expect(generatedImageAcquireTimeout(false, 50'000'000) == 50'000'000,
        "legacy configured acquire ceiling was not preserved");
    expect(generatedImageAcquireTimeout(false, std::nullopt) ==
            std::numeric_limits<uint64_t>::max(),
        "legacy unconfigured acquire behaviour changed");
    constexpr uint64_t acquireBudget = 50'000'000;
    expect(remainingGeneratedImageAcquireBudget(
            std::nullopt, 32'805'100) == std::nullopt,
        "unconfigured ordered acquire unexpectedly gained a finite budget");
    expect(remainingGeneratedImageAcquireBudget(
            acquireBudget, 0) == acquireBudget,
        "fresh ordered acquire budget did not retain its full deadline");
    expect(remainingGeneratedImageAcquireBudget(
            acquireBudget, 32'805'100) == 17'194'900,
        "second generated image did not receive only the remaining present budget");
    expect(remainingGeneratedImageAcquireBudget(
            acquireBudget, 49'999'999) == 1,
        "ordered acquire budget lost its final nanosecond");
    expect(remainingGeneratedImageAcquireBudget(
            acquireBudget, acquireBudget) == 0 &&
            remainingGeneratedImageAcquireBudget(
                acquireBudget, acquireBudget + 1) == 0,
        "exhausted ordered acquire budget allowed another blocking wait");
    expect(orderedRecoveryAcquireTimeout(120, acquireBudget, 1) ==
            8'333'334,
        "first 120 Hz recovery probe lost its one-period budget");
    expect(orderedRecoveryAcquireTimeout(120, acquireBudget, 2) ==
            16'666'668,
        "second 120 Hz recovery probe did not expand conservatively");
    expect(orderedRecoveryAcquireTimeout(120, acquireBudget, 3) ==
            25'000'000 &&
            orderedRecoveryAcquireTimeout(120, acquireBudget, 20) ==
                25'000'000,
        "repeated recovery probes exceeded the hard 25 ms ceiling");
    expect(orderedRecoveryAcquireTimeout(60, acquireBudget, 1) ==
            16'666'667 &&
            orderedRecoveryAcquireTimeout(60, acquireBudget, 2) ==
                25'000'000,
        "60 Hz recovery probe lost display-relative escalation");
    expect(orderedRecoveryAcquireTimeout(
            std::nullopt, 10'000'000, 3) == 10'000'000,
        "recovery probe exceeded the configured acquire ceiling");

    GeneratedImageAdmission admission;
    expect(!admission.underPressure(),
        "generated-image admission started under pressure");
    expect(admission.reportUnavailable(),
        "first nonblocking admission miss was not diagnostic");
    expect(admission.underPressure(),
        "admission pressure was not retained");
    admission.reportBypassedFrame();
    expect(admission.reportUnavailable(),
        "second admission miss should be a power-of-two diagnostic");
    expect(!admission.reportUnavailable(),
        "third admission miss should be aggregated");
    admission.reportBypassedFrame();
    const auto recovery = admission.reportAvailable();
    expect(recovery.resumed && recovery.missedAttempts == 3 &&
            recovery.bypassedFrames == 2,
        "admission recovery lost its aggregated pressure counters");
    expect(!admission.underPressure(),
        "admission recovery did not reset pressure");

    expect(OrderedAcquireRecovery::slowAcquireDuration(120) == 25ms,
        "120 Hz ordered acquire pressure threshold changed");
    expect(OrderedAcquireRecovery::slowAcquireDuration(60) == 25ms,
        "60 Hz ordered acquire pressure threshold changed");
    expect(OrderedAcquireRecovery::slowAcquireDuration(40) >= 37ms &&
            OrderedAcquireRecovery::slowAcquireDuration(40) < 38ms,
        "40 Hz ordered acquire pressure threshold lost display scaling");
    expect(orderedAcquireRecoveryClassificationDuration(33ms, 11ms) == 11ms &&
            orderedAcquireRecoveryClassificationDuration(30ms, 30ms) == 30ms,
        "ordered multi-image recovery did not classify the longest individual acquire");
    expect(!preacquiredImagesRequireRetirement(false, 1) &&
            !preacquiredImagesRequireRetirement(true, 0) &&
            preacquiredImagesRequireRetirement(true, 1),
        "pre-acquired image retirement lost its ownership contract");

    OrderedAcquireRecovery acquireRecovery;
    const auto acquireStart = OrderedAcquireRecovery::TimePoint{};
    auto observation = acquireRecovery.observe(
        acquireStart, 30ms, 25ms, false
    );
    expect(!observation.quarantined && observation.guardArmed &&
            acquireRecovery.active(),
        "one slow ordered acquire did not arm zero-wait protection");
    auto acquireDecision = acquireRecovery.beforePresent(
        acquireStart + 1ms
    );
    expect(!acquireDecision.bypassGeneration &&
            !acquireDecision.beginHistoryWarmup &&
            acquireDecision.limitGeneratedFrames &&
            acquireDecision.preacquireGeneratedFrame,
        "slow-acquire protection did not request a zero-wait frame");
    observation = acquireRecovery.observe(
        acquireStart + 2ms, 0ms, 25ms, false
    );
    expect(observation.recovered && observation.guardCleared &&
            !observation.stabilizing && !acquireRecovery.active(),
        "a successful zero-wait guard did not resume normal policy");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 3ms);
    expect(!acquireDecision.bypassGeneration &&
            !acquireDecision.limitGeneratedFrames,
        "successful slow-acquire protection retained recovery constraints");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart + 32ms, 50ms, 25ms, true
    );
    expect(observation.quarantined && observation.timedOut &&
            observation.consecutiveFailures == 1 &&
            observation.retryDelay == 250ms,
        "one ordered acquire timeout did not start the native drain");
    acquireDecision = acquireRecovery.beforePresent(
        acquireStart + 100ms
    );
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.bypassedFrames == 1,
        "ordered acquire recovery did not bypass generation while draining");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 282ms);
    expect(!acquireDecision.bypassGeneration &&
            acquireDecision.beginHistoryWarmup &&
            acquireDecision.limitGeneratedFrames &&
            acquireDecision.preacquireGeneratedFrame &&
            acquireDecision.boundedAcquireProbe &&
            acquireDecision.consecutiveFailures == 1,
        "ordered acquire recovery did not warm history before a bounded probe");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 300ms);
    expect(acquireDecision.limitGeneratedFrames &&
            acquireDecision.preacquireGeneratedFrame &&
            acquireDecision.boundedAcquireProbe,
        "ordered acquire recovery did not retain its bounded probe");
    const auto failedProbe =
        acquireRecovery.reportNonblockingProbeUnavailable(
            acquireStart + 301ms
        );
    expect(failedProbe.diagnostic && failedProbe.quarantined &&
            failedProbe.boundedProbeFailed &&
            failedProbe.consecutiveFailures == 2 &&
            failedProbe.retryDelay == 500ms && acquireRecovery.active(),
        "failed bounded recovery probe did not return to native backoff");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 800ms);
    expect(acquireDecision.bypassGeneration,
        "second ordered drain ended before its retry deadline");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 801ms);
    expect(acquireDecision.beginHistoryWarmup &&
            acquireDecision.boundedAcquireProbe &&
            acquireDecision.consecutiveFailures == 2,
        "second ordered drain did not request a bounded retry");
    observation = acquireRecovery.observe(
        acquireStart + 900ms, 25ms, 25ms, false, false, true
    );
    expect(observation.recovered && observation.stabilizing &&
            acquireRecovery.active() &&
            observation.consecutiveFailures == 2,
        "healthy ordered acquire probe did not begin constrained stabilization");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 1800ms);
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.nativeOnlyStabilization &&
            !acquireDecision.limitGeneratedFrames &&
            !acquireDecision.preacquireGeneratedFrame &&
            acquireDecision.stabilizationRemaining == 1100ms,
        "ordered recovery did not use deterministic native-only stabilization");
    for (const auto missAt : {1801ms, 2000ms, 2400ms, 2899ms}) {
        static_cast<void>(
            acquireRecovery.reportNonblockingProbeUnavailable(
                acquireStart + missAt
            )
        );
    }
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 2899ms);
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.nativeOnlyStabilization,
        "ordered recovery left native-only stabilization before its deadline");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 2900ms);
    expect(acquireDecision.recoveryStabilized &&
            acquireDecision.beginHistoryWarmup &&
            !acquireRecovery.active(),
        "ordered recovery misses extended the hard stabilization deadline");

    observation = acquireRecovery.observe(
        acquireStart + 4100ms, 50ms, 25ms, true
    );
    expect(observation.quarantined &&
            observation.consecutiveFailures == 1 &&
            observation.retryDelay == 250ms,
        "sustained healthy delivery did not reset ordered retry backoff");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 30ms, 25ms, false
    );
    expect(!observation.quarantined && observation.guardArmed,
        "first repeated-slow sample did not arm protection");
    observation = acquireRecovery.observe(
        acquireStart + 16ms, 30ms, 25ms, false
    );
    expect(observation.quarantined && !observation.timedOut,
        "two repeated slow ordered acquires did not enter recovery");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 60ms, 25ms, false, true
    );
    expect(observation.quarantined &&
            observation.deadlineExceeded && observation.severe &&
            observation.retryDelay == 250ms,
        "successful acquire deadline overrun did not enter recovery");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 50ms, 25ms, false
    );
    expect(observation.quarantined && !observation.deadlineExceeded &&
            observation.severe,
        "severe unbounded acquire did not enter recovery");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 81ms, 25ms, false
    );
    expect(observation.quarantined && observation.severe &&
            !observation.timedOut && !observation.deadlineExceeded,
        "cumulative multi-image acquire time was not classified as severe");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 30ms, 25ms, false
    );
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 1ms);
    const auto guardMiss =
        acquireRecovery.reportNonblockingProbeUnavailable(
            acquireStart + 2ms
        );
    expect(observation.guardArmed &&
            acquireDecision.preacquireGeneratedFrame &&
            !guardMiss.quarantined && guardMiss.diagnostic &&
            guardMiss.guardBypassed &&
            guardMiss.consecutiveFailures == 0 &&
            !acquireRecovery.active(),
        "zero-wait guard miss did not release one native relief frame");
    observation = acquireRecovery.observe(
        acquireStart + 10ms, 5ms, 25ms, false
    );
    expect(!observation.quarantined &&
            observation.bypassedFrames == 0 &&
            !acquireRecovery.active(),
        "healthy delivery after native relief retained stale recovery state");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 30ms, 25ms, false
    );
    static_cast<void>(acquireRecovery.beforePresent(acquireStart + 1ms));
    static_cast<void>(acquireRecovery.reportNonblockingProbeUnavailable(
        acquireStart + 2ms
    ));
    observation = acquireRecovery.observe(
        acquireStart + 20ms, 30ms, 25ms, false
    );
    expect(observation.quarantined &&
            observation.consecutiveSlowFrames == 2 &&
            observation.consecutiveFailures == 1,
        "slow delivery after native relief did not prove repeated pressure");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 50ms, 25ms, true
    );
    auto probeRetryAt = acquireStart + observation.retryDelay;
    constexpr std::array boundedProbeRetryDelays{
        500ms, 1000ms, 2000ms, 2000ms,
    };
    for (const auto expectedDelay : boundedProbeRetryDelays) {
        acquireDecision = acquireRecovery.beforePresent(probeRetryAt);
        expect(acquireDecision.beginHistoryWarmup &&
                acquireDecision.boundedAcquireProbe,
            "bounded recovery attempt did not leave native backoff");
        const auto failedAt = probeRetryAt + 1ms;
        const auto boundedMiss =
            acquireRecovery.reportNonblockingProbeUnavailable(failedAt);
        expect(boundedMiss.quarantined &&
                boundedMiss.boundedProbeFailed &&
                boundedMiss.retryDelay == expectedDelay,
            "bounded recovery miss remained probe-pending");
        const auto beforeRetry = acquireRecovery.beforePresent(
            failedAt + expectedDelay - 1ms
        );
        expect(beforeRetry.bypassGeneration &&
                !beforeRetry.preacquireGeneratedFrame,
            "failed bounded probe retried before its native-drain deadline");
        probeRetryAt = failedAt + expectedDelay;
    }

    acquireRecovery.reset();
    auto repeatedFailureAt = acquireStart;
    constexpr std::array expectedRetryDelays{
        250ms, 500ms, 1000ms, 2000ms, 2000ms,
    };
    for (size_t failure = 0; failure < expectedRetryDelays.size(); ++failure) {
        observation = acquireRecovery.observe(
            repeatedFailureAt, 50ms, 25ms, true
        );
        expect(observation.quarantined &&
                observation.retryDelay == expectedRetryDelays.at(failure),
            "ordered acquire retry did not follow its bounded backoff");
        repeatedFailureAt += expectedRetryDelays.at(failure);
        acquireDecision = acquireRecovery.beforePresent(repeatedFailureAt);
        expect(acquireDecision.beginHistoryWarmup,
            "ordered acquire retry did not leave native drain at deadline");
        repeatedFailureAt += 1ms;
    }

    PipelineBusyRecovery pipelineBusy;
    auto now = PipelineBusyRecovery::TimePoint{};
    for (size_t frame = 0; frame < 12; ++frame) {
        const auto busy = pipelineBusy.reportBusy(now);
        expect(!busy.requestHistoryWarmup,
            "normal one-frame GPU overlap requested history warm-up");
        expect(busy.consecutiveFrames == 1,
            "a ready frame did not end the previous busy interval");
        const auto ready = pipelineBusy.reportReady(now + 8ms);
        expect(ready.resumed && !ready.historyWarmupRequested,
            "transient pipeline overlap recovered as a temporal failure");
        now += 16ms;
    }

    pipelineBusy.reset();
    const auto busyStart = PipelineBusyRecovery::TimePoint{};
    expect(!pipelineBusy.reportBusy(busyStart).requestHistoryWarmup,
        "pipeline pressure requested warm-up immediately");
    expect(!pipelineBusy.reportBusy(busyStart + 249ms).requestHistoryWarmup,
        "pipeline pressure requested warm-up before the sustained threshold");
    expect(pipelineBusy.reportBusy(busyStart + 250ms).requestHistoryWarmup,
        "sustained pipeline pressure did not request history warm-up");
    expect(!pipelineBusy.reportBusy(busyStart + 300ms).requestHistoryWarmup,
        "one busy interval requested history warm-up more than once");
    const auto busyRecovery = pipelineBusy.reportReady(busyStart + 320ms);
    expect(busyRecovery.resumed && busyRecovery.historyWarmupRequested &&
            busyRecovery.bypassedFrames == 4,
        "sustained pipeline recovery lost its one-shot warm-up state");

    // Reproduce the hardware trace: every submitted warm-up frame remains in
    // flight for the following game present. Transient busy/ready alternation
    // must allow the 3 -> 2 -> 1 sequence to terminate instead of rearming it.
    pipelineBusy.reset();
    size_t fixedWarmupRemaining = 3;
    now = PipelineBusyRecovery::TimePoint{};
    while (fixedWarmupRemaining > 0) {
        fixedWarmupRemaining--;
        const auto busy = pipelineBusy.reportBusy(now + 8ms);
        if (busy.requestHistoryWarmup && fixedWarmupRemaining == 0)
            fixedWarmupRemaining = 3;
        static_cast<void>(pipelineBusy.reportReady(now + 16ms));
        now += 24ms;
    }
    expect(fixedWarmupRemaining == 0,
        "transient pipeline overlap trapped fixed mode in history warm-up");

    RealFramePacer framePacer;
    const auto pacingStart = RealFramePacer::TimePoint{};
    expect(framePacer.schedule(pacingStart, 60) == pacingStart,
        "the first capped frame must not be delayed");
    const auto secondDeadline = framePacer.schedule(
        pacingStart + 16ms, 60
    );
    const auto thirdDeadline = framePacer.schedule(
        pacingStart + 32ms, 60
    );
    expect(secondDeadline > pacingStart + 16ms &&
            secondDeadline < pacingStart + 17ms,
        "60 FPS pacing did not schedule a 16.67 ms second frame");
    expect(thirdDeadline > pacingStart + 33ms &&
            thirdDeadline < pacingStart + 34ms,
        "early application frames did not remain on the absolute 60 FPS cadence");

    const auto afterStall = framePacer.schedule(pacingStart + 200ms, 60);
    expect(afterStall == pacingStart + 200ms,
        "a late frame was delayed for stale pacing debt");
    const auto afterStallDeadline = framePacer.schedule(
        pacingStart + 205ms, 60
    );
    expect(afterStallDeadline > pacingStart + 216ms &&
            afterStallDeadline < pacingStart + 217ms,
        "pacing did not rebase after a loading stall");

    expect(framePacer.schedule(pacingStart + 210ms, 0) ==
            pacingStart + 210ms,
        "disabling the cap delayed an application frame");
    expect(framePacer.schedule(pacingStart + 211ms, 60) ==
            pacingStart + 211ms,
        "re-enabling the cap retained a stale deadline");
    expect(framePacer.schedule(pacingStart + 220ms, 30) ==
            pacingStart + 220ms,
        "changing the cap retained the previous cadence");
    const auto thirtyFpsDeadline = framePacer.schedule(
        pacingStart + 230ms, 30
    );
    expect(thirtyFpsDeadline > pacingStart + 253ms &&
            thirtyFpsDeadline < pacingStart + 254ms,
        "30 FPS pacing did not establish a new 33.33 ms cadence");

    framePacer.reset();
    expect(framePacer.schedule(pacingStart, 82.5) == pacingStart,
        "the first fractionally capped frame must not be delayed");
    const auto fractionalDeadline = framePacer.schedule(
        pacingStart + 12ms, 82.5
    );
    expect(fractionalDeadline > pacingStart + 12ms &&
            fractionalDeadline < pacingStart + 13ms,
        "82.5 FPS pacing did not retain its fractional interval");

    SmoothCadencePacerHandoff pacerHandoff;
    auto handoff = pacerHandoff.update(pacingStart, true);
    expect(handoff.active && handoff.changed,
        "qualified Smooth Cadence did not hand pacing to ordered FIFO");
    handoff = pacerHandoff.update(pacingStart + 1s, true);
    expect(handoff.active && !handoff.changed,
        "retained ordered-FIFO handoff reported a false transition");
    handoff = pacerHandoff.update(pacingStart + 2s, false);
    expect(!handoff.active && handoff.changed,
        "lost Smooth Cadence qualification did not restore the base cap");
    handoff = pacerHandoff.update(pacingStart + 30s, true);
    expect(!handoff.active && !handoff.changed,
        "failed pacing handoff retried before its long cooldown");
    handoff = pacerHandoff.update(
        pacingStart + 2s + SmoothCadencePacerHandoff::retryDelay(), true
    );
    expect(handoff.active && handoff.changed,
        "eligible pacing handoff did not retry after its cooldown");
    pacerHandoff.reset();
    handoff = pacerHandoff.update(pacingStart + 3s, true);
    expect(handoff.active && handoff.changed,
        "explicit pacing-handoff reset retained stale cooldown state");

    expect(LowerPresentStallRecovery::stallThreshold(120) == 50ms &&
            LowerPresentStallRecovery::stallThreshold(60) >= 66ms &&
            LowerPresentStallRecovery::stallThreshold(60) < 67ms &&
            LowerPresentStallRecovery::stallThreshold(40) == 100ms,
        "lower-present stall thresholds lost their display-relative floor");
    LowerPresentStallRecovery presentStallRecovery;
    const auto presentStallStart =
        LowerPresentStallRecovery::TimePoint{};
    auto presentStall = presentStallRecovery.observe(
        presentStallStart, 49ms, 120
    );
    expect(!presentStall.quarantined &&
            !presentStallRecovery.active(),
        "a sub-threshold lower present entered recovery");
    presentStall = presentStallRecovery.observe(
        presentStallStart + 1ms, 621ms, 120
    );
    expect(presentStall.quarantined &&
            presentStall.threshold == 50ms &&
            presentStallRecovery.active(),
        "a severe lower present did not enter native stabilization");
    auto presentStallDecision = presentStallRecovery.beforePresent(
        presentStallStart + 1001ms
    );
    expect(presentStallDecision.bypassGeneration &&
            presentStallDecision.bypassedFrames == 1,
        "lower-present recovery did not protect the stabilization window");
    presentStallDecision = presentStallRecovery.beforePresent(
        presentStallStart + 2001ms
    );
    expect(presentStallDecision.recovered &&
            presentStallDecision.beginHistoryWarmup &&
            presentStallDecision.bypassedFrames == 1 &&
            !presentStallRecovery.active(),
        "lower-present recovery did not end at its absolute deadline");

    FixedRefreshBudget budget;
    const auto start = FixedRefreshBudget::TimePoint{};
    size_t generated = 0;
    for (size_t frame = 0; frame <= 630; ++frame) {
        generated += budget.plan(
            start + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(static_cast<double>(frame) / 63.0)
            ),
            120,
            1
        );
    }
    // The first real frame is deliberately ungenerated while timing warms up.
    expect(generated >= 565 && generated <= 575,
        "63 FPS Fixed 2x should budget approximately 120 displayed FPS");

    budget.reset();
    generated = 0;
    for (size_t frame = 0; frame <= 600; ++frame) {
        generated += budget.plan(start + frame * 10ms, 120, 1);
    }
    expect(generated >= 115 && generated <= 125,
        "100 FPS Fixed 2x should synthesize only the displayable remainder");

    budget.reset();
    generated = 0;
    for (size_t frame = 0; frame < 600; ++frame) {
        generated += budget.plan(
            start + std::chrono::milliseconds(frame * 5 / 3), 90, 1
        );
    }
    expect(generated == 0,
        "600 FPS toward a 90 Hz display must remain real-only");

    for (const uint32_t refreshHz : {40U, 60U, 90U, 120U}) {
        for (size_t generatedCapacity = 1; generatedCapacity <= 3;
                ++generatedCapacity) {
            budget.reset();
            generated = 0;
            constexpr size_t sampleRealFrames = 600;
            const double realFps = static_cast<double>(refreshHz) /
                static_cast<double>(generatedCapacity + 1);
            for (size_t frame = 0; frame <= sampleRealFrames; ++frame) {
                const auto when = start +
                    std::chrono::duration_cast<
                        std::chrono::steady_clock::duration
                    >(std::chrono::duration<double>(
                        static_cast<double>(frame) / realFps
                    ));
                generated += budget.plan(
                    when, refreshHz, generatedCapacity
                );
            }
            const double seconds =
                static_cast<double>(sampleRealFrames) / realFps;
            const double outputFps = static_cast<double>(
                sampleRealFrames + generated
            ) / seconds;
            expect(std::abs(outputFps - static_cast<double>(refreshHz)) < 0.5,
                "fixed refresh matrix missed its display budget at " +
                    std::to_string(refreshHz) + " Hz / " +
                    std::to_string(generatedCapacity + 1) + "x");
        }
    }

    std::cout << "presentation policy tests passed\n";
    return 0;
}
