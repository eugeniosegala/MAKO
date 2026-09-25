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

    void testOrderedAcquireUsesExplicitFailureOnly() {
        OrderedAcquireRecovery recovery;
        const auto now = OrderedAcquireRecovery::TimePoint{};

        recovery.armTransitionRecreation();
        expect(recovery.transitionRecoveryActive(),
            "explicit transition did not arm one recreation permission");
        const auto healthy = recovery.observe(now, false, false, false);
        expect(!healthy.quarantined && !recovery.active() &&
                !recovery.transitionRecoveryActive() &&
                !recovery.signalRecreation(),
            "successful generated-image transport was treated as a failure");

        recovery.armTransitionRecreation();
        const auto failed = recovery.observe(now, true, false, false);
        expect(failed.quarantined && failed.timedOut &&
                failed.retryDelay == 250ms && recovery.active(),
            "explicit Vulkan timeout did not enter bounded native backoff");
        expect(recovery.signalRecreation() && !recovery.signalRecreation(),
            "transition-scoped recreation was not one-shot");
    }

    void testOrderedAcquireBackoffAndProbeAreFinite() {
        OrderedAcquireRecovery recovery;
        const auto start = OrderedAcquireRecovery::TimePoint{};
        const auto failed = recovery.observe(start, true);
        expect(recovery.beforePresent(start + 249ms).bypassGeneration,
            "direct acquire failure retried before its backoff");
        auto decision = recovery.beforePresent(start + failed.retryDelay);
        expect(decision.beginHistoryWarmup && decision.limitGeneratedFrames &&
                decision.preacquireGeneratedFrame &&
                decision.boundedAcquireProbe,
            "direct acquire failure did not reach one bounded retry");
        const auto missed = recovery.reportNonblockingProbeUnavailable(
            start + failed.retryDelay
        );
        expect(missed.quarantined && missed.boundedProbeFailed &&
                missed.retryDelay == 500ms,
            "failed bounded probe did not return to finite backoff");
        decision = recovery.beforePresent(
            start + failed.retryDelay + missed.retryDelay
        );
        expect(decision.boundedAcquireProbe,
            "second direct retry did not become eligible");
        const auto recovered = recovery.observe(
            start + failed.retryDelay + missed.retryDelay,
            false, false, true
        );
        expect(recovered.recovered && !recovery.active(),
            "successful bounded probe did not end direct recovery");
    }

    void testExternalInterruptionDiscardsOldFailure() {
        OrderedAcquireRecovery recovery;
        const auto now = OrderedAcquireRecovery::TimePoint{};
        recovery.armTransitionRecreation();
        static_cast<void>(recovery.observe(now, true));
        recovery.pauseForExternalInterruption();
        const auto decision = recovery.beforePresent(now + 1s);
        expect(!recovery.active() && !recovery.transitionRecoveryActive() &&
                !recovery.signalRecreation() &&
                !decision.bypassGeneration &&
                !decision.preacquireGeneratedFrame,
            "menu interruption resumed stale acquire recovery evidence");
    }
}

int main() {
    testOrderedAcquireUsesExplicitFailureOnly();
    testOrderedAcquireBackoffAndProbeAreFinite();
    testExternalInterruptionDiscardsOldFailure();
    expect(!shouldRejectManagedMultiSwapchainPresent(1, true) &&
            !shouldRejectManagedMultiSwapchainPresent(2, false) &&
            shouldRejectManagedMultiSwapchainPresent(2, true),
        "managed multi-swapchain batches must fail closed before shared waits are consumed");

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
    expect(!orderedGeneratedBatchNeedsNonblockingAdmission(3, 5, 0) &&
            !orderedGeneratedBatchNeedsNonblockingAdmission(3, 5, 1) &&
            !orderedGeneratedBatchNeedsNonblockingAdmission(3, 5, 2) &&
            !orderedGeneratedBatchNeedsNonblockingAdmission(4, 5, 1) &&
            orderedGeneratedBatchNeedsNonblockingAdmission(3, 4, 2) &&
            !orderedGeneratedBatchNeedsNonblockingAdmission(4, 6, 1) &&
            orderedGeneratedBatchNeedsNonblockingAdmission(4, 3, 1),
        "ordered admission did not distinguish a fitting batch from insufficient headroom");
    const GamescopePresentationFeedback fixedRefresh{
        .vrrEnabled = false,
        .vrrCapable = true,
        .vrrActive = false,
        .allowTearing = false,
    };
    const GamescopePresentationFeedback fixedRefreshWithTearing{
        .vrrEnabled = false,
        .vrrCapable = true,
        .vrrActive = false,
        .allowTearing = true,
    };
    const GamescopePresentationFeedback requestedVrr{
        .vrrEnabled = true,
        .vrrCapable = true,
        .vrrActive = false,
        .allowTearing = false,
    };
    const GamescopePresentationFeedback activeVrr{
        .vrrEnabled = true,
        .vrrCapable = true,
        .vrrActive = true,
        .allowTearing = true,
    };
    const GamescopePresentationFeedback incapableVrr{
        .vrrEnabled = true,
        .vrrCapable = false,
        .vrrActive = false,
        .allowTearing = true,
    };
    const auto fixedPolicy = selectAdaptiveOrderedDeliveryPolicy(
        true, true, false, false, false, fixedRefresh, 1
    );
    expect(fixedPolicy ==
                AdaptiveOrderedDeliveryPolicy::NotApplicable &&
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, false, false,
                fixedRefreshWithTearing, 1
            ) == fixedPolicy &&
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, false, false, incapableVrr, 1
            ) == fixedPolicy &&
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, false, false, {}, 2
            ) == fixedPolicy,
        "Adaptive fixed-refresh delivery diverged from the 3.3 ordered path");
    const auto variablePolicy = selectAdaptiveOrderedDeliveryPolicy(
        true, true, false, false, false, requestedVrr, 1
    );
    expect(variablePolicy ==
                AdaptiveOrderedDeliveryPolicy::VariableRefreshBounded &&
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, false, false, activeVrr, 2
            ) == variablePolicy,
        "Adaptive VRR feedback did not select bounded delivery");
    for (const auto policy : {
            selectAdaptiveOrderedDeliveryPolicy(
                false, true, false, false, false, fixedRefresh, 1),
            selectAdaptiveOrderedDeliveryPolicy(
                true, false, false, false, false, fixedRefresh, 1),
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, true, false, false, fixedRefresh, 1),
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, true, false, fixedRefresh, 1),
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, false, true, fixedRefresh, 1),
            selectAdaptiveOrderedDeliveryPolicy(
                true, true, false, false, false, fixedRefresh, 0),
        }) {
        expect(policy == AdaptiveOrderedDeliveryPolicy::NotApplicable,
            "non-normal Adaptive delivery selected a pacing adapter");
    }
    expect(adaptiveVariableRefreshDeliveryAcquireBudget(
                variablePolicy, 50'000'000) ==
                    50'000'000 &&
            adaptiveVariableRefreshDeliveryAcquireBudget(
                variablePolicy, 20'000'000) ==
                    20'000'000 &&
            adaptiveVariableRefreshDeliveryAcquireBudget(
                variablePolicy, std::nullopt) ==
                    50'000'000 &&
            adaptiveVariableRefreshDeliveryAcquireBudget(
                fixedPolicy, 17'000'000) ==
                    17'000'000,
        "Adaptive VRR delivery lost its finite application-present ceiling");
    expect(adaptiveOrderedDeliveryNeedsPressurePreflight(
                variablePolicy, true, 1) &&
            !adaptiveOrderedDeliveryNeedsPressurePreflight(
                fixedPolicy, true, 1) &&
            !adaptiveOrderedDeliveryNeedsPressurePreflight(
                variablePolicy, false, 1) &&
            !adaptiveOrderedDeliveryNeedsPressurePreflight(
                variablePolicy, true, 0),
        "Adaptive ordered pressure did not enter bounded zero-wait retry");
    expect(adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 2, 0) &&
            adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 2, 1) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 2, 2) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 0, 0) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                false, true, 2, 0) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                true, false, 2, 0),
        "Adaptive ordered delivery misses lost immediate fallback policy");
    for (uint32_t applicationImages = 2; applicationImages <= 4; ++applicationImages) {
        for (size_t generated = 1; generated <= 4; ++generated) {
            const size_t images = applicationImages + generated;
            expect(!orderedGeneratedBatchNeedsNonblockingAdmission(
                    applicationImages, images, generated),
                "a fitting generated batch was dropped solely for lacking a relief image");
            expect(orderedGeneratedBatchNeedsNonblockingAdmission(
                    applicationImages, images - 1, generated),
                "an undersized pool lost its nonblocking admission protection");
            expect(orderedGeneratedBatchAcquireBudget(
                    applicationImages, images, generated, std::nullopt) == 50'000'000 &&
                    orderedGeneratedBatchAcquireBudget(
                        applicationImages, images, generated, 100'000'000) == 50'000'000 &&
                    orderedGeneratedBatchAcquireBudget(
                        applicationImages, images, generated, 5'000'000) == 5'000'000,
                "tight-pool delivery lost its shared finite ceiling or shorter user limit");
            expect(!orderedGeneratedBatchAcquireBudget(
                    applicationImages, images + 1, generated, std::nullopt),
                "relief-bearing ordered delivery changed its existing acquire contract");
        }
    }
    expect(!orderedGeneratedBatchAcquireBudget(3, 3, 0, std::nullopt) &&
            !orderedGeneratedBatchAcquireBudget(3, 2, 1, std::nullopt),
        "empty or malformed pools gained a synthetic acquire budget");
    expect(adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, true, true, 2, 1, std::nullopt) == 1 &&
            adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, true, true, 4, 2, 3) == 2,
        "partial Adaptive ordered admission did not retain its proven lower capacity");
    expect(!adaptiveOrderedWsiLimitAfterPartialAdmission(
                false, true, true, 2, 1, std::nullopt) &&
            !adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, false, true, 2, 1, std::nullopt) &&
            !adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, true, false, 2, 1, std::nullopt) &&
            !adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, true, true, 2, 0, std::nullopt) &&
            !adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, true, true, 2, 2, std::nullopt) &&
            !adaptiveOrderedWsiLimitAfterPartialAdmission(
                true, true, true, 3, 2, 1),
        "Adaptive ordered admission limit escaped its partial, tighter, nonzero boundary");
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
    expect(orderedRecoveryAcquireTimeout(120, acquireBudget) == acquireBudget &&
            orderedRecoveryAcquireTimeout(60, acquireBudget) == acquireBudget,
        "recovery probe ignored the configured acquire contract");
    expect(orderedRecoveryAcquireTimeout(120, std::nullopt) == 8'333'334 &&
            orderedRecoveryAcquireTimeout(60, std::nullopt) == 16'666'667,
        "unconfigured recovery probe lost its one-period display contract");
    expect(orderedRecoveryAcquireTimeout(std::nullopt, std::nullopt) == 0,
        "unknown unconfigured recovery invented a timeout");
    expect(orderedGeneratedImageAcquireTimeout(120, acquireBudget) ==
            acquireBudget,
        "per-image acquire did not use the authoritative remaining budget");
    expect(orderedGeneratedImageAcquireTimeout(120, 17'194'900) ==
            17'194'900,
        "the per-image ceiling ignored the remaining cumulative budget");
    expect(orderedGeneratedImageAcquireTimeout(120, 7'500'000) ==
            7'500'000,
        "the per-image ceiling exceeded the remaining cumulative budget");
    for (const uint32_t refresh : {40U, 60U, 90U, 120U, 240U, 360U}) {
        expect(orderedGeneratedImageAcquireTimeout(refresh, acquireBudget) ==
                acquireBudget,
            "refresh-rate heuristic changed the configured acquire budget");
    }
    expect(orderedGeneratedImageAcquireTimeout(
            std::nullopt, acquireBudget) == acquireBudget,
        "unknown refresh changed the configured acquire budget");
    expect(orderedGeneratedImageAcquireTimeout(120, std::nullopt) ==
            std::numeric_limits<uint64_t>::max(),
        "an unconfigured ordered path unexpectedly gained a finite timeout");

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
    const auto pendingRecovery = admission.reportAvailable(2);
    expect(!pendingRecovery.resumed && admission.underPressure(),
        "one available batch prematurely re-armed admission");
    expect(admission.reportUnavailable(),
        "a miss after partial recovery lost pressure diagnostics");
    admission.reportBypassedFrame();
    const auto pendingAgain = admission.reportAvailable(2);
    expect(!pendingAgain.resumed && admission.underPressure(),
        "interrupted admission stability was not reset");
    const auto recovery = admission.reportAvailable(2);
    expect(recovery.resumed && recovery.missedAttempts == 4 &&
            recovery.bypassedFrames == 3 && recovery.stableBatches == 2 &&
            recovery.requiredStableBatches == 2,
        "admission recovery lost its aggregated pressure counters");
    expect(!admission.underPressure(),
        "admission recovery did not reset pressure");
    expect(generatedImageAdmissionRecoveryBatches(7, 2) == 3 &&
            generatedImageAdmissionRecoveryBatches(7, 1) == 4 &&
            generatedImageAdmissionRecoveryBatches(4, 3) == 1 &&
            generatedImageAdmissionRecoveryBatches(0, 2) == 1 &&
            generatedImageAdmissionRecoveryBatches(7, 0) == 1,
        "admission recovery was not derived from lower WSI turnover");

    expect(OrderedAcquireRecovery::maximumRetryDelay() == 30s,
        "ordered acquire recovery must cap direct-failure retry delay");
    expect(!preacquiredImagesRequireRetirement(false, 1) &&
            !preacquiredImagesRequireRetirement(true, 0) &&
            preacquiredImagesRequireRetirement(true, 1),
        "pre-acquired image retirement lost its ownership contract");

    ScalingAdmissionRetrySurfaceBudget scalingRetryBudget;
    const ScalingAdmissionRetryKey firstScalingEpisode{1920, 1080, 7};
    const ScalingAdmissionRetryKey changedSourceEpisode{2560, 1440, 7};
    const ScalingAdmissionRetryKey changedPolicyEpisode{2560, 1440, 8};
    expect(scalingRetryBudget.available(firstScalingEpisode),
        "fresh scaling admission episode did not allow one retry");
    scalingRetryBudget.record(firstScalingEpisode);
    expect(!scalingRetryBudget.available(firstScalingEpisode) &&
            scalingRetryBudget.available(changedSourceEpisode),
        "scaling admission retry looped or failed to reset for a new source");
    scalingRetryBudget.record(changedSourceEpisode);
    expect(!scalingRetryBudget.available(changedSourceEpisode) &&
            scalingRetryBudget.available(changedPolicyEpisode),
        "scaling admission retry did not distinguish a new policy revision");

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

    SmoothCadenceBaseCap cadenceBaseCap;
    SmoothCadenceBaseCap::SchedulerState cadenceSnapshot{
        .validatedGenerationLimit = 2,
        .smoothedBaseFps = 45.0,
    };
    auto cadenceCap = cadenceBaseCap.update(
        pacingStart, true, 120, cadenceSnapshot
    );
    expect(!cadenceCap.framesPerSecond && !cadenceCap.changed,
        "Steady integer cadence activated without qualification");
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 999ms, true, 120, cadenceSnapshot
    );
    expect(!cadenceCap.framesPerSecond,
        "Steady integer cadence activated before its qualification hold");
    cadenceCap = cadenceBaseCap.update(
        pacingStart + SmoothCadenceBaseCap::qualificationDuration(),
        true, 120, cadenceSnapshot
    );
    expect(cadenceCap.framesPerSecond && cadenceCap.changed &&
            cadenceCap.multiplier == 3 &&
            std::abs(*cadenceCap.framesPerSecond - 40.0) < 0.001,
        "Steady Adaptive did not align a proven 3x load to 40 -> 120 FPS");
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 2s, true, 120, cadenceSnapshot
    );
    expect(cadenceCap.framesPerSecond && !cadenceCap.changed,
        "retained Steady integer cadence reported a false transition");

    cadenceSnapshot.rampEvaluationActive = true;
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 3s, true, 120, cadenceSnapshot
    );
    expect(!cadenceCap.framesPerSecond && cadenceCap.changed,
        "an Adaptive ramp did not restore the conservative target/2 cap");
    cadenceSnapshot.rampEvaluationActive = false;
    cadenceSnapshot.validatedGenerationLimit = 1;
    cadenceSnapshot.smoothedBaseFps = 58.0;
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 5s, true, 120, cadenceSnapshot
    );
    expect(!cadenceCap.framesPerSecond && !cadenceCap.changed,
        "the integer ladder altered a healthy near-2x source cadence");

    cadenceSnapshot.validatedGenerationLimit = 3;
    cadenceSnapshot.smoothedBaseFps = 32.0;
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 6s, true, 120, cadenceSnapshot
    );
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 7s, true, 120, cadenceSnapshot
    );
    expect(cadenceCap.framesPerSecond && cadenceCap.multiplier == 4 &&
            std::abs(*cadenceCap.framesPerSecond - 30.0) < 0.001,
        "Steady Adaptive did not align a proven 4x load to 30 -> 120 FPS");
    cadenceCap = cadenceBaseCap.update(
        pacingStart + 8s, false, 120, cadenceSnapshot
    );
    expect(!cadenceCap.framesPerSecond && cadenceCap.changed,
        "losing the ordered target-matched guard did not restore the base cap");

    // RE4 hovered around the 95% entry threshold (38 FPS for 40 -> 120).
    // Once qualified, keep the pacer stable across that boundary on every
    // supported integer rung, including handheld refresh rates.
    for (const uint32_t target : {60U, 90U, 120U, 144U}) {
        for (size_t multiplier = 3; multiplier <= 5; ++multiplier) {
            SmoothCadenceBaseCap stableCap;
            auto now = pacingStart;
            const double cap = static_cast<double>(target) / multiplier;
            SmoothCadenceBaseCap::SchedulerState snapshot{
                .validatedGenerationLimit = multiplier - 1,
                .smoothedBaseFps = cap * 0.97,
            };
            static_cast<void>(stableCap.update(now, true, target, snapshot));
            now += 1s;
            auto decision = stableCap.update(now, true, target, snapshot);
            expect(decision.changed && decision.framesPerSecond &&
                    std::abs(*decision.framesPerSecond - cap) < 0.001,
                "integer cadence matrix failed to qualify its initial cap");

            const auto jitterUntil = now + 95s;
            size_t frame = 0;
            while (now < jitterUntil) {
                snapshot.smoothedBaseFps = cap * (frame++ % 2 ? 0.94 : 0.97);
                now += std::chrono::duration_cast<
                    SmoothCadenceBaseCap::Clock::duration>(
                        std::chrono::duration<double>{
                            1.0 / snapshot.smoothedBaseFps});
                decision = stableCap.update(now, true, target, snapshot);
                expect(decision.framesPerSecond && !decision.changed,
                    "entry-boundary jitter repeatedly reset Smooth Cadence pacing");
            }

            snapshot.smoothedBaseFps = cap * 0.85;
            static_cast<void>(stableCap.update(now, true, target, snapshot));
            now += 100ms;
            snapshot.smoothedBaseFps = cap * 0.94;
            decision = stableCap.update(now, true, target, snapshot);
            expect(decision.framesPerSecond && !decision.changed,
                "a short cadence dip unnecessarily released the qualified cap");

            now += 1s;
            snapshot.smoothedBaseFps = cap * 0.85;
            decision = stableCap.update(now, true, target, snapshot);
            expect(decision.framesPerSecond && !decision.changed,
                "a recovered dip left stale cap-release evidence");
            decision = stableCap.update(now + 249ms, true, target, snapshot);
            expect(decision.framesPerSecond && !decision.changed,
                "cap release ignored its sustained-loss hold");
            decision = stableCap.update(now + 250ms, true, target, snapshot);
            expect(!decision.framesPerSecond && decision.changed,
                "sustained cadence loss could not release the integer cap");

            now += 251ms;
            snapshot.smoothedBaseFps = cap * 0.97;
            decision = stableCap.update(now, true, target, snapshot);
            expect(!decision.framesPerSecond && !decision.changed,
                "released cap reactivated without new stable evidence");
            now += 1s;
            decision = stableCap.update(now, true, target, snapshot);
            expect(decision.framesPerSecond && decision.changed,
                "healthy cadence could not requalify after a genuine slowdown");

            snapshot.smoothedBaseFps = static_cast<double>(target) /
                (multiplier - 1);
            static_cast<void>(stableCap.update(now, true, target, snapshot));
            decision = stableCap.update(now + 250ms, true, target, snapshot);
            expect(!decision.framesPerSecond && decision.changed,
                "a sustained return to a lower multiplier retained the old cap");
        }
    }

    // Safety and scheduler transitions bypass the cadence-only hold, even
    // when a release timer is already pending.
    for (size_t guard = 0; guard < 5; ++guard) {
        SmoothCadenceBaseCap guardedCap;
        SmoothCadenceBaseCap::SchedulerState snapshot{
            .validatedGenerationLimit = 2,
            .smoothedBaseFps = 40.0,
        };
        static_cast<void>(guardedCap.update(pacingStart, true, 120, snapshot));
        static_cast<void>(guardedCap.update(pacingStart + 1s, true, 120, snapshot));
        snapshot.smoothedBaseFps = 34.0;
        static_cast<void>(guardedCap.update(pacingStart + 2s, true, 120, snapshot));
        snapshot.rampEvaluationActive = guard == 1;
        snapshot.rearmRequired = guard == 2;
        snapshot.discontinuityRecoveryActive = guard == 3;
        snapshot.validatedGenerationLimit = guard == 4 ? 1 : 2;
        const auto decision = guardedCap.update(
            pacingStart + 2010ms, guard != 0, 120, snapshot);
        expect(!decision.framesPerSecond && decision.changed,
            "cadence retention delayed a transport or scheduler safety exit");
    }

    SmoothCadenceBaseCap probedCap;
    SmoothCadenceBaseCap::SchedulerState probeSnapshot{
        .validatedGenerationLimit = 2,
        .smoothedBaseFps = 40.0,
    };
    static_cast<void>(probedCap.update(pacingStart, true, 120, probeSnapshot));
    static_cast<void>(probedCap.update(pacingStart + 1s, true, 120, probeSnapshot));
    probeSnapshot.smoothedBaseFps = 34.0;
    static_cast<void>(probedCap.update(pacingStart + 2s, true, 120, probeSnapshot));
    probeSnapshot.efficiencyProbeGenerationLimit = 1;
    auto probeCap = probedCap.update(pacingStart + 2010ms, true, 120, probeSnapshot);
    expect(probeCap.framesPerSecond == 60.0 && probeCap.changed,
        "cadence retention prevented a lower-load probe from changing its cap");
    probeSnapshot.efficiencyProbeGenerationLimit.reset();
    probeSnapshot.smoothedBaseFps = 37.7;
    probeCap = probedCap.update(pacingStart + 3s, true, 120, probeSnapshot);
    expect(probeCap.framesPerSecond == 40.0 && probeCap.changed,
        "a rejected lower-load probe failed to restore its retained integer cap");

    SmoothCadencePacerHandoff pacerHandoff;
    auto handoff = pacerHandoff.update(pacingStart, 1);
    expect(handoff.active && handoff.changed && handoff.generationLimit == 1,
        "qualified Smooth Cadence did not hand pacing to ordered FIFO");
    handoff = pacerHandoff.update(pacingStart + 1s, 1);
    expect(handoff.active && !handoff.changed,
        "retained ordered-FIFO handoff reported a false transition");
    handoff = pacerHandoff.update(pacingStart + 2s, std::nullopt);
    expect(!handoff.active && handoff.changed,
        "lost Smooth Cadence qualification did not restore the base cap");
    handoff = pacerHandoff.update(pacingStart + 30s, 1);
    expect(!handoff.active && !handoff.changed,
        "failed pacing handoff retried before its long cooldown");
    handoff = pacerHandoff.update(pacingStart + 30s, 2);
    expect(handoff.active && handoff.changed && handoff.generationLimit == 2,
        "2x pacing cooldown blocked a separately validated 3x rung");
    handoff = pacerHandoff.update(pacingStart + 31s, std::nullopt);
    expect(!handoff.active && handoff.changed &&
            handoff.previousGenerationLimit == 2,
        "lost 3x qualification did not restore the base cap");
    handoff = pacerHandoff.update(pacingStart + 32s, 2);
    expect(!handoff.active,
        "failed 3x handoff retried before its own cooldown");
    handoff = pacerHandoff.update(
        pacingStart + 2s + SmoothCadencePacerHandoff::retryDelay(), 1
    );
    expect(handoff.active && handoff.changed,
        "eligible pacing handoff did not retry after its cooldown");
    pacerHandoff.reset();
    handoff = pacerHandoff.update(pacingStart + 3s, 1);
    expect(handoff.active && handoff.changed,
        "explicit pacing-handoff reset retained stale cooldown state");
    pacerHandoff.pauseForExternalInterruption();
    handoff = pacerHandoff.update(pacingStart + 4s, std::nullopt);
    expect(!handoff.active && !handoff.changed,
        "menu suspension retained an active FIFO handoff");
    handoff = pacerHandoff.update(pacingStart + 5s, 1);
    expect(handoff.active && handoff.changed,
        "menu suspension invented a gameplay pacing cooldown");
    static_cast<void>(pacerHandoff.update(pacingStart + 6s, std::nullopt));
    pacerHandoff.pauseForExternalInterruption();
    handoff = pacerHandoff.update(pacingStart + 7s, 1);
    expect(!handoff.active,
        "menu suspension erased a genuine earlier pacing-failure cooldown");
    pacerHandoff.reset();
    handoff = pacerHandoff.update(pacingStart + 8s, 2);
    expect(handoff.active && handoff.generationLimit == 2,
        "validated 3x did not enter its VRR FIFO handoff before menu focus");
    pacerHandoff.pauseForExternalInterruption();
    handoff = pacerHandoff.update(pacingStart + 9s, std::nullopt);
    expect(!handoff.active && !handoff.changed,
        "Steam menu focus retained the 3x FIFO handoff");
    handoff = pacerHandoff.update(pacingStart + 10s, 2);
    expect(handoff.active && handoff.changed && handoff.generationLimit == 2,
        "Steam menu return imposed a failure cooldown on validated 3x");

    for (size_t remaining = 0; remaining <= 3; ++remaining) {
        expect(historyWarmupFramesAfterRequest(remaining, 3, true) == 3,
            "a new Fixed menu return retained incomplete pre-menu history");
        expect(historyWarmupFramesAfterRequest(remaining, 3, false) ==
                (remaining == 0 ? 3 : remaining),
            "ordinary Fixed readiness checks restarted an active warm-up");
    }
    auto menuWarmupRemaining = historyWarmupFramesAfterRequest(0, 3, true);
    --menuWarmupRemaining;
    menuWarmupRemaining = historyWarmupFramesAfterRequest(menuWarmupRemaining, 3, true);
    for (size_t frame = 0; frame < 3; ++frame) {
        expect(menuWarmupRemaining == 3 - frame,
            "second Fixed interruption did not require three new real frames");
        --menuWarmupRemaining;
    }
    expect(menuWarmupRemaining == 0,
        "Fixed history warm-up did not complete after three fresh frames");

    const auto warmupFrameStarted =
        std::chrono::steady_clock::time_point{} + 100ms;
    const auto warmupRecoveryCompleted = warmupFrameStarted + 12ms;
    expect(historyWarmupCadenceBoundary(
            warmupFrameStarted, warmupRecoveryCompleted, false) ==
            warmupRecoveryCompleted,
        "ordinary scheduler recovery stopped using the 3.3 completion boundary");
    expect(historyWarmupCadenceBoundary(
            warmupFrameStarted, warmupRecoveryCompleted, true) ==
            warmupFrameStarted,
        "event recovery included private work in resumed gameplay cadence");

    expect(automaticRecoveryRecreationAllowed(false) &&
            !automaticRecoveryRecreationAllowed(true) &&
            automaticRecoveryRecreationAllowed(true, true) &&
            !automaticRecoveryRecreationAllowed(true, true, true),
        "staged recovery recreation crossed the combined spatial-scaling safety boundary");

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

    // With Smooth Cadence and ordered FIFO, Fixed requests its full multiplier
    // and lets the present queue provide back-pressure instead of sleeping in
    // the game present call. First-frame and long-stall guards still apply.
    for (const uint32_t refreshHz : {60U, 90U, 120U, 144U}) {
        for (size_t multiplier = 2; multiplier <= 5; ++multiplier) {
            budget.reset();
            const size_t maximumGenerated = multiplier - 1;
            expect(budget.plan(start, refreshHz, maximumGenerated, true) == 0,
                "FIFO-paced Fixed generated on its first timing sample");
            for (size_t frame = 1; frame <= 30; ++frame) {
                expect(budget.plan(
                    start + frame * 10ms, refreshHz,
                    maximumGenerated, true
                ) == maximumGenerated,
                    "FIFO-paced Fixed suppressed part of its multiplier");
            }
            expect(budget.plan(
                start + 1s, refreshHz, maximumGenerated, true
            ) == 0,
                "FIFO-paced Fixed ignored a long timing discontinuity");

            // FIFO backpressure can return slightly early. Keep the selected
            // multiplier stable instead of dropping one output because the
            // fractional refresh budget saw a short interval.
            budget.reset();
            expect(budget.plan(start, refreshHz, maximumGenerated, true) == 0,
                "FIFO-paced Fixed generated on its first timing sample");
            for (size_t frame = 1; frame <= 16; ++frame) {
                const double seconds =
                    static_cast<double>(frame * multiplier) /
                        static_cast<double>(refreshHz) +
                    (frame % 2 == 0 ? 0.001 : -0.001);
                const auto when = start +
                    std::chrono::duration_cast<
                        std::chrono::steady_clock::duration
                    >(std::chrono::duration<double>(seconds));
                expect(budget.plan(
                    when, refreshHz, maximumGenerated, true
                ) == maximumGenerated,
                    "FIFO timing jitter changed the Fixed multiplier");
            }
        }
    }

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
