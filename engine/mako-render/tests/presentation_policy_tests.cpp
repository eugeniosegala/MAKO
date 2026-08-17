/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "presentation_policy.hpp"

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
    // Regression boundary: only an HDR-capable swapchain managed by Gamescope
    // may use the compositor bridge. In particular, merely discovering
    // Gamescope must not route an ordinary SDR game away from ordered FIFO.
    expect(selectPresentationTransport(false, false) ==
            PresentationTransport::OrderedSdr,
        "ordinary SDR did not retain the ordered fork transport");
    expect(selectPresentationTransport(false, true) ==
            PresentationTransport::OrderedSdr,
        "non-Gamescope HDR unexpectedly selected the Gamescope bridge");
    expect(selectPresentationTransport(true, false) ==
            PresentationTransport::OrderedSdr,
        "Gamescope SDR was routed through the HDR transport");
    expect(selectPresentationTransport(true, true) ==
            PresentationTransport::GamescopeHdr,
        "HDR-capable Gamescope swapchain did not select the HDR bridge");

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

    expect(generatedDeliveryHealthy(0, 0),
        "an empty delivery window should be healthy");
    expect(!generatedDeliveryHealthy(1, 0),
        "complete loss in a short window was incorrectly tolerated");
    expect(!generatedDeliveryHealthy(19, 18),
        "short evaluation windows must require complete delivery");
    expect(generatedDeliveryHealthy(20, 19),
        "one isolated miss in a full window should be tolerated");
    expect(!generatedDeliveryHealthy(20, 18),
        "persistent delivery pressure was incorrectly tolerated");

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
