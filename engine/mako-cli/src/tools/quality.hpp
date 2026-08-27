/* SPDX-License-Identifier: GPL-3.0-or-later */

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace mako::cli::quality {

    /// options for the "quality-regression" command
    struct Options {
        std::optional<std::string> dll;
        bool allow_fp16{false};
        std::optional<std::string> gpu;
        std::optional<std::filesystem::path> output;
        std::string scene{"motion-boundary"};
        float interpolation{0.5F};
        float flow_scale{1.0F};
        bool performance_mode{false};
    };

    /// run the deterministic GPU image-quality regression
    int run(const Options& opts);

    /// Options for the real MAKO/LS1 spatial reconstruction regression.
    struct SpatialOptions {
        std::optional<std::string> dll;
        std::optional<std::string> gpu;
        std::optional<std::filesystem::path> output;
        std::string scene{"motion-boundary"};
        std::string method{"mako"};
        float scaling_factor{1.5F};
        float sharpness{0.5F};
        float scene_time{0.5F};
    };

    /// Run one procedural scene through the production spatial scaler.
    int runSpatial(const SpatialOptions& opts);

    /// Options for timestamp-query profiling of the production spatial graph.
    struct SpatialProfileOptions {
        std::optional<std::string> dll;
        std::optional<std::string> gpu;
        std::string method{"mako"};
        uint32_t width{1280};
        uint32_t height{800};
        float scaling_factor{1.5F};
        float sharpness{0.5F};
        uint32_t warmup_iterations{12};
        uint32_t samples{50};
        bool frame_generation_handoff{false};
    };

    /// Measure the complete production spatial command graph with Vulkan GPU
    /// timestamps. Instrumentation exists only in this CLI path.
    int runSpatialProfile(const SpatialProfileOptions& opts);

    /// Options for proving that synchronization validation is active.
    struct SynchronizationCanaryOptions {
        std::optional<std::string> gpu;
    };

    /// Record one intentional transfer hazard for validation-tooling checks.
    int runSynchronizationCanary(const SynchronizationCanaryOptions& opts);

    /// Options for the production spatial-scaling-to-LSFG handoff.
    struct CombinedOptions {
        std::optional<std::string> dll;
        bool allow_fp16{false};
        std::optional<std::string> gpu;
        std::optional<std::filesystem::path> output;
        std::string scene{"motion-boundary"};
        std::string method{"mako"};
        float scaling_factor{1.5F};
        float sharpness{0.5F};
        float interpolation{0.5F};
        float flow_scale{1.0F};
        bool performance_mode{false};
    };

    /// Scale both low-resolution endpoints and feed the reconstructed frames
    /// into the real licensed frame-generation backend.
    int runCombined(const CombinedOptions& opts);
}
