/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "gpu_headroom.hpp"
#include "runtime_health.hpp"

#include <array>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <poll.h>
#include <sys/inotify.h>
#include <thread>
#include <unistd.h>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {
void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
}
using Clock = GpuHeadroomController::Clock;
Clock::time_point timeAt(double seconds) {
    return Clock::time_point(std::chrono::duration_cast<Clock::duration>(
        std::chrono::duration<double>(1000000 + seconds)));
}
}

int main() {
    GpuHeadroomController controller;
    double now = 0;
    GpuHeadroomController::Result result;
    double previousCap = 0;
    for (int frame = 0; frame < 4000; ++frame) {
        now += 1.0 / (result.capFps > 0 ? result.capFps : 45.0);
        result = controller.update(timeAt(now), 10, {timeAt(now), 100.0}, 30);
        expect(result.capFps == 0 || result.capFps >= 30, "Adaptive ceiling preserves the real-frame floor");
        if (result.capFps > 0 && previousCap > 0)
            expect(result.capFps >= previousCap * .949, "no abrupt cap reduction");
        previousCap = result.capFps;
    }
    expect(std::abs(result.capFps - 30) < .01, "sustained overload converges to the allowed floor");
    // Partial WSI admission can reduce the usable multiplier between source
    // measurement windows. Its new floor applies to the very next frame.
    now += .001;
    result = controller.update(timeAt(now), 10, {timeAt(now), 100.0}, 45);
    expect(result.capFps >= 45,
        "reduced generation capacity must raise the source floor immediately");
    now += .001;
    result = controller.update(timeAt(now), 10, {timeAt(now), 100.0}, 30);
    expect(result.capFps >= 45 * .95,
        "restored generation capacity must retain gradual source-cap reductions");
    const auto capped = result.capFps;
    for (int frame = 0; frame < 200; ++frame) {
        now += 1.0 / capped;
        result = controller.update(timeAt(now), 10, {timeAt(now), 90.0}, 30);
        expect(result.capFps == capped, "deadband does not oscillate around target");
    }
    for (int frame = 0; frame < 6000; ++frame) {
        now += 1.0 / (result.capFps > 0 ? result.capFps : 45.0);
        result = controller.update(timeAt(now), 10, {timeAt(now), 50.0}, 30);
    }
    expect(result.capFps == 0, "sustained spare capacity restores normal pacing");

    controller.reset();
    for (int frame = 0; frame < 2000; ++frame) {
        now += 1.0 / 45;
        result = controller.update(timeAt(now), 20, {timeAt(now), 100.0}, 45);
        expect(result.capFps == 0, "2x at its target cannot invent additional generation capacity");
    }
    expect(result.state == "frame-rate-floor" || result.state == "measuring",
        "capacity limitation is observable");
    controller.reset();
    for (int frame = 0; frame < 1000; ++frame) {
        now += 1.0 / (result.capFps > 0 ? result.capFps : 60.0);
        result = controller.update(timeAt(now), 20, {timeAt(now), 100.0}, 20);
    }
    expect(result.capFps > 0, "Fixed can reduce real FPS without a multiplier mutation");
    result = controller.update(timeAt(now + .01), 0, {timeAt(now), 100.0}, 20);
    expect(result.capFps == 0 && result.state == "off" && controller.sourceCeilingFps() == 0,
        "Off clears a live cap and its remembered source ceiling immediately");
    for (int frame = 0; frame < 120; ++frame) {
        now += 1.0 / 60;
        result = controller.update(timeAt(now), 20, {timeAt(now), 100.0}, 20);
        expect(result.capFps == 0, "re-enabling starts a fresh warm-up without the previous cap");
    }
    for (int frame = 0; frame < 1000; ++frame) {
        now += 1.0 / (result.capFps > 0 ? result.capFps : 60.0);
        result = controller.update(timeAt(now), 20, {timeAt(now), 100.0}, 20);
    }
    expect(result.capFps > 0, "precondition: telemetry loss occurs while limiting");
    result = controller.update(timeAt(now + .02), 20, {timeAt(now - 2), 100.0}, 20);
    expect(result.capFps == 0 && result.state == "telemetry-unavailable", "stale telemetry releases only the extra cap");
    result = controller.update(timeAt(now + .04), 0, {}, 20);
    expect(result.capFps == 0 && result.state == "off", "Off is immediately dormant");
    result = controller.update(timeAt(now + .06), 10, {timeAt(now + 5), 100.0}, 20);
    expect(result.capFps == 0, "future timestamps are not valid control evidence");

    controller.reset();
    for (int frame = 0; frame < 450; ++frame) {
        now += 1.0 / 45;
        result = controller.update(timeAt(now), 20, {timeAt(now), 50.0}, 20);
    }
    for (int frame = 0; frame < 60; ++frame) {
        now += 1.0 / 30;
        result = controller.update(timeAt(now), 20, {timeAt(now), 60.0}, 20);
    }
    for (int frame = 0; frame < 180 && result.capFps == 0; ++frame) {
        now += 1.0 / 30;
        result = controller.update(timeAt(now), 20, {timeAt(now), 100.0}, 20);
    }
    expect(result.capFps > 0 && controller.sourceCeilingFps() >= 44.9,
        "telemetry lag must not erase the healthy pre-overload recovery rate");
    controller.reset();
    for (const double fps : {90.0, 45.0}) {
        for (int frame = 0; frame < static_cast<int>(fps * 15); ++frame) {
            now += 1.0 / fps;
            result = controller.update(timeAt(now), 20, {timeAt(now), 50.0}, 20);
        }
    }
    expect(controller.sourceCeilingFps() < 46,
        "expired source peaks must not inflate a later recovery ceiling");

    controller.reset();
    result = {};
    for (int frame = 0; frame < 3450; ++frame) {
        now += 1.0 / (result.capFps > 0 ? result.capFps : 45.0);
        result = controller.update(timeAt(now), 20,
            {timeAt(now), frame < 450 ? 40.0 : 100.0}, 20);
    }
    const double recoveryBegan = now;
    expect(result.capFps == 20, "precondition: deeper overload reached the safe source floor");
    while (result.capFps > 0 && now - recoveryBegan < 12) {
        now += 1.0 / result.capFps;
        result = controller.update(timeAt(now), 20, {timeAt(now), 40.0}, 20);
    }
    expect(result.capFps == 0,
        "clearly spare capacity must restore the healthy source rate promptly after deeper overload");

    // Closed-loop synthetic load: verify convergence without matching the
    // implementation's own formulas, and sustained bounded cadence with noise.
    controller.reset();
    result = {};
    double stableMinimum = 100, stableMaximum = 0;
    const double began = now;
    for (int frame = 0; frame < 300000; ++frame) {
        const double fps = result.capFps > 0 ? result.capFps : 45;
        now += 1.0 / fps;
        const double load = std::clamp(5 + fps * 2.2 + (frame % 3 - 1) * 1.0, 0.0, 100.0);
        result = controller.update(timeAt(now), 10, {timeAt(now), load}, 30);
        expect(result.capFps == 0 || (result.capFps >= 30 && result.capFps <= 45),
            "closed-loop controller remains bounded for a two-hour simulation");
        if (now - began > 60) {
            stableMinimum = std::min(stableMinimum, load);
            stableMaximum = std::max(stableMaximum, load);
        }
    }
    expect(stableMinimum >= 83 && stableMaximum <= 94,
        "noisy closed-loop utilization remains close to its requested target");
    const auto oldCap = result.capFps;
    result = controller.update(timeAt(now + 1), 10, {timeAt(now + 1), 50}, 30);
    expect(oldCap > 0 && result.capFps == 0,
        "loading pauses release the old scene's source ceiling");
    for (const double invalid : {-1.0, 101.0, std::numeric_limits<double>::quiet_NaN()}) {
        result = controller.update(timeAt(now + 1.02), 10, {timeAt(now + 1.02), invalid}, 30);
        expect(result.capFps == 0 && result.state == "telemetry-unavailable", "invalid GPU evidence fails open");
    }

    // Witcher 3 at a 120 FPS target: Headroom cut 60x2 below 50 FPS, then
    // Adaptive's own 40 FPS 3x cap held the source below it at about 80%
    // utilization. That rate says nothing about the cap's rate, so the cap
    // must hold rather than creep up and release 60x2 at full load again.
    controller.reset();
    result = {};
    for (int frame = 0; frame < 2000 && (result.capFps == 0 || result.capFps > 50); ++frame) {
        now += 1.0 / (result.capFps > 0 ? result.capFps : 60.0);
        result = controller.update(timeAt(now), 10, {timeAt(now), 100.0}, 40);
    }
    expect(result.capFps > 40 && result.capFps <= 50, "precondition: 60x2 overload set a cap above 40 FPS");
    const double heldCap = result.capFps;
    for (int frame = 0; frame < 40 * 60; ++frame) {
        now += 1.0 / 40;
        result = controller.update(timeAt(now), 10, {timeAt(now), 80.0}, 40);
        expect(result.capFps == heldCap,
            "utilization below a cap that limits nothing must not raise or release it");
    }
    // A genuinely lighter scene still restores the healthy source rate.
    for (int frame = 0; frame < 40 * 12 && result.capFps > 0; ++frame) {
        now += 1.0 / 40;
        result = controller.update(timeAt(now), 10, {timeAt(now), 40.0}, 40);
    }
    expect(result.capFps == 0,
        "clearly spare capacity below a non-limiting cap still releases it");

    char pattern[] = "/tmp/mako-gpu-headroom.XXXXXX";
    const auto directory = mkdtemp(pattern);
    expect(directory != nullptr, "telemetry fixture");
    const std::filesystem::path root(directory);
    const auto sensor = root / "dev/char/226:129/device/gpu_busy_percent";
    std::filesystem::create_directories(sensor.parent_path());
    std::ofstream(sensor) << "100\n";
    const int accessEvents = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
    expect(accessEvents >= 0 && inotify_add_watch(accessEvents, sensor.c_str(), IN_ACCESS) >= 0,
        "sensor access observation must be available");
    const auto accessedWithin = [&](int milliseconds) {
        pollfd event{accessEvents, POLLIN, 0};
        const int ready = poll(&event, 1, milliseconds);
        expect(ready >= 0, "sensor observation failed");
        return ready > 0;
    };
    const auto discardAccessEvents = [&] {
        std::array<char, 4096> events{};
        while (read(accessEvents, events.data(), events.size()) > 0) {}
    };
    {
        present_diagnostics::GpuLoadMonitor monitor({{}, root, present_diagnostics::DrmRenderNode{226, 129}});
        monitor.setEnabled(false);
        expect(!monitor.latest().percent, "disabled monitor has no sample");
        expect(!accessedWithin(450), "default-Off monitor must not read GPU telemetry");
        monitor.setEnabled(true);
        const auto waitForValue = [&](double value) {
            const auto end = Clock::now() + 4s;
            while (Clock::now() < end) {
                const auto sample = monitor.latest();
                if (sample.percent && *sample.percent == value) return true;
                std::this_thread::sleep_for(20ms);
            }
            return false;
        };
        expect(waitForValue(100), "worker publishes only a complete smoothed window");
        expect(accessedWithin(0), "sensor observation must detect enabled reads");
        monitor.setEnabled(false);
        expect(!monitor.latest().percent, "disable invalidates cached utilization immediately");
        // An already-started driver read may finish after disable; it cannot
        // publish a stale value or schedule another read while the worker parks.
        std::this_thread::sleep_for(300ms);
        discardAccessEvents();
        expect(!accessedWithin(650) && !monitor.latest().percent,
            "live Off must stop sensor polling and keep stale samples invalid");
        std::ofstream(sensor) << "0\n";
        monitor.setEnabled(true);
        expect(waitForValue(0), "reenabling requires fresh samples; genuine zero remains valid");
        monitor.setEnabled(false);
    }
    close(accessEvents);
    std::filesystem::remove_all(root);
    std::cout << "GPU headroom policy and worker tests passed\n";
}
