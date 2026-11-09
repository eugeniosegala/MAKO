/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mako::layer {

struct GpuHeadroomLoad {
    using Clock = std::chrono::steady_clock;
    Clock::time_point sampledAt{};
    std::optional<double> percent;
};

// A soft utilization governor. It changes only the existing real-frame cap;
// generated work, delivery, resource capacity and recovery retain their owners.
// Missing/stale telemetry never becomes zero utilization or an indefinite cap.
class GpuHeadroomController {
public:
    using Clock = GpuHeadroomLoad::Clock;
    struct Result {
        double capFps{};
        std::string_view state{"off"};
    };

    void reset() { *this = {}; }
    [[nodiscard]] double sourceCeilingFps() const { return ceiling; }

    Result update(Clock::time_point now, uint32_t reservePercent,
            const GpuHeadroomLoad& load, double minimumFps) {
        if (reservePercent == 0) {
            reset();
            return {};
        }
        if (!load.percent || !std::isfinite(*load.percent) ||
                *load.percent < 0 || *load.percent > 100 ||
                load.sampledAt > now || now - load.sampledAt > std::chrono::seconds(1)) {
            reset();
            return {0, "telemetry-unavailable"};
        }
        if (reservePercent != configuredReserve) {
            reset();
            configuredReserve = reservePercent;
        }
        if (!lastFrame || now <= *lastFrame || now - *lastFrame > std::chrono::milliseconds(250)) {
            // A loading pause invalidates the old scene's useful source ceiling.
            // Re-measure instead of carrying its throttle into unrelated work.
            cap = ceiling = 0;
            sourceRates = {};
            sourceRateIndex = 0;
            windowStart = now;
            frames = 0;
            nextDecision = now + std::chrono::seconds(3);
            state = cap > 0 ? "limiting" : "measuring";
        }
        lastFrame = now;
        ++frames;
        const double floor = std::max(20.0,
            std::isfinite(minimumFps) ? minimumFps : 20.0);
        // Transport admission can lower generated capacity between windows.
        // Honor the new source floor before returning a cached cap; only
        // utilization-driven reductions wait for the measurement cadence.
        if (cap > 0)
            cap = std::max(cap, floor);
        const double elapsed = std::chrono::duration<double>(now - windowStart).count();
        if (elapsed < 1.0)
            return {cap, state};
        // The first call is the beginning of the first measured interval.
        const double sourceFps = static_cast<double>(frames - 1) / elapsed;
        windowStart = now;
        frames = 1;
        if (!std::isfinite(sourceFps) || sourceFps <= 0)
            return {cap, "measuring"};
        if (cap == 0) {
            // Smoothed telemetry trails a sudden heavy scene. Preserve the
            // recent healthy source rate for recovery instead of replacing it
            // with that scene's already GPU-limited rate before the first cap.
            // Expire old peaks so a past menu cannot inflate a later ceiling.
            sourceRates[sourceRateIndex] = {now, sourceFps};
            sourceRateIndex = (sourceRateIndex + 1) % sourceRates.size();
            ceiling = sourceFps;
            for (const auto& sample : sourceRates)
                if (sample.at <= now && now - sample.at <= std::chrono::seconds(8))
                    ceiling = std::max(ceiling, sample.fps);
        }
        if (now < nextDecision || load.sampledAt <= lastDecisionSample)
            return {cap, state};
        lastDecisionSample = load.sampledAt;
        nextDecision = now + std::chrono::seconds(2);
        const double target = 100.0 - std::clamp(reservePercent, 5U, 20U);
        if (*load.percent > target + 2.0) {
            const double current = cap > 0 ? cap : sourceFps;
            if (current <= floor * 1.01) {
                state = "frame-rate-floor";
                return {cap, state};
            }
            // At most five percent per decision, even at complete saturation.
            // A small sustained overshoot needs a smaller adjustment.
            const double ratio = std::clamp(target / *load.percent, 0.95, 0.99);
            cap = std::max(floor, current * ratio);
            state = "limiting";
            return {cap, state};
        }
        // Another owner, such as Adaptive's integer-rung cap, can hold the
        // source below this cap. Utilization at that lower rate is not spare
        // capacity at the cap: raising a cap that limits nothing until it
        // releases would return the full load at once. Project it first.
        const double capUtilization = cap > 0 && sourceFps < cap * 0.95
            ? *load.percent * cap / sourceFps : *load.percent;
        if (cap > 0 && capUtilization < target - 5.0) {
            // Recover clearly spare capacity promptly after a scene change;
            // approach the utilization band cautiously to avoid oscillation.
            cap *= capUtilization < target * 0.75 ? 1.20 : 1.02;
            if (cap >= ceiling * 0.995)
                cap = 0;
        }
        state = cap > 0 ? "limiting" : "within-budget";
        return {cap, state};
    }

private:
    uint32_t configuredReserve{};
    std::optional<Clock::time_point> lastFrame;
    Clock::time_point windowStart{}, nextDecision{}, lastDecisionSample{};
    size_t frames{};
    struct SourceRate { Clock::time_point at{}; double fps{}; };
    std::array<SourceRate, 8> sourceRates{};
    size_t sourceRateIndex{};
    double cap{}, ceiling{};
    std::string_view state{"measuring"};
};

}
