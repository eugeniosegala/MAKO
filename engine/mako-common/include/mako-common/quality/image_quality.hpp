/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
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

    enum class QualitySceneKind : uint8_t {
        MotionBoundary,
        Traffic,
        Crowd,
        CameraPan,
        HudDisocclusion,
    };

    struct QualitySceneDescriptor {
        QualitySceneKind kind;
        std::string_view name;
        std::string_view description;
    };

    [[nodiscard]] std::span<const QualitySceneDescriptor> qualitySceneCatalog();
    [[nodiscard]] std::optional<QualitySceneKind> qualitySceneFromName(
        std::string_view name
    );
    [[nodiscard]] std::string_view qualitySceneName(QualitySceneKind kind);

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

    struct SpatialRegressionScene {
        uint32_t sourceWidth;
        uint32_t sourceHeight;
        uint32_t presentationWidth;
        uint32_t presentationHeight;
        std::vector<uint8_t> source;
        std::vector<uint8_t> reference;
        std::vector<uint8_t> focusMask;
        std::vector<uint8_t> detailMask;
    };

    struct CombinedRegressionScene {
        uint32_t sourceWidth;
        uint32_t sourceHeight;
        uint32_t presentationWidth;
        uint32_t presentationHeight;
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

    /// Scene-aware LSFG guardrails. The legacy motion-boundary scene retains
    /// its original threshold; workloads with much larger disocclusions use
    /// calibrated focus limits without weakening whole-frame corruption rules.
    [[nodiscard]] ImageQualityThresholds imageQualityThresholds(
        QualitySceneKind kind
    );

    inline constexpr ImageQualityThresholds spatialQualityThresholds{
        .maximumMeanAbsoluteError = 0.08,
        .maximumFocusMeanAbsoluteError = 0.12,
        .maximumSevereFocusErrorFraction = 0.20,
        .maximumDetailMeanAbsoluteError = 0.15,
    };

    /// Build one deterministic temporal interpolation scene. The endpoints
    /// are time 0 and 1; interpolation must be strictly between them.
    [[nodiscard]] RegressionScene makeImageQualityRegressionScene(
        QualitySceneKind kind, float interpolation = 0.5F
    );

    /// Build a deterministic odd-sized scene aimed at AMD image-boundary and
    /// motion-history regressions.
    [[nodiscard]] RegressionScene makeAmdImageQualityRegressionScene();

    /// Build low-resolution source and vector-rendered native-resolution
    /// reference images for the actual MAKO or LS1 spatial pipeline.
    [[nodiscard]] SpatialRegressionScene makeSpatialQualityRegressionScene(
        QualitySceneKind kind, float scalingFactor, float time = 0.5F
    );

    /// Build low-resolution temporal endpoints and a vector-rendered
    /// native-resolution interpolation reference for the production
    /// spatial-scaling-to-LSFG handoff.
    [[nodiscard]] CombinedRegressionScene makeCombinedQualityRegressionScene(
        QualitySceneKind kind, float scalingFactor,
        float interpolation = 0.5F
    );

    /// Compare a generated midpoint frame with the scene's ideal midpoint.
    [[nodiscard]] ImageQualityMetrics evaluateImageQuality(
        const RegressionScene& scene, std::span<const uint8_t> generated
    );

    [[nodiscard]] ImageQualityMetrics evaluateImageQuality(
        const SpatialRegressionScene& scene,
        std::span<const uint8_t> generated
    );

    [[nodiscard]] ImageQualityMetrics evaluateImageQuality(
        const CombinedRegressionScene& scene,
        std::span<const uint8_t> generated
    );

    /// Apply deliberately broad corruption/ghosting guardrails. Metrics remain
    /// available for tighter device-specific baselines.
    [[nodiscard]] bool passesImageQualityRegression(
        const ImageQualityMetrics& metrics,
        const ImageQualityThresholds& thresholds = {}
    );
}
