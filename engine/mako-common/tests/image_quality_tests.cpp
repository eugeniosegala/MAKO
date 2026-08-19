/* SPDX-License-Identifier: GPL-3.0-or-later */

#include "mako-common/quality/image_quality.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
    void expect(const bool condition, const std::string_view message) {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main() {
    const auto scene = mako::quality::makeAmdImageQualityRegressionScene();
    const auto repeated = mako::quality::makeAmdImageQualityRegressionScene();
    expect(scene.width == 321 && scene.height == 181,
        "the regression must keep odd, non-workgroup-aligned dimensions");
    expect(scene.previous == repeated.previous && scene.current == repeated.current &&
            scene.reference == repeated.reference,
        "the synthetic regression scene must be deterministic");
    expect(std::ranges::count(scene.focusMask, uint8_t{1}) > 1000,
        "the scene must include a meaningful motion/disocclusion focus region");
    expect(std::ranges::count(scene.detailMask, uint8_t{1}) > 100,
        "the scene must include thin-detail coverage");

    const auto perfect = mako::quality::evaluateImageQuality(scene, scene.reference);
    expect(mako::quality::passesImageQualityRegression(perfect),
        "the reference midpoint must pass its own quality guardrails");
    expect(perfect.meanAbsoluteError == 0.0 &&
            perfect.focusMeanAbsoluteError == 0.0 &&
            perfect.detailMeanAbsoluteError == 0.0,
        "the reference midpoint must score zero error");

    const auto duplicatedEndpoint = mako::quality::evaluateImageQuality(
        scene, scene.current
    );
    expect(!mako::quality::passesImageQualityRegression(duplicatedEndpoint),
        "a duplicated endpoint must fail the midpoint ghosting guardrails");

    auto blendedEndpoints = scene.previous;
    for (size_t byte = 0; byte < blendedEndpoints.size(); ++byte) {
        blendedEndpoints[byte] = static_cast<uint8_t>(
            (static_cast<uint16_t>(scene.previous[byte]) + scene.current[byte]) / 2
        );
    }
    const auto doubleImage = mako::quality::evaluateImageQuality(
        scene, blendedEndpoints
    );
    expect(!mako::quality::passesImageQualityRegression(doubleImage),
        "a blended double image must fail the midpoint ghosting guardrails");

    auto corrupted = scene.reference;
    for (size_t pixel = 0; pixel < scene.focusMask.size(); ++pixel) {
        if (scene.focusMask[pixel] == 0)
            continue;
        const size_t offset = pixel * 4;
        corrupted[offset] = 255;
        corrupted[offset + 1] = 0;
        corrupted[offset + 2] = 255;
    }
    const auto corruption = mako::quality::evaluateImageQuality(scene, corrupted);
    expect(!mako::quality::passesImageQualityRegression(corruption),
        "large focus-region corruption must fail the regression");

    expect(mako::quality::flowScaleOnePreset.flowScale == 1.0F &&
            mako::quality::flowScaleOnePreset.multiplier == 2 &&
            !mako::quality::flowScaleOnePreset.performanceMode,
        "the quality preset must use full-resolution flow and the quality shader path");
    const float inverseFlow = 1.0F / mako::quality::flowScaleOnePreset.flowScale;
    expect(static_cast<uint32_t>(static_cast<float>(scene.width) / inverseFlow) ==
            scene.width &&
            static_cast<uint32_t>(static_cast<float>(scene.height) / inverseFlow) ==
            scene.height,
        "Flow Scale 1.0 must preserve the exact odd source extent");

    std::cout << "AMD image-quality regression tests passed\n";
    return 0;
}
