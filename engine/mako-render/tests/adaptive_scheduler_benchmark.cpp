/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "adaptive_scheduler.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string_view>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {
    struct BenchmarkCase {
        std::string_view name;
        double baseFps;
        uint32_t targetFps;
        size_t maximumMultiplier;
        bool stableCadence;
        double intervalJitter;
    };

    struct BenchmarkResult {
        double nanosecondsPerDecision;
        double averageGeneratedFrames;
        uint64_t checksum;
    };

    BenchmarkResult runCase(const BenchmarkCase& benchmark,
            const size_t iterations) {
        AdaptiveScheduler scheduler({
            .targetFps = benchmark.targetFps,
            .maximumMultiplier = benchmark.maximumMultiplier,
            .generatedFrameCapacity = 3,
            .stableCadence = benchmark.stableCadence,
            .nearTargetNativePreference = true,
        });

        AdaptiveScheduler::TimePoint simulatedNow{};
        scheduler.beginStabilization(simulatedNow, "benchmark");
        for (size_t frame = 0;
                frame < AdaptiveScheduler::historyWarmupFrameCount(); ++frame) {
            simulatedNow += 16ms;
            scheduler.consumeHistoryWarmupFrame(simulatedNow);
        }

        const auto interval =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(1.0 / benchmark.baseFps)
            );
        const size_t settlingFrames = static_cast<size_t>(
            benchmark.baseFps * 15.0
        );
        for (size_t frame = 0; frame < settlingFrames; ++frame) {
            simulatedNow += interval;
            static_cast<void>(scheduler.planFrame(simulatedNow, false));
        }

        uint64_t checksum = 0;
        uint64_t generatedFrameTotal = 0;
        const std::array noisyIntervals{
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                interval * (1.0 - benchmark.intervalJitter)
            ),
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                interval * (1.0 + benchmark.intervalJitter)
            ),
        };
        const auto started = std::chrono::steady_clock::now();
        if (benchmark.intervalJitter == 0.0) {
            for (size_t frame = 0; frame < iterations; ++frame) {
                simulatedNow += interval;
                const auto plan = scheduler.planFrame(simulatedNow, false);
                generatedFrameTotal += plan.size();
                if (!plan.empty()) {
                    checksum += static_cast<uint64_t>(
                        plan.front() * 1'000'000.0F
                    );
                }
            }
        } else {
            for (size_t frame = 0; frame < iterations; ++frame) {
                simulatedNow += noisyIntervals[frame & 1U];
                const auto plan = scheduler.planFrame(simulatedNow, false);
                generatedFrameTotal += plan.size();
                if (!plan.empty()) {
                    checksum += static_cast<uint64_t>(
                        plan.front() * 1'000'000.0F
                    );
                }
            }
        }
        const auto elapsed = std::chrono::steady_clock::now() - started;

        return {
            .nanosecondsPerDecision =
                std::chrono::duration<double, std::nano>(elapsed).count() /
                    static_cast<double>(iterations),
            .averageGeneratedFrames =
                static_cast<double>(generatedFrameTotal) /
                    static_cast<double>(iterations),
            .checksum = checksum,
        };
    }
}

int main() {
    constexpr size_t iterations = 2'000'000;
    constexpr std::array<BenchmarkCase, 10> cases{{
        {"above-target-real-only", 144.0, 120, 4, false, 0.0},
        {"strict-2x", 60.0, 120, 3, false, 0.0},
        {"strict-4x", 30.0, 120, 4, false, 0.0},
        {"fractional-noisy-placement", 60.0, 90, 2, false, 0.22},
        {"smooth-fractional", 47.0, 90, 2, true, 0.0},
        {"fractional-below-native-boundary", 89.0, 120, 2, false, 0.0},
        {"near-target-native-90", 90.0, 120, 2, false, 0.0},
        {"near-target-native-100", 100.0, 120, 2, false, 0.0},
        {"near-target-native-110", 110.0, 120, 2, false, 0.0},
        {"near-target-native-noisy", 100.0, 120, 2, false, 0.10},
    }};

    std::cout << "case,base_fps,target_fps,max_multiplier,smooth_cadence,"
                 "average_generated,ns_per_decision,checksum\n";
    for (const auto& benchmark : cases) {
        const auto result = runCase(benchmark, iterations);
        std::cout << benchmark.name << ','
                  << std::fixed << std::setprecision(2)
                  << benchmark.baseFps << ','
                  << benchmark.targetFps << ','
                  << benchmark.maximumMultiplier << ','
                  << (benchmark.stableCadence ? "on" : "off") << ','
                  << result.averageGeneratedFrames << ','
                  << result.nanosecondsPerDecision << ','
                  << result.checksum << '\n';
    }
}
