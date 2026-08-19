/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace mako::quality {

    /// Full-resolution optical-flow preset used by the quality regression.
    struct QualityPreset {
        float flowScale;
        uint32_t multiplier;
        bool performanceMode;
    };

    inline constexpr QualityPreset flowScaleOnePreset{
        .flowScale = 1.0F,
        .multiplier = 2,
        .performanceMode = false,
    };

    /// Synthetic motion/disocclusion workload stored as RGBA8 images.
    struct RegressionScene {
        uint32_t width;
        uint32_t height;
        std::vector<uint8_t> previous;
        std::vector<uint8_t> current;
        std::vector<uint8_t> reference;
        std::vector<uint8_t> focusMask;
        std::vector<uint8_t> detailMask;
    };

    struct ImageQualityMetrics {
        double meanAbsoluteError;
        double focusMeanAbsoluteError;
        double severeFocusErrorFraction;
        double detailMeanAbsoluteError;
    };

    struct ImageQualityThresholds {
        double maximumMeanAbsoluteError{0.18};
        double maximumFocusMeanAbsoluteError{0.075};
        double maximumSevereFocusErrorFraction{0.20};
        double maximumDetailMeanAbsoluteError{0.34};
    };

    /// Build a deterministic odd-sized scene aimed at AMD image-boundary and
    /// motion-history regressions.
    [[nodiscard]] RegressionScene makeAmdImageQualityRegressionScene();

    /// Compare a generated midpoint frame with the scene's ideal midpoint.
    [[nodiscard]] ImageQualityMetrics evaluateImageQuality(
        const RegressionScene& scene, std::span<const uint8_t> generated
    );

    /// Apply deliberately broad corruption/ghosting guardrails. Metrics remain
    /// available for tighter device-specific baselines.
    [[nodiscard]] bool passesImageQualityRegression(
        const ImageQualityMetrics& metrics,
        const ImageQualityThresholds& thresholds = {}
    );
}
