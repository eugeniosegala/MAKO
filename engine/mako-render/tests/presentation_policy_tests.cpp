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

    OrderedAcquireRecovery::TimePoint enterMaximumAcquireBackoff(
            OrderedAcquireRecovery& recovery) {
        auto now = OrderedAcquireRecovery::TimePoint{};
        auto delay = recovery.observe(now, 50ms, 25ms, true).retryDelay;
        for (size_t failure = 1; failure < 7; ++failure) {
            now += delay;
            expect(recovery.beforePresent(now).boundedAcquireProbe,
                "backoff setup did not permit its scheduled bounded probe");
            delay = recovery.reportNonblockingProbeUnavailable(now).retryDelay;
        }
        expect(delay == 30s,
            "backoff setup did not reach the persistent-pressure ceiling");
        return now;
    }

    void testEmptyFixedBudgetDoesNotFailPendingAcquireProbe() {
        // A slightly early 45 FPS frame at 90 Hz can legitimately spend the
        // display's fractional credit on only the real image, immediately
        // after recovery history has warmed. No Vulkan acquire runs then.
        for (const bool nativeDrain : {false, true}) {
            OrderedAcquireRecovery recovery;
            auto now = OrderedAcquireRecovery::TimePoint{};
            const auto miss = recovery.observe(now,
                nativeDrain ? 50ms : 17ms, 25ms, true);
            if (nativeDrain)
                now += miss.retryDelay;
            auto decision = recovery.beforePresent(now);
            expect(decision.limitGeneratedFrames &&
                    decision.boundedAcquireProbe == nativeDrain,
                "empty-budget scenario did not arm the expected probe");

            FixedRefreshBudget budget;
            expect(budget.plan(now, 90, 1) == 0,
                "fresh display budget must start with the real frame");
            for (size_t frame = 0; frame < 2; ++frame) {
                now += 22'400us;
                const auto generated = budget.plan(now, 90, 1);
                expect(generated == 1 &&
                        !orderedAcquireProbeEligible(true, true, generated),
                    "history warm-up attempted a recovery acquire");
            }
            now += 21'600us;
            const auto empty = budget.plan(now, 90, 1);
            expect(empty == 0,
                "early 45 FPS frame did not reproduce an empty Fixed budget");
            expect(!orderedAcquireProbeEligible(
                    decision.limitGeneratedFrames, false, empty),
                "empty display budget manufactured an acquire-probe failure");

            now += 22'400us;
            decision = recovery.beforePresent(now);
            const auto generated = budget.plan(now, 90, 1);
            expect(generated == 1 && orderedAcquireProbeEligible(
                    decision.limitGeneratedFrames, false, generated) &&
                    decision.boundedAcquireProbe == nativeDrain &&
                    decision.consecutiveFailures == (nativeDrain ? 1U : 0U),
                "real-only frame lost its pending probe or increased backoff");
            const auto resumed = recovery.observe(now, 0ns, 25ms, false,
                false, decision.boundedAcquireProbe);
            expect(resumed.recovered && resumed.guardCleared == !nativeDrain,
                "next eligible generated frame did not complete recovery");
        }
        for (size_t generated = 0; generated <= 4; ++generated) {
            expect(!orderedAcquireProbeEligible(false, false, generated),
                "ordinary generation incorrectly entered the recovery probe path");
        }
    }

    void testLongAcquireBackoffResumesOnNativeDemand() {
        OrderedAcquireRecovery recovery;
        auto now = enterMaximumAcquireBackoff(recovery);
        const auto originalRetryAt = now + 30s;
        size_t nativeHoldEntries = 0;
        // Reproduce the review case: a target-rate menu during a long drain
        // must qualify the native hold before its retry deadline expires.
        for (size_t frame = 0; frame < 600; ++frame) {
            now += 8'333'333ns;
            const auto decision = recovery.beforePresent(
                now, 8'333'333ns, 120.0
            );
            nativeHoldEntries += decision.nativeCadenceSaturationEntered;
            expect(decision.bypassGeneration &&
                    !decision.beginHistoryWarmup &&
                    !decision.preacquireGeneratedFrame,
                "target-rate native hold scheduled synthetic work");
        }
        expect(nativeHoldEntries == 1,
            "long backoff postponed native qualification until its retry deadline");

        // Near-target variation and isolated slow frames must not accumulate
        // into a sustained demand signal or repeatedly skip the cooldown.
        for (size_t burst = 0; burst < 10; ++burst) {
            now += 16'666'667ns;
            expect(recovery.beforePresent(now, 16'666'667ns, 120.0).
                    bypassGeneration,
                "one slow native frame released long backoff");
            for (size_t frame = 0; frame < 30; ++frame) {
                const auto interval = frame % 2 == 0
                    ? 8'333'333ns : 8'928'571ns; // 120 / 112 FPS
                now += interval;
                const auto decision = recovery.beforePresent(
                    now, interval, 120.0
                );
                expect(decision.nativeCadenceSaturated &&
                        decision.bypassGeneration &&
                        !decision.beginHistoryWarmup &&
                        !decision.nativeCadenceSaturationEntered,
                    "brief native fluctuations escaped the qualified hold");
            }
        }

        const auto gameplayReturnedAt = now;
        bool resumed = false;
        for (size_t frame = 0; frame < 15; ++frame) {
            now += 16'666'667ns;
            const auto decision = recovery.beforePresent(
                now, 16'666'667ns, 120.0
            );
            if (!decision.nativeCadenceDemandResumed)
                continue;
            expect(now < originalRetryAt &&
                    now - gameplayReturnedAt >= 100ms &&
                    !decision.bypassGeneration &&
                    decision.beginHistoryWarmup &&
                    decision.limitGeneratedFrames &&
                    decision.preacquireGeneratedFrame &&
                    decision.boundedAcquireProbe,
                "native demand did not re-arm one bounded probe with fresh history");
            resumed = true;
            break;
        }
        expect(resumed,
            "gameplay return waited for long backoff instead of qualified demand");

        // An early probe is one opportunity, not proof of transport health.
        // Its failure must retain the full backoff and require new evidence.
        now += 1ms;
        const auto failedAt = now;
        const auto miss = recovery.reportNonblockingProbeUnavailable(now);
        expect(miss.quarantined && miss.boundedProbeFailed &&
                miss.retryDelay == 30s && miss.consecutiveFailures == 8,
            "failed demand probe erased persistent acquisition pressure");
        for (size_t frame = 0; frame < 120; ++frame) {
            now += 16'666'667ns;
            const auto decision = recovery.beforePresent(
                now, 16'666'667ns, 120.0
            );
            expect(decision.bypassGeneration &&
                    !decision.preacquireGeneratedFrame,
                "failed demand probe left an early-retry permission active");
        }
        expect(recovery.beforePresent(failedAt + 30s, 16'666'667ns, 120.0).
                boundedAcquireProbe,
            "persistent pressure lost its eventual bounded retry");
    }

    void testLongAcquireBackoffRequiresQualifiedNativeCadence() {
        // A persistently low native rate and a brief target-rate burst both
        // retain the long backoff; neither establishes a changed workload.
        for (const size_t targetRateFrames : {0U, 18U}) {
            OrderedAcquireRecovery recovery;
            auto now = enterMaximumAcquireBackoff(recovery);
            const auto retryAt = now + 30s;
            for (size_t frame = 0; frame < targetRateFrames; ++frame) {
                now += 8'333'333ns;
                const auto decision = recovery.beforePresent(
                    now, 8'333'333ns, 120.0
                );
                expect(!decision.nativeCadenceSaturated &&
                        decision.bypassGeneration,
                    "brief native burst qualified an early recovery hold");
            }
            while (now + 16'666'667ns < retryAt) {
                now += 16'666'667ns;
                const auto decision = recovery.beforePresent(
                    now, 16'666'667ns, 120.0
                );
                expect(decision.bypassGeneration &&
                        !decision.preacquireGeneratedFrame &&
                        !decision.nativeCadenceSaturated,
                    "unqualified native cadence bypassed persistent backoff");
            }
            expect(recovery.beforePresent(retryAt, 16'666'667ns, 120.0).
                    boundedAcquireProbe,
                "ordinary persistent-pressure retry did not remain finite");
        }
    }
}

int main() {
    testEmptyFixedBudgetDoesNotFailPendingAcquireProbe();
    testLongAcquireBackoffResumesOnNativeDemand();
    testLongAcquireBackoffRequiresQualifiedNativeCadence();
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
    expect(orderedGeneratedImageAcquireTimeout(120, acquireBudget) ==
            20'833'333,
        "one 120 Hz image lost its bounded FIFO release grace");
    expect(orderedGeneratedImageAcquireTimeout(120, 17'194'900) ==
            17'194'900,
        "the per-image ceiling ignored the remaining cumulative budget");
    expect(orderedGeneratedImageAcquireTimeout(120, 7'500'000) ==
            7'500'000,
        "the per-image ceiling exceeded the remaining cumulative budget");
    expect(orderedGeneratedImageAcquireTimeout(40, acquireBudget) ==
            37'500'000,
        "the per-image ceiling lost its low-refresh scaling");
    expect(orderedGeneratedImageAcquireTimeout(240, acquireBudget) ==
            10'416'667,
        "the per-image ceiling lost its high-refresh safety floor");
    expect(orderedGeneratedImageAcquireTimeout(60, acquireBudget) ==
            25'000'000 &&
            orderedGeneratedImageAcquireTimeout(90, acquireBudget) ==
                19'444'444 &&
            orderedGeneratedImageAcquireTimeout(360, acquireBudget) ==
                8'000'000,
        "acquire grace exceeded the pressure ceiling or lost the 8 ms floor");
    expect(orderedGeneratedImageAcquireTimeout(
            std::nullopt, acquireBudget) == 25'000'000,
        "an unknown-refresh path lost its historical finite ceiling");
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
    expect(OrderedAcquireRecovery::maximumRetryDelay() == 30s,
        "ordered acquire recovery must cap persistent retry delay at 30 seconds");
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
    // RE4 timeouts returned beyond both the 12.5 and 16.7 ms test deadlines.
    // If an image becomes available in this added grace, success must avoid
    // recovery. A timeout's return time does not prove image availability.
    for (const auto wait : {13'741us, 14'756us, 17'146us, 19'259us}) {
        expect(wait < std::chrono::nanoseconds{
                orderedGeneratedImageAcquireTimeout(120, acquireBudget)},
            "isolated RE4 acquire delay still exceeds the normal image budget");
        const auto recoveredAcquire = acquireRecovery.observe(
            acquireStart, wait, 25ms, false
        );
        expect(!recoveredAcquire.guardArmed &&
                !recoveredAcquire.quarantined && !acquireRecovery.active(),
            "successful short acquire unnecessarily disturbed generation");
    }
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

    // Extended deadlines must preserve the short-miss guard, including at
    // 90 Hz where using the full 25 ms threshold caused immediate native drain.
    // The historical 60 Hz window still reaches the pressure threshold.
    for (const uint32_t refresh : {60U, 72U, 90U, 120U, 144U, 240U}) {
        acquireRecovery.reset();
        const auto threshold =
            OrderedAcquireRecovery::slowAcquireDuration(refresh);
        const auto wait = std::chrono::nanoseconds{
            orderedGeneratedImageAcquireTimeout(refresh, acquireBudget)
        } + 1ms;
        observation = acquireRecovery.observe(
            acquireStart, wait, threshold, true
        );
        acquireDecision = acquireRecovery.beforePresent(acquireStart + wait + 1ms);
        if (refresh == 60U) {
            expect(observation.quarantined && observation.timedOut &&
                    acquireDecision.bypassGeneration &&
                    observation.retryDelay == 250ms,
                "pressure-limited acquire grace delayed native-drain protection");
            continue;
        }
        expect(observation.guardArmed && observation.timedOut &&
                !observation.quarantined &&
                acquireDecision.preacquireGeneratedFrame &&
                !acquireDecision.boundedAcquireProbe &&
                !acquireDecision.bypassGeneration,
            "short image deadline miss did not use zero-wait protection");
        const auto missedGuard =
            acquireRecovery.reportNonblockingProbeUnavailable(
                acquireStart + wait + 2ms
            );
        expect(missedGuard.guardBypassed && !missedGuard.quarantined,
            "short deadline guard miss skipped native relief");
        // Temporal warm-up sends no synthetic acquire observation. The next
        // failed normal acquire must retain the first miss as pressure proof.
        observation = acquireRecovery.observe(
            acquireStart + 90ms, wait, threshold, true
        );
        expect(observation.quarantined && observation.timedOut &&
                observation.retryDelay == 250ms,
            "repeated short timeouts escaped native-drain recovery");
    }

    acquireRecovery.reset();
    static_cast<void>(acquireRecovery.observe(
        acquireStart, 18ms, 25ms, true
    ));
    observation = acquireRecovery.observe(
        acquireStart + 17ms, 0ms, 25ms, false
    );
    expect(observation.guardCleared && !acquireRecovery.active(),
        "successful timeout guard did not permit a normal batch retry");
    observation = acquireRecovery.observe(
        acquireStart + 50ms, 18ms, 25ms, true
    );
    expect(observation.quarantined && observation.retryDelay == 250ms,
        "successful single-image guard erased repeated normal-batch timeouts");

    acquireRecovery.reset();
    static_cast<void>(acquireRecovery.observe(
        acquireStart, 18ms, 25ms, true
    ));
    static_cast<void>(acquireRecovery.observe(
        acquireStart + 17ms, 0ms, 25ms, false
    ));
    // Only an unrestricted healthy batch proves normal transport capacity.
    // A later isolated timeout must not inherit the earlier pressure count.
    observation = acquireRecovery.observe(
        acquireStart + 50ms, 5ms, 25ms, false
    );
    expect(!observation.quarantined && !acquireRecovery.active(),
        "healthy normal batch introduced a recovery constraint");
    observation = acquireRecovery.observe(
        acquireStart + 100ms, 18ms, 25ms, true
    );
    expect(observation.guardArmed && !observation.quarantined,
        "healthy normal batch did not clear earlier timeout evidence");

    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 18ms, 25ms, true, true
    );
    expect(observation.quarantined && observation.deadlineExceeded &&
            !observation.guardArmed,
        "short final image hid cumulative acquire-budget exhaustion");
    acquireRecovery.reset();
    observation = acquireRecovery.observe(
        acquireStart, 25ms, 25ms, true
    );
    expect(observation.quarantined && !observation.guardArmed,
        "timeout at the slow threshold bypassed native-drain recovery");

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
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 1000ms);
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.nativeOnlyStabilization &&
            !acquireDecision.limitGeneratedFrames &&
            !acquireDecision.preacquireGeneratedFrame &&
            acquireDecision.stabilizationRemaining == 150ms,
        "ordered recovery did not use deterministic native-only stabilization");
    for (const auto missAt : {1001ms, 1075ms, 1149ms}) {
        static_cast<void>(
            acquireRecovery.reportNonblockingProbeUnavailable(
                acquireStart + missAt
            )
        );
    }
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 1149ms);
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.nativeOnlyStabilization,
        "ordered recovery left native-only stabilization before its deadline");
    acquireDecision = acquireRecovery.beforePresent(acquireStart + 1150ms);
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

    OrderedAcquireRecovery nativeSaturationRecovery;
    observation = nativeSaturationRecovery.observe(
        acquireStart, 50ms, 25ms, true
    );
    expect(observation.quarantined &&
            observation.retryDelay == 250ms,
        "native-saturation scenario did not begin with a finite drain");
    size_t initialNativeHoldEntries = 0;
    for (size_t frame = 1; frame < 31; ++frame) {
        acquireDecision = nativeSaturationRecovery.beforePresent(
            acquireStart + 8ms * frame, 8ms, 120.0
        );
        initialNativeHoldEntries +=
            acquireDecision.nativeCadenceSaturationEntered;
        expect(acquireDecision.bypassGeneration &&
                !acquireDecision.beginHistoryWarmup,
            "native-saturation qualification attempted generation early");
    }
    acquireDecision = nativeSaturationRecovery.beforePresent(
        acquireStart + 250ms, 8ms, 120.0
    );
    initialNativeHoldEntries +=
        acquireDecision.nativeCadenceSaturationEntered;
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.nativeCadenceSaturated &&
            initialNativeHoldEntries == 1 &&
            !acquireDecision.beginHistoryWarmup &&
            !acquireDecision.boundedAcquireProbe &&
            acquireDecision.nativeBaseFps >= 119.0 &&
            acquireDecision.nativeTargetFps == 120.0,
        "target-satisfying native cadence did not suppress recovery churn");
    acquireDecision = nativeSaturationRecovery.beforePresent(
        acquireStart + 2000ms, 8ms, 120.0
    );
    expect(acquireDecision.bypassGeneration &&
            acquireDecision.nativeCadenceSaturated &&
            !acquireDecision.nativeCadenceSaturationEntered &&
            !acquireDecision.beginHistoryWarmup,
        "native-saturation hold repeated warm-up or probe work");

    bool nativeDemandResumed = false;
    for (size_t frame = 1; frame <= 20; ++frame) {
        acquireDecision = nativeSaturationRecovery.beforePresent(
            acquireStart + 2000ms + 17ms * frame, 17ms, 120.0
        );
        if (!acquireDecision.nativeCadenceDemandResumed)
            continue;
        nativeDemandResumed = true;
        expect(!acquireDecision.bypassGeneration &&
                acquireDecision.beginHistoryWarmup &&
                acquireDecision.limitGeneratedFrames &&
                acquireDecision.preacquireGeneratedFrame &&
                acquireDecision.boundedAcquireProbe &&
                acquireDecision.nativeBaseFps < 108.0,
            "native cadence deficit did not re-arm one bounded probe");
        break;
    }
    expect(nativeDemandResumed,
        "sustained native cadence deficit left recovery permanently held");

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
        500ms, 1000ms, 2000ms, 5000ms, 15000ms, 30000ms, 30000ms,
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
        250ms, 500ms, 1000ms, 2000ms, 5000ms, 15000ms, 30000ms,
        30000ms,
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
    pacerHandoff.pauseForExternalInterruption();
    handoff = pacerHandoff.update(pacingStart + 4s, false);
    expect(!handoff.active && !handoff.changed,
        "menu suspension retained an active FIFO handoff");
    handoff = pacerHandoff.update(pacingStart + 5s, true);
    expect(handoff.active && handoff.changed,
        "menu suspension invented a gameplay pacing cooldown");
    static_cast<void>(pacerHandoff.update(pacingStart + 6s, false));
    pacerHandoff.pauseForExternalInterruption();
    handoff = pacerHandoff.update(pacingStart + 7s, true);
    expect(!handoff.active,
        "menu suspension erased a genuine earlier pacing-failure cooldown");

    expect(LowerPresentStallRecovery::stallThreshold(120) == 250ms &&
            LowerPresentStallRecovery::stallThreshold(60) == 250ms &&
            LowerPresentStallRecovery::stallThreshold(40) == 250ms,
        "lower-present stall threshold lost its severe-hitch floor");
    LowerPresentStallRecovery presentStallRecovery;
    const auto presentStallStart =
        LowerPresentStallRecovery::TimePoint{};
    auto presentStall = presentStallRecovery.observe(
        presentStallStart, 49ms, 120
    );
    expect(!presentStall.quarantined &&
            !presentStallRecovery.active(),
        "a sub-threshold lower present entered recovery");
    for (const auto batch : {std::array{7ms, 47ms}, std::array{47ms, 7ms}}) {
        presentStall = presentStallRecovery.observe(
            presentStallStart, *std::max_element(batch.begin(), batch.end()), 120
        );
        expect(!presentStall.quarantined && !presentStallRecovery.active(),
            "separate sub-threshold FIFO waits were summed into a stall");
    }
    for (const auto duration : {50ms, 69ms, 114ms, 249ms}) {
        presentStall = presentStallRecovery.observe(
            presentStallStart + 1ms, duration, 120
        );
        expect(!presentStall.quarantined && !presentStallRecovery.active(),
            "a returned gameplay hitch entered lower-present quarantine");
    }
    presentStall = presentStallRecovery.observe(
        presentStallStart + 1ms, 621ms, 120
    );
    expect(presentStall.quarantined &&
            presentStall.threshold == 250ms &&
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
    presentStall = presentStallRecovery.observe(
        presentStallStart + 2200ms, 521ms, 120
    );
    expect(presentStall.quarantined &&
            presentStall.consecutiveStalls == 2 &&
            presentStall.stabilizationDuration == 10s,
        "a repeated lower-present collapse did not increase its retry backoff");
    presentStallDecision = presentStallRecovery.beforePresent(
        presentStallStart + 11s
    );
    expect(presentStallDecision.bypassGeneration,
        "repeated lower-present recovery retried inside its extended backoff");
    presentStallDecision = presentStallRecovery.beforePresent(
        presentStallStart + 13s
    );
    expect(presentStallDecision.recovered &&
            !presentStallRecovery.active(),
        "extended lower-present recovery did not end at its absolute deadline");
    presentStallRecovery.reset();

    LowerPresentStallRecovery nativeStallRecovery;
    auto nativeStall = nativeStallRecovery.observe(
        presentStallStart, 300ms, 120
    );
    expect(nativeStall.quarantined,
        "severe generated batch did not start native-only recovery");
    const auto healthyNative =
        nativeStallRecovery.observeNativeRecoveryPresent(
            presentStallStart + 100ms, 40ms, 120);
    expect(!healthyNative.newlyArmedRecreation &&
            !nativeStallRecovery.recreationRequested(),
        "healthy native recovery present armed a recreation");
    const auto isolatedNative = nativeStallRecovery.observeNativeRecoveryPresent(
        presentStallStart + 400ms, 280ms, 120);
    expect(!isolatedNative.newlyArmedRecreation &&
            !nativeStallRecovery.recreationRequested(),
        "one native overlay hitch requested a recreation");
    const auto failedNative = nativeStallRecovery.observeNativeRecoveryPresent(
        presentStallStart + 800ms, 280ms, 120);
    expect(failedNative.newlyArmedRecreation &&
            failedNative.severeNativeStalls == 2 &&
            nativeStallRecovery.recreationRequested(),
        "severe native recovery present did not arm recreation");
    presentStallDecision = nativeStallRecovery.beforePresent(
        presentStallStart + 3s
    );
    expect(presentStallDecision.bypassGeneration &&
            !presentStallDecision.recovered,
        "failed native FIFO resumed generation before recreation");
    expect(nativeStallRecovery.signalRecreation() &&
            !nativeStallRecovery.signalRecreation(),
        "native lower-present recreation signal was not one-shot");
    static_cast<void>(nativeStallRecovery.observeNativeRecoveryPresent(
        presentStallStart + 3100ms, 300ms, 120));
    expect(!nativeStallRecovery.recreationRequested(),
        "an ignored OUT_OF_DATE was signaled again on the same context");
    presentStallDecision = nativeStallRecovery.beforePresent(
        presentStallStart + 3s
    );
    expect(presentStallDecision.recovered &&
            presentStallDecision.beginHistoryWarmup,
        "signaled native FIFO recovery did not release its local guard");
    nativeStallRecovery.reset();
    static_cast<void>(nativeStallRecovery.observe(
        presentStallStart + 4s, 300ms, 120));
    for (size_t i = 1; i <= 4; ++i) {
        static_cast<void>(nativeStallRecovery.observeNativeRecoveryPresent(
            presentStallStart + 4s + i * 300ms, 280ms, 120));
    }
    expect(!nativeStallRecovery.recreationRequested(),
        "a private reset cleared the context's one-shot recreation guard");

    LowerPresentStallRecovery unavailableRecreation;
    static_cast<void>(unavailableRecreation.observe(
        presentStallStart, 300ms, 120));
    for (size_t i = 1; i < 30; ++i) {
        static_cast<void>(unavailableRecreation.observeNativeRecoveryPresent(
            presentStallStart + i * 1s, 300ms, 120));
        expect(unavailableRecreation.beforePresent(
                presentStallStart + i * 1s).bypassGeneration,
            "pending native recreation did not retain its bounded guard");
    }
    const auto boundedRetry = unavailableRecreation.beforePresent(
        presentStallStart + 30s);
    expect(boundedRetry.beginHistoryWarmup && boundedRetry.recreationWaitExpired &&
            !unavailableRecreation.active() &&
            !unavailableRecreation.recreationRequested(),
        "missing retirement proof or surface budget latched native-only forever");

    LowerPresentStallRecovery recoveredBeforeBudget;
    static_cast<void>(recoveredBeforeBudget.observe(
        presentStallStart, 300ms, 120));
    for (size_t i = 1; i <= 2; ++i) {
        static_cast<void>(recoveredBeforeBudget.observeNativeRecoveryPresent(
            presentStallStart + i * 300ms, 280ms, 120));
    }
    static_cast<void>(recoveredBeforeBudget.observeNativeRecoveryPresent(
        presentStallStart + 700ms, 40ms, 120));
    const auto cancelled = recoveredBeforeBudget.observeNativeRecoveryPresent(
        presentStallStart + 1700ms, 40ms, 120);
    expect(cancelled.cancelledRecreation &&
            !recoveredBeforeBudget.recreationRequested() &&
            recoveredBeforeBudget.beforePresent(
                presentStallStart + 2s).beginHistoryWarmup,
        "healthy native output retained a stale rebuild or extended its guard");
    static_cast<void>(recoveredBeforeBudget.observe(
        presentStallStart + 3s, 300ms, 120));
    recoveredBeforeBudget.reset();
    expect(!recoveredBeforeBudget.active() &&
            !recoveredBeforeBudget.recreationRequested(),
        "FG Off retained native-only recovery state");

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

    RecoveryPresentHealth outputHealth;
    auto outputNow = RecoveryPresentHealth::Clock::time_point{};
    outputHealth.beginPresent(outputNow);
    for (size_t frame = 0; frame < 100; ++frame) {
        // Alternating two/three outputs at 50 real FPS: 125 FPS delivered.
        for (size_t output = 0; output < 2 + frame % 2; ++output)
            outputHealth.observePresent(4ms, true);
        outputNow += 20ms;
        outputHealth.beginPresent(outputNow);
    }
    expect(outputHealth.outputFps() && *outputHealth.outputFps() >= 120.0,
        "fractional delivered output did not qualify recovery health");
    for (size_t frame = 0; frame < 30; ++frame) {
        outputHealth.observePresent(20ms, true);
        outputNow += 20ms;
        outputHealth.beginPresent(outputNow);
    }
    expect(outputHealth.outputFps() &&
            std::abs(*outputHealth.outputFps() - 50.0) < 0.001,
        "native-only output was multiplied by a selected 3x ceiling");
    outputHealth.observePresent(280ms, true);
    outputHealth.observePresent(40ms, true);
    expect(outputHealth.maximumPresentDuration() == 280ms,
        "stall health did not retain the longest individual lower present");
    outputNow += 2s;
    outputHealth.beginPresent(outputNow);
    expect(!outputHealth.outputFps() &&
            outputHealth.maximumPresentDuration() == 0ms,
        "a long menu pause retained stale output health or stall timing");
    for (size_t frame = 0; frame < 30; ++frame) {
        outputHealth.observePresent(4ms, true);
        outputHealth.observePresent(4ms, true);
        outputNow += 16ms;
        outputHealth.beginPresent(outputNow);
    }
    expect(outputHealth.outputFps() &&
            std::abs(*outputHealth.outputFps() - 125.0) < 0.001 &&
            std::abs(outputHealth.lowerPresentShare() - 0.5) < 0.001,
        "output and lower-present pressure were not paired with their completed intervals");
    outputHealth.observePresent(4ms, false);
    expect(!outputHealth.outputFps(),
        "failed lower present retained a healthy output classification");

    PersistentAdaptiveRecoveryRecreation request;
    expect(!request.signal() && request.signal(true) && !request.signal(true),
        "recreation requires qualification and stays one-shot for the context");
    expect(persistentAdaptiveRecoveryRecreationCooldown(0) == 0s &&
            persistentAdaptiveRecoveryRecreationCooldown(1) == 30s &&
            persistentAdaptiveRecoveryRecreationCooldown(20) == 30s,
        "surface spacing must not turn a menu visit into a five-minute cooldown");

    using SurfaceBudget = PersistentAdaptiveRecoverySurfaceBudget;
    using RecoverySample = SurfaceBudget::RecoverySample;
    SurfaceBudget spacing;
    auto spacingNow = SurfaceBudget::TimePoint{};
    expect(spacing.available(spacingNow) && spacing.severeLowerPresentAvailable(spacingNow),
        "a fresh surface incurred a recovery cooldown");
    for (size_t requestCount = 1; requestCount <= 20; ++requestCount) {
        spacing.recordRequest(spacingNow);
        expect(spacing.completedRequests() == requestCount && spacing.nextCooldown() == 30s,
            "surface request accounting reset or escalated the fixed cooldown");
        expect(!spacing.available(spacingNow + 29999ms) &&
                !spacing.severeLowerPresentAvailable(spacingNow + 29999ms) &&
                spacing.available(spacingNow + 30s) &&
                spacing.severeLowerPresentAvailable(spacingNow + 30s),
            "watchdog and native requests did not share exact 30-second spacing");
        spacingNow += 30s;
    }
    const RecoverySample healthySample{
        .contextId = 1, .configurationRevision = 2,
        .targetFps = 120, .refreshHz = 120,
        .extents = {2560, 1440, 3840, 2160},
        .focus = {.gameFocused = true},
        .outputFps = 120.0, .lowerPresentShare = 0.7,
    };
    auto watchdogNow = SurfaceBudget::TimePoint{};
    auto sample = healthySample;
    const auto observe = [&](auto& budget, const auto elapsed) {
        watchdogNow += elapsed;
        sample.focus.sampledAt = watchdogNow;
        return budget.observeSustainedDeficit(watchdogNow, sample);
    };
    const auto baseline = [&](auto& budget) {
        for (int i = 0; i < 6; ++i)
            expect(!observe(budget, 250ms).qualified, "healthy gameplay rebuilt");
    };
    const auto menu = [&](auto& budget, const auto duration) {
        sample.focus.gameFocused = false;
        sample.focus.openedAt = watchdogNow;
        sample.outputFps = 30.0;
        expect(!observe(budget, duration).qualified, "menu-open throttle rebuilt");
        sample.focus.gameFocused = true;
        sample.focus.returnedAt = watchdogNow;
        ++sample.focus.returnSequence;
        sample.outputFps = 70.0;
        return observe(budget, 25ms);
    };

    SurfaceBudget stalled;
    baseline(stalled);
    expect(!menu(stalled, 60s).qualified,
        "menu close rebuilt immediately without observing gameplay");
    expect(!observe(stalled, 3999ms).qualified,
        "post-menu watchdog skipped the four-second evidence window");
    expect(observe(stalled, 1ms).qualified && stalled.available(watchdogNow),
        "confirmed return after a long menu lost the pre-menu baseline");
    stalled.recordRequest(watchdogNow, true);
    const auto firstRequest = watchdogNow;
    ++sample.contextId;
    sample.outputFps.reset();
    expect(!observe(stalled, 1s).qualified, "replacement warm-up armed a rebuild");
    sample.outputFps = 70.0;
    static_cast<void>(observe(stalled, 25ms));
    expect(observe(stalled, 4s).qualified && !stalled.available(watchdogNow),
        "first failed rebuild forgot the episode or skipped surface spacing");
    watchdogNow = firstRequest + 30s;
    expect(observe(stalled, 0ms).qualified && stalled.available(watchdogNow),
        "failed first rebuild never became eligible for its bounded retry");
    stalled.recordRequest(watchdogNow, true);
    ++sample.contextId;
    for (int i = 0; i < 200; ++i)
        expect(!observe(stalled, 250ms).qualified, "two failed rebuilds entered a loop");
    sample.outputFps = 120.0;
    baseline(stalled);
    static_cast<void>(menu(stalled, 1s));
    expect(observe(stalled, 4s).qualified,
        "healthy gameplay could not recover a later independent menu visit");
    sample.outputFps = 100.0;
    expect(observe(stalled, 25ms).cancelled, "recovered output did not cancel deficit");
    static_cast<void>(observe(stalled, 1s));
    sample.outputFps = 70.0;
    static_cast<void>(observe(stalled, 25ms));
    expect(!observe(stalled, 4s).qualified,
        "resolved menu recovery leaked into a later heavy scene");

    for (const bool hadBaseline : {false, true}) {
        SurfaceBudget normal;
        watchdogNow = {}; sample = healthySample;
        if (hadBaseline) baseline(normal);
        if (!hadBaseline) static_cast<void>(menu(normal, 1s));
        sample.outputFps = 30.0;
        for (int i = 0; i < 240; ++i)
            expect(!observe(normal, 250ms).qualified,
                "ordinary low FPS or an unattainable target triggered recreation");
    }
    for (int invalidation = 0; invalidation < 11; ++invalidation) {
        SurfaceBudget invalidated;
        watchdogNow = {}; sample = healthySample; baseline(invalidated);
        static_cast<void>(menu(invalidated, 1s));
        if (invalidation == 0) ++sample.configurationRevision;
        if (invalidation == 1) ++sample.contextId;
        if (invalidation == 2) sample.extents[0] = 1920;
        if (invalidation == 3) sample.refreshHz = 90;
        if (invalidation == 4)
            static_cast<void>(invalidated.observeSustainedDeficit(watchdogNow, std::nullopt));
        if (invalidation == 5) sample.lowerPresentShare = 0.1;
        if (invalidation == 6) sample.outputFps.reset();
        if (invalidation == 7) sample.targetFps = 144;
        if (invalidation == 8) sample.focus.gameFocused.reset();
        if (invalidation == 9) watchdogNow += 76s;
        if (invalidation == 10) {
            watchdogNow += 2s;
            expect(!invalidated.observeSustainedDeficit(watchdogNow, sample).qualified,
                "stale focus authorized a rebuild");
        }
        static_cast<void>(observe(invalidated, 25ms));
        expect(!observe(invalidated, 5s).qualified,
            "invalid/stale evidence authorized a post-menu rebuild");
    }
    SurfaceBudget missedPresent;
    watchdogNow = {}; sample = healthySample; baseline(missedPresent);
    // Gamescope's reader observes the menu even if the game submits no frames.
    sample.focus.openedAt = watchdogNow;
    watchdogNow += 60s;
    sample.focus.returnedAt = watchdogNow;
    sample.focus.returnSequence = 1;
    sample.outputFps = 70.0;
    static_cast<void>(observe(missedPresent, 25ms));
    expect(observe(missedPresent, 4s).qualified,
        "no game presents during the menu hid its confirmed focus return");

    const auto cadenceInterval = [](const double framesPerSecond) {
        return std::chrono::duration_cast<
            FixedCadenceCollapseRecovery::Duration
        >(std::chrono::duration<double>(1.0 / framesPerSecond));
    };
    const auto cadenceStep = [&](FixedCadenceCollapseRecovery& recovery,
            FixedCadenceCollapseRecovery::TimePoint& now,
            const double framesPerSecond,
            const uint32_t refreshHz = 120,
            const size_t maximumGeneratedFrames = 1) {
        const auto interval = cadenceInterval(framesPerSecond);
        now += interval;
        return recovery.observe(
            now, interval, refreshHz, maximumGeneratedFrames
        );
    };

    expect(fixedCadenceCollapseRecoveryEligible(
            false, true, false, false, 120, 1),
        "ordinary Fixed ordered cadence was not eligible for collapse recovery");
    expect(!fixedCadenceCollapseRecoveryEligible(
            true, true, false, false, 120, 1) &&
            !fixedCadenceCollapseRecoveryEligible(
                false, false, false, false, 120, 1) &&
            !fixedCadenceCollapseRecoveryEligible(
                false, true, true, false, 120, 1) &&
            !fixedCadenceCollapseRecoveryEligible(
                false, true, false, true, 120, 1) &&
            !fixedCadenceCollapseRecoveryEligible(
                false, true, false, false, std::nullopt, 1) &&
            !fixedCadenceCollapseRecoveryEligible(
                false, true, false, false, 120, 0),
        "Adaptive/HDR/transport-recovery/warm-up/unknown-capacity exclusion regressed");

    FixedCadenceCollapseRecovery fixedCadenceRecovery;
    auto fixedCadenceNow = FixedCadenceCollapseRecovery::TimePoint{};
    for (size_t frame = 0; frame < 120; ++frame) {
        const auto decision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 60.0
        );
        expect(!decision.suppressGeneration,
            "healthy Fixed 2x cadence unexpectedly entered recovery");
    }

    FixedCadenceCollapseRecovery::Decision fixedCollapseDecision;
    size_t collapsedFrames = 0;
    while (!fixedCollapseDecision.probeStarted && collapsedFrames < 60) {
        fixedCollapseDecision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 30.0
        );
        collapsedFrames++;
    }
    expect(fixedCollapseDecision.probeStarted &&
            fixedCollapseDecision.suppressGeneration &&
            fixedCollapseDecision.baselineBaseFps < 40.0 &&
            collapsedFrames < 30,
        "sustained 60-to-30 Fixed cadence collapse did not start a bounded native probe");

    for (size_t confirmation = 1; confirmation <= 3; ++confirmation) {
        fixedCollapseDecision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 60.0
        );
        expect(fixedCollapseDecision.suppressGeneration,
            "faster Fixed cadence probe resumed generation before confirmation");
        if (confirmation < 3) {
            expect(!fixedCollapseDecision.probeRecovered &&
                    fixedCollapseDecision.confirmedSamples == confirmation,
                "Fixed cadence probe lost an intermediate confirmation sample");
        }
    }
    expect(fixedCollapseDecision.probeRecovered &&
            fixedCollapseDecision.confirmedSamples == 3 &&
            fixedCollapseDecision.observedBaseFps > 59.0,
        "three faster native samples did not recover Fixed cadence");

    bool fixedRecoveryVerified = false;
    for (size_t frame = 0; frame < 90 && !fixedRecoveryVerified; ++frame) {
        fixedCollapseDecision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 60.0
        );
        fixedRecoveryVerified = fixedCollapseDecision.recoveryVerified;
    }
    expect(fixedRecoveryVerified,
        "recovered Fixed cadence did not survive generated-delivery verification");

    fixedCadenceRecovery.reset();
    fixedCadenceNow = FixedCadenceCollapseRecovery::TimePoint{};
    for (size_t frame = 0; frame < 120; ++frame)
        static_cast<void>(cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 60.0
        ));
    do {
        fixedCollapseDecision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 30.0
        );
    } while (!fixedCollapseDecision.probeStarted);
    for (size_t confirmation = 0; confirmation < 3; ++confirmation) {
        fixedCollapseDecision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 60.0
        );
    }
    expect(fixedCollapseDecision.probeRecovered,
        "unstable-resume precondition did not recover its native probe");
    fixedCollapseDecision = cadenceStep(
        fixedCadenceRecovery, fixedCadenceNow, 30.0
    );
    expect(fixedCollapseDecision.recoveryUnstable &&
            !fixedCollapseDecision.suppressGeneration &&
            fixedCollapseDecision.consecutiveFailures == 1 &&
            fixedCollapseDecision.retryDelay == 2s,
        "FG-induced post-probe collapse did not enter oscillation backoff");

    fixedCadenceRecovery.reset();
    fixedCadenceNow = FixedCadenceCollapseRecovery::TimePoint{};
    bool unprovenSlowCadenceProbed = false;
    for (size_t frame = 0; frame < 300; ++frame) {
        const auto decision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 30.0
        );
        unprovenSlowCadenceProbed = unprovenSlowCadenceProbed ||
            decision.probeStarted;
    }
    expect(!unprovenSlowCadenceProbed,
        "a genuinely slow Fixed source was probed without a healthy baseline");

    fixedCadenceRecovery.reset();
    fixedCadenceNow = FixedCadenceCollapseRecovery::TimePoint{};
    for (size_t frame = 0; frame < 120; ++frame)
        static_cast<void>(cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 60.0
        ));
    do {
        fixedCollapseDecision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 30.0
        );
    } while (!fixedCollapseDecision.probeStarted);
    fixedCollapseDecision = cadenceStep(
        fixedCadenceRecovery, fixedCadenceNow, 30.0
    );
    expect(fixedCollapseDecision.probeRejected &&
            !fixedCollapseDecision.suppressGeneration &&
            fixedCollapseDecision.consecutiveFailures == 1 &&
            fixedCollapseDecision.retryDelay == 2s,
        "a true 30 FPS slowdown did not reject immediately into bounded backoff");
    bool retriedInsideBackoff = false;
    const auto firstRetryDeadline = fixedCadenceNow + 2s;
    while (fixedCadenceNow < firstRetryDeadline) {
        const auto decision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 30.0
        );
        retriedInsideBackoff = retriedInsideBackoff || decision.probeStarted;
    }
    expect(!retriedInsideBackoff,
        "true Fixed slowdown retried its native probe inside backoff");

    // A rejected native probe disproves the old fast-menu baseline for this
    // workload. Jitter around that rate must not cause another hitch every
    // 30 seconds, across all Fixed multipliers and representative refreshes.
    for (const uint32_t refresh : {60U, 90U, 120U, 144U}) {
        for (size_t outputs = 2; outputs <= 5; ++outputs) {
            FixedCadenceCollapseRecovery steadyRecovery;
            auto now = FixedCadenceCollapseRecovery::TimePoint{};
            const double healthy = static_cast<double>(refresh) / outputs;
            const double slow = healthy * 0.70;
            for (size_t frame = 0; frame < refresh * 2; ++frame)
                static_cast<void>(cadenceStep(
                    steadyRecovery, now, healthy, refresh, outputs - 1));
            size_t started = 0;
            size_t rejected = 0;
            size_t slowFrame = 0;
            const auto steadyUntil = now + 95s;
            while (now < steadyUntil) {
                const double jitter = slowFrame++ % 2 ? 0.98 : 1.02;
                const auto decision = cadenceStep(
                    steadyRecovery, now, slow * jitter, refresh, outputs - 1);
                started += decision.probeStarted;
                rejected += decision.probeRejected;
            }
            expect(started == 1 && rejected == 1,
                "unchanged slow Fixed workload kept repeating native probes");

            bool renewedCollapseProbed = false;
            const auto slowerUntil = now + 3s;
            while (now < slowerUntil) {
                const auto decision = cadenceStep(
                    steadyRecovery, now, slow * 0.70, refresh, outputs - 1);
                renewedCollapseProbed |= decision.probeStarted;
            }
            expect(renewedCollapseProbed,
                "a materially new Fixed cadence collapse could not rearm recovery");

            for (size_t frame = 0; frame < refresh * 2; ++frame)
                static_cast<void>(cadenceStep(
                    steadyRecovery, now, healthy, refresh, outputs - 1));
            bool requalifiedCollapseProbed = false;
            const auto collapsedUntil = now + 3s;
            while (now < collapsedUntil) {
                const auto decision = cadenceStep(
                    steadyRecovery, now, slow, refresh, outputs - 1);
                requalifiedCollapseProbed |= decision.probeStarted;
            }
            expect(requalifiedCollapseProbed,
                "healthy Fixed recovery did not requalify later collapse detection");
        }
    }

    fixedCadenceRecovery.reset();
    fixedCadenceNow = FixedCadenceCollapseRecovery::TimePoint{};
    for (size_t frame = 0; frame < 180; ++frame) {
        const auto decision = cadenceStep(
            fixedCadenceRecovery, fixedCadenceNow, 55.0
        );
        expect(!decision.probeStarted,
            "a moderate 110 FPS Fixed output fluctuation was classified as a severe collapse");
    }
    expect(!fixedCadenceRecovery.observe(
            fixedCadenceNow + 16ms, 16ms, std::nullopt, 1
        ).suppressGeneration &&
            !fixedCadenceRecovery.observe(
                fixedCadenceNow + 32ms, 16ms, 120, 0
            ).suppressGeneration,
        "Fixed cadence recovery activated without ordered refresh/capacity eligibility");

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
