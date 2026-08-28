/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "adaptive_scheduler.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>

using namespace mako::layer;
using namespace std::chrono_literals;

namespace {
    struct MatrixResult {
        double averageGeneratedFrames{0.0};
        double estimatedOutputFps{0.0};
        size_t finalValidatedLimit{0};
        size_t generatedCountChanges{0};
        bool nearTargetNativePreference{false};
        bool valid{true};
    };

    MatrixResult runCase(const double baseFps, const uint32_t targetFps,
            const size_t maximumMultiplier, const bool stableCadence) {
        AdaptiveScheduler scheduler({
            .targetFps = targetFps,
            .maximumMultiplier = maximumMultiplier,
            .generatedFrameCapacity = GeneratedFramePlan::capacity,
            .stableCadence = stableCadence,
            .nearTargetNativePreference = true,
        });

        AdaptiveScheduler::TimePoint now{};
        scheduler.beginStabilization(now, "startup");
        for (size_t i = 0;
                i < AdaptiveScheduler::historyWarmupFrameCount(); ++i) {
            now += 16ms;
            scheduler.consumeHistoryWarmupFrame(now);
        }

        const auto interval = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(1.0 / baseFps)
        );
        const size_t totalFrames = static_cast<size_t>(std::ceil(baseFps * 15.0));
        const size_t sampleFrames = static_cast<size_t>(std::ceil(baseFps * 5.0));
        const size_t sampleStart = totalFrames - sampleFrames;

        double generatedFrames = 0.0;
        size_t generatedCountChanges = 0;
        size_t previousGeneratedCount = 0;
        bool havePreviousCount = false;
        bool valid = true;

        for (size_t frame = 0; frame < totalFrames; ++frame) {
            now += interval;
            const auto timestamps = scheduler.planFrame(now, false);
            valid = valid && timestamps.size() <= maximumMultiplier - 1;
            float previousTimestamp = 0.0F;
            for (const float timestamp : timestamps) {
                valid = valid && timestamp > previousTimestamp &&
                    timestamp < 1.0F;
                previousTimestamp = timestamp;
            }

            if (frame >= sampleStart) {
                generatedFrames += static_cast<double>(timestamps.size());
                if (havePreviousCount &&
                        timestamps.size() != previousGeneratedCount) {
                    generatedCountChanges++;
                }
                previousGeneratedCount = timestamps.size();
                havePreviousCount = true;
            }
        }

        const double averageGeneratedFrames = generatedFrames /
            static_cast<double>(sampleFrames);
        const auto snapshot = scheduler.snapshot();
        return {
            .averageGeneratedFrames = averageGeneratedFrames,
            .estimatedOutputFps = baseFps * (1.0 + averageGeneratedFrames),
            .finalValidatedLimit = snapshot.validatedGenerationLimit,
            .generatedCountChanges = generatedCountChanges,
            .nearTargetNativePreference =
                snapshot.nearTargetNativePreference,
            .valid = valid,
        };
    }
}

int main() {
    constexpr std::array<double, 12> baseRates{
        24.0, 30.0, 45.0, 47.0, 60.0, 80.0, 89.0,
        90.0, 96.0, 100.0, 110.0, 114.0,
    };
    constexpr std::array<uint32_t, 4> targets{60, 90, 100, 120};
    constexpr std::array<size_t, 4> maximumMultipliers{2, 3, 4, 5};
    constexpr std::array<bool, 2> stableCadenceOptions{false, true};

    std::cout << "base_fps,target_fps,max_multiplier,smooth_cadence,"
                 "average_generated,generated_share,estimated_output_fps,"
                 "validated_generated_limit,count_changes,"
                 "near_target_native,status\n";

    size_t failures = 0;
    size_t cases = 0;
    for (const double baseFps : baseRates) {
        for (const uint32_t targetFps : targets) {
            for (const size_t maximumMultiplier : maximumMultipliers) {
                for (const bool stableCadence : stableCadenceOptions) {
                    const auto result = runCase(
                        baseFps, targetFps, maximumMultiplier, stableCadence
                    );
                    bool valid = result.valid &&
                        result.finalValidatedLimit <= maximumMultiplier - 1;
                    if (baseFps >= static_cast<double>(targetFps)) {
                        valid = valid && result.averageGeneratedFrames == 0.0;
                    } else if (result.nearTargetNativePreference) {
                        valid = valid && result.averageGeneratedFrames == 0.0 &&
                            result.finalValidatedLimit == 0;
                    } else {
                        const double achievableOutputFps = std::min(
                            static_cast<double>(targetFps),
                            baseFps * static_cast<double>(maximumMultiplier)
                        );
                        valid = valid &&
                            result.estimatedOutputFps >=
                                achievableOutputFps * 0.95;
                        const double maximumUsefulOutputFps = stableCadence
                            ? std::min(
                                baseFps * static_cast<double>(maximumMultiplier),
                                static_cast<double>(targetFps) / 0.95
                            )
                            : achievableOutputFps + 1.0;
                        valid = valid &&
                            result.estimatedOutputFps <=
                                maximumUsefulOutputFps + 0.01;
                    }
                    if (!valid)
                        failures++;
                    cases++;

                    const double generatedShare =
                        result.averageGeneratedFrames /
                            (1.0 + result.averageGeneratedFrames);

                    std::cout << std::fixed << std::setprecision(2)
                              << baseFps << ',' << targetFps << ','
                              << maximumMultiplier << ','
                              << (stableCadence ? "on" : "off") << ','
                              << result.averageGeneratedFrames << ','
                              << generatedShare << ','
                              << result.estimatedOutputFps << ','
                              << result.finalValidatedLimit << ','
                              << result.generatedCountChanges << ','
                              << (result.nearTargetNativePreference ? 1 : 0)
                              << ','
                              << (valid ? "pass" : "fail") << '\n';
                }
            }
        }
    }

    std::cerr << cases << " deterministic policy-matrix cases, "
              << failures << " invariant failure(s)\n";
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
