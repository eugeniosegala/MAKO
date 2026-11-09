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
#include <vector>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {

    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }

    void testSmoothCadenceCapFollowsActivePlan() {
        const SmoothCadenceBaseCap::TimePoint start{};
        for (const uint32_t target : {60U, 90U, 120U, 144U}) {
            for (size_t validated = 3; validated <= 5; ++validated) {
                for (size_t active = 2; active <= validated; ++active) {
                    SmoothCadenceBaseCap cap;
                    const double activeCap = static_cast<double>(target) / active;
                    SmoothCadenceBaseCap::SchedulerState snapshot{
                        .validatedGenerationLimit = validated - 1,
                        .stableCadenceLimit = active - 1,
                        .smoothedBaseFps = activeCap * 0.97,
                    };
                    static_cast<void>(cap.update(start, true, target, snapshot));
                    auto result = cap.update(start + 1s, true, target, snapshot);
                    if (active == 2) {
                        // FF7 accepted 3x capacity but retained a 2x plan.
                        // Its ordinary half-target cap must remain authoritative.
                        expect(!result.framesPerSecond && !result.changed,
                            "validated higher capacity throttled an active 2x plan");
                    } else {
                        expect(result.framesPerSecond && result.changed &&
                                result.multiplier == active &&
                                std::abs(*result.framesPerSecond - activeCap) < 0.001,
                            "integer cap did not match the active constant plan");
                    }
                }
            }
        }

        SmoothCadenceBaseCap cap;
        SmoothCadenceBaseCap::SchedulerState snapshot{
            .validatedGenerationLimit = 4,
            .smoothedBaseFps = 24.0,
        };
        static_cast<void>(cap.update(start, true, 120, snapshot));
        expect(cap.update(start + 1s, true, 120, snapshot).framesPerSecond == 24.0,
            "validated 5x workload could not qualify without a constant plan");
        snapshot.stableCadenceLimit = 2;
        snapshot.smoothedBaseFps = 39.0;
        auto result = cap.update(start + 2s, true, 120, snapshot);
        expect(!result.framesPerSecond && result.changed,
            "newly accepted 3x plan retained the old 5x cap during qualification");
        result = cap.update(start + 3s, true, 120, snapshot);
        expect(result.framesPerSecond == 40.0 && result.multiplier == 3,
            "active 3x plan could not qualify after releasing a stricter cap");
        snapshot.stableCadenceLimit = 1;
        result = cap.update(start + 3010ms, true, 120, snapshot);
        expect(!result.framesPerSecond && result.changed,
            "newly accepted 2x plan did not immediately release the 3x cap");

        // A lower-load measurement must still override the retained plan.
        snapshot.stableCadenceLimit = 2;
        snapshot.efficiencyProbeGenerationLimit = 1;
        result = cap.update(start + 4s, true, 120, snapshot);
        expect(result.framesPerSecond == 60.0 && result.multiplier == 2,
            "active cadence prevented a lower-load efficiency probe");
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

void testOrderedPresentationTimeline() {
    using Clock = OrderedPresentTimeline::Clock;
    const auto start = Clock::time_point{10s};
    for (const double refresh : {30., 40., 60., 90., 120., 144., 240.}) {
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / refresh));
        // A completion-limited application must not inherit a generated
        // batch's two-refresh lead while live Frame Generation is disabled.
        OrderedPresentTimeline native;
        auto completed = start;
        for (size_t frame = 0; frame < 120; ++frame) {
            const auto slot = native.schedule(completed, refresh, refresh, 1, false);
            expect(slot && slot->submitAt == completed && slot->presentAt == completed,
                "disabled generation delayed a completion-limited application");
            completed += period;
        }
        // Explicit policy changes must retain queued deadlines. An active
        // Fractional one-output turn still needs its original readiness lead.
        OrderedPresentTimeline transition;
        const auto generated = transition.schedule(start, refresh, refresh, 3);
        const auto disabled = transition.schedule(start, refresh, refresh, 1, false);
        expect(disabled->presentAt == generated->presentAt + period &&
                disabled->presentAt - disabled->submitAt == period,
            "live generation-off overtook queued output or retained batch lead");
        const auto resumed = transition.schedule(disabled->presentAt, refresh, refresh, 1, true);
        expect(resumed->presentAt == disabled->presentAt + 2 * period,
            "active one-output turn lost its generated readiness lead");
        const auto stalled = transition.schedule(start + 2s, refresh, refresh, 1, false);
        expect(stalled->presentAt == start + 2s && stalled->submitAt == start + 2s,
            "disabled generation retained queue debt after a stall");
        for (size_t multiplier = 2; multiplier <= 5; ++multiplier) {
            // Returning from an application presentation wait does not prove
            // the next frame's asynchronous GPU work is ready. Preserve the
            // readiness lead even when an earlier deadline is still future.
            OrderedPresentTimeline completionLimited;
            auto ready = start;
            auto previousOutput = start;
            for (size_t batch = 0; batch < 60; ++batch) {
                for (size_t output = 0; output < multiplier; ++output) {
                    const auto slot = completionLimited.schedule(
                        ready, refresh, refresh, multiplier);
                    const auto lead = period * static_cast<int64_t>(multiplier);
                    expect(slot && slot->presentAt - ready >= lead &&
                            slot->presentAt > previousOutput,
                        "completion wait consumed asynchronous GPU readiness lead");
                    previousOutput = slot->presentAt;
                    ready = slot->submitAt;
                }
                ready = previousOutput + period / 4;
            }
            OrderedPresentTimeline timeline;
            auto now = start;
            auto last = start;
            for (size_t batch = 0; batch < 60; ++batch) {
                // Outputs arrive as a burst from each generated batch.
                for (size_t output = 0; output < multiplier; ++output) {
                    const auto slot = timeline.schedule(now, refresh, refresh, multiplier);
                    expect(slot && slot->presentAt > last,
                        "bridge burst overtook an earlier output");
                    now = slot->submitAt;
                    const auto lead = period * static_cast<int64_t>(multiplier);
                    expect(slot->presentAt - now >= lead &&
                            slot->presentAt - now <= lead + period,
                        "bridge queue escaped its refresh-relative bounds");
                    if (batch > 0)
                        expect(slot->presentAt - last == period,
                            "bridge burst lost uniform output spacing");
                    last = slot->presentAt;
                }
            }
            const auto late = timeline.schedule(now + 2s, refresh, refresh);
            expect(late->submitAt == now + 2s &&
                    late->presentAt == now + 2s + 2 * period,
                "loading stall left catch-up debt in the bridge");
        }
    }
    OrderedPresentTimeline timeline;
    const auto slow = timeline.schedule(start, 30, 120);
    const auto slowNext = timeline.schedule(start, 30, 120);
    expect(slowNext->presentAt - slow->presentAt >= 33ms &&
            slowNext->presentAt - slowNext->submitAt <= 42ms,
        "below-refresh target escaped its batch-relative queue bound");
    const auto changed = timeline.schedule(slowNext->submitAt, 240, 240);
    expect(changed->presentAt > slowNext->presentAt &&
            changed->presentAt - changed->submitAt <= 12500us,
        "live refresh/target change overtook queued output or lost its bound");
    expect(!timeline.schedule(start, 120, 120, 0) &&
            !timeline.schedule(start, 120, 120, GeneratedFramePlan::capacity + 2),
        "bridge accepted an unbounded generated batch");
    OrderedPresentTimeline fractional;
    auto last = start;
    for (size_t frame = 0; frame < 40; ++frame) {
        auto arrival = start + 50ms * static_cast<int64_t>(frame);
        const size_t count = frame % 2 == 0 ? 5 : 4;
        for (size_t output = 0; output < count; ++output) {
            const auto slot = fractional.schedule(arrival, 90, 120, count);
            expect(slot->presentAt > last && slot->presentAt - slot->submitAt < 64ms,
                "alternating fractional batches lost ordering or grew the queue");
            arrival = slot->submitAt;
            last = slot->presentAt;
        }
    }
    OrderedPresentTimeline growing;
    const auto small = growing.schedule(start, 120, 120, 2);
    const auto larger = growing.schedule(start + 10ms, 120, 120, 5);
    expect(larger->presentAt > small->presentAt &&
            larger->presentAt >= start + 10ms + 41666us,
        "increased generated workload reused a shorter readiness lead");
    for (const double invalid : {0., -1., 1001.,
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
        expect(!timeline.schedule(start, invalid, 120) &&
                !timeline.schedule(start, 120, invalid),
            "bridge accepted an invalid timing clock");
    }
}

void testBridgeCompositorReadiness() {
    using Timeline = OrderedPresentTimeline;
    using Clock = Timeline::Clock;
    const auto start = Clock::time_point{10s};
    const auto tick = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / 120.0));
    const auto prime = [&](Timeline& timeline, uint32_t id, auto arrival, auto cost) {
        timeline.observeSourceBatch(arrival, id);
        const auto slot = timeline.schedule(arrival, 120, 120, 2);
        timeline.submitted(id, slot->presentAt, slot->submitAt);
        timeline.feedback(id, slot->presentAt, slot->submitAt + cost);
        return *slot;
    };

    // Different CPU arrival gaps with the same readiness evidence must not
    // change GPU allowance. Includes presentation waits, CPU hitches and load.
    for (const auto gap : {16ms, 26ms, 50ms, 80ms}) {
        Timeline timeline;
        prime(timeline, 1, start, tick);
        const auto arrival = start + 200ms;
        timeline.observeSourceBatch(arrival, 2);
        auto slot = timeline.schedule(arrival, 120, 120, 2);
        expect(slot->presentAt == arrival + tick,
            "CPU source interval changed compositor readiness");
        timeline.submitted(2, slot->presentAt, slot->submitAt);
        timeline.feedback(2, slot->presentAt, slot->submitAt + tick);
        timeline.observeSourceBatch(arrival + gap, 3);
        slot = timeline.schedule(arrival + gap, 120, 120, 2);
        expect(slot->presentAt == arrival + gap + tick,
            "presentation waiting fed back into source readiness");
    }

    // Closed loop: each application waits for its real image before supplying
    // another batch. Source timing includes our lead. It must converge instead
    // of multiplying that delay, with every output retaining a distinct slot.
    for (const double refresh : {30., 60., 90., 120., 240.}) {
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / refresh));
        for (size_t count = 2; count <= 5; ++count) {
            Timeline timeline;
            auto arrival = start;
            auto completedReal = start;
            uint32_t id = 0;
            for (uint64_t batch = 0; batch < 80; ++batch) {
                timeline.observeSourceBatch(arrival, batch);
                const auto ready = arrival + period / 2;
                auto last = arrival;
                std::optional<Timeline::Slot> firstOutput;
                uint32_t firstId{};
                std::optional<Timeline::Slot> lastOutput;
                for (size_t output = 0; output < count; ++output) {
                    const auto slot = timeline.schedule(arrival, refresh, refresh, count);
                    timeline.submitted(++id, slot->presentAt, slot->submitAt);
                    if (output == 0) {
                        firstOutput = slot;
                        firstId = id;
                    }
                    expect(slot->presentAt > last,
                        "completion-paced outputs lost ordering");
                    // The real wait releases at compositor refresh, not at
                    // an arbitrary requested timestamp between refreshes.
                    const auto ticks = (slot->presentAt - start + period - 1ns) / period;
                    const auto completed = start + period * ticks;
                    if (batch > 0)
                        expect(completed == completedReal + period *
                                static_cast<int64_t>(output + 1),
                            "serialized source missed a refresh through its own lead");
                    last = completed;
                    lastOutput = slot;
                }
                // Deliver readiness after the real output completes and the
                // application's wait releases, before it begins the next batch.
                timeline.feedback(firstId, firstOutput->presentAt,
                    arrival + period, period - (ready - arrival));
                timeline.feedback(id, lastOutput->presentAt,
                    last, period / 2);
                // Real-image waits can return at the latch before display.
                completedReal = last;
                arrival = last - period / 4;
            }
        }
    }

    // Warm-up native output does not measure generated-batch readiness. Prime
    // when generation first starts, but never re-prime every Fractional turn.
    Timeline warmup;
    warmup.observeSourceBatch(start, 1);
    const auto native = warmup.schedule(start, 120, 120, 1);
    warmup.submitted(1, native->presentAt, start);
    warmup.feedback(1, native->presentAt, start + 1ms);
    warmup.observeSourceBatch(start + 100ms, 2);
    const auto generated = warmup.schedule(start + 100ms, 120, 120, 2);
    expect(generated->presentAt == start + 100ms + 4 * tick,
        "generation startup reused native-only readiness");
    warmup.submitted(2, generated->presentAt, start + 100ms);
    warmup.feedback(2, generated->presentAt, start + 105ms);
    for (uint64_t batch = 3; batch < 12; ++batch) {
        const auto now = start + 100ms * batch;
        warmup.observeSourceBatch(now, batch);
        const auto slot = warmup.schedule(now, 120, 120, batch % 2 ? 1 : 2);
        expect(slot->presentAt == now + tick,
            "Fractional batch alternation repeatedly primed readiness");
        warmup.submitted(static_cast<uint32_t>(batch), slot->presentAt, now);
        warmup.feedback(static_cast<uint32_t>(batch), slot->presentAt, now + 5ms);
    }

    // Asynchronous queued output has extra spacing ahead of its readiness
    // lead. That owned spacing must be subtracted from the readiness sample.
    Timeline queued;
    queued.observeSourceBatch(start, 1);
    const auto first = queued.schedule(start, 120, 120, 2);
    queued.submitted(1, first->presentAt, start);
    const auto real = queued.schedule(start, 120, 120, 2);
    queued.submitted(2, real->presentAt, start);
    queued.observeSourceBatch(start, 2);
    const auto behind = queued.schedule(start, 120, 120, 2);
    queued.submitted(3, behind->presentAt, behind->submitAt);
    queued.feedback(3, behind->presentAt, behind->presentAt - tick);
    queued.observeSourceBatch(start + 200ms, 3);
    const auto afterQueue = queued.schedule(start + 200ms, 120, 120, 2);
    expect(afterQueue->presentAt == start + 200ms + 3 * tick,
        "queued output spacing was counted as GPU work");

    // A separate presentation waiter releases a source-frame credit while
    // the real output of the next batch is still pending. That tail is not
    // a full batch of asynchronous work and must not impose the queue reserve.
    for (size_t count = 2; count <= 5; ++count) {
        Timeline occupancy;
        occupancy.observeSourceBatch(start, 1);
        auto previous = start;
        for (size_t output = 0; output < count; ++output) {
            const auto slot = occupancy.schedule(start, 120, 120, count);
            occupancy.submitted(static_cast<uint32_t>(output + 1), slot->presentAt, start);
            previous = slot->presentAt;
            if (output + 1 < count)
                occupancy.feedback(static_cast<uint32_t>(output + 1), previous, start + tick / 2);
        }
        const auto arrival = previous - 1ms;
        occupancy.observeSourceBatch(arrival, 2);
        const auto tail = occupancy.schedule(arrival, 120, 120, count);
        expect(tail->presentAt == previous + tick,
            "partial queued batch added a hold or lost its next ordered slot");
        occupancy.submitted(static_cast<uint32_t>(count + 1), tail->presentAt, arrival);
        // Keep the next batch pending without acknowledging the old real image.
        for (size_t output = 1; output < count; ++output) {
            const auto slot = occupancy.schedule(arrival, 120, 120, count);
            occupancy.submitted(static_cast<uint32_t>(count + output + 1), slot->presentAt, arrival);
        }
        const auto nextArrival = previous + tick;
        occupancy.observeSourceBatch(nextArrival, 3);
        const auto queuedSlot = occupancy.schedule(nextArrival, 120, 120, count);
        expect(queuedSlot->presentAt >= nextArrival + 3 * tick,
            "fully queued asynchronous work lost its bounded reserve");
        occupancy.submitted(static_cast<uint32_t>(2 * count + 1), queuedSlot->presentAt, nextArrival);
        occupancy.feedback(static_cast<uint32_t>(2 * count + 1), queuedSlot->presentAt, nextArrival + tick / 2);
        occupancy.observeSourceBatch(start + 200ms, 4);
        expect(occupancy.schedule(start + 200ms, 120, 120, count)->presentAt == start + 200ms + tick,
            "completion-paced source retained the asynchronous queue reserve");
    }

    // The next batch can be entirely pending when a separate waiter releases
    // a credit. At depth two, adding a reserve here limits throughput even
    // though every frame's CPU/GPU work fits inside the source budget.
    for (size_t count = 2; count <= 5; ++count) {
        Timeline pipeline;
        pipeline.observeSourceBatch(start, 0);
        const auto learned = pipeline.schedule(start, 120, 120, count);
        pipeline.submitted(UINT32_MAX, learned->presentAt, start);
        pipeline.feedback(UINT32_MAX, learned->presentAt, start + tick / 2);
        const auto arrival = start + 200ms;
        pipeline.observeSourceBatch(arrival, 1);
        auto last = arrival;
        for (size_t output = 0; output < count; ++output) {
            const auto slot = pipeline.schedule(arrival, 120, 120, count);
            pipeline.submitted(static_cast<uint32_t>(output), slot->presentAt, arrival);
            last = slot->presentAt;
        }
        pipeline.observeSourceBatch(last - 1ms, 2);
        expect(pipeline.schedule(last - 1ms, 120, 120, count)->presentAt == last + tick,
            "one pending batch added a hold or lost its next ordered slot");
    }

    // FIFO eligibility includes older outputs. Remove their owned slots from
    // the learned work allowance, but do not subtract spacing already removed
    // by the desired-deadline correction a second time.
    for (const auto arrivalGap : {1ms, 9ms}) {
        Timeline fifo;
        prime(fifo, 1, start, tick / 2);
        const auto producer = start + 200ms;
        fifo.observeSourceBatch(producer, 2);
        const auto priorFirst = fifo.schedule(producer, 120, 120, 2);
        fifo.submitted(2, priorFirst->presentAt, producer);
        const auto priorReal = fifo.schedule(producer, 120, 120, 2);
        fifo.submitted(3, priorReal->presentAt, producer);
        const auto arrival = producer + arrivalGap;
        fifo.observeSourceBatch(arrival, 3);
        const auto first = fifo.schedule(arrival, 120, 120, 2);
        fifo.submitted(4, first->presentAt, arrival);
        const auto reserved = std::max(Clock::duration::zero(),
            first->presentAt - arrival - tick);
        const auto ownedHold = std::max(reserved, 2 * tick);
        fifo.feedback(4, first->presentAt, arrival + ownedHold + tick / 2);
        fifo.observeSourceBatch(start + 400ms, 4);
        expect(fifo.schedule(start + 400ms, 120, 120, 2)->presentAt == start + 400ms + tick,
            "FIFO occupancy was learned as GPU work or deducted twice");
    }

    // Rising GPU cost extends readiness immediately. Four recent batches
    // retain that protection; one late output cannot lower the first deadline.
    Timeline rising;
    prime(rising, 1, start, tick);
    prime(rising, 2, start + 200ms, 26ms);
    rising.observeSourceBatch(start + 300ms, 3);
    const auto expensive = rising.schedule(start + 300ms, 120, 120, 2);
    expect(expensive->presentAt == start + 326ms,
        "GPU readiness feedback did not extend the deadline");
    rising.submitted(3, expensive->presentAt, expensive->submitAt);
    const auto expensiveReal = rising.schedule(start + 300ms, 120, 120, 2);
    rising.submitted(4, expensiveReal->presentAt, expensiveReal->submitAt);
    rising.feedback(3, expensive->presentAt, start + 326ms);
    rising.feedback(4, expensiveReal->presentAt, start + 327ms);
    for (uint32_t id = 5; id < 9; ++id)
        prime(rising, id, start + 200ms * id, tick);
    rising.observeSourceBatch(start + 1900ms, 10);
    expect(rising.schedule(start + 1900ms, 120, 120, 2)->presentAt ==
            start + 1900ms + tick,
        "recovered GPU work retained stale readiness");

    // Compositors need not report a dropped first output. Later feedback must
    // still correct that batch's first deadline rather than hide the miss.
    Timeline dropped;
    dropped.observeSourceBatch(start, 1);
    const auto droppedFirst = dropped.schedule(start, 120, 120, 2);
    dropped.submitted(UINT32_MAX, droppedFirst->presentAt, start);
    const auto surviving = dropped.schedule(start, 120, 120, 2);
    dropped.submitted(0, surviving->presentAt, start);
    dropped.feedback(0, surviving->presentAt, start + 50ms);
    dropped.observeSourceBatch(start + 200ms, 2);
    expect(dropped.schedule(start + 200ms, 120, 120, 2)->presentAt == start + 250ms,
        "dropped first output or protocol-ID wrap lost readiness feedback");

    Timeline invalid;
    invalid.observeSourceBatch(start, 1);
    const auto initial = invalid.schedule(start, 120, 120, 2);
    invalid.submitted(1, initial->presentAt, start);
    invalid.feedback(99, initial->presentAt, start + tick);
    invalid.feedback(1, initial->presentAt + 1ns, start + tick);
    invalid.feedback(1, initial->presentAt, {});
    invalid.feedback(1, initial->presentAt, start - 1ns);
    invalid.feedback(1, initial->presentAt, start + 1s);
    invalid.feedback(1, initial->presentAt, start + tick, -1ns);
    invalid.feedback(1, initial->presentAt, start + tick, (start + tick).time_since_epoch());
    invalid.observeSourceBatch(start + 200ms, 2);
    expect(invalid.schedule(start + 200ms, 120, 120, 2)->presentAt ==
            start + 200ms + 4 * tick,
        "unmatched or invalid compositor feedback affected readiness");
    // No feedback is bounded even after the fixed pending ring wraps.
    for (uint64_t batch = 3; batch < 1000; ++batch) {
        const auto now = start + 200ms * batch;
        invalid.observeSourceBatch(now, batch);
        const auto slot = invalid.schedule(now, 120, 120, 2);
        invalid.submitted(static_cast<uint32_t>(batch), slot->presentAt, now);
        expect(slot->presentAt == now + 4 * tick,
            "missing feedback inflated bridge lead");
    }
    invalid.feedback(1, initial->presentAt, start + tick);

    Timeline transitions;
    prime(transitions, 1, start, 50ms);
    transitions.observeSourceBatch(start + 100ms, 2);
    const auto queuedBeforeOff = transitions.schedule(start + 100ms, 120, 120, 2);
    const auto off = transitions.schedule(start + 101ms, 120, 120, 1, false);
    expect(off->presentAt > queuedBeforeOff->presentAt &&
            off->presentAt - off->submitAt == tick,
        "FG off discarded queued order or retained generated readiness");
    transitions.observeSourceBatch(start + 300ms, 3);
    expect(transitions.schedule(start + 300ms, 120, 120, 2)->presentAt ==
            start + 300ms + 4 * tick,
        "FG re-enable reused stale feedback");
    prime(transitions, 4, start + 500ms, tick);
    transitions.observeSourceBatch(start + 3s, 5);
    expect(transitions.schedule(start + 3s, 120, 120, 2)->presentAt ==
            start + 3s + 2 * tick,
        "long pause lost its bounded recovery reserve");

    // A pause can drain the old queue without its feedback being consumed.
    // Retire those IDs and prime recovery once. Native history warm-up must
    // not release the allowance before generation resumes, nor add a second
    // startup hold. Fresh samples age the recovery allowance out naturally.
    for (const auto cost : {tick, 3 * tick}) {
        Timeline paused;
        prime(paused, 1, start, cost);
        const auto beforePause = start + 100ms;
        paused.observeSourceBatch(beforePause, 2);
        auto old = paused.schedule(beforePause, 120, 120, 2);
        paused.submitted(2, old->presentAt, beforePause);
        for (uint32_t id = 3; id <= 6; ++id) {
            const auto slot = paused.schedule(beforePause, 120, 120, 2);
            paused.submitted(id, slot->presentAt, slot->submitAt);
        }
        // More native-only frames than the readiness window, including a
        // second pause, must not consume the generated recovery allowance.
        for (uint32_t batch = 3; batch <= 16; ++batch) {
            const auto now = start + 2s + 100ms * (batch - 3) +
                (batch >= 9 ? 2s : 0s);
            paused.observeSourceBatch(now, batch);
            // Late feedback from before the pause must not contaminate the
            // retained estimate or the new queue's completion accounting.
            paused.feedback(2, old->presentAt, beforePause + 90ms);
            const auto slot = paused.schedule(now, 120, 120, batch >= 11 ? 2 : 1);
            const auto expected = batch < 7 ? cost : batch < 11 ? tick :
                batch < 15 ? std::max(cost, 2 * tick) : tick;
            expect(slot->presentAt == now + expected,
                "pause warm-up released recovery lead early or re-primed generation");
            paused.submitted(batch + 4, slot->presentAt, now);
            paused.feedback(batch + 4, slot->presentAt, now + tick / 2);
        }
        const auto backwards = start + 1s;
        paused.observeSourceBatch(backwards, 17);
        // Deadlines remain ordered, but the old cost must be invalidated.
        const auto fresh = start + 8s;
        paused.observeSourceBatch(fresh, 18);
        expect(paused.schedule(fresh, 120, 120, 2)->presentAt == fresh + 4 * tick,
            "clock reset retained an invalid readiness estimate");
    }
}

void testBridgeCompletionReturnDoesNotDrift() {
    using Timeline = OrderedPresentTimeline;
    const auto start = Timeline::TimePoint{10s};
    for (const double refresh : {60., 90., 120., 144., 240.}) {
        const auto tick = std::chrono::duration_cast<Timeline::Clock::duration>(
            std::chrono::duration<double>(1.0 / refresh));
        for (size_t count = 1; count <= 5; ++count) {
            for (const auto returnDelay : {100us, 300us, 1000us}) {
                Timeline timeline;
                auto arrival = start;
                auto previous = start;
                uint32_t id = 0;
                for (uint64_t batch = 1; batch <= 120; ++batch) {
                    timeline.observeSourceBatch(arrival, batch);
                    auto submit = arrival;
                    auto first = Timeline::Slot{};
                    uint32_t firstId = 0;
                    for (size_t output = 0; output < count; ++output) {
                        const auto slot = timeline.schedule(submit, refresh, refresh, count);
                        expect(slot.has_value(), "completion-paced bridge rejected a valid batch");
                        submit = slot->submitAt;
                        timeline.submitted(++id, slot->presentAt, submit);
                        if (output == 0) {
                            first = *slot;
                            firstId = id;
                        }
                        if (batch > 1)
                            expect(slot->presentAt == previous + tick,
                                "completion-return overhead drifted a ready ordered deadline");
                        expect(slot->presentAt >= submit + tick / 2,
                            "retained deadline consumed measured GPU work allowance");
                        previous = slot->presentAt;
                    }
                    // The next CPU turn follows completion slightly late, but
                    // the observed work still fits before the next output.
                    timeline.feedback(firstId, first.presentAt, first.submitAt + tick / 2);
                    timeline.feedback(id, previous, submit + tick / 2);
                    arrival = previous + returnDelay;
                }
                // A real source hitch must rebase, never replay old slots.
                const auto late = previous + 100ms;
                timeline.observeSourceBatch(late, 121);
                const auto resumed = timeline.schedule(late, refresh, refresh, count);
                expect(resumed->presentAt == late + tick,
                    "completion-return tolerance retained debt across a source hitch");
            }
        }
    }
}

void testLongSessionDeadlineBounds() {
    const auto start = RealFramePacer::TimePoint{std::chrono::hours(2400)};
    for (const double fps : {45., 60., 90., 120.}) {
        RealFramePacer pacer;
        OrderedPresentTimeline timeline;
        const auto period = std::chrono::duration_cast<RealFramePacer::Clock::duration>(
            std::chrono::duration<double>(1.0 / fps));
        static_cast<void>(pacer.schedule(start, fps));
        auto bridgeArrival = start;
        const size_t frames = static_cast<size_t>(fps * 7200);
        for (size_t frame = 1; frame <= frames; ++frame) {
            const auto expected = start + period * static_cast<int64_t>(frame);
            const auto arrival = expected - 1ms;
            expect(pacer.schedule(arrival, fps) == expected,
                "two-hour source cap accumulated extra deadline debt");
            const auto slot = timeline.schedule(bridgeArrival, fps, fps, 2);
            expect(slot && slot->presentAt - slot->submitAt <= 3 * period,
                "two-hour ordered timeline accumulated a growing queue");
            bridgeArrival = slot->submitAt;
        }
        const auto elapsedNs = std::chrono::duration<long double, std::nano>(
            period * static_cast<int64_t>(frames)).count();
        const auto idealNs = static_cast<long double>(frames) * 1'000'000'000.L / fps;
        expect(std::abs(elapsedNs - idealNs) < 1'000'000.L,
            "two-hour nanosecond rounding became a millisecond-scale pacing error");
        const auto late = start + 2h + 100ms;
        expect(pacer.schedule(late, fps) == late,
            "late source frame after two hours retained old timing debt");
        const auto slot = timeline.schedule(bridgeArrival + 100ms, fps, fps, 2);
        expect(slot && slot->submitAt == bridgeArrival + 100ms,
            "late ordered frame after two hours retained old queue debt");
    }
}

void testFixedUsesExistingSourceDeadline() {
    using Clock = RealFramePacer::Clock;
    const auto start = Clock::time_point{10s};
    for (const double outputFps : {60., 90., 120.}) {
        for (const size_t count : {2U, 3U, 4U, 5U}) {
            const double sourceFps = outputFps / count;
            const auto sourceInterval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1. / sourceFps));
            const auto outputInterval = std::chrono::duration_cast<Clock::duration>(
                std::chrono::duration<double>(1. / outputFps));
            RealFramePacer pacer;
            static_cast<void>(pacer.schedule(start, sourceFps));
            auto previousReal = start;
            std::optional<Clock::time_point> previousOutput;
            for (size_t frame = 0; frame < 120; ++frame) {
                const auto real = pacer.schedule(previousReal + 1ms, sourceFps);
                expect(real == previousReal + sourceInterval,
                    "Fixed output pacing changed the existing source cap");
                const auto batch = pacer.scheduleOutputs(previousReal + 2ms,
                    real, outputFps, count, false);
                expect(batch && batch->count == count &&
                        batch->first > previousReal && batch->at(count - 1) <= real,
                    "Fixed output batch escaped its reserved source interval");
                for (size_t output = 0; output < count; ++output) {
                    const auto deadline = batch->at(output);
                    if (previousOutput) {
                        const auto error = deadline - *previousOutput - outputInterval;
                        expect(error >= -100ns && error <= 100ns,
                            "Fixed generated and real frames lost uniform output spacing");
                    }
                    previousOutput = deadline;
                }
                previousReal = real;
            }
            const auto late = previousReal + 2s;
            const auto real = pacer.schedule(late, sourceFps);
            expect(real == late && !pacer.scheduleOutputs(late, real, outputFps, count, false),
                "Fixed loading stall accumulated catch-up output debt");
            const auto next = pacer.schedule(late + 1ms, sourceFps);
            expect(!pacer.scheduleOutputs(next - 1ms, next, outputFps, count, false),
                "slow Fixed preparation compressed an expired output batch");
            expect(!pacer.scheduleOutputs(late + 1ms, next, outputFps, 1, false),
                "native Fixed recovery acquired a generated-output deadline");
        }
    }
}

void testSourceIntervalSpacing() {
    using Spacing = SourceIntervalOutputSpacing;
    using Clock = Spacing::Clock;
    using Transition = Spacing::Transition;
    const auto start = Clock::time_point{100s};
    static constexpr auto prepare = 2ms;

    // An application model: `period` is its own cadence and `work` the time
    // it needs after its present returns. A throughput-bound game has no own
    // cadence; a timer-driven one waits for its next slot when late.
    struct Source {
        Clock::duration period{};
        Clock::duration work{};
        bool throughputBound{false};
        bool timerSlots{false};
    };
    struct Run {
        Spacing spacing;
        Clock::time_point arrival;
        Clock::time_point due;
        size_t count{2};
        std::vector<Spacing::Observation> transitions;
        size_t frames{};
        size_t held{};
        size_t unspaced{};

        void until(const Clock::time_point end, const Source& source) {
            while (this->arrival < end) {
                const auto observed = this->spacing.observe(this->arrival, true);
                if (observed.transition != Transition::None)
                    this->transitions.push_back(observed);
                auto returned = this->arrival + prepare;
                const bool engaged = this->spacing.engaged();
                if (const auto batch = this->spacing.schedule(returned, this->count)) {
                    ++this->held;
                    returned = batch->at(this->count - 1);
                } else if (engaged) {
                    ++this->unspaced;
                }
                ++this->frames;
                const auto ready = returned + source.work;
                if (source.throughputBound) {
                    this->arrival = this->due = ready;
                    continue;
                }
                this->due += source.period;
                if (source.timerSlots) {
                    while (this->due < ready)
                        this->due += source.period;
                } else {
                    this->due = std::max(this->due, ready);
                }
                this->arrival = this->due;
            }
        }
    };

    // A heavier scene can make every interval exceed twice the old baseline.
    // This is sustained work, not an endless sequence of isolated hitches.
    {
        Run run{.arrival = start, .due = start};
        run.until(start + 3s, {.period = 25ms, .work = 5ms});
        expect(run.spacing.engaged(), "the light scene did not engage spacing");
        const auto changed = run.arrival;
        const auto heldBefore = run.held;
        run.until(changed + 1s, {.work = 55ms, .throughputBound = true});
        expect(run.transitions.size() == 2 &&
                run.transitions.back().transition == Transition::Slowed &&
                run.transitions.back().retryAfter == 5s && !run.spacing.engaged(),
            "sustained intervals above twice the baseline kept spacing engaged");
        expect(run.held - heldBefore <= Spacing::windowSize,
            "a heavier scene was held for more than one observation window");
        const auto unspacedAt = run.arrival;
        const auto framesBefore = run.frames;
        run.until(unspacedAt + 1s, {.work = 55ms, .throughputBound = true});
        expect((run.arrival - unspacedAt) /
                static_cast<int64_t>(run.frames - framesBefore) == prepare + 55ms,
            "a released heavy scene retained a spacing delay");
    }

    // Probe at the 20 FPS boundary, but keep slower sources uninterrupted.
    for (const auto period : {50ms, 51ms, 200ms}) {
        Run run{.arrival = start, .due = start};
        run.until(start + 60s, {.period = period, .work = 1ms});
        expect(run.transitions.size() == 1 &&
                run.transitions[0].transition == Transition::Engaged &&
                run.transitions[0].sourceInterval == period,
            "a steady source released its spacing at the probe-rate boundary");
        const size_t expectedProbes = period == 50ms ? 5 : 0;
        expect(run.unspaced == expectedProbes &&
                run.held == run.frames - Spacing::windowSize - expectedProbes,
            "recovery probes disturbed a source below 20 FPS or skipped the boundary");
    }

    // A 5 FPS video at 5x: after one stable window every output, the real
    // frame included, is 40 ms after the previous one across batches.
    {
        Spacing spacing;
        std::optional<Clock::time_point> previous;
        int64_t engagedAt = -1;
        for (int64_t frame = 0; frame < 200; ++frame) {
            const auto arrival = start + frame * 200ms;
            const auto observed = spacing.observe(arrival, true);
            if (observed.transition == Transition::Engaged) {
                expect(engagedAt < 0 && observed.sourceInterval == 200ms,
                    "video spacing engaged twice or at the wrong interval");
                engagedAt = frame;
            } else {
                expect(observed.transition == Transition::None,
                    "a steady video released its spacing");
            }
            const auto batch = spacing.schedule(arrival + prepare, 5);
            expect(batch.has_value() == (engagedAt >= 0),
                "video-rate spacing left a batch unspaced");
            if (!batch)
                continue;
            expect(batch->at(4) < arrival + 200ms,
                "the real frame borrowed the next source interval");
            for (size_t output = 0; output < batch->count; ++output) {
                if (previous)
                    expect(batch->at(output) - *previous == 40ms,
                        "video outputs lost uniform 40 ms spacing");
                previous = batch->at(output);
            }
        }
        expect(engagedAt == static_cast<int64_t>(Spacing::windowSize),
            "spacing did not engage after one stable window");
    }

    // A game held at 40 FPS by its own limiter keeps its cadence while
    // spaced; only the game-rate probe leaves one frame unspaced per 10 s.
    {
        Run run{.arrival = start, .due = start, .count = 3};
        run.until(start + 60s, {.period = 25ms, .work = 5ms});
        expect(run.transitions.size() == 1 &&
                run.transitions[0].transition == Transition::Engaged &&
                run.transitions[0].sourceInterval == 25ms,
            "a frame-limited game did not keep its spacing");
        expect(run.unspaced == 5 && run.held == run.frames - Spacing::windowSize - 5,
            "game-rate spacing probed more often than every 10 s");
    }

    // A game running flat out loses frame rate to any wait. The guard
    // releases it within one window, backs off 5/15/30/60 s, then leaves an
    // unchanged cadence alone; a changed cadence earns one more attempt.
    {
        Run run{.arrival = start, .due = start};
        run.until(start + 600s, {.work = 23ms, .throughputBound = true});
        std::vector<Clock::duration> retries;
        for (const auto& transition : run.transitions)
            if (transition.transition == Transition::Slowed)
                retries.push_back(transition.retryAfter);
        expect(retries == std::vector<Clock::duration>{5s, 15s, 30s, 60s},
            "throughput-bound retries did not back off and stop");
        expect(run.held <= retries.size() * Spacing::windowSize,
            "the slowdown guard held a throughput-bound game for more than a window");
        expect((run.arrival - start) / static_cast<int64_t>(run.frames) < 25ms + 50us,
            "spacing cost a throughput-bound game its frame rate");

        const auto changed = run.arrival;
        run.until(changed + 120s, {.work = 40ms, .throughputBound = true});
        size_t slowed = 0;
        for (const auto& transition : run.transitions)
            slowed += transition.transition == Transition::Slowed;
        expect(slowed == 5 && run.transitions.back().retryAfter == 60s,
            "a changed throughput-bound cadence was not retried exactly once");
    }

    // An emulator only just keeping its 62.5 FPS cadence loses 3% to the
    // wait (16.5 ms instead of 16 ms); the guard releases it.
    {
        Run run{.arrival = start, .due = start};
        run.until(start + 2s, {.period = 16ms, .work = 6500us});
        expect(run.transitions.size() == 2 &&
                run.transitions[0].sourceInterval == 16ms &&
                run.transitions[1].transition == Transition::Slowed,
            "a 3% emulator slowdown kept its spacing");
    }

    // A source that speeds up presents as soon as the wait returns; the
    // median sees the shorter interval and spacing requalifies at once.
    {
        Run run{.arrival = start, .due = start};
        run.until(start + 3s, {.period = 32ms, .work = 1ms});
        run.until(start + 4s, {.period = 16ms, .work = 1ms});
        expect(run.transitions.size() == 3 &&
                run.transitions[1].transition == Transition::Faster &&
                run.transitions[2].transition == Transition::Engaged &&
                run.transitions[2].sourceInterval == 16ms,
            "a faster source stayed throttled by the old spacing");
    }

    // A timer-driven emulator that switches 30 -> 60 FPS waits for its next
    // slot, so the old wait hides the new cadence until the next probe.
    {
        Run run{.arrival = start, .due = start};
        run.until(start + 3s, {.period = 32ms, .work = 1ms, .timerSlots = true});
        const auto switched = run.arrival;
        run.until(switched + 12s, {.period = 16ms, .work = 1ms, .timerSlots = true});
        expect(run.transitions.size() == 3 &&
                run.transitions[1].transition == Transition::Faster &&
                run.transitions[2].sourceInterval == 16ms,
            "the game-rate probe missed a hidden 30 -> 60 FPS switch");
    }

    // A source that qualifies too fast to space (120 FPS at 2x is under the
    // 240 Hz floor) and then settles at 60 FPS replaces its stale baseline
    // without a penalty and is spaced at the settled cadence.
    {
        Run run{.arrival = start, .due = start};
        run.until(start + 1s, {.period = 8ms, .work = 1ms});
        expect(run.spacing.engaged() && run.held == 0,
            "an unspaceable cadence was spaced");
        run.until(start + 2s, {.period = 16ms, .work = 1ms});
        expect(run.transitions.size() == 3 &&
                run.transitions[1].transition == Transition::Changed &&
                run.transitions[2].transition == Transition::Engaged &&
                run.transitions[2].sourceInterval == 16ms && run.held > 0,
            "a stale unspaced baseline blocked spacing at the settled cadence");
    }

    // Playback that qualifies on a slightly slow start-up cadence settles to
    // its real cadence within one window instead of leaving a short last gap.
    {
        Spacing spacing;
        auto arrival = start;
        for (size_t frame = 0; frame <= Spacing::windowSize; ++frame, arrival += 215ms)
            static_cast<void>(spacing.observe(arrival, true));
        arrival -= 215ms;
        for (size_t frame = 0; frame < 2 * Spacing::windowSize; ++frame) {
            expect(spacing.schedule(arrival + prepare, 5).has_value(),
                "settling video spacing skipped a batch");
            arrival += 200ms;
            expect(spacing.observe(arrival, true).transition == Transition::None,
                "a slightly faster settled cadence released spacing");
        }
        const auto batch = spacing.schedule(arrival + prepare, 5);
        expect(batch && batch->interval == 40ms,
            "spacing kept a slow start-up baseline after settling");
    }

    // One hitch, even above twice the baseline, keeps spacing; a loading
    // stall releases it without a penalty.
    for (const auto [period, hitch] : std::array{
            std::pair{25ms, 75ms}, std::pair{200ms, 240ms}}) {
        Spacing spacing;
        auto arrival = start;
        for (size_t frame = 0; frame <= Spacing::windowSize; ++frame, arrival += period)
            static_cast<void>(spacing.observe(arrival, true));
        arrival -= period;
        expect(spacing.engaged(), "steady video did not engage");
        for (size_t frame = 0; frame < 20; ++frame) {
            expect(spacing.schedule(arrival + prepare, 5).has_value(),
                "engaged video spacing skipped a batch");
            arrival += frame == 10 ? hitch : period;
            expect(spacing.observe(arrival, true).transition == Transition::None,
                "one hitch released video spacing");
        }
        arrival += 300ms;
        expect(spacing.observe(arrival, true).transition == Transition::Stalled,
            "a loading stall kept stale spacing");
        for (size_t frame = 0; frame < Spacing::windowSize; ++frame) {
            arrival += period;
            static_cast<void>(spacing.observe(arrival, true));
        }
        expect(spacing.engaged(), "a stall imposed a retry penalty");
    }

    // An irregular source is never spaced.
    {
        Spacing spacing;
        auto arrival = start;
        for (size_t frame = 0; frame < 200; ++frame) {
            arrival += frame % 2 ? 22ms : 28ms;
            expect(spacing.observe(arrival, true).transition == Transition::None &&
                    !spacing.schedule(arrival + prepare, 2),
                "an irregular source was spaced");
        }
    }

    // Ineligible frames release spacing but keep the retry penalty; a slowed
    // attempt is forgiven only after 10 s of clean spacing.
    {
        // Engaged after 8 x 25 ms, then slowed after 8 x 37.5 ms: 500 ms.
        Run run{.arrival = start, .due = start};
        run.until(start + 510ms, {.work = 23ms, .throughputBound = true});
        const auto slowedAt = start + 500ms;
        expect(run.transitions.size() == 2 &&
                run.transitions.back().transition == Transition::Slowed &&
                run.transitions.back().retryAfter == 5s,
            "throughput-bound attempt was not released");
        for (size_t frame = 0; frame < 4; ++frame, run.arrival += 25ms)
            static_cast<void>(run.spacing.observe(run.arrival, false));
        run.due = run.arrival;
        run.until(slowedAt + 4s, {.period = 25ms, .work = 5ms});
        expect(!run.spacing.engaged(), "an ineligible interval cleared the retry delay");
        run.until(slowedAt + 20s, {.period = 25ms, .work = 5ms});
        expect(run.spacing.engaged(), "a source-paced game was not spaced after its retry");
        run.until(slowedAt + 21s, {.work = 23ms, .throughputBound = true});
        expect(run.transitions.back().transition == Transition::Slowed &&
                run.transitions.back().retryAfter == 5s,
            "clean spacing did not forgive an earlier slowdown");
        run.spacing.reset();
        expect(!run.spacing.engaged(), "reset kept spacing engaged");
    }

    // Batches that cannot be spaced keep ordinary delivery.
    {
        Spacing spacing;
        auto arrival = start;
        for (size_t frame = 0; frame <= Spacing::windowSize; ++frame, arrival += 10ms)
            static_cast<void>(spacing.observe(arrival, true));
        arrival -= 10ms;
        expect(spacing.engaged() && spacing.schedule(arrival + prepare, 2) &&
                !spacing.schedule(arrival + prepare, 3),
            "outputs closer than a 240 Hz refresh were spaced");
        expect(!spacing.schedule(arrival + prepare, 1) &&
                !spacing.schedule(arrival + prepare, GeneratedFramePlan::capacity + 2),
            "a native frame or oversized batch was spaced");
        expect(!spacing.schedule(arrival + 6ms, 2),
            "late preparation pushed the real frame past the next source frame");
    }
}

void testFractionalUsesExistingSourceDeadline() {
    using Clock = RealFramePacer::Clock;
    const auto start = Clock::time_point{10s};
    for (const auto [sourceFps, targetFps] : std::array{
            std::pair{60., 90.}, std::pair{40., 60.}, std::pair{24., 60.},
            std::pair{20., 70.}, std::pair{20., 90.}, std::pair{72., 120.}}) {
        const auto sourceInterval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / sourceFps));
        const auto outputInterval = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / targetFps));
        RealFramePacer pacer;
        static_cast<void>(pacer.schedule(start, sourceFps));
        auto previousReal = start;
        std::optional<Clock::time_point> previousOutput;
        double credit = 0.5;
        for (size_t frame = 0; frame < 60; ++frame) {
            // The cap reserves the same return deadline regardless of whether
            // the Fractional ledger grants a shorter or longer output batch.
            const auto real = pacer.schedule(previousReal + sourceInterval / 8, sourceFps);
            expect(real == previousReal + sourceInterval,
                "Fractional output placement changed the source-cap cadence");
            credit += targetFps / sourceFps;
            const auto count = static_cast<size_t>(std::floor(credit + 1e-9));
            credit -= static_cast<double>(count);
            const auto batch = pacer.scheduleOutputs(previousReal + 1ms, real, targetFps, count, true);
            expect(batch && batch->count == count &&
                    batch->first > previousReal && batch->at(count - 1) <= real,
                "Fractional outputs escaped the reserved source interval");
            for (size_t output = 0; output < count; ++output) {
                const auto deadline = batch->at(output);
                if (previousOutput) {
                    expect(deadline > *previousOutput,
                        "Fractional output overtook an earlier image");
                    if (frame > 2) {
                        const auto error = deadline - *previousOutput - outputInterval;
                        expect(error >= -100ns && error <= 100ns,
                            "alternating Fractional batches lost the target-output clock");
                    }
                }
                previousOutput = deadline;
            }
            previousReal = real;
        }
        const auto lateArrival = previousReal + 2s;
        const auto real = pacer.schedule(lateArrival, sourceFps);
        const auto resumed = pacer.scheduleOutputs(lateArrival, real, targetFps, 2, true);
        expect(real == lateArrival && !resumed,
            "loading stall retained catch-up debt or extended the source cap");
        pacer.resetOutputs();
        const auto changed = pacer.scheduleOutputs(real, real + sourceInterval, targetFps, 1, true);
        expect(changed && changed->first == real + sourceInterval,
            "leaving Fractional retained an earlier output clock");
    }
    RealFramePacer pacer;
    expect(!pacer.scheduleOutputs(start, start, 90., 2, true),
        "output pacing invented an unconfigured source cap");
    static_cast<void>(pacer.schedule(start, 60.));
    expect(!pacer.scheduleOutputs(start, start + 17ms, 90., 5, true) &&
            !pacer.scheduleOutputs(start, start + 17ms, 90., 0, true),
        "output pacing borrowed extra source time for an unplaceable batch");
    for (size_t frame = 1; frame <= 6; ++frame) {
        const auto deadline = start + 17ms * frame;
        expect(!pacer.scheduleOutputs(deadline - 16ms, deadline, 90., 1, false),
            "native-only startup/probe frames must retain the source-cap deadline");
    }
    const auto resumed = pacer.scheduleOutputs(start + 134ms, start + 150ms, 90., 1, true);
    expect(resumed && resumed->first == start + 150ms,
        "native-only pacing retained stale Fractional output debt");
    expect(pacer.scheduleOutputs(start + 151ms, start + 167ms, 120., 2, false).has_value(),
        "an accepted constant generated cadence lost capped output pacing");
    // A cap never guarantees the game reaches it. Both late arrivals and
    // preparation that consumes the cap's slack must keep ordinary delivery.
    for (const auto [cap, actual] : std::array{
            std::pair{30., 27.}, std::pair{45., 38.}, std::pair{45., 37.},
            std::pair{60., 58.}, std::pair{60., 57.}}) {
        RealFramePacer late;
        static_cast<void>(late.schedule(start, cap));
        const auto arrival = start + std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1. / actual));
        const auto deadline = late.schedule(arrival, cap);
        expect(deadline == arrival &&
                !late.scheduleOutputs(arrival, deadline, 2 * cap, 2, true),
            "below-cap source acquired an additional output wait");
        const auto next = late.schedule(arrival + 1ms, cap);
        expect(late.scheduleOutputs(arrival + 1ms, next, 2 * cap, 2, true).has_value(),
            "return to the cap retained stale output debt");
        expect(!late.scheduleOutputs(next + 1ms, next, 2 * cap, 2, true),
            "slow preparation compressed an expired output batch");
    }
    for (const double invalid : {0., -1., 1001.,
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
        expect(!pacer.scheduleOutputs(start, start + 17ms, invalid, 2, true),
            "Fractional output pacing accepted an invalid target");
    }
}

void testBridgeSourceDeadlinePreservesDisplayClock() {
    using Clock = OrderedPresentTimeline::Clock;
    const auto start = Clock::time_point{10s};
    OrderedPresentTimeline ordinary;
    OrderedPresentTimeline capped;
    ordinary.observeSourceBatch(start, 1);
    capped.observeSourceBatch(start, 1);
    (void)ordinary.schedule(start, 60, 120, 2);
    (void)capped.schedule(start, 60, 120, 2);
    // A below-refresh batch can outlast the normal submission lookahead.
    // Its producer's existing deadline permits earlier staging, never an
    // earlier display target or a second output clock.
    auto expected = ordinary.schedule(start, 60, 120, 2);
    auto actual = capped.schedule(start, 60, 120, 2, true, start + 5ms);
    expect(expected->submitAt > start + 5ms && actual->submitAt == start + 5ms &&
            actual->presentAt == expected->presentAt,
        "bridge source budget changed display timing or consumed the next source interval");
    expected = ordinary.schedule(start, 60, 120, 2);
    actual = capped.schedule(start, 60, 120, 2, true, start - 1ms);
    expect(actual->submitAt == start && actual->presentAt == expected->presentAt,
        "expired source budget moved submission into the past or compressed display targets");
    expected = ordinary.schedule(start, 60, 120, 2);
    actual = capped.schedule(start, 60, 120, 2);
    expect(actual->submitAt == expected->submitAt && actual->presentAt == expected->presentAt,
        "bridge retained a source budget after its producer released it");
}

void testBridgeStagedReadiness() {
    using Clock = OrderedPresentTimeline::Clock;
    const auto start = Clock::time_point{10s};
    for (const double refresh : {30., 60., 90., 120., 144., 240.}) {
        const auto period = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / refresh));
        for (size_t batchSize = 2; batchSize <= 5; ++batchSize) {
            // Different compositor holds must learn identical queue readiness.
            for (const auto hold : {0ms, 20ms, 60ms}) {
                OrderedPresentTimeline timeline;
                timeline.observeSourceBatch(start, 1);
                const auto slot = timeline.schedule(start, refresh, refresh, batchSize);
                constexpr uint64_t serial = uint64_t{UINT32_MAX} + 5;
                timeline.submittedTiming(serial, slot->presentAt, start);
                timeline.feedbackTiming(serial, start + 2ms, start + 2ms + hold);
                timeline.feedbackTiming(serial, start + 90ms, start + 90ms); // Duplicate.
                const auto next = start + 500ms;
                timeline.observeSourceBatch(next, 2);
                const auto learned = timeline.schedule(next, refresh, refresh, batchSize);
                expect(learned->presentAt == next + period,
                    "V2 readiness included compositor waiting or duplicate evidence");
                // Idle recovery and native history retain the existing guard.
                timeline.observeSourceBatch(next + 2s, 3);
                const auto recovered = timeline.schedule(next + 2s, refresh, refresh, batchSize);
                expect(recovered->presentAt == next + 2s + 2 * period,
                    "V2 feedback lost bounded recovery readiness");
            }
        }
    }
    for (const int invalid : {0, 1, 2, 3, 4}) {
        OrderedPresentTimeline timeline;
        timeline.observeSourceBatch(start, 1);
        const auto slot = timeline.schedule(start, 120, 120, 2);
        constexpr uint64_t serial = uint64_t{UINT32_MAX} + 8;
        timeline.submittedTiming(serial, slot->presentAt, start);
        timeline.feedbackTiming(invalid == 4 ? static_cast<uint32_t>(serial) : serial,
            invalid == 0 ? Clock::time_point{} : invalid == 1 ? start - 1ns :
            invalid == 2 ? start + 101ms : start + 2ms,
            invalid == 3 ? start + 1ms : start + 200ms);
        timeline.observeSourceBatch(start + 500ms, 2);
        const auto next = timeline.schedule(start + 500ms, 120, 120, 2);
        expect(next->presentAt - (start + 500ms) > 30ms,
            "drop, malformed clock or truncated serial fabricated V2 readiness");
    }
    OrderedPresentTimeline outOfOrder;
    {
        OrderedPresentTimeline droppedFirst;
        droppedFirst.observeSourceBatch(start, 1);
        auto slot = droppedFirst.schedule(start, 120, 120, 2);
        droppedFirst.submittedTiming(1, slot->presentAt, start);
        slot = droppedFirst.schedule(start + 20ms, 120, 120, 2);
        droppedFirst.submittedTiming(2, slot->presentAt, start + 20ms);
        droppedFirst.feedbackTiming(1, {}, start + 40ms);
        droppedFirst.feedbackTiming(2, start + 22ms, start + 40ms);
        droppedFirst.observeSourceBatch(start + 500ms, 2);
        slot = droppedFirst.schedule(start + 500ms, 120, 120, 2);
        expect(slot->presentAt - (start + 500ms) < 9ms,
            "later V2 feedback learned CPU spacing after an earlier dropped output");
    }
    uint64_t serial = 0;
    for (uint64_t batch = 1; batch <= 3; ++batch) {
        const auto now = start + batch * 20ms;
        outOfOrder.observeSourceBatch(now, batch);
        for (size_t output = 0; output < 2; ++output) {
            const auto slot = outOfOrder.schedule(now, 120, 120, 2);
            outOfOrder.submittedTiming(++serial, slot->presentAt, now);
        }
    }
    outOfOrder.feedbackTiming(1, start + 22ms, start + 100ms);
    outOfOrder.feedbackTiming(6, {}, start + 100ms); // Drop newer output first.
    outOfOrder.feedbackTiming(6, {}, start + 100ms); // Duplicate must not retire twice.
    outOfOrder.observeSourceBatch(start + 500ms, 4);
    auto slot = outOfOrder.schedule(start + 500ms, 120, 120, 2);
    expect(slot->presentAt - (start + 500ms) > 24ms,
        "out-of-order V2 drop retired unacknowledged older outputs");
    for (uint64_t id = 2; id <= 5; ++id)
        outOfOrder.feedbackTiming(id, {}, start + 501ms);
    outOfOrder.observeSourceBatch(start + 700ms, 5);
    slot = outOfOrder.schedule(start + 700ms, 120, 120, 2);
    expect(slot->presentAt - (start + 700ms) < 9ms,
        "explicit V2 drops retained nonexistent queue occupancy");
    OrderedPresentTimeline missing;
    serial = 0;
    for (uint64_t batch = 1; batch <= 33; ++batch) {
        const auto now = start + batch * 20ms;
        missing.observeSourceBatch(now, batch);
        for (size_t output = 0; output < 2; ++output) {
            slot = missing.schedule(now, 120, 120, 2);
            missing.submittedTiming(++serial, slot->presentAt, now);
        }
    }
    missing.feedbackTiming(66, start + 662ms, start + 700ms);
    for (uint64_t id = 35; id < 66; ++id)
        missing.feedbackTiming(id, {}, start + 700ms);
    missing.feedbackTiming(1, start + 750ms, start + 750ms); // Expired ring entry.
    missing.observeSourceBatch(start + 900ms, 34);
    slot = missing.schedule(start + 900ms, 120, 120, 2);
    expect(slot->presentAt - (start + 900ms) < 9ms,
        "V2 ring expiry retained phantom occupancy or accepted stale feedback");
}

void testBridgeQueuedRefreshRealignment() {
    using Timeline = OrderedPresentTimeline;
    using Clock = Timeline::Clock;
    const auto tick = std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1.0 / 120.0));
    struct Output {
        uint32_t id;
        Timeline::Slot slot;
    };
    // A 60 FPS source at 2x: one real and one generated output per batch.
    struct Bridge {
        Timeline timeline;
        uint32_t id{};
        uint64_t batch{};
        Clock::time_point arrival{10s};
        Clock::time_point now{10s};
        std::array<Output, 2> produce(Clock::duration tickDuration) {
            arrival = std::max(arrival + 2 * tickDuration, now);
            now = arrival;
            timeline.observeSourceBatch(arrival, ++batch);
            std::array<Output, 2> outputs{};
            for (auto& output : outputs) {
                const auto slot = timeline.schedule(now, 120, 120, 2);
                timeline.submitted(++id, slot->presentAt, slot->submitAt);
                output = {id, *slot};
                now = std::max(now, slot->submitAt);
            }
            return outputs;
        }
    };
    const auto display = [](Bridge& bridge, const Output& output, Clock::duration lateness) {
        bridge.timeline.feedback(output.id, output.slot.presentAt, {}, {},
            output.slot.presentAt + lateness);
    };

    // A missed refresh leaves every later output shown one whole refresh late.
    {
        Bridge bridge;
        std::array<Output, 2> last{};
        for (size_t batch = 0; batch < 6; ++batch) {
            last = bridge.produce(tick);
            for (const auto& output : last)
                display(bridge, output, tick + 1ms);
        }
        const auto realigned = bridge.produce(tick);
        expect(realigned[0].slot.realignment == tick &&
                realigned[0].slot.presentAt >= last[1].slot.presentAt + 2 * tick,
            "queued refresh was not drained from the next output target");
        expect(realigned[1].slot.realignment == Clock::duration::zero() &&
                realigned[1].slot.presentAt - realigned[0].slot.presentAt == tick,
            "realignment changed spacing after its single drained slot");
        // Feedback for outputs requested before the realignment still shows
        // the old queue and must not remove a second refresh.
        for (size_t batch = 0; batch < 6; ++batch) {
            const auto outputs = bridge.produce(tick);
            for (const auto& output : last)
                display(bridge, output, tick + 1ms);
            static_cast<void>(outputs);
        }
        expect(bridge.produce(tick)[0].slot.realignment == Clock::duration::zero(),
            "pre-realignment feedback removed the same queued refresh twice");
    }

    // Timing phase alone stays below one refresh and never moves deadlines.
    {
        Bridge bridge;
        bool moved = false;
        for (size_t batch = 0; batch < 20; ++batch) {
            for (const auto& output : bridge.produce(tick)) {
                moved |= output.slot.realignment > Clock::duration::zero();
                display(bridge, output, tick - 50us);
            }
        }
        expect(!moved, "timing phase within one refresh was treated as a queue");
    }

    // One output shown within a refresh disproves the queue and restarts evidence.
    {
        Bridge bridge;
        bool moved = false;
        size_t index = 0;
        for (size_t batch = 0; batch < 10; ++batch) {
            for (const auto& output : bridge.produce(tick)) {
                moved |= output.slot.realignment > Clock::duration::zero();
                display(bridge, output, ++index % 10 == 0 ? 1ms : tick + 1ms);
            }
        }
        expect(!moved, "a transient late output was treated as a persistent queue");
    }

    // Two queued refreshes drain together.
    {
        Bridge bridge;
        std::array<Output, 2> last{};
        for (size_t batch = 0; batch < 6; ++batch) {
            last = bridge.produce(tick);
            for (const auto& output : last)
                display(bridge, output, 2 * tick + 1ms);
        }
        expect(bridge.produce(tick)[0].slot.realignment == 2 * tick,
            "two queued refreshes were not drained together");
    }

    // A source pause rebases deadlines and discards pending queue evidence.
    {
        Bridge bridge;
        for (size_t batch = 0; batch < 6; ++batch)
            for (const auto& output : bridge.produce(tick))
                display(bridge, output, tick + 1ms);
        bridge.arrival += 2s;
        bridge.now = bridge.arrival;
        expect(bridge.produce(tick)[0].slot.realignment == Clock::duration::zero(),
            "a pause applied queue evidence from before the rebase");
    }

    const auto countRealignments = [&](Bridge& bridge, size_t batches,
            Clock::duration lateness) {
        size_t realignments = 0;
        for (size_t batch = 0; batch < batches; ++batch) {
            for (const auto& output : bridge.produce(tick)) {
                realignments += output.slot.realignment > Clock::duration::zero();
                display(bridge, output, lateness);
            }
        }
        return realignments;
    };

    // Compositor latency of a whole refresh is not a queue: the realigned
    // output stays late, so repeating the delay would hold a refresh every
    // evidence window. One test realignment is allowed, then none.
    {
        Bridge bridge;
        expect(countRealignments(bridge, 30, tick + 1ms) == 1,
            "constant compositor latency repeatedly held a refresh as a queue");
        // A pause or timing reset re-arms realignment for a new queue.
        bridge.arrival += 2s;
        bridge.now = bridge.arrival;
        expect(countRealignments(bridge, 7, tick + 1ms) == 1,
            "a timing reset did not re-arm realignment after a disproved one");
    }

    // A realignment that drained its queue keeps later queues eligible.
    {
        Bridge bridge;
        expect(countRealignments(bridge, 6, tick + 1ms) == 0,
            "queue evidence realigned before it was complete");
        expect(countRealignments(bridge, 4, 1ms) == 1,
            "queued refresh was not drained");
        expect(countRealignments(bridge, 6, tick + 1ms) == 0,
            "a drained queue was realigned again");
        expect(countRealignments(bridge, 1, 1ms) == 1,
            "a proven realignment disabled draining a later queue");
    }
}

int main() {
    {
        mako::layer::RealFramePacer pacer;
        using Clock = mako::layer::RealFramePacer::Clock;
        const auto origin = Clock::time_point{} + std::chrono::hours(100000);
        expect(pacer.schedule(origin, 40) == origin, "initial cap does not wait");
        const auto arrival = origin + std::chrono::milliseconds(2);
        const auto changed = pacer.schedule(arrival, 38, true);
        const auto interval = std::chrono::duration<double>(changed - origin).count();
        expect(interval > .026 && interval < .027,
            "small governor rate change preserves the outstanding source boundary");
        const auto restored = pacer.schedule(changed + std::chrono::milliseconds(2), 40, true);
        const auto restoredInterval = std::chrono::duration<double>(restored - changed).count();
        expect(restoredInterval > .024 && restoredInterval < .026,
            "governor relaxation preserves source cadence without a free frame");
        expect(pacer.schedule(restored, 0, true) == restored,
            "disabling a governed cap releases the source immediately");
    }

    testLongSessionDeadlineBounds();
    testFractionalUsesExistingSourceDeadline();
    testFixedUsesExistingSourceDeadline();
    testSourceIntervalSpacing();
    testSmoothCadenceCapFollowsActivePlan();
    testOrderedPresentationTimeline();
    testBridgeCompositorReadiness();
    testBridgeCompletionReturnDoesNotDrift();
    testBridgeStagedReadiness();
    testBridgeQueuedRefreshRealignment();
    testBridgeSourceDeadlinePreservesDisplayClock();
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
                true, true, 2, 0, true) &&
            adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 2, 1, true) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 2, 2, true) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                true, true, 0, 0, true) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                false, true, 2, 0, true) &&
            !adaptiveOrderedDeliveryMissRequiresFallback(
                true, false, 2, 0, true),
        "Adaptive ordered multiplier probes lost immediate fallback policy");
    for (size_t requested = 1; requested <= 4; ++requested) {
        for (size_t admitted = 0; admitted < requested; ++admitted) {
            expect(!adaptiveOrderedDeliveryMissRequiresFallback(
                    true, true, requested, admitted, false),
                "zero-wait admission miss demoted an already validated load");
        }
    }
    for (uint32_t applicationImages = 2; applicationImages <= 4; ++applicationImages) {
        for (size_t generated = 1; generated <= 4; ++generated) {
            const size_t images = applicationImages + generated;
            expect(orderedGeneratedBatchCanPrepareBeforeCap(
                    applicationImages, images, generated, false) &&
                    !orderedGeneratedBatchCanPrepareBeforeCap(
                        applicationImages, images - 1, generated, false) &&
                    !orderedGeneratedBatchCanPrepareBeforeCap(
                        applicationImages, images, generated, true),
                "source-cap preparation moved undersized or pressured admission before image release");
            expect(!orderedGeneratedBatchCanPrepareBeforeCap(
                    applicationImages, images, generated + 1, false),
                "source-cap preparation ignored a possible higher generated batch");
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
    expect(!orderedGeneratedBatchCanPrepareBeforeCap(3, 7, 0, false) &&
            !orderedGeneratedBatchCanPrepareBeforeCap(3, 2, 1, false),
        "source-cap preparation accepted inactive generation or an invalid pool");
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
    expect(!cadenceBaseCap.activeGenerationLimit(),
        "pending integer cap was reported as applied pacing");
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
    expect(cadenceBaseCap.activeGenerationLimit() == 2,
        "qualified integer cap did not expose its applied generation rung");
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
    expect(!cadenceBaseCap.activeGenerationLimit(),
        "released integer cap retained stale applied pacing authority");
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
    handoff = pacerHandoff.update(pacingStart + 11s, std::nullopt, true);
    expect(!handoff.active && handoff.changed &&
            handoff.previousGenerationLimit == 2,
        "planned VRR efficiency probe retained its previous FIFO handoff");
    handoff = pacerHandoff.update(pacingStart + 12s, 2);
    expect(handoff.active && handoff.changed,
        "rejected VRR efficiency probe invented a 3x retry cooldown");
    handoff = pacerHandoff.update(pacingStart + 13s, std::nullopt, true);
    handoff = pacerHandoff.update(pacingStart + 14s, 1);
    expect(handoff.active && handoff.generationLimit == 1,
        "accepted efficiency probe did not hand off the lower 2x rung");
    handoff = pacerHandoff.update(pacingStart + 15s, std::nullopt);
    expect(!handoff.active && handoff.changed,
        "genuine 2x qualification loss did not restore normal pacing");
    handoff = pacerHandoff.update(pacingStart + 16s, 1);
    expect(!handoff.active,
        "planned 3x probe cleared the genuine 2x retry cooldown");
    handoff = pacerHandoff.update(pacingStart + 16s, 2);
    expect(handoff.active && handoff.generationLimit == 2,
        "accepted 2x probe delayed a separately revalidated 3x rung");
    pacerHandoff.reset();
    static_cast<void>(pacerHandoff.update(pacingStart + 17s, 2));
    handoff = pacerHandoff.update(pacingStart + 18s, 1, true);
    expect(handoff.active && handoff.generationLimit == 1,
        "planned-pause flag prevented an eligible rung transition");
    handoff = pacerHandoff.update(pacingStart + 19s, 2);
    expect(!handoff.active,
        "planned-pause flag bypassed cooldown for an actual rung switch");

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

    // Equal early/late timestamp noise must not slowly drain a full Fixed
    // multiplier. Check the settled tail so startup rounding is not mistaken
    // for a recurring skip, across refresh rates and every supported rung.
    for (const uint32_t refreshHz : {60U, 90U, 120U, 144U}) {
        for (size_t multiplier = 2; multiplier <= 5; ++multiplier) {
            for (const double jitterSeconds : {0.0001, 0.0005, 0.001}) {
                budget.reset();
                (void)budget.plan(start, refreshHz, multiplier - 1);
                size_t tailSkipped = 0;
                for (size_t frame = 1; frame <= 2400; ++frame) {
                    const double seconds =
                        static_cast<double>(frame * multiplier) / refreshHz +
                        (frame % 2 ? jitterSeconds : 0.0);
                    const auto when = start + std::chrono::duration_cast<
                        std::chrono::steady_clock::duration>(
                            std::chrono::duration<double>(seconds));
                    const size_t count = budget.plan(when, refreshHz, multiplier - 1);
                    if (frame > 1200)
                        tailSkipped += multiplier - 1 - count;
                }
                expect(tailSkipped == 0,
                    "balanced timestamp jitter drained the Fixed refresh budget");
            }
        }
    }

    // A slow period may leave one output of credit, not a multi-frame bank
    // capable of defeating the display ceiling after a source-rate increase.
    budget.reset();
    for (size_t frame = 0; frame <= 100; ++frame)
        (void)budget.plan(start + frame * 100ms, 120, 1);
    generated = 0;
    for (size_t frame = 1; frame <= 1000; ++frame) {
        const size_t count = budget.plan(start + 10s + frame * 10ms, 120, 1);
        if (frame > 100)
            generated += count;
    }
    expect(generated >= 179 && generated <= 181,
        "slow-source timing credit defeated the later display budget");

    std::cout << "presentation policy tests passed\n";
    return 0;
}
